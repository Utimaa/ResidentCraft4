/* Adapted from SkyCraft v0.1.0, copyright (c) 2026 chasmlol, MIT. */
package dev.re4craft.client.mixin;

import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import dev.re4craft.client.Re4CraftClient;
import net.minecraft.client.gui.screens.worldselection.WorldOpenFlows;
import net.minecraft.world.level.storage.LevelStorageSource;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;

/** The authored sandbox preset is experimental; only the selected opt-in isolated save skips its prompt. */
@Mixin(WorldOpenFlows.class)
public abstract class WorldOpenFlowsMixin {
    @WrapOperation(method="openWorldCheckWorldStemCompatibility",at=@At(value="INVOKE",target="Lnet/minecraft/client/gui/screens/worldselection/WorldOpenFlows;askForBackup(Lnet/minecraft/world/level/storage/LevelStorageSource$LevelStorageAccess;ZLjava/lang/Runnable;Ljava/lang/Runnable;)V"))
    private void re4craft$ownSandbox(WorldOpenFlows self,LevelStorageSource.LevelStorageAccess access,boolean oldCustomized,Runnable proceed,Runnable cancel,Operation<Void> original) {
        String selected=System.getProperty("re4craft.worldName","RE4Craft_MirrorWorld");
        if(Re4CraftClient.mirrorSandbox() && access.getLevelId().equals(selected)) {
            Re4CraftClient.LOG.info("Opening selected mirror {} without its experimental-preset prompt",selected);
            proceed.run();
        }
        else original.call(self,access,oldCustomized,proceed,cancel);
    }
}
