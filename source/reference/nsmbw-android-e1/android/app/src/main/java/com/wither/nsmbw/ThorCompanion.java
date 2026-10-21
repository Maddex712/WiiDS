package com.wither.nsmbw;

import android.graphics.BitmapShader;
import android.graphics.BlurMaskFilter;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Shader;

/**
 * The lower-screen companion, laid out after New Super Mario Bros. 2's touch screen in NSMB Wii's
 * own art: in a course the course name and lives, a progress track to the goal, the three Star
 * Coins, the score and a tall stored-item box (coins and time stay on the upper screen); on the map the
 * real world graph in a window above NSMB2-style world bubbles; black during the course intro.
 *
 * Pure drawing on a 1240x1080 design canvas (the Thor's lower panel), from the parsed native
 * snapshots. Animation state lives here; nothing reads guest memory or performs input. Values the
 * runtime reports unavailable are left out, never shown as zero or as invented positions.
 */
final class ThorCompanion {
    static final float W = 1240, H = 1080;
    private static final int BACKGROUND = 0xfff4f5f3, STRIPE = 0xffeaecea, PLATE = 0xfffafaf8, EDGE = 0xffc4c7cc,
            TRAY = 0xffeceef0, TRAY_EDGE = 0xffd6d9de, TAB = 0xffe8eaed, TAB_EDGE = 0xffced1d6, TAB_TEXT = 0xff787c84,
            BOX = 0xfff0f2f5, PILL = 0xff7e828a, PILL_EDGE = 0xff6c7078, GROOVE = 0xffd6d8dc, TICK = 0xffaaaeb6,
            GOLD = 0xffffc436, YELLOW = 0xffffd428, HURRY = 0xffff5a46, ROUTE = 0xff6e5c46, BUBBLE_RING = 0xff283478,
            LOCKED = 0xffe8e9eb, LOCKED_EDGE = 0xffced0d4, LOCKED_TEXT = 0xffcdd0d6;

    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint shadow = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint stripes = new Paint();
    private final RectF r = new RectF();

    // Which screen is showing; a change fades the new content in, so a previous course or world
    // is never left on screen.
    private String screen = "";
    private long screenAt, lastFrame;
    // Smoothed markers.
    private float progressShown, mapX, mapZ, progressVelocity;
    // Counter/collection events.
    private int lastCoins, lastLives, lastScore, lastStars, lastItem = -1, lastUses, lastStores;
    private long coinsAt, livesAt, scoreAt, itemInAt, itemOutAt;
    private final long[] starAt = new long[3];
    private int itemOutKind = -1;
    private boolean animating;
    // Where the course's stored-item box is, in design units, while it can be used by touch.
    private final RectF touchBox = new RectF();
    private boolean touchable;
    private float placeX, placeY, placeScale = 1;
    private long pressedAt;
    // Highest world (1-based) this save has reached, from the map; earlier worlds show their bubble.
    private int reachedWorld;
    // Everything that doesn't change on a screen (background, frame, plates and their shadows, the
    // map's route and bubbles) is drawn once into this bitmap; each frame stamps it and draws only
    // the live values on top, so the lower screen costs the game almost nothing.
    private android.graphics.Bitmap base;
    private String baseKey = "";
    private final Paint basePaint = new Paint(Paint.FILTER_BITMAP_FLAG);
    private float routeX, routeZ, routeZoom;   // the map route's placement, fixed with the base

    ThorCompanion() {
        shadow.setMaskFilter(new BlurMaskFilter(10, BlurMaskFilter.Blur.NORMAL));
        // NSMB2's diagonal pinstripes, as a repeating 30x30 tile.
        android.graphics.Bitmap tile = android.graphics.Bitmap.createBitmap(30, 30, android.graphics.Bitmap.Config.ARGB_8888);
        Canvas t = new Canvas(tile);
        t.drawColor(BACKGROUND);
        Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        p.setColor(STRIPE);
        for (int k = -30; k <= 30; k += 30) {
            android.graphics.Path band = new android.graphics.Path();
            band.moveTo(k, 0); band.lineTo(k + 13, 0); band.lineTo(k + 43, 30); band.lineTo(k + 30, 30); band.close();
            t.drawPath(band, p);
        }
        stripes.setShader(new BitmapShader(tile, Shader.TileMode.REPEAT, Shader.TileMode.REPEAT));
    }

