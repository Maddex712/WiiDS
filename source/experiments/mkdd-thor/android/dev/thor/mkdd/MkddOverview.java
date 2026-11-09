package dev.thor.mkdd;

import android.graphics.Bitmap;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/** Own-disc BMD overhead renderer. Mirrors design/bake_overview.py; never contains course data. */
public final class MkddOverview {
    private MkddOverview() {}
    static final int LONG_SIDE = 1024, SUPER = 2;
    static final double CEILING = 1200, ROAD = 2500, SKY = 3000;

    static int u16(byte[] b, int at) { return MkddArt.u16(b, at); }
    static int i32(byte[] b, int at) { return MkddArt.u32(b, at); }
    static double f32(byte[] b, int at) { return Float.intBitsToFloat(i32(b, at)); }
    static void range(byte[] b, int at, int n) throws IOException {
        if (at < 0 || n < 0 || at > b.length - n) throw new IOException("Truncated course model");
    }
    static int section(Map<String, Integer> sections, String name) throws IOException {
        Integer at = sections.get(name);
        if (at == null) throw new IOException("Missing model section " + name);
        return at;
    }
    static Map<String, Integer> sections(byte[] b) throws IOException {
        Map<String, Integer> result = new HashMap<>();
        for (int at = 0x20; at < b.length;) {
            range(b, at, 8);
            int size = i32(b, at + 4);
            if (size < 8) throw new IOException("Invalid model section");
            range(b, at, size);
            result.put(new String(b, at, 4, java.nio.charset.StandardCharsets.US_ASCII), at);
            at += size;
        }
        return result;
    }
    static float[][][] arrays(byte[] b, int o) throws IOException {
        float[][][] out = new float[26][][];
        int[] offsets = new int[13];
        for (int i = 0; i < 13; i++) offsets[i] = i32(b, o + 12 + i * 4);
        int fmt = o + i32(b, o + 8), size = i32(b, o + 4);
        for (int record = 0; record < 26; record++, fmt += 16) {
            range(b, fmt, 16);
            int attr = i32(b, fmt), count = i32(b, fmt + 4), type = i32(b, fmt + 8), frac = b[fmt + 12] & 255;
            if (attr == 255) return out;
            int slot = attr == 9 ? 0 : attr == 10 ? 1 : attr == 25 ? 2 : attr == 11 ? 3 : attr == 12 ? 4 : attr - 8;
            if (slot < 0 || slot >= offsets.length) throw new IOException("Unknown vertex attribute");
            int start = offsets[slot], end = size;
            if (start == 0) continue;
            for (int off : offsets) if (off > start && off < end) end = off;
            range(b, o + start, end - start);
            boolean color = attr == 11 || attr == 12;
            int components = color ? 4 : attr == 9 ? (count != 0 ? 3 : 2) : attr == 10 ? 3 : count != 0 ? 2 : 1;
            if (color && type != 1 && type != 2 && type != 5) throw new IOException("Unsupported vertex color");
            if (!color && (type < 0 || type > 4 || frac > 31)) throw new IOException("Unsupported vertex format");
            int width = color ? 1 : type < 2 ? 1 : type < 4 ? 2 : 4;
            int stride = color ? (type == 1 ? 3 : 4) : components * width;
            float[][] data = new float[(end - start) / stride][components];
            int p = o + start;
            for (float[] row : data) {
                for (int k = 0; k < components; k++) {
                    if (color) row[k] = k == 3 && type != 5 ? 1 : (b[p + k] & 255) / 255f;
                    else {
                        double v = type == 0 ? b[p] & 255 : type == 1 ? b[p] : type == 2 ? u16(b, p)
                                : type == 3 ? (short) u16(b, p) : f32(b, p);
                        row[k] = (float) (type == 4 ? v : v / (1L << frac));
                        p += width;
                    }
                }
                if (color) p += stride;
            }
            out[attr] = data;
        }
        throw new IOException("Unterminated vertex formats");
    }
    static double[][] positions(byte[] b, int o, float[][] source) throws IOException {
        if (source == null) throw new IOException("Missing course positions");
        int d = o + i32(b, o + 12);
        range(b, d, 0x24);
        double sx = f32(b, d + 4), sy = f32(b, d + 8), sz = f32(b, d + 12);
        double rx = (short) u16(b, d + 16) * Math.PI / 32768, ry = (short) u16(b, d + 18) * Math.PI / 32768;
        double rz = (short) u16(b, d + 20) * Math.PI / 32768;
        double cx = Math.cos(rx), ax = Math.sin(rx), cy = Math.cos(ry), ay = Math.sin(ry), cz = Math.cos(rz), az = Math.sin(rz);
        double[][] m = {{cy*cz*sx, (ax*ay*cz-cx*az)*sy, (cx*ay*cz+ax*az)*sz},
                {cy*az*sx, (ax*ay*az+cx*cz)*sy, (cx*ay*az-ax*cz)*sz}, {-ay*sx, ax*cy*sy, cx*cy*sz}};
        double[][] result = new double[source.length][3];
        for (int i = 0; i < source.length; i++) {
            if (source[i].length != 3) throw new IOException("Course positions must be XYZ");
            for (int k = 0; k < 3; k++) result[i][k] = m[k][0]*source[i][0]+m[k][1]*source[i][1]+m[k][2]*source[i][2]+f32(b,d+24+k*4);
        }
        return result;
    }
    static final class Layer { int texture, attr; double[] srt; }
    static final class Material {
        float[] color = {1,1,1,1}; boolean vertex = true, blend; float alpha = -1;
        final List<Layer> layers = new ArrayList<>();
    }
    static Material[] materials(byte[] b, int o) throws IOException {
        int[] t = new int[30];
        for (int i=0;i<t.length;i++) t[i]=i32(b,o+12+i*4);
        Material[] out = new Material[u16(b,o+8)];
        for (int i=0;i<out.length;i++) {
            Material m = out[i] = new Material();
            int r=o+t[0]+u16(b,o+t[1]+i*2)*0x14c;
            range(b,r,0x14c);
            int mc=u16(b,r+8), cc=u16(b,r+12);
            if(mc!=65535) for(int k=0;k<4;k++) m.color[k]=(b[o+t[5]+mc*4+k]&255)/255f;
            if(cc!=65535) m.vertex=b[o+t[7]+cc*8+1]==1;
            for(int slot=0;slot<8;slot++) {
                int tn=u16(b,r+0x84+slot*2);
                if(tn==65535) continue;
                Layer l=new Layer(); l.texture=u16(b,o+t[15]+tn*2);
                int tc=u16(b,r+0x28+slot*2), src=4;
                if(tc!=65535) {
                    int c=o+t[11]+tc*4; src=b[c+1]&255; int mid=b[c+2]&255;
                    if(mid>=0x1e && mid<0x3c) {
                        int tm=u16(b,r+0x48+((mid-0x1e)/3)*2);
                        if(tm!=65535) {
                            int e=o+t[13]+tm*0x64;
                            l.srt=new double[]{f32(b,e+4),f32(b,e+8),f32(b,e+16),f32(b,e+20),
                                    (short)u16(b,e+24)*Math.PI/32768,f32(b,e+28),f32(b,e+32)};
                        }
                    }
                }
                l.attr=13+src-4;
                if(src>=4 && src<=11) m.layers.add(l);
            }
            int ac=u16(b,r+0x12e), bl=u16(b,r+0x130);
            if(ac!=65535) {int a=o+t[24]+ac*8;if(b[a]==4 || b[a]==6)m.alpha=(b[a+1]&255)/255f;}
            m.blend=bl!=65535 && b[o+t[25]+bl*4]==1;
        }
        return out;
    }
    static final class Texture { int width,height,wrapS,wrapT,format; int[] pixels; }
    static Texture[] textures(byte[] b,int o) throws IOException {
        Texture[] out=new Texture[u16(b,o+8)]; int headers=o+i32(b,o+12);
        for(int i=0;i<out.length;i++) {
            int h=headers+i*32; range(b,h,32);
            Texture t=out[i]=new Texture();t.format=b[h]&255;t.wrapS=b[h+6]&255;t.wrapT=b[h+7]&255;
            int start=h+i32(b,h+28);range(b,start,0);
            byte[] copy=new byte[32+b.length-start]; System.arraycopy(b,h,copy,0,32);
            copy[28]=0;copy[29]=0;copy[30]=0;copy[31]=32;System.arraycopy(b,start,copy,32,b.length-start);
            t.width=u16(copy,2);t.height=u16(copy,4);t.pixels=MkddArt.btiPixels(copy);
        }
        return out;
    }
    static final class Shape { int material; final List<int[][]> triangles=new ArrayList<>(); }
    static Shape[] shapes(byte[] b,int o,int hierarchy) throws IOException {
        Shape[] out=new Shape[u16(b,o+8)];
        for(int i=0;i<out.length;i++) out[i]=new Shape();
        int at=hierarchy+i32(b,hierarchy+20), material=0;
        for(int n=0;n<b.length/4;n++,at+=4) {
            range(b,at,4);int kind=u16(b,at),index=u16(b,at+2);
            if(kind==0) break;
            if(kind==0x11) material=index;
            if(kind==0x12) {if(index>=out.length)throw new IOException("Invalid shape index");out[index].material=material;}
        }
        int init=o+i32(b,o+12),remap=o+i32(b,o+16),descs=o+i32(b,o+24),dl=o+i32(b,o+32),draw=o+i32(b,o+40);
        for(int i=0;i<out.length;i++) {
            int r=init+u16(b,remap+i*2)*0x28,groups=u16(b,r+2),desc=descs+u16(b,r+4),first=u16(b,r+8);
            List<int[]> attrs=new ArrayList<>();
            for(int k=0;k<26;k++,desc+=8) {range(b,desc,8);int attr=i32(b,desc),type=i32(b,desc+4);
                if(attr==255)break;if(attr<0||attr>=26||type<0||type>3)throw new IOException("Invalid display attribute");
                if(type==1&&attr>8)throw new IOException("Direct course vertex unsupported");attrs.add(new int[]{attr,type});}
            for(int g=0;g<groups;g++) {
                int d=draw+(first+g)*8,start=dl+i32(b,d+4),size=i32(b,d);range(b,start,size);
                displayList(b,start,size,attrs,out[i].triangles);
            }
        }
        return out;
    }
    static void displayList(byte[] b,int at,int size,List<int[]> attrs,List<int[][]> triangles) throws IOException {
        int end=at+size;
        while(at<end) {
            int op=b[at++]&255;if(op==0)continue;
            if(op==0x20||op==0x28||op==0x30||op==0x38){at+=4;continue;}if(op==8){at+=5;continue;}
            int prim=op&0xf8;if(prim<0x80||prim>0xb8)break;
            range(b,at,2);int n=u16(b,at);at+=2;int[][] v=new int[n][26];
            for(int[] row:v) {Arrays.fill(row,-1);for(int[] a:attrs){int width=a[1]==3?2:a[1]==0?0:1;
                if(at>end-width)throw new IOException("Truncated course display list");
                if(width>0)row[a[0]]=width==1?b[at]&255:u16(b,at);at+=width;}}
            if(prim==0x90)for(int k=0;k+2<n;k+=3)triangles.add(new int[][]{v[k],v[k+1],v[k+2]});
            else if(prim==0x98)for(int k=0;k+2<n;k++)triangles.add(new int[][]{v[k+(k&1)],v[k+1-(k&1)],v[k+2]});
            else if(prim==0xa0)for(int k=1;k+1<n;k++)triangles.add(new int[][]{v[0],v[k],v[k+1]});
            else if(prim==0x80)for(int k=0;k+3<n;k+=4){triangles.add(new int[][]{v[k],v[k+1],v[k+2]});triangles.add(new int[][]{v[k],v[k+2],v[k+3]});}
        }
    }
    static double along(double[] p,String axis) throws IOException {
        if(!axis.equals("x")&&!axis.equals("z")&&!axis.equals("-x")&&!axis.equals("-z"))throw new IOException("Invalid map axis");
        return p[axis.endsWith("x")?0:2]*(axis.startsWith("-")?-1:1);
    }
    static double[][] line(byte[] bol) throws IOException {
        range(bol,0,0x48);int n=u16(bol,0x1a),start=i32(bol,0x44);
        if(n<1)throw new IOException("Missing racing line");range(bol,start,n*32);
        double[][] result=new double[(n-1)*8+1][3];
        for(int i=0;i<n-1;i++)for(int step=0;step<8;step++)for(int k=0;k<3;k++) {
            double a=f32(bol,start+i*32+k*4),z=f32(bol,start+(i+1)*32+k*4);
            result[i*8+step][k]=a+(z-a)*step/8;
        }
        for(int k=0;k<3;k++)result[result.length-1][k]=f32(bol,start+(n-1)*32+k*4);
        return result;
    }
    static float[] ceiling(double[][] line,String[] axes,double[] bounds,double scale,int w,int h,double sky) throws IOException {
        int cw=(w+7)/8,ch=(h+7)/8;float[] out=new float[cw*ch];double[][] points=new double[line.length][3];
        for(int i=0;i<line.length;i++)points[i]=new double[]{(along(line[i],axes[0])-bounds[0])*scale,(along(line[i],axes[1])-bounds[1])*scale,line[i][1]};
        for(int y=0;y<ch;y++)for(int x=0;x<cw;x++) {
            double best=Double.POSITIVE_INFINITY,height=sky;
            for(double[] p:points){double dx=x*8+4-p[0],dy=y*8+4-p[1],d=dx*dx+dy*dy;if(d<best){best=d;height=p[2]+CEILING;}}
            out[y*cw+x]=(float)(Math.sqrt(best)/scale<=ROAD?height:sky);
        }
        return out;
    }
    static int wrap(int c,int n,int mode) {
        if(mode==0)return Math.max(0,Math.min(n-1,c));int v=Math.floorMod(c,mode==2?n*2:n);return mode==2&&v>=n?2*n-1-v:v;
    }
    static float value(float[][] data,int index,int component) throws IOException {
        if(data==null||index<0||index>=data.length||component>=data[index].length)throw new IOException("Invalid course vertex index");return data[index][component];
    }
    static int[] draw(Shape[] shapes,Material[] materials,Texture[] textures,float[][][] arrays,double[][] pos,
                      int w,int h,float[] cap,double sky,int[] keep) throws IOException {
        int count=w*h;float[] depth=new float[count],rgba=new float[count*4];Arrays.fill(depth,Float.NEGATIVE_INFINITY);
        for(int pass=0;pass<2;pass++)for(Shape shape:shapes) {
            if(shape.material>=materials.length)throw new IOException("Invalid course material");Material m=materials[shape.material];if(m.blend!=(pass==1))continue;
            List<Layer> layers=new ArrayList<>();for(Layer l:m.layers)if(l.texture<textures.length)layers.add(l);
            int limit=!layers.isEmpty()&&textures[layers.get(0).texture].format<=3?2:1;
            for(int[][] tri:shape.triangles) {
                int a=tri[0][9],b=tri[1][9],c=tri[2][9];if(a<0||b<0||c<0)continue;
                if(a>=pos.length||b>=pos.length||c>=pos.length)throw new IOException("Invalid course position index");
                double[] p=pos[a],q=pos[b],r=pos[c];
                int x0=Math.max(0,(int)Math.floor(Math.min(p[0],Math.min(q[0],r[0])))),x1=Math.min(w-1,(int)Math.ceil(Math.max(p[0],Math.max(q[0],r[0]))));
                int y0=Math.max(0,(int)Math.floor(Math.min(p[2],Math.min(q[2],r[2])))),y1=Math.min(h-1,(int)Math.ceil(Math.max(p[2],Math.max(q[2],r[2]))));
                double den=(q[2]-r[2])*(p[0]-r[0])+(r[0]-q[0])*(p[2]-r[2]);if(Math.abs(den)<1e-9)continue;
                for(int y=y0;y<=y1;y++)for(int x=x0;x<=x1;x++) {
                    int index=y*w+x;if(keep!=null&&(keep[index]>>>24)!=0)continue;
                    double aa=((q[2]-r[2])*(x+.5-r[0])+(r[0]-q[0])*(y+.5-r[2]))/den;
                    double bb=((r[2]-p[2])*(x+.5-r[0])+(p[0]-r[0])*(y+.5-r[2]))/den,cc=1-aa-bb;
                    if(aa< -1e-6||bb< -1e-6||cc< -1e-6)continue;
                    double zz=aa*p[1]+bb*q[1]+cc*r[1];if(zz<depth[index]||zz>(cap==null?sky:cap[(y/8)*((w+7)/8)+x/8]))continue;
                    double red=1,green=1,blue=1,alpha=1;
                    for(int li=0;li<Math.min(limit,layers.size());li++) {
                        Layer l=layers.get(li);if(arrays[l.attr]==null||tri[0][l.attr]<0||tri[1][l.attr]<0||tri[2][l.attr]<0)continue;
                        double s=aa*value(arrays[l.attr],tri[0][l.attr],0)+bb*value(arrays[l.attr],tri[1][l.attr],0)+cc*value(arrays[l.attr],tri[2][l.attr],0);
                        double t=aa*value(arrays[l.attr],tri[0][l.attr],1)+bb*value(arrays[l.attr],tri[1][l.attr],1)+cc*value(arrays[l.attr],tri[2][l.attr],1);
                        if(l.srt!=null){double[] v=l.srt;double u=(s-v[0])*v[2],v0=(t-v[1])*v[3],cr=Math.cos(v[4]),sr=Math.sin(v[4]);s=u*cr-v0*sr+v[0]+v[5];t=u*sr+v0*cr+v[1]+v[6];}
                        Texture tex=textures[l.texture];int pixel=tex.pixels[wrap((int)Math.floor(t*tex.height),tex.height,tex.wrapT)*tex.width+wrap((int)Math.floor(s*tex.width),tex.width,tex.wrapS)];
                        red*=(pixel>>>16&255)/255.0;green*=(pixel>>>8&255)/255.0;blue*=(pixel&255)/255.0;alpha*=(pixel>>>24)/255.0;
                    }
                    boolean vertex=m.vertex&&arrays[11]!=null&&tri[0][11]>=0&&tri[1][11]>=0&&tri[2][11]>=0;
                    double cr=m.color[0],cg=m.color[1],cb=m.color[2],ca=m.color[3];
                    if(vertex){float[][] colors=arrays[11];cr=aa*value(colors,tri[0][11],0)+bb*value(colors,tri[1][11],0)+cc*value(colors,tri[2][11],0);
                        cg=aa*value(colors,tri[0][11],1)+bb*value(colors,tri[1][11],1)+cc*value(colors,tri[2][11],1);
                        cb=aa*value(colors,tri[0][11],2)+bb*value(colors,tri[1][11],2)+cc*value(colors,tri[2][11],2);
                        ca=aa*value(colors,tri[0][11],3)+bb*value(colors,tri[1][11],3)+cc*value(colors,tri[2][11],3);}
                    red*=cr;green*=cg;blue*=cb;alpha*=ca;if(m.alpha>=0&&alpha<Math.max(m.alpha,.001))continue;
                    int k=index*4;
                    if(m.blend){double weight=Math.max(0,Math.min(1,alpha));rgba[k]=(float)(rgba[k]*(1-weight)+red*weight);rgba[k+1]=(float)(rgba[k+1]*(1-weight)+green*weight);rgba[k+2]=(float)(rgba[k+2]*(1-weight)+blue*weight);rgba[k+3]=Math.max(rgba[k+3],(float)weight);}
                    else {rgba[k]=(float)red;rgba[k+1]=(float)green;rgba[k+2]=(float)blue;rgba[k+3]=1;depth[index]=(float)zz;}
                }
            }
        }
        int[] out=keep==null?new int[count]:keep;
        for(int i=0;i<count;i++)if(keep==null||(keep[i]>>>24)==0){int k=i*4;out[i]=channel(rgba[k+3])<<24|channel(rgba[k])<<16|channel(rgba[k+1])<<8|channel(rgba[k+2]);}
        return out;
    }
    static int channel(double v){return Math.max(0,Math.min(255,(int)(v*255+.5)));}

