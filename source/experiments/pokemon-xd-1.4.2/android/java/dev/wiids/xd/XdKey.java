package dev.wiids.xd;

import android.content.Context;
import android.graphics.*;
import android.os.SystemClock;
import android.widget.Button;

/**
 * A Button that draws itself in the XD skin. The Button text stays the engineering label (tests and
 * accessibility read it); the drawing reads the bound data. Enabled state, listeners and identity
 * belong to the caller and are never changed here.
 */
final class XdKey extends Button {
    static final int MOVE=1,COMMAND=2,PARTY=3,LIST=4,ROUND=5,TILE=6,SETTING=7,UTILITY=8,APP=9,TARGET=10;
    /** Where XdBoard places the key. */
    static final int AT_GRID=1,AT_STRIP=2,AT_LIST=3,AT_DOCK_LEFT=4,AT_DOCK_RIGHT=5,AT_HIDDEN=6,AT_HERO=7,AT_APP=8,AT_PANEL=9,AT_POCKET=10;
    /** Party drawing: a Gen 6 switch tile, or a mini card for the P*DA and Bag columns. */
    static final int TILE_STYLE=0,MINI_STYLE=1;
    final int kind;
    int place,slot,color=XdSkin.UTILITY,style=TILE_STYLE;
    String glyph="",title="";
    XdState.Move move;
    XdState.Pokemon mon;
    /** Target panels: ally side, and the native HP of the highlighted target when this panel is it. */
    boolean ally;
    XdState.Pokemon targetMon;
    boolean acting,chosen,pulse;
    float u=1;
    private long pressedAt,releasedAt;
    private boolean down;
    private final RectF r=new RectF(),inner=new RectF();

    XdKey(Context c,int kind){
        super(c);this.kind=kind;setAllCaps(false);setBackground(null);setStateListAnimator(null);setWillNotDraw(false);
    }
    XdKey look(int color,String glyph,String title){this.color=color;this.glyph=glyph;this.title=title;invalidate();return this;}
    void bindMove(XdState.Move m){move=m;invalidate();}
    void bindMon(XdState.Pokemon p,boolean acting,boolean chosen){mon=p;this.acting=acting;this.chosen=chosen;invalidate();}
    void choose(){chosen=true;invalidate();}

    @Override public void setPressed(boolean pressed){
        if(pressed!=down){down=pressed;long now=SystemClock.uptimeMillis();if(pressed)pressedAt=now;else releasedAt=now;postInvalidateOnAnimation();}
        super.setPressed(pressed);
    }
    /** 60 ms squash to 95%, then a 180 ms release with a small overshoot. */
    private float scale(){
        long now=SystemClock.uptimeMillis();
        if(down){float t=Math.min(1f,(now-pressedAt)/60f);if(t<1)postInvalidateOnAnimation();return 1f-0.05f*t;}
        float t=(now-releasedAt)/180f;if(releasedAt==0||t>=1)return 1f;postInvalidateOnAnimation();
        float s=1.7f,k=t-1;return 0.95f+0.05f*(k*k*((s+1)*k+s)+1);
    }
    private boolean live(){return isEnabled()||chosen;}

    @Override protected void onDraw(Canvas c){
        float w=getWidth(),h=getHeight();if(w<=0||h<=0)return;
        float s=scale();c.save();c.scale(s,s,w/2,h/2);
        // Disabled reads at about 40% like the 3DS games; a just-chosen key keeps full strength.
        boolean dim=!live()&&!(kind==PARTY&&style==MINI_STYLE);
        if(dim)c.saveLayerAlpha(-w,-h,w*2,h*2,102);
        float radius;
        switch(kind){
            case MOVE:radius=drawMove(c,w,h);break;
            case PARTY:radius=style==MINI_STYLE?drawMini(c,w,h):drawTile(c,w,h);break;
            case TILE:radius=drawMenuTile(c,w,h);break;
            case SETTING:case LIST:radius=drawRow(c,w,h);break;
            case ROUND:radius=drawRound(c,w,h);break;
            case UTILITY:radius=drawUtility(c,w,h);break;
            case APP:radius=drawApp(c,w,h);break;
            case TARGET:radius=drawTarget(c,w,h);break;
            default:radius=drawCapsule(c,w,h);
        }
        if(dim)c.restore();
        if(down&&isEnabled()){XdSkin.fill.setColor(0x2E000000);c.drawPath(XdSkin.shape(r,radius),XdSkin.fill);}
        long since=SystemClock.uptimeMillis()-releasedAt;
        if(!down&&releasedAt!=0&&since<90){XdSkin.fill.setColor(XdSkin.alpha(0x66FFFFFF,1f-since/90f));c.drawPath(XdSkin.shape(r,radius),XdSkin.fill);postInvalidateOnAnimation();}
        if(chosen||isFocused()){
            float a=0.65f+0.35f*(float)Math.abs(Math.cos(SystemClock.uptimeMillis()/1000.0*Math.PI));
            // Battle keys use XD's white rounded cursor; every other screen keeps the warm 3DS rim.
            if(kind==MOVE||kind==COMMAND||kind==TARGET)XdSkin.cursor(c,r,radius,a,u);else XdSkin.focus(c,r,radius,a,u);
            postInvalidateDelayed(50);
        }else if(acting&&kind==PARTY&&style==TILE_STYLE)XdSkin.ring(c,r,radius,XdSkin.FOCUS,u*1.3f);
        c.restore();
    }