    /** A touch at view coordinates (x, y): true when it lands on the usable stored item. */
    boolean itemTouched(float x, float y) {
        if (!touchable) return false;
        float dx = (x - placeX) / placeScale, dy = (y - placeY) / placeScale;
        if (!touchBox.contains(dx, dy)) return false;
        pressedAt = lastFrame; return true;
    }

    /** True while something on screen is moving and wants display-rate redraws. */
    boolean animating() { return animating; }

    /** The highest world this save has reached (1-based; 0 unknown). */
    void setReachedWorld(int world) { reachedWorld = Math.max(0, Math.min(9, world)); }

    void draw(Canvas canvas, int width, int height, ThorHudAssets a, ThorGameState s, ThorWorldMap map,
              ThorReserveState reserve, long now) {
        float dt = lastFrame == 0 ? 0 : Math.min(.1f, (now - lastFrame) / 1000f);
        lastFrame = now;
        animating = false;
        String kind = s == null ? "loading" : s.has(ThorGameState.COURSE) ? "course"
                : s.scene == 3 && map != null ? "map" : s.scene == 7 ? "intro" : "title";
        canvas.drawColor(kind.equals("intro") ? 0xff08080a : BACKGROUND);
        float scale = Math.min(width / W, height / H);
        canvas.save();
        placeX = (width - W * scale) / 2; placeY = (height - H * scale) / 2; placeScale = scale;
        touchable = false;
        canvas.translate(placeX, placeY);
        canvas.scale(scale, scale);
        boolean framed = kind.equals("course") || kind.equals("map");
        if (framed) {
            String key = kind.equals("course") ? "course"
                    : "map:" + map.world + ":" + map.submap + ":" + map.points.length + ":" + Math.max(reachedWorld, map.world + 1);
            if (base == null || !key.equals(baseKey)) {
                if (base == null) base = android.graphics.Bitmap.createBitmap((int) W, (int) H, android.graphics.Bitmap.Config.ARGB_8888);
                base.eraseColor(0);
                Canvas b = new Canvas(base);
                b.drawRect(0, 0, W, H, stripes);
                if (kind.equals("course")) courseBase(b, a); else worldBase(b, a, s, map);
                frame(b);
                baseKey = key;
            }
        } else if (kind.equals("title") || kind.equals("loading")) a.tile(canvas, "mapTile", new RectF(0, 0, W, H), 1.5f, 14);

        String id = kind + ":" + (s == null ? "" : kind.equals("course") ? s.world + ":" + s.stage
                : kind.equals("map") ? map.world + ":" + map.submap : s.scene + ":" + s.mode);
        if (!id.equals(screen)) { screen = id; screenAt = now; baseline(s, map, reserve); }
        float in = ease((now - screenAt) / 320f);
        if (in < 1) animating = true;
        int layer = in < 1 ? canvas.saveLayerAlpha(0, 0, W, H, Math.round(255 * in)) : -1;
        canvas.translate(0, 14 * (1 - in));
        if (framed) canvas.drawBitmap(base, 0, 0, basePaint);
        switch (kind) {
            case "course": course(canvas, a, s, reserve, now, dt); break;
            case "map": world(canvas, a, s, map, reserve, now, dt); break;
            case "intro": break;   // NSMB2: the lower screen rests while the course name shows above
            default: title(canvas, a, s, now); break;
        }
        if (layer >= 0) canvas.restoreToCount(layer);
        canvas.restore();
    }

    /** The screen's own frame, matching the plates: a grey rim with a white inner line. */
    private void frame(Canvas c) {
        fill.setStyle(Paint.Style.STROKE);
        fill.setStrokeWidth(12); fill.setColor(EDGE);
        c.drawRoundRect(12, 12, W - 12, H - 12, 46, 46, fill);
        fill.setStrokeWidth(4); fill.setColor(0xffffffff);
        c.drawRoundRect(19, 19, W - 19, H - 19, 36, 36, fill);
        fill.setStyle(Paint.Style.FILL);
    }

    // ------------------------------------------------------------------ shared pieces

