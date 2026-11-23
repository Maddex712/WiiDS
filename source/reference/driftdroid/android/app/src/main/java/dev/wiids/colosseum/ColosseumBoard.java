package dev.wiids.colosseum;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.os.SystemClock;
import android.os.Handler;
import android.os.Looper;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;

import com.driftdroid.android.dualscreen.WiiDsSkin;

import java.util.List;

/**
 * The Colosseum lower screen: one instrument well showing one view at a time (area map, party or
 * Shadow Pokémon), a three-plate view switch and the single WiiDS entry. Everything drawn here is
 * owned by this screen; anything the upper HUD still draws is left out.
 */
final class ColosseumBoard extends View {
    static final int MAP = 0, PARTY = 1, SHADOWS = 2;
    private static final String[] VIEW_NAMES = {"Field", "Party", "Shadows"}, VIEW_GLYPHS = {"map", "party", "shadow"};

    private final OrreSkin skin = new OrreSkin();
    private final Bitmap art;
    private final Runnable openMenu;
    interface Buttons { void send(int key, boolean pressed); }
    private final Buttons buttons;
    private final ColosseumMenu menu = new ColosseumMenu();
    private final RectF[] rowRects = new RectF[ColosseumMenu.MAX_ROWS];
    private final RectF backKey = new RectF();
    private int topLevel = -1;
    private boolean topMissing;
    private int lastWhy = -1, lastRevision = -2;
    private String lastPlace;
    private final RectF frame = new RectF(), top = new RectF(), well = new RectF(), menuKey = new RectF(), r = new RectF(), q = new RectF();
    private final RectF[] tabs = {new RectF(), new RectF(), new RectF()};
    private ColosseumState state;
    private ColosseumBattle battle;
    private final int[] ui = new int[11];
    private final com.driftdroid.android.dualscreen.WiiDsBlackout blackout = new com.driftdroid.android.dualscreen.WiiDsBlackout();
    private boolean uiMissing;
    private int view = MAP, pressed = -1;
    private float u = 1, fieldBottom;
    // Map projection.
    private float[] edges = new float[0], lines = new float[0];
    private float minX, maxX, minZ, maxZ, scale, left, topEdge;
    private int room = Integer.MIN_VALUE;
    private long mapSince;

    ColosseumBoard(Context context, Bitmap art, Runnable openMenu, Buttons buttons) {
        super(context);
        this.art = art;
        this.openMenu = openMenu;
        this.buttons = buttons;
        for (int i = 0; i < rowRects.length; i++) rowRects[i] = new RectF();
        setContentDescription("Pokémon Colosseum field instrument");
    }

    void render(ColosseumState snapshot, ColosseumBattle battleNow) {
        state = snapshot;
        battle = battleNow;
        if (!uiMissing) {
            try { if (!ColosseumState.readUi(ui)) java.util.Arrays.fill(ui, 0); }
            catch (UnsatisfiedLinkError older) { uiMissing = true; java.util.Arrays.fill(ui, 0); }
        }
        if (!inBattleScene(snapshot) || !menu.read()) menu.open = false;
        if (menu.revision != lastRevision) {
            lastRevision = menu.revision;
            StringBuilder b = new StringBuilder("kind=" + menu.kind + " id=0x" + Integer.toHexString(menu.menuId) + " open=" + menu.open + " withheld=" + menu.withheld + " rows=" + menu.rows + " cur=" + menu.cursor);
            for (int i = 0; i < menu.rows; i++) b.append(" [").append(menu.item[i]).append(' ').append(menu.label[i]).append(" c=").append(menu.code[i]).append(" xy=").append(menu.x[i]).append(',').append(menu.y[i]).append(']');
            android.util.Log.i("WiiDsColoMenu", b.toString());
        }
        if (!topMissing) {
            try { topLevel = ColosseumState.topLevel(); }
            catch (UnsatisfiedLinkError older) { topMissing = true; topLevel = -1; }
        }
        if (snapshot != null) {
            float[] next = snapshot.copyMapEdges();
            if (snapshot.mapRevision == 0 && edges.length != 0) next = new float[0];
            if (next != null) {
                edges = next;
                mapSince = SystemClock.uptimeMillis();
                measureMap();
                project();
            }
            if (snapshot.room != room) { room = snapshot.room; mapSince = SystemClock.uptimeMillis(); }
        }
        invalidate();
    }

    /** Held touches end when the WiiDS menu takes this display. */
    void releaseTouches() { pressed = -1; stopDriving(); invalidate(); }

    private static boolean inBattleScene(ColosseumState s) { return s != null && s.scene == ColosseumState.BATTLE; }

    /** Title, attract demos and file screens: nothing of the story is loaded, so only the WiiDS entry shows. */
    private boolean frontend() {
        return state == null || state.scene == ColosseumState.FRONTEND || state.scene == 0
                || (state.scene == ColosseumState.FIELD && state.location.isEmpty() && edges.length == 0);
    }
    private boolean inBattle() { return state != null && state.scene == ColosseumState.BATTLE; }

    private void measureMap() {
        minX = minZ = Float.POSITIVE_INFINITY;
        maxX = maxZ = Float.NEGATIVE_INFINITY;
        for (int i = 0; i + 1 < edges.length; i += 2) {
            minX = Math.min(minX, edges[i]);
            maxX = Math.max(maxX, edges[i]);
            minZ = Math.min(minZ, edges[i + 1]);
            maxZ = Math.max(maxZ, edges[i + 1]);
        }
    }

