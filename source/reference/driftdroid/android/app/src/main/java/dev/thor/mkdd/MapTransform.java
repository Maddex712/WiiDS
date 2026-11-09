package dev.thor.mkdd;

/** Course-space X/Z -> lower canvas. Supply course-specific verified map bounds. */
public final class MapTransform {
    private final double minX, minZ, scale, left, top, width, height;
    private final boolean mirror;
    public MapTransform(double minX, double minZ, double maxX, double maxZ,
                        double left, double top, double width, double height, boolean mirror) {
        for (double v : new double[]{minX,minZ,maxX,maxZ,left,top,width,height})
            if (!Double.isFinite(v)) throw new IllegalArgumentException("Non-finite map bounds");
        if (maxX <= minX || maxZ <= minZ || width <= 0 || height <= 0)
            throw new IllegalArgumentException("Empty map bounds");
        if (!Double.isFinite(maxX-minX) || !Double.isFinite(maxZ-minZ))
            throw new IllegalArgumentException("Overflowing map bounds");
        this.minX=minX; this.minZ=minZ; this.mirror=mirror;
        scale=Math.min(width/(maxX-minX), height/(maxZ-minZ));
        if (!Double.isFinite(scale) || scale<=0) throw new IllegalArgumentException("Invalid map scale");
        this.width=(maxX-minX)*scale; this.height=(maxZ-minZ)*scale;
        this.left=left+(width-this.width)/2; this.top=top+(height-this.height)/2;
    }
    // No edge clamping: out-of-map karts are omitted, never presented at a false position.
    public Point project(double x, double z, double headingRadians) {
        if (!Double.isFinite(x) || !Double.isFinite(z) || !Double.isFinite(headingRadians)) return null;
        double px=(x-minX)*scale, py=(z-minZ)*scale;
        if (px < 0 || px > width || py < 0 || py > height) return null;
        double dx=Math.sin(headingRadians), dy=Math.cos(headingRadians);
        // Caller passes unmirrored course-space bounds and canonical coordinates.
        // A mirrored course asset must use the same reflected X convention.
        return new Point(left+(mirror ? width-px : px), top+py, mirror ? -dx : dx, dy);
    }
    public static final class Point {
        public final double x,y,headingX,headingY;
        // Package-private, not private: see LowerState.Kart.
        Point(double x,double y,double hx,double hy) { this.x=x; this.y=y; headingX=hx; headingY=hy; }
    }
}
