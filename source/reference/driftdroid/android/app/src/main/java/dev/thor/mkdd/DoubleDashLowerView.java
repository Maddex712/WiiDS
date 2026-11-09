package dev.thor.mkdd;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;
import android.view.View;
import java.io.File;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * Claude's Double Dash "duo screen": standings, course map and the owner's kart, drawn in the
 * game's own HUD material. Every letter, number, portrait window, item, map and P1 tag is a texture
 * from the owner's disc ({@link DuoArt}), recoloured the way the game's J2DPictures are; the only
 * WiiDS chrome is the quiet graphite ground, one raised row and hairlines. Draws owned LowerState
 * snapshots only.
 */
public final class DoubleDashLowerView extends View implements LowerScreenController.Renderer {
    private static final int BG=0xff121214, RAISED=0xff1d1d20, HAIR=0x12ffffff, EDGE=0x1affffff,
        PAPER=0xfff2efe6, MUTED=0xff8b8b92, DIM=0xb8000000,
        P1=0xffff5a5a,                                    // Race2D::mPlayerNumberColor[0]
        LAP_TOP=0xffd4f57a, LAP_BOTTOM=0xff4cb83c,         // sampled from the top screen's own HUD
        TIME_TOP=0xfffff4b0, TIME_BOTTOM=0xfff0a030,
        RANK_TOP=0xfffff6c8, RANK_BOTTOM=0xffd8b858,
        WINDOW_BLACK=0xff101a9c, WINDOW_WHITE=0xffc4c8d8;
    private static final int INK=DuoLettering.INK, LEFT=DuoLettering.LEFT, CENTER=DuoLettering.CENTER,
        RIGHT=DuoLettering.RIGHT;

    private final Paint fill=new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint image=new Paint(Paint.ANTI_ALIAS_FLAG|Paint.FILTER_BITMAP_FLAG);
    private final RectF rect=new RectF();
    private final Path clip=new Path();
    private final File folder;
    private final Handler main=new Handler(Looper.getMainLooper());
    private final ExecutorService loader=Executors.newSingleThreadExecutor(r -> {
        Thread t=new Thread(r, "MkddLowerArt"); t.setDaemon(true); return t;
    });
    private final DuoRowMotion motion=new DuoRowMotion();
    private final DuoLettering type=new DuoLettering();
    private DuoScene scene=DuoScene.of(LowerState.unavailable());
    private DuoLayout layout;
    private DuoArt art;
    private DuoArt.CourseMap map;
    private MapTransform transform;
    private DuoLayout.Box mapBox;
    private int mapCourse=-1, mapEpoch, describedKey=Integer.MIN_VALUE;
    private boolean mapLoading, artLoading, closed, held;
    private long artRetryAt;

    /** [artFolder] holds duo_hud.json and its PNGs (MkddArt / design/stage_art.py output). */
    public DoubleDashLowerView(Context context, File artFolder) {
        super(context);
        folder=artFolder;
        setFocusable(false); setClickable(false);
        setContentDescription("Mario Kart Double Dash lower screen");
        loadArt();
    }

    /** The art may still be building when the game starts: retried every few seconds until it loads. */
    private void loadArt() {
        artLoading=true;
        loader.execute(() -> {
            DuoArt loaded=DuoArt.load(folder);
            main.post(() -> {
                artLoading=false;
                if (closed) return;
                if (loaded == null || (loaded.hud.isEmpty() && art != null)) { artRetryAt=SystemClock.uptimeMillis()+3000; return; }
                if (loaded.hud.isEmpty()) artRetryAt=SystemClock.uptimeMillis()+3000;
                art=loaded; type.setArt(loaded);
                if (map == null && !mapLoading) mapCourse=-1;  // fetch this course's map again
                invalidate();
            });
        });
    }

    @Override public View view() { return this; }