    private void baseline(ThorGameState s, ThorWorldMap map, ThorReserveState reserve) {
        lastCoins = s != null ? s.coins : 0; lastLives = s != null ? s.lives : 0; lastScore = s != null ? s.score : 0;
        lastStars = s != null && s.has(ThorGameState.STAR_COINS) ? s.starCoins : 0;
        java.util.Arrays.fill(starAt, 0); coinsAt = livesAt = scoreAt = itemInAt = itemOutAt = 0;
        progressShown = s != null && s.has(ThorGameState.PROGRESS) ? s.progress / 1000f : 0; progressVelocity = 0;
        if (map != null && map.hasPlayer()) { mapX = map.playerX; mapZ = map.playerZ; }
        lastItem = reserve != null ? reserve.item : -1;
        lastUses = reserve != null ? reserve.uses : 0; lastStores = reserve != null ? reserve.stores : 0;
    }

    /** Text with its left edge at x. */
    private static void left(Canvas c, ThorHudAssets a, String text, float x, float top, float h, String face, int tint) {
        float w = a.textWidth(text, h, face);
        a.text(c, text, x + w / 2, top, h, w + 1, face, tint, 255);
    }

    /** A gentle overshoot pop: 1 + amount, settling back to 1 over ms. */
    private float pop(long at, long now, float amount, float ms) {
        if (at == 0) return 1;
        float t = (now - at) / ms;
        if (t >= 1) return 1;
        animating = true;
        return 1 + amount * (float) Math.sin(t * Math.PI) * (1 - t);
    }

    /**
     * NSMB2's rounded plate: a soft shadow, a border ring, the face and an optional gloss over its
     * upper part.
     */
    private void plate(Canvas c, RectF box, float radius, int face, int edge, float border, boolean drop, int gloss) {
        if (drop) {
            shadow.setColor(0x46000000);
            c.drawRoundRect(box.left, box.top + 6, box.right, box.bottom + 6, radius, radius, shadow);
        }
        fill.setColor(edge); c.drawRoundRect(box, radius, radius, fill);
        fill.setColor(face);
        c.drawRoundRect(box.left + border, box.top + border, box.right - border, box.bottom - border,
                radius - border, radius - border, fill);
        if (gloss != 0) {
            fill.setColor(gloss);
            float g = border + 4;
            c.drawRoundRect(box.left + g, box.top + g, box.right - g, box.top + box.height() * .45f,
                    radius - g, radius - g, fill);
        }
    }

    // ------------------------------------------------------------------ course
    // NSMB2's touch screen in NSMB Wii's art: name and lives, the progress track to the Bowser flag,
    // Star Coins on a tray, the score under its tab and the stored-item box. Coins and time are on top.

    /** The course screen's fixed parts, drawn once into the base. */
    private void courseBase(Canvas c, ThorHudAssets a) {
        plate(c, r(52, 186, 1188, 340), 77, PLATE, EDGE, 6, true, 0x78ffffff);
        fill.setColor(GROOVE); c.drawRoundRect(r(150, 252, 1030, 280), 14, 14, fill);
        fill.setColor(TICK);
        for (int i = 0; i < 13; i++) { float x = 206 + i * 63; c.drawRect(x - 2, 257, x + 2, 275, fill); }
        a.image(c, "goalFlag", 1095, 211, 104);   // the game's Bowser flag marks the goal
        plate(c, r(52, 388, 780, 708), 70, TRAY, TRAY_EDGE, 5, true, 0);
        plate(c, r(180, 744, 650, 832), 34, TAB, TAB_EDGE, 4, false, 0);
        a.text(c, "Score", 415, 756, 60, 420, "message", TAB_TEXT, 255);
        plate(c, r(60, 808, 772, 1004), 90, PLATE, EDGE, 6, true, 0);
        plate(c, r(820, 388, 1188, 1004), 70, BOX, EDGE, 8, true, 0x8cffffff);
    }

    private void course(Canvas c, ThorHudAssets a, ThorGameState s, ThorReserveState reserve, long now, float dt) {
        String name = s.stage >= 0 && s.stage <= 8 ? "WORLD " + (s.world + 1) + "-" + (s.stage + 1) : "WORLD " + (s.world + 1);
        left(c, a, name, 64, 50, 104, "main", YELLOW);
        lives(c, a, s, 866, 52, now);
        progress(c, a, s, now, dt);
        starCoins(c, a, s, now);
        score(c, a, s, now);
        itemBox(c, a, reserve, r(820, 388, 1188, 1004), 250, true, now);
        if (s.gameStopped) {   // pause-related stop: everything settles back, no claims made
            fill.setColor(0x26203448); c.drawRect(0, 0, W, H, fill);
        }
    }

