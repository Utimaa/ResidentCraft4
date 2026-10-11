package dev.re4craft.client;

import com.mojang.serialization.Codec;
import com.mojang.serialization.codecs.RecordCodecBuilder;
import dev.re4craft.loot.LootLedger;
import dev.re4craft.loot.LootMilestones;
import dev.re4craft.loot.LootBookCycles;
import dev.re4craft.loot.ProgressionLoot;
import dev.re4craft.loot.LootPendingPolicy;
import dev.re4craft.loot.LootInbox;
import dev.re4craft.protocol.Wire;
import java.util.ArrayList;
import java.util.List;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerTickEvents;
import net.minecraft.core.BlockPos;
import net.minecraft.core.UUIDUtil;
import net.minecraft.core.component.DataComponents;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.core.registries.Registries;
import net.minecraft.resources.Identifier;
import net.minecraft.resources.ResourceKey;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.world.entity.item.ItemEntity;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.Items;
import net.minecraft.world.item.enchantment.ItemEnchantments;
import net.minecraft.world.level.saveddata.SavedData;
import net.minecraft.world.level.saveddata.SavedDataType;
import net.minecraft.world.phys.Vec3;

/** Verified host loot becomes ordinary, saved Minecraft ItemEntities. */
final class HostLoot {
    private static final LootInbox pending=new LootInbox();
    private static final Codec<LootLedger.Stamp> STAMP_CODEC=RecordCodecBuilder.create(instance->instance.group(
            Codec.LONG.fieldOf("session").forGetter(LootLedger.Stamp::session),
            Codec.INT.fieldOf("event").forGetter(LootLedger.Stamp::event)).apply(instance,LootLedger.Stamp::new));
    private static final Codec<LootMilestones.Stamp> MILESTONE_CODEC=RecordCodecBuilder.create(instance->instance.group(
            UUIDUtil.CODEC.fieldOf("player").forGetter(LootMilestones.Stamp::player),
            Codec.INT.fieldOf("tiers").forGetter(LootMilestones.Stamp::tiers)).apply(instance,LootMilestones.Stamp::new));
    private static final Codec<LootBookCycles.Stamp> BOOK_CYCLE_CODEC=RecordCodecBuilder.create(instance->instance.group(
            UUIDUtil.CODEC.fieldOf("player").forGetter(LootBookCycles.Stamp::player),
            Codec.INT.fieldOf("enemy").forGetter(LootBookCycles.Stamp::enemy),
            Codec.INT.fieldOf("crate").forGetter(LootBookCycles.Stamp::crate)).apply(instance,LootBookCycles.Stamp::new));
    static final Codec<LedgerData> CODEC=RecordCodecBuilder.create(instance->instance.group(
            STAMP_CODEC.listOf().fieldOf("sessions").forGetter(d->d.ledger.snapshot()),
            MILESTONE_CODEC.listOf().optionalFieldOf("milestones",List.of()).forGetter(d->d.milestones.snapshot()),
            BOOK_CYCLE_CODEC.listOf().optionalFieldOf("book_cycles",List.of()).forGetter(d->d.books.snapshot())).apply(instance,LedgerData::new));
    private static final SavedDataType<LedgerData> TYPE=new SavedDataType<>(Identifier.fromNamespaceAndPath("re4craft","host_loot"),LedgerData::new,CODEC,null);
    static final class LedgerData extends SavedData {
        final LootLedger ledger;
        final LootMilestones milestones;
        final LootBookCycles books;
        LedgerData(){ledger=new LootLedger();milestones=new LootMilestones();books=new LootBookCycles();}
        LedgerData(List<LootLedger.Stamp> stamps,List<LootMilestones.Stamp> claims,List<LootBookCycles.Stamp> bookCycles){ledger=new LootLedger(stamps);milestones=new LootMilestones(claims);books=new LootBookCycles(bookCycles);}
    }
    record Award(List<ItemEntity> entities,List<ProgressionLoot.Stack> rewards){ }
    private HostLoot() { }
    static void register(){ServerTickEvents.END_SERVER_TICK.register(HostLoot::serverTick);ServerTickEvents.END_SERVER_TICK.register(LootProgressionFixture::tick);ServerTickEvents.END_SERVER_TICK.register(LootExperienceFixture::tick);}
    static void reset(){pending.reset();}
    static void accept(Wire.Loot loot){if(!pending.accept(loot,System.nanoTime()))Re4CraftClient.LOG.warn("Minecraft host loot queue reached its bounded limit");}
    private static void serverTick(MinecraftServer server) {
        for(int count=0;count<16;++count){var event=pending.take(Re4CraftClient.hostEpoch(),Re4CraftClient.hostAnchored(),
                    Re4CraftClient.guestRequested(),Re4CraftClient.guestReady(),System.nanoTime());
            if(event==null)return;
            if(event.action()==LootPendingPolicy.Action.DROP)continue;
            if(event.action()==LootPendingPolicy.Action.WAIT)return;
            Wire.Loot loot=event.event().loot();
            var id=Re4CraftClient.hostPlayerId();var player=id==null?null:server.getPlayerList().getPlayer(id);
            if(player==null||!player.isAlive())continue;
            Vec3 mapped=Re4CraftClient.hostToMinecraftForEpoch(loot.epoch(),loot.x(),loot.y(),loot.z());if(mapped==null)continue;
            Vec3 position=mapped.add(0,.2,0);
            var level=player.level();
            if(!Re4CraftClient.validCoordinates(player,position)||position.distanceToSqr(player.position())>128*128
                    ||!level.hasChunkAt(BlockPos.containing(position)))continue;
            Award awarded=award(player,data(player),loot,position);
            if(awarded==null)continue;
            Re4CraftClient.LOG.info("Minecraft host loot: session={} event={} source={} tier={} actualStacks={} at={}",Long.toUnsignedString(loot.sessionId()),Integer.toUnsignedString(loot.eventId()),loot.source(),loot.tier(),awarded.rewards(),position);
        }
    }
    static LedgerData data(ServerPlayer player){return player.level().getDataStorage().computeIfAbsent(TYPE);}
    /** Called only on the integrated server; no inventory slot or equipment writes. */
    static Award award(ServerPlayer player,LedgerData saved,Wire.Loot loot,Vec3 position){
        return award(player,saved,loot,position,entity->player.level().addFreshEntity(entity));
    }
    static Award award(ServerPlayer player,LedgerData saved,Wire.Loot loot,Vec3 position,java.util.function.Predicate<ItemEntity> spawn){
        if(!saved.ledger.fresh(loot.sessionId(),loot.eventId()))return null;
        List<ProgressionLoot.Stack> rewards;List<ItemStack> items=new ArrayList<>();
        try{
            int bookIndex=saved.books.canTrack(player.getUUID())?saved.books.index(player.getUUID(),loot.source()):Integer.remainderUnsigned(loot.eventId(),7);
            rewards=ProgressionLoot.select(loot.source(),loot.tier(),loot.sessionId(),loot.eventId(),bookIndex);
            for(var reward:rewards)items.add(materialize(player,reward));
        }catch(IllegalArgumentException|IllegalStateException invalid){return null;}
        List<ItemEntity> spawned=new ArrayList<>();
        for(int n=0;n<items.size();++n){
            double angle=n*Math.PI*.5;ItemEntity entity=new ItemEntity(player.level(),position.x,position.y,position.z,items.get(n),Math.cos(angle)*.06,.15,Math.sin(angle)*.06);
            entity.setDefaultPickUpDelay();
            if(!spawn.test(entity)){spawned.forEach(ItemEntity::discard);return null;}spawned.add(entity);
        }
        saved.ledger.commit(loot.sessionId(),loot.eventId());
        if(loot.tier()>=ProgressionLoot.NETHERITE)saved.books.commit(player.getUUID(),loot.source());
        saved.setDirty();return new Award(List.copyOf(spawned),rewards);
    }
    static ItemStack materialize(ServerPlayer player,ProgressionLoot.Stack reward){
        var item=BuiltInRegistries.ITEM.getValue(Identifier.parse(reward.item()));
        if(item==null||item==Items.AIR)throw new IllegalArgumentException("Missing Minecraft loot item");
        ItemStack stack=new ItemStack(item,reward.count());
        if(reward.count()>stack.getMaxStackSize())throw new IllegalArgumentException("Oversized Minecraft loot stack");
        var registry=player.level().registryAccess().lookupOrThrow(Registries.ENCHANTMENT);
        ItemEnchantments.Mutable stored=new ItemEnchantments.Mutable(ItemEnchantments.EMPTY);
        for(var requested:reward.enchantments()){
            var enchantment=registry.getOrThrow(ResourceKey.create(Registries.ENCHANTMENT,Identifier.fromNamespaceAndPath("minecraft",requested.id())));
            if(!stack.is(Items.ENCHANTED_BOOK)||requested.level()>enchantment.value().getMaxLevel())throw new IllegalArgumentException("Invalid Minecraft stored loot enchantment");
            stored.set(enchantment,requested.level());
        }
        if(!reward.enchantments().isEmpty())stack.set(DataComponents.STORED_ENCHANTMENTS,stored.toImmutable());
        return stack;
    }
}
