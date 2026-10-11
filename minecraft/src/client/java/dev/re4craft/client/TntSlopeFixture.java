package dev.re4craft.client;

import java.util.LinkedHashMap;
import java.util.Map;
import net.minecraft.client.Minecraft;
import net.minecraft.core.BlockPos;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.world.entity.item.PrimedTnt;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.phys.Vec3;

/** Genuine TNT tick observations, only in an explicitly named disposable slope lab. */
final class TntSlopeFixture {
    private static final Map<String,PrimedTnt> cases=new LinkedHashMap<>();
    private static MinecraftServer prepared;
    private static int started;
    private static boolean enabled() {
        return Boolean.getBoolean("re4craft.tntSlopeTest") && Boolean.getBoolean("re4craft.physicsTest")
                && System.getProperty("re4craft.worldName","").startsWith("RE4Craft_SlopeQA_")
                && Minecraft.getInstance().gameDirectory.toPath().toAbsolutePath().normalize().getFileName().toString().equals(".qa-void-instance");
    }
    private static void spawn(ServerLevel level,String label,double x,double y,double z,double vx,double vy) {
        Vec3 at=Re4CraftClient.hostToMinecraft(x,y,z);
        PrimedTnt tnt=new PrimedTnt(level,at.x,at.y,at.z,null);
        tnt.setDeltaMovement(vx,vy,0);tnt.setFuse(120);tnt.entityTags().add("RE4CRAFT_SLOPE_QA");
        if(!level.addFreshEntity(tnt))throw new IllegalStateException("Slope QA TNT creation failed");
        cases.put(label,tnt);
    }
    static void tick(MinecraftServer server) {
        if(!enabled() || server.getPlayerList().getPlayers().isEmpty())return;
        var player=server.getPlayerList().getPlayers().getFirst();ServerLevel level=player.level();
        if(prepared!=server && Re4CraftClient.guestReady()) {
            prepared=server;started=server.getTickCount();cases.clear();
            // This independent control uses actual blocks and the unchanged vanilla collision path.
            for(int x=38;x<=46;x++)for(int z=8;z<=16;z++) {
                Vec3 cell=Re4CraftClient.hostToMinecraft(x,-1,z);level.setBlock(BlockPos.containing(cell),Blocks.STONE.defaultBlockState(),3);
            }
            for(int y=0;y<2;y++)for(int z=19;z<=21;z++) {
                Vec3 cell=Re4CraftClient.hostToMinecraft(-6,y,z);level.setBlock(BlockPos.containing(cell),Blocks.STONE.defaultBlockState(),3);
            }
            double slope=Math.tan(Math.toRadians(20)),rest=slope*.15;
            spawn(level,"slope_still",0,rest,-16,0,0);
            spawn(level,"slope_down",0,rest,-12,-.15,0);
            spawn(level,"slope_fall",8,8*slope+4,-20,0,0);
            spawn(level,"flat_still",0,0,8,0,0);
            spawn(level,"flat_move",0,0,12,-.15,0);
            spawn(level,"vanilla_move",42,0,12,-.15,0);
            spawn(level,"block_wall",-4,0,20,-.6,0);
            spawn(level,"air_launch",12,4,8,.15,.2);
            Re4CraftClient.LOG.info("TNT_SLOPE_QA prepared actual vanilla TNT on imported20degree hill, imported flat, real block floor/wall and air; no modified fuse or velocity after spawning");
        }
        if(prepared!=server)return;
        int elapsed=server.getTickCount()-started;
        if(elapsed<=175)for(var entry:cases.entrySet()) {
            PrimedTnt tnt=entry.getValue();Vec3 at=Re4CraftClient.minecraftToHost(tnt.position()),velocity=tnt.getDeltaMovement();
            Re4CraftClient.LOG.info("TNT_SLOPE_QA sample label={} elapsed={} ready={} x={} y={} z={} vx={} vy={} vz={} grounded={} vertical={} below={} horizontal={} step={} fuse={} removed={}",
                    entry.getKey(),elapsed,Re4CraftClient.guestReady(),at.x,at.y,at.z,velocity.x,velocity.y,velocity.z,tnt.onGround(),
                    tnt.verticalCollision,tnt.verticalCollisionBelow,tnt.horizontalCollision,tnt.maxUpStep(),tnt.getFuse(),tnt.isRemoved());
        }
        if(elapsed==180) {
            Re4CraftClient.LOG.info("TNT_SLOPE_QA finished; saving disposable world");Minecraft mc=Minecraft.getInstance();mc.execute(mc::stop);
        }
    }
}
