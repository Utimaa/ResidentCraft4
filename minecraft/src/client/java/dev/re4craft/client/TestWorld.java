package dev.re4craft.client;

import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.screens.TitleScreen;
import net.minecraft.core.registries.Registries;
import net.minecraft.world.Difficulty;
import net.minecraft.world.entity.EquipmentSlot;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.Items;
import net.minecraft.world.level.*;
import net.minecraft.world.level.levelgen.WorldOptions;
import net.minecraft.world.level.levelgen.presets.WorldPresets;
import net.minecraft.world.level.levelgen.presets.WorldPreset;
import net.minecraft.resources.ResourceKey;
import net.minecraft.resources.Identifier;
import net.minecraft.world.phys.Vec3;
import dev.re4craft.control.BackgroundClientPolicy;

/** Explicit development opt-in. Operates only on the project's isolated test-world directory. */
final class TestWorld {
    private static final boolean ENABLED = Boolean.getBoolean("re4craft.testWorld");
    private static final boolean VOID=Boolean.getBoolean("re4craft.voidMirror");
    private static final String NAME = worldName();
    private static String worldName() {
        String name=System.getProperty("re4craft.worldName",VOID?"RE4Craft_MirrorWorld":"RE4Craft_TestWorld");
        if(!name.matches("[A-Za-z0-9][A-Za-z0-9_-]{0,95}"))throw new IllegalArgumentException("Invalid isolated world name");
        return name;
    }
    private static final ResourceKey<WorldPreset> MIRROR=ResourceKey.create(Registries.WORLD_PRESET,Identifier.fromNamespaceAndPath("re4craft","mirror"));
    private static final long START = System.nanoTime();
    private static final long EXIT = Long.getLong("re4craft.testExitSeconds",180L)*1_000_000_000L;
    private static boolean attempted, equipped,createdWorld;
    private static volatile boolean bowGranted;
    private static long nextBowAttempt;
    private static final String STARTER_BOW_TAG="re4craft:starter_bow_v1";
    private static boolean dedicatedInstance(Minecraft mc) {
        String directory=mc.gameDirectory.toPath().toAbsolutePath().normalize().getFileName().toString();
        return directory.equals(".instance") || directory.equals(".dev-instance")
                || directory.equals(".qa-instance") && Boolean.getBoolean("re4craft.sceneTest")
                || directory.equals(".qa-void-instance") && VOID;
    }
    static void frame(Minecraft mc) {
        if (!ENABLED) return;
        // Repeat the directory gate before any world, player, option or kit mutation.
        // A refused first title screen must never later equip a manually opened original save.
        if(!dedicatedInstance(mc)) {
            if(!attempted)Re4CraftClient.LOG.error("TestWorld refused outside the dedicated bridge instance");
            attempted=true;return;
        }
        if(Boolean.getBoolean("re4craft.physicsTest") && mc.options.guiScale().get()!=2)mc.options.guiScale().set(2);
        mc.options.pauseOnLostFocus = false;
        if (mc.gui.screen() != null && mc.gui.screen().getClass().getSimpleName().equals("AccessibilityOnboardingScreen")) {
            mc.options.onboardAccessibility = false;
            mc.gui.setScreen(new TitleScreen());
        }
        if (EXIT>0 && System.nanoTime()-START > EXIT) { mc.stop(); return; }
        if (!attempted && mc.gui.screen() instanceof TitleScreen title) {
            // Guard against accidentally pointing a dev launch at the user's normal .minecraft saves.
            String directory=mc.gameDirectory.toPath().toAbsolutePath().normalize().getFileName().toString();
            if (!directory.equals(".instance") && !directory.equals(".dev-instance") &&
                    !(directory.equals(".qa-instance") && Boolean.getBoolean("re4craft.sceneTest"))) {
                if(!(directory.equals(".qa-void-instance") && VOID)) {
                Re4CraftClient.LOG.error("TestWorld refused: game directory must be the dedicated .instance or .dev-instance"); attempted=true; return;
                }
            }
            attempted=true;
            Re4CraftClient.LOG.info("Opening isolated {} {}",VOID?"void mirror":"flat test world",NAME);
            if (mc.getLevelSource().levelExists(NAME)) {
                mc.createWorldOpenFlows().openWorld(NAME,()->mc.gui.setScreen(title));
            } else {
                createdWorld=true;
                LevelSettings settings=new LevelSettings(NAME,GameType.SURVIVAL,
                        new LevelSettings.DifficultySettings(VOID?Difficulty.NORMAL:Difficulty.PEACEFUL,false,false),true,WorldDataConfiguration.DEFAULT);
                mc.createWorldOpenFlows().createFreshLevel(NAME,settings,new WorldOptions(0,false,false),
                        registries->registries.lookupOrThrow(Registries.WORLD_PRESET).getOrThrow(VOID?MIRROR:WorldPresets.FLAT).value().createWorldDimensions(),title);
            }
        }
        if (!equipped && mc.player!=null && mc.getSingleplayerServer()!=null) {
            equipped=true; var uuid=mc.player.getUUID();
            if(VOID) {
                mc.player.setNoGravity(true);mc.player.setPos(0,64,0);
                mc.player.setDeltaMovement(Vec3.ZERO);mc.player.resetFallDistance();
            }
            mc.getSingleplayerServer().execute(()->{
                var p=mc.getSingleplayerServer().getPlayerList().getPlayer(uuid); if(p==null)return;
                if(VOID) {
                    mc.getSingleplayerServer().setDifficulty(Difficulty.NORMAL,true);
                    p.setNoGravity(true);p.teleportTo(0,64,0);p.setDeltaMovement(Vec3.ZERO);p.resetFallDistance();
                    Re4CraftClient.LOG.info("Void mirror ready: spawn 0,64,0; gravity disabled; block below is {}",p.level().getBlockState(new net.minecraft.core.BlockPos(0,63,0)).getBlock());
                }
                if(BackgroundClientPolicy.starterKit(dedicatedInstance(mc),createdWorld)) {
                String directory=mc.gameDirectory.toPath().toAbsolutePath().normalize().getFileName().toString();
                boolean diagnosticKit=directory.equals(".qa-instance")||directory.equals(".qa-void-instance");
                if(!diagnosticKit) {
                    // New campaign profiles begin at the wooden loot stage.
                    // This branch never runs while loading an existing save.
                    p.setItemSlot(EquipmentSlot.MAINHAND,new ItemStack(Items.WOODEN_SWORD));
                    p.setItemSlot(EquipmentSlot.OFFHAND,new ItemStack(Items.SHIELD));
                    p.getInventory().setItem(1,new ItemStack(Items.OAK_PLANKS,16));
                    p.getInventory().setItem(2,new ItemStack(Items.CRAFTING_TABLE));
                    p.getInventory().setItem(3,new ItemStack(Items.WOODEN_PICKAXE));
                    p.getInventory().setItem(4,new ItemStack(Items.BREAD,8));
                    p.getInventory().setItem(5,new ItemStack(Items.ARROW,16));
                }else {
                p.setItemSlot(EquipmentSlot.CHEST,new ItemStack(Items.DIAMOND_CHESTPLATE));
                p.setItemSlot(EquipmentSlot.HEAD,new ItemStack(Items.DIAMOND_HELMET));
                p.setItemSlot(EquipmentSlot.MAINHAND,new ItemStack(Items.DIAMOND_SWORD));
                p.setItemSlot(EquipmentSlot.OFFHAND,new ItemStack(Items.SHIELD));
                p.getInventory().setItem(1,new ItemStack(Items.STONE,64));
                p.getInventory().setItem(2,new ItemStack(Items.CRAFTING_TABLE,16));
                if(VOID) {
                    p.getInventory().setItem(3,new ItemStack(Items.FLINT_AND_STEEL));
                    p.getInventory().setItem(4,new ItemStack(Items.LAVA_BUCKET));
                    p.getInventory().setItem(5,new ItemStack(Items.TNT,64));
                    p.getInventory().setItem(6,new ItemStack(Items.REDSTONE_TORCH,64));
                    p.getInventory().setItem(7,new ItemStack(Items.DIAMOND_PICKAXE));
                    p.getInventory().setItem(8,new ItemStack(Items.COOKED_BEEF,64));
                    boolean hasWater=false;
                    for(int slot=0;slot<36;slot++)if(p.getInventory().getItem(slot).is(Items.WATER_BUCKET))hasWater=true;
                    if(!hasWater)for(int slot=9;slot<36;slot++)if(p.getInventory().getItem(slot).isEmpty()) {
                        p.getInventory().setItem(slot,new ItemStack(Items.WATER_BUCKET));break;
                    }
                }
                }
                }
                InventoryPreservationFixture.observe(mc,p,createdWorld);
            });
        }
        if(VOID && mc.player!=null && !Re4CraftClient.active()) {mc.player.setNoGravity(true);mc.player.resetFallDistance();}
        if(Boolean.getBoolean("re4craft.grantStarterBow") && !bowGranted && equipped && mc.player!=null && mc.getSingleplayerServer()!=null
                && System.nanoTime()>=nextBowAttempt) {
            nextBowAttempt=System.nanoTime()+2_000_000_000L;
            var uuid=mc.player.getUUID();var server=mc.getSingleplayerServer();
            server.execute(()->{
                var p=server.getPlayerList().getPlayer(uuid);if(p==null || server!=mc.getSingleplayerServer())return;
                // Vanilla player tags are saved in this player's world data. A reconnect/relaunch cannot duplicate the kit.
                if(p.entityTags().contains(STARTER_BOW_TAG)){
                    bowGranted=true;
                    if(Boolean.getBoolean("re4craft.backgroundTest"))Re4CraftClient.LOG.info("Background QA: persisted starter bow marker found; no duplicate grant");
                    return;
                }
                int free=BackgroundClientPolicy.starterBowSlot(false,p.entityTags().size(),
                        slot->p.getInventory().getItem(slot).is(Items.BOW),slot->p.getInventory().getItem(slot).isEmpty());
                if(free==-2){bowGranted=p.addTag(STARTER_BOW_TAG);return;}
                if(free<0)return; // Never replace an item or spill it into the mirror void.
                if(!p.addTag(STARTER_BOW_TAG))return;
                p.getInventory().setItem(free,new ItemStack(Items.BOW));bowGranted=true;
                Re4CraftClient.LOG.info("Starter bow granted once to the selected isolated world player; free inventory slot {}",free);
            });
        }
    }
}
