"""Compile the actual Android-independent framing helper and test geometry semantics.

With --owner-airport, decode the owner's airport0 collision in memory and pass only
an ephemeral float export to Java. Never commits or redistributes collision fixtures.
"""
import argparse
import importlib.util
import math
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'reference/driftdroid/android/app/src/main/java/dev/chasem/smslauncher/ThorMapFraming.java'
JDK = Path('C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot/bin')

HARNESS = r'''
package dev.chasem.smslauncher;
import java.io.*;
import java.util.*;
public final class MapFramingTest {
 static int checks;
 static void check(boolean b, String m) { checks++; if (!b) throw new AssertionError(m); }
 static void add(List<Float> f, float x, float z, float size, float y, float ny, float water) {
  for (float v: new float[]{x,z,x+size,z,x,z+size,y,0,ny,0,water}) f.add(v);
 }
 static float[] floats(List<Float> f) { float[] a=new float[f.size()]; for(int i=0;i<a.length;i++)a[i]=f.get(i);return a; }
 static ThorMapFraming.Fit fit(List<Float> f) { return ThorMapFraming.fit(floats(f),f.size()/11,720,480); }
 static void deterministic(List<Float> f) {
  float[] a=floats(f), copy=a.clone();
  ThorMapFraming.Fit p=ThorMapFraming.fit(a,a.length/11,720,480),q=ThorMapFraming.fit(a,a.length/11,720,480);
  check(Arrays.equals(a,copy),"input remains unchanged"); check(p.k==q.k && p.ox==q.ox && p.oz==q.oz,"stable framing");
 }
 public static void main(String[] args) throws Exception {
  List<Float> a=new ArrayList<>();
  for(int i=0;i<200;i++)add(a,(i%20)*100,(i/20)*100,100,100,1,0);
  ThorMapFraming.Fit base=fit(a); check(base!=null,"ordinary island");
  // Two huge downward ceilings, underwater floors, plus local elevated pool.
  add(a,-50000,-50000,100000,2000,-1,0); int ceiling=a.size()/11-1;
  add(a,-50000,-50000,150000,0,1,1);
  add(a,-30000,-30000,60000,-1000,1,0); int bottom=a.size()/11-1;
  add(a,0,0,100,500,1,1);
  ThorMapFraming.Fit p=fit(a);
  check(!p.draw[ceiling],"ceilings excluded"); check(!p.draw[bottom],"underwater floor excluded");
  check(p.minX==base.minX && p.maxX==base.maxX,"pool is locally applied");
  check(p.minZ==base.minZ && p.maxZ==base.maxZ,"seabed does not expand frame");
  add(a,-90000,90000,100,30,1,0); int remote=a.size()/11-1; p=fit(a);
  check(!p.frame[remote],"sparse remote scenery excluded");
  check(p.k==base.k,"remote scenery cannot shrink island"); deterministic(a);
  // Separate, substantial landmasses remain in view, rather than selecting one component.
  List<Float> many=new ArrayList<>();
  for(int i=0;i<100;i++){add(many,i*10,0,20,0,1,0);add(many,10000+i*10,3000,20,0,1,0);}
  p=fit(many);check(p.maxX>10000,"substantial separate island kept");
  for(int i=0;i<many.size()/11;i++)check(p.frame[i],"all substantive islands fit");
  // Each retained vertex and marker use the identical world transform and fit inside padding.
  float[] fs=floats(many);
  for(int i=0;i<fs.length;i+=11)for(int v=0;v<3;v++){
   float x=fs[i+v*2]*p.k+p.ox,z=fs[i+v*2+1]*p.k+p.oz;
   check(x>=35.9 && x<=684.1 && z>=28.7 && z<=451.3,"vertices within frame");
  }
  List<Float> tiny=new ArrayList<>();for(int i=0;i<3;i++)add(tiny,i*2,0,2,0,1,0);
  p=fit(tiny);check(p.k<=.6481f,"bounded tiny-map zoom");
  check(ThorMapFraming.fit(null,3,720,480)==null,"null rejected");
  check(ThorMapFraming.fit(new float[11],2,720,480)==null,"invalid count rejected");
  List<Float> empty=new ArrayList<>();for(int i=0;i<3;i++)add(empty,i*100,0,100,0,-1,0);
  check(fit(empty)==null,"ceiling-only fallback");
  if(args.length>0){
   float[] f;
   try(DataInputStream in=new DataInputStream(new FileInputStream(args[0]))){f=new float[in.readInt()*11];for(int i=0;i<f.length;i++)f[i]=in.readFloat();}
   p=ThorMapFraming.fit(f,f.length/11,720,480);check(p!=null,"owner collision fit");
   check(p.maxX-p.minX<22000 && p.maxZ-p.minZ<18000,"actual Airstrip excludes distant islet and sea floor");
   check(p.minX < -9500 && p.maxX > 8500 && p.minZ < -9000 && p.maxZ > 4500,"full main Airstrip perimeter retained");
   int dry=0;for(boolean b:p.frame)if(b)dry++;
   System.out.printf(Locale.ROOT,"owner Airstrip: %d input / %d framing, X %.2f..%.2f Z %.2f..%.2f k %.6f%n",f.length/11,dry,p.minX,p.maxX,p.minZ,p.maxZ,p.k);
  }
  System.out.println(checks+" actual-source checks PASS");
 }
}
'''


