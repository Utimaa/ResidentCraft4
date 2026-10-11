/* Adapted from SkyCraft v0.1.0 WorldExporter, copyright (c) 2026 chasmlol, MIT.
 * Minecraft's model/fluid renderers supply the geometry, textures, tint and light. */
package dev.re4craft.client;

import com.mojang.blaze3d.vertex.QuadInstance;
import com.mojang.blaze3d.vertex.VertexConsumer;
import dev.re4craft.protocol.Wire;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Arrays;
import java.util.Comparator;
import java.util.HashSet;
import io.netty.buffer.Unpooled;
import net.minecraft.network.FriendlyByteBuf;
import net.minecraft.world.level.chunk.LevelChunkSection;
import net.minecraft.client.Minecraft;
import net.minecraft.client.model.geom.builders.UVPair;
import net.minecraft.client.renderer.block.BlockQuadOutput;
import net.minecraft.client.renderer.block.FluidRenderer;
import net.minecraft.client.renderer.block.ModelBlockRenderer;
import net.minecraft.client.renderer.chunk.ChunkSectionLayer;
import net.minecraft.client.renderer.texture.TextureAtlas;
import net.minecraft.client.resources.model.geometry.BakedQuad;
import net.minecraft.core.BlockPos;
import net.minecraft.world.level.block.RenderShape;
import net.minecraft.world.level.chunk.status.ChunkStatus;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.block.state.properties.BlockStateProperties;
import net.minecraft.core.Direction;

/** Render-thread export of actual nearby Minecraft blocks, including edits and fluids.
 * Category 2 positions use one stable per-epoch origin, independent of player movement.
 * This capture does not change blocks or fabricate collision/terrain models. */
public final class SceneCapture {
    static final int RADIUS = Math.clamp(Integer.getInteger("re4craft.sceneRadius",128),32,256);
    private static final int HEIGHT = Math.clamp(Integer.getInteger("re4craft.sceneHeight",128),32,256);
    private static final int MAX_VERTICES = 18000;
    private static final int MAX_SCENE_VERTICES=300000,MAX_CACHE_VERTICES=720000;
    private static final int MAX_SECTIONS=Math.clamp(Integer.getInteger("re4craft.sceneSections",1024),128,2048);
    private static final long STATE_BUDGET=500_000L;
    private static final long WORK_BUDGET=2_000_000L;
    private static final boolean PROFILE=Boolean.getBoolean("re4craft.profile");
    private static final long INTERVAL = 100_000_000L;
    private static final int[] TRIANGLES = {0,1,2,0,2,3};
    private final BridgeServer server;
    private final AvatarCapture textures;
    private final Builder builder = new Builder();
    private ModelBlockRenderer blocks;
    private FluidRenderer fluids;
    private Object modelManager;
    private long nextCapture;
    private long nextPublish;
    private int frame, failures, lastBlocks = -1;
    private Object levelIdentity;
    private int cacheEpoch,cachedVertices,workIndex,workVersion,workBlocks;
    private double originX,originY,originZ;
    private boolean changed;
    private SectionEntry work;
    private final Map<SectionKey,SectionEntry> sections=new LinkedHashMap<>();
    private List<SectionEntry> orderedSections=List.of();
    private int stateScanIndex;
    private boolean sectionBounded;
    private final FriendlyByteBuf sectionBytes=new FriendlyByteBuf(Unpooled.buffer(16384));
    private long profileStarted,profileCalls,profileTotal,profileMax,profileDiscover,profileWork,profilePublish,profilePublishes,profileMeshes;
    private int loadedChunks,nonemptySections;
    private final SceneTest sceneTest = new SceneTest();
    private final RangeTest rangeTest = new RangeTest();

    public SceneCapture(BridgeServer server, AvatarCapture textures) {
        this.server=server;this.textures=textures;
    }

    // Render-thread metadata shared with vanilla collision-shape capture.
    // Sources are live loaded sections; revision includes neighboring changes.
    record CachedSection(int x,int y,int z,int revision,LevelChunkSection source,double distance) { }
    List<CachedSection> cachedSections() {
        List<CachedSection> result=new ArrayList<>(sections.size());
        for(var entry:nearest())result.add(new CachedSection(entry.key.x,entry.key.y,entry.key.z,entry.version,entry.source,entry.distance));
        return List.copyOf(result);
    }
    int cachedEpoch() { return cacheEpoch; }
    int minimumCachedY(Minecraft mc) { return minimumSceneY(mc); }

