package dev.re4craft.client;

import com.mojang.blaze3d.vertex.VertexConsumer;
import java.util.ArrayList;
import java.util.List;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.state.level.QuadParticleRenderState;
import net.minecraft.world.phys.Vec3;

/** Capture vanilla's current sprites, pose, UVs and colors, without simulating effects. */
final class ParticleCapture {
    private static final int MAX_QUADS=12000;
    private static int lastCount=-1;
    private static long nextLog;
    record Vertex(float x,float y,float z,float u,float v,int color) { }
    record Batch(int texture,int flags,List<Vertex> vertices) { }
    static List<Batch> capture(Minecraft mc,AvatarCapture textures,Vec3 origin) {
        List<Batch> result=new ArrayList<>();
        Vec3 offset=mc.gameRenderer.mainCamera().position().subtract(origin);
        int quads=0;
        for(var group:mc.gameRenderer.gameRenderState().levelRenderState.particlesRenderState.particles) {
            if(!(group instanceof QuadParticleRenderState particles))continue;
            for(var layer:particles.layers()) {
                int texture=textures.textureId(layer.textureAtlasLocation());if(texture<0)continue;
                Collector collector=new Collector(offset,MAX_QUADS-quads);
                particles.buildLayer(layer,collector);collector.flush();
                if(collector.vertices.isEmpty())continue;
                quads+=collector.vertices.size()/4;
                result.add(new Batch(texture,8|(layer.translucent()?2:0),List.copyOf(collector.vertices)));
                if(quads>=MAX_QUADS)break;
            }
            if(quads>=MAX_QUADS)break;
        }
        long now=System.nanoTime();
        if(quads!=lastCount && now>=nextLog) {
            Re4CraftClient.LOG.info("Minecraft particle export: {} actual vanilla quads, {} layers, limit={}",quads,result.size(),MAX_QUADS);
            lastCount=quads;nextLog=now+1_000_000_000L;
        }
        return result;
    }
    private static final class Collector implements VertexConsumer {
        private final Vec3 offset;
        private final int limit;
        private final List<Vertex> vertices=new ArrayList<>();
        private final Vertex[] quad=new Vertex[4];
        private float x,y,z,u,v;private int color=-1,count;private boolean pending;
        Collector(Vec3 offset,int limit) {this.offset=offset;this.limit=limit;}
        void flush() {
            if(!pending)return;pending=false;
            quad[count++]=new Vertex(x,y,z,u,v,color);
            if(count<4)return;count=0;
            if(vertices.size()/4>=limit)return;
            double cx=0,cy=0,cz=0;
            for(Vertex p:quad) {
                if(!Float.isFinite(p.x)||!Float.isFinite(p.y)||!Float.isFinite(p.z)||!Float.isFinite(p.u)||!Float.isFinite(p.v))return;
                cx+=p.x;cy+=p.y;cz+=p.z;
            }
            if(cx*cx+cy*cy+cz*cz>16*64*64)return;
            java.util.Collections.addAll(vertices,quad);
        }
        @Override public VertexConsumer addVertex(float x,float y,float z) {
            flush();this.x=(float)(x+offset.x);this.y=(float)(y+offset.y);this.z=(float)(z+offset.z);u=v=0;color=-1;pending=true;return this;
        }
        @Override public VertexConsumer setColor(int r,int g,int b,int a) {color=(a<<24)|(r<<16)|(g<<8)|b;return this;}
        @Override public VertexConsumer setColor(int argb) {color=argb;return this;}
        @Override public VertexConsumer setUv(float u,float v) {this.u=u;this.v=v;return this;}
        @Override public VertexConsumer setUv1(int u,int v) {return this;}
        @Override public VertexConsumer setUv2(int u,int v) {return this;}
        @Override public VertexConsumer setUv3(float u,float v) {return this;}
        @Override public VertexConsumer setNormal(float x,float y,float z) {return this;}
        @Override public VertexConsumer setLineWidth(float width) {return this;}
    }
}
