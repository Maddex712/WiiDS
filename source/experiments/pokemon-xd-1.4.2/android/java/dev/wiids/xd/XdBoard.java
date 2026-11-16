package dev.wiids.xd;

import android.animation.ValueAnimator;
import android.content.Context;
import android.graphics.*;
import android.os.SystemClock;
import android.view.View;
import android.view.ViewGroup;
import java.util.ArrayList;
import java.util.Locale;

/**
 * Display-only lower screen in 320-unit design space (pass 5b look, restored for the controller-only
 * build). Field screens sit inside the P*DA hologram with the Orre map, objective and party plates;
 * battle screens are full-bleed in the night Colosseum under a steel header with foe and own battle
 * plates. Every child is an inert XdKey plate used only for drawing: nothing here is clickable or
 * focusable, nothing reads input and nothing is sent to the game. The board shows only fields the
 * companion state really carries and leaves absent ones out.
 */
final class XdBoard extends ViewGroup {
    static final float DESIGN=320f;
    /** Screens, chosen from the state the game publishes. OVERVIEW is battle without a known menu. */
    static final int FIELD=0,COMMAND=1,MOVES=2,TARGET=3,PARTY=4,OVERVIEW=5,WAITING=6;
    final XdKey[] party=new XdKey[6];
    XdState state=XdState.unavailable();
    int mode=FIELD;
    /** Field location (live or last known), header prompt in game wording, and a small status tag. */
    String location="",prompt="",tag="";
    private float u=1;
    private final RectF foePlate=new RectF(),youPlate=new RectF(),screen=new RectF(),tab=new RectF(),strip=new RectF(),map=new RectF(),
            objective=new RectF(),content=new RectF(),side=new RectF(),cap=new RectF(),r=new RectF();
    private final boolean motion;
    private final Paint fieldPaint=new Paint();
    private float fr=1,fg=1,fb=1;private int fieldKey=-1;
    void fieldColor(int color){
        float r=(color>>16)&255,g=(color>>8)&255,b=color&255;
        float l=(r*.299f+g*.587f+b*.114f),mx=Math.max(24,Math.max(r,Math.max(g,b)));
        float shade=Math.min(1,.48f+.52f*l/150f);
        float nr=shade*(.80f+.20f*r/mx),ng=shade*(.80f+.20f*g/mx),nb=shade*(.80f+.20f*b/mx);
        fr+=(nr-fr)*.28f;fg+=(ng-fg)*.28f;fb+=(nb-fb)*.28f;
        int key=(Math.round(fr*255)<<16)|(Math.round(fg*255)<<8)|Math.round(fb*255);
        if(key==fieldKey)return;fieldKey=key;drawingDirty=true;invalidate();
        fieldPaint.setColorFilter(new ColorMatrixColorFilter(new float[]{fr,0,0,0,0, 0,fg,0,0,0, 0,0,fb,0,0, 0,0,0,1,0}));
    }
    private Picture bezelPicture;
    private int bezelWidth,bezelHeight;
    private final RectF bezelScreen=new RectF();
    private Picture boardPicture;
    private boolean drawingDirty=true;
    private long drawingBucket=-1;
    private int drawingWidth,drawingHeight;

    XdBoard(Context c){
        super(c);setWillNotDraw(false);setClickable(false);setFocusable(false);setLongClickable(false);
        boolean animate;try{animate=ValueAnimator.areAnimatorsEnabled();}catch(Throwable old){animate=true;}
        motion=animate;
    }
    void bind(XdState s){drawingDirty=true;state=s;for(int i=0;i<getChildCount();i++)((XdKey)getChildAt(i)).u=u;invalidate();}

    static int mode(XdState s){
        return s.visible&&s.screenKind==XdState.FIELD?FIELD:WAITING;
    }
    /** XD's native battle command rows, in the 3DS colour code. */
    static int commandTone(String label){
        switch(label.toLowerCase(Locale.ROOT)){
            case "fight":return XdSkin.FIGHT;case "pokemon":case "pokémon":return XdSkin.POKEMON;
            case "items":case "item":case "bag":return XdSkin.BAG;case "run":case "give in":return XdSkin.RUN;case "call":return XdSkin.CALL;
            default:return 0;
        }
    }
    static boolean duplicatesMove(XdState s,String label){
        for(XdState.Move m:s.moves)if(m.id!=0&&label.equalsIgnoreCase(m.name))return true;
        return false;
    }
    private boolean device(){return mode==FIELD;}
    private boolean plates(){return mode==COMMAND||mode==MOVES||mode==PARTY||mode==OVERVIEW;}

