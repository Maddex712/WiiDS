package dev.wiids.xd;

import android.graphics.*;
import java.util.Locale;

/**
 * Drawn-in-code skin for the XD companion and WiiDS menu: Gen 6 (X/Y, ORAS) 3DS layouts dressed in
 * Pokemon XD's own materials, studied from the game and redrawn here: brushed-steel plates with
 * porthole caps, chrome-rimmed buttons with rivets, XD's pastel type and status pills with black
 * caps, Cipher purple for Shadow data, and the P*DA as a black hologram screen with teal wire lines.
 * No game, Nintendo or Pokemon artwork is loaded or embedded: every shape is Canvas code.
 * Sizes are in design units: one unit is 1/320 of the panel width, as on the 3DS lower screen.
 */
final class XdSkin {
    private XdSkin(){}
    // P*DA: gunmetal bezel around a black hologram screen with teal wire lines and green active text.
    // SAND_TOP/SAND_LOW keep their names for callers: the P*DA mark and app colour, now XD teal.
    static final int SAND_TOP=0xFF35D6D0,SAND_LOW=0xFF0E7F86,SAND_EDGE=0xFF2C2E33,SAND_RIM=0xFF0B0C0E,
            SCREEN=0xFF03161B,SCREEN_DEEP=0xFF000507,TAB=0xFF07090B,DOCK=0xFF16181C,
            TEAL=0xFF0E8C94,TEAL_HI=0xFF5FE6DE,TEAL_DIM=0xFF0A4446,ACTIVE=0xFF39E549;
    // Brushed steel (battle and party plates), Cipher purple and maroon steel, and the black inset.
    static final int STEEL_HI=0xFFC4C4C4,STEEL=0xFF8E8E8E,STEEL_LOW=0xFF5C5C5C,STEEL_DEEP=0xFF2E2E2E,INSET=0xFF181818,
            CIPHER=0xFF6A3A86,CIPHER_LOW=0xFF3C1E4E,MAROON=0xFF7A1450,MAROON_LOW=0xFF4C0D31;
    // Battle backdrop: Cipher night, deep purple to near black.
    static final int NIGHT_TOP=0xFF35264F,NIGHT_LOW=0xFF120D1E;
    // Text and the dark translucent message box.
    static final int TEXT=0xFFFFFFFF,TEXT_2=0xFFC9CEDA,TEXT_3=0xFF8D95A6,INK=0xFF10131A,MESSAGE=0xE00A0C10;
    // Teal glass bars (XD party and Bag selection bars) and their ink; navy value bar.
    static final int CREAM=0xFFD6EEF2,CREAM_LOW=0xFF8FC2D2,CREAM_EDGE=0xFF3E5866,CREAM_INK=0xFF14232B,NAVY=0xFF14203A;
    // Warm focus, Shadow purple and the Snag teal.
    static final int FOCUS=0xFFFFB531,FOCUS_HOT=0xFFFF7A1F,SHADOW=0xFF5B2A86,SHADOW_DEEP=0xFF201030,SHADOW_AURA=0xFF9A5CFF,
            SHADOW_LIGHT=0xFFC9A8F5,SNAG=0xFF20A9A4;
    // Gen 6 battle command code, plus the dock roles.
    static final int FIGHT=0xFFD8352A,BAG=0xFFE8A51C,POKEMON=0xFF45AE3E,RUN=0xFF3474C8,CALL=0xFF6A37B4,
            BACK=0xFF2F7FD6,CLOSE=0xFFD8323C,CONFIRM=0xFFF08A1C,UTILITY=0xFFA9B0BC,PARTY_TILE=0xFF8E8E8E;
    static final int HP_GOOD=0xFF30D850,HP_MID=0xFFF8C800,HP_LOW=0xFFF84838,HP_TRACK=0xFF2A2C30;

    static final Paint fill=new Paint(Paint.ANTI_ALIAS_FLAG),stroke=new Paint(Paint.ANTI_ALIAS_FLAG),
            text=new Paint(Paint.ANTI_ALIAS_FLAG|Paint.SUBPIXEL_TEXT_FLAG),outline=new Paint(Paint.ANTI_ALIAS_FLAG|Paint.SUBPIXEL_TEXT_FLAG);
    /** Condensed bold for labels and names, condensed regular for body text. */
    static final Typeface HEAVY=heavy(),BOLD=Typeface.create("sans-serif-condensed",Typeface.BOLD),MEDIUM=Typeface.create("sans-serif-condensed",Typeface.NORMAL),
            REGULAR=Typeface.create("sans-serif",Typeface.NORMAL),
            // XD captions and P*DA labels: a wide bold face set with open tracking.
            WIDE=Typeface.create("sans-serif",Typeface.BOLD);
    static{stroke.setStyle(Paint.Style.STROKE);outline.setStyle(Paint.Style.STROKE);outline.setStrokeJoin(Paint.Join.ROUND);outline.setStrokeCap(Paint.Cap.ROUND);}
    private static final RectF scratch=new RectF(),band=new RectF();
    private static final Path path=new Path();
    private static Typeface heavy(){
        // Condensed bold, the nearest system face to the games' narrow rounded sans.
        Typeface t=Typeface.create("sans-serif-condensed",Typeface.BOLD);
        return t==null?Typeface.create("sans-serif",Typeface.BOLD):t;
    }

    /** XD's own type colours, as on its type tags; unknown names stay neutral. Shadow is Cipher purple. */
    static int typeColor(String name){
        switch(type(name)){
            case "NORMAL":return 0xFFACA47B;case "FIRE":return 0xFFF67B31;case "WATER":return 0xFF6A94EE;
            case "ELECTRIC":return 0xFFFFCD31;case "GRASS":return 0xFF7BC552;case "ICE":return 0xFF9CD5DE;
            case "FIGHTING":return 0xFFBD3131;case "POISON":return 0xFFA4419C;case "GROUND":return 0xFFE6BD6A;
            case "FLYING":return 0xFFAC94EE;case "PSYCHIC":return 0xFFEE628B;case "BUG":return 0xFFA4AC4A;
            case "ROCK":return 0xFFA4944A;case "GHOST":return 0xFF735A94;case "DRAGON":return 0xFF7B52EE;
            case "DARK":return 0xFF735A4A;case "STEEL":return 0xFFBDBDCD;case "FAIRY":return 0xFFEE99AC;
            case "SHADOW":return 0xFF5A2E78;default:return 0xFF6AC594;
        }
    }
    /** Move button body: the type colour a little deeper, so white names read on light types too. */
    static int moveColor(String name){int c=typeColor(name);return light(c)?shade(c,0.78f):shade(c,0.92f);}
    /** XD abbreviates long type names on its tags. */
    static String typeTag(String name){
        String t=type(name);
        switch(t){case "PSYCHIC":return "PSYCHC";case "ELECTRIC":return "ELECTR";case "FIGHTING":return "FIGHT";case "":return "???";default:return t;}
    }
    static String type(String name){return name==null?"":name.trim().toUpperCase(Locale.ROOT);}
    /** Badge for XD's condition byte. 3 and 4 are poison (field poison code); 5-8 follow the community enum, unverified. */
    static String statusLabel(int code,int hp,int maxHp){
        if(maxHp>0&&hp==0)return "FNT";
        switch(code){case 0:return "";case 3:case 4:return "PSN";case 5:return "PAR";case 6:return "BRN";case 7:return "FRZ";case 8:return "SLP";default:return "?";}
    }
    static int statusColor(String label){
        // XD's status tag fills.
        switch(label){case "PSN":return 0xFFA462B4;case "PAR":return 0xFFCDC520;case "BRN":return 0xFFFF8B52;
            case "FRZ":return 0xFFB4CDEE;case "SLP":return 0xFFA4A48B;case "FNT":return 0xFFE65A41;default:return 0xFFA4A48B;}
    }
    /** Green above half, yellow at half or below, red at a fifth or below. */
    static int hpColor(int hp,int max){
        if(max<=0)return TEXT_3;
        return hp*5<=max?HP_LOW:hp*2<=max?HP_MID:HP_GOOD;
    }