    @Override public void render(LowerState state) {
        if (closed) return;
        if ((art == null || art.hud.isEmpty()) && !artLoading && SystemClock.uptimeMillis() >= artRetryAt) loadArt();
        DuoScene next=DuoScene.of(state);
        boolean redraw=next.kind != scene.kind || next.state.frame != scene.state.frame
                || next.state.available != scene.state.available;
        if (next.kind == DuoScene.IDLE) motion.reset();
        else {
            long now=SystemClock.uptimeMillis()*1_000_000L;
            for (int i=0; i < next.standings.size(); i++) redraw|=motion.update(next.standings.get(i).id, i, now);
            requestMap(next.state.mapKey());
        }
        scene=next;
        describe();
        if (redraw) invalidate();
    }

    /** The WiiDS menu is open over the frozen game: keep this frame, dimmed. */
    @Override public void hold(boolean held) {
        if (this.held == held) return;
        this.held=held; invalidate();
    }

    @Override public void close() {
        if (closed) return;
        closed=true; mapEpoch++;
        loader.shutdownNow(); main.removeCallbacksAndMessages(null);
        type.setArt(null);
        art=null; map=null; transform=null;
    }

    @Override protected void onSizeChanged(int w, int h, int oldW, int oldH) {
        layout=new DuoLayout(w, h);
        type.place(layout.left, layout.top, layout.scale);
        placeMap();
    }

    private void requestMap(int courseId) {
        if (courseId == mapCourse) return;
        mapCourse=courseId; map=null; transform=null; mapBox=null;
        int token=++mapEpoch;
        mapLoading=courseId >= 0;
        if (!mapLoading) return;
        loader.execute(() -> {
            DuoArt.CourseMap loaded=DuoArt.loadMap(folder, courseId);
            main.post(() -> {
                if (closed || token != mapEpoch) {
                    if (loaded != null) { loaded.image.recycle(); if (loaded.overview != null) loaded.overview.recycle();
                        if (loaded.title != null) loaded.title.recycle(); }
                    return;
                }
                map=loaded; mapLoading=false; placeMap(); invalidate();
            });
        });
    }

    /** The map's drawn part fills the map area; markers share the same rectangle. */
    private void placeMap() {
        transform=null; mapBox=null;
        if (map == null || layout == null || layout.scale <= 0) return;
        try {
            // Texture axes, not world X/Z; mirror=false (mirror races hide the map, see drawMap).
            mapBox=DuoLayout.contentFit(map.maxU-map.minU, map.maxV-map.minV,
                    map.drawnL, map.drawnT, map.drawnR, map.drawnB, layout.mapArea);
            transform=new MapTransform(map.minU, map.minV, map.maxU, map.maxV,
                    mapBox.l, mapBox.t, mapBox.w(), mapBox.h(), false);
        } catch (IllegalArgumentException e) {
            Log.w(DuoArt.TAG, "Rejected map bounds for course "+mapCourse, e); transform=null; mapBox=null;
        }
    }

    private void describe() {
        LowerState.Kart f=scene.focus;
        int key=scene.kind*100000+(f == null ? 0 : f.rank*1000)+scene.lap*10+(scene.state.paused ? 1 : 0);
        if (key == describedKey) return;
        describedKey=key;
        StringBuilder s=new StringBuilder("Mario Kart Double Dash lower screen. ");
        if (scene.kind == DuoScene.IDLE) s.append("Race information appears here during a race.");
        else {
            if (f != null && f.rank >= 1) s.append(scene.focusIsPlayer ? "You are " : "Leader ").append(f.rank)
                    .append(DuoScene.ordinal(f.rank)).append(" of ").append(scene.state.karts.size()).append(". ");
            if (scene.lap >= 1) s.append("Lap ").append(scene.lap).append(" of ").append(scene.state.totalLaps).append(". ");
            if (scene.state.paused) s.append("Paused.");
        }
        setContentDescription(s);
    }

    @Override protected void onDraw(Canvas c) {
        DuoLayout L=layout;
        if (L == null || L.scale <= 0) return;
        c.drawColor(BG);
        if (scene.kind == DuoScene.IDLE) { drawIdle(c); return; }
        drawHeader(c);
        if (scene.kind == DuoScene.TIME_ATTACK) drawSolo(c); else drawBoard(c);
        drawSide(c);
        if (scene.state.paused || held) {
            fill.setColor(DIM); c.drawRect(0, 0, getWidth(), getHeight(), fill);
            letters(c, "Paused", 620, 500, 72, PAPER, PAPER, CENTER, 0);
        }
    }

