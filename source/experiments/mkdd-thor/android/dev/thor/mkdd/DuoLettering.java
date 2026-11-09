package dev.thor.mkdd;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import java.util.HashMap;

/**
 * The game's own lettering and HUD pictures from {@link DuoArt}, for the duo screen and the WiiDS
 * menu. Disc textures are intensity masks; they are recoloured the way the game's J2DPictures are
 * (texture black to one colour, white to a top-to-bottom gradient) and cached per colour set.
 * Coordinates are design units, mapped to pixels by {@link #place}.
 */
final class DuoLettering {
    static final int INK=0xff0c0c0e;
    static final int LEFT=0, CENTER=1, RIGHT=2;
    /** Mariofont spacing, in em: a word space, and the overlap of neighbouring outlined glyphs. */
    private static final float SPACE=.36f, KERN=.1f;
    /** Only used while the disc lettering is still being built. */
    private static final Typeface FALLBACK=Typeface.create("sans-serif-black", Typeface.NORMAL);

    private final HashMap<String, Bitmap> tints=new HashMap<>();
    private final HashMap<String, Integer> spans=new HashMap<>();
    private final Paint image=new Paint(Paint.ANTI_ALIAS_FLAG|Paint.FILTER_BITMAP_FLAG);
    private final Paint text=new Paint(Paint.ANTI_ALIAS_FLAG|Paint.SUBPIXEL_TEXT_FLAG);
    private final RectF rect=new RectF();
    private DuoArt art;
    private float left, top, scale=1;

    /** New art (or none): drops every recoloured copy of the old. */
    void setArt(DuoArt art) { drop(); this.art=art; }
    DuoArt art() { return art; }
    void place(float left, float top, float scale) { this.left=left; this.top=top; this.scale=scale; }

    /** Draws at design coordinates; false (nothing drawn) when the bitmap is missing. */
    boolean picture(Canvas c, Bitmap bitmap, float x, float y, float w, float h) {
        if (bitmap == null) return false;
        rect.set(left+x*scale, top+y*scale, left+(x+w)*scale, top+(y+h)*scale);
        c.drawBitmap(bitmap, null, rect, image);
        return true;
    }

    /**
     * A string in the game's mariofont (one case, like the game). [y] is the glyph box top;
     * shrinks to [maxWidth] when one is given. Returns the right edge, in design units.
     */
    float letters(Canvas c, String s, float x, float y, float size, int colourTop, int colourBottom,
                  int align, float maxWidth) {
        float w=width(s, size);
        if (maxWidth > 0 && w > maxWidth) { y+=size*(1-maxWidth/w)*.5f; size*=maxWidth/w; w=maxWidth; }
        if (align == CENTER) x-=w*.5f; else if (align == RIGHT) x-=w;
        if (!hasGlyphs(s)) {
            text.setTypeface(FALLBACK); text.setTextSize(size*.8f*scale); text.setColor(colourTop);
            text.setTextAlign(Paint.Align.LEFT);
            c.drawText(s, left+x*scale, top+(y+size*.8f)*scale, text);
            return x+w;
        }
        for (int i=0; i < s.length(); i++) {
            char ch=s.charAt(i);
            if (ch == ' ') { x+=size*SPACE; continue; }
            Bitmap g=hud(glyph(ch), INK, colourTop, colourBottom, true);
            float gw=g.getWidth()*size/32f;
            // No apostrophe in the font: the game's full stop, raised.
            picture(c, g, x, ch == '\'' ? y-size*.45f : y, gw, size);
            x+=gw-size*KERN;
        }
        return x+size*KERN;
    }

    /** Width in design units. */
    float width(String s, float size) {
        if (!hasGlyphs(s)) {
            text.setTypeface(FALLBACK); text.setTextSize(size*.8f);
            return text.measureText(s);
        }
        float w=0;
        for (int i=0; i < s.length(); i++) {
            char ch=s.charAt(i);
            w+=ch == ' ' ? size*SPACE : span(glyph(ch))*size/32f-size*KERN;
        }
        return w+size*KERN;
    }

    Bitmap hud(String key, int black, int top, int bottom) { return hud(key, black, top, bottom, false); }

    /** A disc HUD texture recoloured J2DPicture-style; glyphs are cropped to their inked columns. */
    Bitmap hud(String key, int black, int top, int bottom, boolean crop) {
        Bitmap src=art == null || key == null ? null : art.hud.get(key);
        if (src == null) return null;
        String id=key+'/'+black+'/'+top+'/'+bottom+(crop ? "/c" : "");
        Bitmap cached=tints.get(id);
        if (cached != null) return cached;
        int w=src.getWidth(), h=src.getHeight();
        int[] px=new int[w*h];
        src.getPixels(px, 0, w, 0, 0, w, h);
        int[] range=crop ? inked(src) : new int[]{0, w};
        if (range == null) return null;
        int cw=range[1]-range[0];
        int[] out=new int[cw*h];
        for (int y=0; y < h; y++) {
            int white=mix(top, bottom, h > 1 ? y/(float) (h-1) : 0);
            for (int x=0; x < cw; x++) {
                int p=px[y*w+x+range[0]];
                out[y*cw+x]=(p & 0xff000000) | (mix(black, white, ((p >> 16) & 255)/255f) & 0xffffff);
            }
        }
        Bitmap made=Bitmap.createBitmap(out, cw, h, Bitmap.Config.ARGB_8888);
        tints.put(id, made);
        return made;
    }

    void drop() {
        for (Bitmap b : tints.values()) b.recycle();
        tints.clear(); spans.clear();
    }

    private boolean hasGlyphs(String s) {
        if (art == null) return false;
        for (int i=0; i < s.length(); i++) {
            char ch=s.charAt(i);
            if (ch == ' ') continue;
            String key=glyph(ch);
            if (key == null || span(key) <= 0) return false;
        }
        return true;
    }

    private static String glyph(char ch) {
        if (ch >= 'A' && ch <= 'Z') ch=(char) (ch+32);
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) return "glyph_"+ch;
        switch (ch) {
            case ':': return "glyph_colon";
            case '.': case '\'': return "glyph_period";
            case '/': return "glyph_slash";
            case '!': return "glyph_exclamation";
            case '?': return "glyph_question";
            case '-': return "glyph_dash";
            default: return null;
        }
    }

    /** Inked width of a glyph texture in texels (0 when missing). */
    private int span(String key) {
        Integer known=spans.get(key);
        if (known != null) return known;
        Bitmap g=art == null ? null : art.hud.get(key);
        int[] range=g == null ? null : inked(g);
        int w=range == null ? 0 : range[1]-range[0];
        if (art != null) spans.put(key, w);
        return w;
    }

    /** First and past-last columns with any alpha, or null for an empty texture. */
    private static int[] inked(Bitmap g) {
        int w=g.getWidth(), h=g.getHeight(), l=w, r=0;
        int[] px=new int[w*h];
        g.getPixels(px, 0, w, 0, 0, w, h);
        for (int i=0; i < px.length; i++) if ((px[i] >>> 24) != 0) { l=Math.min(l, i%w); r=Math.max(r, i%w+1); }
        return r > l ? new int[]{l, r} : null;
    }

    static int mix(int a, int b, float t) {
        int r=Math.round(((a >> 16) & 255)+(((b >> 16) & 255)-((a >> 16) & 255))*t);
        int g=Math.round(((a >> 8) & 255)+(((b >> 8) & 255)-((a >> 8) & 255))*t);
        int bl=Math.round((a & 255)+((b & 255)-(a & 255))*t);
        return 0xff000000 | r << 16 | g << 8 | bl;
    }
}
