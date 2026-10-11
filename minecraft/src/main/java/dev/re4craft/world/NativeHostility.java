package dev.re4craft.world;
import dev.re4craft.protocol.Wire;

/** Positive actor-kind list from the pinned re4_tweaks Game.cpp emNames table. */
public final class NativeHostility {
    private NativeHostility() { }
    public static boolean knownHostile(int kind) {
        // Mixed Ganado/Merchant kinds13,14,1e need a verified native variant classifier.
        // Unclassified actors stay manually hittable; they are not automatic wolf targets.
        return switch(kind) {
            case 0x11,0x12,0x15,0x16,0x17,0x19,0x1a,0x1b,0x1c,0x1d,0x1f,0x20,
                 0x22,0x25,0x2b,0x2c,0x2d,0x2f,0x31,0x32,0x35,0x36,0x38,0x39,
                 0x3c,0x3f,0x42,0x43,0x44 -> true;
            default -> false;
        };
    }
    public static boolean hostile(int kind,int flags) {
        if((flags&Wire.ACTOR_BREAKABLE)!=0)return false;
        return knownHostile(kind) || (flags&Wire.ACTOR_HOSTILE)!=0 && (kind==0x13||kind==0x14||kind==0x1e);
    }
}