    static int shade(int c,float f){
        return (c&0xFF000000)|(clamp((int)(((c>>16)&255)*f))<<16)|(clamp((int)(((c>>8)&255)*f))<<8)|clamp((int)((c&255)*f));
    }
    static int mix(int a,int b,float t){
        int r=(int)(((a>>16)&255)+(((b>>16)&255)-((a>>16)&255))*t),g=(int)(((a>>8)&255)+(((b>>8)&255)-((a>>8)&255))*t),
            bl=(int)((a&255)+((b&255)-(a&255))*t),al=(int)(((a>>>24))+(((b>>>24))-((a>>>24)))*t);
        return (clamp(al)<<24)|(clamp(r)<<16)|(clamp(g)<<8)|clamp(bl);
    }
    static int alpha(int c,float a){return (clamp((int)((c>>>24)*a))<<24)|(c&0xFFFFFF);}
    private static int clamp(int v){return v<0?0:v>255?255:v;}
    static boolean light(int c){return ((c>>16)&255)*0.299+((c>>8)&255)*0.587+(c&255)*0.114>176;}

    /**
     * Outline of a 3DS menu plate: a rectangle whose corners are clipped at 45 degrees by cut. A cut of
     * half the short side or more gives a circle, used only for dock buttons and ball marks.
     */
    static Path shape(RectF r,float cut){
        Path p=new Path();
        float m=Math.min(r.width(),r.height())/2;
        if(cut>=m-0.01f){p.addOval(r,Path.Direction.CW);return p;}
        cut=Math.max(0,cut);
        p.moveTo(r.left+cut,r.top);p.lineTo(r.right-cut,r.top);p.lineTo(r.right,r.top+cut);p.lineTo(r.right,r.bottom-cut);
        p.lineTo(r.right-cut,r.bottom);p.lineTo(r.left+cut,r.bottom);p.lineTo(r.left,r.bottom-cut);p.lineTo(r.left,r.top+cut);p.close();
        return p;
    }
    private static Path inset(RectF r,float cut,float d){
        boolean round=cut>=Math.min(r.width(),r.height())/2-0.01f;
        scratch.set(r.left+d,r.top+d,r.right-d,r.bottom-d);
        return shape(scratch,round?Math.min(scratch.width(),scratch.height())/2:Math.max(0,cut-d*0.42f));
    }
    /**
     * Gen 6 button finish, kept flat and crisp: a one-unit hard shadow, a dark keyline, a thin light rim,
     * a body shading gently top to bottom, a hard-edged specular band over the top 42% and a dark
     * bottom bevel line.
     */
    static void gloss(Canvas c,RectF r,float cut,int base,float u){
        Path outer=shape(r,cut);
        c.save();c.translate(0,u);fill.setColor(0x47000000);c.drawPath(outer,fill);c.restore();
        fill.setColor(shade(base,0.4f));c.drawPath(outer,fill);
        // XD chrome rim: a bright-to-dark steel bevel around the colour body.
        Path rim=inset(r,cut,u*0.7f);RectF rb=new RectF(scratch);
        fill.setColor(0xFF000000);fill.setShader(new LinearGradient(0,rb.top,0,rb.bottom,new int[]{0xFFF4F4F4,mix(STEEL,base,0.25f),0xFF6E6E6E},
            new float[]{0,0.5f,1},Shader.TileMode.CLAMP));c.drawPath(rim,fill);fill.setShader(null);
        Path body=inset(r,cut,u*1.6f);RectF b=new RectF(scratch);
        fill.setColor(0xFF000000);fill.setShader(new LinearGradient(0,b.top,0,b.bottom,shade(base,1.08f),shade(base,0.88f),Shader.TileMode.CLAMP));
        c.drawPath(body,fill);fill.setShader(null);
        c.save();c.clipPath(body);
        fill.setColor(0x24FFFFFF);c.drawRect(b.left,b.top,b.right,b.top+Math.min(b.height()*0.42f,u*22),fill);
        fill.setColor(0x52FFFFFF);c.drawRect(b.left,b.top,b.right,b.top+u*0.8f,fill);
        fill.setColor(0x33000000);c.drawRect(b.left,b.bottom-u*0.9f,b.right,b.bottom,fill);
        c.restore();
    }
    /** Cream list row (X/Y Bag, PSS profile): small clipped corners; the selected row turns warm orange. */
    static void cream(Canvas c,RectF r,float cut,boolean hot,float u){
        Path outer=shape(r,cut);
        c.save();c.translate(0,u);fill.setColor(0x40000000);c.drawPath(outer,fill);c.restore();
        fill.setColor(hot?0xFF9C4A08:CREAM_EDGE);c.drawPath(outer,fill);
        fill.setColor(0xFFFFFFFF);c.drawPath(inset(r,cut,u*0.7f),fill);
        Path body=inset(r,cut,u*1.4f);RectF b=new RectF(scratch);
        fill.setColor(0xFF000000);fill.setShader(new LinearGradient(0,b.top,0,b.bottom,hot?0xFFFFC75A:CREAM,hot?0xFFF28A1E:CREAM_LOW,Shader.TileMode.CLAMP));
        c.drawPath(body,fill);fill.setShader(null);
        c.save();c.clipPath(body);fill.setColor(0x24FFFFFF);c.drawRect(b.left,b.top,b.right,b.top+b.height()*0.42f,fill);c.restore();
    }
    /** Gen 6 dark translucent box: navy-black at about 85%, small clipped corners, a faint light keyline. */
    static void message(Canvas c,RectF r,float cut,float u){
        fill.setColor(MESSAGE);c.drawPath(shape(r,cut),fill);
        stroke.setStrokeWidth(u*0.7f);stroke.setColor(0x66A8A8A8);c.drawPath(inset(r,cut,u*0.8f),stroke);
    }
    static void ring(Canvas c,RectF r,float cut,int color,float width){
        stroke.setStrokeWidth(width);stroke.setColor(color);stroke.setStrokeJoin(Paint.Join.MITER);
        RectF o=new RectF(r.left-width/2,r.top-width/2,r.right+width/2,r.bottom+width/2);
        boolean round=cut>=Math.min(r.width(),r.height())/2-0.01f;
        c.drawPath(shape(o,round?Math.min(o.width(),o.height())/2:cut+width*0.2f),stroke);stroke.setStrokeJoin(Paint.Join.ROUND);
    }
    /** Warm focus rim: orange outer line around a yellow inner line, alpha follows the 1 Hz pulse. */
    static void focus(Canvas c,RectF r,float cut,float a,float u){
        ring(c,r,cut,alpha(FOCUS_HOT,a),u*2f);
        stroke.setStrokeWidth(u*0.8f);stroke.setColor(alpha(0xFFFFE07A,a));c.drawPath(inset(r,cut,u*0.4f),stroke);
    }

    static Paint font(Typeface face,float size,int color){
        text.setTypeface(face);text.setTextSize(size);text.setColor(color);text.setTextAlign(Paint.Align.LEFT);text.setLetterSpacing(0);return text;
    }
    /** White label with a dark round-joined outline, the 3DS way of keeping text legible on any fill. */
    static void outlined(Canvas c,String s,float x,float y,Typeface face,float size,int color,int edge,float edgeWidth,Paint.Align align){
        if(edgeWidth>0){
            outline.setTypeface(face);outline.setTextSize(size);outline.setColor(edge);outline.setStrokeWidth(edgeWidth);outline.setTextAlign(align);
            c.drawText(s,x,y,outline);
        }
        Paint p=font(face,size,color);p.setTextAlign(align);c.drawText(s,x,y,p);p.setTextAlign(Paint.Align.LEFT);
    }
    static String fit(String s,Paint p,float width){
        if(s==null)return "";
        if(p.measureText(s)<=width)return s;
        int n=s.length();while(n>1&&p.measureText(s.substring(0,n)+"…")>width)n--;return s.substring(0,n)+"…";
    }
    /** Largest size up to max that fits the width, down to min; names shrink before they ever truncate. */
    static float fitSize(String s,Typeface face,float max,float min,float width){
        Paint p=font(face,max,TEXT);float w=p.measureText(s);
        if(w<=width||w<=0)return max;
        return Math.max(min,max*width/w);
    }

