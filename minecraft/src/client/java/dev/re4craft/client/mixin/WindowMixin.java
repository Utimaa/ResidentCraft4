package dev.re4craft.client.mixin;

import com.mojang.blaze3d.platform.Window;
import dev.re4craft.client.Re4CraftClient;
import dev.re4craft.client.BackgroundClient;
import dev.re4craft.control.BackgroundClientPolicy;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.*;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(Window.class)
public abstract class WindowMixin {
    @Shadow private boolean fullscreenRequested;
    @ModifyArg(method="createWindow",at=@At(value="INVOKE",target="Lcom/mojang/renderpearl/api/device/GpuBackend;createWindow(Ljava/lang/String;IIJ)J"),index=3)
    private long re4craft$hiddenWindow(long flags) {
        return BackgroundClientPolicy.windowFlags(flags,BackgroundClient.enabled());
    }
    @Inject(method="createWindow",at=@At("RETURN"))
    private void re4craft$validateHiddenWindow(CallbackInfoReturnable<Long> cir) {BackgroundClient.created(cir.getReturnValue());}
    @Inject(method="setMode",at=@At("HEAD"))
    private void re4craft$windowedBackground(CallbackInfo ci) {if(BackgroundClient.enabled())fullscreenRequested=false;}
    @Inject(method="setFullscreen",at=@At("HEAD"),cancellable=true)
    private void re4craft$noBackgroundFullscreen(boolean fullscreen,CallbackInfo ci) {
        if(BackgroundClient.enabled()){fullscreenRequested=false;ci.cancel();}
    }
    @Inject(method="isFocused",at=@At("HEAD"),cancellable=true)
    private void re4craft$virtualFocus(CallbackInfoReturnable<Boolean> cir) { if(BackgroundClient.enabled() || Re4CraftClient.active()) cir.setReturnValue(true); }
    @Inject(method="isIconified",at=@At("HEAD"),cancellable=true)
    private void re4craft$backgroundRender(CallbackInfoReturnable<Boolean> cir) { if(BackgroundClient.enabled() || Re4CraftClient.active()) cir.setReturnValue(false); }
}