    public void reset() { nextCapture=nextPublish=0;builder.reset();modelManager=null;lastBlocks=-1;sections.clear();orderedSections=List.of();stateScanIndex=0;sectionBounded=false;cachedVertices=0;work=null;levelIdentity=null;changed=true; }

    public void capture(Minecraft mc) {
        if(mc.player==null || mc.level==null || !server.connected() || !Re4CraftClient.hostAnchored())return;
        long profileStart=PROFILE?System.nanoTime():0;
        try {
            sceneTest.frame(mc);
            rangeTest.frame(mc);
            int epoch=Re4CraftClient.hostEpoch();
            if(levelIdentity!=mc.level||cacheEpoch!=epoch){reset();levelIdentity=mc.level;cacheEpoch=epoch;
                originX=Math.floor(mc.player.getX());originY=Math.floor(mc.player.getY());originZ=Math.floor(mc.player.getZ());}
            if(modelManager!=mc.getModelManager()) {
                for(var entry:sections.values()){entry.dirty=true;entry.version++;}work=null;builder.reset();
                modelManager=mc.getModelManager();
                blocks=new ModelBlockRenderer(mc.options.ambientOcclusion().get(),true,mc.getBlockColors());
                fluids=new FluidRenderer(mc.getModelManager().getFluidStateModelSet());
            }
            long now=System.nanoTime();
            if(now>=nextCapture){nextCapture=now+INTERVAL;long start=PROFILE?System.nanoTime():0;discover(mc);if(PROFILE)profileDiscover+=System.nanoTime()-start;}
            scanStates();
            long workStart=PROFILE?System.nanoTime():0;rebuild(mc);if(PROFILE)profileWork+=System.nanoTime()-workStart;
            if(changed && now>=nextPublish){long start=PROFILE?System.nanoTime():0;publish();nextPublish=now+INTERVAL;if(PROFILE){profilePublish+=System.nanoTime()-start;profilePublishes++;}}
            failures=0;
        } catch(RuntimeException error) {
            if(failures++==0)Re4CraftClient.LOG.error("Vanilla nearby block capture failed",error);
        } finally {
            if(PROFILE){long duration=System.nanoTime()-profileStart;profileCalls++;profileTotal+=duration;profileMax=Math.max(profileMax,duration);profile();}
        }
    }
    private void profile(){
        long now=System.nanoTime();if(profileStarted==0)profileStarted=now;if(now-profileStarted<5_000_000_000L)return;
        Re4CraftClient.LOG.info(String.format(java.util.Locale.ROOT,"Scene profile: capture avg/max=%.3f/%.3fms discover/work/publish=%.3f/%.3f/%.3fms calls=%d publishes=%d meshes=%d loadedChunks=%d nonemptySections=%d cachedSections=%d cachedVertices=%d radius=%d",
                profileTotal/1e6/profileCalls,profileMax/1e6,profileDiscover/1e6/profileCalls,profileWork/1e6/profileCalls,profilePublish/1e6/profileCalls,profileCalls,profilePublishes,profileMeshes,loadedChunks,nonemptySections,sections.size(),cachedVertices,RADIUS));
        profileStarted=now;profileCalls=profileTotal=profileMax=profileDiscover=profileWork=profilePublish=profilePublishes=profileMeshes=0;
    }