    @Override protected void onMeasure(int w,int h){
        int width=MeasureSpec.getSize(w);u=width/DESIGN;
        int height=MeasureSpec.getMode(h)==MeasureSpec.UNSPECIFIED?Math.round(u*279):MeasureSpec.getSize(h);
        arrange(width,height,false);setMeasuredDimension(width,height);
    }
    @Override protected void onLayout(boolean changed,int l,int t,int rr,int b){arrange(rr-l,b-t,true);drawingDirty=true;}

    /** Fixes the geometry for the screen, then places the inert plates. */
    private void arrange(int width,int height,boolean layout){
        float W=width,H=height,m=u*6,gap=u*4;
        ArrayList<XdKey> grid=new ArrayList<>(),row=new ArrayList<>(),panels=new ArrayList<>();
        for(int i=0;i<getChildCount();i++){XdKey k=(XdKey)getChildAt(i);if(k.kind==XdKey.PARTY)continue;
            if(k.place==XdKey.AT_GRID)grid.add(k);else if(k.place==XdKey.AT_STRIP)row.add(k);else if(k.place==XdKey.AT_PANEL)panels.add(k);}
        if(device()){
            // The P*DA fills the panel: no dock, so the screen runs to the bottom edge.
            screen.set(u*8,u*34,W-u*8,H-u*8);
            tab.set(0,u*6,u*50+XdSkin.captionWidth("MAP",u*9.5f,0.12f),u*27);
            content.set(screen.left+u*6,screen.top+u*6,screen.right-u*6,screen.bottom-u*6);
            strip.setEmpty();foePlate.setEmpty();youPlate.setEmpty();
            float sw=u*94,cg=u*3,ch=(content.height()-cg*5)/6;
            side.set(content.right-sw,content.top,content.right,content.bottom);
            for(int i=0;i<6;i++){party[i].style=XdKey.MINI_STYLE;put(party[i],side.left,side.top+i*(ch+cg),side.right,side.top+i*(ch+cg)+ch,layout);}
            map.set(content.left,content.top,side.left-u*6,content.bottom);
            float oh=u*50;objective.set(map.left+u*5,map.bottom-oh-u*5,map.right-u*5,map.bottom-u*5);
            for(XdKey k:grid)put(k,0,0,0,0,layout);for(XdKey k:row)put(k,0,0,0,0,layout);for(XdKey k:panels)put(k,0,0,0,0,layout);
            return;
        }
        screen.set(0,0,W,H);tab.setEmpty();map.setEmpty();objective.setEmpty();side.setEmpty();
        // Header strip with a decorative porthole cap at its right end.
        strip.set(m,u*4,W-m-u*34,u*28);cap.set(W-m-u*30,u*2,W-m-u*2,u*30);
        if(plates()){foePlate.set(m,u*32,W/2-u*2,u*62);youPlate.set(W/2+u*2,u*32,W-m,u*62);}else{foePlate.setEmpty();youPlate.setEmpty();}
        content.set(m,plates()?u*66:u*33,W-m,H-m);
        float top=content.top;
        if(mode==COMMAND&&!row.isEmpty()){
            // Commands the game offers right now, as information plates, above the move plates.
            float rh=u*40;row.sort((a,b)->rank(a)-rank(b));
            float total=0;for(XdKey k:row)total+=weight(k);
            float avail=content.width()-gap*(row.size()-1),x=content.left;
            for(XdKey k:row){float w=avail*weight(k)/total;put(k,x,top,x+w,top+rh,layout);x+=w+gap;}
            top+=rh+gap;
        }else for(XdKey k:row)put(k,0,0,0,0,layout);
        if(!grid.isEmpty()){
            float cw=(content.width()-gap)/2,ch=(content.bottom-top-gap)/2;
            for(XdKey k:grid){int i=k.slot;float gx=content.left+(i%2)*(cw+gap),gy=top+(i/2)*(ch+gap);put(k,gx,gy,gx+cw,gy+ch,layout);}
        }
        if(!panels.isEmpty()){
            ArrayList<XdKey> foes=new ArrayList<>(),allies=new ArrayList<>();for(XdKey k:panels)(k.ally?allies:foes).add(k);
            int rows=(foes.isEmpty()?0:1)+(allies.isEmpty()?0:1);float ph=Math.min(u*90,(content.height()-gap*(rows-1))/Math.max(1,rows));
            float py=content.top+(content.height()-rows*ph-(rows-1)*gap)/2;
            for(int half=0;half<2;half++){
                ArrayList<XdKey> side=half==0?foes:allies;if(side.isEmpty())continue;int n=Math.max(2,side.size());float pw=(content.width()-gap*(n-1))/n;
                for(int i=0;i<side.size();i++){float px=content.left+i*(pw+gap);put(side.get(i),px,py,px+pw,py+ph,layout);}
                py+=ph+gap;
            }
        }
        if(mode==PARTY||mode==WAITING){
            float g=u*6,cw=(content.width()-g)/2,ch=(content.height()-g*2)/3;
            for(int i=0;i<6;i++){party[i].style=XdKey.TILE_STYLE;float x=content.left+(i%2)*(cw+g),y=content.top+(i/2)*(ch+g);put(party[i],x,y,x+cw,y+ch,layout);}
        }else for(XdKey k:party)put(k,0,0,0,0,layout);
    }
    private static float weight(XdKey k){return k.color==XdSkin.FIGHT?1.25f:k.color==XdSkin.RUN?0.8f:1f;}
    /** Fight first, then the Gen 6 order: Bag, XD's Snag Ball, Call or Run, Pokemon. */
    private static int rank(XdKey k){int c=k.color;return c==XdSkin.FIGHT?-1:c==XdSkin.BAG?0:c==XdSkin.SNAG?1:c==XdSkin.CALL?2:c==XdSkin.RUN?3:c==XdSkin.POKEMON?4:5;}
    private void put(View v,float l,float t,float rr,float b,boolean layout){
        int L=Math.round(l),T=Math.round(t),R=Math.round(rr),B=Math.round(b);
        if(v instanceof XdKey)((XdKey)v).u=u;
        v.measure(MeasureSpec.makeMeasureSpec(Math.max(0,R-L),MeasureSpec.EXACTLY),MeasureSpec.makeMeasureSpec(Math.max(0,B-T),MeasureSpec.EXACTLY));
        if(layout)v.layout(L,T,R,B);
    }