    /** Blocking; caller supplies verified course placement. No filesystem or save access. */
    public static Bitmap bake(byte[] bmd,byte[] bol,String[] axes,double[] bounds) throws IOException {
        if(axes.length!=2||bounds.length!=4)throw new IOException("Invalid overview placement");
        for(double b:bounds)if(!Double.isFinite(b))throw new IOException("Invalid overview bounds");
        double spanX=bounds[2]-bounds[0],spanY=bounds[3]-bounds[1];if(spanX<=0||spanY<=0)throw new IOException("Empty overview bounds");
        Map<String,Integer> sec=sections(bmd);float[][][] arrays=arrays(bmd,section(sec,"VTX1"));
        double[][] pos=positions(bmd,section(sec,"JNT1"),arrays[9]);Shape[] shapes=shapes(bmd,section(sec,"SHP1"),section(sec,"INF1"));
        Material[] mats=materials(bmd,section(sec,"MAT3"));Texture[] tex=textures(bmd,section(sec,"TEX1"));
        double scale=LONG_SIDE*SUPER/Math.max(spanX,spanY);int w=(int)Math.rint(spanX*scale),h=(int)Math.rint(spanY*scale);
        if(w<2||h<2||w>2048||h>2048)throw new IOException("Invalid overview size");
        for(double[] p:pos){double x=along(p,axes[0]),y=along(p,axes[1]);p[0]=(x-bounds[0])*scale;p[2]=(y-bounds[1])*scale;}
        double[][] route=line(bol);double sky=Double.NEGATIVE_INFINITY;for(double[] p:route)sky=Math.max(sky,p[1]);sky+=SKY;
        float[] cap=ceiling(route,axes,bounds,scale,w,h,sky);
        int[] pixels=draw(shapes,mats,tex,arrays,pos,w,h,cap,sky,null);
        pixels=draw(shapes,mats,tex,arrays,pos,w,h,null,(float)sky,pixels);
        return Bitmap.createBitmap(downsample(pixels,w,h),w/2,h/2,Bitmap.Config.ARGB_8888);
    }
    static double lanczos(double x){x=Math.abs(x);return x==0?1:x>=3?0:Math.sin(Math.PI*x)*Math.sin(Math.PI*x/3)/(Math.PI*Math.PI*x*x/3);}
    /** Separable Lanczos3 with premultiplied alpha, as Pillow's RGBA resize. */
    static int[] downsample(int[] src,int w,int h) {
        int ow=w/2,oh=h/2;float[] temp=new float[ow*h*4];int[] out=new int[ow*oh];
        for(int y=0;y<h;y++)for(int x=0;x<ow;x++) {
            double centre=(x+.5)*w/ow,weight=0;double[] sum=new double[4];
            for(int xx=Math.max(0,(int)(centre-6+.5));xx<Math.min(w,(int)(centre+6+.5));xx++) {
                double k=lanczos((xx+.5-centre)/(w/(double)ow));int p=src[y*w+xx];double a=(p>>>24)/255.0;
                sum[0]+=(p>>>16&255)*a*k;sum[1]+=(p>>>8&255)*a*k;sum[2]+=(p&255)*a*k;sum[3]+=(p>>>24)*k;weight+=k;
            }
            for(int c=0;c<4;c++)temp[(y*ow+x)*4+c]=(float)Math.max(0,Math.min(255,Math.floor(sum[c]/weight+.5)));
        }
        for(int y=0;y<oh;y++)for(int x=0;x<ow;x++) {
            double centre=(y+.5)*h/oh,weight=0;double[] sum=new double[4];
            for(int yy=Math.max(0,(int)(centre-6+.5));yy<Math.min(h,(int)(centre+6+.5));yy++) {
                double k=lanczos((yy+.5-centre)/(h/(double)oh));for(int c=0;c<4;c++)sum[c]+=temp[(yy*ow+x)*4+c]*k;weight+=k;
            }
            for(int c=0;c<4;c++)sum[c]=Math.max(0,Math.min(255,Math.floor(sum[c]/weight+.5)));
            int a=(int)sum[3];int r=a==0?0:Math.min(255,(int)(sum[0]*255/a)),g=a==0?0:Math.min(255,(int)(sum[1]*255/a)),b=a==0?0:Math.min(255,(int)(sum[2]*255/a));
            out[y*ow+x]=a<<24|r<<16|g<<8|b;
        }
        return out;
    }
}
