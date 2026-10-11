/* Adapted from SkyCraft v0.1.0, copyright (c) 2026 chasmlol, MIT. */
package dev.re4craft.world;

/** One exact RE4 collision triangle in Minecraft space, with precomputed plane and bounds. */
public final class HostTriangle {
	/** Surfaces at most ~45 degrees from flat can be walked on. */
	public static final double WALKABLE_NY = 0.7;

	public final double ax, ay, az, bx, by, bz, cx, cy, cz;
	public final double nx, ny, nz; // unit normal (winding is not trusted; use |ny|)
	public final double minX, minY, minZ, maxX, maxY, maxZ;
	public final boolean stairHelper;
	public final boolean walkable;

	public HostTriangle(float[] v, int o, boolean stairHelper) {
		this.ax = v[o];
		this.ay = v[o + 1];
		this.az = v[o + 2];
		this.bx = v[o + 3];
		this.by = v[o + 4];
		this.bz = v[o + 5];
		this.cx = v[o + 6];
		this.cy = v[o + 7];
		this.cz = v[o + 8];
		double ux = this.bx - this.ax, uy = this.by - this.ay, uz = this.bz - this.az;
		double wx = this.cx - this.ax, wy = this.cy - this.ay, wz = this.cz - this.az;
		double qx = uy * wz - uz * wy, qy = uz * wx - ux * wz, qz = ux * wy - uy * wx;
		double len = Math.sqrt(qx * qx + qy * qy + qz * qz);
		if (len < 1e-12) {
			this.nx = 0;
			this.ny = 1;
			this.nz = 0;
		} else {
			this.nx = qx / len;
			this.ny = qy / len;
			this.nz = qz / len;
		}
		this.minX = Math.min(this.ax, Math.min(this.bx, this.cx));
		this.minY = Math.min(this.ay, Math.min(this.by, this.cy));
		this.minZ = Math.min(this.az, Math.min(this.bz, this.cz));
		this.maxX = Math.max(this.ax, Math.max(this.bx, this.cx));
		this.maxY = Math.max(this.ay, Math.max(this.by, this.cy));
		this.maxZ = Math.max(this.az, Math.max(this.bz, this.cz));
		this.stairHelper = stairHelper;
		this.walkable = stairHelper || Math.abs(this.ny) >= WALKABLE_NY;
	}

	public boolean degenerate() {
		return this.maxX - this.minX < 1e-9 && this.maxZ - this.minZ < 1e-9;
	}

	/**
	 * Height of the triangle's plane above (x, z) if that point lies inside the triangle's
	 * horizontal footprint, otherwise NaN. Near-vertical triangles have no meaningful height.
	 */
	public double heightAt(double x, double z) {
		if (x < this.minX - 1e-9 || x > this.maxX + 1e-9 || z < this.minZ - 1e-9 || z > this.maxZ + 1e-9 || Math.abs(this.ny) < 0.05) {
			return Double.NaN;
		}
		double d1 = edge(x, z, this.ax, this.az, this.bx, this.bz);
		double d2 = edge(x, z, this.bx, this.bz, this.cx, this.cz);
		double d3 = edge(x, z, this.cx, this.cz, this.ax, this.az);
		boolean hasNeg = d1 < -1e-12 || d2 < -1e-12 || d3 < -1e-12;
		boolean hasPos = d1 > 1e-12 || d2 > 1e-12 || d3 > 1e-12;
		if (hasNeg && hasPos) {
			return Double.NaN;
		}
		// Plane: n . (p - a) = 0  ->  y = ay - (nx (x - ax) + nz (z - az)) / ny
		return this.ay - (this.nx * (x - this.ax) + this.nz * (z - this.az)) / this.ny;
	}

	private static double edge(double px, double pz, double x0, double z0, double x1, double z1) {
		return (x1 - x0) * (pz - z0) - (z1 - z0) * (px - x0);
	}
}
