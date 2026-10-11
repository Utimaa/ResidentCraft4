package dev.re4craft.client;

import net.fabricmc.fabric.api.event.lifecycle.v1.ServerTickEvents;
import net.minecraft.client.Minecraft;
import net.minecraft.core.BlockPos;
import net.minecraft.server.MinecraftServer;
import net.minecraft.world.level.material.Fluids;
import net.minecraft.world.phys.Vec3;

/** Read-only telemetry, enabled only for the named disposable fluid QA world. */
final class FluidTerrainFixture {
    static void init() {ServerTickEvents.END_SERVER_TICK.register(FluidTerrainFixture::tick);}
    private static void tick(MinecraftServer server) {
        if(!Boolean.getBoolean("re4craft.fluidTest") || server.getTickCount()%20!=0
                || !System.getProperty("re4craft.worldName","").startsWith("RE4Craft_FluidQA_"))return;
        if(!Minecraft.getInstance().gameDirectory.toPath().getFileName().toString().equals(".qa-void-instance"))return;
        var id=Re4CraftClient.hostPlayerId();var player=id==null?null:server.getPlayerList().getPlayer(id);
        if(player==null || !Re4CraftClient.hostAnchored())return;
        Vec3 base=Re4CraftClient.hostToMinecraft(new Vec3(0,0,0));BlockPos center=BlockPos.containing(base);
        int water=0,lava=0,below=0,beyondWall=0;boolean waterSource=false,lavaSource=false;
        for(BlockPos cell:BlockPos.betweenClosed(center.offset(-9,-3,-9),center.offset(9,3,9))) {
            var fluid=player.level().getFluidState(cell);if(fluid.isEmpty())continue;
            if(fluid.getType().isSame(Fluids.WATER)) {water++;waterSource|=fluid.isSource();}
            if(fluid.getType().isSame(Fluids.LAVA)) {lava++;lavaSource|=fluid.isSource();}
            Vec3 host=Re4CraftClient.minecraftToHost(Vec3.atCenterOf(cell));
            if(host.y<-.01)below++;
            if(host.x>4.3)beyondWall++;
        }
        Re4CraftClient.LOG.info("FLUID_TERRAIN_QA ready={} water={} lava={} below={} beyondWall={} waterSource={} lavaSource={} inventory={}",
                Re4CraftClient.guestReady(),water,lava,below,beyondWall,
                waterSource,lavaSource,
                Minecraft.getInstance().gui.screen()==null?"closed":Minecraft.getInstance().gui.screen().getClass().getSimpleName());
    }
}
