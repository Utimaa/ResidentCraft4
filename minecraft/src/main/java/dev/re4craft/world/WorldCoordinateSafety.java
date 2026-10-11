package dev.re4craft.world;

/** A host transform must leave enough real dimension height for the player's body and building. */
public final class WorldCoordinateSafety {
    private WorldCoordinateSafety() { }
    public static boolean validFeet(double x,double y,double z,int minY,int maxY,double height) {
        return Double.isFinite(x) && Double.isFinite(y) && Double.isFinite(z)
                && Double.isFinite(height) && height>0 && maxY>minY
                && Math.abs(x)<29_999_984 && Math.abs(z)<29_999_984
                && y>=minY && y+height<=maxY;
    }
}
