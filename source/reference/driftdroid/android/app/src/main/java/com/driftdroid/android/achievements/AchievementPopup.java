package com.driftdroid.android.achievements;

import android.app.Activity;
import android.graphics.*;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * The small WiiDS "Achievement unlocked" card over a game. It lives in the game Activity's own
 * content (no overlay permission, no extra window), never takes focus or touches, and shows one
 * newly recorded unlock at a time in order. Pause freezes it; destruction clears the queue so
 * nothing can appear over a later game. Reusable by any game with a WiiDS set.
 */
public final class AchievementPopup extends View {
    private static final long ENTER = 320, HOLD = 3400, EXIT = 280, GAP = 220;
    private final Handler main = new Handler(Looper.getMainLooper());
    private final ExecutorService worker = Executors.newSingleThreadExecutor(r -> {
        Thread thread = new Thread(r, "AchievementPopup"); thread.setDaemon(true); return thread;
    });
    private final AchievementCatalog.UnlockQueue queue = new AchievementCatalog.UnlockQueue();
    private final Map<String, AchievementCatalog.Item> items = new HashMap<>();
    private final AchievementBadges badges;
    private final AchievementText text;
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final RectF card = new RectF();
    private AchievementCatalog.Item current;
    private Bitmap badge;
    private long startedAt, pausedAt;
    private boolean paused, preparing, closed;

    public AchievementPopup(Activity activity, AchievementText text) {
        super(activity);
        this.text = text != null ? text : new AchievementText.Plain();
        badges = new AchievementBadges(activity);
        setFocusable(false);
        setClickable(false);
        setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);
        activity.addContentView(this, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
    }

    /** Gameplay touches always pass through to the game and its touch controls. */
    @Override public boolean onTouchEvent(MotionEvent event) { return false; }
    @Override public boolean dispatchTouchEvent(MotionEvent event) { return false; }

    /** Newly recorded unlocks only (the ledger's result); a repeated ID is ignored. UI thread. */
    public void enqueue(AchievementCatalog.Item item) {
        if (closed || item == null) return;
        if (queue.offer(item.id)) items.put(item.id, item);
        advance();
    }

    public void pause() {
        if (paused) return;
        paused = true;
        pausedAt = SystemClock.uptimeMillis();
    }

    public void resume() {
        if (!paused) return;
        paused = false;
        startedAt += SystemClock.uptimeMillis() - pausedAt;
        invalidate();
        advance();
    }

    public void close() {
        closed = true;
        queue.clear();
        items.clear();
        current = null;
        main.removeCallbacksAndMessages(null);
        worker.shutdownNow();
        ViewGroup parent = (ViewGroup) getParent();
        if (parent != null) parent.removeView(this);
    }

    private void advance() {
        if (closed || paused || current != null || preparing || queue.isEmpty()) return;
        String id = queue.next();
        final AchievementCatalog.Item item = items.remove(id);
        if (item == null) { advance(); return; }
        preparing = true;
        final int size = Math.max(64, Math.round(112 * scale()));
        worker.execute(() -> {
            Bitmap art = null;
            try { art = badges.badge(item, size); } catch (RuntimeException missing) { /* card shows without a picture */ }
            final Bitmap ready = art;
            main.post(() -> {
                preparing = false;
                if (closed) return;
                current = item;
                badge = ready;
                startedAt = SystemClock.uptimeMillis();
                if (paused) pausedAt = startedAt;
                invalidate();
            });
        });
    }

    private float scale() { return getWidth() > 0 ? getWidth() / 1920f : 1f; }

    @Override protected void onDraw(Canvas canvas) {
        if (current == null) return;
        long now = paused ? pausedAt : SystemClock.uptimeMillis();
        long t = now - startedAt;
        if (t >= ENTER + HOLD + EXIT) {
            current = null;
            badge = null;
            main.postDelayed(this::advance, GAP);
            return;
        }
        float offset, alpha;
        if (t < ENTER) { float k = t / (float) ENTER; offset = 1f - easeOut(k); alpha = k; }
        else if (t < ENTER + HOLD) { offset = 0f; alpha = 1f; }
        else { float k = (t - ENTER - HOLD) / (float) EXIT; offset = k * k; alpha = 1f - k; }

        float s = scale();
        float titleHeight = 44f * s, headHeight = 27f * s, pad = 16f * s, art = 112f * s;
        float textWidth = Math.max(text.width(current.title, titleHeight), text.width("Achievement unlocked", headHeight));
        float w = Math.min(980f * s, Math.max(560f * s, pad * 3 + art + textWidth + 24f * s)), h = 140f * s;
        float left = getWidth() / 2f - w / 2f, top = 26f * s - offset * (h + 40f * s);
        card.set(left, top, left + w, top + h);
        int layer = canvas.saveLayerAlpha(card.left - 12 * s, card.top - 12 * s, card.right + 12 * s, card.bottom + 16 * s, Math.round(255 * alpha));
        drawCard(canvas, s);
        paint.setColor(0xffffffff); // the card's stripes leave a faint alpha on the shared paint
        if (badge != null) canvas.drawBitmap(badge, null, new RectF(left + pad, top + (h - art) / 2f, left + pad + art, top + (h + art) / 2f), paint);
        float x = left + pad * 2 + art;
        float maxText = card.right - pad - x;
        text.draw(canvas, "Achievement unlocked", x, top + 22f * s, headHeight, maxText, false);
        paint.setColor(0xfff2c14e);
        canvas.drawRect(x, top + 22f * s + headHeight + 4f * s, x + Math.min(maxText, text.width("Achievement unlocked", headHeight)), top + 22f * s + headHeight + 7f * s, paint);
        text.draw(canvas, current.title, x, top + 66f * s, titleHeight, maxText, false);
        canvas.restoreToCount(layer);
        postInvalidateOnAnimation();
    }

    /** The launcher's dark banded panel with a gold edge, at popup scale. */
    private void drawCard(Canvas c, float s) {
        float r = 24f * s;
        paint.setShader(null);
        paint.setColor(0x66000000);
        c.drawRoundRect(new RectF(card.left, card.top + 6f * s, card.right, card.bottom + 8f * s), r, r, paint);
        paint.setShader(new LinearGradient(0, card.top, 0, card.bottom, 0xf2202a3a, 0xf20c1118, Shader.TileMode.CLAMP));
        c.drawRoundRect(card, r, r, paint);
        paint.setShader(null);
        int layer = c.save();
        Path clip = new Path(); clip.addRoundRect(card, r, r, Path.Direction.CW); c.clipPath(clip);
        paint.setColor(0x10ffffff);
        for (float y = card.top + 4f * s; y < card.bottom; y += 8f * s) c.drawRect(card.left, y, card.right, y + 3f * s, paint);
        c.restoreToCount(layer);
        Paint edge = new Paint(Paint.ANTI_ALIAS_FLAG);
        edge.setStyle(Paint.Style.STROKE);
        edge.setStrokeWidth(3.5f * s);
        edge.setShader(new LinearGradient(0, card.top, 0, card.bottom, new int[]{0xfffff3b8, 0xfff2c14e, 0xffa86f0e}, null, Shader.TileMode.CLAMP));
        c.drawRoundRect(card, r, r, edge);
    }

    private static float easeOut(float k) {
        float p = k - 1f;
        return 1f + p * p * (2.6f * p + 1.6f);
    }
}