    private void lives(Canvas c, ThorHudAssets a, ThorGameState s, float x, float top, long now) {
        if (!s.has(ThorGameState.PLAYER_ONE)) return;
        if (s.lives != lastLives) { if (lastLives != 0 || s.lives != 0) livesAt = now; lastLives = s.lives; }
        a.image(c, "character" + Math.max(0, Math.min(3, s.character)), x + 45, top, 90);
        float k = pop(livesAt, now, .2f, 360);
        left(c, a, "×" + Math.max(0, Math.min(99, s.lives)), x + 96, top + 6 - 43 * (k - 1), 86 * k, "number", 0xffffffff);
    }

    private void progress(Canvas c, ThorHudAssets a, ThorGameState s, long now, float dt) {
        float x0 = 150, x1 = 1030;
        if (!s.has(ThorGameState.PROGRESS) || s.progress < 0) return;
        float target = Math.max(0, Math.min(1, s.progress / 1000f));
        float before = progressShown;
        // Per-frame data now: only a light smoothing (about 33 ms) remains, against jitter.
        progressShown += (target - progressShown) * (1 - (float) Math.exp(-dt * 30));
        if (Math.abs(target - progressShown) > .0005f) animating = true; else progressShown = target;
        progressVelocity = progressVelocity * .85f + Math.abs(progressShown - before) * 15f;
        float mx = x0 + (x1 - x0) * progressShown;
        fill.setColor(GOLD); c.drawRoundRect(r(x0, 252, Math.max(x0 + 28, mx), 280), 14, 14, fill);
        // The player's head, with a short hop while moving.
        float hop = progressVelocity > .002f ? Math.abs((float) Math.sin(now / 1000.0 * Math.PI * 4)) * 7 : 0;
        if (hop > 0) animating = true;
        if (s.has(ThorGameState.PLAYER_ONE))
            a.image(c, "character" + Math.max(0, Math.min(3, s.character)), mx, 206 - hop, 112);
    }

    private void starCoins(Canvas c, ThorHudAssets a, ThorGameState s, long now) {
        boolean current = s.has(ThorGameState.STAR_COINS);
        boolean saved = s.has(ThorGameState.SAVED_STAR_COINS);
        int got = current ? s.starCoins : 0, before = saved ? s.savedStarCoins : 0;
        if (current) {
            for (int i = 0; i < 3; i++) if ((got & (1 << i)) != 0 && (lastStars & (1 << i)) == 0) starAt[i] = now;
            lastStars = got;
        }
        for (int i = 0; i < 3; i++) {
            float cx = 180 + i * 236, cy = 548;
            boolean have = (got & (1 << i)) != 0, old = (before & (1 << i)) != 0;
            if (!have && !old) { a.image(c, "starCoinOff", cx, cy - 100, 200, current || saved ? 255 : 90); continue; }
            float k = pop(starAt[i], now, .4f, 520);
            if (starAt[i] != 0 && now - starAt[i] < 520) {
                // A brief gold ring opening outward as the coin lands in its slot.
                float t = (now - starAt[i]) / 520f;
                fill.setStyle(Paint.Style.STROKE); fill.setStrokeWidth(8 * (1 - t));
                fill.setColor(GOLD); fill.setAlpha(Math.round(220 * (1 - t)));
                c.drawCircle(cx, cy, 100 + 56 * t, fill);
                fill.setStyle(Paint.Style.FILL); fill.setAlpha(255);
            }
            // Collected on an earlier run (saved) but not this one: the coin, quieter.
            a.image(c, "starCoin", cx, cy - 100 * k, 200 * k, have ? 255 : 120);
        }
    }

    private void score(Canvas c, ThorHudAssets a, ThorGameState s, long now) {
        if (!s.has(ThorGameState.SCORE) || s.score < 0) return;
        if (s.score != lastScore) { if (s.score > lastScore) scoreAt = now; lastScore = s.score; }
        float k = pop(scoreAt, now, .06f, 240);
        a.text(c, String.format(java.util.Locale.ROOT, "%09d", Math.min(999999999, s.score)), 416, 850 - 54 * (k - 1),
                108 * k, 660, "number", 0xffffffff, 255);
    }