    private void project() {
        scale = 0;
        float pad = u * 7;
        if (edges.length == 0 || well.width() < pad * 3) return;
        float width = maxX - minX, height = maxZ - minZ;
        if (!(width > 0 && height > 0)) return;
        scale = Math.min((well.width() - pad * 2) / width, (well.height() - pad * 2 - u * 6) / height);
        left = well.centerX() - width * scale / 2;
        topEdge = well.centerY() + u * 2 - height * scale / 2;
        if (lines.length != edges.length) lines = new float[edges.length];
        for (int i = 0; i + 1 < edges.length; i += 2) {
            lines[i] = left + (edges[i] - minX) * scale;
            lines[i + 1] = topEdge + (edges[i + 1] - minZ) * scale;
        }
    }

    @Override protected void onSizeChanged(int w, int h, int oldW, int oldH) {
        u = Math.min(w, h) / 100f;
        frame.set(u * 2, u * 2, w - u * 2, h - u * 2);
        float pad = u * 5;
        top.set(frame.left + pad, frame.top + pad - u, frame.right - pad, frame.top + pad + u * 13);
        menuKey.set(top.right - u * 13, top.top + u * 0.5f, top.right, top.top + u * 13.5f);
        float tabHeight = u * 15, gap = u * 2.6f, tabTop = frame.bottom - pad - tabHeight;
        float tabWidth = (frame.width() - pad * 2 - gap * 2) / 3;
        for (int i = 0; i < 3; i++) tabs[i].set(frame.left + pad + i * (tabWidth + gap), tabTop, frame.left + pad + i * (tabWidth + gap) + tabWidth, tabTop + tabHeight);
        fieldBottom = tabTop - u * 3.4f;
        well.set(frame.left + pad, top.bottom + u * 3, frame.right - pad, fieldBottom);
        project();
    }

