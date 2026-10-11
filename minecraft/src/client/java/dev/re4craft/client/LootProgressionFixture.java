package dev.re4craft.client;

import com.mojang.serialization.JsonOps;
import dev.re4craft.loot.ProgressionLoot;
import dev.re4craft.loot.LootMilestones;
import dev.re4craft.protocol.Wire;
import java.nio.file.Path;
import java.util.*;
import net.minecraft.client.Minecraft;
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
import net.minecraft.world.item.crafting.CraftingInput;
import net.minecraft.world.item.crafting.RecipeType;
import net.minecraft.world.item.crafting.SmithingRecipeInput;
import net.minecraft.world.item.enchantment.ItemEnchantments;
import net.minecraft.world.inventory.AnvilMenu;

/** Actual server recipe/book/save QA; no progression equipment is granted by production loot. */
final class LootProgressionFixture {
    private static final long SESSION=0x6c6f6f745142L;
    private static final String GEAR_TAG="re4craft:loot_qa_crafted_v2",BOOK_TAG="re4craft:loot_qa_book_v2";
    private static boolean done;private static int checks;
    static void tick(MinecraftServer server){
        String phase=System.getProperty("re4craft.lootProgressionTest","");
        if(done||!(phase.equals("seed")||phase.equals("check"))||!Boolean.getBoolean("re4craft.backgroundTest")||server.getTickCount()<50)return;
        Path game=Minecraft.getInstance().gameDirectory.toPath().toAbsolutePath().normalize(),lab=game.getParent();
        boolean disposable=game.getFileName().toString().equals(".instance")&&lab!=null&&lab.getFileName().toString().matches("[a-f0-9]{32}")
                &&lab.getParent()!=null&&lab.getParent().getFileName().toString().equals("release-review")&&lab.getParent().getParent()!=null&&lab.getParent().getParent().getFileName().toString().equals("build");
        if(!disposable)throw new IllegalStateException("Loot recipe QA refused outside a disposable review instance");
        var players=server.getPlayerList().getPlayers();if(players.size()!=1)return;
        done=true;var player=players.getFirst();List<ItemStack> before=new ArrayList<>();
        for(int slot=0;slot<player.getInventory().getContainerSize();slot++)before.add(player.getInventory().getItem(slot).copy());
        try{
            var saved=HostLoot.data(player);
            var legacy=HostLoot.CODEC.parse(JsonOps.INSTANCE,com.google.gson.JsonParser.parseString("{\"sessions\":[{\"session\":11,\"event\":12}]} ")).getOrThrow();
            require(!legacy.ledger.fresh(11,12)&&legacy.books.index(player.getUUID(),2)==0,"legacy sessions-only ledger starts book rotation at zero");
            if(phase.equals("seed")){
                var scratch=new HostLoot.LedgerData(List.of(),List.of(new LootMilestones.Stamp(player.getUUID(),63)),List.of());
                var failed=new ArrayList<ItemEntity>();int[] attempts={0};
                var receipt=HostLoot.award(player,scratch,event(SESSION,70,2,4),player.position().add(0,1,0),entity->{
                    if(++attempts[0]==3)return false;boolean ok=player.level().addFreshEntity(entity);if(ok)failed.add(entity);return ok;
                });
                require(receipt==null&&failed.size()==2&&failed.stream().noneMatch(ItemEntity::isAlive),"actual partial spawn rolls back");
                require(scratch.ledger.fresh(SESSION,70)&&scratch.books.index(player.getUUID(),2)==0,"failed event leaves book rotation and replay stamp untouched");
                var retried=HostLoot.award(player,scratch,event(SESSION,70,2,4),player.position().add(0,1,0));
                require(retried!=null&&scratch.books.index(player.getUUID(),2)==1,"exact retry advances book rotation once");
                checkEntities(player,retried);retried.entities().forEach(ItemEntity::discard);
                require(scratch.milestones.snapshot().getFirst().tiers()==63,"legacy gear claims are readable but inactive");
                for(int tier=0;tier<=5;tier++){
                    var awarded=HostLoot.award(player,saved,event(SESSION,tier+1,1,tier),player.position().add(0,1,0));
                    require(awarded!=null,"all chapter material tiers materialize");checkEntities(player,awarded);awarded.entities().forEach(ItemEntity::discard);
                }
                Bank bank=new Bank();
                for(int crate=0;crate<16;crate++){
                    int beforeBook=saved.books.index(player.getUUID(),2);
                    // Sparse native IDs deliberately include interleaved enemies.
                    var enemy=HostLoot.award(player,saved,event(SESSION+2,crate*37+1,1,4),player.position().add(0,1,0));
                    require(enemy!=null&&saved.books.index(player.getUUID(),2)==beforeBook,"enemy awards cannot skip crate books");enemy.entities().forEach(ItemEntity::discard);
                    var awarded=HostLoot.award(player,saved,event(SESSION+1,crate*37+1,2,4),player.position().add(0,1,0));
                    require(awarded!=null,"sixteen moderate late crate rewards spawn");checkEntities(player,awarded);
                    for(var entity:awarded.entities()){bank.add(entity.getItem());entity.discard();}
                }
                require(bank.count(Items.DIAMOND)>=29&&bank.count(Items.IRON_INGOT)>=95,"diamond gear and up to three anvils plus smithing table affordable");
                require(bank.count(Items.NETHERITE_SCRAP)>=24&&bank.count(Items.GOLD_INGOT)>=24&&bank.count(Items.NETHERITE_UPGRADE_SMITHING_TEMPLATE)>=6,"six real consumed upgrades affordable");
                int minimumXp=bank.count(Items.EXPERIENCE_BOTTLE)*3;
                // All outputs below come from owned26.3 RecipeManager/AnvilMenu
                // and paid QA resource stacks, never from production gear drops.
                for(int n=0;n<16;n++)bank.add(craft(server,player,bank,1,1,List.of(Items.OAK_LOG)));
                bank.add(craft(server,player,bank,2,2,List.of(Items.OAK_PLANKS,Items.OAK_PLANKS,Items.OAK_PLANKS,Items.OAK_PLANKS)));
                bank.add(craft(server,player,bank,2,3,List.of(Items.IRON_INGOT,Items.IRON_INGOT,Items.OAK_PLANKS,Items.OAK_PLANKS,Items.OAK_PLANKS,Items.OAK_PLANKS)));
                for(int n=0;n<3;n++)bank.add(craft(server,player,bank,3,3,Collections.nCopies(9,Items.IRON_INGOT)));
                bank.add(craft(server,player,bank,3,3,grid("BBB"," I ","III",Items.IRON_BLOCK,Items.IRON_INGOT)));
                for(int n=0;n<2;n++)bank.add(craft(server,player,bank,1,2,List.of(Items.OAK_PLANKS,Items.OAK_PLANKS)));
                for(int n=0;n<6;n++)bank.add(craft(server,player,bank,3,3,List.of(Items.NETHERITE_SCRAP,Items.NETHERITE_SCRAP,Items.NETHERITE_SCRAP,Items.NETHERITE_SCRAP,Items.GOLD_INGOT,Items.GOLD_INGOT,Items.GOLD_INGOT,Items.GOLD_INGOT,Items.AIR)));
                String[][] patterns={{"DDD","D D"},{"D D","DDD","DDD"},{"DDD","D D","D D"},{"D D","D D"},{"D","D","S"},{"DDD"," S "," S "}};
                List<ProgressionLoot.Stack> definitions=List.of(ProgressionLoot.books().get(0),ProgressionLoot.books().get(1),ProgressionLoot.books().get(2),ProgressionLoot.books().get(3),ProgressionLoot.books().get(4),ProgressionLoot.books().get(5),ProgressionLoot.books().get(6));
                int xpRequired=0;
                for(int n=0;n<6;n++){
                    String[] rows=patterns[n];var diamond=craft(server,player,bank,rows[0].length(),rows.length,grid(rows,Items.DIAMOND,Items.STICK));
                    var input=new SmithingRecipeInput(bank.take(Items.NETHERITE_UPGRADE_SMITHING_TEMPLATE),diamond,bank.take(Items.NETHERITE_INGOT));
                    var recipe=server.getRecipeManager().getRecipeFor(RecipeType.SMITHING,input,player.level()).orElseThrow();
                    require(recipe.value().matches(input,player.level()),"actual smithing recipe consumes template/base/ingot");var upgraded=recipe.value().assemble(input);
                    require(BuiltInRegistries.ITEM.getKey(upgraded.getItem()).toString().contains("netherite"),"actual smithing output is netherite");
                    xpRequired+=anvil(player,bank,upgraded,definitions.get(n));
                }
                var bow=craft(server,player,bank,3,3,List.of(Items.AIR,Items.STICK,Items.STRING,Items.STICK,Items.AIR,Items.STRING,Items.AIR,Items.STICK,Items.STRING));
                require(bow.is(Items.BOW),"actual bow crafting recipe is reachable from resource drops");xpRequired+=anvil(player,bank,bow,definitions.get(6));
                require(minimumXp>=xpRequired,"minimum real bottle XP funds seven separate vanilla anvil operations");
                for(var definition:new LinkedHashSet<>(definitions))saveEntity(player,HostLoot.materialize(player,definition),BOOK_TAG);
                Re4CraftClient.LOG.info("LOOT_RECIPE_QA crates=16 diamondAvailable=48 ironAvailable=96 scrapAvailable=48 goldAvailable=48 templatesAvailable=16 xpBottles=512 minimumRealXp={} requiredVanillaAnvilXp={} actualCraftedGear=7 actualStoredBooks=5",minimumXp,xpRequired);
            }else{
                for(int tier=0;tier<=5;tier++)require(HostLoot.award(player,saved,event(SESSION,tier+1,1,tier),player.position())==null,"saved native event replay is rejected");
                var gear=player.level().getEntitiesOfClass(ItemEntity.class,player.getBoundingBox().inflate(32)).stream().filter(entity->entity.entityTags().contains(GEAR_TAG)).toList();
                var books=player.level().getEntitiesOfClass(ItemEntity.class,player.getBoundingBox().inflate(32)).stream().filter(entity->entity.entityTags().contains(BOOK_TAG)).toList();
                require(gear.size()==7&&books.size()==5,"actual crafted gear and stored books survive save reload");
                for(var item:gear)require(item.getItem().isEnchanted(),"saved recipe/anvil output retains normal enchantment component");
                for(var item:books)require(!item.getItem().getOrDefault(DataComponents.STORED_ENCHANTMENTS,ItemEnchantments.EMPTY).isEmpty(),"saved book retains stored enchantment component");
                require(saved.books.index(player.getUUID(),2)==2,"crate book rotation survives actual save reload");
                int expected=saved.books.index(player.getUUID(),2);var fresh=HostLoot.award(player,saved,event(SESSION+3,1,2,4),player.position().add(0,1,0));
                require(fresh!=null&&saved.books.index(player.getUUID(),2)==(expected+1)%7,"native process restart continues saved book rotation");checkEntities(player,fresh);fresh.entities().forEach(ItemEntity::discard);
            }
            var roundTrip=HostLoot.CODEC.parse(JsonOps.INSTANCE,HostLoot.CODEC.encodeStart(JsonOps.INSTANCE,saved).getOrThrow()).getOrThrow();
            require(roundTrip.books.snapshot().equals(saved.books.snapshot())&&roundTrip.ledger.snapshot().equals(saved.ledger.snapshot()),"real saved codec round trip preserves book rotations and replay stamps");
            for(int slot=0;slot<before.size();slot++)require(ItemStack.matches(before.get(slot),player.getInventory().getItem(slot)),"material loot and QA recipe previews leave inventory/equipment unchanged");
            Re4CraftClient.LOG.info("LOOT_PROGRESSION_QA phase={} passed=true checks={} bookCycles={} actualSavedCraftedGear={} actualSavedStoredBooks={}",phase,checks,saved.books.snapshot(),phase.equals("check")?7:0,phase.equals("check")?5:0);
        }catch(RuntimeException failure){Re4CraftClient.LOG.error("LOOT_PROGRESSION_QA phase={} passed=false checks={}",phase,checks,failure);throw failure;}
    }
    private static Wire.Loot event(long session,int id,int source,int tier){return new Wire.Loot(1,id,source,tier,session,0,0,0);}
    private static List<net.minecraft.world.item.Item> grid(String a,String b,String c,net.minecraft.world.item.Item first,net.minecraft.world.item.Item second){return grid(new String[]{a,b,c},first,second);}
    private static List<net.minecraft.world.item.Item> grid(String[] rows,net.minecraft.world.item.Item first,net.minecraft.world.item.Item second){
        var result=new ArrayList<net.minecraft.world.item.Item>();for(String row:rows)for(char cell:row.toCharArray())result.add(cell==' '?Items.AIR:cell=='D'||cell=='B'?first:second);return result;
    }
    private static ItemStack craft(MinecraftServer server,ServerPlayer player,Bank bank,int width,int height,List<net.minecraft.world.item.Item> ingredients){
        List<ItemStack> input=new ArrayList<>();for(var item:ingredients)input.add(item==Items.AIR?ItemStack.EMPTY:bank.take(item));
        var recipeInput=CraftingInput.of(width,height,input);var recipe=server.getRecipeManager().getRecipeFor(RecipeType.CRAFTING,recipeInput,player.level()).orElseThrow();
        require(recipe.value().matches(recipeInput,player.level()),"actual crafting recipe matches paid resource grid");ItemStack output=recipe.value().assemble(recipeInput);require(!output.isEmpty(),"actual crafting recipe returns real output");return output;
    }
    private static int anvil(ServerPlayer player,Bank bank,ItemStack item,ProgressionLoot.Stack definition){
        ItemStack book=bank.takeBook(definition);var menu=new AnvilMenu(71,player.getInventory());menu.getSlot(0).set(item);menu.getSlot(1).set(book);menu.createResult();
        var result=menu.getSlot(2).getItem().copy();int levels=menu.getCost();require(!result.isEmpty()&&levels>0&&levels<40,"actual vanilla anvil accepts stored enchantments at a Survival-legal cost");
        checkEnchantments(player,definition,result,false);saveEntity(player,result,GEAR_TAG);
        // For costs below17 the actual vanilla total XP curve is L^2+6L.
        require(levels<=16,"actual fresh combined-book cost stays in independently budgeted XP range");return levels*levels+6*levels;
    }
    private static void saveEntity(ServerPlayer player,ItemStack stack,String tag){
        var entity=new ItemEntity(player.level(),player.getX()+3,player.getY()+1,player.getZ(),stack);entity.addTag(tag);entity.setNeverPickUp();entity.setUnlimitedLifetime();entity.setNoGravity(true);entity.setDeltaMovement(0,0,0);require(player.level().addFreshEntity(entity),"actual crafted/book item entity saved");
    }
    private static void checkEntities(ServerPlayer player,HostLoot.Award awarded){
        require(awarded.entities().size()==awarded.rewards().size(),"all real resource reward entities exist");
        for(int n=0;n<awarded.entities().size();n++){
            ItemStack actual=awarded.entities().get(n).getItem();var expected=awarded.rewards().get(n);
            require(awarded.entities().get(n).isAlive()&&actual.getCount()==expected.count()&&BuiltInRegistries.ITEM.getKey(actual.getItem()).toString().equals(expected.item()),"actual material item registry id/count match reward");
            require(!expected.item().matches(".*_(sword|pickaxe|helmet|chestplate|leggings|boots)$")&&!expected.item().equals("minecraft:bow"),"production loot never drops equipment");
            checkEnchantments(player,expected,actual,true);
        }
    }
    private static void checkEnchantments(ServerPlayer player,ProgressionLoot.Stack expected,ItemStack actual,boolean stored){
        var component=actual.getOrDefault(stored?DataComponents.STORED_ENCHANTMENTS:DataComponents.ENCHANTMENTS,ItemEnchantments.EMPTY);
        require(component.entrySet().size()==expected.enchantments().size(),"correct actual stored/normal enchantment component count");
        for(var requested:expected.enchantments()){
            var holder=player.level().registryAccess().lookupOrThrow(Registries.ENCHANTMENT).getOrThrow(ResourceKey.create(Registries.ENCHANTMENT,Identifier.fromNamespaceAndPath("minecraft",requested.id())));
            require(component.getLevel(holder)==requested.level()&&(stored||holder.value().canEnchant(actual)),"actual registry enchantment levels and applicability verified");
        }
    }
    private static final class Bank {
        final Map<net.minecraft.world.item.Item,Integer> resources=new HashMap<>();final List<ItemStack> books=new ArrayList<>();
        void add(ItemStack stack){if(stack.is(Items.ENCHANTED_BOOK))books.add(stack.copy());else resources.merge(stack.getItem(),stack.getCount(),Integer::sum);}
        int count(net.minecraft.world.item.Item item){return resources.getOrDefault(item,0);}
        ItemStack take(net.minecraft.world.item.Item item){require(count(item)>0,"paid actual recipe input is available from resource drops");resources.put(item,count(item)-1);return new ItemStack(item);}
        ItemStack takeBook(ProgressionLoot.Stack expected){
            for(int n=0;n<books.size();n++){var candidate=books.get(n);var enchantments=candidate.getOrDefault(DataComponents.STORED_ENCHANTMENTS,ItemEnchantments.EMPTY);
                if(enchantments.entrySet().size()==expected.enchantments().size()&&expected.enchantments().stream().allMatch(e->enchantments.entrySet().stream().anyMatch(entry->entry.getKey().unwrapKey().orElseThrow().identifier().getPath().equals(e.id())&&entry.getIntValue()==e.level())))return books.remove(n);
            }
            throw new IllegalStateException("Expected book recipe is not affordable from crate book rotation");
        }
    }
    private static void require(boolean ok,String condition){checks++;if(!ok)throw new IllegalStateException(condition);}
}
