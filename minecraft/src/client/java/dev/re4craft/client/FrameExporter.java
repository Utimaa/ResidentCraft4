/* Adapted from SkyCraft v0.1.0 FrameExporter, copyright (c) 2026 chasmlol, MIT. */
package dev.re4craft.client;

import com.mojang.blaze3d.pipeline.RenderTarget;
import com.mojang.blaze3d.systems.RenderSystem;
import com.mojang.renderpearl.api.buffers.GpuBuffer;
import com.mojang.renderpearl.api.buffers.GpuBufferSlice;
import dev.re4craft.protocol.Wire;
import dev.re4craft.perf.SampleWindow;
import java.nio.ByteBuffer;
import java.util.concurrent.atomic.AtomicLong;
import net.minecraft.client.Minecraft;

/** Three asynchronous GPU buffers and three reusable CPU buffers bound memory.
 * The render thread only maps completed readback and copies pixels. A daemon
 * encodes the newest pending frame; pixels never change while it owns a buffer.
 * Source RGBA/premultiplied alpha and bottom-up rows remain lossless. */
public final class FrameExporter {
    private static final int FREE=0,PENDING=1,READY=2,ENCODING=3,COPYING=4;
    private final Slot[] slots=new Slot[3];
    private final CpuSlot[] cpu=new CpuSlot[]{new CpuSlot(),new CpuSlot(),new CpuSlot()};
    private final Object work=new Object();
    private final BridgeServer server;
    private CpuSlot pending;
    private int frame;
    private volatile int epoch;
    private boolean logged;
    private final boolean profile=Boolean.getBoolean("re4craft.profile");
    private final SampleWindow queueSamples=new SampleWindow(),copySamples=new SampleWindow(),encodeSamples=new SampleWindow(),intervalSamples=new SampleWindow(),ageSamples=new SampleWindow();
    private long lastCapture;
    private long profileTime=System.nanoTime(),copied,copyNanos,queued,queueNanos,replaced;
    private final AtomicLong encoded=new AtomicLong(),encodeNanos=new AtomicLong(),encodeMax=new AtomicLong(),published=new AtomicLong();
    private static final class Slot {GpuBuffer buffer;int width,height,frame,epoch;long captured;volatile int state=FREE;}
    private static final class CpuSlot {byte[] rgba;int width,height,frame,epoch,generation,state=FREE;long captured;}
    public FrameExporter(BridgeServer server){
        this.server=server;
        Thread.ofPlatform().daemon().name("RE4Craft HUD encoder").start(this::encode);
    }
    public void reset(){
        logged=false;
        synchronized(work){epoch++;if(pending!=null){pending.state=FREE;pending=null;}}
    }
    public void capture(Minecraft mc){
        shipReady();report();
        if(!server.connected())return;
        RenderTarget target=mc.gameRenderer.mainRenderTarget();var color=target.getColorTexture();
        int width=target.width,height=target.height;long bytes=(long)width*height*4;
        if(color==null||width<1||height<1||width>1920||height>1080||bytes+16>Wire.MAX_PAYLOAD)return;
        Slot free=null;
        for(int i=0;i<slots.length;i++){if(slots[i]==null)slots[i]=new Slot();if(slots[i].state==FREE){free=slots[i];break;}}
        if(free==null)return;
        long start=profile?System.nanoTime():0;
        if(profile){if(lastCapture!=0)intervalSamples.add(start-lastCapture);lastCapture=start;}
        if(free.buffer==null||free.width!=width||free.height!=height){
            if(free.buffer!=null)free.buffer.close();
            free.buffer=RenderSystem.getDevice().createBuffer(()->"RE4Craft transparent GUI readback",9,bytes);free.width=width;free.height=height;
        }
        final Slot slot=free;slot.frame=++frame;slot.epoch=epoch;slot.captured=start;slot.state=PENDING;
        RenderSystem.getDevice().createCommandEncoder().copyTextureToBuffer(color,slot.buffer,0L,()->slot.state=READY,0);
        if(profile){long elapsed=System.nanoTime()-start;queued++;queueNanos+=elapsed;queueSamples.add(elapsed);}
        if(!logged){logged=true;Re4CraftClient.LOG.info("Minecraft transparent hand/HUD/GUI capture {}x{} format {}",width,height,color.getFormat());}
    }
    private void shipReady(){
        Slot newest=null;
        for(Slot slot:slots)if(slot!=null&&slot.state==READY){if(slot.epoch!=epoch)slot.state=FREE;else if(newest==null||slot.frame>newest.frame)newest=slot;}
        if(newest==null)return;
        CpuSlot destination=null;
        synchronized(work){for(CpuSlot slot:cpu)if(slot.state==FREE){destination=slot;slot.state=COPYING;break;}}
        if(destination==null)return;
        long start=profile?System.nanoTime():0;
        int bytes=Math.multiplyExact(Math.multiplyExact(newest.width,newest.height),4);
        boolean complete=false;
        try{
            if(destination.rgba==null||destination.rgba.length!=bytes)destination.rgba=new byte[bytes];
            try(GpuBufferSlice.MappedView mapped=newest.buffer.map(true,false)){
                ByteBuffer pixels=mapped.data().duplicate();pixels.position(0).limit(bytes);pixels.get(destination.rgba,0,bytes);
            }
            destination.width=newest.width;destination.height=newest.height;destination.frame=newest.frame;destination.epoch=newest.epoch;destination.captured=newest.captured;destination.generation=server.generation();complete=true;
        }finally{
            synchronized(work){
                if(complete&&destination.epoch==epoch&&server.connected()){
                    if(pending!=null){pending.state=FREE;replaced++;}
                    destination.state=PENDING;pending=destination;work.notifyAll();
                }else destination.state=FREE;
            }
            for(Slot slot:slots)if(slot!=null&&slot.state==READY&&slot.frame<=newest.frame)slot.state=FREE;
            if(profile){long elapsed=System.nanoTime()-start;copied++;copyNanos+=elapsed;copySamples.add(elapsed);}
        }
    }
    private void encode(){
        int failures=0;
        while(true){
            CpuSlot slot;
            synchronized(work){
                while(pending==null)try{work.wait();}catch(InterruptedException interrupted){return;}
                slot=pending;pending=null;slot.state=ENCODING;
            }
            long start=profile?System.nanoTime():0;
            try{
                Wire.ImageEncoding image=Wire.encodeImage(ByteBuffer.wrap(slot.rgba));
                // Reset/session changes discard in-flight data, including old
                // resolution/frame dimensions, before publishing a new HUD.
                if(slot.epoch==epoch&&slot.generation==server.generation()&&server.connected()){
                    ByteBuffer packet=Wire.buffer(16+image.pixels().length).putInt(slot.width).putInt(slot.height).putInt(1|image.flags()).putInt(slot.frame).put(image.pixels());
                    synchronized(work){if(slot.epoch==epoch&&slot.generation==server.generation()&&server.connected()){
                        server.publishHud(packet.array());if(profile)published.incrementAndGet();
                    }}
                }
                failures=0;
            }catch(RuntimeException error){if(failures++==0)Re4CraftClient.LOG.error("Lossless HUD encoder failed",error);}
            finally{
                if(profile){long ended=System.nanoTime(),elapsed=ended-start;encoded.incrementAndGet();encodeNanos.addAndGet(elapsed);encodeMax.accumulateAndGet(elapsed,Math::max);encodeSamples.add(elapsed);ageSamples.add(ended-slot.captured);}
                synchronized(work){slot.state=FREE;}
            }
        }
    }
    private void report(){
        if(!profile)return;long now=System.nanoTime();if(now-profileTime<5_000_000_000L)return;
        long count=encoded.getAndSet(0),nanos=encodeNanos.getAndSet(0),maximum=encodeMax.getAndSet(0);
        Re4CraftClient.LOG.info("HUD profile: queued={} GPUqueue={}ms copied={} mapCopy={}ms encoded={} encode={}ms encodeMax={}ms sent={} replaced={}",
                queued,queued==0?0:queueNanos/queued/1e6,copied,copied==0?0:copyNanos/copied/1e6,count,count==0?0:nanos/count/1e6,maximum/1e6,published.getAndSet(0),replaced);
        Re4CraftClient.LOG.info("HUD tails: GPUqueue=[{}] mapCopy=[{}] encode=[{}] interval=[{}] captureToEncoded=[{}]",queueSamples.take(),copySamples.take(),encodeSamples.take(),intervalSamples.take(),ageSamples.take());
        profileTime=now;copied=copyNanos=queued=queueNanos=replaced=0;
    }
}
