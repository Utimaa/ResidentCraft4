package dev.re4craft.client;

import net.minecraft.client.Minecraft;

/** Shares the vanilla model collector and session textures with avatar capture.
 * Category3 frames replace one bounded mailbox; an empty frame removes old
 * drops/projectiles after vanilla pickup or removal. */
public final class EntityCapture {
    private final AvatarCapture collector;
    public EntityCapture(BridgeServer server,AvatarCapture collector){this.collector=collector;}
    public void capture(Minecraft mc){collector.captureEntities(mc);}
}
