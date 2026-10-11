package dev.re4craft.control;

import org.junit.jupiter.api.Test;
import java.nio.file.Path;
import static org.junit.jupiter.api.Assertions.*;

class BackgroundClientPolicyTest {
    @Test void visibleClientsKeepTheirExactFlags() {
        for(long flags:new long[]{0,8224,65,Long.MAX_VALUE,-1})assertEquals(flags,BackgroundClientPolicy.windowFlags(flags,false));
    }
    @Test void hiddenIsRequestedBeforeCreationWithoutMinimizingOrFullscreen() {
        for(long flags:new long[]{0,8224,65,Long.MAX_VALUE,-1}) {
            long actual=BackgroundClientPolicy.windowFlags(flags,true);
            assertTrue(BackgroundClientPolicy.safeWindow(actual));
            long controlled=BackgroundClientPolicy.HIDDEN|BackgroundClientPolicy.NOT_FOCUSABLE|BackgroundClientPolicy.MINIMIZED|BackgroundClientPolicy.FULLSCREEN;
            assertEquals(flags&~controlled,actual&~controlled);
        }
    }
    @Test void aVisibleFocusableMinimizedOrFullscreenWindowIsRejected() {
        long valid=BackgroundClientPolicy.windowFlags(8224,true);
        assertFalse(BackgroundClientPolicy.safeWindow(valid&~BackgroundClientPolicy.HIDDEN));
        assertFalse(BackgroundClientPolicy.safeWindow(valid&~BackgroundClientPolicy.NOT_FOCUSABLE));
        assertFalse(BackgroundClientPolicy.safeWindow(valid|BackgroundClientPolicy.MINIMIZED));
        assertFalse(BackgroundClientPolicy.safeWindow(valid|BackgroundClientPolicy.FULLSCREEN));
    }
    @Test void connectedActiveClientGetsSixtyWhileIdleClientUsesLessGpu() {
        assertEquals(60,BackgroundClientPolicy.renderLimit(true));assertEquals(15,BackgroundClientPolicy.renderLimit(false));
    }
    @Test void shutdownMustBeExactFlagInAnIsolatedInstance() {
        Path parent=Path.of("build").toAbsolutePath();
        for(String name:new String[]{".instance",".dev-instance",".qa-void-instance"}) {
            Path game=parent.resolve(name);
            assertTrue(BackgroundClientPolicy.shutdownPathAllowed(game,game.resolve(".re4craft/shutdown.request")));
            assertFalse(BackgroundClientPolicy.shutdownPathAllowed(game,game.resolve("shutdown.request")));
            assertFalse(BackgroundClientPolicy.shutdownPathAllowed(game,game.resolve(".re4craft/../../shutdown.request")));
        }
        Path normal=parent.resolve(".minecraft");
        assertFalse(BackgroundClientPolicy.shutdownPathAllowed(normal,normal.resolve(".re4craft/shutdown.request")));
    }
    @Test void staleOrForeignShutdownReceiptsCannotStopTheClient() {
        String token="test-launch-0123456789abcdefghijkl";
        assertTrue(BackgroundClientPolicy.matchesShutdownToken(token,token+"\r\n"));
        assertFalse(BackgroundClientPolicy.matchesShutdownToken(token,"foreign-launch-0123456789abcdefgh"));
        assertFalse(BackgroundClientPolicy.matchesShutdownToken("", ""));
        assertFalse(BackgroundClientPolicy.matchesShutdownToken(token,token+" ".repeat(257)));
        assertFalse(BackgroundClientPolicy.matchesShutdownToken(token,"prefix"+token));
    }
    @Test void starterBowNeverReplacesAnItemOrDuplicatesAnExistingKit() {
        assertEquals(10,BackgroundClientPolicy.starterBowSlot(false,0,s->false,s->s==10 || s==11));
        assertEquals(-2,BackgroundClientPolicy.starterBowSlot(false,0,s->s==35,s->s==0));
        assertEquals(-1,BackgroundClientPolicy.starterBowSlot(true,1,s->false,s->true));
        assertEquals(-1,BackgroundClientPolicy.starterBowSlot(false,0,s->false,s->false));
        assertEquals(-1,BackgroundClientPolicy.starterBowSlot(false,1024,s->false,s->true));
    }
    @Test void loadedSavesKeepEquipmentAndHotbarWhileBowMigrationRemainsPossible() {
        assertTrue(BackgroundClientPolicy.starterKit(true,true));
        assertFalse(BackgroundClientPolicy.starterKit(true,false));
        assertFalse(BackgroundClientPolicy.starterKit(false,true));
        assertFalse(BackgroundClientPolicy.starterKit(false,false));
        // The legacy save's missing bow may still enter an empty inventory slot,
        // and a saved hotbar bow must only be marked, never replaced or duplicated.
        assertEquals(10,BackgroundClientPolicy.starterBowSlot(false,0,s->false,s->s==10));
        assertEquals(-2,BackgroundClientPolicy.starterBowSlot(false,0,s->s==0,s->s==10));
    }
}