    @Override protected void onDraw(Canvas c){
        if(!isShown())return;
        long bucket=motion?SystemClock.uptimeMillis()/100:0;
        if(boardPicture==null||drawingDirty||(!device()&&drawingBucket!=bucket)||drawingWidth!=getWidth()||drawingHeight!=getHeight()){
            if(getWidth()<=0||getHeight()<=0)return;
            Picture next=new Picture();Canvas recorded=next.beginRecording(getWidth(),getHeight());
            drawBoard(recorded);next.endRecording();boardPicture=next;
            drawingDirty=false;drawingBucket=bucket;drawingWidth=getWidth();drawingHeight=getHeight();
        }
        c.drawPicture(boardPicture);
        if(!device()&&motion&&isAttachedToWindow()&&isShown())postInvalidateDelayed(100);
    }
    private void drawBoard(Canvas c){
        float w=getWidth(),h=getHeight();
        float phase=motion&&!device()?(SystemClock.uptimeMillis()%600000L)/1000f*14f:0;
        if(device()){
            if(bezelPicture==null||bezelWidth!=getWidth()||bezelHeight!=getHeight()||!bezelScreen.equals(screen)){
                Picture next=new Picture();Canvas recorded=next.beginRecording(getWidth(),getHeight());
                r.set(0,0,w,h);XdSkin.bezel(recorded,r,screen,u);next.endRecording();
                bezelPicture=next;bezelWidth=getWidth();bezelHeight=getHeight();bezelScreen.set(screen);
            }
            int bg=c.saveLayer(0,0,w,h,fieldPaint);
            c.drawPicture(bezelPicture);
            XdSkin.hexes(c,screen,0x16FFFFFF,u,phase);
            c.restoreToCount(bg);
            drawTab(c);drawMap(c,phase);
        }else{
            r.set(0,0,w,h);XdSkin.arena(c,r,u,motion?XdSkin.clock():0);
            drawStrip(c);
            if(!foePlate.isEmpty()){
                if(state.targeted.species!=0)XdSkin.battlePlate(c,foePlate,state.targeted,true,u);
                if(state.acting.species!=0)XdSkin.battlePlate(c,youPlate,state.acting,false,u);
            }
        }

    }
    /** Small tracked status tag (menu open, before save, title menu, updating), right-aligned at x. */
    private void drawTag(Canvas c,float right,float cy){
        if(tag.isEmpty())return;
        float size=u*7.5f,tw=XdSkin.captionWidth(tag,size,0.12f);
        r.set(right-tw-u*14,cy-u*8,right,cy+u*8);
        XdSkin.fill.setColor(0xE6000A0D);c.drawPath(XdSkin.shape(r,u*2.5f),XdSkin.fill);
        XdSkin.stroke.setStrokeWidth(u*0.8f);XdSkin.stroke.setColor(XdSkin.TEAL);c.drawPath(XdSkin.shape(r,u*2.5f),XdSkin.stroke);
        XdSkin.caption(c,tag,r.centerX(),cy+u*2.7f,size,0xFFDDF8F6,0,0,0.12f,Paint.Align.CENTER);
    }
    /** Black header tab on the bezel's left edge: teal P*DA mark and the app name in tracked capitals. */
    private void drawTab(Canvas c){
        XdSkin.headerTab(c,tab,u);
        float g=u*14;XdSkin.glyph(c,"pda",u*10,tab.centerY()-g/2,g,XdSkin.SAND_TOP,u);
        XdSkin.caption(c,"MAP",u*29,tab.centerY()+u*3.4f,u*9.5f,0xFFDDF8F6,0xFF001014,u*1f,0.12f,Paint.Align.LEFT);
        drawMonitor(c);
        drawTag(c,getWidth()-u*8,tab.centerY());
    }
    /** Shadow Monitor counts on the bezel, beside the tab: snagged and missed only, as observed. */
    private void drawMonitor(Canvas c){
        int snagged=0,missed=0;for(XdState.Shadow s:state.shadows){if(s.snagged)snagged++;else if(s.missed)missed++;}
        if(state.shadows.isEmpty())return;
        String a="Snagged "+snagged,b="Missed "+missed;
        Paint p=XdSkin.font(XdSkin.HEAVY,u*8.5f,0);float wa=p.measureText(a),wb=p.measureText(b);
        float x=tab.right+u*6,h=u*19,y=tab.centerY()-h/2,wTot=u*24+wa+u*10+wb+u*8;
        r.set(x,y,x+wTot,y+h);XdSkin.fill.setColor(0xE6201030);c.drawPath(XdSkin.shape(r,u*3),XdSkin.fill);
        XdSkin.ring(c,r,u*3,XdSkin.alpha(XdSkin.SHADOW_AURA,0.85f),u*0.9f);
        XdSkin.glyph(c,"heart",x+u*6,y+u*4.5f,u*10,XdSkin.SHADOW_LIGHT,u);
        XdSkin.outlined(c,a,x+u*21,y+h*0.68f,XdSkin.HEAVY,u*8.5f,XdSkin.TEXT,0,0,Paint.Align.LEFT);
        XdSkin.fill.setColor(0x80FFFFFF);c.drawRect(x+u*21+wa+u*4,r.centerY()-u*1,x+u*21+wa+u*6,r.centerY()+u*1,XdSkin.fill);
        XdSkin.outlined(c,b,x+u*21+wa+u*10,y+h*0.68f,XdSkin.HEAVY,u*8.5f,0xFFFFB8B0,0,0,Paint.Align.LEFT);
    }

