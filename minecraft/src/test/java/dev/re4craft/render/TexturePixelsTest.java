package dev.re4craft.render;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

final class TexturePixelsTest {
    @Test void binaryCutoutQualificationPreservesIntermediateFluidAndGhostAlpha() {
        assertTrue(TexturePixels.binaryAlpha(TexturePixels.encode(1,2,1,(x,y)->x==0?0x00010203:0xffabcdef)));
        for(int alpha:new int[]{1,63,127,254})
            assertFalse(TexturePixels.binaryAlpha(TexturePixels.encode(1,2,1,(x,y)->(x==0?255:alpha)<<24|0x123456)));
    }
    @Test void stitchedSixteenTexelSpriteRetainsEverySourceTexel() {
        int[] atlas=new int[64*64];
        TexturePixels.blit(64,64,16,32,16,16,(x,y)->0xff000000|(x<<16)|(y<<8)|(x^y),(x,y,p)->atlas[y*64+x]=p);
        byte[] packet=TexturePixels.encode(7,64,64,(x,y)->atlas[y*64+x]);
        for(int y=0;y<16;y++)for(int x=0;x<16;x++) {
            int at=12+4*((32+y)*64+16+x);
            assertEquals(x,Byte.toUnsignedInt(packet[at]));assertEquals(y,Byte.toUnsignedInt(packet[at+1]));
            assertEquals(x^y,Byte.toUnsignedInt(packet[at+2]));assertEquals(255,Byte.toUnsignedInt(packet[at+3]));
        }
        assertEquals(0,atlas[31*64+16]);assertEquals(0,atlas[32*64+32]);
    }
    @Test void fullVanilla2048AtlasFitsWithoutResampling() {
        byte[] packet=TexturePixels.encode(9,2048,2048,(x,y)->0x7f000000|((x&255)<<16)|((y&255)<<8)|(x^y)&255);
        assertEquals(16*1024*1024+12,packet.length);
        ByteBuffer prefix=ByteBuffer.wrap(packet).order(ByteOrder.LITTLE_ENDIAN);
        assertEquals(9,prefix.getInt());assertEquals(2048,prefix.getInt());assertEquals(2048,prefix.getInt());
        int last=packet.length-4;assertEquals(255,Byte.toUnsignedInt(packet[last]));assertEquals(255,Byte.toUnsignedInt(packet[last+1]));assertEquals(0,Byte.toUnsignedInt(packet[last+2]));assertEquals(127,Byte.toUnsignedInt(packet[last+3]));
    }
    @Test void oversizedTexturesAreRejectedInsteadOfSilentlyReducingPixels() {
        assertThrows(IllegalArgumentException.class,()->TexturePixels.encode(1,4096,2048,(x,y)->0));
        assertThrows(IllegalArgumentException.class,()->TexturePixels.blit(64,64,60,0,16,16,(x,y)->0,(x,y,p)->{}));
    }
}
