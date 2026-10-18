package dev.re4craft.client;

import com.mojang.blaze3d.vertex.PoseStack;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.PriorityQueue;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.feature.ModelFeatureRenderer;
import net.minecraft.client.renderer.state.level.CameraRenderState;
import net.minecraft.client.renderer.state.level.LevelRenderState;
import net.minecraft.world.level.block.entity.BlockEntity;
import net.minecraft.world.level.chunk.status.ChunkStatus;
import net.minecraft.world.phys.Vec3;

/** The normal block mesh intentionally excludes animated block-entity models.
 * Run their original renderer each frame, alongside the original entity models.
 * Query only loaded chunks; neither this export nor its selection loads a chunk. */
final class BlockEntityCapture {
    private static final int MAX_BLOCK_ENTITIES=128;
    private final int range;
    private final BlockEntityRenderFixture fixture=new BlockEntityRenderFixture();

    BlockEntityCapture(int range){this.range=range;}

    int submit(Minecraft mc,AvatarCapture collector,Vec3 origin,float partial,CameraRenderState camera,LevelRenderState levelState){
        fixture.frame(mc);
        var dispatcher=mc.getBlockEntityRenderDispatcher();
        dispatcher.prepare(camera.pos);
        Comparator<BlockEntity> nearestFirst=Comparator.comparingDouble(entity->entity.getBlockPos().distToCenterSqr(origin));
        PriorityQueue<BlockEntity> selected=new PriorityQueue<>(MAX_BLOCK_ENTITIES,nearestFirst.reversed());
        int cx=mc.player.getBlockX(),cz=mc.player.getBlockZ();
        for(int x=(cx-range)>>4;x<=(cx+range)>>4;x++)for(int z=(cz-range)>>4;z<=(cz+range)>>4;z++){
            var chunk=mc.level.getChunkSource().getChunk(x,z,ChunkStatus.FULL,false);
            if(chunk==null)continue;
            for(var entity:chunk.getBlockEntities().values()){
                if(entity.isRemoved()||entity.getLevel()!=mc.level||entity.getBlockPos().distToCenterSqr(origin)>range*range)continue;
                var renderer=dispatcher.getRenderer(entity);
                if(renderer==null||!renderer.shouldRender(entity,camera.pos))continue;
                if(selected.size()<MAX_BLOCK_ENTITIES)selected.add(entity);
                else if(nearestFirst.compare(entity,selected.peek())<0){selected.poll();selected.add(entity);}
            }
        }
        List<BlockEntity> nearby=new ArrayList<>(selected);nearby.sort(nearestFirst);
        boolean observe=fixture.enabled(mc);int count=0;
        if(observe)fixture.beginFrame();
        for(var entity:nearby){
            // Retain complete earlier models if a dense scene reaches the existing
            // category-3 wire limit. Selection never raises the protocol budgets.
            var mark=collector.markGeometry();
            var position=entity.getBlockPos();
            var pose=new PoseStack();pose.translate(position.getX()-origin.x,position.getY()-origin.y,position.getZ()-origin.z);
            ModelFeatureRenderer.CrumblingOverlay crumble=null;
            for(var breaking:levelState.blockBreakingRenderStates)if(breaking.blockPos().equals(position)){
                crumble=new ModelFeatureRenderer.CrumblingOverlay(breaking.progress(),pose.last().copy());break;
            }
            var renderer=dispatcher.getRenderer(entity);
            // Vanilla partitions off-screen renderers into a second extraction pass.
            // This CPU collector handles each renderer once, without a camera frustum.
            var state=dispatcher.tryExtractRenderState(entity,partial,crumble,renderer.shouldRenderOffScreen());
            if(state==null)continue;
            if(observe)collector.beginObservation();
            BlockEntityRenderFixture.Geometry observation;
            try {dispatcher.submit(state,pose,collector,camera);collector.flushGeometry();}
            finally {observation=collector.endObservation();}
            if(!collector.geometryWithinLimits()){collector.restoreGeometry(mark);break;}
            if(observe)fixture.observed(entity,observation);
            count++;
        }
        if(observe)fixture.endFrame(mc);
        return count;
    }
}
