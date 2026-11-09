"""Run production Java against own-disc courses. All derived inputs/outputs stay private.

Usage: py -3.12 -I test_overview_baker.py --iso <owner ISO> --out <private folder>
       [--art <existing art>] [--reference <existing Python overview folder>]
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[1]
JAVA = Path('C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot/bin')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--iso', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--art', type=Path, default=Path('M:/WiiDS-mkdd-2026-11-01/lower-art'))
    parser.add_argument('--reference', type=Path, default=Path('M:/WiiDS-mkdd-2026-11-01/overview'))
    parser.add_argument('--course', nargs='*', type=lambda v: int(v, 0))
    parser.add_argument('--full-art', action='store_true', help='Also exercise the complete production art transaction')
    a = parser.parse_args()
    out = a.out.resolve()
    if out == REPO or REPO in out.parents:
        parser.error('Own-disc fixtures and renders must be outside the source tree')
    out.mkdir(parents=True, exist_ok=True)
    sources = {
        'Bitmap.java': '''package android.graphics;
import java.awt.image.BufferedImage;
import java.io.OutputStream;
import javax.imageio.ImageIO;
public final class Bitmap {
 public static final class Config {public static final Config ARGB_8888=new Config();}
 public static final class CompressFormat {public static final CompressFormat PNG=new CompressFormat();}
 final int[] pixels; final int w,h;
 Bitmap(int[] p,int w,int h){pixels=p.clone();this.w=w;this.h=h;}
 public static Bitmap createBitmap(int[] p,int w,int h,Config c){return new Bitmap(p,w,h);}
 public int getWidth(){return w;} public int getHeight(){return h;} public void recycle(){}
 public void getPixels(int[] out,int off,int stride,int x,int y,int width,int height){
  for(int yy=0;yy<height;yy++)System.arraycopy(pixels,(y+yy)*w+x,out,off+yy*stride,width);}
 public boolean compress(CompressFormat f,int q,OutputStream out){try{
  BufferedImage b=new BufferedImage(w,h,BufferedImage.TYPE_INT_ARGB);b.setRGB(0,0,w,h,pixels,0,w);
  return ImageIO.write(b,"png",out);}catch(Exception e){throw new RuntimeException(e);}}
}''',
        'DuoArt.java': 'package dev.thor.mkdd; final class DuoArt {static final String MANIFEST="duo_hud.json";}',
        'BakeTest.java': '''package dev.thor.mkdd;
import java.nio.file.*;import java.io.*;import org.json.*;import android.graphics.Bitmap;
public final class BakeTest {
 public static void main(String[] args)throws Exception {
  if(args.length>1){File root=new File(args[0]);boolean plus=Boolean.parseBoolean(args[2]);
   String error=MkddArt.build(new File(args[1]),new File(root,"full-art"),plus,(done,total,course)->{if(done==total)System.out.println("Built "+done+"/"+total);});
   if(error!=null)throw new AssertionError(error);if(!MkddArt.ready(new File(root,"full-art")))throw new AssertionError("complete art rejected");
   File sentinel=new File(root,"full-art/owner-sentinel");Files.write(sentinel.toPath(),new byte[]{1,2,3});
   error=MkddArt.build(new File(args[1]),new File(root,"full-art"),plus,(d,t,c)->{throw new IOException("test cancellation");});
   if(error==null||!sentinel.isFile()||!MkddArt.ready(new File(root,"full-art")))throw new AssertionError("failed rebuild lost original");
   if(new File(root,"full-art.building").exists())throw new AssertionError("failed build leaked staging");
   File missing=new File(root,"full-art/overview/duo_overview_36.png");if(!missing.delete())throw new AssertionError("fixture missing");
   if(MkddArt.ready(new File(root,"full-art")))throw new AssertionError("missing overview accepted");
   System.out.println("Complete transaction, failed-rebuild preservation and missing-output checks passed");return;}
  File root=new File(args[0]);JSONObject all=new JSONObject(new String(Files.readAllBytes(new File(root,"fixtures.json").toPath()),"UTF-8"));
  int checks=0;
  for(String key:all.keySet()) {JSONObject e=all.getJSONObject(key);JSONArray a=e.getJSONArray("axes"),b=e.getJSONArray("bounds");
   String[] axes={a.getString(0),a.getString(1)};double[] bounds={b.getDouble(0),b.getDouble(1),b.getDouble(2),b.getDouble(3)};
   long start=System.nanoTime();Bitmap bitmap=MkddOverview.bake(Files.readAllBytes(new File(root,key+".bmd").toPath()),Files.readAllBytes(new File(root,key+".bol").toPath()),axes,bounds);
   try(FileOutputStream f=new FileOutputStream(new File(root,key+".png"))){bitmap.compress(Bitmap.CompressFormat.PNG,100,f);}
   System.out.println(key+": "+bitmap.getWidth()+"x"+bitmap.getHeight()+" in "+(System.nanoTime()-start)/1e9+"s");bitmap.recycle();checks++;
  }
  if(MkddOverview.wrap(-1,4,1)!=3||MkddOverview.wrap(-1,4,2)!=0||MkddOverview.wrap(4,4,2)!=3)throw new AssertionError("texture wrapping");checks+=3;
  try{MkddOverview.sections(new byte[40]);throw new AssertionError("truncated model accepted");}catch(IOException expected){checks++;}
  System.out.println(checks+" course/render/parser checks passed");
 }
}'''}
    for name, source in sources.items():
        (out / name).write_text(source, encoding='utf-8')
    jar = next((Path.home() / '.gradle/caches/modules-2/files-2.1/org.json/json/20240303').rglob('*.jar'))
    classes = out / 'classes'
    classes.mkdir(exist_ok=True)
    production = ROOT / 'android/dev/thor/mkdd'
    subprocess.run([str(JAVA / 'javac.exe'), '-source', '8', '-target', '8', '-encoding', 'UTF-8',
                    '-cp', str(jar), '-d', str(classes), *[str(out / n) for n in sources],
                    str(production / 'MkddArt.java'), str(production / 'MkddOverview.java')], check=True)
    spec = importlib.util.spec_from_file_location('stage_art', ROOT / 'design/stage_art.py')
    sa = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(sa)
    manifest = json.loads((a.art / 'duo_hud.json').read_text(encoding='utf-8'))
    fixtures = {}
    with a.iso.open('rb') as disc:
        files = sa.disc_files(disc)
        for key, entry in manifest['maps'].items():
            key = int(key)
            if a.course and key not in a.course:
                continue
            page, cid = divmod(key, 256)
            arc = sa.COURSES[cid]
            path = f'/Cours{page}/{arc}.arc' if manifest.get('course_names') else f'/Course/{arc}.arc'
            offset, size = files[path]
            disc.seek(offset)
            course = sa.rarc(sa.yaz0(disc.read(size)))
            for extension in ('bmd', 'bol'):
                (out / f'{key}.{extension}').write_bytes(course[f'{arc.lower()}_course.{extension}'])
            fixtures[str(key)] = entry
    (out / 'fixtures.json').write_text(json.dumps(fixtures), encoding='utf-8')
    started = time.monotonic()
    subprocess.run([str(JAVA / 'java.exe'), '-Xmx1400m', '-cp', f'{classes};{jar}', 'dev.thor.mkdd.BakeTest', str(out)], check=True)
    from PIL import Image
    import numpy as np
    results = {}
    reference = json.loads((a.reference / 'overview.json').read_text())['overviews']
    for key in fixtures:
        image = np.asarray(Image.open(out / f'{key}.png').convert('RGBA')).astype(np.int16)
        expected = np.asarray(Image.open(a.reference / reference[key]['png']).convert('RGBA')).astype(np.int16)
        assert image.shape == expected.shape, (key, image.shape, expected.shape)
        delta = np.abs(image - expected)
        results[key] = {'shape': list(image.shape), 'mean_abs_channel_error': float(delta.mean()),
                        'p99_channel_error': float(np.percentile(delta, 99)),
                        'alpha_mean_error': float(delta[:, :, 3].mean()),
                        'sha256': hashlib.sha256((out / f'{key}.png').read_bytes()).hexdigest()}
        assert delta.mean() < .02 and np.percentile(delta, 99) <= 1, (key, results[key])
    receipt = {'host_only': True, 'courses': results, 'seconds': time.monotonic() - started,
               'source_sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                                 for p in (production / 'MkddArt.java', production / 'MkddOverview.java')}}
    (out / 'comparison.json').write_text(json.dumps(receipt, indent=2))
    print(json.dumps(receipt, indent=2))
    if a.full_art:
        subprocess.run([str(JAVA / 'java.exe'), '-Xmx256m', '-cp', f'{classes};{jar}', 'dev.thor.mkdd.BakeTest', str(out), str(a.iso), str(bool(manifest.get('course_names'))).lower()], check=True)


if __name__ == '__main__':
    main()