    // Moves as XD metal battle plates: brushed steel (Cipher purple for Shadow moves) around a type-colour
    // inlay, the type emblem in a porthole cap, XD's type pill, the name, PP in a black well and the
    // games' match wording. Shadow moves breathe a purple aura.
    private float drawMove(Canvas c,float w,float h){
        boolean empty=move==null||move.id==0;
        float radius=u*5;r.set(u*1,u*1,w-u*1,h-u*2);
        boolean shadow=!empty&&"SHADOW".equals(XdSkin.type(move.typeName));
        int base=empty?0xFF4A4C54:XdSkin.moveColor(move.typeName);
        if(shadow&&live()){XdSkin.aura(c,r,radius,u);if(XdSkin.motion()&&isShown())postInvalidateDelayed(100);}
        XdSkin.steel(c,r,radius,shadow?XdSkin.CIPHER:0,u);
        RectF in=new RectF(r.left+u*4,r.top+u*4,r.right-u*4,r.bottom-u*4);XdSkin.inlay(c,in,u*3,base,u);
        if(empty){XdSkin.xdLabel(c,"—",in.centerX(),in.centerY(),u*12,u*8,in.width(),0,u*1.4f,Paint.Align.CENTER);return radius;}
        // Type emblem in a porthole at the top-left.
        float d=Math.min(in.height()*0.46f,u*34),cx=in.left+u*3+d/2,cy=in.top+u*3+d/2;
        XdSkin.porthole(c,cx,cy,d,shadow?XdSkin.CIPHER:0,u);
        int tc=shadow?0xFFBD6ACD:XdSkin.typeColor(move.typeName);
        XdSkin.fill.setColor(0xFF000000);XdSkin.fill.setShader(new RadialGradient(cx-d*0.12f,cy-d*0.14f,d*0.42f,XdSkin.mix(tc,0xFFFFFFFF,0.3f),XdSkin.shade(tc,0.62f),Shader.TileMode.CLAMP));
        c.drawCircle(cx,cy,d/2-u*2.4f,XdSkin.fill);XdSkin.fill.setShader(null);
        float gs=d*0.46f;String type=XdSkin.type(move.typeName);
        c.save();c.translate(0,u*0.6f);XdSkin.typeGlyph(c,type,cx-gs/2,cy-gs/2,gs,0x99000000,u);c.restore();
        XdSkin.typeGlyph(c,type,cx-gs/2,cy-gs/2,gs,0xFFFFFFFF,u);
        // Type pill and name beside the porthole.
        float x0=cx+d/2+u*5,x1=in.right-u*5;
        String tag=XdSkin.typeTag(move.typeName);float th=u*10;
        XdSkin.pill(c,new RectF(x0,in.top+u*4,x0+XdSkin.pillWidth(tag,th),in.top+u*4+th),tc,tag,u);
        String name=move.name.isEmpty()?"Move "+move.id:move.name;
        float size=XdSkin.fitSize(name,XdSkin.HEAVY,Math.min(u*14,in.height()*0.2f),u*8,x1-x0);
        int edge=XdSkin.shade(base,0.28f);
        XdSkin.outlined(c,XdSkin.fit(name,XdSkin.font(XdSkin.HEAVY,size,0),x1-x0),x0,in.top+u*4+th+u*3+size*0.82f,XdSkin.HEAVY,size,XdSkin.TEXT,edge,u*1.8f,Paint.Align.LEFT);
        // PP in a black well, bottom right; colour-coded half yellow, quarter orange, empty red.
        // Shadow moves have no PP in XD and show dashes (pass 13 rule); other unknown PP stays explicit.
        String pp=shadow?"—/—":move.pp<0||move.maxPp<=0||move.pp>move.maxPp?"?/?":move.pp+"/"+move.maxPp;
        int ppColor=move.pp<0||move.maxPp<=0?XdSkin.TEXT:move.pp==0?0xFFFF5A4A:move.pp*4<=move.maxPp?0xFFFFA23A:move.pp*2<=move.maxPp?0xFFFFE04A:XdSkin.TEXT;
        float wh=u*13,by=in.bottom-u*4;float ppw=0;
        if(!pp.isEmpty()){
            Paint pf=XdSkin.font(XdSkin.HEAVY,u*9,0);float tw=pf.measureText(pp);Paint lf=XdSkin.font(XdSkin.HEAVY,u*6.5f,0);float lw=lf.measureText("PP");
            ppw=tw+lw+u*12;RectF well=new RectF(in.right-u*4-ppw,by-wh,in.right-u*4,by);XdSkin.well(c,well,u*2.5f,u);
            XdSkin.outlined(c,"PP",well.left+u*4,well.centerY()+u*2.3f,XdSkin.HEAVY,u*6.5f,0xFFFF6A3D,0,0,Paint.Align.LEFT);
            XdSkin.outlined(c,pp,well.right-u*4,well.centerY()+u*3.2f,XdSkin.HEAVY,u*9,ppColor,0,0,Paint.Align.RIGHT);
        }
        // Type match from native data, worded like the games; never a damage or success promise.
        String words=matchWords(move);
        if(!words.isEmpty()){
            Paint mp=XdSkin.font(XdSkin.HEAVY,u*7,0);float mw=Math.min(mp.measureText(words)+u*8,in.width()-ppw-u*14);
            RectF tagR=new RectF(in.left+u*4,by-u*11,in.left+u*4+mw,by-u*1);
            XdSkin.fill.setColor(0xB3000000);c.drawPath(XdSkin.shape(tagR,u*2),XdSkin.fill);
            XdSkin.outlined(c,XdSkin.fit(words,mp,mw-u*6),tagR.left+u*4,tagR.centerY()+u*2.5f,XdSkin.HEAVY,u*7,matchTone(move),0,0,Paint.Align.LEFT);
        }
        return radius;
    }
    /** The games' own wording. Status moves and unknown matches show no tag. */
    static String matchWords(XdState.Move m){
        if("SHADOW".equals(XdSkin.type(m.typeName))&&m.effectivenessInfo<3)return "Super effective";
        switch(m.effectivenessInfo){
            case 2:return "";case 3:return "Blocked by Ability";case 4:return "Already affected";
            case 5:return "No effect";case 6:return "Blocked";case 7:return "Blocked by Uproar";
            case 8:return "Not allowed";default:break;
        }
        int n=m.effectivenessNumerator,d=m.effectivenessDenominator;
        if(n<0||d<=0)return "";
        if(n==0)return "No effect";
        return n>d?"Super effective":n<d?"Not very effective":"Effective";
    }
    private static int matchTone(XdState.Move m){
        String w=matchWords(m);
        switch(w){case "Super effective":return 0xFFFFE04A;case "Not very effective":return 0xFFB8C8E8;case "Effective":return 0xFFFFFFFF;default:return 0xFFFFB0A8;}
    }

