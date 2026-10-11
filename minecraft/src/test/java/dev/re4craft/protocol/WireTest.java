package dev.re4craft.protocol;

import java.io.*;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class WireTest {
    private byte[] input(float yaw) { return Wire.buffer(80).putInt(3).putInt(1).putInt(2).putFloat(yaw).putFloat(0)
            .putInt(-1).putInt(0).putInt(1).putDouble(1).putDouble(2).putDouble(3)
            .putInt(1920).putInt(1080).putFloat(.25f).putFloat(.75f).putInt(1).putInt(0).array(); }
    @Test void fragmentedReadsMatchCppLayout() throws Exception {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        Wire.write(out, new Wire.Packet(Wire.INPUT, 77, input(42)));
        assertArrayEquals(new byte[] {'M','C','4','B',6,0,16,0,80,0,0,0,77,0,0,0}, java.util.Arrays.copyOf(out.toByteArray(), 16));
        InputStream fragmented = new FilterInputStream(new ByteArrayInputStream(out.toByteArray())) {
            @Override public int read(byte[] b, int off, int len) throws IOException { return super.read(b, off, Math.min(3,len)); }
        };
        var parsed = Wire.input(Wire.read(fragmented));
        assertEquals(77, parsed.sequence()); assertEquals(42, parsed.yaw()); assertTrue(parsed.active());
        assertEquals(3, parsed.hostZ());
        assertEquals(1920,parsed.viewportWidth());assertEquals(.75f,parsed.cursorY());assertEquals(1,parsed.mouseButtons());
    }
    @Test void rejectsOversizedPayloadBeforeAllocation() {
        byte[] bad = Wire.buffer(16).putInt(Wire.MAGIC).putShort((short)Wire.VERSION).putShort((short)16)
                .putInt(Wire.MAX_PAYLOAD + 1).putInt(0).array();
        assertThrows(IOException.class, () -> Wire.read(new ByteArrayInputStream(bad)));
    }
    @Test void previousVersionCannotSilentlyAcceptNewDamageContract() {
        byte[] previous=Wire.buffer(16).putInt(Wire.MAGIC).putShort((short)4).putShort((short)16).putInt(0).putInt(1).array();
        assertThrows(IOException.class,()->Wire.read(new ByteArrayInputStream(previous)));
    }
    @Test void guiInputAndTextRemainBounded() throws Exception {
        var key=Wire.key(new Wire.Packet(Wire.KEY_EVENT,1,Wire.buffer(16).putInt(62).putInt(1).putInt(0).putInt(0).array()));
        assertEquals(62,key.scancode());
        assertEquals("Steve é",Wire.text(new Wire.Packet(Wire.TEXT,2,"Steve é".getBytes(java.nio.charset.StandardCharsets.UTF_8))));
        assertThrows(IOException.class,()->Wire.text(new Wire.Packet(Wire.TEXT,3,new byte[]{(byte)0xff})));
        assertThrows(IOException.class,()->Wire.key(new Wire.Packet(Wire.KEY_EVENT,4,Wire.buffer(16).putInt(512).putInt(1).putInt(0).putInt(0).array())));
        byte[] malformed=input(0);java.nio.ByteBuffer.wrap(malformed).order(java.nio.ByteOrder.LITTLE_ENDIAN).putFloat(64,Float.NaN);
        assertThrows(IOException.class,()->Wire.input(new Wire.Packet(Wire.INPUT,5,malformed)));
    }
    @Test void rejectsTruncationAndNanInput() {
        assertThrows(EOFException.class, () -> Wire.read(new ByteArrayInputStream(new byte[] {1,2})));
        assertThrows(IOException.class, () -> Wire.input(new Wire.Packet(Wire.INPUT,1,input(Float.NaN))));
    }
    @Test void lootUsesStableSessionIdentityAndBoundedActualCoordinates() throws Exception {
        byte[] bytes=Wire.buffer(48).putInt(7).putInt(-1).putInt(2).putInt(3).putLong(Long.MIN_VALUE)
                .putDouble(-123).putDouble(2.5).putDouble(456).array();
        ByteArrayOutputStream stream=new ByteArrayOutputStream();
        Wire.write(stream,new Wire.Packet(Wire.LOOT,9,bytes));
        var loot=Wire.loot(Wire.read(new ByteArrayInputStream(stream.toByteArray())));
        assertEquals(-1,loot.eventId());assertEquals(Long.MIN_VALUE,loot.sessionId());assertEquals(3,loot.tier());assertEquals(2.5,loot.y());
        for(int offset:new int[]{0,4,8}) {
            byte[] malformed=bytes.clone();java.nio.ByteBuffer.wrap(malformed).order(java.nio.ByteOrder.LITTLE_ENDIAN).putInt(offset,0);
            assertThrows(IOException.class,()->Wire.loot(new Wire.Packet(Wire.LOOT,1,malformed)));
        }
        for(int tier:new int[]{-1,6}) {
            byte[] malformed=bytes.clone();java.nio.ByteBuffer.wrap(malformed).order(java.nio.ByteOrder.LITTLE_ENDIAN).putInt(12,tier);
            assertThrows(IOException.class,()->Wire.loot(new Wire.Packet(Wire.LOOT,1,malformed)));
        }
        byte[] finalChapter=bytes.clone();java.nio.ByteBuffer.wrap(finalChapter).order(java.nio.ByteOrder.LITTLE_ENDIAN).putInt(12,5);
        assertEquals(5,Wire.loot(new Wire.Packet(Wire.LOOT,1,finalChapter)).tier());
        for(double coordinate:new double[]{Double.NaN,Double.POSITIVE_INFINITY,30_000_001}) {
            byte[] malformed=bytes.clone();java.nio.ByteBuffer.wrap(malformed).order(java.nio.ByteOrder.LITTLE_ENDIAN).putDouble(24,coordinate);
            assertThrows(IOException.class,()->Wire.loot(new Wire.Packet(Wire.LOOT,1,malformed)));
        }
        byte[] malformed=bytes.clone();java.nio.ByteBuffer.wrap(malformed).order(java.nio.ByteOrder.LITTLE_ENDIAN).putLong(16,0);
        assertThrows(IOException.class,()->Wire.loot(new Wire.Packet(Wire.LOOT,1,malformed)));
        assertThrows(IOException.class,()->Wire.loot(new Wire.Packet(Wire.LOOT,1,new byte[47])));
    }
    @Test void supportsGuestAuthorityAndRejectsConflictingAuthorities() throws Exception {
        byte[] bytes=input(0);var buffer=java.nio.ByteBuffer.wrap(bytes).order(java.nio.ByteOrder.LITTLE_ENDIAN);
        buffer.putInt(0,19);buffer.putInt(8,96);
        var parsed=Wire.input(new Wire.Packet(Wire.INPUT,1,bytes));assertTrue(parsed.active());assertEquals(96,parsed.pressed());
        buffer.putInt(0,23);assertThrows(IOException.class,()->Wire.input(new Wire.Packet(Wire.INPUT,1,bytes)));
    }
    @Test void rgbaCompressionRetainsPixelsAndFallsBackForNoise(){
        var rgba=Wire.buffer(400);for(int i=0;i<100;i++)rgba.putInt(0x12345678);rgba.flip();
        var image=Wire.encodeImage(rgba);assertEquals(2,image.flags());assertArrayEquals(Wire.buffer(8).putInt(0x80000064).putInt(0x12345678).array(),image.pixels());
        var noise=Wire.buffer(400);for(int i=0;i<100;i++)noise.putInt(i);noise.flip();var raw=Wire.encodeImage(noise);
        assertEquals(0,raw.flags());assertArrayEquals(noise.array(),raw.pixels());
        assertThrows(IllegalArgumentException.class,()->Wire.encodeImage(Wire.buffer(3)));
    }
    @Test void combatPacketsRejectDuplicateActorsAndInvalidDamage() throws Exception {
        byte[] damage=Wire.buffer(48).putInt(4).putInt(1).putFloat(6).putInt(1).putInt(12).putInt(0).putDouble(0).putDouble(0).putDouble(0).array();
        Wire.damage(new Wire.Packet(Wire.DAMAGE,5,damage));
        java.nio.ByteBuffer.wrap(damage).order(java.nio.ByteOrder.LITTLE_ENDIAN).putFloat(8,Float.NaN);
        assertThrows(IOException.class,()->Wire.damage(new Wire.Packet(Wire.DAMAGE,5,damage)));
        var actors=Wire.buffer(160).putInt(4).putInt(2).putLong(20);
        for(int i=0;i<2;i++)actors.putInt(3).putInt(1).putDouble(0).putDouble(0).putDouble(2)
            .putFloat(-.3f).putFloat(0).putFloat(-.3f).putFloat(.3f).putFloat(1.8f).putFloat(.3f)
            .putFloat(20).putFloat(20).putFloat(0).putInt(18);
        assertThrows(IOException.class,()->Wire.enemies(new Wire.Packet(Wire.ENEMIES,6,actors.array())));
    }
    @Test void positionedDamageRejectsUnknownFatalAndNonfiniteOrigins() throws Exception {
        var packet=Wire.buffer(48).putInt(4).putInt(1).putFloat(6).putInt(1).putInt(0).putInt(Wire.DAMAGE_HAS_POSITION)
                .putDouble(10).putDouble(2).putDouble(-4);
        Wire.damage(new Wire.Packet(Wire.DAMAGE,1,packet.array()));
        packet.putInt(20,Wire.DAMAGE_HAS_POSITION|Wire.DAMAGE_FATAL);
        assertThrows(IOException.class,()->Wire.damage(new Wire.Packet(Wire.DAMAGE,1,packet.array())));
        packet.putInt(20,Wire.DAMAGE_HAS_POSITION);packet.putInt(12,0);
        assertThrows(IOException.class,()->Wire.damage(new Wire.Packet(Wire.DAMAGE,1,packet.array())));
        packet.putInt(12,1);packet.putDouble(24,Double.NaN);
        assertThrows(IOException.class,()->Wire.damage(new Wire.Packet(Wire.DAMAGE,1,packet.array())));
        packet.putDouble(24,30_000_001);
        assertThrows(IOException.class,()->Wire.damage(new Wire.Packet(Wire.DAMAGE,1,packet.array())));
        assertThrows(IOException.class,()->Wire.damage(new Wire.Packet(Wire.DAMAGE,1,new byte[24])));
    }
    @Test void nativeHostilityFlagRetainsLayoutAndRejectsHostileCrates() throws Exception {
        var actors=Wire.buffer(88).putInt(4).putInt(1).putLong(20)
                .putInt(3).putInt(Wire.ACTOR_ALIVE|Wire.ACTOR_HOSTILE)
                .putDouble(0).putDouble(0).putDouble(2)
                .putFloat(-.3f).putFloat(0).putFloat(-.3f).putFloat(.3f).putFloat(1.8f).putFloat(.3f)
                .putFloat(20).putFloat(20).putFloat(0).putInt(0x14);
        Wire.enemies(new Wire.Packet(Wire.ENEMIES,1,actors.array()));
        actors.putInt(20,Wire.ACTOR_ALIVE|Wire.ACTOR_HOSTILE|Wire.ACTOR_BREAKABLE);
        assertThrows(IOException.class,()->Wire.enemies(new Wire.Packet(Wire.ENEMIES,1,actors.array())));
        actors.putInt(20,Wire.ACTOR_ALIVE|16);
        assertThrows(IOException.class,()->Wire.enemies(new Wire.Packet(Wire.ENEMIES,1,actors.array())));
    }
    @Test void largeLiteralRunsPreserveHeapSlicesAndDirectGpuBytes(){
        var expected=Wire.buffer(70200*4);
        for(int i=0;i<100;i++)expected.putInt(0);
        for(int i=0;i<70000;i++)expected.putInt(0xff000000|i);
        for(int i=0;i<100;i++)expected.putInt(0);
        for(boolean direct:new boolean[]{false,true}){
            var source=direct?java.nio.ByteBuffer.allocateDirect(expected.capacity()+16):Wire.buffer(expected.capacity()+16);
            source.position(8);source.put(expected.array());source.limit(source.position());source.position(8);
            var encoded=Wire.encodeImage(source);assertEquals(2,encoded.flags());
            var data=java.nio.ByteBuffer.wrap(encoded.pixels()).order(java.nio.ByteOrder.LITTLE_ENDIAN);
            var decoded=Wire.buffer(expected.capacity());
            while(data.hasRemaining()){
                int token=data.getInt(),count=token&0x7fffffff;assertTrue(count>0);
                if(token<0){int pixel=data.getInt();for(int i=0;i<count;i++)decoded.putInt(pixel);}
                else {assertTrue(count<=65536);for(int i=0;i<count;i++)decoded.putInt(data.getInt());}
            }
            assertEquals(expected.capacity(),decoded.position());assertArrayEquals(expected.array(),decoded.array());
            assertEquals(8,source.position());
        }
    }
}
