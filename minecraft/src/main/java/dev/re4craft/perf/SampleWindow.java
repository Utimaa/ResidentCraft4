package dev.re4craft.perf;

import java.util.Arrays;

/** Opt-in profiling only: bounded nanosecond samples, with an atomic window reset. */
public final class SampleWindow {
    private final long[] samples=new long[2048];
    private long count,sum,maximum;
    public synchronized void add(long nanos) {
        if(nanos<0)return;
        samples[(int)(count%samples.length)]=nanos;
        count++;sum+=nanos;maximum=Math.max(maximum,nanos);
    }
    public synchronized Summary take() {
        int size=(int)Math.min(count,samples.length);
        long[] sorted=Arrays.copyOf(samples,size);Arrays.sort(sorted);
        Summary result=new Summary(count,count==0?0:(double)sum/count/1e6,
                percentile(sorted,.5),percentile(sorted,.95),percentile(sorted,.99),maximum/1e6);
        count=sum=maximum=0;return result;
    }
    private static double percentile(long[] sorted,double fraction) {
        return sorted.length==0?0:sorted[Math.max(0,(int)Math.ceil(sorted.length*fraction)-1)]/1e6;
    }
    public record Summary(long count,double avg,double p50,double p95,double p99,double max) {
        @Override public String toString() {
            return String.format(java.util.Locale.ROOT,"n=%d avg=%.3f p50=%.3f p95=%.3f p99=%.3f max=%.3fms",count,avg,p50,p95,p99,max);
        }
    }
}
