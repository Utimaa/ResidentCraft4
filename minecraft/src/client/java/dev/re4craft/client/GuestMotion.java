package dev.re4craft.client;
import com.mojang.blaze3d.vertex.PoseStack;
import dev.re4craft.client.mixin.GameRendererAccessor;
import dev.re4craft.protocol.Wire;
import net.minecraft.client.Minecraft;
import net.minecraft.world.phys.Vec3;
import org.joml.Matrix4f;
import org.joml.Quaternionf;
import org.joml.Vector3f;
/** Export the same interpolated feet, camera and vanilla view-bob used by this rendered frame. */
final class GuestMotion {
    private static long frame;
    static void capture(Minecraft mc) {
        if(mc.player==null || !Re4CraftClient.hostAnchored())return;
        var p=mc.player;var camera=mc.gameRenderer.mainCamera();
        float partial=mc.getDeltaTracker().getGameTimeDeltaPartialTick(true);
        Vec3 exportedFeet=p.getPosition(partial);
        Vec3 feet=Re4CraftClient.minecraftToHost(exportedFeet);
        Vec3 cameraPosition=camera.position();
        Quaternionf orientation=new Quaternionf(camera.rotation());
        var state=mc.gameRenderer.gameRenderState().levelRenderState.cameraRenderState;
        if(state!=null && state.initialized) {
            PoseStack bob=new PoseStack();GameRendererAccessor vanilla=(GameRendererAccessor)mc.gameRenderer;
            vanilla.re4craft$bobHurt(state,bob);
            if(mc.options.bobView().get())vanilla.re4craft$bobView(state,bob);
            // Vanilla applies this matrix to the view. Its inverse is the corresponding camera pose.
            Matrix4f inverse=new Matrix4f(bob.last().pose()).invert();
            Vector3f offset=inverse.getTranslation(new Vector3f());camera.rotation().transform(offset);
            cameraPosition=cameraPosition.add(offset.x,offset.y,offset.z);
            orientation.mul(inverse.getUnnormalizedRotation(new Quaternionf()).normalize());
        }
        Vector3f forward=orientation.transform(new Vector3f(0,0,-1));
        float yaw=(float)Math.toDegrees(Math.atan2(-forward.x,forward.z));
        float pitch=(float)Math.toDegrees(-Math.asin(Math.clamp(forward.y,-1,1)));
        Quaternionf base=new Quaternionf().rotationYXZ((float)Math.PI-(float)Math.toRadians(yaw),-(float)Math.toRadians(pitch),0);
        Vector3f up=orientation.transform(new Vector3f(0,1,0));
        Vector3f baseUp=base.transform(new Vector3f(0,1,0)),baseRight=base.transform(new Vector3f(1,0,0));
        float roll=(float)Math.toDegrees(Math.atan2(-up.dot(baseRight),up.dot(baseUp)));
        Vec3 hostCamera=Re4CraftClient.minecraftToHost(cameraPosition),velocity=p.getDeltaMovement();
        int flags=(Re4CraftClient.guestReady()?1:0)|(p.onGround()?2:0)|(p.isDeadOrDying()?4:0)
                |(mc.gui.screen()!=null?8:0)|(state!=null && state.initialized?16:0);
        if((flags&5)==0 && Re4CraftClient.traversalAcknowledged(mc,exportedFeet))flags|=Wire.MOTION_NATIVE_TRAVERSAL_ACK;
        var b=Wire.buffer(108).putInt(Re4CraftClient.hostEpoch()).putInt(flags).putLong(++frame);
        b.putDouble(feet.x).putDouble(feet.y).putDouble(feet.z).putDouble(hostCamera.x).putDouble(hostCamera.y).putDouble(hostCamera.z)
                .putFloat((float)velocity.x).putFloat((float)velocity.y).putFloat((float)velocity.z)
                .putFloat(yaw).putFloat(pitch).putFloat(p.getHealth()).putFloat(p.getMaxHealth()).putFloat(roll).putFloat(camera.getFov())
                .putInt(mc.options.getCameraType().ordinal()).putInt(0);
        Re4CraftClient.SERVER.publishMotion(b.array());
    }
}
