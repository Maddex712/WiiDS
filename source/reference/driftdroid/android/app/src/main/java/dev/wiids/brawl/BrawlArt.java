package dev.wiids.brawl;

import android.content.Context;
import android.graphics.*;
import org.json.*;
import java.io.*;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.util.*;

/** Extracts the game's own GX textures and RFNT fonts. No Nintendo art travels in the APK. */
public final class BrawlArt {
    private static final int LIMIT=64*1024*1024;
    private BrawlArt(){}
    public static boolean ready(Context c) {
        File f=new File(BrawlStorage.data(c),"ThorHud/brawl");
        for(String p:new String[]{"logo.png","bust_01_0.png","stock_01_0.png","dmg_0.png","mark_01.png","fonts/font_hira.json","fonts/font_melee.json"})
            if(!new File(f,p).isFile())return false;
        return true;
    }
    public static synchronized String build(Context c,File disc,File hud) {
        File stage=new File(hud,"brawl.building"),dest=new File(hud,"brawl");
        try {
            if(Files.isSymbolicLink(hud.toPath())||Files.isSymbolicLink(stage.toPath())||Files.isSymbolicLink(dest.toPath()))throw new IOException("Linked Brawl art storage is not supported.");
            erase(stage);stage.mkdirs();
            File portraits=new File(disc,"files/menu/common/char_bust_tex");File[] files=portraits.listFiles((d,n)->n.startsWith("MenSelchrFaceB")&&n.endsWith(".brres"));
            if(files==null||files.length==0)throw new IOException("The imported disc is missing Brawl portraits.");
            for(File file:files)portraits(read(file),stage,false,-1);
            portraits(read(new File(disc,"files/menu/common/StockFaceTex_en.brres")),stage,false,-1);
            Map<String,Texture> info=textures(read(new File(disc,"files/info/Info_en.brres")));
            for(int n=0;n<10;n++)write(stage,"dmg_"+n+".png",required(info,"InfDamageNm."+n).rotate());
            write(stage,"dmg_pct.png",required(info,"InfDamageNmPer"));
            for(int n=1;n<=20;n++)write(stage,String.format(Locale.US,"mark_%02d.png",n),required(info,String.format(Locale.US,"MenSelchrMark.%02d",n)));
            logo(textures(read(new File(disc,"files/menu2/sc_title_en.pac"))),new File(stage,"logo.png"));
            File fonts=new File(stage,"fonts");fonts.mkdirs();
            for(String name:new String[]{"font_hira","font_melee"})font(read(new File(disc,"files/system/font/"+name+".brfnt")),fonts,name);
            // Publish only after the whole set exists. A working old set is kept on any decode failure.
            File previous=new File(hud,"brawl.previous");if(Files.isSymbolicLink(previous.toPath()))throw new IOException("Linked Brawl art backup.");
            if(previous.exists()) { if(!dest.exists())Files.move(previous.toPath(),dest.toPath(),StandardCopyOption.ATOMIC_MOVE);else erase(previous); }
            if(dest.exists())Files.move(dest.toPath(),previous.toPath(),StandardCopyOption.ATOMIC_MOVE);
            try {Files.move(stage.toPath(),dest.toPath(),StandardCopyOption.ATOMIC_MOVE);}catch(IOException failure){if(!dest.exists()&&previous.exists())Files.move(previous.toPath(),dest.toPath(),StandardCopyOption.ATOMIC_MOVE);throw failure;}
            erase(previous);
            return null;
        }catch(Exception e){android.util.Log.e("WiiDS-BrawlArt","Disc art extraction failed",e);return "Brawl art: "+e.getMessage();}
    }
    private static void erase(File f)throws IOException {
        if(Files.isSymbolicLink(f.toPath()))throw new IOException("Linked art file.");
        if(!f.exists())return;
        if(f.isDirectory()){File[] children=f.listFiles();if(children==null)throw new IOException("Unreadable art staging.");for(File x:children)erase(x);}
        if(!f.delete())throw new IOException("Could not clear art staging.");
    }
    private static byte[] read(File f)throws IOException {
        if(!f.isFile()||f.length()>LIMIT)throw new IOException("Missing or oversized "+f.getName());
        return Files.readAllBytes(f.toPath());
    }
    private static int u8(byte[] d,int p)throws IOException {if(p<0||p>=d.length)throw new IOException("Truncated texture/font.");return d[p]&255;}
    private static int s8(byte[] d,int p)throws IOException {return (byte)u8(d,p);}
    private static int u16(byte[]d,int p)throws IOException{return (u8(d,p)<<8)|u8(d,p+1);}
    private static int i32(byte[]d,int p)throws IOException{return (u16(d,p)<<16)|u16(d,p+2);}
    private static String str(byte[]d,int p)throws IOException {int end=p;while(end<d.length&&end-p<1024&&u8(d,end)!=0)end++;if(p<0||end==d.length||end-p>=1024)throw new IOException("Invalid texture name.");return new String(d,p,end-p,StandardCharsets.US_ASCII);}
    private static byte[] unpack(byte[]d)throws IOException {
        if(d.length<8||u8(d,0)!=0x10)return d;
        int size=u8(d,1)|(u8(d,2)<<8)|(u8(d,3)<<16);if(size<=0||size>LIMIT)throw new IOException("Invalid archive size.");
        byte[] out=new byte[size];int p=4,o=0;
        while(o<size){int flags=u8(d,p++);for(int bit=0;bit<8&&o<size;bit++) {
            if((flags&(0x80>>bit))==0)out[o++]=(byte)u8(d,p++);
            else {int a=u8(d,p++),b=u8(d,p++),n=(a>>4)+3,back=((a&15)<<8|b)+1;if(back>o||o+n>size)throw new IOException("Invalid compressed texture.");for(int k=0;k<n;k++){out[o]=out[o-back];o++;}}
        }}return out;
    }
    private static int rgb565(int x){return 0xff000000|(((x>>11)&31)*255/31<<16)|(((x>>5)&63)*255/63<<8)|((x&31)*255/31);}
    private static int rgb5a3(int x){return (x&0x8000)!=0?0xff000000|(((x>>10)&31)*255/31<<16)|(((x>>5)&31)*255/31<<8)|((x&31)*255/31):(((x>>12)&7)*255/7<<24)|(((x>>8)&15)*17<<16)|(((x>>4)&15)*17<<8)|(x&15)*17;}
    private static final class Texture {
        final int w,h;final int[] pixels;
        Texture(int w,int h,int[]p){this.w=w;this.h=h;pixels=p;}
        Texture rotate(){int[] p=new int[pixels.length];for(int y=0;y<h;y++)for(int x=0;x<w;x++)p[(w-1-x)*h+y]=pixels[y*w+x];return new Texture(h,w,p);}
        Bitmap bitmap(){Bitmap b=Bitmap.createBitmap(w,h,Bitmap.Config.ARGB_8888);b.setPremultiplied(false);b.setPixels(pixels,0,w,0,0,w,h);return b;}
        Bitmap drawable(){return Bitmap.createBitmap(pixels,w,h,Bitmap.Config.ARGB_8888);}
    }
    private static Texture decode(byte[]d,int start,int format,int w,int h,int[]palette)throws IOException {
        if(w<=0||h<=0||w>2048||h>2048)throw new IOException("Invalid texture dimensions.");
        int bw,bh,bpp;
        switch(format){case 0:case 8:case 14:bw=8;bh=8;bpp=4;break;case 1:case 2:case 9:bw=8;bh=4;bpp=8;break;case 3:case 4:case 5:bw=4;bh=4;bpp=16;break;case 6:bw=4;bh=4;bpp=32;break;default:throw new IOException("Unsupported GX format "+format);}
        long needed=(long)((w+bw-1)/bw)*((h+bh-1)/bh)*bw*bh*bpp/8;if(start<0||needed>d.length-start)throw new IOException("Truncated GX pixels.");
        int[] out=new int[w*h];int p=start;
        for(int by=0;by<h;by+=bh)for(int bx=0;bx<w;bx+=bw) {
            if(format==14) {
                for(int sub=0;sub<4;sub++) {
                    int c0=u16(d,p),c1=u16(d,p+2),a=rgb565(c0),b=rgb565(c1),bits=i32(d,p+4);p+=8;
                    int[] pal={a,b,mix(a,b,c0>c1?2:1,1,c0>c1?3:2),c0>c1?mix(a,b,1,2,3):0};
                    for(int n=0;n<16;n++)put(out,w,h,bx+(sub&1)*4+(n&3),by+(sub>>1)*4+(n>>2),pal[(bits>>>(30-2*n))&3]);
                }
            }else if(format==6) {
                for(int n=0;n<16;n++)put(out,w,h,bx+(n&3),by+(n>>2),(u8(d,p+2*n)<<24)|(u8(d,p+2*n+1)<<16)|(u8(d,p+32+2*n)<<8)|u8(d,p+33+2*n));p+=64;
            }else {
                for(int y=0;y<bh;y++)for(int x=0;x<bw;x++) {
                    int n=y*bw+x,color,v;
                    switch(format) {
                        case 8:v=u8(d,p+n/2);v=(n&1)==0?v>>4:v&15;color=pal(palette,v);break;
                        case 9:color=pal(palette,u8(d,p+n));break;
                        case 0:v=u8(d,p+n/2);v=((n&1)==0?v>>4:v&15)*17;color=0xff000000|v*0x10101;break;
                        case 1:v=u8(d,p+n);color=0xff000000|v*0x10101;break;
                        case 2:v=u8(d,p+n);color=((v>>4)*17<<24)|(v&15)*17*0x10101;break;
                        case 3:color=(u8(d,p+2*n)<<24)|u8(d,p+2*n+1)*0x10101;break;
                        default:v=u16(d,p+2*n);color=format==4?rgb565(v):rgb5a3(v);
                    }put(out,w,h,bx+x,by+y,color);
                }p+=bw*bh*bpp/8;
            }
        }return new Texture(w,h,out);
    }
    private static int mix(int a,int b,int na,int nb,int den){return 0xff000000|((((a>>16)&255)*na+((b>>16)&255)*nb)/den<<16)|((((a>>8)&255)*na+((b>>8)&255)*nb)/den<<8)|(((a&255)*na+(b&255)*nb)/den);}
    private static int pal(int[]p,int i)throws IOException {if(p==null||i>=p.length)throw new IOException("Missing texture palette.");return p[i];}
    private static void put(int[]p,int w,int h,int x,int y,int c){if(x<w&&y<h)p[y*w+x]=c;}
    private static Map<String,Texture> textures(byte[]raw)throws IOException {
        byte[]d=unpack(raw);Map<String,int[]>palettes=new HashMap<>();Map<String,Texture> textures=new LinkedHashMap<>();
        for(int p=0;p+32<=d.length;p++)if(d[p]=='P'&&d[p+1]=='L'&&d[p+2]=='T'&&d[p+3]=='0') {
            try {int at=p+i32(d,p+16),format=i32(d,p+24),count=u16(d,p+28);String name=str(d,p+i32(d,p+20));if(count>16384||format>2)continue;int[] colors=new int[count];for(int i=0;i<count;i++){int v=u16(d,at+2*i);colors[i]=format==0?(v>>8<<24)|(v&255)*0x10101:format==1?rgb565(v):rgb5a3(v);}palettes.put(name,colors);}catch(IOException ignored){}
        }
        for(int p=0;p+40<=d.length;p++)if(d[p]=='T'&&d[p+1]=='E'&&d[p+2]=='X'&&d[p+3]=='0') {
            try {int head=p+(i32(d,p+8)>=3?0x1c:0x18);String name=str(d,p+i32(d,p+20));int w=u16(d,head+4),h=u16(d,head+6),format=i32(d,head+8);if(w>1024||h>1024)continue;Texture t=decode(d,p+i32(d,p+16),format,w,h,palettes.get(name));textures.put(name,t);}catch(IOException ignored){}
        }return textures;
    }
    private static Texture required(Map<String,Texture>m,String n)throws IOException {Texture t=m.get(n);if(t==null)throw new IOException("Missing disc texture "+n);return t;}
    private static void write(File dir,String name,Texture t)throws IOException {Bitmap b=t.bitmap();try(FileOutputStream out=new FileOutputStream(new File(dir,name))){if(!b.compress(Bitmap.CompressFormat.PNG,100,out))throw new IOException("PNG encoding failed.");out.getFD().sync();}finally{b.recycle();}}
    private static void portraits(byte[]blob,File out,boolean mod,int fighter)throws IOException {
        for(Map.Entry<String,Texture> e:textures(blob).entrySet()) {
            String n=e.getKey(),digits=n.substring(n.lastIndexOf('.')+1),prefix=n.startsWith("MenSelchrFaceB.")?"bust":n.startsWith("InfStc.")?"stock":null;
            if(prefix==null||!digits.matches("[0-9]+"))continue;int id,costume,number=Integer.parseInt(digits);
            if(!mod){if(digits.length()!=3||number%10==0)continue;id=number/10;costume=number%10-1;}
            else if(prefix.equals("bust")){id=fighter;costume=number-fighter*10-1;}
            else {if(digits.length()!=4||number%50==0)continue;id=(number-1)/50;costume=(number-1)%50;}
            if(id<0||id>99||costume<0||costume>49)continue;
            write(out,String.format(Locale.US,"%s_%02d_%d.png",prefix,id,costume),e.getValue());
        }
    }
    private static void logo(Map<String,Texture>m,File out)throws IOException {
        Bitmap plate=required(m,"enTitleFrontfont_dodai").drawable(),white=required(m,"enTitleFrontfont_White").drawable(),word=required(m,"enTitleFrontfont_brawl").drawable();
        int width=(int)(plate.getWidth()*.62),height=Math.round(word.getHeight()*(float)width/word.getWidth()),pad=60;
        Bitmap big=Bitmap.createScaledBitmap(word,width,height,true),canvas=Bitmap.createBitmap(plate.getWidth()+120,plate.getHeight()+height+120,Bitmap.Config.ARGB_8888);Canvas c=new Canvas(canvas);Paint paint=new Paint(3);
        paint.setColor(Color.argb(200,255,236,200));paint.setMaskFilter(new BlurMaskFilter(28,BlurMaskFilter.Blur.NORMAL));c.drawOval(pad+plate.getWidth()*.14f,pad+plate.getHeight()-20,pad+plate.getWidth()*.86f,pad+plate.getHeight()+height-10,paint);paint.setMaskFilter(null);
        c.drawBitmap(plate,pad,pad,paint);c.drawBitmap(white,pad+(plate.getWidth()-white.getWidth())/2,pad+(plate.getHeight()-white.getHeight())/2,paint);c.drawBitmap(big,pad+(plate.getWidth()-width)/2,pad+plate.getHeight()-22,paint);
        int finalWidth=Math.round(900f*canvas.getWidth()/plate.getWidth());Bitmap scaled=Bitmap.createScaledBitmap(canvas,finalWidth,Math.round(canvas.getHeight()*(float)finalWidth/canvas.getWidth()),true);
        try(FileOutputStream stream=new FileOutputStream(out)){if(!scaled.compress(Bitmap.CompressFormat.PNG,100,stream))throw new IOException("Logo encoding failed.");}
        for(Bitmap b:new Bitmap[]{plate,white,word,big,canvas,scaled})b.recycle();
    }
    private static void font(byte[]d,File out,String name)throws Exception {
        if(i32(d,0)!=0x52464e54||u16(d,4)!=0xfeff)throw new IOException("Unsupported RFNT font.");
        int p=u16(d,12),blocks=u16(d,14),finf=-1;if(blocks>256)throw new IOException("Invalid font blocks.");
        for(int n=0;n<blocks;n++){int size=i32(d,p+4);if(i32(d,p)==0x46494e46)finf=p+8;if(size<8||size>d.length-p)throw new IOException("Invalid font block.");p+=size;}
        if(finf<0)throw new IOException("Font info missing.");
        int line=s8(d,finf+1),dl=s8(d,finf+4),dw=u8(d,finf+5),dc=s8(d,finf+6),encoding=u8(d,finf+7),glyph=i32(d,finf+8),widths=i32(d,finf+12),maps=i32(d,finf+16);
        int cw=u8(d,glyph),ch=u8(d,glyph+1),baseline=s8(d,glyph+2),sheetSize=i32(d,glyph+4),sheets=u16(d,glyph+8),format=u16(d,glyph+10)&0x7fff,rows=u16(d,glyph+12),lines=u16(d,glyph+14),sw=u16(d,glyph+16),sh=u16(d,glyph+18),image=i32(d,glyph+20);
        if(sheets>128||rows==0||lines==0||sheetSize<0)throw new IOException("Invalid font sheets.");JSONArray files=new JSONArray();
        for(int n=0;n<sheets;n++){Texture t=decode(d,image+n*sheetSize,format,sw,sh,null);if(format<=3){int[] pixels=t.pixels;for(int i=0;i<pixels.length;i++){int a=format<=1?pixels[i]&255:pixels[i]>>>24;pixels[i]=(a<<24)|0xffffff;}}String file=String.format(Locale.US,"%s_sheet%02d.png",name,n);write(out,file,t);files.put(file);}
        Map<Integer,int[]> table=new HashMap<>();Set<Integer>seen=new HashSet<>();
        for(p=widths;p!=0;p=i32(d,p+4)){if(!seen.add(p)||seen.size()>256)throw new IOException("Cyclic font widths.");int a=u16(d,p),b=u16(d,p+2);for(int i=a;i<=b;i++){int q=p+8+(i-a)*3;table.put(i,new int[]{s8(d,q),u8(d,q+1),s8(d,q+2)});}}
        Map<Integer,Integer> cmap=new LinkedHashMap<>();seen.clear();
        for(p=maps;p!=0;p=i32(d,p+8)){if(!seen.add(p)||seen.size()>256)throw new IOException("Cyclic font map.");int a=u16(d,p),b=u16(d,p+2),method=u16(d,p+4),q=p+12;
            if(method==0){int first=u16(d,q);for(int n=a;n<=b;n++)cmap.putIfAbsent(n,first+n-a);}
            else if(method==1){for(int n=a;n<=b;n++){int idx=u16(d,q+2*(n-a));if(idx!=65535)cmap.putIfAbsent(n,idx);}}
            else if(method==2){int count=u16(d,q);for(int n=0;n<count;n++)cmap.putIfAbsent(u16(d,q+2+4*n),u16(d,q+4+4*n));}
            else throw new IOException("Unsupported font map.");
        }
        JSONObject glyphs=new JSONObject();int per=rows*lines;
        for(Map.Entry<Integer,Integer> e:cmap.entrySet()){int code=e.getKey(),idx=e.getValue(),sheet=idx/per,cell=idx%per;if(sheet>=sheets)continue;int[] metric=table.getOrDefault(idx,new int[]{dl,dw,dc});if(encoding>1&&code<256)code=new String(new byte[]{(byte)code},"windows-1252").codePointAt(0);
            glyphs.put(Integer.toString(code),new JSONObject().put("sheet",sheet).put("x",(cell%rows)*(cw+1)).put("y",(cell/rows)*(ch+1)).put("w",metric[1]).put("h",ch).put("left",metric[0]).put("advance",metric[2]));}
        JSONObject meta=new JSONObject().put("font",name).put("baseline",baseline).put("lineFeed",line).put("cellHeight",ch).put("sheet",new JSONObject().put("files",files).put("format",format)).put("glyphs",glyphs);
        try(FileOutputStream stream=new FileOutputStream(new File(out,name+".json"))){stream.write(meta.toString().getBytes(StandardCharsets.UTF_8));stream.getFD().sync();}
    }
}
