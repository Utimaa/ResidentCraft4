package dev.re4craft.perf;

import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

final class SampleWindowTest {
    @Test void outlierTailsAreReportedRatherThanHiddenInAnAverage() {
        var window=new SampleWindow();
        for(int i=0;i<98;i++)window.add(16_000_000);
        window.add(35_000_000);window.add(80_000_000);
        var summary=window.take();
        assertEquals(100,summary.count());assertEquals(16,summary.p95());
        assertEquals(35,summary.p99());assertEquals(80,summary.max());
        assertEquals(16.83,summary.avg(),1e-9);
        assertEquals(0,window.take().count());
    }
    @Test void boundedSamplesStillRetainTotalCountAndFreshTail() {
        var window=new SampleWindow();
        for(int i=0;i<5000;i++)window.add(1_000_000);
        for(int i=0;i<2048;i++)window.add(2_000_000);
        var summary=window.take();assertEquals(7048,summary.count());
        assertEquals(2,summary.p50());assertEquals(2,summary.p99());
        assertEquals(2,summary.max());
    }
}
