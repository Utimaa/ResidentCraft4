/* Adapted from SkyCraft v0.1.0, copyright (c) 2026 chasmlol, MIT. */
package dev.re4craft.world;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.ArrayList;
import java.util.List;
import org.junit.jupiter.api.Test;

class HostTriangleColliderTest {
	private static final double R = 0.3, H = 1.8, STEP = 0.6;
	private static final double GRAVITY_TICK = -0.0784; // what Minecraft asks for while standing

	/** Axis-aligned horizontal quad at height y. */
	private static void flat(List<HostTriangle> out, double x0, double z0, double x1, double z1, double y) {
		quad(out, x0, y, z0, x1, y, z0, x1, y, z1, x0, y, z1);
	}

	/** Slope rising along +X: height = y0 + (x - x0) * tan(angle). */
	private static void ramp(List<HostTriangle> out, double x0, double x1, double z0, double z1, double y0, double degrees) {
		double y1 = y0 + (x1 - x0) * Math.tan(Math.toRadians(degrees));
		quad(out, x0, y0, z0, x1, y1, z0, x1, y1, z1, x0, y0, z1);
	}

	/** Vertical wall in the plane x = xw. */
	private static void wallX(List<HostTriangle> out, double xw, double z0, double z1, double y0, double y1) {
		quad(out, xw, y0, z0, xw, y0, z1, xw, y1, z1, xw, y1, z0);
	}