    // Battle commands as XD machine plates in the 3DS colour code: a brushed-steel frame around a
    // colour inlay, the icon in a porthole cap, the caption in XD's wide tracked capitals with a navy
    // outline. Fight is the hero: chrome frame, rivets, a large emblem porthole and a slow glint. Snag
    // Ball carries the Snag Machine arm and, when a snag is possible, a Shadow aura.
    private float drawCapsule(Canvas c,float w,float h){
        boolean hero=place==AT_HERO,snag=color==XdSkin.SNAG;
        r.set(u*1,u*1,w-u*1,h-u*2);float radius=hero?u*7:u*5;
        if(snag&&isEnabled()){XdSkin.aura(c,r,radius,u);if(XdSkin.motion()&&isShown())postInvalidateDelayed(100);}
        XdSkin.steel(c,r,radius,0,u);
        String label=(title.isEmpty()?String.valueOf(getText()):title).toUpperCase(java.util.Locale.ROOT);
        if(hero){
            RectF in=new RectF(r.left+u*7,r.top+u*7,r.right-u*7,r.bottom-u*7);XdSkin.inlay(c,in,u*4,color,u);
            float k=u*3.6f,rv=u*2;
            XdSkin.rivet(c,r.left+u*14,r.top+k,rv);XdSkin.rivet(c,r.right-u*14,r.top+k,rv);XdSkin.rivet(c,r.left+u*14,r.bottom-k,rv);XdSkin.rivet(c,r.right-u*14,r.bottom-k,rv);
            XdSkin.rivet(c,r.left+k,r.centerY(),rv);XdSkin.rivet(c,r.right-k,r.centerY(),rv);
            // Emblem porthole on the right, with the crossed strikes.
            float d=Math.min(in.height()-u*14,u*76),cx=in.right-u*10-d/2,cy=in.centerY();
            XdSkin.porthole(c,cx,cy,d,0,u);
            XdSkin.fill.setColor(0xFF000000);XdSkin.fill.setShader(new RadialGradient(cx-d*0.12f,cy-d*0.15f,d*0.45f,0xFFFF8A6A,0xFF8E1A12,Shader.TileMode.CLAMP));
            c.drawCircle(cx,cy,d/2-u*3,XdSkin.fill);XdSkin.fill.setShader(null);
            float g=d*0.5f;c.save();c.translate(0,u*0.8f);XdSkin.glyph(c,"fight",cx-g/2,cy-g/2,g,0x99000000,u);c.restore();XdSkin.glyph(c,"fight",cx-g/2,cy-g/2,g,0xFFFFFFFF,u);
            // Caption, with a thin rule and the arena subtitle under it.
            float lx=in.left+(cx-d/2-in.left)/2,avail=cx-d/2-in.left-u*16;
            XdSkin.xdLabel(c,label,lx,in.centerY()-u*4,Math.min(u*30,in.height()*0.28f),u*14,avail,0.16f,u*2.6f,Paint.Align.CENTER);
            XdSkin.fill.setColor(0x66FFFFFF);c.drawRect(lx-avail*0.32f,in.centerY()+u*12,lx+avail*0.32f,in.centerY()+u*12.7f,XdSkin.fill);
            XdSkin.glint(c,in,u*4,3.4f,0,u);if(XdSkin.motion()&&isShown())postInvalidateDelayed(100);
            return radius;
        }
        RectF in=new RectF(r.left+u*4,r.top+u*4,r.right-u*4,r.bottom-u*4);XdSkin.inlay(c,in,u*3,color,u);
        if(snag){
            // The Snag Machine arm reaching across the inlay, in a faint plate tone.
            float s=in.height()*1.1f;XdSkin.snagArm(c,in.right-s*0.86f,in.top-s*0.08f,s,0x33FFFFFF,u);
        }
        boolean stack=in.width()<u*70;
        if(stack){
            float g=Math.min(in.height()*0.44f,u*16),total=g+u*2+u*9,top=in.centerY()-total/2;
            iconDisc(c,in.centerX()-g/2,top,g,color);
            XdSkin.xdLabel(c,label,in.centerX(),top+g+u*2+u*4.5f,u*9,u*6,in.width()-u*6,0.1f,u*1.5f,Paint.Align.CENTER);
            return radius;
        }
        float d=Math.min(in.height()-u*6,u*34),cx=in.left+u*3+d/2,cy=in.centerY();
        XdSkin.porthole(c,cx,cy,d,0,u);
        XdSkin.fill.setColor(0xFF000000);XdSkin.fill.setShader(new RadialGradient(cx-d*0.12f,cy-d*0.15f,d*0.45f,XdSkin.mix(color,0xFFFFFFFF,0.3f),XdSkin.shade(color,0.55f),Shader.TileMode.CLAMP));
        c.drawCircle(cx,cy,d/2-u*2.4f,XdSkin.fill);XdSkin.fill.setShader(null);
        float g=d*0.5f;
        if(snag)XdSkin.snagArm(c,cx-g*0.62f,cy-g*0.62f,g*1.24f,0xFFFFFFFF,u);else iconDisc(c,cx-g/2,cy-g/2,g,color);
        float lx=cx+d/2+(in.right-cx-d/2)/2;
        XdSkin.xdLabel(c,label,lx,cy,u*11,u*6.5f,in.right-cx-d/2-u*8,0.1f,u*1.7f,Paint.Align.CENTER);
        return radius;
    }
    /** Command icon: the glyph in white with a dark keyline, so it reads on yellow as well as blue. */
    private void iconDisc(Canvas c,float x,float y,float g,int base){
        if(glyph.isEmpty())return;
        if(glyph.equals("party")||glyph.equals("snag")){XdSkin.glyph(c,glyph,x,y,g,glyph.equals("snag")?0xFF1C8E8A:0xFFE2463A,u);return;}
        c.save();c.translate(0,u*0.6f);XdSkin.glyph(c,glyph,x,y,g,XdSkin.shade(base,0.4f),u);c.restore();
        XdSkin.glyph(c,glyph,x,y,g,0xFFFFFFFF,u);
    }

