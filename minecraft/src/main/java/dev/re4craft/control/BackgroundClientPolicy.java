package dev.re4craft.control;
import java.nio.file.Path;
import java.util.function.IntPredicate;

/** Window creation stays native; these flags select a hidden, non-focusable SDL3 window. */
public final class BackgroundClientPolicy {
    public static final long HIDDEN=8L, MINIMIZED=64L, FULLSCREEN=1L, NOT_FOCUSABLE=0x80000000L;
    private BackgroundClientPolicy() { }
    public static long windowFlags(long original,boolean background) {
        return background?(original|HIDDEN|NOT_FOCUSABLE)&~(MINIMIZED|FULLSCREEN):original;
    }
    public static boolean safeWindow(long actual) {
        return (actual&HIDDEN)!=0 && (actual&NOT_FOCUSABLE)!=0 && (actual&(MINIMIZED|FULLSCREEN))==0;
    }
    public static int renderLimit(boolean active) {return active?60:15;}
    /** Fresh profiles get a starter kit; a loaded save owns all its existing equipment and inventory. */
    public static boolean starterKit(boolean dedicated,boolean createdWorld) {return dedicated && createdWorld;}
    public static boolean shutdownPathAllowed(Path game,Path requested) {
        game=game.toAbsolutePath().normalize();requested=requested.toAbsolutePath().normalize();
        if(game.getFileName()==null)return false;
        String name=game.getFileName().toString();
        return (name.equals(".instance") || name.equals(".dev-instance") || name.equals(".qa-void-instance"))
                && requested.equals(game.resolve(".re4craft/shutdown.request"));
    }
    public static boolean validShutdownToken(String token) {return token!=null && token.matches("[A-Za-z0-9_-]{32,96}");}
    public static boolean matchesShutdownToken(String token,String receipt) {
        return validShutdownToken(token) && receipt!=null && receipt.length()<=256 && token.equals(receipt.strip());
    }
    /** -2: already has a bow and may mark the kit; -1: defer; otherwise a genuinely empty slot. */
    public static int starterBowSlot(boolean tagged,int tagCount,IntPredicate bow,IntPredicate empty) {
        if(tagged || tagCount>=1024)return -1;
        int free=-1;
        for(int slot=0;slot<36;slot++) {
            if(bow.test(slot))return -2;
            if(free<0 && empty.test(slot))free=slot;
        }
        return free;
    }
}