    private record SectionKey(int x,int y,int z){}
    private static final class SectionEntry {
        final SectionKey key;LevelChunkSection source;byte[] states;int version,vertices,blockCount;boolean dirty=true,truncated;double distance;long retryAt;
        List<Batch> mesh=List.of();
        SectionEntry(SectionKey key,LevelChunkSection source){this.key=key;this.source=source;}
    }
    private static double axisDistance(double point,int minimum,int maximum){return point<minimum?minimum-point:point>maximum?point-maximum:0;}
    private List<SectionEntry> nearest(){return orderedSections;}
    private void discover(Minecraft mc){
        int cx=mc.player.getBlockX(),cy=mc.player.getBlockY(),cz=mc.player.getBlockZ(),minimum=minimumSceneY(mc);
        List<SectionEntry> candidates=new ArrayList<>();
        loadedChunks=0;
        // Inspect loaded section metadata, never an enormous cube of air and
        // never a generating chunk request. Empty mirror sections cost no model work.
        for(int x=(cx-RADIUS)>>4;x<=(cx+RADIUS)>>4;++x)for(int z=(cz-RADIUS)>>4;z<=(cz+RADIUS)>>4;++z){
            var chunk=mc.level.getChunkSource().getChunk(x,z,ChunkStatus.FULL,false);if(chunk==null)continue;loadedChunks++;
            var source=chunk.getSections();
            for(int index=0;index<source.length;++index){int y=chunk.getSectionYFromSectionIndex(index);var section=source[index];
                if(section.hasOnlyAir()||y*16+15<minimum||y*16>cy+HEIGHT||y*16+15<cy-HEIGHT)continue;
                double dx=axisDistance(mc.player.getX(),x*16,x*16+16),dz=axisDistance(mc.player.getZ(),z*16,z*16+16);if(dx*dx+dz*dz>RADIUS*RADIUS)continue;
                var key=new SectionKey(x,y,z);var entry=sections.get(key);if(entry==null)entry=new SectionEntry(key,section);
                if(entry.source!=section){entry.source=section;entry.states=null;entry.dirty=true;entry.version++;}
                entry.distance=dx*dx+dz*dz+Math.pow(axisDistance(mc.player.getY(),y*16,y*16+16),2);candidates.add(entry);
            }
        }
        nonemptySections=candidates.size();candidates.sort(Comparator.comparingDouble(entry->entry.distance));
        boolean bounded=candidates.size()>MAX_SECTIONS;if(bounded!=sectionBounded){sectionBounded=bounded;changed=true;}
        if(bounded)candidates.subList(MAX_SECTIONS,candidates.size()).clear();
        var retained=new HashSet<SectionKey>();for(var entry:candidates)retained.add(entry.key);
        List<SectionKey> modified=new ArrayList<>();
        var iterator=sections.entrySet().iterator();while(iterator.hasNext()){var entry=iterator.next().getValue();if(!retained.contains(entry.key)){cachedVertices-=entry.vertices;iterator.remove();modified.add(entry.key);changed=true;if(work==entry){work=null;builder.reset();}}}
        for(var entry:candidates)sections.put(entry.key,entry);
        orderedSections=List.copyOf(candidates);
        invalidateNeighbors(modified);
        if(work!=null&&work.version!=workVersion){work=null;builder.reset();}
    }
    private void scanStates(){
        if(orderedSections.isEmpty())return;
        List<SectionKey> modified=new ArrayList<>();long deadline=System.nanoTime()+STATE_BUDGET;
        // Poll fairly across the enlarged cache. Never restart at the nearest
        // section every discovery interval and starve distant block updates.
        int checked=0;
        do {if(stateScanIndex>=orderedSections.size())stateScanIndex=0;var entry=orderedSections.get(stateScanIndex++);
            sectionBytes.clear();entry.source.getStates().write(sectionBytes);
            int bytes=sectionBytes.writerIndex();if(bytes>65536)throw new IllegalArgumentException("Unbounded section state encoding");
            if(entry.states==null||entry.states.length!=bytes||!Arrays.equals(entry.states,0,bytes,sectionBytes.array(),sectionBytes.arrayOffset(),sectionBytes.arrayOffset()+bytes)){
                entry.states=Arrays.copyOfRange(sectionBytes.array(),sectionBytes.arrayOffset(),sectionBytes.arrayOffset()+bytes);modified.add(entry.key);}
        }while(++checked<Math.min(32,orderedSections.size())&&System.nanoTime()<deadline);
        invalidateNeighbors(modified);
        if(work!=null&&work.version!=workVersion){work=null;builder.reset();}
    }
    private void invalidateNeighbors(List<SectionKey> modified){
        // Block faces/AO depend on adjacent sections, including corner neighbors.
        for(var key:modified)for(int dx=-1;dx<=1;++dx)for(int dy=-1;dy<=1;++dy)for(int dz=-1;dz<=1;++dz){var entry=sections.get(new SectionKey(key.x+dx,key.y+dy,key.z+dz));if(entry!=null){entry.dirty=true;entry.version++;entry.retryAt=0;}}
    }
    private void rebuild(Minecraft mc){
        long deadline=System.nanoTime()+WORK_BUDGET;int completed=0;
        do {
        if(work==null){for(var entry:nearest())if(entry.dirty&&System.nanoTime()>=entry.retryAt){
            double farthest=-1;for(var cached:sections.values())if(cached.vertices>0)farthest=Math.max(farthest,cached.distance);
            if(entry.vertices==0&&cachedVertices>=MAX_CACHE_VERTICES-MAX_VERTICES&&entry.distance>=farthest)continue;
            work=entry;workVersion=entry.version;workIndex=workBlocks=0;builder.reset();break;}}
        if(work==null)return;
        int checked=0,minimum=minimumSceneY(mc);var pos=new BlockPos.MutableBlockPos();
        while(workIndex<4096){int index=workIndex++,x=work.key.x*16+(index&15),z=work.key.z*16+((index>>4)&15),y=work.key.y*16+(index>>8);++checked;
            if(y>=minimum){var state=work.source.getBlockState(x&15,y&15,z&15);if(!state.isAir()){
                pos.set(x,y,z);if(!state.getFluidState().isEmpty()){builder.fluidX=(float)(work.key.x*16-originX);builder.fluidY=(float)(work.key.y*16-originY);builder.fluidZ=(float)(work.key.z*16-originZ);fluids.tesselate(mc.level,pos,builder,state,state.getFluidState());}
                if(state.getRenderShape()==RenderShape.MODEL){var model=mc.getModelManager().getBlockStateModelSet().get(state);blocks.tesselateBlock(builder,(float)(x-originX),(float)(y-originY),(float)(z-originZ),mc.level,pos.immutable(),state,model,state.getSeed(pos));}++workBlocks;
            }}
            if(builder.missingTexture){work.retryAt=System.nanoTime()+INTERVAL;work=null;builder.reset();return;}
            if((checked&15)==0&&workIndex<4096&&System.nanoTime()>=deadline)return;
        }
        if(builder.missingTexture){work.retryAt=System.nanoTime()+INTERVAL;work=null;builder.reset();return;}
        cachedVertices-=work.vertices;work.mesh=new ArrayList<>(builder.batches.values());work.vertices=builder.count;work.blockCount=workBlocks;work.truncated=builder.truncated;work.dirty=false;cachedVertices+=work.vertices;changed=true;
        if(PROFILE)profileMeshes++;
        while(cachedVertices>MAX_CACHE_VERTICES){SectionEntry farthest=null;for(var entry:sections.values())if(entry!=work&&entry.vertices>0&&(farthest==null||entry.distance>farthest.distance))farthest=entry;
            if(farthest==null)break;cachedVertices-=farthest.vertices;farthest.vertices=farthest.blockCount=0;farthest.mesh=List.of();farthest.dirty=true;}
        work=null;builder.reset();
        } while(++completed<8&&System.nanoTime()<deadline);
    }
    private void publish(){
        Map<Long,Batch> merged=new LinkedHashMap<>();int count=0,blockCount=0;boolean bounded=sectionBounded;
        for(var entry:nearest()){
            if(count+entry.vertices>MAX_SCENE_VERTICES){bounded=true;continue;}count+=entry.vertices;blockCount+=entry.blockCount;bounded|=entry.truncated;
            for(var batch:entry.mesh){long key=((long)batch.texture<<32)|(batch.flags&0xffffffffL);merged.computeIfAbsent(key,ignored->new Batch(batch.texture,batch.flags)).vertices.addAll(batch.vertices);}
        }
        List<Batch> used=new ArrayList<>();int bytes=40;for(var batch:merged.values())if(!batch.vertices.isEmpty()){used.add(batch);bytes+=12+batch.vertices.size()*36;}
        if(used.size()>256||bytes>Wire.MAX_PAYLOAD)throw new IllegalArgumentException("Unbounded cached scene mesh");
        double[] origin=Re4CraftClient.hostCoordinates(originX,originY,originZ);ByteBuffer payload=Wire.buffer(bytes).putInt(2).putInt(++frame).putInt(used.size()).putInt(cacheEpoch).putDouble(origin[0]).putDouble(origin[1]).putDouble(origin[2]);
        for(var batch:used){payload.putInt(batch.texture).putInt(batch.flags).putInt(batch.vertices.size());for(var vertex:batch.vertices)vertex.write(payload);}server.publishMesh(payload.array());changed=false;
        if(blockCount!=lastBlocks){Re4CraftClient.LOG.info("Minecraft scene export: {} actual blocks, {} vertices, {} batches, {} cached sections of {}, range={} blocks{}",blockCount,count,used.size(),sections.size(),nonemptySections,RADIUS,bounded?" (bounded)":"");lastBlocks=blockCount;}
    }