    /** The tall stored-item box: original stock-item art, arriving with sparkles and leaving upward. */
    private void itemBox(Canvas c, ThorHudAssets a, ThorReserveState reserve, RectF box, float size, boolean course, long now) {
        float cx = box.centerX(), cy = box.centerY();
        int item = reserve != null ? reserve.item : -1;
        if (reserve != null) {
            if (reserve.uses > lastUses && lastItem >= 0) { itemOutAt = now; itemOutKind = lastItem; }
            if (reserve.stores > lastStores && item >= 0) itemInAt = now;
            lastUses = reserve.uses; lastStores = reserve.stores; lastItem = item;
        }
        if (itemOutAt != 0 && now - itemOutAt < 460) {
            float t = (now - itemOutAt) / 460f; animating = true;
            a.image(c, "reserve" + itemOutKind, cx, cy - size / 2 - 200 * ease(t), size * (1 - .2f * t), Math.round(255 * (1 - t)));
        }
        if (item < 0 || !a.has("reserve" + item)) return;
        // NSMB2: a stored power-up warps into the box as yellow sparkles, then appears.
        long since = itemInAt != 0 ? now - itemInAt : Long.MAX_VALUE;
        if (since < SPARKLE_MS) { animating = true; sparkles(c, cx, cy, since / (float) SPARKLE_MS); return; }
        since -= SPARKLE_MS;
        float k = since < 420 ? 0.2f + .8f * overshoot(since / 420f) : 1;
        if (k != 1) animating = true;
        if (pressedAt != 0 && now - pressedAt < 180) { k *= .9f; animating = true; }   // a touch presses the item in
        a.image(c, "reserve" + item, cx, cy - size * k / 2, size * k);
        if (course && reserve.canUse && !reserve.queued) {
            touchable = true; touchBox.set(box);
            // The physical button, only when the item can actually be dropped right now.
            RectF badge = r(cx - 70, box.bottom - 86, cx + 70, box.bottom - 30);
            plate(c, badge, 28, PILL, PILL_EDGE, 4, false, 0);
            a.text(c, "R3", cx, badge.top + 6, 44, 120, "message", 0xffffffff, 255);
        }
    }

    // ------------------------------------------------------------------ world map
    // NSMB2's map screen: the route in a window, the world bubbles below in a linked zigzag.

    /** The map screen's fixed parts for this world: window, route, bubbles, bottom-row plates. */
    private void worldBase(Canvas c, ThorHudAssets a, ThorGameState s, ThorWorldMap map) {
        int current = map.world >= 0 ? map.world : s.world;   // 0-based
        RectF window = r(44, 40, 1196, 476);
        plate(c, window, 60, PLATE, EDGE, 6, true, 0x6effffff);
        RectF inner = r(window.left + 40, window.top + 30, window.right - 40, window.bottom - 30);
        float spanX = Math.max(1e-3f, map.maxX - map.minX), spanZ = Math.max(1e-3f, map.maxZ - map.minZ);
        float padX = 50, padY = 70;
        routeZoom = Math.min((inner.width() - 2 * padX) / spanX, (inner.height() - 2 * padY) / spanZ);
        routeZoom = Math.min(routeZoom, (inner.width() - 2 * padX) / 6f);   // a two-point world doesn't fill the window
        routeX = inner.centerX() - (map.minX + map.maxX) / 2 * routeZoom;
        routeZ = inner.centerY() + 10 - (map.minZ + map.maxZ) / 2 * routeZoom;
        c.save(); c.clipRect(inner.left - 30, inner.top - 20, inner.right + 30, inner.bottom + 20);
        fill.setStyle(Paint.Style.STROKE); fill.setStrokeCap(Paint.Cap.ROUND);
        fill.setStrokeWidth(13); fill.setColor(ROUTE);
        for (ThorWorldMap.Point p : map.points) for (int id : p.links) {
            if (id <= p.id || id >= map.byId.length || map.byId[id] == null) continue;
            ThorWorldMap.Point o = map.byId[id];
            c.drawLine(routeX + p.x * routeZoom, routeZ + p.z * routeZoom, routeX + o.x * routeZoom, routeZ + o.z * routeZoom, fill);
        }
        fill.setStyle(Paint.Style.FILL);
        // Stops (points named W...) as NSMB2's open circles; unlock/clear state is not claimed.
        for (ThorWorldMap.Point p : map.points) {
            if (p.kind != 1) continue;
            float x = routeX + p.x * routeZoom, y = routeZ + p.z * routeZoom;
            fill.setColor(ROUTE); c.drawCircle(x, y, 27, fill);
            fill.setColor(0xffffffff); c.drawCircle(x, y, 18, fill);
        }
        c.restore();
        bubbles(c, a, current);
        // Bottom row plates: lives, the world's name, the stored item.
        plate(c, r(44, 924, 360, 1034), 54, PILL, PILL_EDGE, 5, false, 0);
        plate(c, r(392, 924, 848, 1034), 54, PLATE, EDGE, 6, false, 0);
        if (current >= 0 && current < 9) a.text(c, "WORLD " + (current + 1), 620, 940, 76, 420, "main", YELLOW, 255);
        plate(c, r(880, 924, 1196, 1034), 54, PILL, PILL_EDGE, 5, false, 0);
        left(c, a, "Item", 916, 948, 62, "message", 0xffffffff);
    }

