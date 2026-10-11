package dev.re4craft.client;

import dev.re4craft.control.BackgroundClientPolicy;
import dev.re4craft.control.BackgroundShutdown;
import net.minecraft.client.Minecraft;
import org.lwjgl.sdl.SDLVideo;
import java.nio.file.Path;
import java.io.IOException;

/** A real GPU client without a desktop window. Guest input still comes only from the bridge. */
public final class BackgroundClient {
    private static final boolean ENABLED=Boolean.getBoolean("re4craft.background");
    private static final boolean TEST=Boolean.getBoolean("re4craft.backgroundTest");
    private static long nextReport,frames,started,lastReport;
    private static long nextShutdownCheck;
    private static boolean shutdownPathRejected;
    public static boolean enabled() {return ENABLED;}
    public static void created(long handle) {
        if(!ENABLED)return;
        long flags=SDLVideo.SDL_GetWindowFlags(handle);
        if(!BackgroundClientPolicy.safeWindow(flags))
            throw new IllegalStateException("RE4Craft background client requires a hidden, non-focusable SDL window");
        Re4CraftClient.LOG.info("Background Minecraft window created hidden and non-focusable; native flags {}",Long.toUnsignedString(flags));
    }
    static void frame(Minecraft mc) {
        if(!ENABLED)return;
        shutdownRequest(mc);
        // Vanilla focus-loss pause must not suspend the integrated server when RE4 owns the desktop.
        // The release uses a private options file; regular visible clients are unaffected.
        mc.options.pauseOnLostFocus=false;
        if(!TEST)return;
        long now=System.nanoTime();if(started==0){started=now;lastReport=now;}
        frames++;
        if(now<nextReport)return;
        nextReport=now+5_000_000_000L;
        var server=mc.getSingleplayerServer();long flags=SDLVideo.SDL_GetWindowFlags(mc.getWindow().handle());
        Re4CraftClient.LOG.info("Background QA: safe={}, flags={}, frames={}, intervalMs={}, serverTick={}, level={}, active={}, paused={}, virtualFocus={}, iconified={}",
                BackgroundClientPolicy.safeWindow(flags),Long.toUnsignedString(flags),frames,(now-lastReport)/1_000_000L,
                server==null?-1:server.getTickCount(),mc.level!=null,Re4CraftClient.active(),mc.isPaused(),mc.isWindowActive(),mc.getWindow().isIconified());
        lastReport=now;frames=0;
    }
    private static void shutdownRequest(Minecraft mc) {
        String configured=System.getProperty("re4craft.shutdownFile","");
        if(configured.isEmpty() || shutdownPathRejected)return;
        String token=System.getProperty("re4craft.shutdownToken","");
        if(!BackgroundClientPolicy.validShutdownToken(token)) {
            shutdownPathRejected=true;Re4CraftClient.LOG.error("Rejected missing or invalid owned-launch shutdown token");return;
        }
        long now=System.nanoTime();if(now<nextShutdownCheck)return;nextShutdownCheck=now+500_000_000L;
        try {
            Path game=mc.gameDirectory.toPath().toAbsolutePath().normalize();Path requested=Path.of(configured).toAbsolutePath().normalize();
            if(!BackgroundClientPolicy.shutdownPathAllowed(game,requested)) {
                shutdownPathRejected=true;Re4CraftClient.LOG.error("Rejected shutdown path outside the dedicated bridge instance");return;
            }
            if(!BackgroundShutdown.consume(game,requested,token))return;
            Re4CraftClient.LOG.info("Owned launcher requested graceful Minecraft shutdown; saving the selected world");mc.stop();
        } catch(IOException | IllegalArgumentException error) {
            Re4CraftClient.LOG.warn("Could not consume bridge shutdown request: {}",error.getMessage());
        }
    }
}
