package dev.re4craft.protocol;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.*;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

final class LatestFrameMailboxTest {
    private static Wire.Packet packet(int type,int sequence){return new Wire.Packet(type,sequence,new byte[0]);}
    @Test void resourceFifoPrecedesAnyReferencingLatestMeshAndFramesStayBounded() throws Exception {
        var mailbox=new LatestFrameMailbox();Object peer=new Object();mailbox.begin(peer);
        for(int i=1;i<=64;i++)assertTrue(mailbox.offerReliable(peer,packet(Wire.TEXTURE,i)));
        assertFalse(mailbox.offerReliable(peer,packet(Wire.TEXTURE,65)));
        for(int i=0;i<10000;i++)assertTrue(mailbox.publish(peer,LatestFrameMailbox.Slot.AVATAR,packet(Wire.MESH,100+i)));
        assertTrue(mailbox.publish(peer,LatestFrameMailbox.Slot.HUD,packet(Wire.HUD,10100)));
        var output=new ArrayList<Wire.Packet>();assertTrue(mailbox.awaitDrain(peer,output));
        assertEquals(66,output.size());
        for(int i=0;i<64;i++){assertEquals(Wire.TEXTURE,output.get(i).type());assertEquals(i+1,output.get(i).sequence());}
        assertEquals(10099,output.get(64).sequence());assertEquals(Wire.HUD,output.get(65).type());
    }
    @Test void newFrameWakesBlockedWriterAndDrainClearsTheReusedList() throws Exception {
        var mailbox=new LatestFrameMailbox();Object peer=new Object();mailbox.begin(peer);
        var output=new ArrayList<Wire.Packet>();output.add(packet(Wire.HUD,999));
        var result=new CompletableFuture<Boolean>();var entered=new CountDownLatch(1);
        Thread writer=Thread.ofPlatform().daemon().start(()->{try{entered.countDown();result.complete(mailbox.awaitDrain(peer,output));}catch(Throwable error){result.completeExceptionally(error);}});
        assertTrue(entered.await(1,TimeUnit.SECONDS));
        assertThrows(TimeoutException.class,()->result.get(50,TimeUnit.MILLISECONDS));
        assertEquals(Thread.State.WAITING,writer.getState());
        assertTrue(mailbox.publish(peer,LatestFrameMailbox.Slot.AVATAR,packet(Wire.MESH,1)));
        assertTrue(result.get(1,TimeUnit.SECONDS));assertEquals(List.of(1),output.stream().map(Wire.Packet::sequence).toList());
    }
    @Test void disconnectWakesIdleWriterAndOldPeerCannotOwnReconnectedFrames() throws Exception {
        var mailbox=new LatestFrameMailbox();Object oldPeer=new Object(),newPeer=new Object();mailbox.begin(oldPeer);
        var closed=new CompletableFuture<Boolean>();
        Thread.ofPlatform().daemon().start(()->{try{closed.complete(mailbox.awaitDrain(oldPeer,new ArrayList<>()));}catch(Throwable error){closed.completeExceptionally(error);}});
        mailbox.end(oldPeer);assertFalse(closed.get(1,TimeUnit.SECONDS));
        mailbox.begin(newPeer);
        assertFalse(mailbox.publish(oldPeer,LatestFrameMailbox.Slot.AVATAR,packet(Wire.MESH,1)));
        assertFalse(mailbox.offerReliable(oldPeer,packet(Wire.TEXTURE,2)));
        mailbox.end(oldPeer); // an old reader's cleanup must not end the new socket
        assertTrue(mailbox.offerReliable(newPeer,packet(Wire.TEXTURE,3)));
        assertTrue(mailbox.publish(newPeer,LatestFrameMailbox.Slot.AVATAR,packet(Wire.MESH,4)));
        var output=new ArrayList<Wire.Packet>();assertTrue(mailbox.awaitDrain(newPeer,output));
        assertEquals(List.of(3,4),output.stream().map(Wire.Packet::sequence).toList());
    }
    @Test void reconnectClearsOldReliableAndReplaceableContent() throws Exception {
        var mailbox=new LatestFrameMailbox();Object oldPeer=new Object(),newPeer=new Object();mailbox.begin(oldPeer);
        mailbox.offerReliable(oldPeer,packet(Wire.TEXTURE,1));mailbox.publish(oldPeer,LatestFrameMailbox.Slot.HUD,packet(Wire.HUD,2));
        mailbox.begin(newPeer);mailbox.publish(newPeer,LatestFrameMailbox.Slot.STATE,packet(Wire.STATE,3));
        var output=new ArrayList<Wire.Packet>();assertFalse(mailbox.awaitDrain(oldPeer,output));
        assertTrue(mailbox.awaitDrain(newPeer,output));assertEquals(List.of(3),output.stream().map(Wire.Packet::sequence).toList());
    }
}