    /** The map screen's live parts: Mario on the route and on his world, lives, the stored item. */
    private void world(Canvas c, ThorHudAssets a, ThorGameState s, ThorWorldMap map, ThorReserveState reserve, long now, float dt) {
        int current = map.world >= 0 ? map.world : s.world;
        String who = "character" + (s.has(ThorGameState.PLAYER_ONE) ? Math.max(0, Math.min(3, s.character)) : 0);
        if (map.hasPlayer()) {
            float k = 1 - (float) Math.exp(-dt * 30);
            mapX += (map.playerX - mapX) * k; mapZ += (map.playerZ - mapZ) * k;
            if (Math.abs(map.playerX - mapX) + Math.abs(map.playerZ - mapZ) > .01f) animating = true;
            a.image(c, who, routeX + mapX * routeZoom, routeZ + mapZ * routeZoom - 92, 118);
        }
        if (current >= 0 && current < 9) a.image(c, who, bx(current) - 82 + 24, by(current) - 82 - 22, 84);
        if (s.has(ThorGameState.PLAYER_ONE)) {
            a.image(c, who, 117, 940, 78);
            left(c, a, "×" + Math.max(0, Math.min(99, s.lives)), 166, 948, 66, "number", 0xffffffff);
        }
        if (reserve != null && reserve.item >= 0 && a.has("reserve" + reserve.item))
            a.image(c, "reserve" + reserve.item, 1124, 937, 84);
    }

    /** W1..W9 in NSMB2's linked zigzag; reached worlds show their own course picture, others "W?". */
    private void bubbles(Canvas c, ThorHudAssets a, int current) {
        float radius = 82;
        int reached = Math.max(reachedWorld, current + 1);
        fill.setStyle(Paint.Style.STROKE); fill.setStrokeCap(Paint.Cap.ROUND); fill.setStrokeWidth(26); fill.setColor(GOLD);
        for (int i = 0; i < 8; i++) c.drawLine(bx(i), by(i), bx(i + 1), by(i + 1), fill);
        fill.setStyle(Paint.Style.FILL);
        for (int i = 0; i < 9; i++) {
            float x = bx(i), y = by(i);
            shadow.setColor(0x50000000);
            c.drawCircle(x, y + 10, radius + 2, shadow);
            if (i < reached && a.has("course" + (i + 1))) {
                fill.setColor(BUBBLE_RING); c.drawCircle(x, y, radius + 8, fill);
                fill.setColor(0xffffffff); c.drawCircle(x, y, radius + 2, fill);
                a.imageCircle(c, "course" + (i + 1), x, y, radius - 4, 255);
                fill.setColor(0x3cffffff);
                c.drawOval(x - radius + 18, y - radius + 10, x + radius - 18, y - 6, fill);
                a.text(c, "W" + (i + 1), x, y + radius - 70, 64, 2 * radius, "main", 0xffffffff, 255);
            } else {
                fill.setColor(LOCKED_EDGE); c.drawCircle(x, y, radius + 2, fill);
                fill.setColor(LOCKED); c.drawCircle(x, y, radius - 4, fill);
                a.text(c, "W?", x, y - 33, 64, 2 * radius, "main", LOCKED_TEXT, 255);
            }
        }
    }

