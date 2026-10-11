package dev.re4craft.client.mixin;
import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import dev.re4craft.client.HostCollision;
import net.minecraft.client.Camera;
import net.minecraft.world.level.ClipContext;
import net.minecraft.world.level.Level;
import net.minecraft.world.phys.BlockHitResult;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
@Mixin(Camera.class)
public abstract class CameraZoomMixin {
    @WrapOperation(method="getMaxZoom",at=@At(value="INVOKE",target="Lnet/minecraft/world/level/Level;clip(Lnet/minecraft/world/level/ClipContext;)Lnet/minecraft/world/phys/BlockHitResult;"))
    private BlockHitResult re4craft$zoomAgainstHost(Level level,ClipContext context,Operation<BlockHitResult> original) {
        return HostCollision.refine(context.getFrom(),context.getTo(),original.call(level,context));
    }
}