    /** HP bar with the dark "HP" tag at its left end, like the 3DS databoxes. */
    static void hpBar(Canvas c,RectF r,int hp,int max,boolean tag,float u){
        float h=r.height(),left=r.left;
        if(tag){
            float tw=h*2.6f;scratch.set(r.left,r.top-h*0.35f,r.left+tw+h,r.bottom+h*0.35f);
            fill.setColor(0xFF0C0C0C);c.drawPath(shape(scratch,scratch.height()*0.3f),fill);
            Paint p=font(HEAVY,h*1.25f,0xFFFF6A3D);c.drawText("HP",r.left+h*0.55f,r.centerY()+h*0.45f,p);left=r.left+tw;
        }
        scratch.set(left,r.top,r.right,r.bottom);float radius=h/2;
        fill.setColor(0xFF1A1E26);scratch.inset(-u*0.6f,-u*0.6f);c.drawRoundRect(scratch,radius+u*0.6f,radius+u*0.6f,fill);scratch.inset(u*0.6f,u*0.6f);
        fill.setColor(HP_TRACK);c.drawRoundRect(scratch,radius,radius,fill);
        if(max<=0)return;
        float t=Math.max(0f,Math.min(1f,hp/(float)max));if(t<=0)return;
        scratch.set(left,r.top,left+Math.max(h,(r.right-left)*t),r.bottom);
        int base=hpColor(hp,max);
        fill.setColor(0xFF000000);fill.setShader(new LinearGradient(0,r.top,0,r.bottom,new int[]{mix(base,0xFFFFFFFF,0.35f),base,shade(base,0.72f)},
            new float[]{0,0.45f,1},Shader.TileMode.CLAMP));
        c.drawRoundRect(scratch,radius,radius,fill);fill.setShader(null);
    }
    /** Heart Gauge in five segments; purple segments clear as the heart opens. Unknown values draw outlines. */
    static void heartGauge(Canvas c,RectF r,int heart,int max,float u){
        float gap=u*0.8f,w=(r.width()-gap*4)/5;boolean known=heart>=0&&max>0;
        int lit=known?(int)Math.ceil(Math.min(1f,heart/(float)max)*5-1e-4):0;
        for(int i=0;i<5;i++){
            scratch.set(r.left+i*(w+gap),r.top,r.left+i*(w+gap)+w,r.bottom);float radius=r.height()/2;
            if(!known){stroke.setStrokeWidth(u*0.6f);stroke.setColor(alpha(SHADOW_LIGHT,0.6f));c.drawRoundRect(scratch,radius,radius,stroke);continue;}
            fill.setColor(0xFF1A1024);scratch.inset(-u*0.4f,-u*0.4f);c.drawRoundRect(scratch,radius,radius,fill);scratch.inset(u*0.4f,u*0.4f);
            fill.setColor(i<lit?SHADOW_AURA:0xFFE9E4F2);c.drawRoundRect(scratch,radius,radius,fill);
        }
    }
    /** Status condition tag: white caps on the condition colour, in its own slot. */
    /** XD status tag: a pastel pill with a light top, a darker rim and black caps with a faint glow. */
    static void badge(Canvas c,String label,float x,float y,float h,float u){
        if(label.isEmpty())return;
        pill(c,new RectF(x,y,x+badgeWidth(label,h),y+h),statusColor(label),label,u);
    }
    /** The XD tag pill shared by type and status tags. */
    static void pill(Canvas c,RectF r,int col,String label,float u){
        float h=r.height(),rad=h/2;
        fill.setColor(shade(col,0.45f));c.drawRoundRect(r,rad,rad,fill);
        scratch.set(r.left+u*0.6f,r.top+u*0.6f,r.right-u*0.6f,r.bottom-u*0.6f);
        fill.setColor(0xFF000000);fill.setShader(new LinearGradient(0,scratch.top,0,scratch.bottom,new int[]{mix(col,0xFFFFFFFF,0.45f),col,shade(col,0.8f)},
            new float[]{0,0.35f,1},Shader.TileMode.CLAMP));c.drawRoundRect(scratch,rad,rad,fill);fill.setShader(null);
        outline.setTypeface(HEAVY);outline.setTextSize(h*0.72f);outline.setColor(alpha(mix(col,0xFFFFFFFF,0.7f),0.9f));outline.setStrokeWidth(u*0.9f);outline.setTextAlign(Paint.Align.CENTER);
        outline.setLetterSpacing(0.02f);c.drawText(label,r.centerX(),r.top+h*0.76f,outline);outline.setLetterSpacing(0);
        Paint p=font(HEAVY,h*0.72f,0xFF0C0C0C);p.setLetterSpacing(0.02f);p.setTextAlign(Paint.Align.CENTER);c.drawText(label,r.centerX(),r.top+h*0.76f,p);
        p.setTextAlign(Paint.Align.LEFT);p.setLetterSpacing(0);
    }
    static float pillWidth(String label,float h){Paint p=font(HEAVY,h*0.72f,TEXT);p.setLetterSpacing(0.02f);float w=p.measureText(label)+h*1.1f;p.setLetterSpacing(0);return w;}
    static float badgeWidth(String label,float h){return label.isEmpty()?0:pillWidth(label,h);}
    /** A party menu icon slot: a soft disc with an original ball mark, purple-ringed for Shadow Pokemon. */
    static void monSlot(Canvas c,float cx,float cy,float d,boolean shadow,boolean fainted,float u){
        fill.setColor(0x40000000);c.drawCircle(cx,cy+u*0.8f,d/2,fill);
        fill.setColor(0xFF000000);fill.setShader(new RadialGradient(cx-d*0.15f,cy-d*0.2f,d*0.7f,
            shadow?0xFF6B4A9A:0xFFF4F7FB,shadow?0xFF2A1640:0xFFB9C3D1,Shader.TileMode.CLAMP));
        c.drawCircle(cx,cy,d/2,fill);fill.setShader(null);
        ball(c,cx,cy,d*0.62f,fainted?0xFF8A8F99:shadow?0xFF8A5CCF:0xFFE2463A,shadow?0xFF3A2456:0xFF2B2F38,u);
        if(shadow){stroke.setStrokeWidth(u*1.3f);stroke.setColor(alpha(SHADOW_AURA,0.9f));c.drawCircle(cx,cy,d/2+u*0.4f,stroke);}
    }
    /** A generic two-tone capture ball mark, drawn here; used as icon and party pip. */
    static void ball(Canvas c,float cx,float cy,float d,int top,int line,float u){
        float r=d/2;scratch.set(cx-r,cy-r,cx+r,cy+r);
        fill.setColor(0xFFF6F6F6);c.drawCircle(cx,cy,r,fill);
        fill.setColor(top);c.drawArc(scratch,180,180,true,fill);
        stroke.setColor(line);stroke.setStrokeWidth(Math.max(1f,d*0.09f));c.drawCircle(cx,cy,r,stroke);
        c.drawLine(cx-r,cy,cx+r,cy,stroke);
        fill.setColor(0xFFF6F6F6);c.drawCircle(cx,cy,r*0.3f,fill);c.drawCircle(cx,cy,r*0.3f,stroke);
    }

