package dev.re4craft.render;

import dev.re4craft.protocol.Wire;
import java.nio.ByteBuffer;
import java.util.function.IntBinaryOperator;

/** Lossless source-texel copying and RGBA encoding; never scales a stitched atlas. */
public final class TexturePixels {
    public static final int MAX_DIMENSION=2048;
    @FunctionalInterface public interface PixelSink {void set(int x,int y,int argb);}
    private TexturePixels() { }
    /** Only full-source binary alpha is eligible for cutout rather than blending. */
    public static boolean binaryAlpha(byte[] rgbaPacket) {
        if(rgbaPacket.length<16 || (rgbaPacket.length-12)%4!=0)throw new IllegalArgumentException("Invalid texture pixels");
        for(int p=15;p<rgbaPacket.length;p+=4)if(rgbaPacket[p]!=0 && rgbaPacket[p]!=(byte)255)return false;
        return true;
    }
    public static byte[] encode(int id,int width,int height,IntBinaryOperator pixel) {
        long bytes=12L+4L*width*height;
        if(id<=0 || width<=0 || height<=0 || width>MAX_DIMENSION || height>MAX_DIMENSION || bytes>Wire.MAX_PAYLOAD)
            throw new IllegalArgumentException("Texture exceeds the lossless transport dimensions/payload");
        ByteBuffer result=Wire.buffer((int)bytes).putInt(id).putInt(width).putInt(height);
        for(int y=0;y<height;y++)for(int x=0;x<width;x++) {
            int argb=pixel.applyAsInt(x,y);
            result.put((byte)(argb>>16)).put((byte)(argb>>8)).put((byte)argb).put((byte)(argb>>>24));
        }
        return result.array();
    }
    public static void blit(int atlasWidth,int atlasHeight,int x,int y,int width,int height,
                            IntBinaryOperator source,PixelSink destination) {
        if(atlasWidth<=0 || atlasHeight<=0 || x<0 || y<0 || width<=0 || height<=0
                || (long)x+width>atlasWidth || (long)y+height>atlasHeight)
            throw new IllegalArgumentException("Sprite lies outside its unscaled stitched atlas");
        for(int sy=0;sy<height;sy++)for(int sx=0;sx<width;sx++)destination.set(x+sx,y+sy,source.applyAsInt(sx,sy));
    }
}
