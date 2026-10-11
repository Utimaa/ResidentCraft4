package dev.re4craft.client;

import net.fabricmc.fabric.api.event.lifecycle.v1.ServerTickEvents;
import net.minecraft.client.Minecraft;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.world.entity.EquipmentSlot;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.Items;
import net.minecraft.world.damagesource.DamageSource;

/** Explicit disposable-world preparations and observations of vanilla defense. */
final class ShieldFixture {
    record Observation(float health,int damage,int count,boolean using,boolean blocking,int useTicks,boolean cooldown,int armor) { }
    private static MinecraftServer preparedServer;
    private static int finishedTick=-1;
    static void init() {ServerTickEvents.END_SERVER_TICK.register(ShieldFixture::tick);}
    private static boolean enabled() {
        return Boolean.getBoolean("re4craft.shieldTest")
                &&System.getProperty("re4craft.worldName","").startsWith("RE4Craft_ShieldQA_")
                &&Minecraft.getInstance().gameDirectory.toPath().toAbsolutePath().normalize().getFileName().toString().equals(".qa-void-instance");
    }
    private static void tick(MinecraftServer server) {
        if(!enabled()||server.getPlayerList().getPlayers().isEmpty())return;
        ServerPlayer player=server.getPlayerList().getPlayers().getFirst();
        if(finishedTick>=0&&server.getTickCount()-finishedTick>40) {
            finishedTick=-1;Minecraft mc=Minecraft.getInstance();mc.execute(mc::stop);return;
        }
        if(preparedServer!=server&&Re4CraftClient.guestReady()) {
            preparedServer=server;player.getInventory().clearContent();
            for(var slot:new EquipmentSlot[]{EquipmentSlot.HEAD,EquipmentSlot.CHEST,EquipmentSlot.LEGS,EquipmentSlot.FEET,EquipmentSlot.MAINHAND})player.setItemSlot(slot,ItemStack.EMPTY);
            player.setItemSlot(EquipmentSlot.OFFHAND,new ItemStack(Items.SHIELD));
            player.setHealth(player.getMaxHealth());player.getFoodData().setFoodLevel(20);
            Re4CraftClient.LOG.info("SHIELD_QA prepared real Survival player and unenchanted offhand shield");
        }
        if(preparedServer==server&&server.getTickCount()%5==0) {
            var shield=player.getOffhandItem();
            Re4CraftClient.LOG.info("SHIELD_QA tick={} ready={} using={} blocking={} useTicks={} cooldown={} shieldCount={} shieldDamage={} health={}",
                    server.getTickCount(),Re4CraftClient.guestReady(),player.isUsingItem(),player.isBlocking(),player.getTicksUsingItem(),
                    player.getCooldowns().isOnCooldown(shield),shield.getCount(),shield.getDamageValue(),player.getHealth());
        }
    }
    static Observation beforeDamage(ServerPlayer player,int event) {
        if(!enabled())return null;
        // Independent cases retain vanilla damage/use timing. Only health and
        // explicit test equipment are prepared; no blocking math is replaced.
        player.setHealth(player.getMaxHealth());
        var shield=player.getOffhandItem();
        if(event==7) {player.stopUsingItem();player.getCooldowns().addCooldown(shield,40);}
        if(event==8&&!shield.isEmpty())shield.setDamageValue(shield.getMaxDamage()-1);
        return new Observation(player.getHealth(),shield.getDamageValue(),shield.getCount(),player.isUsingItem(),player.isBlocking(),
                player.getTicksUsingItem(),player.getCooldowns().isOnCooldown(shield),player.getArmorValue());
    }
    static void afterDamage(ServerPlayer player,int event,int kind,int flags,DamageSource source,Observation before) {
        if(before==null)return;
        var shield=player.getOffhandItem();
        Re4CraftClient.LOG.info("SHIELD_QA event={} kind={} flags={} healthBefore={} healthAfter={} using={} blocking={} useTicks={} cooldown={} armor={} damageBefore={} damageAfter={} countBefore={} countAfter={} type={} source={}",
                event,kind,flags,before.health,player.getHealth(),before.using,before.blocking,before.useTicks,before.cooldown,before.armor,
                before.damage,shield.getDamageValue(),before.count,shield.getCount(),source.getMsgId(),source.getSourcePosition());
        if(event==8) {player.stopUsingItem();player.setItemSlot(EquipmentSlot.CHEST,new ItemStack(Items.DIAMOND_CHESTPLATE));}
        if(event==9) {player.stopUsingItem();player.setItemSlot(EquipmentSlot.OFFHAND,new ItemStack(Items.SHIELD));}
        if(event==10&&player.getHealth()<=0)finishedTick=player.level().getServer().getTickCount();
    }
}
