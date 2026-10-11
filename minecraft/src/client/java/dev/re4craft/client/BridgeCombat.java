package dev.re4craft.client;

import dev.re4craft.protocol.Wire;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.*;
import java.util.concurrent.ConcurrentLinkedQueue;
import net.fabricmc.fabric.api.client.rendering.v1.EntityRendererRegistry;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerTickEvents;
import net.fabricmc.fabric.api.object.builder.v1.entity.FabricDefaultAttributeRegistry;
import net.minecraft.client.renderer.entity.NoopRenderer;
import net.minecraft.core.Registry;
import net.minecraft.core.registries.*;
import net.minecraft.resources.*;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.world.entity.*;
import net.minecraft.world.phys.Vec3;

/** Real Minecraft weapons and defense act on invisible stand-ins for actual RE4 actors. */
final class BridgeCombat {
    private static final ResourceKey<EntityType<?>> KEY=ResourceKey.create(Registries.ENTITY_TYPE,Identifier.fromNamespaceAndPath("re4craft","host_actor"));
    static final EntityType<HostActorEntity> TYPE=Registry.register(BuiltInRegistries.ENTITY_TYPE,KEY,
            EntityType.Builder.<HostActorEntity>of(HostActorEntity::new,MobCategory.MISC).sized(.6f,1.8f)
                    .noSave().noSummon().noLootTable().clientTrackingRange(10).updateInterval(1).build(KEY));
    private record Actor(int id,int flags,Vec3 position,float minX,float minY,float minZ,float maxX,float maxY,float maxZ,float yaw,int kind) { }
    private record Snapshot(int epoch,List<Actor> actors,long received) { }
    private static volatile Snapshot snapshot;
    private static final ConcurrentLinkedQueue<byte[]> damages=new ConcurrentLinkedQueue<>();
    private static final Map<Integer,HostActorEntity> proxies=new HashMap<>();
    private static final LinkedHashSet<Long> seenDamage=new LinkedHashSet<>();
    private static int eventId,proxyEpoch;
    static void register() {
        FabricDefaultAttributeRegistry.register(TYPE,LivingEntity.createLivingAttributes());
        ServerTickEvents.END_SERVER_TICK.register(BridgeCombat::serverTick);
    }
    static void init() {EntityRendererRegistry.register(TYPE,NoopRenderer::new);}
    static void reset() {snapshot=null;damages.clear();}
    static void accept(Wire.Packet packet) throws IOException {
        if(packet.type()==Wire.DAMAGE) {Wire.damage(packet);if(damages.size()<256)damages.add(packet.payload());return;}
        Wire.enemies(packet);ByteBuffer b=ByteBuffer.wrap(packet.payload()).order(ByteOrder.LITTLE_ENDIAN);
        int epoch=b.getInt(),count=b.getInt();b.getLong();List<Actor> actors=new ArrayList<>(count);
        for(int i=0;i<count;i++) {
            int id=b.getInt(),flags=b.getInt();Vec3 pos=new Vec3(b.getDouble(),b.getDouble(),b.getDouble());
            float x0=b.getFloat(),y0=b.getFloat(),z0=b.getFloat(),x1=b.getFloat(),y1=b.getFloat(),z1=b.getFloat();
            b.getFloat();b.getFloat();float yaw=b.getFloat();int kind=b.getInt();
            actors.add(new Actor(id,flags,pos,x0,y0,z0,x1,y1,z1,yaw,kind));
        }
        snapshot=new Snapshot(epoch,List.copyOf(actors),System.nanoTime());
    }
    private static void removeAll() {proxies.values().forEach(Entity::discard);proxies.clear();}
    private static void serverTick(MinecraftServer server) {
        final int serverEpoch=Re4CraftClient.hostEpoch();
        if(proxyEpoch!=serverEpoch) {removeAll();proxyEpoch=serverEpoch;}
        var uuid=Re4CraftClient.hostPlayerId();ServerPlayer player=uuid==null?null:server.getPlayerList().getPlayer(uuid);
        if(player==null || !Re4CraftClient.hostAnchored() || !Re4CraftClient.guestRequested()) {removeAll();return;}
        // The retained epoch anchor also retains vanilla fire timers during focus/coverage holds.
        if(!Re4CraftClient.guestReady())return;
        Snapshot s=snapshot;
        if(s==null || s.epoch!=serverEpoch || System.nanoTime()-s.received>1_500_000_000L)removeAll();
        else {
            HashSet<Integer> live=new HashSet<>();
            for(Actor a:s.actors) {
                if((a.flags&Wire.ACTOR_ALIVE)==0 || (a.flags&Wire.ACTOR_DEAD)!=0)continue;
                live.add(a.id);HostActorEntity proxy=proxies.get(a.id);
                if(proxy!=null && (proxy.isRemoved() || proxy.level()!=player.level())) {proxy.discard();proxies.remove(a.id);proxy=null;}
                Vec3 position=Re4CraftClient.hostToMinecraft(a.position.add((a.minX+a.maxX)/2,a.minY,(a.minZ+a.maxZ)/2));
                if(proxy==null) {
                    proxy=new HostActorEntity(TYPE,player.level());proxy.setFormId(a.id);
                    proxy.snapTo(position.x,position.y,position.z,a.yaw,0);
                    if(!player.level().addFreshEntity(proxy))continue;proxies.put(a.id,proxy);
                } else {proxy.setPos(position.x,position.y,position.z);proxy.setYRot(a.yaw);proxy.setYHeadRot(a.yaw);}
                proxy.setSize(Math.max(.1f,a.maxX-a.minX),Math.max(.1f,a.maxY-a.minY),Math.max(.1f,a.maxZ-a.minZ));
                proxy.setBreakable((a.flags&Wire.ACTOR_BREAKABLE)!=0);
                proxy.setHostile(dev.re4craft.world.NativeHostility.hostile(a.kind,a.flags));
            }
            for(var iterator=proxies.entrySet().iterator();iterator.hasNext();) {
                var e=iterator.next();if(!live.contains(e.getKey())) {e.getValue().discard();iterator.remove();}
            }
        }
        byte[] raw;
        while((raw=damages.poll())!=null) {
            if(Re4CraftClient.hostEpoch()!=serverEpoch) {removeAll();return;}
            ByteBuffer b=ByteBuffer.wrap(raw).order(ByteOrder.LITTLE_ENDIAN);
            int epoch=b.getInt(),event=b.getInt();float amount=b.getFloat();int kind=b.getInt(),attackerId=b.getInt(),flags=b.getInt();
            double sourceX=b.getDouble(),sourceY=b.getDouble(),sourceZ=b.getDouble();
            if(epoch!=serverEpoch || !player.isAlive())continue;
            Vec3 sourcePosition=(flags&HostDamageBridge.HAS_POSITION)!=0
                    ?Re4CraftClient.hostToMinecraftForEpoch(epoch,sourceX,sourceY,sourceZ):null;
            // Never turn a stale directional strike into an anonymous hit
            // simply because the room transform changed during this tick.
            if((flags&HostDamageBridge.HAS_POSITION)!=0&&sourcePosition==null)continue;
            long identity=((long)epoch<<32)|Integer.toUnsignedLong(event);
            if(!seenDamage.add(identity))continue;if(seenDamage.size()>2048)seenDamage.remove(seenDamage.iterator().next());
            var source=HostDamageBridge.source(player,kind,flags,sourcePosition,proxies.get(attackerId));
            var observation=ShieldFixture.beforeDamage(player,event);
            float before=player.getHealth();player.hurtServer(player.level(),source,amount);
            Re4CraftClient.LOG.info("RE4 damage {} -> vanilla health {} to {}",amount,before,player.getHealth());
            ShieldFixture.afterDamage(player,event,kind,flags,source,observation);
        }
        for(HostActorEntity proxy:proxies.values()) {
            HostActorEntity.Hit hit;
            while((hit=proxy.takeHit())!=null) {
            if(Re4CraftClient.hostEpoch()!=serverEpoch) {removeAll();return;}
            if(hit.damage()<=0)continue;
            Vec3 origin=Re4CraftClient.minecraftToHost(hit.origin()),direction=hit.direction();
            var b=Wire.buffer(64).putInt(serverEpoch).putInt(proxy.formId()).putInt(++eventId)
                    .putInt(hit.flags()).putFloat(hit.damage()).putFloat(hit.knockback());
            b.putDouble(origin.x).putDouble(origin.y).putDouble(origin.z).putFloat((float)direction.x).putFloat((float)direction.y)
                    .putFloat((float)direction.z).putFloat(hit.reach());
            if(!Re4CraftClient.SERVER.publishMelee(b.array()))Re4CraftClient.LOG.warn("Melee transport queue full");
            else Re4CraftClient.LOG.info("Vanilla hit RE4 actor {} for {} (flags {})",proxy.formId(),hit.damage(),hit.flags());
            }
        }
    }
}
