package dev.wiids.xd;

import com.driftdroid.android.dualscreen.WiiDsPresentation;
import android.content.Context;
import android.os.*;
import android.view.*;

/** Runtime host supplies the existing WiiDS-selected built-in companion display. */
public final class XdPresentation extends WiiDsPresentation {
    private final Handler handler=new Handler(Looper.getMainLooper());
    private XdCompanionView view;
    private boolean started;
    private final XdState.Decoder decoder=new XdState.Decoder();
    
    private final Runnable tick=new Runnable(){public void run(){
        if(!started)return;
        XdState state;
        try {state=decoder.read(XdNative.read(),System.nanoTime()/1000000L);}
        catch(UnsatisfiedLinkError missing){state=XdState.unavailable();}
        view.fieldColor(XdNative.fieldColor());view.render(state);handler.postDelayed(this,100);
    }};
    public XdPresentation(Context context,Display display,Runnable openMenu){super(context,display);}
    public void setMenuOpen(boolean value){if(view!=null)view.setMenuOpen(value);}
    @Override protected void onCreate(Bundle state){
        super.onCreate(state);
        if(getWindow()!=null)getWindow().addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE);
        view=new XdCompanionView(getContext());
        android.widget.FrameLayout layers=new android.widget.FrameLayout(getContext());
        layers.addView(view,new android.widget.FrameLayout.LayoutParams(-1,-1));
        android.view.SurfaceView lower=new android.view.SurfaceView(getContext());
        lower.setClickable(false);lower.setFocusable(false);lower.setFocusableInTouchMode(false);
        lower.setOnTouchListener(new View.OnTouchListener(){
            float downX,downY;boolean active;
            public boolean onTouch(View v,MotionEvent event){
                if(event.getActionMasked()==MotionEvent.ACTION_DOWN){downX=event.getX();downY=event.getY();active=true;return true;}
                if(event.getPointerCount()>1||event.getActionMasked()==MotionEvent.ACTION_CANCEL)active=false;
                if(event.getActionMasked()==MotionEvent.ACTION_UP){
                    if(active&&Math.hypot(event.getX()-downX,event.getY()-downY)<24&&v.getWidth()>0)
                        XdNative.lowerTouch(event.getX()*320f/v.getWidth(),event.getY()*320f/v.getWidth());
                    active=false;
                }return true;
            }
        });
        lower.setZOrderOnTop(true);lower.getHolder().setFormat(android.graphics.PixelFormat.TRANSLUCENT);
        lower.getHolder().addCallback(new android.view.SurfaceHolder.Callback(){
            public void surfaceCreated(android.view.SurfaceHolder h){}
            public void surfaceChanged(android.view.SurfaceHolder h,int format,int width,int height){XdNative.lowerSurface(h.getSurface(),width,height);}
            public void surfaceDestroyed(android.view.SurfaceHolder h){XdNative.lowerSurface(null,0,0);}
        });
        layers.addView(lower,new android.widget.FrameLayout.LayoutParams(-1,-1));setContentView(layers);
    }
    @Override protected void onStart(){super.onStart();started=true;handler.post(tick);}
    @Override protected void onStop(){
        started=false;handler.removeCallbacks(tick);
        XdNative.lowerSurface(null,0,0);
        try {XdNative.suspend();}catch(UnsatisfiedLinkError missing){/* unavailable runtime */}
        super.onStop();
    }
}
