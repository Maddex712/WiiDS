package dev.wiids.xd;

import android.app.Dialog;
import android.content.Context;
import android.graphics.*;
import android.graphics.drawable.ColorDrawable;
import android.os.*;
import android.util.TypedValue;
import android.view.*;
import android.widget.*;
import java.util.ArrayList;

/**
 * WiiDS pause/menu window: Back and controller focus; native game-thread settings.
 * Drawn as the XD P*DA (gunmetal device, black hologram screen) over the paused game, in the Gen 6 X-menu pattern: six glossy tiles on
 * the first page, cream setting rows with the value in a navy bar, Return and Back in the dock.
 */
final class XdSettingsMenu extends Dialog {
    private final XdGameActivity host;
    private final Handler handler=new Handler(Looper.getMainLooper());
    private LinearLayout rows;
    private TextView state;
    private int page;
    private int focusIndex;
    private final ArrayList<Button> navigation=new ArrayList<>();
    private int[] values;
    private final ArrayList<Button> settings=new ArrayList<>();
    private final Runnable poll=new Runnable(){public void run(){
        if(!isShowing())return;
        values=XdNative.runtimeRead();
        boolean ready=values!=null&&values.length==13&&values[1]==2;
        state.setText(ready?"Changes save as they apply":"Waiting for a safe pause point");
        for(Button b:settings){b.setEnabled(ready);if(ready)b.setText(settingLabel((Integer)b.getTag()));} if(!navigation.isEmpty()&&navigation.get(focusIndex).isEnabled()&&!navigation.get(focusIndex).isFocused())navigation.get(focusIndex).requestFocus();
        handler.postDelayed(this,100);
    }};
    XdSettingsMenu(XdGameActivity activity){
        super(activity,android.R.style.Theme_Translucent_NoTitleBar_Fullscreen);host=activity;
        setCancelable(false);showPage(0);
        setOnKeyListener((dialog,key,event)->{
            if(key==KeyEvent.KEYCODE_BACK||key==KeyEvent.KEYCODE_BUTTON_B){
                if(event.getAction()==KeyEvent.ACTION_UP){if(page==0)host.closeMenu();else showPage(0);}return true;
            }return false;
        });
    }
    private void focus(int index){
        if(navigation.isEmpty())return;
        focusIndex=Math.max(0,Math.min(navigation.size()-1,index));navigation.get(focusIndex).requestFocus();
    }
    @Override public boolean dispatchKeyEvent(KeyEvent event){
        int key=event.getKeyCode();
        if(key==KeyEvent.KEYCODE_BACK||key==KeyEvent.KEYCODE_BUTTON_B||key==KeyEvent.KEYCODE_BUTTON_SELECT||key==KeyEvent.KEYCODE_BUTTON_START){
            if(event.getAction()==KeyEvent.ACTION_UP){if(page==0)host.closeMenu();else showPage(0);}return true;
        }
        int direction=key==KeyEvent.KEYCODE_DPAD_UP?-2:key==KeyEvent.KEYCODE_DPAD_DOWN?2:key==KeyEvent.KEYCODE_DPAD_LEFT?-1:key==KeyEvent.KEYCODE_DPAD_RIGHT?1:0;
        if(direction!=0){
            if(event.getAction()==KeyEvent.ACTION_DOWN){
                int step=page==0?direction:direction<0?-1:1;focus(focusIndex+step);
            }return true;
        }
        if(key==KeyEvent.KEYCODE_BUTTON_A||key==KeyEvent.KEYCODE_DPAD_CENTER||key==KeyEvent.KEYCODE_ENTER){
            if(event.getAction()==KeyEvent.ACTION_UP&&!navigation.isEmpty())navigation.get(focusIndex).performClick();return true;
        }
        return super.dispatchKeyEvent(event);
    }
    private long axisAt;
    @Override public boolean onGenericMotionEvent(MotionEvent event){
        if((event.getSource()&InputDevice.SOURCE_JOYSTICK)==InputDevice.SOURCE_JOYSTICK){
            float x=event.getAxisValue(MotionEvent.AXIS_X),y=event.getAxisValue(MotionEvent.AXIS_Y);
            long now=SystemClock.uptimeMillis();
            if(now-axisAt>220&&(Math.abs(x)>.6f||Math.abs(y)>.6f)){
                int step=Math.abs(y)>=Math.abs(x)?(y<0?-2:2):(x<0?-1:1);
                if(page!=0)step=step<0?-1:1;focus(focusIndex+step);axisAt=now;
            }return true;
        }return super.onGenericMotionEvent(event);
    }
    private Button button(String label,int kind,Runnable action){
        XdKey b=new XdKey(getContext(),kind);b.pulse=true;b.setFocusable(true);b.setFocusableInTouchMode(true);b.setText(label);b.setOnClickListener(v->action.run());
        switch(label){
            case "Resume":b.look(XdSkin.POKEMON,"play","");break;
            case "Controls":b.look(XdSkin.SNAG,"pad","");break;
            case "Display":b.look(XdSkin.RUN,"display","");break;
            case "Audio":b.look(XdSkin.CALL,"audio","");break;
            case "Game":b.look(XdSkin.FIGHT,"game","");break;
            case "Saves":b.look(XdSkin.BAG,"save","");break;
            case "Return to WiiDS":b.look(XdSkin.UTILITY,"exit","");break;
            case "Back":b.look(XdSkin.BACK,"back","");break;
            default:break;
        }
        rows.addView(b);navigation.add(b);return b;
    }
    private void note(String text){TextView v=new TextView(getContext());v.setText(text);v.setTextColor(XdSkin.TEXT_2);v.setTypeface(XdSkin.REGULAR);rows.addView(v);}
    private void setting(int key){Button b=button("Loading",XdKey.SETTING,()->change(key));b.setTag(key);b.setEnabled(false);settings.add(b);}
    private void showPage(int target){
        page=target;settings.clear();navigation.clear();focusIndex=0;
        ScrollView scroll=new ScrollView(getContext());scroll.setFillViewport(true);scroll.setVerticalScrollBarEnabled(false);
        rows=new Sheet(getContext());rows.setOrientation(LinearLayout.VERTICAL);scroll.addView(rows);
        TextView title=new TextView(getContext());title.setText(new String[]{host.isXg() ? "WiiDS menu · Pokémon XG" : "WiiDS menu","Display","Controls","Saves","Audio","Game"}[page]);title.setTextColor(XdSkin.TEXT);title.setTypeface(XdSkin.HEAVY);rows.addView(title);
        state=new TextView(getContext());state.setTextColor(XdSkin.TEXT_2);state.setTypeface(XdSkin.BOLD);rows.addView(state);
        if(page==0){
            int tile=XdKey.TILE;
            button("Resume",tile,host::closeMenu);button("Controls",tile,()->showPage(2));button("Display",tile,()->showPage(1));
            button("Audio",tile,()->showPage(4));button("Game",tile,()->showPage(5));button("Saves",tile,()->showPage(3));
            button("Return to WiiDS",XdKey.UTILITY,()->{
                int[] v=XdNative.runtimeRead();
                if(v==null||v[0]!=1||v[1]!=2){Toast.makeText(host,"Wait for the safe pause point.",Toast.LENGTH_SHORT).show();return;}
                android.app.AlertDialog confirm=new android.app.AlertDialog.Builder(host).setTitle("Return to WiiDS?")
                    .setMessage("Progress since your last in-game save will be lost.")
                    .setPositiveButton("Return",(d,w)->host.leaveGame()).setNegativeButton("Keep playing",null).create();
                confirm.show();confirm.getWindow().addFlags(WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE);
                final int[] choice={-2};
                confirm.getButton(-2).setFocusableInTouchMode(true);confirm.getButton(-1).setFocusableInTouchMode(true);
                confirm.getButton(-2).requestFocus();
                confirm.setOnKeyListener((d,key,event)->{
                    boolean cancel=key==KeyEvent.KEYCODE_BUTTON_B||key==KeyEvent.KEYCODE_BACK||key==KeyEvent.KEYCODE_BUTTON_SELECT||key==KeyEvent.KEYCODE_BUTTON_START;
                    boolean accept=key==KeyEvent.KEYCODE_BUTTON_A||key==KeyEvent.KEYCODE_DPAD_CENTER||key==KeyEvent.KEYCODE_ENTER;
                    boolean left=key==KeyEvent.KEYCODE_DPAD_LEFT||key==KeyEvent.KEYCODE_DPAD_UP;
                    boolean right=key==KeyEvent.KEYCODE_DPAD_RIGHT||key==KeyEvent.KEYCODE_DPAD_DOWN;
                    if(left||right){if(event.getAction()==KeyEvent.ACTION_DOWN){choice[0]=left?-2:-1;confirm.getButton(choice[0]).requestFocus();}return true;}
                    if(cancel||accept){if(event.getAction()==KeyEvent.ACTION_UP){if(cancel)confirm.dismiss();else confirm.getButton(choice[0]).performClick();}return true;}
                    return false;
                });
            });
        }else if(page==1){setting(2);setting(3);setting(4);setting(5);
            note("16:9 shows more of the scene. Original follows the retail game's update rate.");
        }else if(page==2){
            String battleControls = host.isXg()
                ? "Tap the commands, moves, and Pokémon shown on the lower screen. You can also use the controller."
                : "All commands, moves, items, targets, switching and messages use the original game menus. The lower display shows game information.";
            note("A: confirm / interact\nB: back\nD-pad / left stick: move and choose\nStart: field menu\nBack / Select: WiiDS menu\n\n" + battleControls);
        }else if(page==3){
            note("Use Save in the field menu to save your progress. WiiDS keeps this game's card separate. Return to the library to back up, export or restore it with Save manager. The menu waits for active card operations before pausing.");
        }else if(page==4){setting(6);setting(7);setting(8);setting(9);
        }else if(page==5){setting(10);setting(11);setting(12);note("Hold the right stick button (R3) or Tab to fast-forward. Release it to return to normal speed.");}
        if(page!=0)button("Back",XdKey.ROUND,()->showPage(0));setContentView(scroll);focus(0);
    }
    private String settingLabel(int key){
        int v=values[key];
        switch(key){
            case 2:return "Aspect ratio: "+(v!=0?"16:9":"4:3");
            case 3:return "Native update rate: "+(v!=0?"60 Hz":"Original");
            case 4:return v==7?"Resolution: 1080p (1920x1080)":"Resolution: "+v+"x";
            case 5:return "Show frame rate: "+(v!=0?"On":"Off");
            case 6:return "Master volume: "+v+"%";
            case 7:return "Music: "+v+"%";
            case 8:return "Sound effects: "+v+"%";
            case 9:return "Pokémon cries: "+v+"%";
            case 10:return "Text speed: "+new String[]{"Normal","2x","3x","Instant"}[Math.max(0,Math.min(3,v))];
            case 11:return "Fast battle animations: "+(v!=0?"On":"Off");
            case 12:return "Fast-forward key: "+(v!=0?"On":"Off");
            default:return "Unavailable";
        }
    }
    private void change(int key){
        if(values==null||values[1]!=2)return;int current=values[key],next;
        if(key>=6&&key<=9)next=current>=100?0:Math.min(100,current+10);
        else if(key==4)next=current==3?7:current>=7?1:current+1;
        else if(key==10)next=(current+1)%4;
        else next=current==0?1:0;
        XdNative.runtimeSet(key,next);
    }
    @Override protected void onStart(){super.onStart();
        Window w=getWindow();if(w!=null){w.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));w.setLayout(-1,-1);w.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON|WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE);
            w.setDecorFitsSystemWindows(false);WindowInsetsController c=w.getInsetsController();if(c!=null)c.hide(WindowInsets.Type.systemBars());}
        handler.post(poll);
    }
    @Override protected void onStop(){handler.removeCallbacks(poll);super.onStop();}

    /**
     * Lays the menu's own children out on the P*DA device centred over the dimmed game, as the Gen 6
     * X menu sits over the lower-screen device: title in the black header tab, tiles in a 2x3 grid, cream
     * setting rows in one column, notes in a dark message box, Return to WiiDS as a grey utility at the
     * dock's left, Back as the blue U-turn at its right and the pause state between them. Opens with a
     * short scale-and-fade pop.
     */
    static final class Sheet extends LinearLayout {
        private float u=1;
        private long opened;
        private final RectF panel=new RectF(),screen=new RectF(),tab=new RectF(),dock=new RectF(),notes=new RectF(),r=new RectF();
        Sheet(Context c){super(c);setWillNotDraw(false);}
        @Override protected void onMeasure(int w,int h){
            int width=MeasureSpec.getSize(w),height=MeasureSpec.getMode(h)==MeasureSpec.UNSPECIFIED?Math.round(width*9f/16):MeasureSpec.getSize(h);
            frame(width,height);place(false);setMeasuredDimension(width,height);
        }
        @Override protected void onLayout(boolean changed,int l,int t,int rr,int b){frame(rr-l,b-t);place(true);}
        /** The menu opens on the game display, so the panel takes the 3DS upper-screen shape (400 x 250 units). */
        private void frame(int width,int height){
            float ph=height*0.9f,pw=Math.min(width*0.92f,ph*1.6f);ph=Math.min(ph,pw/1.6f);
            panel.set((width-pw)/2,(height-ph)/2,(width+pw)/2,(height+ph)/2);u=ph/250f;
            screen.set(panel.left+u*8,panel.top+u*36,panel.right-u*8,panel.bottom-u*48);
            dock.set(panel.left,panel.bottom-u*44,panel.right,panel.bottom);
        }
        private void place(boolean layout){
            float m=u*8,left=screen.left+m,right=screen.right-m,top=screen.top+m,bottom=screen.bottom-m;
            ArrayList<XdKey> tiles=new ArrayList<>(),list=new ArrayList<>(),dockLeft=new ArrayList<>(),dockRight=new ArrayList<>();
            ArrayList<TextView> texts=new ArrayList<>();
            for(int i=2;i<getChildCount();i++){
                View v=getChildAt(i);
                if(v instanceof XdKey){
                    XdKey k=(XdKey)v;k.u=u;
                    if(k.kind==XdKey.TILE)tiles.add(k);else if(k.kind==XdKey.SETTING)list.add(k);
                    else if(k.kind==XdKey.UTILITY)dockLeft.add(k);else dockRight.add(k);
                }else if(v instanceof TextView)texts.add((TextView)v);
            }
            TextView title=(TextView)getChildAt(0),line=(TextView)getChildAt(1);
            size(title,u*9.5f);size(line,u*8.5f);for(TextView n:texts)size(n,u*9f);
            // The title keeps its TextView (tests read it); it is drawn in the tab with XD's open tracking.
            title.setLetterSpacing(0.1f);title.setTextColor(0xFFDDF8F6);title.setTypeface(XdSkin.WIDE);
            tab.set(panel.left,panel.top+u*6,panel.left+u*54+XdSkin.captionWidth(String.valueOf(title.getText()),u*9.5f,0.1f),panel.top+u*27);
            put(title,panel.left+u*28,tab.top,tab.right-u*10,tab.bottom,layout);title.setGravity(android.view.Gravity.CENTER_VERTICAL);
            float db=u*30,dy=dock.centerY()-db/2,lx=dock.left+u*10,rx=dock.right-u*10;
            for(XdKey k:dockLeft){put(k,lx,dy,lx+u*118,dy+db,layout);lx+=u*124;}
            for(int i=dockRight.size()-1;i>=0;i--){XdKey k=dockRight.get(i);put(k,rx-db,dy,rx,dy+db,layout);rx-=db+u*6;}
            put(line,lx+u*4,dock.top,rx-u*4,dock.bottom,layout);
            line.setGravity(dockLeft.isEmpty()?android.view.Gravity.CENTER_VERTICAL|android.view.Gravity.START:android.view.Gravity.CENTER);
            float y=top,gap=u*5;
            if(!tiles.isEmpty()){
                int rowsN=(tiles.size()+1)/2;float th=(bottom-y-gap*(rowsN-1))/rowsN,tw=(right-left-gap)/2;
                for(int i=0;i<tiles.size();i++){float x=left+(i%2)*(tw+gap),yy=y+(i/2)*(th+gap);put(tiles.get(i),x,yy,x+tw,yy+th,layout);}
                y+=rowsN*(th+gap);
            }
            // Setting rows fill the left column; a help note sits beside them, or fills the screen alone.
            float rowsRight=texts.isEmpty()||list.isEmpty()?right:left+(right-left)*0.62f;
            if(!list.isEmpty()){
                float rh=Math.min(u*32,(bottom-y-gap*(list.size()-1))/list.size());
                for(XdKey k:list){put(k,left,y,rowsRight,y+rh,layout);y+=rh+gap;}
            }
            notes.setEmpty();
            if(!texts.isEmpty()){
                notes.set(list.isEmpty()?left:rowsRight+gap,top,right,bottom);
                for(TextView n:texts){put(n,notes.left+u*12,notes.top+u*10,notes.right-u*12,notes.bottom-u*10,layout);}
            }
        }
        private void size(TextView v,float px){if(Math.abs(v.getTextSize()-px)>0.5f)v.setTextSize(TypedValue.COMPLEX_UNIT_PX,px);}
        private void put(View v,float l,float t,float rr,float b,boolean layout){
            int L=Math.round(l),T=Math.round(t),R=Math.round(rr),B=Math.round(b);
            v.measure(MeasureSpec.makeMeasureSpec(Math.max(0,R-L),MeasureSpec.EXACTLY),MeasureSpec.makeMeasureSpec(Math.max(0,B-T),MeasureSpec.EXACTLY));
            if(layout)v.layout(L,T,R,B);
        }
        @Override protected void dispatchDraw(Canvas c){
            long now=SystemClock.uptimeMillis();if(opened==0)opened=now;
            float t=Math.min(1f,(now-opened)/180f),e=1f-(1f-t)*(1f-t);
            c.drawColor(XdSkin.alpha(0xFF000000,0.55f*e));
            int save=c.saveLayerAlpha(panel.left-u*16,panel.top-u*4,panel.right+u*4,panel.bottom+u*4,(int)(255*e));
            float s=0.92f+0.08f*e;c.scale(s,s,panel.centerX(),panel.centerY());
            // The P*DA device: gunmetal bezel, black hologram screen with teal wire grid, dock band, header tab.
            c.save();c.clipPath(XdSkin.shape(panel,u*6));
            XdSkin.bezel(c,panel,screen,u);
            XdSkin.hexes(c,screen,0x16FFFFFF,u,0);
            XdSkin.fill.setColor(0xFF000000);XdSkin.fill.setShader(new LinearGradient(0,dock.top,0,dock.bottom,0xFF3A3D43,XdSkin.DOCK,Shader.TileMode.CLAMP));
            c.drawRect(dock,XdSkin.fill);XdSkin.fill.setShader(null);
            XdSkin.fill.setColor(0x33FFFFFF);c.drawRect(dock.left,dock.top,dock.right,dock.top+u*0.8f,XdSkin.fill);
            c.restore();
            if(!notes.isEmpty())XdSkin.message(c,notes,u*3,u);
            XdSkin.headerTab(c,tab,u);
            XdSkin.glyph(c,"pda",panel.left+u*10,tab.centerY()-u*7,u*14,XdSkin.SAND_TOP,u);
            super.dispatchDraw(c);
            c.restoreToCount(save);
            if(t<1)postInvalidateOnAnimation();
        }
    }
}
