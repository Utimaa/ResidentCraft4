package dev.re4craft.client;

import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Set;
import net.minecraft.client.Minecraft;
import net.minecraft.core.BlockPos;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.entity.BlockEntity;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.block.state.properties.BlockStateProperties;
import net.minecraft.world.level.block.state.properties.BedPart;

/** Explicit disposable-world QA: original block-entity models, lid animation,
 * source texture publication, real removal, and no fabricated model geometry. */
final class BlockEntityRenderFixture {
    private static final boolean REQUESTED=Boolean.getBoolean("re4craft.blockEntityRenderTest");
    private final Map<BlockPos,String> expected=new LinkedHashMap<>();
    private final Map<String,Geometry> observed=new HashMap<>(),closed=new HashMap<>();
    private long started;
    private int stage,checks,failures;
    private boolean animated,baseline,completed;
    private volatile boolean setupDone;
    private BlockPos chest;

    boolean enabled(Minecraft mc){
        return REQUESTED&&Boolean.getBoolean("re4craft.testWorld")&&Boolean.getBoolean("re4craft.voidMirror")
                &&mc.gameDirectory.toPath().toAbsolutePath().normalize().getFileName().toString().equals(".qa-void-instance")
                &&System.getProperty("re4craft.worldName","").startsWith("RE4Craft_BlockEntityQA_")
                &&mc.player!=null&&mc.level!=null&&mc.getSingleplayerServer()!=null
                &&mc.getSingleplayerServer().getWorldData().getLevelName().equals(System.getProperty("re4craft.worldName",""));
    }
    void frame(Minecraft mc){
        if(!enabled(mc)||completed||!Re4CraftClient.guestReady())return;
        if(started==0){
            started=System.nanoTime();var origin=mc.player.blockPosition();
            Map<BlockPos,BlockState> blocks=new LinkedHashMap<>();
            chest=add(blocks,origin.offset(3,0,2),"chest",Blocks.CHEST.defaultBlockState());
            add(blocks,origin.offset(5,0,2),"trapped_chest",Blocks.TRAPPED_CHEST.defaultBlockState());
            add(blocks,origin.offset(7,0,2),"ender_chest",Blocks.ENDER_CHEST.defaultBlockState());
            add(blocks,origin.offset(9,0,2),"skeleton_skull",Blocks.SKELETON_SKULL.defaultBlockState());
            add(blocks,origin.offset(11,0,2),"player_head",Blocks.PLAYER_HEAD.defaultBlockState());
            add(blocks,origin.offset(13,0,2),"dragon_head",Blocks.DRAGON_HEAD.defaultBlockState());
            // 26.3 beds use ordinary block models, with no registered BED block
            // entity renderer. Leave those on the existing scene-mesh route.
            blocks.put(origin.offset(3,0,5),Blocks.BED.red().defaultBlockState().setValue(BlockStateProperties.BED_PART,BedPart.FOOT));
            blocks.put(origin.offset(3,0,4),Blocks.BED.red().defaultBlockState().setValue(BlockStateProperties.BED_PART,BedPart.HEAD));
            add(blocks,origin.offset(5,0,5),"banner",Blocks.BANNER.red().defaultBlockState());
            add(blocks,origin.offset(7,0,5),"shulker_box",Blocks.DYED_SHULKER_BOX.purple().defaultBlockState());
            add(blocks,origin.offset(9,0,5),"decorated_pot",Blocks.DECORATED_POT.defaultBlockState());
            add(blocks,origin.offset(11,0,5),"enchanting_book",Blocks.ENCHANTING_TABLE.defaultBlockState());
            add(blocks,origin.offset(13,0,5),"bell",Blocks.BELL.defaultBlockState());
            // Standing-sign bodies likewise use ordinary models; their optional
            // glyph submissions are a separate font-atlas export capability.
            blocks.put(origin.offset(5,0,8),Blocks.OAK_SIGN.defaultBlockState());
            add(blocks,origin.offset(7,0,8),"conduit",Blocks.CONDUIT.defaultBlockState());
            var uuid=mc.player.getUUID();
            mc.getSingleplayerServer().execute(()->{
                var player=mc.getSingleplayerServer().getPlayerList().getPlayer(uuid);if(player==null)return;
                for(var pos:blocks.keySet())player.level().setBlock(pos.below(),Blocks.STONE.defaultBlockState(),Block.UPDATE_ALL);
                for(var entry:blocks.entrySet())player.level().setBlock(entry.getKey(),entry.getValue(),Block.UPDATE_ALL);
                setupDone=true;
                Re4CraftClient.LOG.info("BLOCK_ENTITY_RENDER_QA placed {} real block fixtures, {} use block-entity renderers",blocks.size(),expected.size());
            });
            return;
        }
        if(!setupDone)return;
        long elapsed=System.nanoTime()-started;
        if(stage==0&&elapsed>=5_000_000_000L){
            if(!baseline){var missing=new HashSet<>(expected.values());missing.removeAll(observed.keySet());Re4CraftClient.LOG.error("BLOCK_ENTITY_RENDER_QA missing or invalid models {} observed={}",missing,observed.keySet());}
            check(baseline,"all original block-entity models acquired geometry and textures");
            event(mc,1);stage=1;
        }else if(stage==1&&elapsed>=8_000_000_000L){
            check(animated,"real chest open event changes original lid geometry");
            event(mc,0);stage=2;
        }else if(stage==2&&elapsed>=10_000_000_000L){
            check(observed.containsKey("chest"),"real chest remains present after closing");
            var uuid=mc.player.getUUID();var positions=Set.copyOf(expected.keySet());
            mc.getSingleplayerServer().execute(()->{
                var player=mc.getSingleplayerServer().getPlayerList().getPlayer(uuid);if(player==null)return;
                for(var position:positions)player.level().setBlock(position,Blocks.AIR.defaultBlockState(),Block.UPDATE_ALL);
                Re4CraftClient.LOG.info("BLOCK_ENTITY_RENDER_QA removed real model fixtures");
            });stage=3;
        }else if(stage==3&&elapsed>=12_000_000_000L){
            check(observed.isEmpty(),"removed block entities emit no stale geometry");
            Re4CraftClient.LOG.info("BLOCK_ENTITY_RENDER_QA result checks={} failures={} models={}",checks,failures,closed.size());
            completed=true;stage=4;
        }
    }
    private BlockPos add(Map<BlockPos,BlockState> blocks,BlockPos pos,String name,BlockState state){expected.put(pos,name);blocks.put(pos,state);return pos;}
    private void event(Minecraft mc,int open){
        var uuid=mc.player.getUUID();
        mc.getSingleplayerServer().execute(()->{
            var player=mc.getSingleplayerServer().getPlayerList().getPlayer(uuid);if(player==null)return;
            player.level().blockEvent(chest,Blocks.CHEST,1,open);
            Re4CraftClient.LOG.info("BLOCK_ENTITY_RENDER_QA real chest event open={}",open);
        });
    }
    void beginFrame(){observed.clear();}
    void observed(BlockEntity entity,Geometry geometry){String name=expected.get(entity.getBlockPos());if(name!=null&&geometry!=null)observed.put(name,geometry);}
    void endFrame(Minecraft mc){
        if(!enabled(mc)||completed||!setupDone)return;
        if(!baseline&&stage==0&&observed.size()==expected.size()&&observed.values().stream().allMatch(Geometry::valid)){
            for(var entry:observed.entrySet()){
                check(entry.getValue().valid(),entry.getKey()+" original textured finite quad geometry");
                var sample=entry.getValue();
                Re4CraftClient.LOG.info("BLOCK_ENTITY_RENDER_QA model={} vertices={} textures={} hash={} bounds=[{},{},{}]-[{},{},{}]",
                        entry.getKey(),sample.vertices,sample.textures,Long.toUnsignedString(sample.hash),sample.minX,sample.minY,sample.minZ,sample.maxX,sample.maxY,sample.maxZ);
            }
            closed.putAll(observed);baseline=true;
        }
        if(stage==1&&closed.containsKey("chest")&&observed.containsKey("chest"))
            animated|=closed.get("chest").hash!=observed.get("chest").hash;
    }
    private void check(boolean value,String label){checks++;if(!value){failures++;Re4CraftClient.LOG.error("BLOCK_ENTITY_RENDER_QA FAIL {}",label);}}

