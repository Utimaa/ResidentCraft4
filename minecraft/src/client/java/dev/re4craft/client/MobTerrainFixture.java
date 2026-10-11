package dev.re4craft.client;

import java.util.HashSet;
import java.util.Set;
import java.util.UUID;
import net.minecraft.client.Minecraft;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.server.MinecraftServer;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.Mob;
import net.minecraft.world.entity.vehicle.VehicleEntity;

/** Opt-in telemetry for actual cow/pig/vehicle entities in the disposable mirror only. */
final class MobTerrainFixture {
    private static final boolean ENABLED=Boolean.getBoolean("re4craft.mobTest");
    private static final Set<UUID> reported=new HashSet<>();
    static void tick(MinecraftServer server) {
        if(!ENABLED || !Boolean.getBoolean("re4craft.physicsTest") || !Re4CraftClient.guestReady())return;
        String directory=Minecraft.getInstance().gameDirectory.toPath().toAbsolutePath().normalize().getFileName().toString();
        if(!directory.equals(".qa-void-instance"))return;
        UUID playerId=Re4CraftClient.hostPlayerId();
        var player=playerId==null?null:server.getPlayerList().getPlayer(playerId);
        if(player==null)return;
        for(Entity entity:player.level().getEntitiesOfClass(Entity.class,player.getBoundingBox().inflate(16),
                candidate->candidate.entityTags().contains("RE4CRAFT_MOB_QA"))) {
            if(!(entity instanceof Mob) && !(entity instanceof VehicleEntity))continue;
            if(entity.tickCount<80 || reported.contains(entity.getUUID()))continue;
            reported.add(entity.getUUID());
            var host=Re4CraftClient.minecraftToHost(entity.position());
            double floor=HostCollision.groundAt(entity.position(),.1);
            boolean supported=Double.isFinite(floor) && Math.abs(floor-entity.getY())<.03 && entity.onGround();
            Re4CraftClient.LOG.info("MOB_TERRAIN_QA type={} supported={} onGround={} hostFeet={} ticks={} velocity={}",
                    BuiltInRegistries.ENTITY_TYPE.getKey(entity.getType()),supported,entity.onGround(),host,entity.tickCount,entity.getDeltaMovement());
        }
    }
}
