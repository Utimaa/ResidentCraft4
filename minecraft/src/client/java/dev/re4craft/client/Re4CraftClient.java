package dev.re4craft.client;

import net.fabricmc.api.ClientModInitializer;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientLifecycleEvents;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerTickEvents;
import net.fabricmc.fabric.api.entity.event.v1.ServerPlayerEvents;
import net.minecraft.client.gui.screens.PauseScreen;
import net.minecraft.client.Minecraft;
import dev.re4craft.protocol.Wire;
import java.nio.ByteBuffer;
import java.util.UUID;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.phys.Vec3;
import dev.re4craft.world.WorldCoordinateSafety;
import dev.re4craft.control.NativeTraversalReceipt;

public final class Re4CraftClient implements ClientModInitializer {
    public static final Logger LOG=LoggerFactory.getLogger("re4craft");
    public static final BridgeServer SERVER=new BridgeServer();
    private record Anchor(int epoch,Vec3 minecraft,Vec3 host) { }
    private static volatile Anchor anchor;
    private static volatile Wire.Input latest;
    private static volatile long lastInputNanos;
    private static volatile boolean active,guestRequested,guestReady;
    private static String holdReason;
    private static volatile int generation;
    private static long tick;
    private static AvatarCapture avatar;
    private static SceneCapture scene;
    private static EntityCapture entities;
    private static final FrameExporter overlay=new FrameExporter(SERVER);
    private static int appliedWidth,appliedHeight;
    private static Boolean savedPauseOnLostFocus;
    private static LocalPlayer hostPlayer;
    private static boolean savedNoGravity;
    private static Vec3 savedVelocity;
    private static long nextHostSync;
    private static boolean gaitAnchored,snapped;
    private static double gaitX,gaitZ;
    private static int gaitEpoch;
    private static volatile UUID hostPlayerId;
    private static volatile boolean hostGrounded;
    private record TraversalRequest(NativeTraversalReceipt.Scope scope,Anchor anchor,Vec3 host,Vec3 target) { }
    private record ServerSnap(Vec3 target,TraversalRequest traversal) { }
    private static volatile ServerSnap serverSnap;
    private static volatile TraversalRequest traversalRequest;
    private static volatile NativeTraversalReceipt.Receipt traversalReceipt;
    private static TraversalRequest appliedTraversal; // integrated-server thread only
    private static long traversalSerial;
    private static net.minecraft.server.level.ServerPlayer savedServerPlayer;
    private static boolean savedServerGravity;
    private static Vec3 savedServerVelocity;
    // This survives native room/toggle epochs and the temporary null hostPlayer
    // during vanilla respawn. It belongs to one integrated server and dimension.
    private record Checkpoint(UUID player,net.minecraft.resources.ResourceKey<net.minecraft.world.level.Level> dimension,
                              net.minecraft.server.MinecraftServer server,Vec3 feet) { }
    private static volatile Checkpoint checkpoint,pendingRespawn;
    private static Checkpoint coordinateRecovery;
    private static String coordinateHold;
    @Override public void onInitializeClient() {
        BridgeCombat.init();
        SERVER.start();
        ServerPlayerEvents.AFTER_RESPAWN.register((oldPlayer,newPlayer,alive)->{
            Checkpoint point=checkpoint;
            if(!mirrorSandbox() || point==null || !point.player.equals(newPlayer.getUUID())
                    || point.server!=newPlayer.level().getServer() || !point.dimension.equals(newPlayer.level().dimension())
                    || !validCoordinates(newPlayer,point.feet))return;
            // Empty mirror dimensions have no vanilla spawn floor. Send one
            // real teleport after the vanilla respawn packet, with its normal
            // acknowledgment; recurring host movement never teleports.
            newPlayer.teleportTo(point.feet.x,point.feet.y,point.feet.z);
            newPlayer.setDeltaMovement(Vec3.ZERO);newPlayer.resetFallDistance();
            pendingRespawn=point;
            LOG.info("Mirror respawn restored grounded checkpoint {} in {}",point.feet,point.dimension.identifier());
        });
        ClientLifecycleEvents.CLIENT_STOPPING.register(mc->SERVER.close());
        ClientTickEvents.END_CLIENT_TICK.register(Re4CraftClient::updateGait);
        ClientTickEvents.END_CLIENT_TICK.register(MovingSurfaceFixture::tick);
        ServerTickEvents.START_SERVER_TICK.register(server->{
            UUID id=hostPlayerId;
            var p=id==null?null:server.getPlayerList().getPlayer(id);
            if(p!=savedServerPlayer && savedServerPlayer!=null) {
                savedServerPlayer.setNoGravity(savedServerGravity);savedServerPlayer.setDeltaMovement(savedServerVelocity);
                savedServerPlayer=null;
            }
            if(p==null)return;
            if(savedServerPlayer==null) {savedServerPlayer=p;savedServerGravity=p.isNoGravity();savedServerVelocity=p.getDeltaMovement();}
            p.setNoGravity(!guestReady);
            if(!guestReady) {p.setDeltaMovement(Vec3.ZERO);p.resetFallDistance();}
            ServerSnap snap=serverSnap;
            if(snap!=null) {
                serverSnap=null;
                TraversalRequest request=snap.traversal;
                if(request==null || traversalServerEligible(server,p,request)) {
                    Vec3 target=snap.target;p.setPos(target.x,target.y,target.z);p.setDeltaMovement(Vec3.ZERO);p.resetFallDistance();p.setOnGround(hostGrounded);
                    appliedTraversal=request;
                } else appliedTraversal=null;
            }
        });
        ServerTickEvents.END_SERVER_TICK.register(server->{
            TraversalRequest request=appliedTraversal;
            if(request!=null) {
                var p=server.getPlayerList().getPlayer(request.scope.player());
                if(p!=null && traversalServerEligible(server,p,request)
                        && NativeTraversalReceipt.close(point(p.position()),point(request.target)))
                    traversalReceipt=new NativeTraversalReceipt.Receipt(request.scope,point(request.target),point(p.position()),System.nanoTime());
                else {traversalReceipt=null;appliedTraversal=null;}
            }
            if(!guestRequested && hostPlayerId!=null) {var p=server.getPlayerList().getPlayer(hostPlayerId);if(p!=null)p.setOnGround(hostGrounded);}
            MobTerrainFixture.tick(server);
            HazardTerrainFixture.tick(server);
        });
    }
    public static boolean active() {return active;}
    public static boolean guestReady() {return active && guestRequested && guestReady;}
    public static boolean guestRequested() {return guestRequested && hostPlayer!=null;}
    public static boolean guestPlayer(Entity entity) {
        return guestRequested && hostPlayerId!=null && entity.getUUID().equals(hostPlayerId) && !entity.noPhysics;
    }
    public static UUID hostPlayerId() {return hostPlayerId;}
    private static boolean terrainEntityKind(Entity entity) {
        return !(entity instanceof HostActorEntity) && (entity instanceof net.minecraft.world.entity.item.ItemEntity
                || entity instanceof net.minecraft.world.entity.ExperienceOrb
                || entity instanceof net.minecraft.world.entity.item.PrimedTnt
                || entity instanceof net.minecraft.world.entity.projectile.Projectile
                || entity instanceof net.minecraft.world.entity.Mob
                || entity instanceof net.minecraft.world.entity.vehicle.VehicleEntity);
    }
    private static boolean mirrorTerrainEntity(Entity entity) {
        return terrainEntityKind(entity) && mirrorSandbox()
                && entity.level().dimension().equals(net.minecraft.world.level.Level.OVERWORLD);
    }
    /** Hold before vanilla's temporary noPhysics movement bypass in the empty mirror. */
    public static boolean holdMirrorMovement(Entity entity) {
        return mirrorTerrainEntity(entity) && (!guestReady() || !HostCollision.covered(entity.position()));
    }
    public static boolean physicsEntity(Entity entity) {
        if(guestPlayer(entity))return true;
        if(entity.noPhysics || !terrainEntityKind(entity))return false;
        // This owned empty mirror has no vanilla floor. Persisted entities must
        // wait for their matching host terrain even while a reconnect, player
        // replacement or save reload has temporarily removed the anchor.
        // Normal Minecraft worlds retain the existing activated-player scope.
        if(mirrorTerrainEntity(entity))return true;
        LocalPlayer p=hostPlayer;
        return guestRequested && anchor!=null && p!=null
                && entity.level().dimension().equals(p.level().dimension()) && entity.position().distanceToSqr(p.position())<=128*128;
    }
    public static boolean mirrorSandbox() {
        if(!Boolean.getBoolean("re4craft.testWorld") || !Boolean.getBoolean("re4craft.voidMirror"))return false;
        String dir=Minecraft.getInstance().gameDirectory.toPath().toAbsolutePath().normalize().getFileName().toString();
        return dir.equals(".dev-instance") || dir.equals(".instance") || dir.equals(".qa-void-instance");
    }
    public static boolean hostAnchored() {return anchor!=null && hostPlayer!=null;}
    public static Vec3 hostToMinecraft(double x,double y,double z) {
        Anchor a=anchor;return a==null?new Vec3(x,y,z):a.minecraft.add(x-a.host.x,y-a.host.y,z-a.host.z);
    }
    public static Vec3 hostToMinecraft(Vec3 p) {return hostToMinecraft(p.x,p.y,p.z);}
    /** A native event may only use its own epoch's single sampled transform. */
    public static Vec3 hostToMinecraftForEpoch(int epoch,double x,double y,double z) {
        Anchor a=anchor;
        return a==null || a.epoch!=epoch?null:a.minecraft.add(x-a.host.x,y-a.host.y,z-a.host.z);
    }
    public static Vec3 minecraftToHost(Vec3 p) {
        Anchor a=anchor;return a==null?p:a.host.add(p.x-a.minecraft.x,p.y-a.minecraft.y,p.z-a.minecraft.z);
    }
    public static double[] hostCoordinates(double x,double y,double z) {
        Vec3 p=minecraftToHost(new Vec3(x,y,z));return new double[]{p.x,p.y,p.z};
    }
    public static int hostEpoch() {Anchor a=anchor;return a==null?0:a.epoch;}
    private static NativeTraversalReceipt.Point point(Vec3 p) {return new NativeTraversalReceipt.Point(p.x,p.y,p.z);}
    private static void queueServerSnap(Vec3 target) {serverSnap=new ServerSnap(target,null);}
    private static void clearTraversal() {traversalRequest=null;traversalReceipt=null;}
    private static boolean traversalServerEligible(net.minecraft.server.MinecraftServer server,
            net.minecraft.server.level.ServerPlayer player,TraversalRequest request) {
        Wire.Input input=latest;Anchor transform=anchor;long now=System.nanoTime();
        return request==traversalRequest && transform==request.anchor && request.scope.server()==server
                && request.scope.generation()==generation && generation==SERVER.generation()
                && request.scope.player().equals(hostPlayerId) && request.scope.player().equals(player.getUUID())
                && request.scope.dimension().equals(player.level().dimension()) && player.isAlive()
                && input!=null && input.hostEpoch()==request.scope.epoch() && input.active() && active && SERVER.connected()
                && now>=lastInputNanos && now-lastInputNanos<NativeTraversalReceipt.INPUT_FRESH_NANOS
                && NativeTraversalReceipt.traversalInput(input.flags())
                && new Vec3(input.hostX(),input.hostY(),input.hostZ()).equals(request.host)
                && validCoordinates(player,request.target);
    }
    /** Called with the actual interpolated feet that this frame will export. */
    static boolean traversalAcknowledged(Minecraft mc,Vec3 exportedFeet) {
        TraversalRequest request=traversalRequest;Wire.Input input=latest;Anchor transform=anchor;
        if(request==null || input==null || transform!=request.anchor || mc.player==null || mc.player!=hostPlayer
                || request.scope.generation()!=generation || generation!=SERVER.generation()
                || request.scope.server()!=mc.getSingleplayerServer()
                || !request.scope.player().equals(mc.player.getUUID())
                || !request.scope.dimension().equals(mc.player.level().dimension())
                || input.hostEpoch()!=request.scope.epoch() || guestReady()
                || !new Vec3(input.hostX(),input.hostY(),input.hostZ()).equals(request.host))return false;
        return NativeTraversalReceipt.acknowledge(request.scope,point(request.target),point(exportedFeet),traversalReceipt,
                input.flags(),active,SERVER.connected(),mc.player.isAlive(),System.nanoTime(),lastInputNanos);
    }
    private static void synchronizeTraversal(Minecraft mc,Vec3 target) {
        var server=mc.getSingleplayerServer();Anchor transform=anchor;
        if(server==null || transform==null || mc.player==null || !mc.player.isAlive() || !validCoordinates(mc.player,target)) {clearTraversal();return;}
        Vec3 host=new Vec3(latest.hostX(),latest.hostY(),latest.hostZ());TraversalRequest previous=traversalRequest;
        if(previous==null || previous.anchor!=transform || previous.scope.generation()!=generation
                || previous.scope.server()!=server || !previous.scope.player().equals(mc.player.getUUID())
                || !previous.scope.dimension().equals(mc.player.level().dimension()) || !previous.host.equals(host)) {
            traversalReceipt=null;
            previous=new TraversalRequest(new NativeTraversalReceipt.Scope(generation,transform.epoch,mc.player.getUUID(),
                    mc.player.level().dimension(),server,++traversalSerial),transform,host,target);
            traversalRequest=previous;nextHostSync=0;
        }
        if(System.nanoTime()>=nextHostSync) {
            nextHostSync=System.nanoTime()+50_000_000L;serverSnap=new ServerSnap(target,previous);
        }
    }
    public static boolean hostCollisionReady(int epoch) {Anchor a=anchor;return a!=null && hostPlayer!=null && a.epoch==epoch;}
    public static boolean validCoordinates(Entity player,Vec3 feet) {
        int reserve=mirrorSandbox()?8:0;
        return WorldCoordinateSafety.validFeet(feet.x,feet.y,feet.z,player.level().getMinY()+reserve,player.level().getMaxY()-reserve,player.getBbHeight());
    }
    private static Checkpoint checkpointFor(Minecraft mc) {
        Checkpoint point=checkpoint;
        return point!=null && mc.player!=null && point.player.equals(mc.player.getUUID())
                && point.server==mc.getSingleplayerServer() && point.dimension.equals(mc.player.level().dimension())
                && validCoordinates(mc.player,point.feet)?point:null;
    }
    public static void beginFrame(Minecraft mc) {
        BackgroundClient.frame(mc);
        TestWorld.frame(mc);
        int currentGeneration=SERVER.generation();
        if(generation!=currentGeneration) {
            generation=currentGeneration;latest=null;anchor=null;snapped=false;guestRequested=false;guestReady=false;holdReason=null;restoreHostPlayer();
            if(avatar!=null)avatar.reset();if(scene!=null)scene.reset();entities=null;overlay.reset();HostCollision.reset();BridgeCombat.reset();HostLoot.reset();
        }
        Wire.Packet event;
        while((event=SERVER.pollEvent())!=null) {
            try {
                if(event.type()==Wire.ENEMIES || event.type()==Wire.DAMAGE) {BridgeCombat.accept(event);continue;}
                if(event.type()==Wire.LOOT) {HostLoot.accept(Wire.loot(event));continue;}
                if(event.type()==Wire.KEY_EVENT) {VirtualInput.key(mc,Wire.key(event));continue;}
                if(event.type()==Wire.TEXT) {VirtualInput.text(mc,Wire.text(event));continue;}
                Wire.Input input=Wire.input(event);boolean wasActive=active;
                latest=input;lastInputNanos=System.nanoTime();active=input.active() && SERVER.connected();
                if(!active || !NativeTraversalReceipt.traversalInput(input.flags()))clearTraversal();
                if(active && !wasActive && mc.gui.screen() instanceof PauseScreen)mc.gui.setScreen(null);
                VirtualInput.apply(mc,input,true);
            } catch(java.io.IOException error) {LOG.warn("Rejected host event: {}",error.getMessage());}
        }
        // The host increments this epoch for an explicit toggle, room change or
        // connection reset. Focus/pause and temporary input loss keep it fixed.
        if(anchor!=null && latest!=null && anchor.epoch!=latest.hostEpoch()) {
            anchor=null;snapped=false;guestRequested=false;guestReady=false;holdReason=null;restoreHostPlayer();HostCollision.reset();
        }
        byte[] collision=SERVER.pollCollision();if(collision!=null)HostCollision.accept(collision);
        byte[] moving=SERVER.pollMovingSurfaces();if(moving!=null)HostCollision.acceptMoving(moving);
        active=latest!=null && latest.active() && SERVER.connected() && System.nanoTime()-lastInputNanos<750_000_000L;
        if(!active) {
            VirtualInput.releaseAll(mc);guestReady=false;clearTraversal();
            if(SERVER.connected() && hostAnchored()) {
                String reason=System.nanoTime()-lastInputNanos>=750_000_000L?"input older than 750 ms"
                        :latest!=null && (latest.flags()&8)!=0?"host paused":"host focus/control hold";
                if(!reason.equals(holdReason))LOG.info("Bridge hold: {}; retained anchor epoch {}",reason,anchor.epoch);
                holdReason=reason;
                hostPlayer.setNoGravity(true);hostPlayer.setDeltaMovement(Vec3.ZERO);hostPlayer.resetFallDistance();
                // Keep the guest collision scope, player identity and transform.
                // HostCollision freezes its entities while guestReady is false.
                return;
            }
            guestRequested=false;holdReason=null;restoreHostPlayer();
            if(savedPauseOnLostFocus!=null) {mc.options.pauseOnLostFocus=savedPauseOnLostFocus;savedPauseOnLostFocus=null;}
            return;
        }
        if(holdReason!=null) {LOG.info("Bridge resumed with retained anchor epoch {}",anchor==null?0:anchor.epoch);holdReason=null;}
        if(savedPauseOnLostFocus==null)savedPauseOnLostFocus=mc.options.pauseOnLostFocus;
        mc.options.pauseOnLostFocus=false;
        if(latest.viewportWidth()>0 && latest.viewportHeight()>0) {
            double scale=Math.min(1,Math.min(1920.0/latest.viewportWidth(),1080.0/latest.viewportHeight()));
            int w=Math.max(320,(int)(latest.viewportWidth()*scale)),h=Math.max(180,(int)(latest.viewportHeight()*scale));
            if(w!=appliedWidth || h!=appliedHeight) {appliedWidth=w;appliedHeight=h;mc.getWindow().setWindowed(w,h);}
        }
        if(mc.player==null || (latest.flags()&20)==0) {guestReady=false;guestRequested=false;restoreHostPlayer();return;}
        guestRequested=(latest.flags()&16)!=0;
        if(hostPlayer!=mc.player) {
            Checkpoint point=checkpointFor(mc);
            boolean respawn=point!=null && (pendingRespawn==point || hostPlayer!=null && hostPlayer.isDeadOrDying());
            restoreHostPlayer();hostPlayer=mc.player;savedNoGravity=hostPlayer.isNoGravity();savedVelocity=hostPlayer.getDeltaMovement();
            hostPlayerId=hostPlayer.getUUID();nextHostSync=0;snapped=false;
            if(respawn) {hostPlayer.setPos(point.feet.x,point.feet.y,point.feet.z);hostPlayer.setDeltaMovement(Vec3.ZERO);hostPlayer.resetFallDistance();queueServerSnap(point.feet);pendingRespawn=null;}
        }
        if(anchor==null || anchor.epoch!=latest.hostEpoch()) {
            if(!validCoordinates(mc.player,mc.player.position())) {
                Checkpoint point=mirrorSandbox()?checkpointFor(mc):null;
                if(point!=null && !mc.player.isDeadOrDying()) {
                    // Recovery is limited to establishing a new transform, not
                    // to each tick of ordinary movement or missing coverage.
                    if(coordinateRecovery!=point) {
                        coordinateRecovery=point;
                        point.server.execute(()->{
                            var player=point.server.getPlayerList().getPlayer(point.player);
                            if(player!=null && player.level().dimension().equals(point.dimension)
                                    && validCoordinates(player,point.feet)) {
                                player.teleportTo(point.feet.x,point.feet.y,point.feet.z);
                                player.setDeltaMovement(Vec3.ZERO);player.resetFallDistance();
                            }
                        });
                        LOG.warn("Rejected out-of-build-range host anchor; restoring mirror checkpoint {} with one acknowledged teleport",point.feet);
                    }
                    guestReady=false;mc.player.setNoGravity(true);mc.player.setDeltaMovement(Vec3.ZERO);mc.player.resetFallDistance();
                    return; // The real server position packet establishes the safe new anchor.
                } else {
                    guestReady=false;mc.player.setNoGravity(true);mc.player.setDeltaMovement(Vec3.ZERO);mc.player.resetFallDistance();
                    if(coordinateHold==null)LOG.warn("Host anchor waiting: Minecraft feet {} outside safe dimension build range [{}, {})",mc.player.position(),mc.level.getMinY(),mc.level.getMaxY());
                    coordinateHold="anchor outside dimension build range";return;
                }
            }
            coordinateHold=null;coordinateRecovery=null;
            anchor=new Anchor(latest.hostEpoch(),mc.player.position(),new Vec3(latest.hostX(),latest.hostY(),latest.hostZ()));snapped=false;
            LOG.info("Bridge anchor epoch {} at Minecraft {} / host {}",anchor.epoch,anchor.minecraft,anchor.host);
        }
        if(guestRequested) {
            clearTraversal();
            boolean coordinatesValid=validCoordinates(mc.player,mc.player.position());
            boolean ready=coordinatesValid && HostCollision.covered(mc.player.position());
            if(ready && !snapped) {
                double ground=HostCollision.groundAt(mc.player.position(),1.5);
                if(Double.isFinite(ground) && Math.abs(ground-mc.player.getY())<=2) {
                    mc.player.setPos(mc.player.getX(),ground,mc.player.getZ());hostGrounded=true;mc.player.setOnGround(true);
                    mc.player.setDeltaMovement(Vec3.ZERO);mc.player.resetFallDistance();queueServerSnap(mc.player.position());
                }
                snapped=true;
            }
            if(guestReady!=ready)LOG.info("Minecraft movement authority {} (terrain watchdog: {})",ready?"ready":"waiting",coordinatesValid?HostCollision.coverageStatus(mc.player.position()):"feet outside dimension build range");
            guestReady=ready;mc.player.setNoGravity(!ready);
            if(!ready) {mc.player.setDeltaMovement(Vec3.ZERO);mc.player.resetFallDistance();}
            // No position, velocity, health or animation overrides while vanilla owns movement.
        } else {
            guestReady=false;mc.player.setNoGravity(true);
            Vec3 p=hostToMinecraft(latest.hostX(),latest.hostY(),latest.hostZ());
            mc.player.setPos(p.x,p.y,p.z);hostGrounded=HostCollision.grounded(p);mc.player.setOnGround(hostGrounded);
            mc.player.setDeltaMovement(Vec3.ZERO);mc.player.resetFallDistance();
            if(NativeTraversalReceipt.traversalInput(latest.flags()))synchronizeTraversal(mc,p);
            else {clearTraversal();if(System.nanoTime()>=nextHostSync) {nextHostSync=System.nanoTime()+50_000_000L;queueServerSnap(p);}}
        }
        if(NativeTraversalReceipt.traversalInput(latest.flags()))VirtualInput.releaseAll(mc);
        else VirtualInput.apply(mc,latest,false);
    }
    private static void restoreHostPlayer() {
        hostPlayerId=null;hostGrounded=false;serverSnap=null;clearTraversal();
        if(hostPlayer!=null) {hostPlayer.setNoGravity(savedNoGravity);hostPlayer.setDeltaMovement(savedVelocity);}
        hostPlayer=null;savedVelocity=null;nextHostSync=0;gaitAnchored=false;
    }
    private static void updateGait(Minecraft mc) {
        if(mirrorSandbox() && mc.player!=null && !mc.player.isDeadOrDying() && guestReady()
                && validCoordinates(mc.player,mc.player.position()) && HostCollision.covered(mc.player.position())
                && (HostCollision.grounded(mc.player.position()) || mc.player.onGround()
                    && !mc.level.noCollision(mc.player,mc.player.getBoundingBox().move(0,-.1,0))))
            checkpoint=new Checkpoint(mc.player.getUUID(),mc.player.level().dimension(),mc.getSingleplayerServer(),mc.player.position());
        if(guestRequested || !active || hostPlayer==null || latest==null || mc.player==null) {gaitAnchored=false;return;}
        hostGrounded=HostCollision.grounded(mc.player.position());mc.player.setOnGround(hostGrounded);
        if(!gaitAnchored || gaitEpoch!=latest.hostEpoch()) {gaitX=latest.hostX();gaitZ=latest.hostZ();gaitEpoch=latest.hostEpoch();gaitAnchored=true;}
        double distance=Math.hypot(latest.hostX()-gaitX,latest.hostZ()-gaitZ);gaitX=latest.hostX();gaitZ=latest.hostZ();
        mc.player.walkAnimation.update(distance>5?0:(float)Math.min(distance*4,1),.4f,1);
    }
    public static void afterRender(Minecraft mc) {
        if(!SERVER.connected())return;
        var p=mc.player;ByteBuffer state=Wire.buffer(80);int cameraMode=mc.options.getCameraType().ordinal();
        if(p==null) {state.put(new byte[64]).putLong(tick++).putInt(cameraMode).putInt(0);SERVER.publishState(state.array());return;}
        int flags=1|(mc.gui.screen()!=null?2:0)|(p.onGround()?4:0)|(p.isCrouching()?8:0)|(p.isSprinting()?16:0)
                |(p.isSwimming()?32:0)|(p.isDeadOrDying()?64:0);
        state.putDouble(p.getX()).putDouble(p.getY()).putDouble(p.getZ()).putFloat(p.getYRot()).putFloat(p.getXRot())
                .putFloat(p.getHealth()).putFloat(p.getMaxHealth()).putFloat(p.getFoodData().getFoodLevel())
                .putFloat(p.getFoodData().getSaturationLevel()).putFloat(p.walkAnimation.position()).putFloat(p.walkAnimation.speed())
                .putInt(flags).putInt(p.getInventory().getSelectedSlot()).putLong(p.tickCount).putInt(cameraMode).putInt(0);
        SERVER.publishState(state.array());
        if(hostAnchored())GuestMotion.capture(mc);
        if(active) {
            if(avatar==null)avatar=new AvatarCapture(SERVER);avatar.capture(mc);
            if(scene==null)scene=new SceneCapture(SERVER,avatar);scene.capture(mc);overlay.capture(mc);
            GuestBlocks.capture(mc,scene);
            if(entities==null)entities=new EntityCapture(SERVER,avatar);entities.capture(mc);
        }
    }
}