    private static float bx(int i) { return 114 + i * 124.5f; }
    private static float by(int i) { return i % 2 == 0 ? 600 : 790; }

    // ------------------------------------------------------------------ title, menus, loading

    private void header(Canvas c, ThorHudAssets a, int world, long now) {
        // The course-intro landscape for this world, drifting slowly across its full width.
        String strip = "world" + (world >= 0 && world <= 8 ? world + 1 : 1);
        float pan = .5f + .5f * (float) Math.sin(now / 1000.0 * Math.PI * 2 / 48.0);
        a.strip(c, strip, r(0, 0, W, 136), pan, 255);
        fill.setColor(0x1c2a4a6a); c.drawRect(0, 136, W, 141, fill);
    }

    private void title(Canvas c, ThorHudAssets a, ThorGameState s, long now) {
        header(c, a, 0, now);
        // The same landscape as ground along the bottom, drifting the other way.
        float pan = .5f - .5f * (float) Math.sin(now / 1000.0 * Math.PI * 2 / 48.0);
        fill.setColor(0x1c2a4a6a); c.drawRect(0, 939, W, 944, fill);
        a.strip(c, "world1", r(0, 944, W, 1080), pan, 255);
        float bob = (float) Math.sin(now / 1000.0 * Math.PI * 2 / 3.0) * 8; animating = true;
        fill.setColor(0x22203448); c.drawOval(520, 640, 720, 670, fill);
        if (a.has("heroMario")) a.image(c, "heroMario", 620, 330 + bob, 300);
        else a.image(c, "character0", 620, 380 + bob, 220);
        plate(c, r(110, 700, 1130, 820), 46, PLATE, EDGE, 6, true, 0);
        a.text(c, "New Super Mario Bros. Wii", 620, 726, 70, 960, "main", 0xffffffff, 255);
        if (s == null) {
            // Loading: three coins taking turns, nothing else to promise.
            for (int i = 0; i < 3; i++) {
                float t = (float) ((now / 1000.0 * 1.4 - i * .22) % 1.0);
                float lift = t < .3f ? (float) Math.sin(t / .3f * Math.PI) * 18 : 0;
                a.image(c, "coin", 560 + i * 60, 880 - lift, 50, t < .3f ? 255 : 150);
            }
        }
    }

    // ------------------------------------------------------------------ primitives

    private RectF r(float l, float t, float rr, float b) { r.set(l, t, rr, b); return new RectF(r); }

    private static final long SPARKLE_MS = 380;
    private final android.graphics.Path star = new android.graphics.Path();

    /** Twelve four-point stars spiralling in from around the box to its centre (t 0..1). */
    private void sparkles(Canvas c, float cx, float cy, float t) {
        float in = ease(t);
        for (int i = 0; i < 12; i++) {
            double angle = i * Math.PI / 6 + in * 1.6;
            float radius = (1 - in) * (230 + (i % 3) * 40);
            float x = cx + (float) Math.cos(angle) * radius, y = cy + (float) Math.sin(angle) * radius * .8f;
            float size = (i % 2 == 0 ? 26 : 18) * (1 - .55f * in);
            star.reset();
            star.moveTo(x, y - size); star.quadTo(x, y, x + size, y); star.quadTo(x, y, x, y + size);
            star.quadTo(x, y, x - size, y); star.quadTo(x, y, x, y - size); star.close();
            fill.setColor(i % 3 == 0 ? 0xffffffff : GOLD);
            fill.setAlpha(Math.round(255 * Math.min(1, (1 - t) * 3)));
            c.drawPath(star, fill);
        }
        fill.setAlpha(255);
    }

    private static float ease(float t) { t = Math.max(0, Math.min(1, t)); return t * t * (3 - 2 * t); }

    private static float overshoot(float t) {
        t = Math.max(0, Math.min(1, t)); float s = 1.7f; t -= 1; return t * t * ((s + 1) * t + s) + 1;
    }
}