def airport_export():
    spec = importlib.util.spec_from_file_location('thor_hud', ROOT / 'experiments/sunshine-thor/Extract-ThorHud.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    archive = ROOT / 'build/sunshine-thor/extracted/files/data/scene/airport0.szs'
    b = mod.rarc(mod.yaz0(archive.read_bytes()))['map/map.col']
    nv, vo, ng, go = struct.unpack_from('>4I', b)
    vertices = [struct.unpack_from('>3f', b, vo + i * 12) for i in range(nv)]
    rows = []
    water_types = {0x100, 0x101, 0x102, 0x103, 0x104, 0x105, 0x4104}
    through = {0x400, 0x8400, 0x100, 0x101, 0x102, 0x103, 0x800, 0x801, 0x201, 0x203}
    for g in range(ng):
        typ, count, flags, indices, _, _, extra = struct.unpack_from('>HHIIIII', b, go + g * 24)
        water = typ in water_types
        if not water and typ in through:
            continue
        for i in range(count):
            p = [vertices[q] for q in struct.unpack_from('>3H', b, indices + i * 6)]
            a = [p[1][j] - p[0][j] for j in range(3)]
            c = [p[2][j] - p[0][j] for j in range(3)]
            normal = [a[1]*c[2]-a[2]*c[1], a[2]*c[0]-a[0]*c[2], a[0]*c[1]-a[1]*c[0]]
            length = math.sqrt(sum(x*x for x in normal))
            if length == 0 or abs(normal[1]/length) < .35:
                continue
            rows.append([p[0][0],p[0][2],p[1][0],p[1][2],p[2][0],p[2][2],sum(q[1] for q in p)/3,
                         *(x/length for x in normal),float(water)])
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--owner-airport', action='store_true')
    args = ap.parse_args()
    with tempfile.TemporaryDirectory(prefix='thor-framing-') as folder:
        out = Path(folder)
        harness = out / 'MapFramingTest.java'
        harness.write_text(HARNESS)
        subprocess.run([str(JDK / 'javac.exe'), '-d', str(out), str(SOURCE), str(harness)], check=True)
        command = [str(JDK / 'java.exe'), '-cp', str(out), 'dev.chasem.smslauncher.MapFramingTest']
        if args.owner_airport:
            rows = airport_export()
            fixture = out / 'owner-private-floors.bin'
            with fixture.open('wb') as f:
                f.write(struct.pack('>I', len(rows)))
                for row in rows:
                    f.write(struct.pack('>11f', *row))
            command.append(str(fixture))
        subprocess.run(command, check=True)


if __name__ == '__main__':
    main()