    /** Line and solid glyphs, original, drawn in a size x size box at (x,y). */
    static void glyph(Canvas c,String name,float x,float y,float size,int color,float u){
        stroke.setColor(color);stroke.setStrokeWidth(Math.max(1f,size*0.12f));stroke.setStrokeCap(Paint.Cap.ROUND);stroke.setStrokeJoin(Paint.Join.ROUND);
        fill.setColor(color);float s=size;path.reset();
        switch(name){
            case "bag":
                scratch.set(x+s*0.14f,y+s*0.32f,x+s*0.86f,y+s*0.9f);c.drawRoundRect(scratch,s*0.16f,s*0.16f,fill);
                scratch.set(x+s*0.32f,y+s*0.1f,x+s*0.68f,y+s*0.5f);c.drawArc(scratch,180,180,false,stroke);
                fill.setColor(0x55000000);scratch.set(x+s*0.14f,y+s*0.5f,x+s*0.86f,y+s*0.58f);c.drawRect(scratch,fill);break;
            case "party":case "ball":
                ball(c,x+s/2,y+s/2,s*0.84f,color,0xFF20242C,u);break;
            case "pda":
                scratch.set(x+s*0.18f,y+s*0.06f,x+s*0.82f,y+s*0.94f);c.drawRoundRect(scratch,s*0.14f,s*0.14f,fill);
                fill.setColor(0xFF1E2430);scratch.set(x+s*0.28f,y+s*0.17f,x+s*0.72f,y+s*0.55f);c.drawRoundRect(scratch,s*0.05f,s*0.05f,fill);
                star(c,x+s*0.5f,y+s*0.74f,s*0.11f,0xFF1E2430);break;
            case "snag":
                // Snag Machine claw: three hooked arcs closing on a ball. Original glyph.
                for(int i=0;i<3;i++){
                    float cx=x+s*0.5f,cy=y+s*0.5f;scratch.set(cx-s*0.42f,cy-s*0.42f,cx+s*0.42f,cy+s*0.42f);
                    c.drawArc(scratch,-90+i*120+14,72,false,stroke);
                }
                ball(c,x+s*0.5f,y+s*0.5f,s*0.42f,color,0xFF20242C,u);break;
            case "run":
                path.moveTo(x+s*0.16f,y+s*0.2f);path.lineTo(x+s*0.46f,y+s*0.5f);path.lineTo(x+s*0.16f,y+s*0.8f);
                path.moveTo(x+s*0.5f,y+s*0.2f);path.lineTo(x+s*0.8f,y+s*0.5f);path.lineTo(x+s*0.5f,y+s*0.8f);
                stroke.setStrokeWidth(s*0.16f);c.drawPath(path,stroke);break;
            case "back":
                // 3DS U-turn arrow.
                stroke.setStrokeWidth(s*0.16f);
                path.moveTo(x+s*0.3f,y+s*0.82f);path.lineTo(x+s*0.56f,y+s*0.82f);
                path.quadTo(x+s*0.84f,y+s*0.82f,x+s*0.84f,y+s*0.56f);path.quadTo(x+s*0.84f,y+s*0.3f,x+s*0.56f,y+s*0.3f);path.lineTo(x+s*0.3f,y+s*0.3f);
                c.drawPath(path,stroke);path.reset();
                path.moveTo(x+s*0.1f,y+s*0.3f);path.lineTo(x+s*0.36f,y+s*0.08f);path.lineTo(x+s*0.36f,y+s*0.52f);path.close();c.drawPath(path,fill);break;
            case "close":
                stroke.setStrokeWidth(s*0.17f);c.drawLine(x+s*0.24f,y+s*0.24f,x+s*0.76f,y+s*0.76f,stroke);c.drawLine(x+s*0.76f,y+s*0.24f,x+s*0.24f,y+s*0.76f,stroke);break;
            case "fight":
                // Two crossed strikes.
                stroke.setStrokeWidth(s*0.14f);
                c.drawLine(x+s*0.18f,y+s*0.82f,x+s*0.78f,y+s*0.22f,stroke);c.drawLine(x+s*0.22f,y+s*0.22f,x+s*0.82f,y+s*0.82f,stroke);
                path.moveTo(x+s*0.86f,y+s*0.14f);path.lineTo(x+s*0.86f,y+s*0.4f);path.lineTo(x+s*0.6f,y+s*0.14f);path.close();c.drawPath(path,fill);break;
            case "call":
                scratch.set(x+s*0.24f,y+s*0.24f,x+s*0.76f,y+s*0.76f);c.drawArc(scratch,-60,120,false,stroke);
                scratch.set(x+s*0.06f,y+s*0.06f,x+s*0.94f,y+s*0.94f);c.drawArc(scratch,-55,110,false,stroke);
                c.drawCircle(x+s*0.32f,y+s*0.5f,s*0.12f,fill);break;
            case "menu":
                for(int i=0;i<3;i++){float yy=y+s*(0.26f+0.24f*i);c.drawLine(x+s*0.2f,yy,x+s*0.8f,yy,stroke);}break;
            case "up":case "down":case "left":case "right":
                c.save();c.rotate(name.equals("up")?0:name.equals("right")?90:name.equals("down")?180:270,x+s/2,y+s/2);
                path.moveTo(x+s*0.5f,y+s*0.2f);path.lineTo(x+s*0.84f,y+s*0.7f);path.lineTo(x+s*0.16f,y+s*0.7f);path.close();c.drawPath(path,fill);c.restore();break;
            case "play":
                path.moveTo(x+s*0.3f,y+s*0.16f);path.lineTo(x+s*0.84f,y+s*0.5f);path.lineTo(x+s*0.3f,y+s*0.84f);path.close();c.drawPath(path,fill);break;
            case "check":
                stroke.setStrokeWidth(s*0.17f);path.moveTo(x+s*0.18f,y+s*0.52f);path.lineTo(x+s*0.42f,y+s*0.76f);path.lineTo(x+s*0.84f,y+s*0.26f);c.drawPath(path,stroke);break;
            case "pad":
                scratch.set(x+s*0.36f,y+s*0.08f,x+s*0.64f,y+s*0.92f);c.drawRoundRect(scratch,s*0.06f,s*0.06f,fill);
                scratch.set(x+s*0.08f,y+s*0.36f,x+s*0.92f,y+s*0.64f);c.drawRoundRect(scratch,s*0.06f,s*0.06f,fill);break;
            case "display":
                scratch.set(x+s*0.06f,y+s*0.14f,x+s*0.94f,y+s*0.7f);c.drawRoundRect(scratch,s*0.1f,s*0.1f,stroke);
                c.drawLine(x+s*0.5f,y+s*0.72f,x+s*0.5f,y+s*0.86f,stroke);c.drawLine(x+s*0.28f,y+s*0.88f,x+s*0.72f,y+s*0.88f,stroke);break;
            case "audio":
                path.moveTo(x+s*0.1f,y+s*0.36f);path.lineTo(x+s*0.3f,y+s*0.36f);path.lineTo(x+s*0.52f,y+s*0.14f);path.lineTo(x+s*0.52f,y+s*0.86f);
                path.lineTo(x+s*0.3f,y+s*0.64f);path.lineTo(x+s*0.1f,y+s*0.64f);path.close();c.drawPath(path,fill);
                scratch.set(x+s*0.42f,y+s*0.3f,x+s*0.8f,y+s*0.7f);c.drawArc(scratch,-50,100,false,stroke);
                scratch.set(x+s*0.38f,y+s*0.12f,x+s*0.96f,y+s*0.88f);c.drawArc(scratch,-50,100,false,stroke);break;
            case "game":
                c.drawCircle(x+s*0.5f,y+s*0.5f,s*0.18f,stroke);
                for(int i=0;i<8;i++){double a=Math.PI*i/4;c.drawLine(x+s*0.5f+(float)Math.cos(a)*s*0.3f,y+s*0.5f+(float)Math.sin(a)*s*0.3f,
                    x+s*0.5f+(float)Math.cos(a)*s*0.42f,y+s*0.5f+(float)Math.sin(a)*s*0.42f,stroke);}break;
            case "save":
                scratch.set(x+s*0.14f,y+s*0.08f,x+s*0.86f,y+s*0.92f);c.drawRoundRect(scratch,s*0.1f,s*0.1f,fill);
                fill.setColor(0x66000000);for(int i=0;i<3;i++){scratch.set(x+s*0.3f,y+s*(0.3f+0.18f*i),x+s*(i==2?0.56f:0.7f),y+s*(0.36f+0.18f*i));c.drawRoundRect(scratch,s*0.03f,s*0.03f,fill);}break;
            case "exit":
                scratch.set(x+s*0.12f,y+s*0.12f,x+s*0.58f,y+s*0.88f);c.drawRoundRect(scratch,s*0.08f,s*0.08f,stroke);
                path.moveTo(x+s*0.4f,y+s*0.5f);path.lineTo(x+s*0.9f,y+s*0.5f);path.moveTo(x+s*0.74f,y+s*0.32f);path.lineTo(x+s*0.9f,y+s*0.5f);
                path.lineTo(x+s*0.74f,y+s*0.68f);c.drawPath(path,stroke);break;
            case "mail":
                scratch.set(x+s*0.08f,y+s*0.2f,x+s*0.92f,y+s*0.8f);c.drawRoundRect(scratch,s*0.1f,s*0.1f,fill);
                stroke.setColor(0x80000000);stroke.setStrokeWidth(s*0.08f);path.moveTo(x+s*0.12f,y+s*0.26f);path.lineTo(x+s*0.5f,y+s*0.54f);path.lineTo(x+s*0.88f,y+s*0.26f);c.drawPath(path,stroke);break;
            case "memo":
                scratch.set(x+s*0.16f,y+s*0.08f,x+s*0.84f,y+s*0.92f);c.drawRoundRect(scratch,s*0.1f,s*0.1f,fill);
                fill.setColor(0x66000000);c.drawCircle(x+s*0.38f,y+s*0.36f,s*0.1f,fill);
                scratch.set(x+s*0.28f,y+s*0.58f,x+s*0.72f,y+s*0.64f);c.drawRect(scratch,fill);scratch.set(x+s*0.28f,y+s*0.72f,x+s*0.6f,y+s*0.78f);c.drawRect(scratch,fill);break;
            case "flag":
                stroke.setStrokeWidth(s*0.12f);c.drawLine(x+s*0.26f,y+s*0.08f,x+s*0.26f,y+s*0.94f,stroke);
                path.moveTo(x+s*0.3f,y+s*0.1f);path.lineTo(x+s*0.86f,y+s*0.28f);path.lineTo(x+s*0.3f,y+s*0.48f);path.close();c.drawPath(path,fill);break;
            case "heart":
                path.moveTo(x+s*0.5f,y+s*0.88f);path.cubicTo(x-s*0.12f,y+s*0.42f,x+s*0.2f,y,x+s*0.5f,y+s*0.28f);
                path.cubicTo(x+s*0.8f,y,x+s*1.12f,y+s*0.42f,x+s*0.5f,y+s*0.88f);path.close();c.drawPath(path,fill);break;
            case "star":star(c,x+s/2,y+s/2,s*0.46f,color);break;
            default:c.drawCircle(x+s*0.5f,y+s*0.5f,s*0.14f,fill);
        }
    }
    static void star(Canvas c,float cx,float cy,float r,int color){
        Path p=new Path();
        for(int i=0;i<10;i++){double a=-Math.PI/2+i*Math.PI/5;float rr=i%2==0?r:r*0.45f;
            float px=cx+(float)Math.cos(a)*rr,py=cy+(float)Math.sin(a)*rr;if(i==0)p.moveTo(px,py);else p.lineTo(px,py);}
        p.close();fill.setColor(color);c.drawPath(p,fill);
    }
    /**
     * Original emblem for each type, drawn in one colour inside a size box. These are simple symbols of
     * the element (flame, drop, leaf...), not the games' type icons.
     */
    static void typeGlyph(Canvas c,String type,float x,float y,float s,int color,float u){
        fill.setColor(color);stroke.setColor(color);stroke.setStrokeWidth(s*0.14f);stroke.setStrokeCap(Paint.Cap.ROUND);stroke.setStrokeJoin(Paint.Join.ROUND);
        path.reset();float cx=x+s/2,cy=y+s/2;
        switch(type(type)){
            case "FIRE":path.moveTo(cx,y+s*0.04f);path.cubicTo(x+s*0.98f,y+s*0.4f,x+s*0.86f,y+s*0.96f,cx,y+s*0.96f);
                path.cubicTo(x+s*0.14f,y+s*0.96f,x+s*0.06f,y+s*0.5f,x+s*0.34f,y+s*0.3f);path.cubicTo(x+s*0.32f,y+s*0.5f,x+s*0.42f,y+s*0.56f,x+s*0.46f,y+s*0.5f);
                path.cubicTo(x+s*0.4f,y+s*0.3f,x+s*0.44f,y+s*0.14f,cx,y+s*0.04f);c.drawPath(path,fill);break;
            case "WATER":path.moveTo(cx,y+s*0.04f);path.cubicTo(x+s*0.62f,y+s*0.3f,x+s*0.88f,y+s*0.5f,x+s*0.86f,y+s*0.66f);
                path.cubicTo(x+s*0.84f,y+s*0.86f,x+s*0.68f,y+s*0.96f,cx,y+s*0.96f);path.cubicTo(x+s*0.32f,y+s*0.96f,x+s*0.16f,y+s*0.86f,x+s*0.14f,y+s*0.66f);
                path.cubicTo(x+s*0.12f,y+s*0.5f,x+s*0.38f,y+s*0.3f,cx,y+s*0.04f);c.drawPath(path,fill);break;
            case "GRASS":path.moveTo(x+s*0.1f,y+s*0.9f);path.cubicTo(x+s*0.1f,y+s*0.3f,x+s*0.5f,y+s*0.06f,x+s*0.92f,y+s*0.08f);
                path.cubicTo(x+s*0.94f,y+s*0.5f,x+s*0.7f,y+s*0.9f,x+s*0.1f,y+s*0.9f);c.drawPath(path,fill);break;
            case "ELECTRIC":path.moveTo(x+s*0.62f,y+s*0.02f);path.lineTo(x+s*0.16f,y+s*0.56f);path.lineTo(x+s*0.46f,y+s*0.56f);
                path.lineTo(x+s*0.36f,y+s*0.98f);path.lineTo(x+s*0.86f,y+s*0.4f);path.lineTo(x+s*0.54f,y+s*0.4f);path.close();c.drawPath(path,fill);break;
            case "ICE":for(int i=0;i<3;i++){double a=Math.PI*i/3;float dx=(float)Math.cos(a)*s*0.44f,dy=(float)Math.sin(a)*s*0.44f;c.drawLine(cx-dx,cy-dy,cx+dx,cy+dy,stroke);}break;
            case "FIGHTING":scratch.set(x+s*0.14f,y+s*0.18f,x+s*0.86f,y+s*0.86f);c.drawRoundRect(scratch,s*0.2f,s*0.2f,fill);
                stroke.setColor(0x66000000);stroke.setStrokeWidth(s*0.07f);for(int i=1;i<3;i++){float xx=x+s*(0.14f+0.24f*i);c.drawLine(xx,y+s*0.2f,xx,y+s*0.5f,stroke);}break;
            case "POISON":c.drawCircle(cx,y+s*0.4f,s*0.3f,fill);scratch.set(x+s*0.3f,y+s*0.6f,x+s*0.7f,y+s*0.94f);c.drawRoundRect(scratch,s*0.1f,s*0.1f,fill);break;
            case "GROUND":path.moveTo(x+s*0.04f,y+s*0.9f);path.lineTo(x+s*0.38f,y+s*0.24f);path.lineTo(x+s*0.56f,y+s*0.56f);path.lineTo(x+s*0.7f,y+s*0.38f);
                path.lineTo(x+s*0.96f,y+s*0.9f);path.close();c.drawPath(path,fill);break;
            case "FLYING":path.moveTo(x+s*0.06f,y+s*0.3f);path.cubicTo(x+s*0.4f,y+s*0.22f,x+s*0.8f,y+s*0.36f,x+s*0.94f,y+s*0.86f);
                path.cubicTo(x+s*0.6f,y+s*0.7f,x+s*0.3f,y+s*0.7f,x+s*0.06f,y+s*0.3f);c.drawPath(path,fill);break;
            case "PSYCHIC":scratch.set(x+s*0.06f,y+s*0.24f,x+s*0.94f,y+s*0.76f);c.drawOval(scratch,stroke);c.drawCircle(cx,cy,s*0.16f,fill);break;
            case "BUG":scratch.set(x+s*0.22f,y+s*0.18f,x+s*0.78f,y+s*0.94f);c.drawOval(scratch,fill);
                c.drawLine(x+s*0.36f,y+s*0.2f,x+s*0.22f,y+s*0.04f,stroke);c.drawLine(x+s*0.64f,y+s*0.2f,x+s*0.78f,y+s*0.04f,stroke);break;
            case "ROCK":path.moveTo(x+s*0.3f,y+s*0.08f);path.lineTo(x+s*0.8f,y+s*0.16f);path.lineTo(x+s*0.94f,y+s*0.62f);path.lineTo(x+s*0.6f,y+s*0.94f);
                path.lineTo(x+s*0.12f,y+s*0.8f);path.lineTo(x+s*0.06f,y+s*0.36f);path.close();c.drawPath(path,fill);break;
            case "GHOST":path.moveTo(x+s*0.16f,y+s*0.94f);path.lineTo(x+s*0.16f,y+s*0.44f);path.cubicTo(x+s*0.16f,y+s*0.04f,x+s*0.84f,y+s*0.04f,x+s*0.84f,y+s*0.44f);
                path.lineTo(x+s*0.84f,y+s*0.94f);path.lineTo(x+s*0.67f,y+s*0.8f);path.lineTo(cx,y+s*0.94f);path.lineTo(x+s*0.33f,y+s*0.8f);path.close();c.drawPath(path,fill);break;
            case "DRAGON":path.moveTo(x+s*0.2f,y+s*0.06f);path.cubicTo(x+s*0.9f,y+s*0.2f,x+s*0.96f,y+s*0.8f,x+s*0.5f,y+s*0.96f);
                path.cubicTo(x+s*0.66f,y+s*0.6f,x+s*0.5f,y+s*0.3f,x+s*0.2f,y+s*0.06f);c.drawPath(path,fill);break;
            case "DARK":{
                // Crescent moon.
                Path moon=new Path(),bite=new Path();moon.addCircle(cx,cy,s*0.44f,Path.Direction.CW);bite.addCircle(cx+s*0.24f,cy-s*0.14f,s*0.36f,Path.Direction.CW);
                moon.op(bite,Path.Op.DIFFERENCE);c.drawPath(moon,fill);break;
            }
            case "STEEL":c.drawCircle(cx,cy,s*0.42f,fill);fill.setColor(0x66000000);c.drawCircle(cx,cy,s*0.18f,fill);break;
            case "FAIRY":star(c,cx,cy,s*0.48f,color);break;
            case "SHADOW":
                // A dark flame with an eye of light: XD's aura, drawn as an original mark.
                path.moveTo(cx,y+s*0.02f);path.cubicTo(x+s*1.0f,y+s*0.36f,x+s*0.9f,y+s*0.98f,cx,y+s*0.98f);
                path.cubicTo(x+s*0.1f,y+s*0.98f,x,y+s*0.36f,cx,y+s*0.02f);c.drawPath(path,fill);
                fill.setColor(0xFFC9A8F5);c.drawCircle(cx,y+s*0.64f,s*0.13f,fill);fill.setColor(color);break;
            default:star(c,cx,cy,s*0.44f,color);
        }
    }