    /**
     * Battle header as an XD battle plate: brushed steel ending in a porthole cap with a ball mark;
     * party pips and the game's prompt (or a tracked caption) in the black inset, status tag at right.
     */
    private void drawStrip(Canvas c){
        RectF plate=new RectF(strip.left-u*1,strip.top-u*1.5f,getWidth()-u*4,strip.bottom+u*1.5f);
        XdSkin.steel(c,plate,u*4,0,u);
        XdSkin.porthole(c,cap.centerX(),cap.centerY(),cap.height(),0,u);
        XdSkin.monSlot(c,cap.centerX(),cap.centerY(),cap.height()-u*9,false,false,u);
        RectF well=new RectF(strip.left+u*3,strip.top+u*2,strip.right-u*2,strip.bottom-u*2);XdSkin.well(c,well,u*3,u);
        float d=u*8.2f,x=strip.left+u*10+d/2,cy=strip.centerY();int shown=0;
        for(int i=0;i<6;i++){
            XdState.Pokemon p=state.party.get(i);if(p.species==0)continue;float cx=x+shown*u*10.4f;shown++;
            boolean fainted=p.maxHp>0&&p.hp==0;String st=XdSkin.statusLabel(p.status,p.hp,p.maxHp);
            int top=fainted?0xFF5A606C:p.shadowId!=0?0xFF8A5CCF:!st.isEmpty()?XdSkin.statusColor(st):0xFFE2463A;
            XdSkin.ball(c,cx,cy,d,top,0xFF20242C,u);
            if(fainted){XdSkin.fill.setColor(0x80000000);c.drawCircle(cx,cy,d/2,XdSkin.fill);}
            if(state.actorPartySlot==i&&state.acting.species!=0){XdSkin.stroke.setStrokeWidth(u*1.2f);XdSkin.stroke.setColor(XdSkin.FOCUS);c.drawCircle(cx,cy,d/2+u*1.4f,XdSkin.stroke);}
        }
        float tx=strip.left+u*76,tagRight=well.right-u*3;
        float tagW=tag.isEmpty()?0:XdSkin.captionWidth(tag,u*7.5f,0.12f)+u*18;
        if(prompt.startsWith("#")){
            String cap=prompt.substring(1);float size=u*9,track=0.14f,avail=tagRight-tagW-tx;
            while(size>u*6.5f&&XdSkin.captionWidth(cap,size,track)>avail)size-=u*0.25f;
            XdSkin.caption(c,cap,tx,cy+size*0.36f,size,0xFFDDF8F6,0xFF14203A,u*1.4f,track,Paint.Align.LEFT);
        }else if(!prompt.isEmpty()){
            float size=u*9.5f;Paint p=XdSkin.font(XdSkin.BOLD,size,XdSkin.TEXT);
            p.setShadowLayer(u*0.8f,0,u*0.5f,0xCC000000);c.drawText(XdSkin.fit(prompt,p,tagRight-tagW-tx),tx,cy+size*0.36f,p);p.clearShadowLayer();
        }
        drawTag(c,tagRight,cy);
    }

