package dev.re4craft.client;
import net.fabricmc.api.ModInitializer;
/** Register entity types before Minecraft freezes its registries. */
public final class Re4CraftInit implements ModInitializer {
    @Override public void onInitialize() {BridgeCombat.register();HostLoot.register();FluidTerrainFixture.init();LootTerrainFixture.init();ShieldFixture.init();MobNavigationFixture.init();}
}