    /**
     * Battle backdrop texture: a fine diagonal weave and a faint dot grid at low contrast, drifting a few
     * units a second, like the fine patterns behind the 3DS menus.
     */
    static void weave(Canvas c,RectF r,float u,float phase){
        c.save();c.clipRect(r);stroke.setStrokeCap(Paint.Cap.BUTT);stroke.setStrokeWidth(u*0.45f);
        float step=u*4,off=(phase*u*0.3f)%step;
        stroke.setColor(0x0FFFFFFF);for(float x=r.left-r.height()-step+off;x<r.right+step;x+=step)c.drawLine(x,r.bottom,x+r.height(),r.top,stroke);
        stroke.setColor(0x0A000000);for(float x=r.left-step-off;x<r.right+r.height()+step;x+=step)c.drawLine(x,r.top,x-r.height(),r.bottom,stroke);
        fill.setColor(0x10FFFFFF);float d=u*8;
        for(float y=r.top+d/2;y<r.bottom;y+=d)for(float x=r.left+d/2;x<r.right;x+=d)c.drawRect(x-u*0.35f,y-u*0.35f,x+u*0.35f,y+u*0.35f,fill);
        stroke.setStrokeCap(Paint.Cap.ROUND);c.restore();
    }
    /**
     * P*DA hologram texture: a fine teal wire grid with brighter major lines every fourth cell and a
     * slow scan line, on the black screen. The colour argument sets the grid strength.
     */
    static void hexes(Canvas c,RectF r,int color,float u,float phase){
        c.save();c.clipRect(r);float step=u*6,off=(phase*u*0.25f)%(step*4);
        stroke.setStrokeCap(Paint.Cap.BUTT);int a=Math.max(8,(color>>>24));
        int i=0;for(float x=r.left-step*4+off;x<r.right;x+=step,i++){stroke.setStrokeWidth(u*(i%4==0?0.5f:0.3f));stroke.setColor(alpha(TEAL,(i%4==0?a*2.4f:a*1.3f)/255f));c.drawLine(x,r.top,x,r.bottom,stroke);}
        i=0;for(float y=r.top;y<r.bottom;y+=step,i++){stroke.setStrokeWidth(u*(i%4==0?0.5f:0.3f));stroke.setColor(alpha(TEAL,(i%4==0?a*2.4f:a*1.3f)/255f));c.drawLine(r.left,y,r.right,y,stroke);}
        float scan=r.top+((phase*u*2f)%(r.height()+u*20))-u*10;
        fill.setColor(0x00000000);fill.setShader(new LinearGradient(0,scan-u*6,0,scan+u*6,new int[]{0x0035D6D0,0x1835D6D0,0x0035D6D0},null,Shader.TileMode.CLAMP));
        c.drawRect(r.left,scan-u*6,r.right,scan+u*6,fill);fill.setShader(null);
        stroke.setStrokeCap(Paint.Cap.ROUND);c.restore();
    }
    /**
     * Brushed steel plate in XD's manner: a dark keyline, a light-to-dark rim, a body shaded top to
     * bottom with fine horizontal brush streaks and one soft reflection patch. tint 0 is silver; a
     * colour gives Cipher purple or maroon steel.
     */
    static void steel(Canvas c,RectF r,float cut,int tint,float u){
        int hi=tint==0?STEEL_HI:mix(tint,0xFFFFFFFF,0.35f),mid=tint==0?STEEL:tint,lo=tint==0?STEEL_LOW:shade(tint,0.62f);
        Path outer=shape(r,cut);
        c.save();c.translate(0,u);fill.setColor(0x4D000000);c.drawPath(outer,fill);c.restore();
        fill.setColor(0xFF0E0E0E);c.drawPath(outer,fill);
        Path rim=inset(r,cut,u*0.6f);RectF rr=new RectF(scratch);
        fill.setColor(0xFF000000);fill.setShader(new LinearGradient(0,rr.top,0,rr.bottom,mix(hi,0xFFFFFFFF,0.5f),shade(lo,0.7f),Shader.TileMode.CLAMP));c.drawPath(rim,fill);fill.setShader(null);
        Path body=inset(r,cut,u*1.4f);RectF b=new RectF(scratch);
        fill.setShader(new LinearGradient(0,b.top,0,b.bottom,new int[]{hi,mid,lo},new float[]{0,0.45f,1},Shader.TileMode.CLAMP));c.drawPath(body,fill);fill.setShader(null);
        c.save();c.clipPath(body);stroke.setStrokeCap(Paint.Cap.BUTT);
        int seed=(int)(r.left*7+r.top*13);
        for(float y=b.top+u*0.5f;y<b.bottom;y+=u*0.7f){
            seed=seed*1103515245+12345;int v=(seed>>>16)&63;stroke.setStrokeWidth(u*0.3f);
            stroke.setColor(v<32?((v*2)<<24|0xFFFFFF):(((v-32)*2)<<24));c.drawLine(b.left,y,b.right,y,stroke);
        }
        fill.setShader(new RadialGradient(b.left+b.width()*0.28f,b.top+b.height()*0.2f,Math.max(b.width(),b.height())*0.45f,0x30FFFFFF,0x00FFFFFF,Shader.TileMode.CLAMP));
        c.drawRect(b,fill);fill.setShader(null);stroke.setStrokeCap(Paint.Cap.ROUND);c.restore();
    }
    /** Black inset window cut into a plate, with a shadowed top edge and a faint light bottom edge. */
    static void well(Canvas c,RectF r,float cut,float u){
        Path p=shape(r,cut);fill.setColor(INSET);c.drawPath(p,fill);
        c.save();c.clipPath(p);fill.setColor(0x99000000);c.drawRect(r.left,r.top,r.right,r.top+u*1.2f,fill);
        fill.setColor(0x26FFFFFF);c.drawRect(r.left,r.bottom-u*0.6f,r.right,r.bottom,fill);c.restore();
    }
    /** XD porthole cap: a steel ring around a dark disc. */
    static void porthole(Canvas c,float cx,float cy,float d,int tint,float u){
        RectF o=new RectF(cx-d/2,cy-d/2,cx+d/2,cy+d/2);steel(c,o,d/2,tint,u);
        fill.setColor(0xFF161616);c.drawCircle(cx,cy,d/2-u*2.2f,fill);
        stroke.setStrokeWidth(u*0.6f);stroke.setColor(0x40FFFFFF);c.drawCircle(cx,cy+u*0.4f,d/2-u*2.2f,stroke);
    }
    /** A small domed rivet, as on XD's menu card frames. */
    static void rivet(Canvas c,float x,float y,float r){
        fill.setColor(0x80000000);c.drawCircle(x,y+r*0.35f,r,fill);
        fill.setColor(0xFF000000);fill.setShader(new RadialGradient(x-r*0.35f,y-r*0.35f,r*1.3f,0xFFFFFFFF,0xFF7A7A7A,Shader.TileMode.CLAMP));
        c.drawCircle(x,y,r,fill);fill.setShader(null);
    }
    /** XD caption: wide bold caps with open tracking, white (or tinted) with a dark outline. */
    static void caption(Canvas c,String s,float x,float y,float size,int color,int edge,float edgeWidth,float track,Paint.Align align){
        outline.setTypeface(WIDE);outline.setTextSize(size);outline.setColor(edge);outline.setStrokeWidth(edgeWidth);outline.setTextAlign(align);outline.setLetterSpacing(track);
        if(edgeWidth>0)c.drawText(s,x,y,outline);outline.setLetterSpacing(0);
        Paint p=font(WIDE,size,color);p.setTextAlign(align);p.setLetterSpacing(track);c.drawText(s,x,y,p);p.setLetterSpacing(0);p.setTextAlign(Paint.Align.LEFT);
    }
    static float captionWidth(String s,float size,float track){Paint p=font(WIDE,size,TEXT);p.setLetterSpacing(track);float w=p.measureText(s);p.setLetterSpacing(0);return w;}
    // ---- Battle only (pass 5): Colosseum arena, XD machine plates, glints, aura, white cursor. ----

