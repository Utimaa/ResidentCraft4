package dev.re4craft.client;

import dev.re4craft.protocol.Wire;
import dev.re4craft.world.BlockShapeMerger;
import java.util.*;
import net.minecraft.client.Minecraft;
import net.minecraft.core.BlockPos;
import net.minecraft.world.level.chunk.LevelChunkSection;
import net.minecraft.world.phys.AABB;
import net.minecraft.world.phys.Vec3;

/** Actual vanilla collision shapes, reusing the scene's loaded-section discovery.
 * Neutral block collision context represents native actors; entity-dependent
 * scaffolding/powder-snow behavior remains the real Minecraft entity's physics. */
final class GuestBlocks {
    private static final int MAX_SHAPES=4096,MAX_CACHE_SHAPES=32768,MAX_SECTION_SHAPES=16384;
    private static final long WORK_BUDGET=500_000L,INTERVAL=100_000_000L;
    private static final boolean PROFILE=Boolean.getBoolean("re4craft.profile");
    private record Key(int x,int y,int z) { }
    private record Shape(AABB box,double distance) { }
    private static final class Section {
        final Key key;
        LevelChunkSection source;int revision,builtRevision=-1;double distance;
        List<AABB> shapes=List.of();
        Section(Key key){this.key=key;}
    }
    private static final Map<Key,Section> sections=new HashMap<>();
    private static final PriorityQueue<Shape> building=new PriorityQueue<>(Comparator.comparingDouble(Shape::distance).reversed());
    private static Section work;
    private static int workIndex,workRevision,cacheEpoch,cacheShapes;
    private static Object level;
    private static Vec3 workFeet;
    private static long next,nextDiscovery,profileStart,profileCalls,profileTotal,profileMax,profileRebuilds,nextLog;
    private static int lastEpoch,lastCount=-1;

