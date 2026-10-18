package dev.re4craft.client;

import dev.re4craft.protocol.Wire;
import dev.re4craft.protocol.LatestFrameMailbox;
import java.io.*;
import java.net.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.*;

/** Socket workers never call Minecraft APIs. Frames can be replaced; textures cannot. */
public final class BridgeServer implements AutoCloseable {
    private final ArrayBlockingQueue<Wire.Packet> inputs = new ArrayBlockingQueue<>(256);
    private final LatestFrameMailbox frames=new LatestFrameMailbox();
    private final AtomicReference<Wire.Packet> collision=new AtomicReference<>();
    private final AtomicReference<Wire.Packet> movingSurfaces=new AtomicReference<>();
    private final AtomicInteger sequence = new AtomicInteger(), generation = new AtomicInteger();
    private volatile Socket peer;
    private volatile ServerSocket listener;
    private volatile boolean running = true;
    public void start() { Thread.ofPlatform().daemon().name("RE4Craft listener").start(this::serve); }
    public int generation() { return generation.get(); }
    public boolean connected() { return peer != null && !peer.isClosed(); }
    public Wire.Packet pollEvent() { return inputs.poll(); }
    public byte[] pollCollision() { var packet=collision.getAndSet(null); return packet==null?null:packet.payload(); }
    public byte[] pollMovingSurfaces() {var packet=movingSurfaces.getAndSet(null);return packet==null?null:packet.payload();}
    public void publishState(byte[] payload) { frames.publish(peer,LatestFrameMailbox.Slot.STATE,packet(Wire.STATE,payload)); }
    public void publishMesh(byte[] payload) {
        Socket socket=peer;if(socket!=null) {
            int category=java.nio.ByteBuffer.wrap(payload).order(java.nio.ByteOrder.LITTLE_ENDIAN).getInt();
            frames.publish(socket,category==2?LatestFrameMailbox.Slot.SCENE:category==3?LatestFrameMailbox.Slot.ENTITIES:LatestFrameMailbox.Slot.AVATAR,packet(Wire.MESH,payload));
        }
    }
    public void publishHud(byte[] payload) { frames.publish(peer,LatestFrameMailbox.Slot.HUD,packet(Wire.HUD,payload)); }
    public void publishMotion(byte[] payload) { frames.publish(peer,LatestFrameMailbox.Slot.MOTION,packet(Wire.MOTION,payload)); }
    public void publishBlockColliders(byte[] payload) { frames.publish(peer,LatestFrameMailbox.Slot.BLOCKS,packet(Wire.BLOCKS,payload)); }
    public boolean publishMelee(byte[] payload) { return frames.offerReliable(peer,packet(Wire.MELEE,payload)); }
    public boolean publishTexture(byte[] payload) { return frames.offerReliable(peer,packet(Wire.TEXTURE,payload)); }
    private Wire.Packet packet(int type, byte[] bytes) { return new Wire.Packet(type, sequence.incrementAndGet(), bytes); }
    private void serve() {
        try (ServerSocket server = new ServerSocket()) {
            server.setReuseAddress(true);
            int port=Integer.getInteger("re4craft.port",Wire.PORT);
            if(port<1024 || port>65535)throw new IOException("Invalid development bridge port");
            server.bind(new InetSocketAddress(InetAddress.getByName("127.0.0.1"), port), 1);
            listener = server;
            Re4CraftClient.LOG.info("RE4Craft listening at 127.0.0.1:{}", port);
            while (running) {
                try (Socket socket = server.accept()) {
                    socket.setTcpNoDelay(true);
                    socket.setSoTimeout(5000);
                    clear();
                    peer = socket;
                    frames.begin(socket);
                    generation.incrementAndGet();
                    Thread writer = Thread.ofPlatform().daemon().name("RE4Craft writer").start(() -> write(socket));
                    Re4CraftClient.LOG.info("RE4Craft host connected");
                    try {
                        while (running && !socket.isClosed()) {
                            Wire.Packet packet = Wire.read(socket.getInputStream());
                            if (packet == null || packet.type() == Wire.GOODBYE) break;
                            if(packet.type()==Wire.COLLISION) {collision.set(packet);continue;}
                            if(packet.type()==Wire.MOVING_SURFACES) {movingSurfaces.set(packet);continue;}
                            if(packet.type()==Wire.INPUT)Wire.input(packet);
                            else if(packet.type()==Wire.KEY_EVENT)Wire.key(packet);
                            else if(packet.type()==Wire.TEXT)Wire.text(packet);
                            else if(packet.type()==Wire.ENEMIES)Wire.enemies(packet);
                            else if(packet.type()==Wire.DAMAGE)Wire.damage(packet);
                            else if(packet.type()==Wire.LOOT)Wire.loot(packet);
                            else throw new IOException("Unexpected host event");
                            if (!inputs.offer(packet)) throw new IOException("Host input queue overflow");
                        }
                    } finally {
                        frames.end(socket);
                        peer = null;
                        socket.close();
                        writer.interrupt();
                        clear();
                        Re4CraftClient.LOG.info("RE4Craft host disconnected; virtual input will release");
                    }
                } catch (IOException e) {
                    if (running) Re4CraftClient.LOG.warn("RE4Craft connection ended: {}", e.getMessage());
                }
            }
        } catch (IOException e) {
            Re4CraftClient.LOG.error("RE4Craft listener unavailable: {}", e.getMessage());
        } finally { peer = null; listener = null; }
    }
    private void write(Socket socket) {
        try {
            OutputStream output = new BufferedOutputStream(socket.getOutputStream());
            java.util.List<Wire.Packet> packets = new java.util.ArrayList<>(71);
            while (running && !socket.isClosed() && peer == socket) {
                if(!frames.awaitDrain(socket,packets))break;
                // The snapshot lock guarantees a texture is written before any frame referencing it.
                for (Wire.Packet packet : packets) Wire.write(output,packet);
                if (!packets.isEmpty()) output.flush();
            }
        } catch (IOException | InterruptedException e) {
            try { socket.close(); } catch (IOException ignored) { }
        }
    }
    private void clear() {
        inputs.clear();collision.set(null);movingSurfaces.set(null);
    }
    @Override public void close() {
        running = false;
        frames.end(peer);
        try { if (peer != null) peer.close(); } catch (IOException ignored) { }
        try { if (listener != null) listener.close(); } catch (IOException ignored) { }
    }
}
