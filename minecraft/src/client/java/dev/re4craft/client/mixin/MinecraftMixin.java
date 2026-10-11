package dev.re4craft.client.mixin;

import dev.re4craft.client.Re4CraftClient;
import dev.re4craft.client.BackgroundClient;
import net.minecraft.client.Minecraft;
import net.minecraft.client.MouseHandler;
import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.*;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(Minecraft.class)
public abstract class MinecraftMixin {
    @Inject(method="runTick", at=@At("HEAD"))
    private void re4craft$input(boolean advanceGameTime, CallbackInfo ci) { Re4CraftClient.beginFrame((Minecraft)(Object)this); }
    @Inject(method="renderFrame", at=@At(value="INVOKE",target="Lnet/minecraft/client/renderer/GameRenderer;render()V",shift=At.Shift.AFTER))
    private void re4craft$export(boolean advanceGameTime, CallbackInfo ci) { Re4CraftClient.afterRender((Minecraft)(Object)this); }
    @Inject(method="isWindowActive", at=@At("HEAD"), cancellable=true)
    private void re4craft$hostFocus(CallbackInfoReturnable<Boolean> cir) {
        if (BackgroundClient.enabled() || Re4CraftClient.active()) cir.setReturnValue(true);
    }
    @WrapOperation(method="handleKeybinds",at=@At(value="INVOKE",target="Lnet/minecraft/client/MouseHandler;isMouseGrabbed()Z"))
    private boolean re4craft$hostMining(MouseHandler handler,Operation<Boolean> original) {
        return Re4CraftClient.active() || original.call(handler);
    }
    @Inject(method="startAttack",at=@At("HEAD"))
    private void re4craft$qaAttackTarget(CallbackInfoReturnable<Boolean> cir) {
        if(!Boolean.getBoolean("re4craft.physicsTest") || !Re4CraftClient.active())return;
        Minecraft mc=(Minecraft)(Object)this;
        var hit=mc.hitResult;
        Re4CraftClient.LOG.info("QA vanilla attack: hit {}, target {}, slot {}, eye {}",hit==null?null:hit.getType(),
                hit instanceof net.minecraft.world.phys.EntityHitResult entity?entity.getEntity().getType():null,
                mc.player==null?-1:mc.player.getInventory().getSelectedSlot(),mc.player==null?null:mc.player.getEyePosition());
    }
}