    private static int minimumSceneY(Minecraft mc) {
        // The explicit development flat world has its original grass at Y=-61.
        // Do not lay generated grass across RE4. Blocks placed at/above Y=-60
        // remain eligible even after the player stands on them. Other worlds
        // export their actual nearby terrain; a void mirror has no strata.
        if(Boolean.getBoolean("re4craft.testWorld") && !Boolean.getBoolean("re4craft.voidMirror") && !Boolean.getBoolean("re4craft.sceneIncludeTerrain"))
            return Integer.getInteger("re4craft.sceneMinY",-60);
        return Integer.getInteger("re4craft.sceneMinY",Integer.MIN_VALUE);
    }

    /** Explicit QA opt-in: exercises real integrated-server edits in a separate world. */
    private static final class RangeTest {
        private static final int[] DISTANCES={32,64,96,120};
        private long started;
        private int stage;
        private BlockPos origin;
        private net.minecraft.world.entity.item.ItemEntity item;
        void frame(Minecraft mc) {
            if(!Boolean.getBoolean("re4craft.rangeTest") || !System.getProperty("re4craft.worldName","").startsWith("RE4Craft_RangeQA_")
                    || !mc.gameDirectory.toPath().getFileName().toString().equals(".qa-void-instance")
                    || mc.player==null || mc.getSingleplayerServer()==null || !Re4CraftClient.guestReady())return;
            if(started==0){started=System.nanoTime();origin=mc.player.blockPosition();}
            long elapsed=System.nanoTime()-started;
            int desired=(int)Math.min(6,elapsed/3_000_000_000L);
            if(stage>desired)return;
            int action=stage++;var uuid=mc.player.getUUID();
            if(action==6){Re4CraftClient.LOG.info("RANGE_QA completed real distant block/shape/return edits");mc.stop();return;}
            mc.getSingleplayerServer().execute(()->{
                var player=mc.getSingleplayerServer().getPlayerList().getPlayer(uuid);if(player==null)return;
                for(int distance:DISTANCES){BlockState state=(action==2)?Blocks.AIR.defaultBlockState()
                        :(action==1 && (distance==64||distance==120))?Blocks.STONE_STAIRS.defaultBlockState().setValue(BlockStateProperties.HORIZONTAL_FACING,Direction.EAST)
                        :Blocks.STONE.defaultBlockState();
                    player.level().setBlock(origin.offset(distance,0,4),state,Block.UPDATE_ALL);}
                if(action==4)player.teleportTo(origin.getX()+96,origin.getY(),origin.getZ());
                if(action==5)player.teleportTo(origin.getX(),origin.getY(),origin.getZ());
                if(action==0){item=new net.minecraft.world.entity.item.ItemEntity(player.level(),origin.getX()+120.5,origin.getY()+3,origin.getZ()+8.5,
                        new net.minecraft.world.item.ItemStack(net.minecraft.world.item.Items.COBBLESTONE,8));player.level().addFreshEntity(item);}
                if(item!=null)Re4CraftClient.LOG.info("RANGE_QA item stage={} x={} y={} z={} onGround={} removed={}",action,item.getX(),item.getY(),item.getZ(),item.onGround(),item.isRemoved());
                Re4CraftClient.LOG.info("RANGE_QA stage={} distances=32,64,96,120 player={} loadedChunks={}",action,player.position(),player.level().getChunkSource().getLoadedChunksCount());
            });
        }
    }

