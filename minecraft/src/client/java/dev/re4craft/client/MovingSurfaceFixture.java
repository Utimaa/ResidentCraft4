package dev.re4craft.client;

import dev.re4craft.protocol.Wire;
import java.nio.ByteBuffer;
import java.util.UUID;
import net.minecraft.client.Minecraft;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.EntitySpawnReason;
import net.minecraft.world.entity.EntityType;
import net.minecraft.world.entity.EntityTypes;
import net.minecraft.world.entity.Mob;
import net.minecraft.world.entity.MoverType;
import net.minecraft.world.phys.Vec3;

/** Explicit disposable imported-mesh lab; never enabled in a campaign profile.
 * The fake host supplies ordinary input, then leaves terrain to this fixture. */
final class MovingSurfaceFixture {
    private static final long PLATFORM=0x4d4f56494e47L,GROUND=0x47524f554e44L;
    private static int tick,checks,failures;private static boolean started,finished;
    private static volatile boolean serverPrepared;
    private static UUID cow,zombie;
    private static Vec3 leaveStart,staleStart;
    private static double x,y,z,angle;
    private static boolean sawAirborne,sawFall,sawHold;
    private static boolean enabled(Minecraft mc) {
        return Boolean.getBoolean("re4craft.movingSurfaceTest")&&Boolean.getBoolean("re4craft.physicsTest")
                && System.getProperty("re4craft.worldName","").startsWith("RE4Craft_MovingQA_")
                && mc.gameDirectory.toPath().toAbsolutePath().normalize().getFileName().toString().equals(".qa-void-instance");
    }
    private static synchronized void check(boolean valid,String label) {
        checks++;if(!valid){failures++;Re4CraftClient.LOG.error("MOVING_SURFACE_QA FAIL tick={} check={}",tick,label);}
    }
    private static void floor(ByteBuffer b,float half,float height){
        for(float v:new float[]{-half,height,-half,-half,height,half,half,height,-half,half,height,-half,-half,height,half,half,height,half})b.putFloat(v);
    }
    private static void installTerrain() {
        ByteBuffer b=Wire.buffer(32+4*48);b.putInt(Re4CraftClient.hostEpoch()).putInt(4).putInt(5).putInt(0x4d51).putFloat(0).putFloat(0).putFloat(0).putFloat(64);
        floor(b,4,0);floor(b,32,-2);b.putLong(PLATFORM).putInt(0).putLong(PLATFORM).putInt(1).putLong(GROUND).putInt(0).putLong(GROUND).putInt(1);
        HostCollision.accept(b.array());
    }
    private static void transform(boolean removed) {
        ByteBuffer b=Wire.buffer(84);b.putInt(Re4CraftClient.hostEpoch()).putInt(0x4d51).putInt(1).putInt(0).putLong(tick+1).putLong(PLATFORM).putInt(removed?1:0);
        float c=(float)Math.cos(angle),s=(float)Math.sin(angle);
        for(float v:new float[]{c,0,-s,(float)x,0,1,0,(float)y,s,0,c,(float)z})b.putFloat(v);
        HostCollision.acceptMoving(b.array());
    }
    private static Vec3 expected(double localX,double localZ){return new Vec3(x+Math.cos(angle)*localX-Math.sin(angle)*localZ,y,z+Math.sin(angle)*localX+Math.cos(angle)*localZ);}
    private static Mob spawn(net.minecraft.server.level.ServerLevel level,EntityType<? extends Mob> type,double px,double pz){
        Mob entity=type.create(level,EntitySpawnReason.COMMAND);if(entity==null)throw new IllegalStateException("Moving surface fixture entity missing");
        Vec3 at=Re4CraftClient.hostToMinecraft(px,0,pz);entity.setPos(at.x,at.y,at.z);entity.setDeltaMovement(Vec3.ZERO);entity.setOnGround(true);entity.setNoAi(true);entity.setPersistenceRequired();entity.setSilent(true);
        entity.entityTags().add("RE4CRAFT_MOVING_QA");if(!level.addFreshEntity(entity))throw new IllegalStateException("Moving surface fixture entity rejected");return entity;
    }
    static void tick(Minecraft mc) {
        if(!enabled(mc)||finished||mc.player==null||mc.getSingleplayerServer()==null||!Re4CraftClient.hostAnchored())return;
        var server=mc.getSingleplayerServer();UUID id=mc.player.getUUID();
        if(!started) {
            if(!Re4CraftClient.guestReady())return;started=true;installTerrain();
            server.execute(()->{var p=server.getPlayerList().getPlayer(id);if(p==null)return;Vec3 at=Re4CraftClient.hostToMinecraft(1,0,1);p.teleportTo(at.x,at.y,at.z);p.setDeltaMovement(Vec3.ZERO);p.setOnGround(true);p.resetFallDistance();
                cow=spawn(p.level(),EntityTypes.COW,-2,1).getUUID();zombie=spawn(p.level(),EntityTypes.ZOMBIE,2,-1).getUUID();serverPrepared=true;});
            Re4CraftClient.LOG.info("MOVING_SURFACE_QA started controlled imported SAT platform + lower landing floor; real LocalPlayer/ServerPlayer/Cow/Zombie; normal player movement packets retained");
        }
        if(!serverPrepared)return;tick++;
        if(tick%60==0)installTerrain(); // renew the independent five-second full-mesh watchdog
        // Keep each fixture in a deterministic stable transform for settlement,
        // then use50ms samples as the production dynamic SAT channel does.
        if(tick>=41&&tick<=80){x+=.05;y+=.0125;z+=.025;}
        if(tick>=81&&tick<=120)angle+=Math.toRadians(2);
        if(tick>=122&&tick<=140)x+=.04;
        if(tick>=142&&tick<=160)x+=.04;
        boolean removed=tick>=181;
        if(tick<201||tick>=221)transform(removed);
        Vec3 client=Re4CraftClient.minecraftToHost(mc.player.position());
        if(tick==121)mc.player.jumpFromGround();
        if(tick>=122&&tick<=140&&client.y>y+.25)sawAirborne=true;
        if(tick==141){check(sawAirborne,"actual vanilla jump left moving support");mc.player.move(MoverType.SELF,new Vec3(8,0,0));leaveStart=Re4CraftClient.minecraftToHost(mc.player.position());}
        if(tick==160){check(Math.abs(client.x-leaveStart.x)<.15,"disembarked player did not inherit later platform translation");Vec3 at=Re4CraftClient.hostToMinecraft(expected(1,1));
            server.execute(()->{var p=server.getPlayerList().getPlayer(id);if(p!=null){p.teleportTo(at.x,at.y,at.z);p.setDeltaMovement(Vec3.ZERO);p.setOnGround(true);p.resetFallDistance();}});}
        if(tick>=185&&tick<=200&&client.y<y-.3)sawFall=true;
        if(tick==200){check(sawFall,"removed original SAT registration released support");staleStart=mc.player.position();}
        if(tick>=208&&tick<=220&&!Re4CraftClient.guestReady()){sawHold=true;check(mc.player.position().distanceTo(staleStart)<.08,"expired moving geometry froze source physics");}
        if(tick==222){check(sawHold,"250ms moving geometry watchdog entered hold");check(Re4CraftClient.guestReady(),"fresh moving receipt resumed source physics");}
        if(tick%5==0){final int sample=tick;final Vec3 clientSample=client,expectedPlayer=expected(1,1),expectedCow=expected(-2,1),expectedZombie=expected(2,-1);final boolean ready=Re4CraftClient.guestReady();
            final boolean clientGrounded=mc.player.onGround();server.execute(()->{var p=server.getPlayerList().getPlayer(id);if(p==null)return;Entity c=p.level().getEntity(cow),q=p.level().getEntity(zombie);Vec3 sp=Re4CraftClient.minecraftToHost(p.position()),cp=c==null?null:Re4CraftClient.minecraftToHost(c.position()),zp=q==null?null:Re4CraftClient.minecraftToHost(q.position());
                Re4CraftClient.LOG.info("MOVING_SURFACE_QA sample tick={} ready={} client={} server={} expected={} cow={} cowExpected={} zombie={} zombieExpected={} grounded={} serverGrounded={}",sample,ready,clientSample,sp,expectedPlayer,cp,expectedCow,zp,expectedZombie,clientGrounded,p.onGround());
                if(sample>=50&&sample<=115){check(clientSample.distanceTo(expectedPlayer)<.18,"stationary player followed translated/rotated actual surface");check(sp.distanceTo(clientSample)<.22,"normal integrated-server player validation accepted platform carry");check(cp!=null&&cp.distanceTo(expectedCow)<.2,"actual stationary cow followed platform");check(zp!=null&&zp.distanceTo(expectedZombie)<.2,"actual stationary hostile zombie followed platform");}
            });
        }
        if(tick==230){finished=true;server.execute(()->Re4CraftClient.LOG.info("MOVING_SURFACE_QA RESULT {} checks={} failures={} airborne={} removedFall={} staleHold={} (controlled imported geometry; native RE4 carriage not exercised)",failures==0?"PASS":"FAIL",checks,failures,sawAirborne,sawFall,sawHold));}
    }
}
