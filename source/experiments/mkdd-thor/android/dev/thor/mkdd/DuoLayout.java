package dev.thor.mkdd;

/** Fixed 1240x1080 design grid, scaled uniformly and centred on the lower display. */
final class DuoLayout {
    static final float W = 1240, H = 1080, ROW = 104, ROW_STEP = 112, BOARD_TOP = 144;

    static final class Box {
        final float l, t, r, b;
        Box(float l, float t, float r, float b) { this.l=l; this.t=t; this.r=r; this.b=b; }
        float w() { return r-l; }
        float h() { return b-t; }
        float cx() { return (l+r)*.5f; }
        float cy() { return (t+b)*.5f; }
        Box inset(float d) { return new Box(l+d, t+d, r-d, b-d); }
    }

    final float scale, left, top;
    /** Header (lap, time), standings column, right column (course name + map), map drawing area. */
    final Box screen, header, board, side, mapArea;

    DuoLayout(int width, int height) {
        scale = width > 0 && height > 0 ? Math.min(width/W, height/H) : 0;
        left = (width-W*scale)*.5f; top = (height-H*scale)*.5f;
        screen = box(0, 0, 1240, 1080); header = box(0, 0, 1240, 120);
        board = box(16, BOARD_TOP, 592, 1040); side = box(616, 128, 1216, 1048);
        mapArea = box(632, 232, 1200, 1040);
    }

    Box box(float l, float t, float r, float b) { return new Box(x(l), y(t), x(r), y(b)); }
    float x(float design) { return left+design*scale; }
    float y(float design) { return top+design*scale; }
    float px(float design) { return design*scale; }
    /** Design-space top of a standings row; slots may be fractional while rows slide. */
    static float rowTop(float slot) { return BOARD_TOP+slot*ROW_STEP; }

    /** The centred aspect fit MapTransform uses, so map art and markers share one rectangle. */
    static Box fit(double spanX, double spanZ, Box area) {
        double scale = Math.min(area.w()/spanX, area.h()/spanZ);
        float w = (float) (spanX*scale), h = (float) (spanZ*scale);
        float l = area.l+(area.w()-w)*.5f, t = area.t+(area.h()-h)*.5f;
        return new Box(l, t, l+w, t+h);
    }

    /**
     * Where to draw a whole map texture (bounds aspect spanU:spanV) so that only its drawn part,
     * the fractions [f0, f1] of the texture, fills and centres in [area]. MapTransform given this
     * box maps the bounds onto it exactly, so markers stay aligned with the art.
     */
    static Box contentFit(double spanU, double spanV, double fu0, double fv0, double fu1, double fv1, Box area) {
        double du = Math.max(1e-6, fu1-fu0)*spanU, dv = Math.max(1e-6, fv1-fv0)*spanV;
        double k = Math.min(area.w()/du, area.h()/dv);
        double w = spanU*k, h = spanV*k;
        double l = area.cx()-(fu0+fu1)*.5*w, t = area.cy()-(fv0+fv1)*.5*h;
        return new Box((float) l, (float) t, (float) (l+w), (float) (t+h));
    }
}