    /** Animation is on unless the system animator scale is zero. */
    static boolean motion(){try{return android.animation.ValueAnimator.areAnimatorsEnabled();}catch(Throwable old){return true;}}
    static float clock(){return (android.os.SystemClock.uptimeMillis()%600000L)/1000f;}

    /**
     * Battle backdrop: a night Colosseum. Deep Cipher-violet sky, two slow spotlight cones, an arena
     * floor in perspective (teal grid fanning to a vanishing point) with two elliptical ring marks.
     */
    static void arena(Canvas c,RectF r,float u,float t){
        float w=r.width(),h=r.height(),hz=r.top+h*0.36f,cx=r.centerX();
        fill.setColor(0xFF000000);fill.setShader(new LinearGradient(0,r.top,0,r.bottom,new int[]{0xFF07060D,0xFF1E1233,0xFF130C22,0xFF06050A},
            new float[]{0,0.36f,0.62f,1},Shader.TileMode.CLAMP));c.drawRect(r,fill);fill.setShader(null);
        c.save();c.clipRect(r);
        // Spotlights, swaying a few degrees.
        for(int i=0;i<2;i++){
            float sx=i==0?r.left+w*0.18f:r.right-w*0.18f,sway=(float)Math.sin(t*0.35f+i*2.1f)*w*0.06f;
            Path cone=new Path();cone.moveTo(sx-u*6,r.top);cone.lineTo(sx+u*6,r.top);cone.lineTo(cx+sway+(i==0?w*0.02f:-w*0.02f)+w*0.12f,r.bottom);
            cone.lineTo(cx+sway+(i==0?w*0.02f:-w*0.02f)-w*0.12f,r.bottom);cone.close();
            fill.setShader(new LinearGradient(0,r.top,0,r.bottom,0x26E8E0FF,0x00E8E0FF,Shader.TileMode.CLAMP));c.drawPath(cone,fill);fill.setShader(null);
        }
        // Arena floor: perspective grid.
        stroke.setStrokeCap(Paint.Cap.BUTT);stroke.setStrokeWidth(u*0.45f);
        for(int i=-12;i<=12;i++){stroke.setColor(alpha(TEAL,0.34f-Math.abs(i)*0.016f));c.drawLine(cx+i*u*3,hz,cx+i*w*0.16f,r.bottom,stroke);}
        float y=hz,step=u*1.6f;
        while(y<r.bottom){stroke.setColor(alpha(TEAL,Math.min(0.32f,0.08f+(y-hz)/h*0.45f)));c.drawLine(r.left,y,r.right,y,stroke);step*=1.22f;y+=step;}
        // Two arena ring marks on the floor, and a horizon glow.
        stroke.setStrokeWidth(u*0.9f);RectF ring=new RectF(cx-w*0.42f,r.bottom-h*0.3f,cx+w*0.42f,r.bottom+h*0.06f);
        stroke.setColor(0x809A5CFF);c.drawOval(ring,stroke);ring.inset(w*0.1f,h*0.05f);stroke.setColor(0x559A5CFF);c.drawOval(ring,stroke);
        fill.setShader(new LinearGradient(0,hz-u*10,0,hz+u*10,new int[]{0x00000000,0x2A6A3A86,0x00000000},null,Shader.TileMode.CLAMP));
        c.drawRect(r.left,hz-u*10,r.right,hz+u*10,fill);fill.setShader(null);
        // Vignette.
        fill.setShader(new RadialGradient(cx,r.top+h*0.5f,Math.max(w,h)*0.75f,0x00000000,0x80000000,Shader.TileMode.CLAMP));c.drawRect(r,fill);fill.setShader(null);
        stroke.setStrokeCap(Paint.Cap.ROUND);c.restore();
    }
    /**
     * Colour inlay set into a steel plate: the colour shades top to bottom, a hard specular band, fine
     * diagonal pinstripes, an inner shadow under the top edge and a dark keyline.
     */
    static void inlay(Canvas c,RectF r,float cut,int base,float u){
        Path p=shape(r,cut);
        fill.setColor(0xFF000000);fill.setShader(new LinearGradient(0,r.top,0,r.bottom,new int[]{mix(base,0xFFFFFFFF,0.18f),base,shade(base,0.62f)},
            new float[]{0,0.5f,1},Shader.TileMode.CLAMP));c.drawPath(p,fill);fill.setShader(null);
        c.save();c.clipPath(p);
        stroke.setStrokeCap(Paint.Cap.BUTT);stroke.setStrokeWidth(u*0.4f);stroke.setColor(0x14FFFFFF);
        for(float x=r.left-r.height();x<r.right;x+=u*3)c.drawLine(x,r.bottom,x+r.height(),r.top,stroke);
        fill.setColor(0x26FFFFFF);c.drawRect(r.left,r.top,r.right,r.top+Math.min(r.height()*0.4f,u*20),fill);
        fill.setColor(0x80000000);c.drawRect(r.left,r.top,r.right,r.top+u*1.4f,fill);
        fill.setColor(0x33FFFFFF);c.drawRect(r.left,r.bottom-u*0.8f,r.right,r.bottom,fill);
        stroke.setStrokeCap(Paint.Cap.ROUND);c.restore();
        stroke.setStrokeWidth(u*0.8f);stroke.setColor(0xB3000000);c.drawPath(p,stroke);
    }
    /** A bright diagonal glint crossing the area once per period; returns true while it needs frames. */
    static void glint(Canvas c,RectF r,float cut,float period,float offset,float u){
        if(!motion())return;
        float t=((clock()+offset)%period)/period;if(t>0.32f)return;
        float k=t/0.32f,e=k*k*(3-2*k),bw=u*22,x=r.left-bw-r.height()+(r.width()+bw*2+r.height())*e;
        c.save();c.clipPath(shape(r,cut));
        Path band=new Path();band.moveTo(x,r.bottom);band.lineTo(x+bw,r.bottom);band.lineTo(x+bw+r.height()*0.6f,r.top);band.lineTo(x+r.height()*0.6f,r.top);band.close();
        fill.setShader(new LinearGradient(x,0,x+bw,0,new int[]{0x00FFFFFF,0x66FFFFFF,0x00FFFFFF},null,Shader.TileMode.CLAMP));c.drawPath(band,fill);fill.setShader(null);
        c.restore();
    }
    /** Shadow aura: soft purple rings breathing outward on a 1.6 s cycle. */
    static void aura(Canvas c,RectF r,float cut,float u){
        float t=motion()?(clock()%1.6f)/1.6f:0.5f,a=0.55f+0.45f*(float)Math.sin(t*Math.PI*2);
        for(int i=3;i>=1;i--){RectF o=new RectF(r.left-u*i*1.3f,r.top-u*i*1.3f,r.right+u*i*1.3f,r.bottom+u*i*1.3f);
            stroke.setStrokeWidth(u*1.4f);stroke.setColor(alpha(SHADOW_AURA,a*(0.5f-i*0.12f)));c.drawPath(shape(o,cut+u*i),stroke);}
    }
    /**
     * XD's selection cursor, following the plate's own clipped-corner outline just inside its edge:
     * a dark keyline, a white line and a faint cyan inner line, pulsing.
     */
    static void cursor(Canvas c,RectF r,float cut,float a,float u){
        RectF o=new RectF(r.left+u*1.6f,r.top+u*1.6f,r.right-u*1.6f,r.bottom-u*1.6f);float k=Math.max(0,cut-u*0.7f);
        stroke.setStrokeJoin(Paint.Join.MITER);
        stroke.setStrokeWidth(u*3.6f);stroke.setColor(alpha(0xFF000000,0.5f*a));c.drawPath(shape(o,k),stroke);
        stroke.setStrokeWidth(u*2f);stroke.setColor(alpha(0xFFFFFFFF,a));c.drawPath(shape(o,k),stroke);
        RectF in=new RectF(o.left+u*1.7f,o.top+u*1.7f,o.right-u*1.7f,o.bottom-u*1.7f);
        stroke.setStrokeWidth(u*0.7f);stroke.setColor(alpha(0xFFBFF6F2,a*0.8f));c.drawPath(shape(in,Math.max(0,k-u*0.7f)),stroke);
        stroke.setStrokeJoin(Paint.Join.ROUND);
    }
    /** XD caption with a navy outline, shrunk to fit a width. */
    static void xdLabel(Canvas c,String s,float x,float y,float max,float min,float width,float track,float edge,Paint.Align align){
        float size=max;while(size>min&&captionWidth(s,size,track)>width)size-=max*0.04f;
        caption(c,s,x,y+size*0.36f,size,TEXT,0xFF14203A,edge,track,align);
    }
    /**
     * Snag Machine arm, an original motif: an armoured forearm with two hinged segments ending in a
     * three-finger claw closing on a ball. Drawn in one colour plus the ball.
     */
    static void snagArm(Canvas c,float x,float y,float s,int color,float u){
        stroke.setColor(color);stroke.setStrokeCap(Paint.Cap.ROUND);stroke.setStrokeJoin(Paint.Join.ROUND);fill.setColor(color);
        Path arm=new Path();arm.moveTo(x,y+s*0.86f);arm.lineTo(x+s*0.22f,y+s*0.86f);arm.lineTo(x+s*0.46f,y+s*0.62f);arm.lineTo(x+s*0.4f,y+s*0.52f);
        arm.lineTo(x+s*0.16f,y+s*0.68f);arm.lineTo(x,y+s*0.68f);arm.close();c.drawPath(arm,fill);
        fill.setColor(shade(color,0.6f));c.drawCircle(x+s*0.44f,y+s*0.58f,s*0.07f,fill);fill.setColor(color);
        stroke.setStrokeWidth(s*0.07f);
        float bx=x+s*0.7f,by=y+s*0.34f;
        c.drawLine(x+s*0.46f,y+s*0.56f,bx-s*0.14f,by+s*0.1f,stroke);
        for(int i=-1;i<=1;i++){double a=Math.toRadians(200+i*55);float fx=bx+(float)Math.cos(a)*s*0.2f,fy=by+(float)Math.sin(a)*s*0.2f;
            Path f=new Path();f.moveTo(bx-s*0.12f,by+s*0.06f);f.quadTo(fx,fy,bx+(float)Math.cos(a+Math.PI*0.55)*s*0.16f,by+(float)Math.sin(a+Math.PI*0.55)*s*0.16f);c.drawPath(f,stroke);}
        ball(c,bx,by,s*0.26f,0xFF8A5CCF,0xFF1B1428,u);
    }
    /**
     * XD battle plate for the header: brushed steel (maroon for the foe, purple for a Shadow Pokemon,
     * silver for yours) with a porthole cap holding the ball mark, the name, status pill, red-orange HP
     * label and bar in a black inset, HP numbers on yours and the Heart Gauge on Shadow Pokemon.
     */
    static void battlePlate(Canvas c,RectF r,XdState.Pokemon p,boolean foe,float u){
        boolean known=p!=null&&p.species!=0,shadow=known&&p.shadowId!=0;
        int tint=shadow?CIPHER:foe?MAROON:0;
        if(shadow)aura(c,r,u*3.5f,u);
        steel(c,r,u*3.5f,tint,u);
        float d=r.height()-u*3,cx=foe?r.right-u*1.5f-d/2:r.left+u*1.5f+d/2,cy=r.centerY();
        porthole(c,cx,cy,d,tint,u);
        RectF well=foe?new RectF(r.left+u*3,r.top+u*3,cx-d/2-u*2,r.bottom-u*3):new RectF(cx+d/2+u*2,r.top+u*3,r.right-u*3,r.bottom-u*3);
        well(c,well,u*2,u);
        if(!known){monSlot(c,cx,cy,d-u*7,false,true,u);caption(c,"—",well.left+u*5,well.centerY()+u*3,u*8,TEXT_3,0,0,0,Paint.Align.LEFT);return;}
        boolean fainted=p.maxHp>0&&p.hp==0;monSlot(c,cx,cy,d-u*7,shadow,fainted,u);
        float x0=well.left+u*4,x1=well.right-u*4;
        String st=statusLabel(p.status,p.hp,p.maxHp);float bh=u*7,bw=badgeWidth(st,bh);
        String numbers=!foe&&p.maxHp>0?p.hp+"/"+p.maxHp:"";float nw=numbers.isEmpty()?0:font(HEAVY,u*7.5f,0).measureText(numbers)+u*3;
        String name=p.name.isEmpty()?"Pokémon":p.name;float size=fitSize(name,HEAVY,u*9,u*6.5f,x1-x0-(bw>0?bw+u*3:0));
        float top=well.top+u*1.2f;
        outlined(c,name,x0,top+size*0.82f,HEAVY,size,TEXT,0xFF000000,u*1f,Paint.Align.LEFT);
        badge(c,st,x1-bw,top+u*0.4f,bh,u);
        float by=well.top+well.height()*0.56f;
        RectF bar=new RectF(x0,by,x1-nw,by+u*3);hpBar(c,bar,p.hp,p.maxHp,true,u);
        if(!numbers.isEmpty())outlined(c,numbers,x1,by+u*3.2f,HEAVY,u*7.5f,TEXT,0xFF000000,u*0.9f,Paint.Align.RIGHT);
        if(shadow){RectF g=new RectF(x0+u*8,by+u*4.6f,Math.min(x1-nw,x0+u*60),by+u*6.4f);heartGauge(c,g,p.heart,p.heartMax,u);}
    }

