package dev.re4craft.loot;

import java.util.ArrayList;
import java.util.List;
import java.util.Set;

/** Deterministic chapter crafting resources; equipment is earned through vanilla recipes. */
public final class ProgressionLoot {
    public static final int GANADO=1,CRATE=2,WOODEN_BOX=CRATE;
    public static final int WOOD=0,STONE=1,IRON=2,DIAMOND=3,NETHERITE=4,ENCHANTED_NETHERITE=5,ADVANCED=NETHERITE;
    public static final int MAX_TIER=ENCHANTED_NETHERITE,MAX_STACKS=12;
    private static final Set<String> ENCHANTMENTS=Set.of("protection","feather_falling","sharpness","efficiency","fortune","unbreaking","mending","power","infinity");
    public record Enchantment(String id,int level) {
        public Enchantment {if(id==null||!ENCHANTMENTS.contains(id)||level<1||level>5)throw new IllegalArgumentException("Invalid loot enchantment");}
    }
    public record Stack(String item,int count,List<Enchantment> enchantments) {
        public Stack {
            if(item==null||!item.matches("minecraft:[a-z_]+")||count<1||count>64||enchantments==null||enchantments.size()>4)
                throw new IllegalArgumentException("Invalid loot stack");
            enchantments=List.copyOf(enchantments);
            if(!enchantments.isEmpty()&&(count!=1||enchantments.stream().map(Enchantment::id).distinct().count()!=enchantments.size()))
                throw new IllegalArgumentException("Invalid enchanted loot stack");
        }
        public Stack(String item,int count){this(item,count,List.of());}
    }
    private static final List<Stack> BOOKS=buildBooks();
    private ProgressionLoot() { }
    public static List<Stack> select(int source,int tier,long session,int event){return select(source,tier,session,event,Integer.remainderUnsigned(event,7));}
    public static List<Stack> select(int source,int tier,long session,int event,int bookIndex) {
        if(source<GANADO||source>CRATE||tier<WOOD||tier>MAX_TIER||session==0||event==0||bookIndex<0||bookIndex>=7)
            throw new IllegalArgumentException("Invalid host loot identity/tier");
        long seed=mix(session^Integer.toUnsignedLong(event)*0x9e3779b97f4a7c15L);
        boolean crate=source==CRATE;int amount=crate?3+roll(seed,3):1+roll(seed,2);
        List<Stack> result=new ArrayList<>();
        if(!crate)result.add(new Stack("minecraft:arrow",2+roll(seed>>>7,3)));
        switch(tier) {
            case WOOD -> {result.add(new Stack("minecraft:oak_log",amount));if(crate)result.add(new Stack("minecraft:stick",2));}
            case STONE -> {result.add(new Stack("minecraft:cobblestone",amount+1));result.add(new Stack("minecraft:coal",crate?2:1));result.add(new Stack("minecraft:oak_log",1));}
            case IRON -> {result.add(new Stack("minecraft:iron_ingot",crate?4:2));result.add(new Stack("minecraft:coal",crate?2:1));result.add(new Stack("minecraft:oak_log",1));}
            case DIAMOND -> {result.add(new Stack("minecraft:diamond",crate?3:2));result.add(new Stack("minecraft:iron_ingot",crate?4:2));result.add(new Stack("minecraft:lapis_lazuli",crate?3:1));result.add(new Stack("minecraft:oak_log",1));}
            case NETHERITE,ENCHANTED_NETHERITE -> {
                boolean finalChapter=tier==ENCHANTED_NETHERITE;
                result.add(new Stack("minecraft:netherite_scrap",crate?(finalChapter?4:3):(finalChapter?2:1)));
                result.add(new Stack("minecraft:gold_ingot",crate?(finalChapter?4:3):(finalChapter?2:1)));
                if(crate||Integer.remainderUnsigned(event,3)==0)result.add(new Stack("minecraft:netherite_upgrade_smithing_template",1));
                result.add(new Stack("minecraft:diamond",crate?(finalChapter?4:3):1));
                result.add(new Stack("minecraft:iron_ingot",crate?(finalChapter?8:6):3));
                result.add(new Stack("minecraft:oak_log",1));
                if(crate){result.add(new Stack("minecraft:string",1));result.add(new Stack("minecraft:netherrack",1));}
                result.add(books().get(bookIndex));
                result.add(new Stack("minecraft:experience_bottle",crate?(finalChapter?40:32):(finalChapter?20:16)));
            }
            default -> throw new AssertionError();
        }
        if(crate||roll(seed>>>29,3)==0)result.add(new Stack(tier>=IRON?"minecraft:cooked_beef":"minecraft:bread",crate?2:1));
        if(result.size()>MAX_STACKS)throw new AssertionError("Unbounded chapter loot");
        return List.copyOf(result);
    }
    public static List<Stack> books(){return BOOKS;}
    private static List<Stack> buildBooks(){
        Stack armor=book("protection",4,null,0),boots=book("protection",4,"feather_falling",4);
        Stack sword=book("sharpness",5,null,0),pickaxe=book("efficiency",5,"fortune",3);
        Stack bow=new Stack("minecraft:enchanted_book",1,List.of(new Enchantment("power",5),new Enchantment("unbreaking",3),new Enchantment("infinity",1)));
        return List.of(armor,armor,armor,boots,sword,pickaxe,bow);
    }
    private static Stack book(String main,int level,String extra,int extraLevel){
        List<Enchantment> enchants=new ArrayList<>(List.of(new Enchantment(main,level),new Enchantment("unbreaking",3),new Enchantment("mending",1)));
        if(extra!=null)enchants.add(new Enchantment(extra,extraLevel));
        return new Stack("minecraft:enchanted_book",1,enchants);
    }
    private static int roll(long seed,int bound){return (int)Long.remainderUnsigned(seed,bound);}
    private static long mix(long seed){seed=(seed^(seed>>>30))*0xbf58476d1ce4e5b9L;seed=(seed^(seed>>>27))*0x94d049bb133111ebL;return seed^(seed>>>31);}
}
