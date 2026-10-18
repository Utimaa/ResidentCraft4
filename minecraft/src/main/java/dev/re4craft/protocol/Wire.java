package dev.re4craft.protocol;

import java.io.*;
import java.nio.*;
import java.nio.charset.StandardCharsets;
import java.nio.charset.CodingErrorAction;

/** Fixed little-endian layout shared with protocol/re4craft_protocol.h. */
public final class Wire {
    public static final int MAGIC = 0x4234434d, VERSION = 6, PORT = 18044;
    public static final int HIT_CRITICAL=1, HIT_PROJECTILE=2, HIT_FIRE=4, HIT_HAZARD=8, HIT_EXPLOSION=16, HIT_ALLY=32;
    public static final int DAMAGE_FATAL=1, DAMAGE_HAS_POSITION=4;
    public static final int ACTOR_ALIVE=1, ACTOR_DEAD=2, ACTOR_BREAKABLE=4, ACTOR_HOSTILE=8;
    public static final int BATCH_DOUBLE_SIDED=1, BATCH_TRANSLUCENT=2, BATCH_CRUMBLING=4, BATCH_PARTICLE=8, BATCH_WHITE_OVERLAY=16;
    public static final int INPUT_HOST_AUTHORITY=4, INPUT_GUEST_AUTHORITY=16, INPUT_NATIVE_TRAVERSAL=32;
    public static final int MOTION_NATIVE_TRAVERSAL_ACK=32;
    public static final int MAX_PAYLOAD = 32 * 1024 * 1024;
    public static final int STATE = 1, TEXTURE = 2, MESH = 3, HUD = 4, MOTION = 5, MELEE = 6, BLOCKS = 7,
            INPUT = 16, GOODBYE = 17, KEY_EVENT = 18, TEXT = 19, COLLISION = 20, ENEMIES = 21, DAMAGE = 22, LOOT = 23, MOVING_SURFACES = 24;
    public record Packet(int type, int sequence, byte[] payload) { }
    public record Input(int sequence, int flags, int held, int pressed, float yaw, float pitch,
                        int selectedSlot, int scroll, int hostEpoch, double hostX, double hostY, double hostZ,
                        int viewportWidth, int viewportHeight, float cursorX, float cursorY, int mouseButtons, int reserved) {
        public boolean active() { return (flags & 3) == 3 && (flags & 8) == 0; }
    }
    public record Key(int scancode, int action, int modifiers) { }
    public record Loot(int epoch,int eventId,int source,int tier,long sessionId,double x,double y,double z) { }
    public record ImageEncoding(int flags, byte[] pixels) { }
    private Wire() { }
    public static ByteBuffer buffer(int bytes) { return ByteBuffer.allocate(bytes).order(ByteOrder.LITTLE_ENDIAN); }
    /** Lossless RGBA32 run/literal encoding; raw frames are used when smaller. */
    public static ImageEncoding encodeImage(ByteBuffer source) {
        ByteBuffer rgba=source.slice().order(ByteOrder.LITTLE_ENDIAN);int bytes=rgba.remaining();
        if((bytes&3)!=0 || bytes>1920*1080*4)throw new IllegalArgumentException("Invalid image buffer");
        int pixels=bytes/4,index=0;byte[] literalScratch=null;
        ByteArrayOutputStream encoded=new ByteArrayOutputStream(Math.min(bytes,262144));
        while(index<pixels) {
            int value=rgba.getInt(index*4),run=1;
            while(index+run<pixels && rgba.getInt((index+run)*4)==value)run++;
            if(run>=3){putInt(encoded,0x80000000|run);putInt(encoded,value);index+=run;}
            else {
                int start=index;index+=run;
                while(index<pixels && index-start<65536) {
                    int next=rgba.getInt(index*4),remaining=pixels-index;
                    if(remaining>=3 && next==rgba.getInt((index+1)*4) && next==rgba.getInt((index+2)*4))break;
                    index++;
                }
                putInt(encoded,index-start);
                int literalBytes=(index-start)*4;
                if(rgba.hasArray())encoded.write(rgba.array(),rgba.arrayOffset()+start*4,literalBytes);
                else {
                    if(literalScratch==null)literalScratch=new byte[Math.min(bytes,65536*4)];
                    rgba.get(start*4,literalScratch,0,literalBytes);encoded.write(literalScratch,0,literalBytes);
                }
            }
            if(encoded.size()>=bytes){byte[] raw=new byte[bytes];rgba.get(raw);return new ImageEncoding(0,raw);}
        }
        return new ImageEncoding(2,encoded.toByteArray());
    }
    private static void putInt(ByteArrayOutputStream stream,int value){stream.write(value);stream.write(value>>>8);stream.write(value>>>16);stream.write(value>>>24);}
    public static Packet read(InputStream stream) throws IOException {
        byte[] header = stream.readNBytes(16);
        if (header.length == 0) return null;
        if (header.length != 16) throw new EOFException("Truncated RE4Craft header");
        ByteBuffer h = ByteBuffer.wrap(header).order(ByteOrder.LITTLE_ENDIAN);
        int magic = h.getInt(), version = Short.toUnsignedInt(h.getShort());
        int type = Short.toUnsignedInt(h.getShort()), size = h.getInt(), sequence = h.getInt();
        if (magic != MAGIC || version != VERSION || size < 0 || size > MAX_PAYLOAD)
            throw new IOException("Invalid RE4Craft packet header");
        if (type != INPUT && type != GOODBYE && type != KEY_EVENT && type != TEXT && type != COLLISION && type != MOVING_SURFACES && type != ENEMIES && type != DAMAGE && type != LOOT)
            throw new IOException("Unsupported host packet type " + type);
        if ((type == INPUT && size != 80) || (type == GOODBYE && size != 0)
                || (type == KEY_EVENT && size != 16) || (type == TEXT && size > 4096)
                || (type == COLLISION && (size < 32 || ((size-32)%36 != 0 && (size-32)%48 != 0)))
                || (type == MOVING_SURFACES && (size < 24 || (size-24)%60 != 0 || size > 24+4096*60))
                || (type == ENEMIES && (size < 16 || (size-16)%72 != 0 || size > 16+512*72))
                || (type == DAMAGE && size != 48) || (type == LOOT && size != 48))
            throw new IOException("Invalid host payload length");
        byte[] payload = stream.readNBytes(size);
        if (payload.length != size) throw new EOFException("Truncated RE4Craft payload");
        return new Packet(type, sequence, payload);
    }
    public static Input input(Packet packet) throws IOException {
        if (packet.type != INPUT || packet.payload.length != 80) throw new IOException("Expected INPUT80");
        ByteBuffer b = ByteBuffer.wrap(packet.payload).order(ByteOrder.LITTLE_ENDIAN);
        Input value = new Input(packet.sequence, b.getInt(), b.getInt(), b.getInt(), b.getFloat(), b.getFloat(),
                b.getInt(), b.getInt(), b.getInt(), b.getDouble(), b.getDouble(), b.getDouble(),
                b.getInt(), b.getInt(), b.getFloat(), b.getFloat(), b.getInt(), b.getInt());
        if ((value.flags & ~63) != 0 || (value.held & ~511) != 0 || (value.pressed & ~127) != 0
                || (value.flags & 20) == 20
                || ((value.flags & INPUT_NATIVE_TRAVERSAL)!=0 && (value.flags & 20)!=INPUT_HOST_AUTHORITY)
                || !Float.isFinite(value.yaw) || !Float.isFinite(value.pitch) || Math.abs(value.pitch) > 90
                || !Double.isFinite(value.hostX) || !Double.isFinite(value.hostY) || !Double.isFinite(value.hostZ)
                || (value.selectedSlot != -1 && (value.selectedSlot < 0 || value.selectedSlot > 8))
                || Math.abs((long) value.scroll) > 7680
                || value.viewportWidth < 0 || value.viewportWidth > 16384 || value.viewportHeight < 0 || value.viewportHeight > 16384
                || !Float.isFinite(value.cursorX) || !Float.isFinite(value.cursorY)
                || value.cursorX < 0 || value.cursorX > 1 || value.cursorY < 0 || value.cursorY > 1
                || (value.mouseButtons & ~7) != 0 || value.reserved != 0)
            throw new IOException("Invalid RE4Craft input values");
        return value;
    }
    public static Key key(Packet packet) throws IOException {
        if(packet.type != KEY_EVENT || packet.payload.length != 16)throw new IOException("Expected KEY_EVENT16");
        ByteBuffer b=ByteBuffer.wrap(packet.payload).order(ByteOrder.LITTLE_ENDIAN);
        Key key=new Key(b.getInt(),b.getInt(),b.getInt());int reserved=b.getInt();
        if(key.scancode<=0 || key.scancode>=512 || key.action < -1 || key.action>1 || (key.modifiers & ~0xffff)!=0 || reserved!=0)
            throw new IOException("Invalid key event");
        return key;
    }
    public static String text(Packet packet) throws IOException {
        if(packet.type != TEXT || packet.payload.length>4096)throw new IOException("Invalid text packet");
        return StandardCharsets.UTF_8.newDecoder().onMalformedInput(CodingErrorAction.REPORT)
                .onUnmappableCharacter(CodingErrorAction.REPORT).decode(ByteBuffer.wrap(packet.payload)).toString();
    }
    /** Validate variable host actor records before they reach the game thread. */
    public static void enemies(Packet packet) throws IOException {
        if(packet.type != ENEMIES || packet.payload.length < 16)throw new IOException("Expected ENEMIES");
        ByteBuffer b=ByteBuffer.wrap(packet.payload).order(ByteOrder.LITTLE_ENDIAN);
        b.getInt();int count=b.getInt();b.getLong();
        if(count<0 || count>512 || packet.payload.length != 16+72*count)throw new IOException("Invalid enemy count");
        java.util.HashSet<Integer> ids=new java.util.HashSet<>();
        for(int i=0;i<count;i++) {
            int id=b.getInt(),flags=b.getInt();
            if(id<=0 || !ids.add(id) || (flags & ~15)!=0 || (flags&(ACTOR_BREAKABLE|ACTOR_HOSTILE))==(ACTOR_BREAKABLE|ACTOR_HOSTILE))throw new IOException("Invalid enemy identity");
            for(int axis=0;axis<3;axis++){double v=b.getDouble();if(!Double.isFinite(v)||Math.abs(v)>30_000_000)throw new IOException("Invalid enemy position");}
            float[] bounds=new float[6];for(int j=0;j<6;j++)bounds[j]=b.getFloat();
            for(int axis=0;axis<3;axis++)if(!Float.isFinite(bounds[axis]) || !Float.isFinite(bounds[axis+3])
                    || bounds[axis]>bounds[axis+3] || Math.abs(bounds[axis])>128 || Math.abs(bounds[axis+3])>128)throw new IOException("Invalid enemy bounds");
            float health=b.getFloat(),max=b.getFloat(),yaw=b.getFloat();int kind=b.getInt();
            if(!Float.isFinite(health)||!Float.isFinite(max)||!Float.isFinite(yaw)||health<0||max<=0||health>max||kind<0||kind>65535)
                throw new IOException("Invalid enemy state");
        }
    }
    public static void damage(Packet packet) throws IOException {
        if(packet.type != DAMAGE || packet.payload.length != 48)throw new IOException("Expected DAMAGE48");
        ByteBuffer b=ByteBuffer.wrap(packet.payload).order(ByteOrder.LITTLE_ENDIAN);
        b.getInt();int event=b.getInt();float amount=b.getFloat();int kind=b.getInt(),attacker=b.getInt(),flags=b.getInt();
        double x=b.getDouble(),y=b.getDouble(),z=b.getDouble();
        if(event==0 || !Float.isFinite(amount)||amount<=0||amount>1024||kind<0||kind>16||attacker<0||(flags & ~7)!=0
                || !Double.isFinite(x)||!Double.isFinite(y)||!Double.isFinite(z)
                || Math.abs(x)>30_000_000||Math.abs(y)>30_000_000||Math.abs(z)>30_000_000
                || ((flags&DAMAGE_HAS_POSITION)!=0 && (kind!=1 || (flags&DAMAGE_FATAL)!=0)))
            throw new IOException("Invalid host damage");
    }
    public static Loot loot(Packet packet) throws IOException {
        if(packet.type!=LOOT || packet.payload.length!=48)throw new IOException("Expected LOOT48");
        ByteBuffer b=ByteBuffer.wrap(packet.payload).order(ByteOrder.LITTLE_ENDIAN);
        Loot loot=new Loot(b.getInt(),b.getInt(),b.getInt(),b.getInt(),b.getLong(),b.getDouble(),b.getDouble(),b.getDouble());
        if(loot.epoch==0 || loot.eventId==0 || loot.sessionId==0 || loot.source<1 || loot.source>2 || loot.tier<0 || loot.tier>5
                || !Double.isFinite(loot.x) || !Double.isFinite(loot.y) || !Double.isFinite(loot.z)
                || Math.abs(loot.x)>30_000_000 || Math.abs(loot.y)>30_000_000 || Math.abs(loot.z)>30_000_000)
            throw new IOException("Invalid host loot");
        return loot;
    }
    public static void write(OutputStream stream, Packet packet) throws IOException {
        if (packet.payload.length > MAX_PAYLOAD) throw new IOException("Payload too large");
        stream.write(buffer(16).putInt(MAGIC).putShort((short) VERSION).putShort((short) packet.type)
                .putInt(packet.payload.length).putInt(packet.sequence).array());
        stream.write(packet.payload);
    }
}