    /** Explicit QA opt-in: exercises real integrated-server edits in a separate world. */
    private static final class SceneTest {
        private static final boolean ENABLED=Boolean.getBoolean("re4craft.sceneTest");
        private long started;
        private int stage;
        private BlockPos position;
        void frame(Minecraft mc) {
            if(!ENABLED || mc.getSingleplayerServer()==null || mc.player.tickCount<60)return;
            String directory=mc.gameDirectory.toPath().toAbsolutePath().normalize().getFileName().toString();
            if(!directory.equals(".qa-instance"))return;
            if(started==0) {started=System.nanoTime();position=mc.player.blockPosition().offset(2,0,0);}
            long elapsed=System.nanoTime()-started;
            if(stage==0 && elapsed>2_000_000_000L) {
                set(mc,Blocks.CRAFTING_TABLE.defaultBlockState(),"place crafting_table");stage=1;
            } else if(stage==1 && elapsed>5_000_000_000L) {
                set(mc,Blocks.STONE_STAIRS.defaultBlockState().setValue(BlockStateProperties.HORIZONTAL_FACING,Direction.EAST),"replace with east-facing stone_stairs");stage=2;
            } else if(stage==2 && elapsed>8_000_000_000L) {
                set(mc,Blocks.AIR.defaultBlockState(),"break block");stage=3;
            }
        }
        private void set(Minecraft mc,BlockState state,String label) {
            var uuid=mc.player.getUUID();BlockPos target=position;
            mc.getSingleplayerServer().execute(()->{
                var player=mc.getSingleplayerServer().getPlayerList().getPlayer(uuid);
                if(player==null)return;
                player.level().setBlock(target,state,Block.UPDATE_ALL);
                Re4CraftClient.LOG.info("Scene QA {} at {}",label,target);
            });
        }
    }

