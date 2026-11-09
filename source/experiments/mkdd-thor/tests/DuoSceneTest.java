package dev.thor.mkdd;

/** JVM checks for the duo screen's pure-Java choices: scene, names, layout, map fit, row motion. */
public final class DuoSceneTest {
    static int checks;
    static void check(boolean ok) { checks++; if (!ok) throw new AssertionError("check "+checks); }
    static boolean near(double a, double b) { return Math.abs(a-b) < 0.01; }
    static int bits(float v) { return Float.floatToRawIntBits(v); }
    static int[] packet(int mode, int laps, int target, int flags) {
        int[] p=new int[144]; p[0]=LowerState.MAGIC; p[1]=1; p[2]=144; p[3]=7;
        p[4]=flags; p[5]=mode; p[6]=10; p[7]=1; p[8]=0x24; p[9]=4; p[10]=1; p[11]=1; p[12]=laps; p[13]=target;
        for (int i=0;i<4;i++) {
            int b=16+i*16; p[b]=1|2|4|8|32|64|(laps > 0 ? 16 : 0); p[b+1]=4-i; p[b+2]=laps > 0 ? 1 : 0;
            p[b+3]=83456; p[b+4]=7; p[b+5]=8; p[b+6]=i%2; p[b+7]=0; p[b+8]=16;
            p[b+9]=bits(10); p[b+10]=bits(0); p[b+11]=bits(20); p[b+12]=bits(0); p[b+13]=bits(80);
            p[b+14]=bits(.25f);
        }
        return p;
    }
    static DuoScene scene(int[] p) { return DuoScene.of(LowerState.decode(p)); }

