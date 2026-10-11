package dev.re4craft.client;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import net.minecraft.client.Minecraft;
import net.minecraft.core.BlockPos;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.world.entity.ExperienceOrb;
import net.minecraft.world.entity.projectile.throwableitemprojectile.ThrownExperienceBottle;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.Items;
import net.minecraft.world.phys.AABB;
import net.minecraft.world.phys.Vec3;

/** Real bottle impact, imported-floor support and vanilla XP pickup in a disposable review save. */
final class LootExperienceFixture {
    private static final String TAG="re4craft:loot_experience_qa_v1";
    private static final int MAX_TICKS=180;
    private static final List<ExperienceOrb> orbs=new ArrayList<>();
    private static final Set<Integer> existing=new HashSet<>();
    private static MinecraftServer owner;
    private static UUID playerId;
    private static ThrownExperienceBottle bottle;
    private static Vec3 site;
    private static int generation,epoch,started,checks,restTicks,xpBefore;
    private static boolean captured,pickup,done;

    static void tick(MinecraftServer server) {
        String phase=System.getProperty("re4craft.lootProgressionTest","");
        if(done||!(phase.equals("seed")||phase.equals("check"))||!Boolean.getBoolean("re4craft.backgroundTest"))return;
        requirePrivateProfile();
        var uuid=Re4CraftClient.hostPlayerId();
        var player=uuid==null?null:server.getPlayerList().getPlayer(uuid);
        if(owner==null) {
            if(server.getTickCount()<50||player==null||!Re4CraftClient.guestReady()||!Re4CraftClient.hostAnchored())return;
            // Outside ordinary XP attraction range: first establish actual terrain contact,
            // then move only these fixture orbs into pickup bounds, without moving the player.
            Vec3 probe=player.position().add(11,0,0);
            if(!HostCollision.covered(probe))return;
            double ground=HostCollision.groundAt(probe,1);
            if(!Double.isFinite(ground))return;
            try {
                require(player.level().getBlockState(BlockPos.containing(probe.x,ground-.05,probe.z)).isAir(),"XP support is imported terrain with no synthetic Minecraft floor");
                owner=server;playerId=uuid;generation=Re4CraftClient.SERVER.generation();epoch=Re4CraftClient.hostEpoch();
                started=server.getTickCount();xpBefore=player.totalExperience;site=new Vec3(probe.x,ground,probe.z);
                for(var orb:nearby(player))existing.add(orb.getId());
                bottle=new ThrownExperienceBottle(player.level(),site.x,site.y+1.5,site.z,new ItemStack(Items.EXPERIENCE_BOTTLE));
                bottle.setOwner(player);bottle.addTag(TAG);bottle.setDeltaMovement(0,-.15,0);
                require(Re4CraftClient.physicsEntity(bottle),"real thrown bottle uses host terrain physics");
                require(player.level().addFreshEntity(bottle),"real thrown experience bottle spawned");
            } catch(RuntimeException failure) {fail(phase,failure);}
            return;
        }
        if(owner!=server||player==null||!player.getUUID().equals(playerId)||generation!=Re4CraftClient.SERVER.generation()
                ||epoch!=Re4CraftClient.hostEpoch()||!Re4CraftClient.SERVER.connected()) {
            cleanup();done=true;
            Re4CraftClient.LOG.error("LOOT_EXPERIENCE_QA phase={} passed=false cancelled=connection_or_player_scope checks={}",phase,checks);
            return;
        }
        try {
            require(server.getTickCount()-started<=MAX_TICKS,"bottle impact, floor support and pickup finish within nine seconds");
            if(!Re4CraftClient.guestReady()||!HostCollision.covered(site))return;
            if(!captured) {
                if(bottle.isAlive()) {
                    require(bottle.getY()>=site.y-.05,"actual experience bottle cannot fall through imported floor");
                    return;
                }
                for(var orb:nearby(player))if(!existing.contains(orb.getId())) {orb.addTag(TAG);orbs.add(orb);}
                require(!orbs.isEmpty(),"original experience-bottle impact spawned real XP orbs");
                require(orbs.stream().allMatch(orb->orb.isAlive()&&orb.getValue()>0&&Re4CraftClient.physicsEntity(orb)),"all real XP orbs participate in host terrain physics");
                captured=true;
            }
            if(!pickup) {
                require(orbs.stream().anyMatch(ExperienceOrb::isAlive),"original bottle orbs remain before pickup (vanilla merging is allowed)");
                boolean resting=true;
                for(var orb:orbs) {
                    if(!orb.isAlive())continue;
                    double ground=HostCollision.groundAt(orb.position(),.35);
                    require(Double.isFinite(ground)&&orb.getY()>=ground-.03,"actual XP orbs stay above imported ground");
                    resting&=orb.onGround()&&HostCollision.grounded(orb.position())&&Math.abs(orb.getY()-ground)<.03;
                }
                restTicks=resting?restTicks+1:0;
                if(restTicks<5)return;
                require(player.totalExperience==xpBefore,"orbs were not picked up before terrain contact was proven");
                // This is fixture placement only. Original entity ticks/Player touch
                // perform every XP credit; no giveExperience* or synthetic orb is used.
                for(var orb:orbs) {orb.setPos(player.position().add(.1,.1,0));orb.setDeltaMovement(Vec3.ZERO);}
                pickup=true;
                return;
            }
            if(orbs.stream().anyMatch(ExperienceOrb::isAlive))return;
            int received=player.totalExperience-xpBefore;
            require(received>=3&&received<=11,"vanilla pickup credits the original bottle's three to eleven XP points");
            done=true;
            Re4CraftClient.LOG.info("LOOT_EXPERIENCE_QA phase={} passed=true checks={} actualBottleImpact=true importedGround=true groundedTicks={} vanillaPickup=true xpBefore={} xpAfter={} received={} epoch={}",
                    phase,checks,restTicks,xpBefore,player.totalExperience,received,epoch);
            cleanup();
        } catch(RuntimeException failure) {fail(phase,failure);}
    }

    private static List<ExperienceOrb> nearby(ServerPlayer player) {
        return player.level().getEntitiesOfClass(ExperienceOrb.class,AABB.ofSize(site,10,8,10));
    }
    private static void requirePrivateProfile() {
        Path game=Minecraft.getInstance().gameDirectory.toPath().toAbsolutePath().normalize(),lab=game.getParent();
        boolean disposable=game.getFileName().toString().equals(".instance")&&lab!=null&&lab.getFileName().toString().matches("[a-f0-9]{32}")
                &&lab.getParent()!=null&&lab.getParent().getFileName().toString().equals("release-review")
                &&lab.getParent().getParent()!=null&&lab.getParent().getParent().getFileName().toString().equals("build");
        if(!disposable)throw new IllegalStateException("Loot experience QA refused outside a disposable review instance");
    }
    private static void require(boolean ok,String condition) {checks++;if(!ok)throw new IllegalStateException(condition);}
    private static void cleanup() {
        if(bottle!=null&&bottle.isAlive())bottle.discard();
        for(var orb:orbs)if(orb.isAlive())orb.discard();
        orbs.clear();existing.clear();
    }
    private static void fail(String phase,RuntimeException failure) {
        done=true;cleanup();Re4CraftClient.LOG.error("LOOT_EXPERIENCE_QA phase={} passed=false checks={}",phase,checks,failure);
        throw failure;
    }
}