	private static void quad(List<HostTriangle> out, double... p) {
		out.add(tri(p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8]));
		out.add(tri(p[0], p[1], p[2], p[6], p[7], p[8], p[9], p[10], p[11]));
	}

	private static HostTriangle tri(double... v) {
		float[] f = new float[9];
		for (int i = 0; i < 9; i++) {
			f[i] = (float) v[i];
		}
		return new HostTriangle(f, 0, false);
	}

	/** Runs ticks of constant requested movement; returns [x, y, z, onGround] after each. */
	private static List<double[]> simulate(List<HostTriangle> tris, double x, double y, double z, double mx, double my, double mz, int ticks) {
		List<double[]> path = new ArrayList<>();
		boolean onGround = true;
		for (int i = 0; i < ticks; i++) {
			double[] m = HostTriangleCollider.resolve(tris, x, y, z, R, H, STEP, onGround, mx, my, mz);
			onGround = m[1] != my && my < 0;
			x += m[0];
			y += m[1];
			z += m[2];
			path.add(new double[] { x, y, z, onGround ? 1 : 0 });
		}
		return path;
	}

	@Test
	void unobstructedMovementIsReturnedBitForBit() {
		List<HostTriangle> t = new ArrayList<>();
		flat(t, -50, -50, 50, 50, 0);
		// rising (jump) and moving horizontally, far from any wall: must not register a collision
		double[] m = HostTriangleCollider.resolve(t, 12.345678, 3.3333333, -7.777777, R, H, STEP, false, 0.21784, 0.33159999, -0.1234567);
		assertEquals(0.21784, m[0], 0.0);
		assertEquals(0.33159999, m[1], 0.0);
		assertEquals(-0.1234567, m[2], 0.0);
	}

	@Test
	void fullJumpArcIsNotCutShort() {
		List<HostTriangle> t = new ArrayList<>();
		flat(t, -50, -50, 50, 50, 0);
		double y = 0, vy = 0.42, peak = 0;
		boolean onGround = false;
		for (int i = 0; i < 30 && !(onGround && i > 0); i++) {
			double[] m = HostTriangleCollider.resolve(t, 0.5, y, 0.5, R, H, STEP, i == 0, 0.1, vy, 0);
			boolean vertical = m[1] != vy;
			onGround = vertical && vy < 0;
			y += m[1];
			peak = Math.max(peak, y);
			vy = vertical ? 0 : vy;
			vy = (vy - 0.08) * 0.98;
		}
		assertTrue(peak > 1.2, "reaches Minecraft's normal jump height (peak=" + peak + ")");
	}

	@Test
	void standsOnFlatGround() {
		List<HostTriangle> t = new ArrayList<>();
		flat(t, -5, -5, 5, 5, 0);
		double[] m = HostTriangleCollider.resolve(t, 0, 0, 0, R, H, STEP, true, 0, GRAVITY_TICK, 0);
		assertEquals(0.0, m[1], 1e-9);
	}

	@Test
	void walksUpAThirtyDegreeSlopeSmoothly() {
		List<HostTriangle> t = new ArrayList<>();
		flat(t, -5, -5, 0, 5, 0);
		ramp(t, 0, 10, -5, 5, 0, 30);
		List<double[]> path = simulate(t, -1, 0, 0, 0.2, GRAVITY_TICK, 0, 40);
		double prevY = 0;
		for (double[] p : path) {
			assertTrue(p[3] == 1, "stays on the ground while walking uphill");
			double expected = Math.max(0, p[0]) * Math.tan(Math.toRadians(30));
			assertEquals(expected, p[1], 0.1, "follows the slope surface at x=" + p[0]);
			assertTrue(p[1] - prevY < 0.2 * Math.tan(Math.toRadians(30)) + 0.05, "no stair-step jumps");
			prevY = p[1];
		}
		assertTrue(path.getLast()[0] > 6, "keeps walking forward up the slope");
	}

	@Test
	void sticksToGroundWalkingDownhill() {
		List<HostTriangle> t = new ArrayList<>();
		ramp(t, -10, 10, -5, 5, -10 * Math.tan(Math.toRadians(35)), 35); // passes through the origin
		double startY = 5 * Math.tan(Math.toRadians(35));
		List<double[]> path = simulate(t, 5, startY, 0, -0.28, GRAVITY_TICK, 0, 30);
		for (double[] p : path) {
			assertTrue(p[3] == 1, "never airborne going downhill at sprint speed");
			// the player rests on the highest point under its 0.15-block footprint
			assertEquals(p[0] * Math.tan(Math.toRadians(35)), p[1], 0.15 * Math.tan(Math.toRadians(35)) + 0.01);
		}
	}

	@Test
	void steepSlopeBlocksWalking() {
		List<HostTriangle> t = new ArrayList<>();
		flat(t, -5, -5, 0, 5, 0);
		ramp(t, 0, 5, -5, 5, 0, 65);
		List<double[]> path = simulate(t, -1, 0, 0, 0.2, GRAVITY_TICK, 0, 40);
		double[] last = path.getLast();
		assertTrue(last[1] < STEP + 0.05, "can't climb a 65 degree slope (y=" + last[1] + ")");
		assertTrue(last[0] < 0.6, "stopped at the foot of the slope (x=" + last[0] + ")");
	}

	@Test
	void wallStopsAtRadius() {
		List<HostTriangle> t = new ArrayList<>();
		flat(t, -5, -5, 5, 5, 0);
		wallX(t, 1, -5, 5, 0, 4);
		List<double[]> path = simulate(t, 0, 0, 0, 0.25, GRAVITY_TICK, 0, 20);
		assertEquals(1 - R, path.getLast()[0], 0.01);
	}

	@Test
	void slidesAlongADiagonalWall() {
		List<HostTriangle> t = new ArrayList<>();
		flat(t, -10, -10, 10, 10, 0);
		// wall along the line x = z (45 degrees), player walks +X into it
		quad(t, -5, 0, -5, 5, 0, 5, 5, 4, 5, -5, 4, -5);
		List<double[]> path = simulate(t, 0, 0, -2, 0.2, GRAVITY_TICK, 0, 30);
		double[] last = path.getLast();
		assertTrue(last[2] < -2 + 1e-6 || last[0] - last[2] >= R * Math.sqrt(2) - 0.05, "never inside the wall");
		assertTrue(last[0] > 0.5, "slides along instead of sticking (x=" + last[0] + ")");
	}

	@Test
	void landsFromAFall() {
		List<HostTriangle> t = new ArrayList<>();
		flat(t, -5, -5, 5, 5, 0);
		boolean onGround = false;
		double y = 5, vy = 0;
		for (int i = 0; i < 60; i++) {
			vy = (vy - 0.08) * 0.98;
			double[] m = HostTriangleCollider.resolve(t, 0, y, 0, R, H, STEP, onGround, 0, vy, 0);
			onGround = m[1] != vy && vy < 0;
			y += m[1];
			if (onGround) {
				vy = 0;
			}
		}
		assertEquals(0.0, y, 1e-9);
	}

	@Test
	void stepsUpHalfBlockLedge() {
		List<HostTriangle> t = new ArrayList<>();
		flat(t, -5, -5, 1, 5, 0);
		flat(t, 1, -5, 6, 5, 0.5);
		wallX(t, 1, -5, 5, 0, 0.5);
		List<double[]> path = simulate(t, 0, 0, 0, 0.2, GRAVITY_TICK, 0, 20);
		assertEquals(0.5, path.getLast()[1], 1e-6);
		assertTrue(path.getLast()[0] > 3);
	}

	@Test
	void fullBlockLedgeBlocks() {
		List<HostTriangle> t = new ArrayList<>();
		flat(t, -5, -5, 1, 5, 0);
		flat(t, 1, -5, 6, 5, 1.0);
		wallX(t, 1, -5, 5, 0, 1.0);
		List<double[]> path = simulate(t, 0, 0, 0, 0.2, GRAVITY_TICK, 0, 20);
		assertEquals(0.0, path.getLast()[1], 1e-6);
		assertEquals(1 - R, path.getLast()[0], 0.02);
	}

	@Test
	void ceilingStopsAJump() {
		List<HostTriangle> t = new ArrayList<>();
		flat(t, -5, -5, 5, 5, 0);
		flat(t, -5, -5, 5, 5, 2.0);
		double[] m = HostTriangleCollider.resolve(t, 0, 0, 0, R, H, STEP, true, 0, 0.42, 0);
		assertEquals(0.2, m[1], 1e-6);
	}

	@Test
	void zeroStepBodyDoesNotGainHorizontalMovementFromSupportedSlope() {
		List<HostTriangle> t = new ArrayList<>();
		double slope = Math.tan(Math.toRadians(20));
		ramp(t, -20, 20, -20, 20, -20 * slope, 20);
		double x = 0, y = .15 * slope;
		for (int tick = 0; tick < 120; tick++) {
			double[] movement = HostTriangleCollider.resolve(t, x, y, 0, .49, .98, 0, tick > 0, 0, -.04, 0);
			assertEquals(0, movement[0], 0, "supported zero-step body must not be pushed by its ground plane");
			assertEquals(0, movement[2], 0);
			x += movement[0];y += movement[1];
			assertEquals(.15 * slope, y, 1e-7);
			assertTrue(movement[1] != -.04, "vanilla must observe the real vertical ground contact");
		}
	}

	@Test
	void zeroStepDownhillMomentumRemainsRequestedAndGrounded() {
		List<HostTriangle> t = new ArrayList<>();
		double slope = Math.tan(Math.toRadians(20));
		ramp(t, -20, 20, -20, 20, -20 * slope, 20);
		double[] movement = HostTriangleCollider.resolve(t, 0, .15 * slope, 0, .49, .98, 0, true, -.15, -.04, 0);
		assertEquals(-.15, movement[0], 0, "collision must neither erase momentum nor manufacture downhill acceleration");
		assertEquals(0, movement[2], 0);
		assertEquals(-.15 * slope, movement[1], 1e-7);
		assertTrue(movement[1] != -.04);
	}

	@Test
	void zeroStepAirAndMissingSupportRetainActualRequestedMovement() {
		List<HostTriangle> t = new ArrayList<>();
		ramp(t, -20, 20, -20, 20, -20 * Math.tan(Math.toRadians(20)), 20);
		double[] air = HostTriangleCollider.resolve(t, 0, 4, 0, .49, .98, 0, false, .15, .2, .01);
		assertEquals(.15, air[0], 0);assertEquals(.2, air[1], 0);assertEquals(.01, air[2], 0);
		double[] gap = HostTriangleCollider.resolve(t, 25, 0, 0, .49, .98, 0, true, .15, -.04, 0);
		assertEquals(.15, gap[0], 0);assertEquals(-.04, gap[1], 0);assertEquals(0, gap[2], 0);
	}

	@Test
	void zeroStepNativeWallAndCeilingStaySolid() {
		List<HostTriangle> t = new ArrayList<>();
		flat(t, -5, -5, 5, 5, 0);wallX(t, 1, -5, 5, 0, 3);
		double[] wall = HostTriangleCollider.resolve(t, .3, 0, 0, .49, .98, 0, true, .4, -.04, 0);
		assertTrue(wall[0] < .4);assertTrue(.3 + wall[0] <= 1 - .49 + 1e-5);
		flat(t, -5, -5, 5, 5, 1.1);
		double[] ceiling = HostTriangleCollider.resolve(t, 0, 0, 0, .49, .98, 0, false, 0, .2, 0);
		assertEquals(.12, ceiling[1], 1e-7);
	}
}