    public static void main(String[] args) {
        DuoScene idle=DuoScene.of(LowerState.unavailable());
        check(idle.kind==DuoScene.IDLE && idle.focus==null && idle.standings.isEmpty());
        check(DuoScene.of(null).kind==DuoScene.IDLE);

        DuoScene gp=scene(packet(2, 3, 0, 1));
        check(gp.kind==DuoScene.RACE && gp.focusIsPlayer && gp.focus.id==0 && gp.lap==2);
        check(gp.standings.get(0).id==3 && gp.isFocus(gp.state.karts.get(0)));
        DuoScene demo=scene(packet(2, 3, 0, 1|2));
        check(!demo.focusIsPlayer && demo.focus.id==3 && demo.focus.rank==1);
        DuoScene spectate=scene(packet(3, 3, -1, 1));
        check(!spectate.focusIsPlayer && spectate.focus.rank==1);
        check(scene(packet(1, 7, 0, 1)).kind==DuoScene.TIME_ATTACK && scene(packet(1, 7, 0, 1)).lap==2);
        DuoScene battle=scene(packet(4, 0, 0, 1));
        check(battle.kind==DuoScene.BATTLE && battle.lap==-1);
        check(scene(packet(6, 0, 0, 1|4)).state.paused);

        // Items follow character slots; the back-seat rider holds the usable one.
        LowerState.Kart k0=gp.state.karts.get(0), k1=gp.state.karts.get(1);
        check(k0.driverSlot==0 && DuoScene.driverItem(k0)==0 && DuoScene.usableItem(k0)==16);
        check(k1.driverSlot==1 && DuoScene.driverItem(k1)==16 && DuoScene.usableItem(k1)==0);
        int[] p=packet(2, 3, 0, 1); p[16]&=~64;
        LowerState.Kart unknownSeat=LowerState.decode(p).karts.get(0);
        check(DuoScene.usableItem(unknownSeat)==-1 && DuoScene.driverItem(unknownSeat)==-1);

        check(DuoScene.time(83456).equals("1:23.456") && DuoScene.time(0).equals("0:00.000"));
        check(DuoScene.time(-1).equals("-:--.---") && DuoScene.time(600000).equals("10:00.000"));
        check(DuoScene.hudTime(17067).equals("00:17:067") && DuoScene.hudTime(83456).equals("01:23:456"));
        check(DuoScene.hudTime(-1).equals("--:--:---") && DuoScene.hudTime(5999998).equals("99:59:998"));
        check(DuoScene.ordinal(1).equals("st") && DuoScene.ordinal(2).equals("nd") && DuoScene.ordinal(3).equals("rd"));
        check(DuoScene.ordinal(4).equals("th") && DuoScene.ordinal(11).equals("th") && DuoScene.ordinal(8).equals("th"));

        check("Mario".equals(DuoNames.character(7)) && "Donkey Kong".equals(DuoNames.character(13)));
        check("Petey Piranha".equals(DuoNames.character(20)) && DuoNames.character(0)==null && DuoNames.character(21)==null);
        check("Luigi Circuit".equals(DuoNames.course(0x24)) && "Baby Park".equals(DuoNames.course(0x21)));
        check(DuoNames.course(0x27)==null && DuoNames.course(0x34)==null && DuoNames.course(-1)==null);
        check("Green Shell".equals(DuoNames.item(0)) && "Fireballs".equals(DuoNames.item(21)));
        check(DuoNames.item(16)==null && DuoNames.item(1)==null && DuoNames.item(-1)==null && DuoNames.item(22)==null);
        check("Bob-omb Blast".equals(DuoNames.mode(6)) && DuoNames.mode(8)==null);

        DuoLayout thor=new DuoLayout(1240, 1080);
        check(thor.scale==1 && thor.left==0 && thor.top==0);
        check(thor.header.b <= thor.board.t && thor.board.r < thor.side.l && thor.side.r <= 1240 && thor.side.b <= 1080);
        check(DuoLayout.rowTop(7)+4+DuoLayout.ROW <= thor.board.b && DuoLayout.rowTop(0) == thor.board.t);
        check(thor.mapArea.l >= thor.side.l && thor.mapArea.r <= thor.side.r && thor.mapArea.b <= thor.side.b);
        DuoLayout half=new DuoLayout(620, 540);
        check(half.scale==.5f && near(half.side.r, 608));
        DuoLayout square=new DuoLayout(1240, 1240);
        check(square.scale==1 && square.top==80 && near(square.screen.b, 1160));
        check(new DuoLayout(0, 0).scale==0);

        // Map art rectangle and MapTransform markers must agree at the bounds' corners.
        DuoLayout.Box area=thor.mapArea, fit=DuoLayout.fit(4000, 2000, area);
        MapTransform t=new MapTransform(-1000, -500, 3000, 1500, area.l, area.t, area.w(), area.h(), false);
        MapTransform.Point a=t.project(-1000, -500, 0), b=t.project(3000, 1500, 0);
        check(near(a.x, fit.l) && near(a.y, fit.t) && near(b.x, fit.r) && near(b.y, fit.b));
        check(near(fit.cx(), area.cx()) && near(fit.w(), area.w()) && fit.h() <= area.h());
        check(t.project(3001, 0, 0)==null);

        // Fitting by the drawn part: a texture whose art fills its middle half grows to fill the area,
        // still centred on that art, and MapTransform on the same box keeps markers on it.
        DuoLayout.Box grown=DuoLayout.contentFit(100, 200, .25, .25, .75, .75, area);
        double k=Math.min(area.w()/50.0, area.h()/100.0);
        check(near(grown.w(), 100*k) && near(grown.h(), 200*k));
        check(near(grown.l+grown.w()*.5, area.cx()) && near(grown.t+grown.h()*.5, area.cy()));
        check(near(grown.t+grown.h()*.25, area.cy()-50*k) && near(grown.t+grown.h()*.75, area.cy()+50*k));
        DuoLayout.Box whole=DuoLayout.contentFit(4000, 2000, 0, 0, 1, 1, area);
        check(near(whole.l, fit.l) && near(whole.t, fit.t) && near(whole.r, fit.r) && near(whole.b, fit.b));
        MapTransform g=new MapTransform(0, 0, 100, 200, grown.l, grown.t, grown.w(), grown.h(), false);
        MapTransform.Point mid=g.project(50, 100, 0);
        check(near(mid.x, area.cx()) && near(mid.y, area.cy()));

        // Retail orientation 2 (Luigi Circuit): u=-x, v=-z, so world min corner lands bottom-right.
        DuoMapAxes flip=DuoMapAxes.parse("-x", "-z");
        check(flip.u(100, 200)==-100 && flip.v(100, 200)==-200);
        check(near(flip.heading(0), Math.PI) || near(flip.heading(0), -Math.PI));
        DuoMapAxes turn=DuoMapAxes.parse("z", "-x");  // orientation 3
        check(turn.u(3, 7)==7 && turn.v(3, 7)==-3 && near(turn.heading(0), Math.PI/2));
        DuoMapAxes plain=DuoMapAxes.parse("x", "z");
        check(near(plain.heading(1.2), 1.2) && plain.u(5, 6)==5);
        check(DuoMapAxes.parse("x", "x")==null && DuoMapAxes.parse("-z", "z")==null && DuoMapAxes.parse("y", "z")==null);
        MapTransform luigi=new MapTransform(-16332.9, -32069.7, 18519.0, 37634.3, 0, 0, 128, 256, false);
        // Retail table: x -18519.0..16332.9, z -37634.3..32069.7; top-left is world (maxX, maxZ).
        MapTransform.Point corner=luigi.project(flip.u(16332.9, 32069.7), flip.v(16332.9, 32069.7), 0);
        check(corner != null && near(corner.x, 0) && near(corner.y, 0));
        MapTransform.Point far=luigi.project(flip.u(-18519.0, -37634.3), flip.v(-18519.0, -37634.3), 0);
        check(far != null && near(far.x, 128) && near(far.y, 256));

        DuoRowMotion m=new DuoRowMotion();
        check(!m.update(2, 5, 0) && m.slot(2)==5);
        check(m.update(2, 1, 1_000_000_000L) && m.slot(2)==5);
        check(m.update(2, 1, 1_080_000_000L) && m.slot(2) < 5 && m.slot(2) > 1);
        check(!m.update(2, 1, 1_160_000_000L) && m.slot(2)==1);
        m.reset(); check(!m.update(2, 6, 2_000_000_000L) && m.slot(2)==6);
        check(!m.update(9, 1, 0) && m.slot(9)==0);

        System.out.println("DuoSceneTest: "+checks+" checks passed");
    }
}
