package dev.re4craft.world;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;
class NativeHostilityTest {
    @Test void actualHostilesArePositiveAndMixedMerchantKindsExcluded() {
        for(int kind:new int[]{0x11,0x12,0x15,0x22,0x36,0x42})assertTrue(NativeHostility.knownHostile(kind));
        for(int kind:new int[]{0x13,0x14,0x18,0x1e})assertFalse(NativeHostility.knownHostile(kind));
    }
    @Test void FriendlyDogNeutralAnimalsCratesAndUnknownKindsAreNotTargets() {
        for(int kind:new int[]{0x21,0x23,0x26,0x27,0x28,0x29,0x2e,0x3d,0x53,0,0x7fff})assertFalse(NativeHostility.knownHostile(kind));
    }
    @Test void mixedKindsRequireVerifiedNativeVariantMarker() {
        for(int kind:new int[]{0x13,0x14,0x1e}) {
            assertTrue(NativeHostility.hostile(kind,9));
            assertFalse(NativeHostility.hostile(kind,1));
        }
        for(int kind:new int[]{0x18,0x21,0x26,0x53,0x7fff})assertFalse(NativeHostility.hostile(kind,9));
        assertFalse(NativeHostility.hostile(0x12,5));
    }
}
