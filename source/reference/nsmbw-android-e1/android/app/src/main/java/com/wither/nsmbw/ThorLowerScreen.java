package com.wither.nsmbw;

import android.app.Activity;
import android.app.Presentation;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.hardware.display.DisplayManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;
import android.view.Display;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import java.io.File;

/** Handheld-style course progress, Star Coins, world overview and physical reserve action. */
final class ThorLowerScreen implements DisplayManager.DisplayListener {
    private static final String TAG = "NSMBW-ThorHUD";
    private final Activity activity;
    private final DisplayManager displays;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private HudPresentation presentation;
    private ThorHudAssets assets;
    private ThorGameState state;
    private ThorWorldMap worldMap;
    private ThorReserveState reserve;
    private boolean running, closed;
    private long lastSequence = -1, lastAdvance, lastLog;
    private String lastIdentity = "";
    private String loggedMap = "";
    private long lastVisual;

    ThorLowerScreen(Activity activity) {
        this.activity = activity;
        displays = activity.getSystemService(DisplayManager.class);
        final File folder = new File(AppFiles.dataDir(activity), "ThorHud");
        new Thread(() -> {
            ThorHudAssets loaded = null;
            try { loaded = new ThorHudAssets(folder); }
            catch (Exception failure) { Log.w(TAG, "Original lower HUD assets unavailable", failure); }
            final ThorHudAssets result = loaded;
            handler.post(() -> {
                if (closed) { if (result != null) result.close(); return; }
                assets = result;
                if (presentation != null) presentation.view.invalidate();
                Log.i(TAG, "Original lower HUD assets " + (result == null ? "unavailable" : "verified"));
            });
        }, "ThorHudAssets").start();
    }

    void start() {
        if (running || closed || displays == null) return;
        running = true;
        state = null; worldMap = null; reserve = null; lastSequence = -1; lastAdvance = SystemClock.uptimeMillis();
        displays.registerDisplayListener(this, handler);
        refresh(); android.view.Choreographer.getInstance().postFrameCallback(poll);
    }

    void stop() {
        if (!running) return;
        running = false;
        displays.unregisterDisplayListener(this);
        android.view.Choreographer.getInstance().removeFrameCallback(poll);
        state = null;
        worldMap = null; reserve = null;
        dismiss();
        Log.i(TAG, "Lower screen released");
    }

    void close() {
        stop(); closed = true;
        if (assets != null) { assets.close(); assets = null; }
    }

    // Read on every display frame: the game publishes its snapshot every frame, so the lower screen
    // shows a change on the next refresh instead of up to a 50 ms poll later.
    private final android.view.Choreographer.FrameCallback poll = new android.view.Choreographer.FrameCallback() {
        @Override public void doFrame(long frameTimeNanos) {
            if (!running || closed) return;
            long now = SystemClock.uptimeMillis();
            ThorGameState next = null;
            try { next = ThorGameState.read(); }
            catch (UnsatisfiedLinkError unavailable) { Log.e(TAG, "Snapshot bridge unavailable", unavailable); stop(); return; }
            if (next != null && next.sequence != lastSequence) {
                lastSequence = next.sequence; lastAdvance = now;
                state = next;
            } else if (now - lastAdvance > 1500) state = null;
            worldMap = state != null && state.scene == 3 ? ThorWorldMap.read() : null;
            reserve = ThorReserveState.read();
            rememberProgress();
            // Redraw only when something the lower screen shows has changed (the snapshot's sequence
            // advances every game frame); running animations schedule their own frames.
            long visual = visualKey();
            if (presentation != null && visual != lastVisual) { presentation.view.acceptMotion(); presentation.view.invalidate(); }
            lastVisual = visual;
            if (state != null) {
                String identity = state.scene + ":" + state.mode + ":" + state.world + ":" + state.stage + ":" + state.fields
                        + ":" + state.starCoins + ":" + state.savedStarCoins
                        + ":" + (worldMap == null ? 0 : worldMap.points.length)
                        + ":" + (reserve == null ? -1 : reserve.item)
                        + ":" + (reserve == null ? 0 : reserve.uses) + ":" + (reserve == null ? 0 : reserve.stores);
                if (!identity.equals(lastIdentity) || (state.has(ThorGameState.COURSE) && now - lastLog > 5000)) {
                    Log.i(TAG, "state seq=" + state.sequence + " fields=" + state.fields + " scene=" + state.scene
                            + " mode=" + state.mode + " world=" + state.world + " course=" + state.stage
                            + " character=" + state.character + " lives=" + state.lives + " p1Coins=" + state.coins
                            + " seconds=" + state.seconds + " progress=" + state.progress
                            + " stars=" + state.starCoins + "/" + state.savedStarCoins + " score=" + state.score
                            + " area=" + state.area + " nodes=" + (worldMap == null ? 0 : worldMap.points.length)
                            + " reserve=" + (reserve == null ? -1 : reserve.item)
                            + " reserveSeq=" + (reserve == null ? -1 : reserve.sequence)
                            + " reserveSlot=" + (reserve == null ? -1 : reserve.context)
                            + " reserveReady=" + (reserve != null && reserve.canUse)
                            + " uses=" + (reserve == null ? 0 : reserve.uses)
                            + " stores=" + (reserve == null ? 0 : reserve.stores));
                    lastIdentity = identity; lastLog = now;
                }
            }
            android.view.Choreographer.getInstance().postFrameCallback(this);
        }
    };