    // Party: XD party plate laid out as a 3DS switch tile. Brushed steel (Cipher purple for Shadow,
    // maroon when fainted), the icon in a porthole cap at the left, name and HP in a black inset.
    private float drawTile(Canvas c,float w,float h){
        r.set(u*1,u*1,w-u*1,h-u*2);float radius=u*4;
        boolean empty=mon==null||mon.species==0;
        if(empty){
            XdSkin.fill.setColor(0x33000000);c.drawPath(XdSkin.shape(r,radius),XdSkin.fill);
            XdSkin.stroke.setStrokeWidth(u*0.6f);XdSkin.stroke.setColor(0x33FFFFFF);c.drawPath(XdSkin.shape(r,radius),XdSkin.stroke);
            return radius;
        }
        boolean shadow=mon.shadowId!=0,fainted=mon.maxHp>0&&mon.hp==0;
        int tint=shadow?XdSkin.CIPHER:fainted?XdSkin.MAROON:0;
        if(shadow)XdSkin.ring(c,r,radius,XdSkin.alpha(XdSkin.SHADOW_AURA,0.9f),u*1.6f);
        XdSkin.steel(c,r,radius,tint,u);
        float pad=u*4,d=Math.min(r.height()-u*8,u*36),cx=r.left+pad+d/2,cy=r.centerY();
        XdSkin.porthole(c,cx,cy,d,tint,u);XdSkin.monSlot(c,cx,cy,d-u*8,shadow,fainted,u);
        RectF well=new RectF(cx+d/2+u*3,r.top+u*4,r.right-u*4,r.bottom-u*4);XdSkin.well(c,well,u*2.5f,u);
        float x0=well.left+u*5,x1=well.right-u*5;
        String status=XdSkin.statusLabel(mon.status,mon.hp,mon.maxHp);float bh=u*9,bw=XdSkin.badgeWidth(status,bh);
        String name=mon.name.isEmpty()?"Pokémon":mon.name;
        float nameW=x1-x0-(bw>0?bw+u*4:0),size=XdSkin.fitSize(name,XdSkin.HEAVY,u*11.5f,u*7,nameW);
        float top=well.top+u*3;
        XdSkin.outlined(c,name,x0,top+size*0.82f,XdSkin.HEAVY,size,XdSkin.TEXT,0xFF000000,u*1.2f,Paint.Align.LEFT);
        XdSkin.badge(c,status,x1-bw,top+u*0.5f,bh,u);
        float barTop=well.top+well.height()*0.48f;inner.set(x0,barTop,x1,barTop+u*4);XdSkin.hpBar(c,new RectF(inner),mon.hp,mon.maxHp,true,u);
        String numbers=mon.maxHp>0?mon.hp+"/"+mon.maxHp:"?/?";
        float ny=Math.min(well.bottom-u*3,barTop+u*14);
        XdSkin.outlined(c,numbers,x1,ny,XdSkin.HEAVY,u*9,XdSkin.TEXT,0xFF000000,u*1.1f,Paint.Align.RIGHT);
        if(shadow){float nw=XdSkin.font(XdSkin.HEAVY,u*9,0).measureText(numbers);inner.set(x0,ny-u*5,Math.min(x0+u*48,x1-nw-u*6),ny-u*2.2f);XdSkin.heartGauge(c,new RectF(inner),mon.heart,mon.heartMax,u);}
        return radius;
    }
    // Party: mini XD party plate for the P*DA sidebar and the Bag column: porthole icon, black inset.
    private float drawMini(Canvas c,float w,float h){
        r.set(u*0.5f,u*0.5f,w-u*0.5f,h-u*1.2f);float radius=u*3;
        boolean empty=mon==null||mon.species==0;
        if(empty){XdSkin.fill.setColor(0x40000000);c.drawPath(XdSkin.shape(r,radius),XdSkin.fill);
            XdSkin.stroke.setStrokeWidth(u*0.5f);XdSkin.stroke.setColor(0x33FFFFFF);c.drawPath(XdSkin.shape(r,radius),XdSkin.stroke);return radius;}
        boolean shadow=mon.shadowId!=0,fainted=mon.maxHp>0&&mon.hp==0,on=isEnabled()||chosen;
        int tint=shadow?XdSkin.CIPHER:fainted?XdSkin.MAROON:0;
        if(shadow)XdSkin.ring(c,r,radius,XdSkin.alpha(XdSkin.SHADOW_AURA,0.8f),u*1f);
        XdSkin.steel(c,r,radius,tint,u);
        float d=r.height()-u*2,cx=r.left+u*1+d/2,cy=r.centerY();
        XdSkin.porthole(c,cx,cy,d,tint,u);XdSkin.monSlot(c,cx,cy,d-u*6,shadow,fainted,u);
        RectF well=new RectF(cx+d/2+u*2,r.top+u*2.5f,r.right-u*2.5f,r.bottom-u*2.5f);XdSkin.well(c,well,u*2,u);
        float x0=well.left+u*3,x1=well.right-u*3;
        String status=XdSkin.statusLabel(mon.status,mon.hp,mon.maxHp);float bh=u*7,bw=XdSkin.badgeWidth(status,bh);
        String name=mon.name.isEmpty()?"Pokémon":mon.name;
        float size=XdSkin.fitSize(name,XdSkin.HEAVY,u*8,u*6,x1-x0-(bw>0?bw+u*3:0));
        float lines=shadow?u*18:u*14.5f,top=well.centerY()-lines/2;
        XdSkin.outlined(c,name,x0,top+size*0.8f,XdSkin.HEAVY,size,fainted?XdSkin.TEXT_2:XdSkin.TEXT,0xFF000000,u*1f,Paint.Align.LEFT);
        XdSkin.badge(c,status,x1-bw,top-u*0.3f,bh,u);
        inner.set(x0,top+u*10,x1,top+u*12.6f);XdSkin.hpBar(c,new RectF(inner),mon.hp,mon.maxHp,true,u);
        if(shadow){inner.set(x0+u*7,top+u*15.2f,x1,top+u*17.4f);XdSkin.heartGauge(c,new RectF(inner),mon.heart,mon.heartMax,u);}
        if(!on){XdSkin.fill.setColor(0x73000000);c.drawPath(XdSkin.shape(r,radius),XdSkin.fill);}
        return radius;
    }

