package dev.thor.mkdd;

public final class LowerStateTest {
    static int checks;
    static void check(boolean ok) { checks++; if (!ok) throw new AssertionError("check "+checks); }
    static int bits(float v) { return Float.floatToRawIntBits(v); }
    static int[] fixture() {
        int[] p=new int[144]; p[0]=LowerState.MAGIC; p[1]=1; p[2]=144; p[3]=42;
        p[4]=1; p[5]=2; p[6]=17; p[7]=2; p[8]=0x24; p[9]=8; p[10]=1; p[11]=1; p[12]=3;
        for (int i=0;i<8;i++) {
            int b=16+i*16; p[b]=1|2|4|16|32|64; p[b+1]=8-i; p[b+2]=1; p[b+3]=12345;
            p[b+4]=7; p[b+5]=8; p[b+6]=i%2; p[b+7]=p[b+8]=-1;
            p[b+9]=bits(-300); p[b+10]=bits(20); p[b+11]=bits(900);
            p[b+12]=bits(1.57f); p[b+13]=bits(21.6f); p[b+14]=bits(0.5f);
        }
        return p;
    }
    public static void main(String[] args) {
        check(!LowerState.decode(null).available); check(!LowerState.decode(new int[2]).available);
        int[] p=fixture(); LowerState s=LowerState.decode(p);
        check(s.available && s.karts.size()==8 && s.playerKart().id==0);
        check(s.standings().get(0).id==7 && s.karts.get(0).id==0);
        check(s.karts.get(0).positionKnown && s.karts.get(0).x==-300);
        check(s.karts.get(0).driverCharacter()==7 && s.karts.get(1).driverCharacter()==8);
        check(s.karts.get(1).passengerCharacter()==7);
        check(s.karts.get(0).displayLap(3)==2);
        check(s.karts.get(0).item0==-1 && s.karts.get(0).item1==-1);
        check(s.courseSet == 0 && s.mapKey() == 0x24);
        for (int page=1;page<=4;page++) {
            int[] mod=fixture(); mod[15]=page; LowerState plus=LowerState.decode(mod);
            check(plus.available && plus.courseId==0x24 && plus.courseSet==page && plus.mapKey()==page*256+0x24);
        }
        int[] invalidSet=fixture(); invalidSet[15]=5; check(!LowerState.decode(invalidSet).available);
        invalidSet[15]=-1; check(!LowerState.decode(invalidSet).available);
        p[17]=1; check(s.karts.get(0).rank==8);
        boolean immutable=false;
        try { s.karts.clear(); } catch (UnsupportedOperationException expected) { immutable=true; }
        check(immutable);
        for(int at:new int[]{0,1,2,5,6,7,8,9,10,11,12,13,14}) {
            p=fixture(); p[at]=999; check(!LowerState.decode(p).available);
        }
        p=fixture(); p[4]=0; check(!LowerState.decode(p).available);
        p=fixture(); p[16+9]=bits(Float.NaN); check(!LowerState.decode(p).karts.get(0).positionKnown);
        p=fixture(); p[17]=9; check(LowerState.decode(p).karts.get(0).rank==-1);
        p=fixture(); p[20]=21; check(LowerState.decode(p).karts.get(0).character0==-1);
        p=fixture(); p[22]=2; check(LowerState.decode(p).karts.get(0).driverCharacter()==-1);
        p=fixture(); p[19]=5999999; check(LowerState.decode(p).karts.get(0).timeMs==-1);
        LowerState.Feed feed=new LowerState.Feed(); p=fixture();
        check(feed.accept(p,1).available); check(feed.accept(p,400_000_000).available);
        check(!feed.accept(p,500_000_001).available);
        check(!feed.accept(p,600_000_001).available);
        p[3]++; check(feed.accept(p,600_000_002).available);
        check(!feed.accept(null,600_000_003).available);
        p[3]=Integer.MIN_VALUE; check(feed.accept(p,700_000_000).available);
        check(!feed.accept(p,1).available); feed.clear();
        check(feed.accept(p,1).available);
        MapTransform map=new MapTransform(-100,-200,100,200,0,0,100,100,false);
        MapTransform.Point q=map.project(0,0,0);
        check(q.x==50 && q.y==50 && q.headingX==0 && q.headingY==1);
        q=map.project(-100,-200,Math.PI/2); check(q.x==25 && q.y==0 && Math.abs(q.headingX-1)<0.001);
        check(map.project(101,0,0)==null); check(map.project(Double.NaN,0,0)==null);
        map=new MapTransform(-100,-200,100,200,0,0,100,100,true);
        q=map.project(-100,-200,Math.PI/2); check(q.x==75 && q.headingX < -0.99);
        boolean invalid=false;
        try { new MapTransform(0,0,0,0,0,0,100,100,false); } catch (IllegalArgumentException expected) { invalid=true; }
        check(invalid);
        invalid=false;
        try { new MapTransform(-Double.MAX_VALUE,0,Double.MAX_VALUE,1,0,0,100,100,false); }
        catch (IllegalArgumentException expected) { invalid=true; }
        check(invalid);
        System.out.println("PASS "+checks+" Java lower model/map/lifecycle-feed checks; no live game evidence");
    }
}
