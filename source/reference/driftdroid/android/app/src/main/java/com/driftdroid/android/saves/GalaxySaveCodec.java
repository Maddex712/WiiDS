package com.driftdroid.android.saves;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.*;

/** Retail BE <-> native LE. Layout follows Galaxy's serializers and DolphinSaveConverter.
 * All container/record bounds and the input checksum are checked BEFORE conversion.
 * This is an in-game save, never a Dolphin save state. */
public final class GalaxySaveCodec {
    private GalaxySaveCodec() {}
    private static void need(boolean ok) throws IOException {
        if (!ok) throw new IOException("Galaxy save has an invalid checksum or unsupported layout.");
    }
    static long number(byte[] b, int off, int width, boolean be) {
        long n = 0;
        for (int i = 0; i < width; i++) n = (n << 8) | (b[off + (be ? i : width - 1 - i)] & 255L);
        return n;
    }
    static void put(byte[] b, int off, long n, int width, boolean be) {
        for (int i = 0; i < width; i++) { b[off + (be ? width - 1 - i : i)] = (byte)n; n >>>= 8; }
    }
    private static int u16(byte[] b, int o, boolean be) { return (int)number(b,o,2,be); }
    private static int u32(byte[] b, int o, boolean be) throws IOException {
        long n = number(b,o,4,be); need(n <= Integer.MAX_VALUE); return (int)n;
    }
    private static void swap(byte[] b, int p, int width) {
        for (int i=0;i<width/2;i++) { byte t=b[p+i]; b[p+i]=b[p+width-1-i]; b[p+width-1-i]=t; }
    }
    static long checksum(byte[] b, boolean be) {
        int sum=0, inverse=0;
        for (int p=4;p<b.length;p+=2) { int w=u16(b,p,be); sum+=w; inverse+=(~w)&65535; }
        return ((sum&65535L)<<16)|(inverse&65535L);
    }
    public static byte[] toNative(byte[] input) throws IOException {
        need(input.length==0xbe00);
        if (number(input,4,4,true)==2) return convert(input,true);
        // Also accept WiiDS GameData.bin, but subject it to the same complete validation.
        convert(input,false);
        return input.clone();
    }
    public static byte[] convert(byte[] input, boolean fromBigEndian) throws IOException {
        need(input.length==0xbe00);
        boolean be=fromBigEndian;
        need(number(input,4,4,be)==2 && number(input,8,4,be)==19 && number(input,12,4,be)==input.length);
        need(number(input,0,4,be)==checksum(input,be));
        byte[] b=input.clone();
        int expected=0x140;
        for(int i=0;i<19;i++) {
            int m=16+i*16;
            String name=i==18?"sysconf":(i%3==0?"mario":i%3==1?"luigi":"config")+(i/3+1);
            byte[] encoded=name.getBytes(StandardCharsets.US_ASCII);
            for(int j=0;j<12;j++) need(b[m+j]==(j<encoded.length?encoded[j]:0));
            int off=u32(b,m+12,be), size=i==18?0x80:i%3==2?0x60:0xf80;
            need(off==expected && off+size<=b.length); expected+=size;
            swap(b,m+12,4);
            need(b[off]==1 && b[off+2]==0 && b[off+3]==0);
            int chunks=b[off+1]&255, c=off+4;
            Set<String> seen=new HashSet<>();
            for(int j=0;j<chunks;j++) {
                need(c+12<=off+size);
                byte[] tag=Arrays.copyOfRange(b,c,c+4);
                if(!be) for(int k=0;k<2;k++){byte t=tag[k];tag[k]=tag[3-k];tag[3-k]=t;}
                String sig=new String(tag,StandardCharsets.US_ASCII);
                String allowed=i==18?"SYSC":i%3==2?"CONF,MII ,MISC":"PLAY,FLG1,PCE1,VLE1,EVL1,SPN1,GALA";
                need(Arrays.asList(allowed.split(",")).contains(sig) && seen.add(sig));
                Map<String,Long> hashes=new HashMap<>();
                hashes.put("PLAY",0x27c90fL);hashes.put("FLG1",0x65020442L);hashes.put("PCE1",0xf5de1dc0L);
                hashes.put("SPN1",0x12345679L);hashes.put("VLE1",0x564c4531L);hashes.put("GALA",0xbf0640eeL);
                hashes.put("CONF",0x2432daL);hashes.put("MII ",0x2836e9L);hashes.put("MISC",1L);hashes.put("SYSC",1L);
                need(hashes.containsKey(sig) && number(b,c+4,4,be)==hashes.get(sig));
                int n=u32(b,c+8,be); need(n>=12 && n<=off+size-c);
                payload(b,c+12,n-12,sig,be);
                swap(b,c,4); swap(b,c+4,4); swap(b,c+8,4); c+=n;
            }
            need(i==18?seen.contains("SYSC"):i%3==2?seen.containsAll(Arrays.asList("CONF","MII ","MISC")):
                seen.containsAll(Arrays.asList("PLAY","FLG1","PCE1","SPN1","GALA")));
        }
        swap(b,4,4); swap(b,8,4); swap(b,12,4);
        put(b,0,checksum(b,!be),4,!be);
        return b;
    }
    private static void payload(byte[] b,int p,int n,String sig,boolean be) throws IOException {
        switch(sig) {
            case "CONF": need(n==1); break;
            case "MII ": need(n==9 || n==10); break; // RFLCreateID is opaque bytes, icon is u8.
            case "MISC": need(n==1 || n==9); if(n==9) swap(b,p+1,8); break;
            case "PLAY": need(n==5 || n==7 || n==9); swap(b,p+1,4); if(n>=7)swap(b,p+5,2); if(n==9)swap(b,p+7,2); break;
            case "FLG1": case "PCE1": case "VLE1": case "EVL1":
                need(n%2==0); for(int k=0;k<n;k+=2)swap(b,p+k,2); break;
            case "SPN1": {
                need(n>=1); int c=1;
                for(int g=0;g<(b[p]&255);g++) {
                    need(c+6<=n); int len=u16(b,p+c+2,be); need(len>=6 && len<=n-c);
                    int scenarios=b[p+c+4]&255, s=c+6;
                    for(int i=0;i<scenarios;i++) {
                        need(s+2<=c+len); int sl=u16(b,p+s,be); need(sl>=2 && sl<=c+len-s);
                        swap(b,p+s,2); s+=sl;
                    }
                    need(s==c+len); swap(b,p+c,2); swap(b,p+c+2,2); c+=len;
                }
                need(c==n); break;
            }
            case "GALA": {
                need(n>=6); int count=u16(b,p,be), attrs=u16(b,p+2,be), stride=u16(b,p+4,be);
                need(attrs==4 && stride==20 && 6L+attrs*4L+count*(long)stride==n);
                // Known serializer order and offsets: galaxy hash, two flag bytes, eight u16 coins.
                int[] offsets={0,2,3,4};
                for(int a=0;a<attrs;a++) { need(u16(b,p+8+a*4,be)==offsets[a]);swap(b,p+6+a*4,2);swap(b,p+8+a*4,2); }
                for(int a=0;a<count;a++) { int e=p+6+attrs*4+a*stride;swap(b,e,2);for(int k=4;k<20;k+=2)swap(b,e+k,2); }
                swap(b,p,2);swap(b,p+2,2);swap(b,p+4,2); break;
            }
            case "SYSC": {
                need(n>=4);int attrs=u16(b,p,be), stride=u16(b,p+2,be);
                need(attrs==3 && stride==20 && n==4+attrs*4+stride);
                int[] offsets={0,8,16};
                for(int a=0;a<attrs;a++){need(u16(b,p+6+a*4,be)==offsets[a]);swap(b,p+4+a*4,2);swap(b,p+6+a*4,2);}
                int t=p+4+attrs*4;swap(b,t,8);swap(b,t+8,8);swap(b,t+16,4);swap(b,p,2);swap(b,p+2,2);break;
            }
            default: throw new IOException("Unsupported Galaxy save chunk.");
        }
    }
}
