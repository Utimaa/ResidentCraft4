package dev.re4craft.world;

import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

final class WorldCoordinateSafetyTest {
    @Test void rejectsActualEmptyVoidRespawnAndBodyOutsideBuildRange() {
        assertFalse(WorldCoordinateSafety.validFeet(8.5,-1024.5169479491046,8.5,-1024,1024,1.8));
        assertFalse(WorldCoordinateSafety.validFeet(0,1023,0,-1024,1024,1.8));
        assertTrue(WorldCoordinateSafety.validFeet(0,64,0,-1024,1024,1.8));
        assertTrue(WorldCoordinateSafety.validFeet(0,-1016,0,-1024,1024,1.8));
    }
    @Test void rejectsMalformedCoordinatesAndUsesActualDimensionBounds() {
        assertFalse(WorldCoordinateSafety.validFeet(0,Double.NaN,0,-1024,1024,1.8));
        assertFalse(WorldCoordinateSafety.validFeet(Double.POSITIVE_INFINITY,64,0,-1024,1024,1.8));
        assertFalse(WorldCoordinateSafety.validFeet(0,64,0,320,-64,1.8));
        assertFalse(WorldCoordinateSafety.validFeet(0,-64.01,0,-64,320,1.8));
        assertTrue(WorldCoordinateSafety.validFeet(0,-60,0,-64,320,1.8));
        assertTrue(WorldCoordinateSafety.validFeet(0,64,0,-64,320,1.8));
    }
}