    /** A fingerprint of exactly what the lower screen draws (coins and time stay on the upper screen). */
    private long visualKey() {
        long h = 1469598103934665603L;
        ThorGameState s = state;
        int[] values = s == null ? new int[]{-1} : new int[]{s.fields, s.scene, s.mode, s.world, s.stage, s.character,
                s.lives, s.progress, s.starCoins, s.savedStarCoins, s.score, s.gameStopped ? 1 : 0};
        for (int v : values) h = (h ^ v) * 1099511628211L;
        if (worldMap != null) {
            for (int v : new int[]{worldMap.world, worldMap.submap, worldMap.fields, worldMap.points.length,
                    Float.floatToIntBits(worldMap.playerX), Float.floatToIntBits(worldMap.playerZ)}) h = (h ^ v) * 1099511628211L;
        }
        if (reserve != null) {
            for (int v : new int[]{reserve.item, reserve.canUse ? 1 : 0, reserve.queued ? 1 : 0, reserve.uses, reserve.stores})
                h = (h ^ v) * 1099511628211L;
        }
        return h;
    }

    /** Uses the current save's world flags; old cached maximum-world preferences are ignored. */
    private void rememberProgress() {
        if (presentation != null) presentation.view.companion.setUnlockedWorlds(
                worldMap != null ? worldMap.unlockedWorlds() : 0);
        if (worldMap != null) {
            String id = worldMap.world + ":" + worldMap.submap;
            if (!id.equals(loggedMap)) {
                loggedMap = id;
                StringBuilder names = new StringBuilder();
                for (ThorWorldMap.Point p : worldMap.points) if (p.kind == 1) names.append(p.name).append(' ');
                Log.i(TAG, "map world=" + worldMap.world + " submap=" + worldMap.submap + " unlocked=" + worldMap.unlockedWorlds()
                        + " stops=" + names.toString().trim());
            }
        }
    }

    private void dismiss() {
        HudPresentation old = presentation;
        presentation = null;
        if (old != null) old.dismiss();
    }

    private void refresh() {
        if (!running || closed) return;
        int upper = activity.getWindowManager().getDefaultDisplay().getDisplayId();
        Display target = null;
        for (Display display : displays.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION)) {
            // Thor's internal panel only: never put a HUD on a cast/virtual/HDMI display.
            if (display.isValid() && display.getDisplayId() != upper && "Screen-2".equals(display.getName())) {
                target = display; break;
            }
        }
        if (target != null && presentation != null && presentation.isShowing()
                && presentation.getDisplay().getDisplayId() == target.getDisplayId()) return;
        dismiss();
        if (target == null) return;
        HudPresentation next = new HudPresentation(target);
        presentation = next;
        next.setOnDismissListener(dialog -> { if (presentation == next) presentation = null; });
        try { next.show(); Log.i(TAG, "Lower screen attached display=" + target.getDisplayId()); }
        catch (WindowManager.InvalidDisplayException | WindowManager.BadTokenException | SecurityException unavailable) {
            Log.w(TAG, "Lower screen unavailable", unavailable); dismiss();
        }
    }

    @Override public void onDisplayAdded(int id) { refresh(); }
    @Override public void onDisplayRemoved(int id) { refresh(); }
    @Override public void onDisplayChanged(int id) { refresh(); }

    private final class HudPresentation extends Presentation {
        final HudView view;
        HudPresentation(Display display) { super(activity, display); view = new HudView(getContext()); }
        @Override protected void onCreate(Bundle saved) {
            super.onCreate(saved);
            Window window = getWindow();
            if (window != null) {
                // Touches reach only the stored-item box; never focusable, so the controller keeps
                // driving the game on the upper display.
                window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON
                        | WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE);
                window.getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
            }
            setContentView(view);
            if (window != null) window.setLayout(-1, -1);
        }
    }

    private final class HudView extends View {
        private final Paint fallback = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final ThorCompanion companion = new ThorCompanion();
        HudView(Context context) { super(context); setFocusable(false); setClickable(false); }
        @Override public boolean onTouchEvent(android.view.MotionEvent event) {
            if (event.getActionMasked() != android.view.MotionEvent.ACTION_DOWN) return true;
            if (assets != null && companion.itemTouched(event.getX(), event.getY())) {
                try { GameActivity.nativeUseThorReserve(); } catch (UnsatisfiedLinkError unavailable) { return true; }
                Log.i(TAG, "Stored item touched");
                invalidate();
            }
            return true;
        }
        void acceptMotion() { }
        @Override protected void onDraw(Canvas canvas) {
            super.onDraw(canvas);
            if (assets == null) {
                // A readable missing-assets status; gameplay values never use substitute fonts.
                canvas.drawColor(Color.rgb(221, 227, 233));
                float scale = Math.min(getWidth() / 1240f, getHeight() / 1080f);
                canvas.save();
                canvas.translate((getWidth() - 1240 * scale) / 2, (getHeight() - 1080 * scale) / 2);
                canvas.scale(scale, scale);
                fallback.setColor(Color.DKGRAY); fallback.setTextSize(40); fallback.setTextAlign(Paint.Align.CENTER);
                canvas.drawText("New Super Mario Bros. Wii", 620, 500, fallback);
                fallback.setTextSize(28);
                canvas.drawText("Lower-screen artwork unavailable", 620, 560, fallback);
                canvas.restore(); return;
            }
            companion.draw(canvas, getWidth(), getHeight(), assets, state, worldMap, reserve, SystemClock.uptimeMillis());
            if (isAttachedToWindow() && companion.animating()) postInvalidateOnAnimation();
        }
    }
}