    @Override public boolean onTouchEvent(MotionEvent event) {
        if (dark()) { pressed = -1; return true; }
        int hit = menuKey.contains(event.getX(), event.getY()) ? 3 : -1;
        if (!frontend()) for (int i = 0; i < 3; i++) if (tabs[i].contains(event.getX(), event.getY())) hit = i;
        if (commandsShown()) {
            for (int i = 0; i < menu.rows; i++) if (menu.enabled[i] == 1 && rowRects[i].contains(event.getX(), event.getY())) hit = 10 + i;
            if (menu.kind != ColosseumMenu.COMMANDS && backKey.contains(event.getX(), event.getY())) hit = 9;
        }
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN: pressed = hit; break;
            case MotionEvent.ACTION_MOVE: if (hit != pressed) pressed = -1; break;
            case MotionEvent.ACTION_UP: {
                int target = pressed;
                pressed = -1;
                if (target >= 0 && target == hit) {
                    if (target == 3) openMenu.run();
                    else if (target == 9) { stopDriving(); tap(KeyEvent.KEYCODE_BUTTON_B); }
                    else if (target >= 10) choose(target - 10);
                    else view = target;
                }
                break;
            }
            default: pressed = -1;
        }
        invalidate();
        return true;
    }

    @Override protected void onDraw(Canvas c) {
        skin.backdrop(c, getWidth(), getHeight(), u);
        skin.chassis(c, frame, u);
        drawTop(c);
        // Without the view switch, the well takes the whole instrument face.
        well.bottom = frontend() ? frame.bottom - u * 5 : fieldBottom;
        skin.well(c, well, u * 3, u);
        c.save();
        c.clipRect(well);
        if (frontend()) drawFrontend(c);
        else if (commandsShown()) drawCommands(c);
        else if (view == PARTY) drawParty(c);
        else if (view == SHADOWS) drawShadows(c);
        else if (inBattle()) drawBattle(c);
        else drawMap(c);
        c.restore();
        if (!frontend()) {
            for (int i = 0; i < 3; i++) drawTab(c, i);
            skin.focus(c, tabs[view], WiiDsSkin.pulse(), u);
            if (WiiDsSkin.motion()) postInvalidateDelayed(40);
        }
        // Owner rule: the lower screen goes dark with the upper one. Start-up before the game draws, movies,
        // and field dialogue (the game's message window) black it out; battle messages do not, the lower
        // screen owns the player's status through the whole battle.
        boolean starting = state == null || state.scene == 0;
        // ui[6] (the message system's state byte) stays set across the whole field once the first text has run, so it
        // cannot tell when dialogue is on screen; field dialogue waits for a reliable signal (ui[8] from newer hosts).
        boolean movie = ui[7] == 1, dialogue = !inBattle() && !frontend() && ui.length > 8 && ui[8] == 1;
        // Story events that take the camera (ui[9]): the lower screen goes dark with them, as for movies.
        boolean event = !inBattle() && !frontend() && ui[9] == 1;
        // The upper picture itself gone black (fades between areas, battle intros): match it, unless input is waiting below.
        boolean blackTop = topLevel >= 0 && topLevel < 6 && !commandsShown();
        blackout.target(starting || movie || dialogue || event || blackTop ? 1f : 0f);
        int why = (starting ? 1 : 0) | (movie ? 2 : 0) | (dialogue ? 4 : 0) | (blackTop ? 8 : 0) | (event ? 16 : 0);
        if (why != lastWhy) { lastWhy = why; android.util.Log.i("WiiDsColoLower", "blackout=" + why + " scene=" + (state == null ? -1 : state.scene) + " ui6=" + ui[6] + " ui7=" + ui[7] + " top=" + topLevel + " menu=" + menu.kind + "/" + menu.open); }
        if (blackout.draw(c, getWidth(), getHeight())) postInvalidateOnAnimation();
    }

    /** While the lower screen is blacked out it takes no touches. */
    boolean dark() { return blackout.fullyBlack(); }

    private void drawTop(Canvas c) {
        // A room whose archive the host has not named yet keeps the last place shown, not a generic "Orre".
        if (!frontend() && !inBattle() && !state.location.isEmpty()) lastPlace = state.location;
        String place = frontend() ? "Pokémon Colosseum" : inBattle() ? "Battle" : state.location.isEmpty() ? (lastPlace != null ? lastPlace : "Orre") : state.location;
        String caption = frontend() ? "WiiDS" : view == MAP ? (inBattle() ? "Battle instrument" : "Area map") : view == PARTY ? "Party" : "Shadow Pokémon";
        float width = menuKey.left - top.left - u * 4;
        skin.caps(c, caption, top.left + u * 0.6f, top.top + u * 3, u * 3.3f, u * 2.4f, width, OrreSkin.BRASS, Paint.Align.LEFT);
        skin.caps(c, place, top.left + u * 0.4f, top.top + u * 9.4f, u * 7.6f, u * 4, width, OrreSkin.SAND, Paint.Align.LEFT);
        r.set(menuKey);
        if (pressed == 3) r.offset(0, u * 0.4f);
        skin.plate(c, r, u * 2.4f, 0, pressed == 3 ? WiiDsSkin.PRESSED : 0, u);
        skin.glyph(c, "gear", r.centerX(), r.centerY() - u * 1.4f, u * 6, OrreSkin.INK);
        skin.caps(c, "WiiDS", r.centerX(), r.bottom - u * 2.6f, u * 2.6f, u * 2, r.width() - u * 2, OrreSkin.INK, Paint.Align.CENTER);
    }

    private void drawTab(Canvas c, int index) {
        r.set(tabs[index]);
        boolean selected = index == view, down = pressed == index;
        if (down) r.offset(0, u * 0.4f);
        skin.plate(c, r, u * 2.4f, index == SHADOWS ? (selected ? OrreSkin.SHADOW : 0xFF6B4FA0) : selected ? OrreSkin.BRASS : 0,
                (selected ? WiiDsSkin.FOCUSED : 0) | (down ? WiiDsSkin.PRESSED : 0), u);
        int ink = index == SHADOWS ? 0xFFFFFFFF : OrreSkin.INK;
        float icon = u * 6;
        skin.glyph(c, VIEW_GLYPHS[index], r.left + u * 6.5f, r.centerY(), icon, ink);
        skin.caps(c, VIEW_NAMES[index], r.left + u * 11.5f, r.centerY(), u * 4.6f, u * 3, r.right - r.left - u * 14, ink, Paint.Align.LEFT);
    }

    private void message(Canvas c, String title, String detail) {
        skin.caps(c, title, well.centerX(), well.centerY() - u * 3, u * 5.2f, u * 3.2f, well.width() - u * 12, OrreSkin.SAND, Paint.Align.CENTER);
        if (detail != null) skin.label(c, detail, well.centerX(), well.centerY() + u * 4, u * 3.8f, well.width() - u * 12,
                WiiDsSkin.alpha(OrreSkin.SAND, 0.7f), OrreSkin.REGULAR, Paint.Align.CENTER);
    }

    private void drawFrontend(Canvas c) {
        grid(c);
        if (art == null) { message(c, "Pokémon Colosseum", null); return; }
        float width = Math.min(well.width() * 0.72f, well.height() * 2.2f), height = width * art.getHeight() / art.getWidth();
        r.set(well.centerX() - width / 2, well.centerY() - height / 2, well.centerX() + width / 2, well.centerY() + height / 2);
        c.drawBitmap(art, null, r, skin.imagePaint());
    }

    private void grid(Canvas c) {
        Paint line = skin.linePaint(u * 0.18f, WiiDsSkin.alpha(OrreSkin.BRASS, 0.16f));
        float step = u * 8;
        for (float x = well.left + step; x < well.right; x += step) c.drawLine(x, well.top, x, well.bottom, line);
        for (float y = well.top + step; y < well.bottom; y += step) c.drawLine(well.left, y, well.right, y, line);
    }

    private void drawMap(Canvas c) {
        grid(c);
        if (scale == 0) { message(c, "No area map", "This place has no walkable outline."); return; }
        float appear = WiiDsSkin.motion() ? Math.min(1f, (SystemClock.uptimeMillis() - mapSince) / 220f) : 1f;
        c.drawLines(lines, skin.linePaint(u * 1.5f, WiiDsSkin.alpha(OrreSkin.RUST, 0.55f * appear)));
        c.drawLines(lines, skin.linePaint(u * 0.55f, WiiDsSkin.alpha(OrreSkin.SAND, appear)));
        float x = left + (state.playerX - minX) * scale, y = topEdge + (state.playerZ - minZ) * scale;
        if (well.contains(x, y)) {
            float pulse = WiiDsSkin.pulse();
            c.drawCircle(x, y, u * (2.4f + 1.2f * pulse), skin.fillPaint(WiiDsSkin.alpha(OrreSkin.COAT, 0.28f)));
            c.drawCircle(x, y, u * 1.7f, skin.fillPaint(0xFFFFFFFF));
            c.drawCircle(x, y, u * 1.2f, skin.fillPaint(OrreSkin.COAT));
        }
        // Compass and a room tag come from the map itself: north is the game's -Z, as its own maps draw it.
        float cx = well.right - u * 6, cy = well.top + u * 7;
        c.drawCircle(cx, cy, u * 3.4f, skin.linePaint(u * 0.3f, WiiDsSkin.alpha(OrreSkin.BRASS, 0.8f)));
        skin.caps(c, "N", cx, cy - u * 0.1f, u * 3.2f, u * 3, u * 6, OrreSkin.BRASS, Paint.Align.CENTER);
        skin.caps(c, "Room " + state.room, well.left + u * 3, well.top + u * 4, u * 3, u * 2.4f, well.width() * 0.6f,
                WiiDsSkin.alpha(OrreSkin.SAND, 0.75f), Paint.Align.LEFT);
    }

    private void drawBattle(Canvas c) {
        grid(c);
        // The game's party screen on the upper screen (with a message over it) shows HP already: nothing repeats here.
        if (battle != null && battle.partyScreenUp) { message(c, "Party", "On the upper screen with the game's message."); return; }
        if (battle != null && battle.ownsStatus && battle.count > 0) {
            float gap = u * 3, height = Math.min(u * 26, (well.height() - u * 10 - gap) / 2), y = well.centerY() - (height * battle.count + gap * (battle.count - 1)) / 2;
            for (int i = 0; i < battle.count; i++) {
                r.set(well.left + u * 5, y, well.right - u * 5, y + height);
                RectF plate = new RectF(r); // statusPlate reuses r for the HP bar
                statusPlate(c, new RectF(plate), battle.names[i], battle.level[i], battle.hp[i], battle.maxHp[i], battle.shadow[i], true);
                battleExtras(c, plate, battle.status[i], battle.exp[i]);
                if (battle.shadow[i] && battle.heart[i] >= 0) {
                    RectF gauge = new RectF(plate.right - u * 30, plate.top + u * 3.4f, plate.right - u * 18, plate.top + u * 6.6f);
                    skin.heartGauge(c, gauge, battle.heart[i], u);
                }
                y += height + gap;
            }
            return;
        }
        drawFrontend(c);   // no battler on the field yet (team picker, intro): the game emblem, as on the title
        // Shown only while the upper HUD still draws the player plates.
    }

    private static final String[] CONDITIONS = {null, null, null, "PSN", "PSN", "PAR", "BRN", "FRZ", "SLP"};

    /** Status condition and EXP, the two other things the upper plate showed. Unknown values are left out. */
    private void battleExtras(Canvas c, RectF plate, int status, int exp) {
        float x0 = plate.left + u * 4.8f, x1 = plate.right - u * 4.8f, y = plate.bottom - u * 3.6f;
        if (status >= 3 && status < CONDITIONS.length) {
            q.set(x1 - u * 11, plate.top + u * 3.2f, x1, plate.top + u * 7.4f);
            skin.plate(c, new RectF(q), u, OrreSkin.HP_LOW, 0, u);
            skin.caps(c, CONDITIONS[status], q.centerX(), q.centerY(), u * 2.8f, u * 2, u * 9, 0xFFFFFFFF, Paint.Align.CENTER);
        }
        if (exp >= 0) {
            skin.caps(c, "EXP", x0, y, u * 2.2f, u * 1.8f, u * 6, OrreSkin.BRASS, Paint.Align.LEFT);
            q.set(x0 + u * 6, y - u * 0.7f, x1, y + u * 0.7f);
            c.drawRect(q, skin.fillPaint(0xFF050403));
            q.right = q.left + q.width() * Math.min(1000, exp) / 1000f;
            c.drawRect(q, skin.fillPaint(OrreSkin.COAT));
        }
    }

    private void statusPlate(Canvas c, RectF area, String name, int level, int hp, int maxHp, boolean shadow, boolean large) {
        skin.plate(c, area, u * 2.2f, shadow ? 0xFF6B4FA0 : 0, 0, u);
        q.set(area.left + u * 1.8f, area.top + u * 1.8f, area.right - u * 1.8f, area.bottom - u * 1.8f);
        skin.well(c, q, u * 1.4f, u);
        float x0 = q.left + u * 3, x1 = q.right - u * 3, nameSize = large ? u * 6 : u * 4.4f;
        float nameY = q.top + q.height() * 0.28f;
        if (level > 0) skin.caps(c, "Lv " + level, x1, nameY, nameSize * 0.7f, u * 2.4f, u * 16, OrreSkin.BRASS, Paint.Align.RIGHT);
        skin.caps(c, name == null ? "Pokémon" : name, x0, nameY, nameSize, u * 2.8f, x1 - x0 - u * 17, shadow ? 0xFFE3CCFF : OrreSkin.SAND, Paint.Align.LEFT);
        if (maxHp > 0) {
            float barHeight = large ? u * 3.2f : u * 2.2f, barY = q.top + q.height() * 0.62f;
            skin.caps(c, "HP", x0, barY, u * 2.8f, u * 2.2f, u * 6, OrreSkin.BRASS, Paint.Align.LEFT);
            String numbers = hp + "/" + maxHp;
            float numbersWidth = large ? u * 22 : u * 15;
            r.set(x0 + u * 6, barY - barHeight / 2, x1 - numbersWidth, barY + barHeight / 2);
            skin.hpBar(c, new RectF(r), hp, maxHp, u);
            skin.caps(c, numbers, x1, barY, large ? u * 5 : u * 3.6f, u * 2.4f, numbersWidth - u * 2, OrreSkin.SAND, Paint.Align.RIGHT);
        }
    }

    private void drawParty(Canvas c) {
        List<ColosseumState.PartyMember> party = state.party;
        if (party.isEmpty()) { message(c, "No party yet", "Your Pokémon appear here in Story Mode."); return; }
        float pad = u * 3.5f, gap = u * 2.4f;
        int rows = 3;
        float width = (well.width() - pad * 2 - gap) / 2, height = Math.min(u * 22, (well.height() - pad * 2 - gap * (rows - 1)) / rows);
        float top = well.centerY() - (height * rows + gap * (rows - 1)) / 2;
        for (ColosseumState.PartyMember member : party) {
            if (member.slot < 0 || member.slot > 5) continue;
            float x = well.left + pad + (member.slot % 2) * (width + gap), y = top + (member.slot / 2) * (height + gap);
            r.set(x, y, x + width, y + height);
            // While a battle is up, the upper HUD owns the status of the Pokémon on the field.
            // The game's own menu shows party HP on the upper screen while it is open; the lower list then leaves it out.
            boolean menuShows = !inBattle() && ui[0] == 1;
            boolean upperOwns = menuShows || inBattle() && (battle == null || !battle.valid || (battle.active(member.slot) && !battle.ownsStatus));
            statusPlate(c, new RectF(r), member.name, upperOwns ? 0 : member.level, member.hp, upperOwns ? 0 : member.maxHp, false, false);
            if (upperOwns) skin.caps(c, menuShows ? "In game menu" : "In battle", r.left + u * 4.8f, r.top + r.height() * 0.66f, u * 3, u * 2.4f, r.width() - u * 9,
                    OrreSkin.BRASS, Paint.Align.LEFT);
        }
    }

    private void drawShadows(Canvas c) {
        List<ColosseumState.ShadowPokemon> shadows = state.shadows;
        if (shadows.isEmpty()) { message(c, "No Shadow Pokémon seen", "Shadow Pokémon you encounter are logged here."); return; }
        float pad = u * 3.5f, gap = u * 2, height = u * 13;
        int fit = Math.max(1, (int) ((well.height() - pad * 2 + gap) / (height + gap)));
        float y = well.top + pad;
        int from = Math.max(0, shadows.size() - fit);
        for (int i = from; i < shadows.size(); i++) {
            ColosseumState.ShadowPokemon entry = shadows.get(i);
            r.set(well.left + pad, y, well.right - pad, y + height);
            skin.plate(c, r, u * 2, entry.status == ColosseumState.PURIFIED ? 0 : 0xFF6B4FA0, 0, u);
            q.set(r.left + u * 1.5f, r.top + u * 1.5f, r.right - u * 1.5f, r.bottom - u * 1.5f);
            skin.well(c, q, u * 1.2f, u);
            skin.glyph(c, "shadow", q.left + u * 4.5f, q.centerY(), u * 5.4f, entry.status == ColosseumState.PURIFIED ? OrreSkin.SAND_MID : OrreSkin.SHADOW);
            String status = entry.status == ColosseumState.PURIFIED ? "Purified" : entry.status == ColosseumState.SNAGGED ? "Snagged" : "Seen";
            float x0 = q.left + u * 9.5f, gaugeWidth = u * 26, statusWidth = u * 17;
            skin.caps(c, entry.name == null ? "Shadow " + entry.id : entry.name, x0, q.centerY(), u * 4.4f, u * 2.8f,
                    q.right - x0 - gaugeWidth - statusWidth - u * 5, 0xFFE9DAFF, Paint.Align.LEFT);
            skin.caps(c, status, q.right - gaugeWidth - u * 4, q.centerY(), u * 3.2f, u * 2.4f, statusWidth, OrreSkin.BRASS, Paint.Align.RIGHT);
            if (entry.heart >= 0 && entry.status != ColosseumState.PURIFIED) {
                RectF gauge = new RectF(q.right - gaugeWidth - u * 2, q.centerY() - u * 1.6f, q.right - u * 2, q.centerY() + u * 1.6f);
                skin.heartGauge(c, gauge, entry.heart, u);
            }
            y += height + gap;
        }
    }

    // ---- Battle input, 3DS style: the game's own command, move and target windows, drawn and touched here ----

    /** True while a battle input window the lower screen draws is open (the upper copy is withheld). */
    private boolean commandsShown() { return inBattle() && menu.drawnBelow(); }

    private void drawCommands(Canvas c) {
        grid(c);
        if (menu.kind >= ColosseumMenu.PARTY) { drawWindow(c); return; }
        // The player's own status stays in view above the choices, smaller.
        float y = well.top + u * 3;
        if (battle != null && battle.ownsStatus && battle.count > 0) {
            float h = u * 15, gap = u * 2, w = (well.width() - u * 6 - gap * (battle.count - 1)) / battle.count;
            for (int i = 0; i < battle.count; i++) {
                r.set(well.left + u * 3 + i * (w + gap), y, well.left + u * 3 + i * (w + gap) + w, y + h);
                statusPlate(c, new RectF(r), battle.names[i], battle.level[i], battle.hp[i], battle.maxHp[i], battle.shadow[i], false);
            }
            y += h + u * 3;
        }
        String heading = menu.kind == ColosseumMenu.MOVES ? "Moves" : menu.kind == ColosseumMenu.TARGETS ? "Choose a target" : "Command";
        if (menu.title != null && menu.kind != ColosseumMenu.TARGETS) heading = menu.title + "  ·  " + heading;
        skin.caps(c, heading, well.left + u * 4, y + u * 2.4f, u * 3.6f, u * 2.4f, well.width() - u * 30, OrreSkin.BRASS, Paint.Align.LEFT);
        boolean back = menu.kind != ColosseumMenu.COMMANDS;
        if (back) {
            backKey.set(well.right - u * 20, y - u * 1, well.right - u * 3, y + u * 6);
            q.set(backKey);
            if (pressed == 9) q.offset(0, u * 0.4f);
            skin.plate(c, new RectF(q), u * 1.6f, 0, pressed == 9 ? WiiDsSkin.PRESSED : 0, u);
            skin.caps(c, "Back", q.centerX(), q.centerY(), u * 3.2f, u * 2.2f, q.width() - u * 2, OrreSkin.INK, Paint.Align.CENTER);
        } else backKey.setEmpty();
        y += u * 7.5f;
        // Rows keep the game's own arrangement (its authored positions), as a grid of large plates.
        int n = menu.rows, cols = n <= 2 ? 1 : 2, lines = (n + cols - 1) / cols;
        int[] order = arrangement(n, cols);
        float gap = u * 2.6f, cw = (well.width() - u * 6 - gap * (cols - 1)) / cols;
        float ch = Math.min(u * 24, (well.bottom - y - u * 3 - gap * (lines - 1)) / lines);
        for (int slot = 0; slot < n; slot++) {
            int row = order[slot];
            float x0 = well.left + u * 3 + (slot % cols) * (cw + gap), y0 = y + (slot / cols) * (ch + gap);
            rowRects[row].set(x0, y0, x0 + cw, y0 + ch);
            drawRow(c, row);
        }
        for (int row = n; row < rowRects.length; row++) rowRects[row].setEmpty();
    }

    /** Orders rows into the grid by the game's authored positions (top-to-bottom, then left-to-right). */
    private int[] arrangement(int n, int cols) {
        Integer[] idx = new Integer[n];
        for (int i = 0; i < n; i++) idx[i] = i;
        boolean known = true;
        for (int i = 0; i < n; i++) if (menu.x[i] < 0 || menu.y[i] < 0) known = false;
        if (known) java.util.Arrays.sort(idx, (a, b) -> Math.abs(menu.y[a] - menu.y[b]) > 8 ? menu.y[a] - menu.y[b] : menu.x[a] - menu.x[b]);
        int[] out = new int[n];
        for (int i = 0; i < n; i++) out[i] = idx[i];
        return out;
    }

    private void drawRow(Canvas c, int row) {
        RectF area = new RectF(rowRects[row]);
        boolean on = menu.enabled[row] == 1, down = pressed == 10 + row, cursor = row == menu.cursor;
        if (down) area.offset(0, u * 0.4f);
        int tint = 0;
        if (menu.kind == ColosseumMenu.MOVES && menu.type[row] >= 0 && menu.type[row] < ColosseumMenu.TYPE_COLORS.length) tint = ColosseumMenu.TYPE_COLORS[menu.type[row]];
        if (menu.special[row] == 1 || menu.special[row] == 2) tint = 0xFF6B4FA0;
        skin.plate(c, area, u * 2.2f, on ? tint : 0xFF3A302A, (cursor ? WiiDsSkin.FOCUSED : 0) | (down ? WiiDsSkin.PRESSED : 0), u);
        if (row == menu.swapFrom) c.drawRect(area, skin.linePaint(u * 0.6f, OrreSkin.FOCUS));
        int ink = !on ? WiiDsSkin.alpha(OrreSkin.SAND, 0.45f) : tint != 0 ? 0xFFFFFFFF : OrreSkin.INK;
        String text = menu.label[row];
        if (text == null) text = menu.kind == ColosseumMenu.TARGETS ? targetName(row) : "—";
        if (menu.kind == ColosseumMenu.MOVES) {
            skin.caps(c, text, area.left + u * 3.4f, area.top + area.height() * 0.36f, u * 5, u * 3, area.width() - u * 7, ink, Paint.Align.LEFT);
            String left = menu.detail[row] != null ? menu.detail[row] : menu.type[row] >= 0 && menu.type[row] < ColosseumMenu.TYPES.length ? ColosseumMenu.TYPES[menu.type[row]] : null;
            if (left != null) skin.caps(c, left, area.left + u * 3.4f, area.top + area.height() * 0.74f, u * 3, u * 2.2f, area.width() * 0.5f, ink, Paint.Align.LEFT);
            if (menu.pp[row] >= 0 && menu.maxPp[row] > 0)
                skin.caps(c, "PP " + menu.pp[row] + "/" + menu.maxPp[row], area.right - u * 3.4f, area.top + area.height() * 0.74f, u * 3.4f, u * 2.2f,
                        area.width() * 0.45f, menu.pp[row] == 0 ? OrreSkin.HP_LOW : ink, Paint.Align.RIGHT);
        } else {
            skin.caps(c, text, area.centerX(), area.centerY(), u * 5.6f, u * 3.2f, area.width() - u * 6, ink, Paint.Align.CENTER);
        }
        if (cursor && WiiDsSkin.motion()) skin.focus(c, area, WiiDsSkin.pulse(), u);
    }

    /** Target rows carry no text; name them from the field position the game returns. */
    private String targetName(int row) {
        switch (menu.code[row]) {
            case 0x45: case 0x46: return "Foe " + (menu.code[row] - 0x44);
            case 0x47: case 0x48: {
                int p = menu.code[row] - 0x47;
                if (battle != null && p < battle.count && battle.names[p] != null) return battle.names[p];
                return "Ally " + (p + 1);
            }
            default: return "Target " + (row + 1);
        }
    }

    // ---- Driving the game's cursor: a tap walks the native cursor to the row, then presses A ----
    private final Handler driver = new Handler(Looper.getMainLooper()), releases = new Handler(Looper.getMainLooper());
    private final ColosseumMenu probe = new ColosseumMenu();
    private int driveRow = -1, driveKind, driveMenu, driveSteps, drivePlace;
    private boolean settled;

    private void choose(int row) {
        stopDriving();
        driveRow = row;
        driveKind = menu.kind;
        driveMenu = menu.menuId;
        drivePlace = menu.code[row];
        driveSteps = 0;
        driver.post(this::step);
    }

    private void stopDriving() { driveRow = -1; settled = false; driver.removeCallbacksAndMessages(null); }

    private void step() {
        if (driveRow < 0) return;
        if (!probe.read() || probe.kind != driveKind || probe.menuId != driveMenu || (driveKind != ColosseumMenu.BAG && driveRow >= probe.rows) || ++driveSteps > 30) { stopDriving(); return; }
        if (driveKind == ColosseumMenu.BAG) {
            // Bag rows are a window around the cursor; the target is the item's place in the pocket.
            if (probe.place == drivePlace && settled) { driveRow = -1; settled = false; tap(KeyEvent.KEYCODE_BUTTON_A); return; }
            if (probe.place == drivePlace) { settled = true; driver.postDelayed(this::step, 120); return; }
            settled = false;
            if (probe.place < 0) { stopDriving(); return; }
            tap(drivePlace > probe.place ? KeyEvent.KEYCODE_DPAD_DOWN : KeyEvent.KEYCODE_DPAD_UP);
            driver.postDelayed(this::step, 240);
            return;
        }
        int at = probe.cursor;
        android.util.Log.i("WiiDsColoDrive", "kind=" + probe.kind + " menu=" + probe.menuId + " at=" + at + " item=" + probe.cursorItem
                + " target=" + driveRow + "/" + probe.item[Math.max(0, Math.min(driveRow, probe.rows - 1))] + " xy=" + (at >= 0 ? probe.x[at] + "," + probe.y[at] : "-")
                + " txy=" + probe.x[Math.max(0, driveRow)] + "," + probe.y[Math.max(0, driveRow)] + " rows=" + probe.rows);
        // A is pressed only once the game's cursor has read as the chosen item twice in a row (the feed trails the
        // key press by a frame or two).
        boolean there = at == driveRow && (probe.cursorItem < 0 || probe.cursorItem == probe.item[driveRow]);
        if (there && settled) { driveRow = -1; settled = false; tap(KeyEvent.KEYCODE_BUTTON_A); return; }
        if (there) { settled = true; driver.postDelayed(this::step, 120); return; }
        settled = false;
        int key;
        if (at < 0) key = KeyEvent.KEYCODE_DPAD_DOWN;
        else if (probe.x[at] >= 0 && probe.y[at] >= 0 && probe.x[driveRow] >= 0 && probe.y[driveRow] >= 0) {
            int dx = probe.x[driveRow] - probe.x[at], dy = probe.y[driveRow] - probe.y[at];
            key = Math.abs(dy) >= Math.abs(dx) ? (dy > 0 ? KeyEvent.KEYCODE_DPAD_DOWN : KeyEvent.KEYCODE_DPAD_UP)
                    : (dx > 0 ? KeyEvent.KEYCODE_DPAD_RIGHT : KeyEvent.KEYCODE_DPAD_LEFT);
        } else key = driveRow > at ? KeyEvent.KEYCODE_DPAD_DOWN : KeyEvent.KEYCODE_DPAD_UP;
        tap(key);
        driver.postDelayed(this::step, 240);
    }

    /** One press long enough for the game to see it (it polls once a frame). */
    private void tap(int key) {
        buttons.send(key, true);
        releases.postDelayed(() -> buttons.send(key, false), 70);
    }

    // ---- The party screen, bag, two-way choices and a member's action list ----
    private static final String[] STATUS = {null, null, null, "PSN", "PSN", "PAR", "BRN", "FRZ", "SLP"};

    private void drawWindow(Canvas c) {
        float y = well.top + u * 3;
        String heading;
        switch (menu.kind) {
            case ColosseumMenu.PARTY: heading = "Choose a Pokémon"; break;
            case ColosseumMenu.BAG: heading = menu.title != null ? menu.title : "Items"; break;
            case ColosseumMenu.CHOICE: heading = menu.title != null ? menu.title : "Choose"; break;
            default: heading = "What will you do?";
        }
        skin.caps(c, heading, well.left + u * 4, y + u * 2.4f, u * 3.6f, u * 2.4f, well.width() - u * 30, OrreSkin.BRASS, Paint.Align.LEFT);
        backKey.set(well.right - u * 20, y - u, well.right - u * 3, y + u * 6);
        q.set(backKey);
        if (pressed == 9) q.offset(0, u * 0.4f);
        skin.plate(c, new RectF(q), u * 1.6f, 0, pressed == 9 ? WiiDsSkin.PRESSED : 0, u);
        skin.caps(c, "Back", q.centerX(), q.centerY(), u * 3.2f, u * 2.2f, q.width() - u * 2, OrreSkin.INK, Paint.Align.CENTER);
        y += u * 8.5f;
        int n = menu.rows;
        for (int row = n; row < rowRects.length; row++) rowRects[row].setEmpty();
        if (menu.kind == ColosseumMenu.PARTY) {
            // Six member plates in two columns, CANCEL as a wide plate under them (the game's own order).
            int members = Math.min(n, 6);
            float gap = u * 2.2f, cw = (well.width() - u * 6 - gap) / 2, bottom = well.bottom - u * 3;
            boolean cancel = n > 6;
            int filledRows = 0;
            for (int row = 0; row < members; row++) if (menu.type[row] > 0) filledRows = row / 2 + 1;
            int lines = Math.max(1, filledRows);
            float cancelH = cancel ? u * 9 : 0, ch = Math.min(u * 18, (bottom - y - cancelH - (cancel ? gap : 0) - gap * (lines - 1)) / lines);
            for (int row = 0; row < members; row++) {
                if (row / 2 >= lines) { rowRects[row].setEmpty(); continue; }   // empty slots past the last member
                float x0 = well.left + u * 3 + (row % 2) * (cw + gap), y0 = y + (row / 2) * (ch + gap);
                rowRects[row].set(x0, y0, x0 + cw, y0 + ch);
                drawMember(c, row);
            }
            if (cancel) {
                rowRects[6].set(well.left + u * 3, bottom - cancelH, well.right - u * 3, bottom);
                drawPlain(c, 6, menu.label[6] != null ? menu.label[6] : "Cancel");
            }
            return;
        }
        if (menu.kind == ColosseumMenu.BAG) {
            // The items around the cursor, top to bottom in pocket order; quantities on the right.
            float gap = u * 1f, rh = Math.min(u * 10, (well.bottom - u * 3 - y - gap * (n - 1)) / Math.max(1, n));
            for (int row = 0; row < n; row++) {
                float y0 = y + row * (rh + gap);
                rowRects[row].set(well.left + u * 3, y0, well.right - u * 3, y0 + rh);
                RectF area = new RectF(rowRects[row]);
                boolean down = pressed == 10 + row, cursor = menu.place >= 0 && menu.code[row] == menu.place;
                if (down) area.offset(0, u * 0.4f);
                skin.plate(c, area, u * 1.8f, 0, (cursor ? WiiDsSkin.FOCUSED : 0) | (down ? WiiDsSkin.PRESSED : 0), u);
                if (menu.special[row] == 1) c.drawRect(area, skin.linePaint(u * 0.6f, OrreSkin.FOCUS));
                float text = Math.min(u * 4.4f, rh * 0.62f);
                skin.caps(c, menu.label[row] != null ? menu.label[row] : "Item", area.left + u * 3.4f, area.centerY(), text, Math.min(u * 2.8f, text * 0.7f),
                        area.width() - u * 22, OrreSkin.INK, Paint.Align.LEFT);
                if (menu.pp[row] >= 0)
                    skin.caps(c, "x" + menu.pp[row], area.right - u * 3.4f, area.centerY(), text, Math.min(u * 2.8f, text * 0.7f), u * 14, OrreSkin.INK, Paint.Align.RIGHT);
            }
            return;
        }
        // Two-way choice and the action list: one large plate per choice.
        float gap = u * 2.4f, rh = Math.min(u * 16, (well.bottom - u * 3 - y - gap * (n - 1)) / Math.max(1, n));
        for (int row = 0; row < n; row++) {
            float y0 = y + row * (rh + gap);
            rowRects[row].set(well.left + u * 6, y0, well.right - u * 6, y0 + rh);
            String fallback = menu.kind == ColosseumMenu.CHOICE ? (menu.code[row] == 1 ? "Yes" : "No") : "-";
            drawPlain(c, row, menu.label[row] != null ? menu.label[row] : fallback);
        }
    }

    private void drawPlain(Canvas c, int row, String text) {
        RectF area = new RectF(rowRects[row]);
        boolean on = menu.enabled[row] != 0, down = pressed == 10 + row, cursor = row == menu.cursor;
        if (down) area.offset(0, u * 0.4f);
        skin.plate(c, area, u * 2.2f, on ? 0 : 0xFF3A302A, (cursor ? WiiDsSkin.FOCUSED : 0) | (down ? WiiDsSkin.PRESSED : 0), u);
        skin.caps(c, text, area.centerX(), area.centerY(), u * 5.2f, u * 3.2f, area.width() - u * 6,
                on ? OrreSkin.INK : WiiDsSkin.alpha(OrreSkin.SAND, 0.45f), Paint.Align.CENTER);
    }

    private void drawMember(Canvas c, int row) {
        RectF area = new RectF(rowRects[row]);
        boolean filled = menu.type[row] > 0, down = pressed == 10 + row, cursor = row == menu.cursor;
        if (!filled) { skin.well(c, area, u * 2, u); return; }
        if (down) area.offset(0, u * 0.4f);
        int flags = menu.extra[row], level = flags >= 0 ? flags & 0xFF : -1;
        boolean onField = flags >= 0 && (flags & 0x100) != 0, fainted = flags >= 0 && (flags & 0x200) != 0;
        skin.plate(c, area, u * 2, fainted ? 0xFF3A302A : 0, (cursor ? WiiDsSkin.FOCUSED : 0) | (down ? WiiDsSkin.PRESSED : 0), u);
        float x0 = area.left + u * 3, x1 = area.right - u * 3, h = area.height();
        int ink = fainted ? WiiDsSkin.alpha(OrreSkin.SAND, 0.6f) : OrreSkin.INK;
        float nameSize = Math.min(u * 4.2f, h * 0.3f), smallSize = Math.min(u * 3.2f, h * 0.24f);
        String name = menu.label[row] != null ? menu.label[row] : (menu.detail[row] != null ? menu.detail[row] : "Pokemon");
        skin.caps(c, name, x0, area.top + h * 0.28f, nameSize, Math.min(u * 2.6f, nameSize * 0.7f), area.width() - u * 18, ink, Paint.Align.LEFT);
        if (level > 0) skin.caps(c, "Lv " + level, x1, area.top + h * 0.28f, smallSize, Math.min(u * 2.2f, smallSize * 0.7f), u * 12, ink, Paint.Align.RIGHT);
        if (menu.maxPp[row] > 0) {
            float barY = area.top + h * 0.62f, barH = Math.max(u * 0.8f, Math.min(u * 1.6f, h * 0.1f));
            r.set(x0, barY - barH, x1 - u * 15, barY + barH);
            skin.hpBar(c, new RectF(r), Math.max(0, menu.pp[row]), menu.maxPp[row], u);
            skin.caps(c, Math.max(0, menu.pp[row]) + "/" + menu.maxPp[row], x1, barY, smallSize, Math.min(u * 2.2f, smallSize * 0.7f), u * 14, ink, Paint.Align.RIGHT);
        }
        String note = fainted ? "Fainted" : onField ? "In battle" : null;
        int status = menu.special[row];
        if (note == null && status >= 3 && status < STATUS.length) note = STATUS[status];
        if (note != null && h > u * 12)
            skin.caps(c, note, x0, area.top + h * 0.87f, Math.min(u * 2.8f, h * 0.18f), u * 2, area.width() - u * 6, OrreSkin.BRASS, Paint.Align.LEFT);
    }
}