    // Target panels: XD battle plates. Foes in maroon steel, allies in silver, Shadow foes in Cipher
    // purple; the icon sits in a porthole cap at the right end, as on XD's battle plates.
    private float drawTarget(Canvas c,float w,float h){
        r.set(u*1,u*1,w-u*1,h-u*2);float radius=u*4.5f;
        XdState.Pokemon t=targetMon;boolean shadow=t!=null&&t.species!=0&&t.shadowId!=0;
        int tint=shadow?XdSkin.CIPHER:ally?0:XdSkin.MAROON;
        if(shadow)XdSkin.ring(c,r,radius,XdSkin.alpha(XdSkin.SHADOW_AURA,0.9f),u*1.6f);
        XdSkin.steel(c,r,radius,tint,u);
        XdSkin.rivet(c,r.left+u*3.2f,r.top+u*10,u*1.6f);XdSkin.rivet(c,r.left+u*3.2f,r.bottom-u*10,u*1.6f);
        float d=Math.min(r.height()-u*10,u*40),cx=r.right-u*5-d/2,cy=r.centerY();
        XdSkin.porthole(c,cx,cy,d,tint,u);XdSkin.monSlot(c,cx,cy,d-u*9,shadow,false,u);
        RectF well=new RectF(r.left+u*5,r.top+u*5,cx-d/2-u*4,r.bottom-u*5);XdSkin.well(c,well,u*2.5f,u);
        float x0=well.left+u*6,x1=well.right-u*6;
        XdSkin.caption(c,ally?"ALLY":"FOE",x0,well.top+u*9,u*6.5f,ally?0xFF9FD4FF:0xFFFF9A8A,0,0,0.12f,Paint.Align.LEFT);
        String name=String.valueOf(getText());float size=XdSkin.fitSize(name,XdSkin.HEAVY,u*13,u*8,x1-x0);
        boolean bar=t!=null&&t.species!=0;
        float ny=bar?well.centerY()+u*2:well.centerY()+size*0.5f;
        XdSkin.outlined(c,name,x0,ny,XdSkin.HEAVY,size,XdSkin.TEXT,0xFF000000,u*1.2f,Paint.Align.LEFT);
        if(bar){
            inner.set(x0,ny+u*4,x1,ny+u*8);XdSkin.hpBar(c,new RectF(inner),t.hp,t.maxHp,true,u);
            if(shadow){inner.set(x0+u*14,ny+u*11,Math.min(x1,x0+u*70),ny+u*14.5f);XdSkin.heartGauge(c,new RectF(inner),t.heart,t.heartMax,u);}
        }
        return radius;
    }