    private static double distance(AABB b,Vec3 p) {
        double x=Math.max(b.minX-p.x,Math.max(0,p.x-b.maxX));
        double y=Math.max(b.minY-p.y,Math.max(0,p.y-b.maxY));
        double z=Math.max(b.minZ-p.z,Math.max(0,p.z-b.maxZ));return x*x+y*y+z*z;
    }
    private static void reset(Minecraft mc,int epoch) {
        sections.clear();building.clear();work=null;cacheShapes=0;level=mc.level;cacheEpoch=epoch;next=nextDiscovery=0;
    }
    private static List<Section> nearest() {
        var result=new ArrayList<>(sections.values());result.sort(Comparator.comparingDouble(e->e.distance));return result;
    }
    static void capture(Minecraft mc,SceneCapture scene) {
        if(mc.player==null || mc.level==null || !Re4CraftClient.hostAnchored())return;
        int epoch=Re4CraftClient.hostEpoch();
        if(level!=mc.level || cacheEpoch!=epoch)reset(mc,epoch);
        if(scene.cachedEpoch()!=epoch)return;
        long start=PROFILE?System.nanoTime():0,now=System.nanoTime();
        try {
            // Scene revisions include neighbor edits, empty-section removal and
            // resource reload. No duplicate chunk discovery or block-state encoding.
            if(now>=nextDiscovery){nextDiscovery=now+INTERVAL;var retained=new HashSet<Key>();
            for(var source:scene.cachedSections()) {
                Key key=new Key(source.x(),source.y(),source.z());retained.add(key);
                Section entry=sections.computeIfAbsent(key,Section::new);
                entry.distance=source.distance();
                if(entry.source!=source.source() || entry.revision!=source.revision()) {
                    entry.source=source.source();entry.revision=source.revision();
                    if(work==entry){work=null;building.clear();}
                }
            }
            for(var it=sections.values().iterator();it.hasNext();) {
                Section entry=it.next();
                if(!retained.contains(entry.key)){cacheShapes-=entry.shapes.size();it.remove();if(work==entry){work=null;building.clear();}}
            }
            }
            rebuild(mc,scene.minimumCachedY(mc));
            if(now>=next){next=now+INTERVAL;publish(mc,epoch);}
        } finally {
            if(PROFILE){long duration=System.nanoTime()-start;profileCalls++;profileTotal+=duration;profileMax=Math.max(profileMax,duration);profile();}
        }
    }
    private static void rebuild(Minecraft mc,int minY) {
        long deadline=System.nanoTime()+WORK_BUDGET;int completed=0;
        do {
        if(work==null)for(Section entry:nearest())if(entry.builtRevision!=entry.revision) {
            double farthest=-1;for(Section cached:sections.values())if(!cached.shapes.isEmpty())farthest=Math.max(farthest,cached.distance);
            if(entry.shapes.isEmpty() && cacheShapes>=MAX_CACHE_SHAPES && entry.distance>=farthest)continue;
            work=entry;workIndex=0;workRevision=entry.revision;workFeet=mc.player.position();building.clear();break;
        }
        if(work==null)return;
        int checked=0;BlockPos.MutableBlockPos pos=new BlockPos.MutableBlockPos();
        while(workIndex<4096) {
            int index=workIndex++,x=work.key.x*16+(index&15),z=work.key.z*16+((index>>4)&15),y=work.key.y*16+(index>>8);checked++;
            if(y>=minY) {
                var state=work.source.getBlockState(x&15,y&15,z&15);
                if(!state.isAir()) {
                    pos.set(x,y,z);
                    // Neutral context handles actual door state, stair halves,
                    // fences, slabs and shape neighbors without guessing boxes.
                    for(AABB local:state.getCollisionShape(mc.level,pos).toAabbs()) {
                        AABB box=local.move(x,y,z);Shape shape=new Shape(box,distance(box,workFeet));
                        if(building.size()<MAX_SECTION_SHAPES)building.add(shape);
                        else if(shape.distance<building.peek().distance){building.poll();building.add(shape);}
                    }
                }
            }
            if((checked&15)==0 && workIndex<4096 && System.nanoTime()>=deadline)return;
        }
        // Keep completed shapes until the entire changed section has rebuilt.
        var exact=building.stream().map(Shape::box).map(b->new BlockShapeMerger.Box(b.minX,b.minY,b.minZ,b.maxX,b.maxY,b.maxZ)).toList();
        var merged=BlockShapeMerger.merge(exact);
        cacheShapes-=work.shapes.size();work.shapes=merged.stream().map(b->new AABB(b.minX(),b.minY(),b.minZ(),b.maxX(),b.maxY(),b.maxZ())).toList();
        work.builtRevision=workRevision;cacheShapes+=work.shapes.size();profileRebuilds++;
        while(cacheShapes>MAX_CACHE_SHAPES) {
            Section farthest=null;for(Section entry:sections.values())if(entry!=work && !entry.shapes.isEmpty() && (farthest==null || entry.distance>farthest.distance))farthest=entry;
            if(farthest==null)break;cacheShapes-=farthest.shapes.size();farthest.shapes=List.of();farthest.builtRevision=-1;
        }
        work=null;building.clear();
        }while(++completed<8 && System.nanoTime()<deadline);
    }
    private static void publish(Minecraft mc,int epoch) {
        ArrayList<AABB> boxes=new ArrayList<>();Vec3 feet=mc.player.position();
        double radius=SceneCapture.RADIUS;
        for(Section section:sections.values())for(AABB box:section.shapes)if(distance(box,feet)<=radius*radius)boxes.add(box);
        boxes.sort(Comparator.comparingDouble(box->distance(box,feet)));boolean bounded=boxes.size()>MAX_SHAPES;
        if(bounded)boxes.subList(MAX_SHAPES,boxes.size()).clear();
        var b=Wire.buffer(16+24*boxes.size()).putInt(epoch).putInt(boxes.size()).putLong(mc.player.tickCount);
        AABB bounds=null;
        for(AABB box:boxes) {
            Vec3 min=Re4CraftClient.minecraftToHost(new Vec3(box.minX,box.minY,box.minZ));
            Vec3 max=Re4CraftClient.minecraftToHost(new Vec3(box.maxX,box.maxY,box.maxZ));
            b.putFloat((float)min.x).putFloat((float)min.y).putFloat((float)min.z).putFloat((float)max.x).putFloat((float)max.y).putFloat((float)max.z);
            AABB hostBox=new AABB(min,max);bounds=bounds==null?hostBox:bounds.minmax(hostBox);
        }
        if(epoch!=Re4CraftClient.hostEpoch())return;
        if((epoch!=lastEpoch || boxes.size()!=lastCount) && System.nanoTime()>=nextLog) {
            Re4CraftClient.LOG.info("Guest block colliders: epoch {}, {} exact merged shapes, host bounds {}, cached sections {}, range={} blocks{}",epoch,boxes.size(),bounds,sections.size(),radius,bounded?" (nearest-shape budget reached)":"");
            lastEpoch=epoch;lastCount=boxes.size();nextLog=System.nanoTime()+1_000_000_000L;
        }
        Re4CraftClient.SERVER.publishBlockColliders(b.array());
    }
    private static void profile() {
        long now=System.nanoTime();if(profileStart==0)profileStart=now;if(now-profileStart<5_000_000_000L)return;
        Re4CraftClient.LOG.info(String.format(Locale.ROOT,"Collider profile: capture avg/max=%.3f/%.3fms calls=%d sectionRebuilds=%d cachedSections=%d cachedShapes=%d",profileTotal/1e6/profileCalls,profileMax/1e6,profileCalls,profileRebuilds,sections.size(),cacheShapes));
        profileStart=now;profileCalls=profileTotal=profileMax=profileRebuilds=0;
    }
}