    // ---- regions ----

    /** The game's own LAP and TIME, in the same lettering and tints as the top screen. */
    private void drawHeader(Canvas c) {
        LowerState s=scene.state;
        if (scene.kind != DuoScene.BATTLE && scene.lap >= 1 && s.totalLaps >= 1) {
            float x=168;
            if (!picture(c, hud("lap", INK, LAP_TOP, LAP_BOTTOM), 32, 44, 128, 64))
                x=letters(c, "Lap", 32, 52, 48, LAP_TOP, LAP_BOTTOM, LEFT, 0)+16;
            float r=letters(c, Integer.toString(scene.lap), x, 28, 80, LAP_TOP, LAP_BOTTOM, LEFT, 0);
            letters(c, "/"+s.totalLaps, r-4, 64, 44, LAP_TOP, LAP_BOTTOM, LEFT, 0);
        }
        LowerState.Kart f=scene.focus;
        int ms=f == null ? -1 : f.timeMs;
        if (ms < 0 && scene.kind == DuoScene.BATTLE) return;
        String clock=DuoScene.hudTime(ms);
        float left=1208-width(clock, 52);
        picture(c, hud("time", INK, TIME_TOP, TIME_BOTTOM), left-116, 46, 104, 52);
        letters(c, clock, 1208, 44, 52, TIME_TOP, TIME_BOTTOM, RIGHT, 0);
    }

    private void drawBoard(Canvas c) {
        List<LowerState.Kart> rows=scene.standings;
        DuoLayout L=layout;
        fill.setColor(HAIR);
        for (int i=1; i < rows.size(); i++) {
            float y=L.y(DuoLayout.rowTop(i));
            c.drawRect(L.x(96), y, L.x(584), y+Math.max(1, L.px(1)), fill);
        }
        // The owner's row draws last so it stays on top while rows slide past it.
        for (LowerState.Kart k : rows) if (!(scene.focusIsPlayer && scene.isFocus(k))) drawRow(c, k, false);
        if (scene.focusIsPlayer) drawRow(c, scene.focus, true);
    }

    /** Rank, the game's paired portrait windows (back rider large), names, then held items only. */
    private void drawRow(Canvas c, LowerState.Kart k, boolean me) {
        DuoLayout L=layout;
        float t=DuoLayout.rowTop(motion.slot(k.id));
        if (me) {
            rect.set(L.x(16), L.y(t+4), L.x(592), L.y(t+4+DuoLayout.ROW));
            fill.setColor(RAISED); c.drawRoundRect(rect, L.px(16), L.px(16), fill);
            fill.setColor(EDGE); fill.setStyle(Paint.Style.STROKE); fill.setStrokeWidth(Math.max(1, L.px(1.5f)));
            c.drawRoundRect(rect, L.px(16), L.px(16), fill); fill.setStyle(Paint.Style.FILL);
        }
        if (k.rank >= 1) letters(c, Integer.toString(k.rank), 56, t+30, 52, RANK_TOP, RANK_BOTTOM, CENTER, 0);
        int back=k.passengerCharacter(), drive=k.driverCharacter();
        window(c, back, 96, t+12, 84);
        window(c, drive, 156, t+48, 56);
        Bitmap usable=item(DuoScene.usableItem(k), back), waiting=item(DuoScene.driverItem(k), drive);
        float right=waiting != null ? 440 : usable != null ? 496 : 584;
        tagged(c, DuoNames.character(back), me && scene.focusIsPlayer, 232, t+26, 30, right);
        String driver=DuoNames.character(drive);
        if (driver != null) letters(c, driver, 232, t+64, 22, MUTED, MUTED, LEFT, right-232);
        if (usable != null) contain(c, usable, 504, t+16, 80);
        if (waiting != null) contain(c, waiting, 448, t+48, 48);
    }

