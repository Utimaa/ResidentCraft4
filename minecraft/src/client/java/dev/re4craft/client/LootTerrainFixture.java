package dev.re4craft.client;

import java.util.ArrayList;
import java.util.Map;
import java.util.TreeMap;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerTickEvents;
import net.minecraft.client.Minecraft;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.server.MinecraftServer;
import net.minecraft.world.entity.item.ItemEntity;

/** Bounded, read-only observations of real rewards in an explicitly named QA save. */
final class LootTerrainFixture {
    static void init() {ServerTickEvents.END_SERVER_TICK.register(LootTerrainFixture::tick);}
    private static void tick(MinecraftServer server) {
        if(!Boolean.getBoolean("re4craft.lootTest") || server.getTickCount()%10!=0
                || !System.getProperty("re4craft.worldName","").startsWith("RE4Craft_LootQA_"))return;
        if(!Minecraft.getInstance().gameDirectory.toPath().getFileName().toString().equals(".qa-void-instance"))return;
        var players=server.getPlayerList().getPlayers();if(players.isEmpty())return;
        var player=players.getFirst();
        Map<String,Integer> inventory=new TreeMap<>(),items=new TreeMap<>();
        for(int slot=0;slot<player.getInventory().getContainerSize();slot++) {
            var stack=player.getInventory().getItem(slot);
            if(!stack.isEmpty())inventory.merge(BuiltInRegistries.ITEM.getKey(stack.getItem()).toString(),stack.getCount(),Integer::sum);
        }
        var detail=new ArrayList<String>();int count=0;double minimumY=Double.POSITIVE_INFINITY,maximumSpeed=0;
        for(var item:player.level().getEntitiesOfClass(ItemEntity.class,player.getBoundingBox().inflate(128))) {
            if(!item.isAlive())continue;if(++count>128)break;
            var stack=item.getItem();String type=BuiltInRegistries.ITEM.getKey(stack.getItem()).toString();
            items.merge(type,stack.getCount(),Integer::sum);
            minimumY=Math.min(minimumY,item.getY());maximumSpeed=Math.max(maximumSpeed,item.getDeltaMovement().length());
            if(detail.size()<64)detail.add(type+"="+stack.getCount()+"@"+item.getX()+","+item.getY()+","+item.getZ());
        }
        Re4CraftClient.LOG.info("LOOT_TERRAIN_QA tick={} epoch={} ready={} anchored={} player={} items={} minimumY={} maximumSpeed={} entityStacks={} inventory={} entities={}",
                server.getTickCount(),Re4CraftClient.hostEpoch(),Re4CraftClient.guestReady(),Re4CraftClient.hostAnchored(),
                player.position(),count,count==0?null:minimumY,maximumSpeed,items,inventory,detail);
    }
}