    /**
     * Map app: a stylised Orre drawn here (sand basin, rock ridges, a coast, scooter roads), with the
     * places XD names and Michael's marker on the current one. Positions are illustrative, not traced.
     */
    private static final String[][] PLACES={
        {"Outskirt Stand","0.30","0.46"},{"Phenac City","0.20","0.66"},{"Pyrite Town","0.50","0.32"},{"Agate Village","0.74","0.20"},
        {"Gateon Port","0.84","0.62"},{"Pokémon HQ Lab","0.60","0.80"},{"Realgam Tower","0.88","0.38"},{"Mt. Battle","0.62","0.10"},
        {"Kaminko's House","0.12","0.36"},{"Cipher Key Lair","0.42","0.66"},{"Snagem Hideout","0.08","0.84"},{"Citadark Isle","0.96","0.08"}};
    private static final int[][] ROADS={{0,1},{0,2},{2,3},{2,7},{3,6},{6,4},{4,5},{5,9},{9,1},{0,8},{1,10},{9,2}};
    private int here(){
        String n=location.toLowerCase(Locale.ROOT).replace('é','e');if(n.isEmpty())return -1;
        String[] keys={"outskirt","phenac","pyrite","agate","gateon","hq lab","realgam","mt. battle","kaminko","key lair","snagem","citadark"};
        for(int i=0;i<keys.length;i++)if(n.contains(keys[i]))return i;
        if(n.contains("under"))return 2;
        return -1;
    }
    private void drawMap(Canvas c,float phase){
        RectF m=map;c.save();
        c.clipPath(XdSkin.shape(m,u*3));
        int tint=c.saveLayer(m,fieldPaint);
        // XD's P*DA map: teal contour lines on black over the fine wire grid.
        XdSkin.fill.setColor(0xFF000A0D);c.drawRect(m,XdSkin.fill);
        XdSkin.hexes(c,m,0x12FFFFFF,u,phase);
        Path land=new Path();float L=m.left,T=m.top,Wm=m.width(),Hm=m.height();
        land.moveTo(L-u*4,T+Hm*0.12f);land.cubicTo(L+Wm*0.3f,T-u*4,L+Wm*0.6f,T+Hm*0.04f,L+Wm*0.86f,T-u*2);
        land.lineTo(L+Wm*0.92f,T+Hm*0.3f);land.cubicTo(L+Wm*1.0f,T+Hm*0.5f,L+Wm*0.9f,T+Hm*0.74f,L+Wm*0.97f,T+Hm*1.04f);
        land.lineTo(L-u*4,T+Hm*1.04f);land.close();
        XdSkin.fill.setColor(0x220E8C94);c.drawPath(land,XdSkin.fill);
        XdSkin.stroke.setStrokeWidth(u*1.2f);XdSkin.stroke.setColor(XdSkin.TEAL_HI);c.drawPath(land,XdSkin.stroke);
        for(int i=1;i<=3;i++){
            Matrix mx=new Matrix();float sc=1f-i*0.08f;mx.setScale(sc,sc,L+Wm*0.46f,T+Hm*0.58f);Path ring=new Path();land.transform(mx,ring);
            XdSkin.stroke.setStrokeWidth(u*0.5f);XdSkin.stroke.setColor(XdSkin.alpha(XdSkin.TEAL,0.75f-i*0.18f));c.drawPath(ring,XdSkin.stroke);
        }
        float[][] rocks={{0.44f,0.16f,0.16f,0.07f},{0.70f,0.08f,0.13f,0.06f},{0.18f,0.24f,0.12f,0.06f},{0.64f,0.52f,0.11f,0.07f},{0.30f,0.86f,0.14f,0.05f}};
        for(float[] k:rocks)for(int j=0;j<3;j++){
            float gx=Wm*k[2]*(0.5f-j*0.14f),gy=Hm*k[3]*(0.5f-j*0.14f);r.set(L+Wm*k[0]-gx,T+Hm*k[1]-gy,L+Wm*k[0]+gx,T+Hm*k[1]+gy);
            XdSkin.stroke.setStrokeWidth(u*0.6f);XdSkin.stroke.setColor(XdSkin.alpha(XdSkin.TEAL_HI,0.7f-j*0.2f));c.drawOval(r,XdSkin.stroke);
        }
        // Scooter roads.
        XdSkin.stroke.setStrokeWidth(u*1.3f);XdSkin.stroke.setColor(XdSkin.alpha(XdSkin.TEAL_HI,0.85f));
        for(int[] e:ROADS)c.drawLine(px(e[0]),py(e[0]),px(e[1]),py(e[1]),XdSkin.stroke);
        c.restoreToCount(tint);
        int here=here();
        for(int i=0;i<PLACES.length;i++){
            float cx=px(i),cy=py(i),s=u*6;r.set(cx-s/2,cy-s/2,cx+s/2,cy+s/2);
            XdSkin.fill.setColor(0xFF000A0D);c.drawRect(r,XdSkin.fill);
            XdSkin.stroke.setStrokeWidth(u*0.9f);XdSkin.stroke.setColor(i==here?XdSkin.ACTIVE:XdSkin.TEAL_HI);c.drawRect(r,XdSkin.stroke);
            if(i==here){XdSkin.fill.setColor(XdSkin.ACTIVE);RectF in=new RectF(r);in.inset(u*1.3f,u*1.3f);c.drawRect(in,XdSkin.fill);}
        }
        if(here>=0){
            float cx=px(here),cy=py(here),pulse=motion?(SystemClock.uptimeMillis()%1000)/1000f:0.4f;
            XdSkin.stroke.setStrokeWidth(u*1.2f);XdSkin.stroke.setColor(XdSkin.alpha(XdSkin.ACTIVE,1f-pulse));c.drawCircle(cx,cy,u*(6+8*pulse),XdSkin.stroke);
            // Michael's marker: a round head badge with a direction pip, original.
            float mr=u*7.5f,my=cy-u*13;
            Path pin=new Path();pin.moveTo(cx-u*3.5f,my+mr*0.7f);pin.lineTo(cx,cy-u*3);pin.lineTo(cx+u*3.5f,my+mr*0.7f);pin.close();
            XdSkin.fill.setColor(XdSkin.ACTIVE);c.drawPath(pin,XdSkin.fill);
            XdSkin.fill.setColor(XdSkin.ACTIVE);c.drawCircle(cx,my,mr+u*1.2f,XdSkin.fill);
            XdSkin.fill.setColor(0xFFFFFFFF);c.drawCircle(cx,my,mr,XdSkin.fill);
            XdSkin.fill.setColor(0xFF2C3E78);r.set(cx-mr,my-mr,cx+mr,my+mr);c.drawArc(r,180,180,true,XdSkin.fill);
            XdSkin.fill.setColor(0xFFF2C79A);c.drawCircle(cx,my+u*1.6f,mr*0.5f,XdSkin.fill);
        }
        c.restore();
        XdSkin.stroke.setStrokeWidth(u*0.8f);XdSkin.stroke.setColor(XdSkin.TEAL);c.drawPath(XdSkin.shape(m,u*3),XdSkin.stroke);
        // Place name on a black plate with a teal keyline, in the P*DA's tracked capitals; left out when
        // the game reports no location. Long names shrink, then end in an ellipsis.
        if(!location.isEmpty()){
        String name=location.toUpperCase(java.util.Locale.ROOT);
        float size=u*9.5f,track=0.1f,avail=m.width()-u*26;while(size>u*7&&XdSkin.captionWidth(name,size,track)>avail)size-=u*0.25f;
        while(name.length()>1&&XdSkin.captionWidth(name,size,track)>avail)name=name.substring(0,name.length()-2)+"…";
        float nw=XdSkin.captionWidth(name,size,track);
        r.set(m.left+u*4,m.top+u*4,m.left+u*4+nw+u*14,m.top+u*4+size+u*8);
        XdSkin.fill.setColor(0xE6000A0D);c.drawPath(XdSkin.shape(r,u*2.5f),XdSkin.fill);
        XdSkin.stroke.setStrokeWidth(u*0.8f);XdSkin.stroke.setColor(XdSkin.TEAL);c.drawPath(XdSkin.shape(r,u*2.5f),XdSkin.stroke);
        XdSkin.caption(c,name,r.left+u*7,r.centerY()+size*0.36f,size,0xFFDDF8F6,0xFF001014,u*1f,track,Paint.Align.LEFT);
        }
        // Objective box: only for a milestone the game reports, as its own words, wrapped and ellipsized.
        String text=milestone();if(text.isEmpty())return;
        XdSkin.fill.setColor(0xEB000A0D);c.drawPath(XdSkin.shape(objective,u*3),XdSkin.fill);
        XdSkin.stroke.setStrokeWidth(u*0.8f);XdSkin.stroke.setColor(XdSkin.TEAL);c.drawPath(XdSkin.shape(objective,u*3),XdSkin.stroke);
        float g=u*11,tx=objective.left+u*7+g+u*5;
        XdSkin.glyph(c,"flag",objective.left+u*7,objective.top+u*8,g,0xFFF0463A,u);
        XdSkin.caption(c,"NEXT MILESTONE",tx,objective.top+u*10.5f,u*6.5f,XdSkin.TEAL_HI,0,0,0.14f,Paint.Align.LEFT);
        float y=objective.top+u*21f;
        for(String line:wrap(text,XdSkin.font(XdSkin.BOLD,u*9f,XdSkin.TEXT),objective.right-tx-u*8,3)){c.drawText(line,tx,y,XdSkin.font(XdSkin.BOLD,u*9f,XdSkin.TEXT));y+=u*10.5f;}
    }
    /** The native "Next story milestone: n description" with the prefix and number removed (pass 13 rule). */
    private String milestone(){
        if(!state.objectiveKnown||state.objective.isEmpty()||state.mapName.isEmpty())return "";
        String text=state.objective.replaceFirst("^Next story milestone: ","").replaceFirst("^\\d+\\s+","").trim();
        // The native packet holds at most 95 characters; a string that fills it was cut there.
        return state.objective.length()>=95?text.replaceFirst("\\s*\\S*$","")+"…":text;
    }
    private float px(int i){return map.left+u*10+(map.width()-u*20)*Float.parseFloat(PLACES[i][1]);}
    private float py(int i){return map.top+u*30+(map.height()-u*76)*Float.parseFloat(PLACES[i][2]);}
    private static ArrayList<String> wrap(String text,Paint p,float width,int lines){
        ArrayList<String> out=new ArrayList<>();String[] words=text.split(" ");StringBuilder line=new StringBuilder();
        for(int i=0;i<words.length;i++){
            String next=line.length()==0?words[i]:line+" "+words[i];
            if(p.measureText(next)<=width||line.length()==0){line=new StringBuilder(next);continue;}
            if(out.size()==lines-1){out.add(XdSkin.fit(line+" "+String.join(" ",java.util.Arrays.copyOfRange(words,i,words.length)),p,width));return out;}
            out.add(line.toString());line=new StringBuilder(words[i]);
        }
        if(line.length()>0)out.add(XdSkin.fit(line.toString(),p,width));
        return out;
    }

}
