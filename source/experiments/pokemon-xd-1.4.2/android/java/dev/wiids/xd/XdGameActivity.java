package dev.wiids.xd;

import android.hardware.display.DisplayManager;
import android.os.Bundle;
import android.view.Display;
import dev.wiids.xd.sdl.SDLActivity;
import com.driftdroid.android.dualscreen.WiiDsDisplays;

/** Actual SDL3 host, packaged only with the linked native runtime. */
public final class XdGameActivity extends SDLActivity implements DisplayManager.DisplayListener {
    // Reserve low address space before SDL loads or creates its surface.
    static { System.loadLibrary("wiids_xd_memory"); }
    private DisplayManager displays;
    private XdPresentation companion;
    private boolean resumed;
    private boolean destroying;
    private final android.os.Handler lifecycleHandler=new android.os.Handler(android.os.Looper.getMainLooper());
    private final Runnable reconcile=this::showCompanion;
    private XdSettingsMenu menu;
    @Override protected String[] getLibraries() { return new String[]{"SDL3","wiids_xd_host"}; }
    @Override protected dev.wiids.xd.sdl.SDLSurface createSDLSurface(android.content.Context context) {
        return new XdSurface(context);
    }
    @Override protected String[] getArguments() {
        return new String[]{"--data",XdStorage.root(this).getAbsolutePath(),
            "--libraries",getApplicationInfo().nativeLibraryDir,"--disc",XdStorage.iso(this).getAbsolutePath()};
    }
    @Override public void onCreate(Bundle state) {
        XdStorage.root(this).mkdirs(); XdStorage.saves(this).mkdirs();
        new java.io.File(XdStorage.root(this),"cache").mkdirs();
        super.onCreate(state);
        getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE);
        displays=(DisplayManager)getSystemService(DISPLAY_SERVICE);
        if(displays!=null) displays.registerDisplayListener(this,null);
    }
    private void closeCompanion() {
        XdPresentation old=companion;companion=null;
        if(old!=null) { old.setOnDismissListener(null);old.dismiss(); }
    }
    private void showCompanion() {
        if(!resumed||destroying||isFinishing()||displays==null) return;
        Display display=WiiDsDisplays.companion(this,displays);
        if(display==null) { closeCompanion(); return; }
        if(companion!=null&&companion.isShowing()&&companion.getDisplay().getDisplayId()==display.getDisplayId()) return;
        closeCompanion(); companion=new XdPresentation(this,display,this::openMenu);
        final XdPresentation created=companion;
        created.setOnDismissListener(ignored->{
            if(companion!=created)return;
            companion=null;
            if(resumed&&!destroying&&!isFinishing()){lifecycleHandler.removeCallbacks(reconcile);lifecycleHandler.post(reconcile);}
        });
        try { companion.show(); }
        catch(android.view.WindowManager.InvalidDisplayException|android.view.WindowManager.BadTokenException|SecurityException failure) { closeCompanion(); }
    }
    @Override public void onResume() { super.onResume(); resumed=true; showCompanion(); }
    @Override public void onPause() {
        resumed=false;lifecycleHandler.removeCallbacks(reconcile);closeCompanion();closeMenu();XdNative.suspend();super.onPause();
    }
    @Override public void onDestroy() {
        destroying=true;resumed=false;lifecycleHandler.removeCallbacksAndMessages(null);
        if(menu!=null){menu.dismiss();menu=null;XdNative.menuOpen(false);}
        if(displays!=null) displays.unregisterDisplayListener(this);
        closeCompanion();XdNative.suspend();super.onDestroy();
    }
    @Override public void onDisplayAdded(int id) { showCompanion(); }
    @Override public void onDisplayChanged(int id) { showCompanion(); }
    @Override public void onDisplayRemoved(int id) { showCompanion(); }
    @Override public void onBackPressed(){openMenu();}
    void openMenu(){
        if(menu!=null||!resumed||destroying||isFinishing())return;
        if(!XdNative.menuOpen(true)){android.widget.Toast.makeText(this,"Pokemon XD is still starting.",android.widget.Toast.LENGTH_SHORT).show();return;}
        if(companion!=null)companion.setMenuOpen(true);
        menu=new XdSettingsMenu(this);menu.show();
    }
    void closeMenu(){if(menu==null)return;menu.dismiss();menu=null;XdNative.menuOpen(false);if(companion!=null)companion.setMenuOpen(false);}
    void leaveGame(){
        startActivity(new android.content.Intent().setClassName(this,"com.driftdroid.android.LibraryActivity")
            .addFlags(android.content.Intent.FLAG_ACTIVITY_NEW_TASK|android.content.Intent.FLAG_ACTIVITY_CLEAR_TOP));
        finish();android.os.Process.killProcess(android.os.Process.myPid());
    }
    @Override public void onWindowFocusChanged(boolean focus){if(menu==null||focus)super.onWindowFocusChanged(focus);}
    @Override public boolean dispatchKeyEvent(android.view.KeyEvent event){
        if(menu==null&&(event.getKeyCode()==android.view.KeyEvent.KEYCODE_BACK||event.getKeyCode()==android.view.KeyEvent.KEYCODE_BUTTON_SELECT)){
            if(event.getAction()==android.view.KeyEvent.ACTION_UP)openMenu();return true;
        }
        // Virtual gamepad keys from Android's input tool use the connected controller mapping.
        if(menu==null&&!dev.wiids.xd.sdl.SDLControllerManager.isDeviceSDLJoystick(event.getDeviceId())&&
            (android.view.KeyEvent.isGamepadButton(event.getKeyCode())||
             (event.getKeyCode()>=android.view.KeyEvent.KEYCODE_DPAD_UP&&event.getKeyCode()<=android.view.KeyEvent.KEYCODE_DPAD_CENTER))){
            for(int id:android.view.InputDevice.getDeviceIds())if(dev.wiids.xd.sdl.SDLControllerManager.isDeviceSDLJoystick(id)){
                if(event.getAction()==android.view.KeyEvent.ACTION_DOWN)
                    dev.wiids.xd.sdl.SDLControllerManager.onNativePadDown(id,event.getKeyCode(),event.getScanCode());
                else if(event.getAction()==android.view.KeyEvent.ACTION_UP)
                    dev.wiids.xd.sdl.SDLControllerManager.onNativePadUp(id,event.getKeyCode(),event.getScanCode());
                return true;
            }
        }
        return super.dispatchKeyEvent(event);
    }
}
