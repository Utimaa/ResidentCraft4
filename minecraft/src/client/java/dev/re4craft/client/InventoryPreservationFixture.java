package dev.re4craft.client;

import net.minecraft.client.Minecraft;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.world.entity.EquipmentSlot;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.Items;
import java.nio.file.Path;

/** Explicit actual-server QA for startup inventory, restricted to disposable review directories. */
final class InventoryPreservationFixture {
    private static final String TAG="re4craft:inventory_qa_v1";
    static void observe(Minecraft mc,ServerPlayer player,boolean created) {
        String phase=System.getProperty("re4craft.inventoryPreservationTest","");
        if(!Boolean.getBoolean("re4craft.backgroundTest") || !(phase.equals("seed") || phase.equals("check")))return;
        Path game=mc.gameDirectory.toPath().toAbsolutePath().normalize(),lab=game.getParent();
        boolean disposable=game.getFileName().toString().equals(".qa-void-instance")
                || game.getFileName().toString().equals(".instance") && lab!=null && lab.getFileName().toString().matches("[a-f0-9]{32}")
                && lab.getParent()!=null && lab.getParent().getFileName().toString().equals("release-review")
                && lab.getParent().getParent()!=null && lab.getParent().getParent().getFileName().toString().equals("build");
        if(!disposable)throw new IllegalStateException("Inventory QA refused outside a disposable review instance");
        if(phase.equals("seed")) {
            ItemStack bow=player.getInventory().getItem(0).is(Items.BOW)?player.getInventory().getItem(0):ItemStack.EMPTY;
            if(bow.isEmpty())for(int slot=1;slot<36;slot++)if(player.getInventory().getItem(slot).is(Items.BOW)) {
                bow=player.getInventory().getItem(slot);player.getInventory().setItem(slot,ItemStack.EMPTY);break;
            }
            player.getInventory().setItem(0,bow.isEmpty()?new ItemStack(Items.BOW):bow);
            player.getInventory().setItem(1,new ItemStack(Items.NETHERITE_PICKAXE));
            player.setItemSlot(EquipmentSlot.CHEST,new ItemStack(Items.NETHERITE_CHESTPLATE));
            player.getInventory().setSelectedSlot(0);player.addTag(TAG);
            Re4CraftClient.LOG.info("Inventory persistence QA seeded through the actual integrated-server player API");return;
        }
        boolean bow=player.getInventory().getItem(0).is(Items.BOW),tool=player.getInventory().getItem(1).is(Items.NETHERITE_PICKAXE);
        boolean armor=player.getItemBySlot(EquipmentSlot.CHEST).is(Items.NETHERITE_CHESTPLATE),tag=player.entityTags().contains(TAG);
        boolean passed=!created && bow && tool && armor && tag && player.getInventory().getSelectedSlot()==0;
        Re4CraftClient.LOG.info("Inventory persistence QA: passed={}, created={}, bowHotbar={}, tool={}, armor={}, marker={}, selectedSlot={}",
                passed,created,bow,tool,armor,tag,player.getInventory().getSelectedSlot());
        if(!passed)throw new IllegalStateException("Loaded-save inventory was changed during bridge startup");
    }
}