    private record V(float x,float y,float z,float nx,float ny,float nz,float u,float v,int color) {
        void write(ByteBuffer out) {out.putFloat(x).putFloat(y).putFloat(z).putFloat(nx).putFloat(ny).putFloat(nz).putFloat(u).putFloat(v).putInt(color);}
    }
    private static final class Batch {
        final int texture,flags;final List<V> vertices=new ArrayList<>();
        Batch(int texture,int flags) {this.texture=texture;this.flags=flags;}
    }
    private final class Builder implements BlockQuadOutput,FluidRenderer.Output,VertexConsumer {
        final Map<Long,Batch> batches=new LinkedHashMap<>();
        int count,fluidTexture=-1,fluidCount;boolean truncated,fluidTranslucent,missingTexture;
        float fluidX,fluidY,fluidZ;
        final V[] fluidQuad=new V[4];
        void reset() {batches.clear();count=0;truncated=missingTexture=false;fluidCount=0;}
        Batch batch(int texture,boolean translucent) {
            int flags=translucent?2:1;
            long key=((long)texture<<32)|flags;
            return batches.computeIfAbsent(key,ignored->new Batch(texture,flags));
        }
        boolean capacity() {if(missingTexture)return false;if(count+6<=MAX_VERTICES)return true;truncated=true;return false;}
        @Override public void put(float x,float y,float z,BakedQuad quad,QuadInstance instance) {
            if(!capacity())return;
            var material=quad.materialInfo();int texture=textures.textureId(material.sprite().atlasLocation());
            if(texture<0){missingTexture=true;return;}
            Batch output=batch(texture,material.layer().translucent());
            var direction=quad.direction();
            for(int k:TRIANGLES) {
                var position=quad.position(k);long uv=quad.packedUV(k);
                output.vertices.add(new V(position.x()+x,position.y()+y,position.z()+z,direction.getStepX(),direction.getStepY(),direction.getStepZ(),UVPair.unpackU(uv),UVPair.unpackV(uv),instance.getColor(k)));
            }
            count+=6;
        }
        @Override public VertexConsumer getBuilder(ChunkSectionLayer layer) {
            fluidTranslucent=layer.translucent();fluidTexture=missingTexture?-1:textures.textureId(TextureAtlas.LOCATION_BLOCKS);if(fluidTexture<0)missingTexture=true;return this;
        }
        @Override public void addVertex(float x,float y,float z,int color,float u,float v,int overlay,int light,float nx,float ny,float nz) {
            fluidQuad[fluidCount++]=new V(x+fluidX,y+fluidY,z+fluidZ,nx,ny,nz,u,v,color);
            if(fluidCount==4) {
                fluidCount=0;
                if(fluidTexture<0 || !capacity())return;
                Batch output=batch(fluidTexture,fluidTranslucent);
                for(int k:TRIANGLES)output.vertices.add(fluidQuad[k]);
                count+=6;
            }
        }
        @Override public VertexConsumer addVertex(float x,float y,float z) {throw new UnsupportedOperationException("Fluid renderer must submit complete vertices");}
        @Override public VertexConsumer setColor(int r,int g,int b,int a) {return this;}
        @Override public VertexConsumer setColor(int color) {return this;}
        @Override public VertexConsumer setUv(float u,float v) {return this;}
        @Override public VertexConsumer setUv1(int u,int v) {return this;}
        @Override public VertexConsumer setUv2(int u,int v) {return this;}
        @Override public VertexConsumer setUv3(float u,float v) {return this;}
        @Override public VertexConsumer setNormal(float x,float y,float z) {return this;}
        @Override public VertexConsumer setLineWidth(float width) {return this;}
    }
}