    /** ORAS-style header tab in XD's P*DA colours: a black plate with a slanted right end and a teal keyline. */
    static void headerTab(Canvas c,RectF r,float u){
        float s=r.height()*0.55f;Path p=new Path();
        p.moveTo(r.left,r.top);p.lineTo(r.right-s,r.top);p.lineTo(r.right,r.bottom);p.lineTo(r.left,r.bottom);p.close();
        c.save();c.translate(0,u);fill.setColor(0x59000000);c.drawPath(p,fill);c.restore();
        fill.setColor(TAB);c.drawPath(p,fill);
        Path k=new Path();float d=u*1.1f;
        k.moveTo(r.left,r.top+d);k.lineTo(r.right-s-d*0.4f,r.top+d);k.lineTo(r.right-d*1.6f,r.bottom-d);k.lineTo(r.left,r.bottom-d);
        stroke.setStrokeWidth(u*0.8f);stroke.setColor(TEAL);stroke.setStrokeJoin(Paint.Join.MITER);c.drawPath(k,stroke);stroke.setStrokeJoin(Paint.Join.ROUND);
    }
    /**
     * P*DA device: a brushed gunmetal body with a top highlight, a black frame with a teal keyline and
     * a bright inner line, and the black hologram screen inside with small clipped corners.
     */
    static void bezel(Canvas c,RectF outer,RectF screenRect,float u){
        fill.setColor(0xFF000000);fill.setShader(new LinearGradient(0,outer.top,0,outer.bottom,0xFF5A5D63,0xFF2A2C31,Shader.TileMode.CLAMP));
        c.drawRect(outer,fill);fill.setShader(null);
        stroke.setStrokeCap(Paint.Cap.BUTT);stroke.setStrokeWidth(u*0.3f);int seed=17;
        for(float y=outer.top+u*0.5f;y<outer.bottom;y+=u*0.7f){seed=seed*1103515245+12345;int v=(seed>>>16)&31;
            stroke.setColor(v<16?((v*3)<<24|0xFFFFFF):(((v-16)*3)<<24));c.drawLine(outer.left,y,outer.right,y,stroke);}
        stroke.setStrokeCap(Paint.Cap.ROUND);
        fill.setColor(0x4DFFFFFF);c.drawRect(outer.left,outer.top,outer.right,outer.top+u*0.8f,fill);
        RectF frame=new RectF(screenRect.left-u*2.2f,screenRect.top-u*2.2f,screenRect.right+u*2.2f,screenRect.bottom+u*2.2f);
        fill.setColor(0x40FFFFFF);c.save();c.translate(0,u*0.7f);c.drawPath(shape(frame,u*5),fill);c.restore();
        fill.setColor(SAND_RIM);c.drawPath(shape(frame,u*5),fill);
        Path scr=shape(screenRect,u*3.5f);
        fill.setColor(0xFF000000);fill.setShader(new LinearGradient(0,screenRect.top,0,screenRect.bottom,SCREEN,SCREEN_DEEP,Shader.TileMode.CLAMP));
        c.drawPath(scr,fill);fill.setShader(null);
        stroke.setStrokeWidth(u*0.8f);stroke.setColor(TEAL);c.drawPath(shape(new RectF(screenRect.left-u*0.9f,screenRect.top-u*0.9f,screenRect.right+u*0.9f,screenRect.bottom+u*0.9f),u*4),stroke);
    }
}