    /** Time trial: one kart, so its riders and both items get the column. */
    private void drawSolo(Canvas c) {
        LowerState.Kart f=scene.focus;
        if (f == null) { letters(c, "No kart to follow", 304, 560, 28, MUTED, MUTED, CENTER, 0); return; }
        int back=f.passengerCharacter(), drive=f.driverCharacter();
        window(c, back, 48, 160, 224);
        window(c, drive, 208, 288, 144);
        tagged(c, DuoNames.character(back), scene.focusIsPlayer, 48, 480, 44, 576);
        String driver=DuoNames.character(drive);
        if (driver != null) letters(c, driver, 48, 540, 30, MUTED, MUTED, LEFT, 528);
        Bitmap usable=item(DuoScene.usableItem(f), back), waiting=item(DuoScene.driverItem(f), drive);
        if (usable != null) contain(c, usable, 48, 640, 160);
        if (waiting != null) contain(c, waiting, 240, 704, 96);
    }

    private void drawSide(Canvas c) {
        LowerState s=scene.state;
        String course=DuoNames.course(s.courseId), mode=s.demo ? "Demo" : DuoNames.mode(s.mode);
        if (map != null && map.title != null) {
            float k = Math.min(568f/map.title.getWidth(), 96f/map.title.getHeight());
            float w = map.title.getWidth()*k, h = map.title.getHeight()*k;
            picture(c, map.title, 916-w*.5f, 104+(96-h)*.5f, w, h);
          } else if (course != null && scene.state.courseSet == 0 && art != null && !art.courseNames)
            letters(c, course, 916, 136, 34, PAPER, PAPER, CENTER, 576);
        if (mode != null) letters(c, mode, 916, map != null && map.title != null ? 206 : course == null ? 144 : 180,
                22, MUTED, MUTED, CENTER, 576);
        drawMap(c);
    }

    /** The one bright element: the course's own minimap with the game's map icons and P1 tag. */
    private void drawMap(Canvas c) {
        DuoLayout L=layout;
        if (map == null || transform == null || mapBox == null || scene.state.mirror) {
            // Mirror placement (art and markers together) has not been checked against a real race.
            if (scene.state.mirror && map != null) {
                picture(c, hud("mirror", INK, PAPER, PAPER), 788, 552, 256, 64);
                letters(c, "No map in mirror mode", 916, 640, 26, MUTED, MUTED, CENTER, 560);
            } else {
                letters(c, mapCourse < 0 ? "Course not known" : mapLoading ? "Loading map" : "No map for this course",
                        916, 600, 28, MUTED, MUTED, CENTER, 560);
            }
            return;
        }
        if (map.overview != null) {
            // The course from above, in the same rectangle as the minimap so the markers stay on it;
            // its surroundings are cropped to the map area.
            // The frame hugs the picture: the map area, less any part the picture doesn't reach.
            DuoLayout.Box m=L.mapArea;
            DuoLayout.Box a=new DuoLayout.Box(Math.max(m.l, mapBox.l), Math.max(m.t, mapBox.t),
                    Math.min(m.r, mapBox.r), Math.min(m.b, mapBox.b));
            rect.set(a.l, a.t, a.r, a.b);
            float r=L.px(16);
            clip.reset(); clip.addRoundRect(rect, r, r, Path.Direction.CW);
            c.save(); c.clipPath(clip);
            rect.set(mapBox.l, mapBox.t, mapBox.r, mapBox.b);
            c.drawBitmap(map.overview, null, rect, image);
            c.restore();
            rect.set(a.l, a.t, a.r, a.b);
            fill.setColor(EDGE); fill.setStyle(Paint.Style.STROKE); fill.setStrokeWidth(Math.max(1, L.px(1.5f)));
            c.drawRoundRect(rect, r, r, fill); fill.setStyle(Paint.Style.FILL);
        } else {
            rect.set(mapBox.l, mapBox.t, mapBox.r, mapBox.b);
            image.setAlpha(235); c.drawBitmap(map.image, null, rect, image); image.setAlpha(255);
        }
        c.save(); c.clipRect(L.side.l, L.side.t, L.side.r, L.side.b);
        List<LowerState.Kart> rows=scene.standings;
        for (int i=rows.size()-1; i >= 0; i--) if (!scene.isFocus(rows.get(i))) marker(c, rows.get(i), false);
        if (scene.focus != null) marker(c, scene.focus, scene.focusIsPlayer);
        c.restore();
    }

