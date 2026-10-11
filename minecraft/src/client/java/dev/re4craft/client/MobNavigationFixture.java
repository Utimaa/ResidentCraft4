package dev.re4craft.client;

import java.util.*;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerTickEvents;
import net.minecraft.client.Minecraft;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.world.entity.*;
import net.minecraft.world.entity.ai.goal.Goal;
import net.minecraft.world.entity.animal.wolf.Wolf;
import net.minecraft.world.phys.Vec3;

/** Actual vanilla AI/physics observations, exclusively in an explicitly named disposable world. */
final class MobNavigationFixture {
    private static final Map<String,Mob> mobs=new LinkedHashMap<>();
    private static MinecraftServer prepared;
    private static int started;
    private static boolean zombieAdded;
    private static boolean blockWallAdded;
    static void init() {ServerTickEvents.END_SERVER_TICK.register(MobNavigationFixture::tick);}
    private static boolean enabled() {
        return Boolean.getBoolean("re4craft.mobNavigationTest") && Boolean.getBoolean("re4craft.physicsTest")
                && System.getProperty("re4craft.worldName","").startsWith("RE4Craft_MobQA_")
                && Minecraft.getInstance().gameDirectory.toPath().toAbsolutePath().normalize().getFileName().toString().equals(".qa-void-instance");
    }
    private static <T extends Mob> T spawn(ServerLevel level,EntityType<T> type,String label,double x,double y,double z) {
        T mob=type.create(level,EntitySpawnReason.COMMAND);
        if(mob==null)throw new IllegalStateException("Mob QA entity creation failed");
        Vec3 at=Re4CraftClient.hostToMinecraft(x,y,z);mob.snapTo(at.x,at.y,at.z,0,0);mob.setPersistenceRequired();
        mob.entityTags().add("RE4CRAFT_MOB_NAV_QA");
        if(!level.addFreshEntity(mob))throw new IllegalStateException("Mob QA spawn failed");
        mobs.put(label,mob);return mob;
    }
    private static void route(Mob mob,Vec3 hostDestination) {
        Vec3 destination=Re4CraftClient.hostToMinecraft(hostDestination);
        mob.getGoalSelector().removeAllGoals(goal->true);
        mob.getGoalSelector().addGoal(0,new Goal() {
            private int refresh;
            {setFlags(EnumSet.of(Flag.MOVE));}
            @Override public boolean canUse() {return mob.position().distanceToSqr(destination)>.4;}
            @Override public boolean canContinueToUse() {return canUse();}
            @Override public void tick() {if(++refresh%5==1)mob.getNavigation().moveTo(destination.x,destination.y,destination.z,1);}
            @Override public void stop() {mob.getNavigation().stop();}
        });
    }
    private static void tick(MinecraftServer server) {
        if(!enabled() || server.getPlayerList().getPlayers().isEmpty())return;
        ServerPlayer player=server.getPlayerList().getPlayers().getFirst();ServerLevel level=player.level();
        if(prepared!=server && Re4CraftClient.guestReady()) {
            prepared=server;started=server.getTickCount();zombieAdded=false;blockWallAdded=false;mobs.clear();
            route(spawn(level,EntityTypes.COW,"cow",-6,0,-3),new Vec3(8,0,-3));
            route(spawn(level,EntityTypes.PIG,"pig",-6,0,10),new Vec3(6,2,10));
            Wolf wolf=spawn(level,EntityTypes.WOLF,"owned_wolf",-6,0,4);wolf.tame(player);wolf.setOrderedToSit(false);
            Wolf friend=spawn(level,EntityTypes.WOLF,"friendly_wolf",-5,0,-8);friend.tame(player);friend.setOrderedToSit(true);
            spawn(level,EntityTypes.WOLF,"wild_wolf",-4,0,-8);
            // Retain these two mobs' original goals; commanded routes alone do
            // not verify vanilla random destination selection in the void mirror.
            spawn(level,EntityTypes.COW,"wander_cow",-18,0,18);
            spawn(level,EntityTypes.PIG,"wander_pig",-18,0,-18);
            Re4CraftClient.LOG.info("MOB_NAV_QA prepared actual cow, pig and tame/wild wolves; no floor blocks or authored paths");
        }
        if(prepared!=server)return;
        int elapsed=server.getTickCount()-started;
        if(elapsed==40) {
            Mob pig=mobs.get("pig");
            for(int x=-1;x<7;x++)for(int y=0;y<4;y++) {
                Vec3 at=Re4CraftClient.hostToMinecraft(x+.5,y,10.5);var cell=net.minecraft.core.BlockPos.containing(at);
                var type=HostMobNavigation.nodeType(pig,cell.getX(),cell.getY(),cell.getZ(),net.minecraft.world.level.pathfinder.PathType.OPEN);
                double floor=HostMobNavigation.floor(pig,at,1.001);
                Re4CraftClient.LOG.info("MOB_NAV_QA ramp_node x={} y={} type={} floor={} body={}",x,y,type,
                        Double.isFinite(floor)?Re4CraftClient.minecraftToHost(new Vec3(at.x,floor,at.z)).y:Double.NaN,
                        Double.isFinite(floor)&&HostCollision.navigationBody(pig,new Vec3(at.x,floor,at.z)));
            }
            for(int x=-1;x<6;x++) {
                double fy=x<0?0:Math.ceil((x+.5)/3),ty=Math.ceil((x+1.5)/3);
                Re4CraftClient.LOG.info("MOB_NAV_QA ramp_edge x={} fromY={} toY={} allowed={}",x,fy,ty,
                        HostMobNavigation.edge(pig,Re4CraftClient.hostToMinecraft(x+.5,fy,10.5),Re4CraftClient.hostToMinecraft(x+1.5,ty,10.5)));
            }
            Wolf owned=(Wolf)mobs.get("owned_wolf");
            for(HostActorEntity actor:level.getEntitiesOfClass(HostActorEntity.class,player.getBoundingBox().inflate(64)))
                Re4CraftClient.LOG.info("MOB_NAV_QA owner_defense_filter id={} hostile={} wantsToAttack={}",actor.formId(),actor.isHostile(),owned.wantsToAttack(actor,player));
        }
        if(elapsed>=450 && !blockWallAdded && Re4CraftClient.guestReady()) {
            for(int x=7;x<10;x++)for(int y=0;y<2;y++) {
                Vec3 at=Re4CraftClient.hostToMinecraft(x,y,-6);
                level.setBlock(net.minecraft.core.BlockPos.containing(at),net.minecraft.world.level.block.Blocks.STONE.defaultBlockState(),3);
            }
            route(mobs.get("cow"),new Vec3(8,0,-9));blockWallAdded=true;
            Re4CraftClient.LOG.info("MOB_NAV_QA real_block_wall: six actual stone blocks, redirecting cow with vanilla moveTo");
        }
        if(elapsed>=500 && !zombieAdded && Re4CraftClient.guestReady()) {
            Mob owned=mobs.get("owned_wolf");Vec3 host=Re4CraftClient.minecraftToHost(owned.position());
            Mob zombie=spawn(level,EntityTypes.ZOMBIE,"zombie",host.x+3,host.y,host.z);zombie.setNoAi(true);
            // A real pumpkin protects this independent vanilla target from sunlight.
            zombie.setItemSlot(EquipmentSlot.HEAD,new net.minecraft.world.item.ItemStack(net.minecraft.world.item.Items.CARVED_PUMPKIN));zombieAdded=true;
        }
        if(elapsed%10==0)for(var entry:mobs.entrySet()) {
            Mob mob=entry.getValue();Vec3 host=Re4CraftClient.minecraftToHost(mob.position());
            double support=HostCollision.groundAt(mob.position(),.1);
            var path=mob.getNavigation().getPath();LivingEntity target=mob.getTarget();
            String targetLabel=target instanceof HostActorEntity actor?"host_"+actor.formId():target==null?"none":BuiltInRegistries.ENTITY_TYPE.getKey(target.getType()).toString();
            Re4CraftClient.LOG.info("MOB_NAV_QA sample label={} elapsed={} ready={} x={} y={} z={} vy={} grounded={} supported={} mobTicks={} pathNodes={} pathReach={} target={} health={}",
                    entry.getKey(),elapsed,Re4CraftClient.guestReady(),host.x,host.y,host.z,mob.getDeltaMovement().y,mob.onGround(),
                    Double.isFinite(support)&&Math.abs(support-mob.getY())<.15,mob.tickCount,path==null?0:path.getNodeCount(),path!=null&&path.canReach(),targetLabel,mob.getHealth());
        }
        if(elapsed>800) {Re4CraftClient.LOG.info("MOB_NAV_QA finished; saving disposable world");Minecraft mc=Minecraft.getInstance();mc.execute(mc::stop);}
    }
}
