/* Adapted from SkyCraft v0.1.0 AvatarExporter, copyright (c) 2026 chasmlol, MIT.
 * The vanilla renderer poses every part; no replacement Steve geometry is authored here. */
package dev.re4craft.client;

import com.mojang.blaze3d.platform.NativeImage;
import com.mojang.blaze3d.vertex.PoseStack;
import com.mojang.blaze3d.vertex.VertexConsumer;
import com.mojang.blaze3d.vertex.QuadInstance;
import com.mojang.blaze3d.vertex.SheetedDecalTextureGenerator;
import dev.re4craft.client.mixin.*;
import dev.re4craft.protocol.Wire;
import dev.re4craft.perf.SampleWindow;
import dev.re4craft.render.TexturePixels;
import java.nio.ByteBuffer;
import java.util.*;
import net.fabricmc.fabric.api.client.renderer.v1.mesh.MeshView;
import net.fabricmc.fabric.api.client.renderer.v1.mesh.QuadAtlas;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.Font;
import net.minecraft.client.model.Model;
import net.minecraft.client.model.geom.builders.UVPair;
import net.minecraft.client.renderer.*;
import net.minecraft.client.renderer.block.MovingBlockRenderState;
import net.minecraft.client.renderer.block.dispatch.BlockStateModelPart;
import net.minecraft.client.renderer.chunk.ChunkSectionLayer;
import net.minecraft.client.renderer.entity.state.EntityRenderState;
import net.minecraft.client.renderer.entity.state.AvatarRenderState;
import net.minecraft.client.renderer.entity.state.TntRenderState;
import net.minecraft.client.renderer.entity.TntRenderer;
import net.minecraft.client.renderer.feature.ModelFeatureRenderer;
import net.minecraft.client.renderer.gizmos.DrawableGizmoPrimitives;
import net.minecraft.client.renderer.item.ItemStackRenderState;
import net.minecraft.client.renderer.rendertype.RenderType;
import net.minecraft.client.renderer.state.level.CameraRenderState;
import net.minecraft.client.renderer.state.level.QuadParticleRenderState;
import net.minecraft.client.renderer.texture.*;
import net.minecraft.client.resources.model.geometry.BakedQuad;
import net.minecraft.client.resources.model.geometry.ItemQuads;
import net.minecraft.client.resources.model.ModelBakery;
import net.minecraft.core.Direction;
import net.minecraft.core.ClientAsset;
import net.minecraft.data.AtlasIds;
import net.minecraft.network.chat.Component;
import net.minecraft.resources.Identifier;
import net.minecraft.util.FormattedCharSequence;
import net.minecraft.world.item.ItemDisplayContext;
import net.minecraft.world.entity.player.PlayerSkin;
import net.minecraft.world.entity.player.PlayerModelType;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.phys.Vec3;
import net.minecraft.world.phys.shapes.VoxelShape;
import org.joml.Matrix4f;
import org.joml.Quaternionf;
import org.joml.Vector3f;
import org.jspecify.annotations.Nullable;