    // List rows are P*DA hologram rows (black, teal wire keyline, tracked cyan caption, green when
    // chosen); setting rows are XD's teal glass bars with the value in a navy bar.
    private float drawRow(Canvas c,float w,float h){
        r.set(u*1,u*1,w-u*1,h-u*2);float radius=u*3.5f;
        boolean hot=chosen&&kind==LIST;
        String label=String.valueOf(getText()),value="";
        if(!title.isEmpty())label=title;
        if(kind==LIST){
            Path p=XdSkin.shape(r,radius);
            XdSkin.fill.setColor(0xE6021317);c.drawPath(p,XdSkin.fill);
            c.save();c.clipPath(p);XdSkin.fill.setColor(0x1F35D6D0);c.drawRect(r.left,r.top,r.right,r.top+r.height()*0.45f,XdSkin.fill);c.restore();
            XdSkin.stroke.setStrokeWidth(u*0.9f);XdSkin.stroke.setColor(hot?XdSkin.ACTIVE:XdSkin.TEAL);c.drawPath(p,XdSkin.stroke);
            float x=r.left+u*9,size=Math.min(u*9.5f,r.height()*0.34f);
            if(!glyph.isEmpty()){
                float g=r.height()-u*10;float cx=r.left+u*5+g/2;RectF chip=new RectF(cx-g/2,r.centerY()-g/2,cx+g/2,r.centerY()+g/2);
                XdSkin.fill.setColor(XdSkin.alpha(color,0.55f));c.drawPath(XdSkin.shape(chip,u*2f),XdSkin.fill);
                XdSkin.stroke.setStrokeWidth(u*0.7f);XdSkin.stroke.setColor(XdSkin.TEAL_HI);c.drawPath(XdSkin.shape(chip,u*2f),XdSkin.stroke);
                float gs=g*0.56f;XdSkin.glyph(c,glyph,cx-gs/2,r.centerY()-gs/2,gs,0xFFE6FFFC,u);x=cx+g/2+u*7;
            }
            XdSkin.caption(c,XdSkin.fit(label,XdSkin.font(XdSkin.WIDE,size,0),r.right-x-u*14),x,r.centerY()+size*0.36f,size,hot?XdSkin.ACTIVE:0xFFDDF8F6,0xFF001014,u*1f,0.06f,Paint.Align.LEFT);
            // A small chevron at the right end, as on the P*DA's menu rows.
            float cxv=r.right-u*9,cyv=r.centerY();Path v=new Path();v.moveTo(cxv-u*2,cyv-u*3.5f);v.lineTo(cxv+u*1.5f,cyv);v.lineTo(cxv-u*2,cyv+u*3.5f);
            XdSkin.stroke.setStrokeWidth(u*1.1f);XdSkin.stroke.setColor(hot?XdSkin.ACTIVE:XdSkin.TEAL_HI);c.drawPath(v,XdSkin.stroke);
            return radius;
        }
        XdSkin.cream(c,r,radius,false,u);
        int split=label.lastIndexOf(": ");
        if(split>0){value=label.substring(split+2);label=label.substring(0,split);}
        float pad=u*9,size=Math.min(u*10,r.height()*0.36f),x=r.left+pad,vw=0;
        if(!value.isEmpty()){
            Paint vp=XdSkin.font(XdSkin.HEAVY,size*0.92f,0);vw=Math.max(u*60,vp.measureText(value)+u*16);
            inner.set(r.right-u*4-vw,r.top+u*4,r.right-u*4,r.bottom-u*4);
            XdSkin.well(c,new RectF(inner),u*2.5f,u);
            XdSkin.outlined(c,value,inner.right-u*8,inner.centerY()+size*0.33f,XdSkin.HEAVY,size*0.92f,0xFFFFD45A,0,0,Paint.Align.RIGHT);
        }
        Paint lp=XdSkin.font(XdSkin.BOLD,size,XdSkin.CREAM_INK);c.drawText(XdSkin.fit(label,lp,r.right-x-pad-vw),x,r.centerY()+size*0.36f,lp);
        return radius;
    }
    // Round dock buttons: blue U-turn Back, red close, orange confirm, grey utilities.
    private float drawRound(Canvas c,float w,float h){
        float d=Math.min(w,h)-u*3;r.set((w-d)/2,(h-d)/2-u*0.5f,(w+d)/2,(h+d)/2-u*0.5f);
        XdSkin.gloss(c,r,d/2,color,u);
        float g=d*0.5f;boolean grey=color==XdSkin.UTILITY;
        int ink=grey?0xFF3A404C:0xFFFFFFFF;
        if(!grey){c.save();c.translate(0,u*0.8f);XdSkin.glyph(c,glyph,r.centerX()-g/2,r.centerY()-g/2,g,XdSkin.shade(color,0.4f),u);c.restore();}
        XdSkin.glyph(c,glyph,r.centerX()-g/2,r.centerY()-g/2,g,ink,u);
        return d/2;
    }
    /** Grey utility circle with its label beside it, for WiiDS functions in the dock. */
    private float drawUtility(Canvas c,float w,float h){
        float d=h-u*3;r.set(0,0,w,h);
        RectF circle=new RectF(u*1.5f,u*1,u*1.5f+d,u*1+d);XdSkin.gloss(c,circle,d/2,XdSkin.UTILITY,u);
        float g=d*0.5f;XdSkin.glyph(c,glyph,circle.centerX()-g/2,circle.centerY()-g/2,g,0xFF3A404C,u);
        String label=title.isEmpty()?String.valueOf(getText()):title;float size=Math.min(u*9,h*0.34f);
        XdSkin.outlined(c,XdSkin.fit(label,XdSkin.font(XdSkin.HEAVY,size,0),w-d-u*10),circle.right+u*5,h/2+size*0.36f,XdSkin.HEAVY,size,XdSkin.TEXT,0xFF10131A,u*1.4f,Paint.Align.LEFT);
        r.set(circle);return d/2;
    }
    // P*DA dock apps: brushed silver plates with a coloured emblem and dark caption.
    private float drawApp(Canvas c,float w,float h){
        r.set(u*1,u*1,w-u*1,h-u*2);float radius=u*4;
        XdSkin.steel(c,r,radius,0,u);
        float g=r.height()*0.54f,size=Math.min(u*10,r.height()*0.32f);
        String label=title.isEmpty()?String.valueOf(getText()):title;
        Paint p=XdSkin.font(XdSkin.HEAVY,size,0);float tw=p.measureText(label);
        float x=r.centerX()-(g+u*6+tw)/2,gy=r.centerY()-g/2;
        inner.set(x-u*2,gy-u*2,x+g+u*2,gy+g+u*2);XdSkin.gloss(c,new RectF(inner),u*2.5f,color,u);
        XdSkin.glyph(c,glyph,x+g*0.12f,gy+g*0.12f,g*0.76f,glyph.equals("party")?0xFFE2463A:0xFFFFFFFF,u);
        XdSkin.outlined(c,label,x+g+u*6,r.centerY()+size*0.36f,XdSkin.HEAVY,size,0xFF141414,0x66FFFFFF,u*1f,Paint.Align.LEFT);
        return radius;
    }
    // WiiDS menu tiles, XD menu-card style: brushed silver tile with corner rivets, the icon in a
    // chrome-rimmed coloured disc, the caption in a black inset in XD's tracked capitals.
    private float drawMenuTile(Canvas c,float w,float h){
        r.set(u*1,u*1,w-u*1,h-u*2);float radius=u*5;
        XdSkin.steel(c,r,radius,0,u);
        float k=u*5.5f,rv=u*1.5f;XdSkin.rivet(c,r.left+k,r.top+k,rv);XdSkin.rivet(c,r.right-k,r.top+k,rv);XdSkin.rivet(c,r.left+k,r.bottom-k,rv);XdSkin.rivet(c,r.right-k,r.bottom-k,rv);
        float d=Math.min(r.height()-u*14,u*30),cx=r.left+u*11+d/2,cy=r.centerY();
        RectF disc=new RectF(cx-d/2,cy-d/2,cx+d/2,cy+d/2);XdSkin.gloss(c,disc,d/2,color,u);
        float g=d*0.54f;XdSkin.glyph(c,glyph,cx-g/2,cy-g/2,g,0xFFFFFFFF,u);
        RectF well=new RectF(disc.right+u*6,cy-u*11,r.right-u*12,cy+u*11);XdSkin.well(c,well,u*2.5f,u);
        String label=String.valueOf(getText()).toUpperCase(java.util.Locale.ROOT);float size=Math.min(u*10,well.height()*0.46f);
        float track=0.1f;while(size>u*6&&XdSkin.captionWidth(label,size,track)>well.width()-u*12)size-=u*0.25f;
        XdSkin.caption(c,label,well.left+u*7,well.centerY()+size*0.36f,size,XdSkin.TEXT,0xFF14203A,u*1.2f,track,Paint.Align.LEFT);
        return radius;
    }
}
