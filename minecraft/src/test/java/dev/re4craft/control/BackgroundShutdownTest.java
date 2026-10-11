package dev.re4craft.control;

import java.nio.file.Files;
import java.nio.file.Path;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;
import static org.junit.jupiter.api.Assertions.*;

class BackgroundShutdownTest {
    @TempDir Path root;
    private static final String TOKEN="owned-launch-0123456789abcdefghijk";
    @Test void onlyTheCurrentLaunchReceiptIsConsumed() throws Exception {
        Path game=root.resolve(".instance"),flag=game.resolve(".re4craft/shutdown.request");Files.createDirectories(flag.getParent());
        Files.writeString(flag,"previous-launch-0123456789abcdefgh");
        assertFalse(BackgroundShutdown.consume(game,flag,TOKEN));assertTrue(Files.exists(flag));
        Files.writeString(flag,TOKEN+"\r\n");
        assertTrue(BackgroundShutdown.consume(game,flag,TOKEN));assertFalse(Files.exists(flag));
        assertFalse(BackgroundShutdown.consume(game,flag,TOKEN));
    }
    @Test void malformedOrUnboundedReceiptsAreRetained() throws Exception {
        Path game=root.resolve(".dev-instance"),flag=game.resolve(".re4craft/shutdown.request");Files.createDirectories(flag.getParent());
        Files.writeString(flag,TOKEN+" ".repeat(512));
        assertFalse(BackgroundShutdown.consume(game,flag,TOKEN));assertTrue(Files.exists(flag));
        Files.writeString(flag,TOKEN);
        assertFalse(BackgroundShutdown.consume(game,flag,""));assertTrue(Files.exists(flag));
    }
    @Test void originalGameDirectoryAndOutsideFlagsAreNeverConsumed() throws Exception {
        Path original=root.resolve(".minecraft"),flag=original.resolve(".re4craft/shutdown.request");Files.createDirectories(flag.getParent());Files.writeString(flag,TOKEN);
        assertFalse(BackgroundShutdown.consume(original,flag,TOKEN));assertTrue(Files.exists(flag));
        Path game=root.resolve(".instance");Files.createDirectories(game);
        assertFalse(BackgroundShutdown.consume(game,flag,TOKEN));assertTrue(Files.exists(flag));
    }
}