/** Runs Minecraft's actual avatar/armor/cape/item renderers into a CPU vertex collector. */
public final class AvatarCapture implements SubmitNodeCollector {
    private static final int ENTITY_RANGE=Math.clamp(Integer.getInteger("re4craft.entityRadius",128),16,128);
    private final BridgeServer server;
    private final Map<Identifier,Integer> textureIds = new HashMap<>();
    private final Set<Integer> binaryAlphaTextures = new HashSet<>();
    private final Map<Long,Batch> batches = new LinkedHashMap<>();
    private final Capture vertices = new Capture();
    private int nextTexture = 1, frame;
    private int entityFrame,lastEntityCount=-1,entityFailures;
    private int consecutiveFailures;
    private String lastEffects = "";
    private final CaptureProfile profile=Boolean.getBoolean("re4craft.profile")?new CaptureProfile():null;
    private static final class CaptureProfile {
        final SampleWindow extract=new SampleWindow(),submit=new SampleWindow(),serialize=new SampleWindow(),total=new SampleWindow(),interval=new SampleWindow(),entities=new SampleWindow();
        long lastCapture,reportTime=System.nanoTime();
        void report() {
            long now=System.nanoTime();if(now-reportTime<5_000_000_000L)return;reportTime=now;
            Re4CraftClient.LOG.info("Avatar profile: extract=[{}] submit=[{}] serialize=[{}] total=[{}] interval=[{}] entities=[{}]",extract.take(),submit.take(),serialize.take(),total.take(),interval.take(),entities.take());
        }
    }
    public AvatarCapture(BridgeServer server) { this.server = server; }
    public void reset() { textureIds.clear(); binaryAlphaTextures.clear(); nextTexture = 1; batches.clear(); }
    public void capture(Minecraft mc) {
        // The linked client's actual render loop is capped at 60 Hz. Capture
        // once per rendered frame: a second wall-clock gate drops jittered
        // frames and unnecessarily turns a steady render loop into 30 Hz.
        if (mc.player == null || mc.level == null) return;
        long started=profile==null?0:System.nanoTime(),stage=started;
        if(profile!=null){if(profile.lastCapture!=0)profile.interval.add(started-profile.lastCapture);profile.lastCapture=started;}
        batches.clear();
        try {
            var dispatcher = mc.getEntityRenderDispatcher();
            var camera = mc.gameRenderer.mainCamera();
            dispatcher.prepare(camera,mc.crosshairPickEntity);
            float partial = mc.getDeltaTracker().getGameTimeDeltaPartialTick(true);
            EntityRenderState state = dispatcher.extractEntity(mc.player,partial);
            if (state instanceof AvatarRenderState player && !Boolean.getBoolean("re4craft.usePlayerSkin")) {
                // Export appearance only: real vanilla wide Steve, without changing accounts or the MC world skin.
                player.skin = PlayerSkin.insecure(new ClientAsset.ResourceTexture(Identifier.withDefaultNamespace("entity/player/wide/steve")),
                        player.skin.cape(),player.skin.elytra(),PlayerModelType.WIDE);
            }
            state.nameTag = null;
            if(profile!=null){long now=System.nanoTime();profile.extract.add(now-stage);stage=now;}
            CameraRenderState cameraState = mc.gameRenderer.gameRenderState().levelRenderState.cameraRenderState;
            dispatcher.submit(state,cameraState,0,0,0,new PoseStack(),this);
            vertices.flush();
            if(profile!=null){long now=System.nanoTime();profile.submit.add(now-stage);stage=now;}
            int bytes = 40, vertexCount = 0;
            List<Batch> used = new ArrayList<>();
            for (Batch b : batches.values()) {
                int count = b.data.size()/4*6;
                if (count == 0) continue;
                used.add(b); vertexCount += count; bytes += 12 + count*36;
            }
            if (used.size() > 256 || vertexCount > 300000 || bytes > Wire.MAX_PAYLOAD) return;
            Vec3 interpolated=mc.player.getPosition(partial);
            double[] origin=Re4CraftClient.hostCoordinates(interpolated.x,interpolated.y,interpolated.z);
            ByteBuffer payload = Wire.buffer(bytes).putInt(1).putInt(++frame).putInt(used.size()).putInt(Re4CraftClient.hostEpoch())
                    .putDouble(origin[0]).putDouble(origin[1]).putDouble(origin[2]);
            for (Batch b : used) {
                // Vanilla Steve uses an entity-translucent pipeline even when
                // the entire skin is binary alpha. Real cutout texels and
                // opaque vertex colors can retain depth/self-occlusion.
                int flags=b.flags;
                if(flags==2 && binaryAlphaTextures.contains(b.texture) && b.opaqueVertices)flags=0;
                payload.putInt(b.texture).putInt(flags).putInt(b.data.size()/4*6);
                b.write(payload);
            }
            server.publishMesh(payload.array());
            if(profile!=null)profile.serialize.add(System.nanoTime()-stage);
            consecutiveFailures = 0;
        } catch (RuntimeException e) {
            if (consecutiveFailures++ == 0) Re4CraftClient.LOG.error("Vanilla avatar capture failed",e);
        } finally {if(profile!=null){profile.total.add(System.nanoTime()-started);profile.report();}}
    }
    /** Real nearby item/arrow/entity poses, with a distinct absolute capture origin. */
    void captureEntities(Minecraft mc) {
        if(mc.player==null||mc.level==null||!server.connected())return;
        long started=profile==null?0:System.nanoTime();
        batches.clear();int count=0;
        try {
            var dispatcher=mc.getEntityRenderDispatcher();dispatcher.prepare(mc.gameRenderer.mainCamera(),mc.crosshairPickEntity);
            float partial=mc.getDeltaTracker().getGameTimeDeltaPartialTick(true);
            Vec3 origin=mc.player.getPosition(partial);
            var cameraState=mc.gameRenderer.gameRenderState().levelRenderState.cameraRenderState;
            // Select the nearest bounded set from loaded entities, so a 12m
            // capture cutoff cannot erase a real flying/stuck arrow early.
            Comparator<Entity> nearestFirst=Comparator.comparingDouble(e->e.distanceToSqr(origin));
            PriorityQueue<Entity> selected=new PriorityQueue<>(64,nearestFirst.reversed());
            for(Entity entity:mc.level.entitiesForRendering()) {
                if(entity==mc.player||entity instanceof HostActorEntity||entity.isRemoved()||entity.isInvisible()||entity.distanceToSqr(origin)>ENTITY_RANGE*ENTITY_RANGE)continue;
                if(selected.size()<64)selected.add(entity);
                else if(nearestFirst.compare(entity,selected.peek())<0){selected.poll();selected.add(entity);}
            }
            List<Entity> nearby=new ArrayList<>(selected);nearby.sort(nearestFirst);
            float tntFuse=-1;boolean tntLit=false;
            for(Entity entity:nearby) {
                EntityRenderState state=dispatcher.extractEntity(entity,partial);state.nameTag=null;
                if(state instanceof TntRenderState tnt){tntFuse=tnt.fuseRemainingInTicks;tntLit=TntRenderer.isLit(tntFuse);}
                dispatcher.submit(state,cameraState,state.x-origin.x,state.y-origin.y,state.z-origin.z,new PoseStack(),this);
                vertices.flush();count++;
            }
            var levelState=mc.gameRenderer.gameRenderState().levelRenderState;
            PoseStack breakingPose=new PoseStack();
            breakingPose.translate(cameraState.pos.subtract(origin));
            ((LevelRendererAccessor)mc.levelRenderer).re4craft$submitBlockDestroyAnimation(breakingPose,this,levelState);
            vertices.flush();
            int particleVertices=0;
            for(ParticleCapture.Batch particle:ParticleCapture.capture(mc,this,origin)) {
                Batch target=batch(particle.texture(),particle.flags());
                for(ParticleCapture.Vertex p:particle.vertices())target.add(p.x(),p.y(),p.z(),p.u(),p.v(),p.color());
                particleVertices+=particle.vertices().size()/4*6;
            }
            List<Batch> used=new ArrayList<>();int bytes=40,total=0;
            for(Batch batch:batches.values()){int size=batch.data.size()/4*6;if(size==0)continue;used.add(batch);total+=size;bytes+=12+size*36;}
            if(total>300000||used.size()>256||bytes>Wire.MAX_PAYLOAD)return;
            double[] host=Re4CraftClient.hostCoordinates(origin.x,origin.y,origin.z);
            ByteBuffer payload=Wire.buffer(bytes).putInt(3).putInt(++entityFrame).putInt(used.size()).putInt(Re4CraftClient.hostEpoch())
                    .putDouble(host[0]).putDouble(host[1]).putDouble(host[2]);
            for(Batch batch:used){payload.putInt(batch.texture).putInt(batch.flags).putInt(batch.data.size()/4*6);batch.write(payload);}
            server.publishMesh(payload.array());
            if(Boolean.getBoolean("re4craft.effectsTest") && mc.gameDirectory.toPath().getFileName().toString().equals(".qa-void-instance")
                    && System.getProperty("re4craft.worldName","").startsWith("RE4Craft_EffectsQA_")) {
                var stages=levelState.blockBreakingRenderStates.stream().map(s->s.progress()).distinct().sorted().toList();
                int cracks=batches.values().stream().filter(b->(b.flags&4)!=0).mapToInt(b->b.data.size()/4*6).sum();
                int white=batches.values().stream().filter(b->(b.flags&16)!=0).mapToInt(b->b.data.size()/4*6).sum();
                String effects="tntLit="+tntLit+" crackStages="+stages+" crackVertices="+cracks+" whiteVertices="+white+" particles="+(particleVertices>0);
                if(!effects.equals(lastEffects)){Re4CraftClient.LOG.info("Minecraft effects export: frame={} tntFuse={} {} particleVertices={}",entityFrame,tntFuse,effects,particleVertices);lastEffects=effects;}
            }
            if(count!=lastEntityCount){Re4CraftClient.LOG.info("Minecraft entity export: {} actual entities, {} vertices, {} batches, range={} blocks",count,total,used.size(),ENTITY_RANGE);lastEntityCount=count;}
            entityFailures=0;
        } catch(RuntimeException exception){if(entityFailures++==0)Re4CraftClient.LOG.error("Vanilla entity capture failed",exception);}
        finally {if(profile!=null)profile.entities.add(System.nanoTime()-started);}
    }
    private Batch batch(int id) { return batch(id,0); }
    private Batch batch(int id,int flags) {return batches.computeIfAbsent(((long)id<<32)|(flags&0xffffffffL),key->new Batch(id,flags));}
    private @Nullable Batch batchFor(RenderType type) { return batchFor(type,0); }
    private @Nullable Batch batchFor(RenderType type,int extraFlags) {
        RenderTypeAccessor access = (RenderTypeAccessor)type;
        if (access.re4craft$name().contains("glint") || access.re4craft$name().contains("outline")) return null;
        Object binding = ((RenderSetupAccessor)(Object)access.re4craft$state()).re4craft$textures().get("Sampler0");
        if (binding == null) return null;
        int id = textureId(((TextureBindingAccessor)binding).re4craft$location());
        return id < 0 ? null : batch(id,(access.re4craft$name().contains("translucent")?2:0)|extraFlags);
    }
    int textureId(Identifier location) {
        Integer known = textureIds.get(location);
        if (known != null) return known;
        try (NativeImage image = image(location)) {
            if (image == null) return -1;
            // Preserve every source texel. Oversized resource packs are rejected
            // explicitly instead of silently halving each stitched sprite.
            int width=image.getWidth(),height=image.getHeight();
            int id = nextTexture;
            byte[] packet=TexturePixels.encode(id,width,height,image::getPixel);
            if (!server.publishTexture(packet)) return -1;
            if(TexturePixels.binaryAlpha(packet))binaryAlphaTextures.add(id);
            Re4CraftClient.LOG.info("Lossless Minecraft texture {}: {}x{} source texels",location,width,height);
            textureIds.put(location,id); nextTexture++;
            return id;
        } catch (java.io.IOException | RuntimeException e) {
            Re4CraftClient.LOG.debug("Texture unavailable: {}",location,e); return -1;
        }
    }
    private static @Nullable NativeImage image(Identifier location) throws java.io.IOException {
        Minecraft mc = Minecraft.getInstance();
        var resource = mc.getResourceManager().getResource(location);
        if (resource.isPresent()) try(var in = resource.get().open()) { return NativeImage.read(in); }
        var registered = ((TextureManagerAccessor)mc.getTextureManager()).re4craft$byPath().get(location);
        if (registered instanceof DynamicTexture texture && texture.getPixels() != null) {
            NativeImage copy = new NativeImage(texture.getPixels().getWidth(),texture.getPixels().getHeight(),false);
            copy.copyFrom(texture.getPixels()); return copy;
        }
        TextureAtlas atlas = null;
        if (registered instanceof TextureAtlas found) atlas = found;
        else if (location.equals(TextureAtlas.LOCATION_BLOCKS)) atlas = mc.getAtlasManager().getAtlasOrThrow(AtlasIds.BLOCKS);
        else if (location.equals(TextureAtlas.LOCATION_ITEMS)) atlas = mc.getAtlasManager().getAtlasOrThrow(AtlasIds.ITEMS);
        if (atlas == null) return null;
        TextureAtlasAccessor a = (TextureAtlasAccessor)atlas;
        int atlasWidth = a.re4craft$width(), atlasHeight = a.re4craft$height();
        if(atlasWidth>TexturePixels.MAX_DIMENSION || atlasHeight>TexturePixels.MAX_DIMENSION)
            throw new IllegalArgumentException("Stitched atlas exceeds lossless 2048 texture limit");
        int width=atlasWidth,height=atlasHeight;
        NativeImage copy = new NativeImage(width,height,true);
        try {
            for (TextureAtlasSprite sprite : a.re4craft$sprites().values()) {
                NativeImage original = ((SpriteContentsAccessor)sprite.contents()).re4craft$originalImage();
                int x0 = Math.round(sprite.getU0()*width), y0 = Math.round(sprite.getV0()*height);
                TexturePixels.blit(width,height,x0,y0,sprite.contents().width(),sprite.contents().height(),original::getPixel,copy::setPixel);
            }
            return copy;
        } catch(RuntimeException exception) {copy.close();throw exception;}
    }
    private record V(float x,float y,float z,float u,float v,int color) { }
    private static final class Batch {
        final int texture;
        final int flags;
        final List<V> data = new ArrayList<>();
        boolean opaqueVertices=true;
        Batch(int texture,int flags) { this.texture = texture;this.flags=flags; }
        void add(float x,float y,float z,float u,float v,int color) {
            if (!Float.isFinite(x)||!Float.isFinite(y)||!Float.isFinite(z)||!Float.isFinite(u)||!Float.isFinite(v))
                throw new IllegalArgumentException("Non-finite avatar vertex");
            if (data.size() >= 200000) throw new IllegalArgumentException("Avatar vertex limit");
            data.add(new V(x,y,z,u,v,color));
            opaqueVertices &= (color>>>24)==255;
        }
        void write(ByteBuffer out) {
            Vector3f ab = new Vector3f(), ac = new Vector3f();
            for(int q=0;q+3<data.size();q+=4) {
                V a=data.get(q), b=data.get(q+1), c=data.get(q+2);
                ab.set(b.x-a.x,b.y-a.y,b.z-a.z); ac.set(c.x-a.x,c.y-a.y,c.z-a.z);
                ab.cross(ac); if(ab.lengthSquared()>1e-12f) ab.normalize(); else ab.set(0,1,0);
                for(int k : new int[] {0,1,2,0,2,3}) {
                    V v=data.get(q+k);
                    out.putFloat(v.x).putFloat(v.y).putFloat(v.z).putFloat(ab.x).putFloat(ab.y).putFloat(ab.z)
                            .putFloat(v.u).putFloat(v.v).putInt(v.color);
                }
            }
        }
    }
    private final class Capture implements VertexConsumer {
        private Batch batch; private boolean pending;
        private float x,y,z,u,v; private int color;
        void begin(Batch b) { flush(); batch=b; }
        void flush() { if(pending && batch!=null) batch.add(x,y,z,u,v,color); pending=false; }
        @Override public VertexConsumer addVertex(float x,float y,float z) { flush();this.x=x;this.y=y;this.z=z;u=v=0;color=-1;pending=true;return this; }
        @Override public VertexConsumer setColor(int r,int g,int b,int a) { color=(a<<24)|(r<<16)|(g<<8)|b;return this; }
        @Override public VertexConsumer setColor(int argb) { color=argb;return this; }
        @Override public VertexConsumer setUv(float u,float v) {this.u=u;this.v=v;return this;}
        @Override public VertexConsumer setUv1(int u,int v) {return this;}
        @Override public VertexConsumer setUv2(int u,int v) {return this;}
        @Override public VertexConsumer setUv3(float u,float v) {return this;}
        @Override public VertexConsumer setNormal(float x,float y,float z) {return this;}
        @Override public VertexConsumer setLineWidth(float width) {return this;}
    }
    @Override public OrderedSubmitNodeCollector order(int order) { return this; }
    @Override public <S> void submitModel(Model<? super S> model,S state,PoseStack pose,RenderType type,int light,int overlay,int color,
                                         @Nullable UvMapping uv,int outline) {
        Batch batch=batchFor(type,overlayFlags(overlay)); if(batch==null)return;
        vertices.begin(batch); model.setupAnim(state);
        model.renderToBuffer(pose,uv==null?vertices:uv.wrap(vertices),light,overlay,color); vertices.flush();
    }
    private static int overlayFlags(int overlay) {
        // TntMinecartRenderer's actual white solid overlay. The shader mixes
        // white RGB with the original color at the overlay texel's alpha63.
        return overlay==OverlayTexture.pack(OverlayTexture.u(1),10)?16:0;
    }
    private void quad(Matrix4f pose,BakedQuad quad,int[] tint) { quad(pose,quad,tint,0); }
    private void quad(Matrix4f pose,BakedQuad quad,int[] tint,int extraFlags) {
        var material=quad.materialInfo(); var sprite=material.sprite();
        int id=textureId(sprite.atlasLocation()); if(id<0)return;
        Batch batch=batch(id,(material.layer()==ChunkSectionLayer.TRANSLUCENT?2:0)|extraFlags); int layer=material.isTinted()?material.tintIndex():-1;
        int color=layer>=0 && layer<tint.length?tint[layer]:-1;
        Vector3f p=new Vector3f();
        for(int k=0;k<4;k++) {
            pose.transformPosition(quad.position(k),p); long uv=quad.packedUV(k);
            batch.add(p.x,p.y,p.z,UVPair.unpackU(uv),UVPair.unpackV(uv),color);
        }
    }
    @Override public void submitItem(PoseStack pose,ItemDisplayContext context,int light,int overlay,int outline,int[] tint,
                                     ItemQuads quads,ItemStackRenderState.FoilType foil) {
        vertices.flush(); for(BakedQuad q:quads.all()) quad(pose.last().pose(),q,tint,overlayFlags(overlay));
    }
    @Override public void submitItem(PoseStack pose,ItemDisplayContext context,int light,int overlay,int outline,int[] tint,
                                     ItemQuads quads,@Nullable MeshView mesh,ItemStackRenderState.FoilType foil) {
        submitItem(pose,context,light,overlay,outline,tint,quads,foil); addMesh(pose.last().pose(),mesh,overlayFlags(overlay));
    }
    private void addMesh(Matrix4f pose,@Nullable MeshView mesh,int extraFlags) {
        if(mesh==null)return; Vector3f p=new Vector3f();
        mesh.forEach(q->{
            int id=textureId(q.atlas()==QuadAtlas.ITEM?TextureAtlas.LOCATION_ITEMS:TextureAtlas.LOCATION_BLOCKS);
            if(id<0)return; Batch batch=batch(id,extraFlags);
            for(int k=0;k<4;k++) {pose.transformPosition(q.x(k),q.y(k),q.z(k),p); batch.add(p.x,p.y,p.z,q.u(k),q.v(k),q.color(k));}
        });
    }
    @Override public void submitBlockModel(PoseStack pose,RenderType type,List<BlockStateModelPart> parts,int[] tint,int light,int overlay,int outline) {
        vertices.flush(); for(var part:parts) for(Direction face:new Direction[]{null,Direction.DOWN,Direction.UP,Direction.NORTH,Direction.SOUTH,Direction.WEST,Direction.EAST})
            for(BakedQuad q:part.getQuads(face)) quad(pose.last().pose(),q,tint,overlayFlags(overlay));
    }
    @Override public void submitBlockModel(PoseStack pose,java.util.function.Function<ChunkSectionLayer,RenderType> types,boolean translucent,
            List<BlockStateModelPart> parts,net.fabricmc.fabric.api.client.renderer.v1.mesh.@Nullable Mesh mesh,int[] tint,int light,int overlay,int outline) {
        submitBlockModel(pose,(RenderType)null,parts,tint,light,overlay,outline); addMesh(pose.last().pose(),mesh,overlayFlags(overlay));
    }
    @Override public void submitBreakingBlockModel(PoseStack p,List<BlockStateModelPart> parts,net.fabricmc.fabric.api.client.renderer.v1.mesh.@Nullable Mesh mesh,int progress,boolean translucent) {
        submitBreakingBlockModel(p,parts,progress,translucent);
        if(mesh==null||progress<0||progress>=ModelBakery.DESTROY_TYPES.size())return;
        Batch target=batchFor(ModelBakery.DESTROY_TYPES.get(progress),4);if(target==null)return;
        vertices.begin(target);VertexConsumer decal=new SheetedDecalTextureGenerator(vertices,p.last(),1);
        Vector3f position=new Vector3f(),normal=new Vector3f();
        mesh.forEach(q->{for(int k=0;k<4;k++) {
            p.last().pose().transformPosition(q.x(k),q.y(k),q.z(k),position);
            if(q.hasNormal(k))normal.set(q.normalX(k),q.normalY(k),q.normalZ(k));else normal.set(q.faceNormal());
            p.last().normal().transform(normal);
            decal.addVertex(position.x,position.y,position.z).setColor(-1).setNormal(normal.x,normal.y,normal.z);
        }});vertices.flush();
    }
    @Override public void submitShadow(PoseStack p,float radius,List<EntityRenderState.ShadowPiece> pieces) { }
    @Override public void submitNameTag(PoseStack p,@Nullable Vec3 attachment,int offset,Component name,boolean through,int light,CameraRenderState camera) { }
    @Override public void submitText(PoseStack p,float x,float y,FormattedCharSequence text,boolean shadow,Font.DisplayMode mode,int light,int color,int bg,int outline) { }
    @Override public void submitTextBackground(PoseStack p,float x0,float y0,float x1,float y1,int color,Font.DisplayMode mode,int light) { }
    @Override public void submitFlame(PoseStack p,EntityRenderState state,Quaternionf rotation) { }
    @Override public void submitLeash(PoseStack p,EntityRenderState.LeashState state) { }
    @Override public <S> void submitCrumblingOverlay(Model<? super S> model,S state,PoseStack p,RenderType type,int light,int overlay,int color,ModelFeatureRenderer.CrumblingOverlay crumble) { }
    @Override public void submitMovingBlock(PoseStack p,MovingBlockRenderState state,int outline) { }
    @Override public void submitBreakingBlockModel(PoseStack p,List<BlockStateModelPart> parts,int progress,boolean translucent) {
        if(progress<0||progress>=ModelBakery.DESTROY_TYPES.size())return;
        Batch target=batchFor(ModelBakery.DESTROY_TYPES.get(progress),4);if(target==null)return;
        vertices.begin(target);VertexConsumer decal=new SheetedDecalTextureGenerator(vertices,p.last(),1);
        QuadInstance instance=new QuadInstance();instance.setColor(-1);instance.setLightCoords(15728880);instance.setOverlayCoords(OverlayTexture.NO_OVERLAY);
        for(var part:parts)for(Direction face:new Direction[]{null,Direction.DOWN,Direction.UP,Direction.NORTH,Direction.SOUTH,Direction.WEST,Direction.EAST})
            for(BakedQuad quad:part.getQuads(face))decal.putBakedQuad(p.last(),quad,instance);
        vertices.flush();
    }
    @Override public void submitShapeOutline(PoseStack p,VoxelShape shape,RenderType type,int color,float width,boolean afterTerrain) { }
    @Override public void submitCustomGeometry(PoseStack p,RenderType type,SubmitNodeCollector.CustomGeometryRenderer renderer) { }
    @Override public void submitQuadParticleGroup(QuadParticleRenderState particles) { }
    @Override public void submitGizmoPrimitives(DrawableGizmoPrimitives.Group group,CameraRenderState camera,boolean onTop) { }
}
