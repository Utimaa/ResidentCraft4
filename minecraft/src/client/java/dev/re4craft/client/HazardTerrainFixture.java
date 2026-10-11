package dev.re4craft.client;

import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import net.minecraft.client.Minecraft;
import net.minecraft.server.MinecraftServer;
import net.minecraft.world.entity.item.PrimedTnt;
import net.minecraft.world.phys.AABB;
import net.minecraft.world.phys.Vec3;

/** Read-only opt-in telemetry for actual primed TNT in a newly named disposable QA save. */
final class HazardTerrainFixture {
    private static final Map<Integer,AABB> reportedBounds=new HashMap<>();
    static void tick(MinecraftServer server) {
        TntSlopeFixture.tick(server);
        if(!Boolean.getBoolean("re4craft.hazardTest") || !Boolean.getBoolean("re4craft.physicsTest"))return;
        String world=System.getProperty("re4craft.worldName","");
        if(!world.startsWith("RE4Craft_HazardQA_"))return;
        String directory=Minecraft.getInstance().gameDirectory.toPath().toAbsolutePath().normalize().getFileName().toString();
        if(!directory.equals(".qa-void-instance") || server.getTickCount()%20!=0)return;
        var uuid=Re4CraftClient.hostPlayerId();var player=uuid==null?null:server.getPlayerList().getPlayer(uuid);
        if(player==null)return;
        var present=new HashSet<Integer>();
        for(HostActorEntity actor:player.level().getEntitiesOfClass(HostActorEntity.class,player.getBoundingBox().inflate(128))) {
            int id=actor.formId();present.add(id);AABB box=actor.getBoundingBox();
            if(box.equals(reportedBounds.put(id,box)))continue;
            Vec3 lo=Re4CraftClient.minecraftToHost(new Vec3(box.minX,box.minY,box.minZ));
            Vec3 hi=Re4CraftClient.minecraftToHost(new Vec3(box.maxX,box.maxY,box.maxZ));
            Re4CraftClient.LOG.info("HOST_ACTOR_BOUNDS_QA id={} min={} max={}",id,lo,hi);
        }
        reportedBounds.keySet().removeIf(id->{if(present.contains(id))return false;
            Re4CraftClient.LOG.info("HOST_ACTOR_BOUNDS_QA removed={}",id);return true;});
        for(PrimedTnt tnt:player.level().getEntitiesOfClass(PrimedTnt.class,player.getBoundingBox().inflate(40),
                entity->entity.entityTags().contains("RE4CRAFT_HAZARD_QA"))) {
            var host=Re4CraftClient.minecraftToHost(tnt.position());
            double ground=HostCollision.groundAt(tnt.position(),.15);
            boolean supported=Double.isFinite(ground) && Math.abs(tnt.getY()-ground)<.03 && tnt.onGround();
            Re4CraftClient.LOG.info("TNT_TERRAIN_QA supported={} ready={} hostFeet={} fuse={} ticks={}",
                    supported,Re4CraftClient.guestReady(),host,tnt.getFuse(),tnt.tickCount);
        }
    }
}
