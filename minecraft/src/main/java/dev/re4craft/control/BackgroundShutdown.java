package dev.re4craft.control;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;

/** Filesystem receipt only: caller invokes Minecraft's normal save-and-stop after a successful consume. */
public final class BackgroundShutdown {
    private BackgroundShutdown() { }
    public static boolean consume(Path game,Path requested,String token) throws IOException {
        game=game.toAbsolutePath().normalize();requested=requested.toAbsolutePath().normalize();
        if(!BackgroundClientPolicy.validShutdownToken(token) || !BackgroundClientPolicy.shutdownPathAllowed(game,requested)
                || !Files.isRegularFile(requested,LinkOption.NOFOLLOW_LINKS))return false;
        if(!requested.getParent().toRealPath().equals(game.toRealPath().resolve(".re4craft")))return false;
        try(var receipt=Files.newInputStream(requested,LinkOption.NOFOLLOW_LINKS)) {
            byte[] bytes=receipt.readNBytes(257);
            if(bytes.length>256 || !BackgroundClientPolicy.matchesShutdownToken(token,new String(bytes,StandardCharsets.UTF_8)))return false;
        }
        Files.delete(requested);return true;
    }
}