    private void marker(Canvas c, LowerState.Kart k, boolean me) {
        if (!k.positionKnown) return;
        DuoMapAxes axes=map.axes;
        MapTransform.Point p=transform.project(axes.u(k.x, k.z), axes.v(k.x, k.z), axes.heading(k.headingRadians));
        if (p == null) return; // Off the verified map: omitted, never clamped to an edge.
        DuoLayout L=layout;
        float x=(float) p.x, y=(float) p.y;
        if (me) {
            Bitmap tag=hud("p1_map", INK, P1, P1);
            if (tag != null) { rect.set(x-L.px(84), y-L.px(88), x-L.px(12), y-L.px(16)); c.drawBitmap(tag, null, rect, image); }
        }
        int character=k.passengerCharacter();
        Bitmap icon=art == null || character < 1 || character > 20 ? null : art.icons[character];
        if (icon == null) return;
        float h=L.px(me ? 30 : 26);
        rect.set(x-h, y-h, x+h, y+h);
        c.drawBitmap(icon, null, rect, image);
    }

    private void drawIdle(Canvas c) {
        Bitmap logo=art == null ? null : art.hud.get("logo");
        if (!picture(c, logo, 300, 340, 640, 200))
            letters(c, "Double Dash!!", 620, 400, 72, PAPER, PAPER, CENTER, 1000);
        letters(c, "Standings and the map show here in a race", 620, 600, 28, MUTED, MUTED, CENTER, 1080);
    }

    // ---- pieces ----

    /** A name with the game's P1 tag after it when it is the owner's kart. */
    private void tagged(Canvas c, String name, boolean tag, float x, float top, float size, float right) {
        float tagW=tag ? 76 : 0, end=x;
        if (name != null) end=letters(c, name, x, top, size, PAPER, PAPER, LEFT, right-x-tagW)+12;
        if (tag) picture(c, hud("p1", INK, P1, P1), end, top-4, 64, 32);
    }

    /** The game's portrait window: navy fill, light bevel, the disc portrait inside. */
    private void window(Canvas c, int character, float x, float y, float size) {
        DuoLayout L=layout;
        if (!picture(c, hud("window", WINDOW_BLACK, WINDOW_WHITE, WINDOW_WHITE), x, y, size, size)) {
            rect.set(L.x(x), L.y(y), L.x(x+size), L.y(y+size));
            fill.setColor(WINDOW_BLACK); c.drawRoundRect(rect, L.px(8), L.px(8), fill);
        }
        Bitmap face=art == null || character < 1 || character > 20 ? null : art.faces[character];
        float inset=size*.1f;
        picture(c, face, x+inset, y+inset, size-2*inset, size-2*inset);
    }

    /** Known items with disc art only: empty, unknown and unverified kinds show nothing. */
    private Bitmap item(int kind, int holder) {
        if (art == null || kind < 0 || kind == DuoScene.ITEM_EMPTY) return null;
        return art.item(kind, holder);
    }

    private void contain(Canvas c, Bitmap bitmap, float l, float t, float size) {
        float s=size/Math.max(bitmap.getWidth(), bitmap.getHeight()), w=bitmap.getWidth()*s, h=bitmap.getHeight()*s;
        picture(c, bitmap, l+(size-w)*.5f, t+(size-h)*.5f, w, h);
    }

    private boolean picture(Canvas c, Bitmap bitmap, float x, float y, float w, float h) {
        return type.picture(c, bitmap, x, y, w, h);
    }

    private float letters(Canvas c, String s, float x, float top, float size, int colourTop, int colourBottom,
                          int align, float maxWidth) {
        return type.letters(c, s, x, top, size, colourTop, colourBottom, align, maxWidth);
    }

    private Bitmap hud(String key, int black, int top, int bottom) { return type.hud(key, black, top, bottom); }

    private float width(String s, float size) { return type.width(s, size); }
}