    static final class Geometry {
        int vertices;
        long hash=0xcbf29ce484222325L;
        final Set<Integer> textures=new HashSet<>();
        float minX=Float.POSITIVE_INFINITY,minY=minX,minZ=minX,maxX=Float.NEGATIVE_INFINITY,maxY=maxX,maxZ=maxX;
        void vertex(int texture,int flags,float x,float y,float z,float u,float v,int color){
            vertices++;textures.add(texture);
            minX=Math.min(minX,x);minY=Math.min(minY,y);minZ=Math.min(minZ,z);maxX=Math.max(maxX,x);maxY=Math.max(maxY,y);maxZ=Math.max(maxZ,z);
            for(int part:new int[]{Float.floatToIntBits(x),Float.floatToIntBits(y),Float.floatToIntBits(z),Float.floatToIntBits(u),Float.floatToIntBits(v),color,texture,flags})hash=(hash^part)*0x100000001b3L;
        }
        boolean valid(){return vertices>=4&&vertices%4==0&&!textures.isEmpty()&&Float.isFinite(minX)&&Float.isFinite(minY)&&Float.isFinite(minZ)
                &&Float.isFinite(maxX)&&Float.isFinite(maxY)&&Float.isFinite(maxZ)&&maxX>minX&&maxY>minY&&maxZ>minZ;}
    }
}
