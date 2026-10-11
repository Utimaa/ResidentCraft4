package dev.re4craft.client.mixin;
import dev.re4craft.client.Re4CraftClient;
import net.minecraft.server.MinecraftServer;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;
@Mixin(MinecraftServer.class)
public abstract class HostFlightMixin {
    @Inject(method="allowFlight",at=@At("HEAD"),cancellable=true)
    private void re4craft$hostFloorFlightCheck(CallbackInfoReturnable<Boolean> cir) {
        // Waiting on streamed terrain must not become a vanilla flight kick.
        if(Re4CraftClient.guestRequested() || Re4CraftClient.mirrorSandbox())cir.setReturnValue(true);
    }
}
