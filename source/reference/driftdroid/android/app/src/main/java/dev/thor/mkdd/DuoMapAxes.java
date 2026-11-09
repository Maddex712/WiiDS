package dev.thor.mkdd;

/**
 * World X/Z to a minimap texture's own axes. The retail minimap table gives each course an
 * orientation 0-3; the staged manifest records it as the signed world axis along the texture's
 * u (right) and v (down), e.g. ["-x","-z"] for orientation 2. Feeding MapTransform u/v keeps
 * the art exactly as the game draws it.
 */
final class DuoMapAxes {
    private final boolean uIsZ, uNegative, vIsZ, vNegative;

    private DuoMapAxes(boolean uIsZ, boolean uNegative, boolean vIsZ, boolean vNegative) {
        this.uIsZ=uIsZ; this.uNegative=uNegative; this.vIsZ=vIsZ; this.vNegative=vNegative;
    }

    /** Null unless u and v are "x", "-x", "z" or "-z" naming different world axes. */
    static DuoMapAxes parse(String u, String v) {
        if (!valid(u) || !valid(v) || u.endsWith("z") == v.endsWith("z")) return null;
        return new DuoMapAxes(u.endsWith("z"), u.startsWith("-"), v.endsWith("z"), v.startsWith("-"));
    }

    double u(double x, double z) { double a = uIsZ ? z : x; return uNegative ? -a : a; }
    double v(double x, double z) { double b = vIsZ ? z : x; return vNegative ? -b : b; }

    /** Heading in the same convention MapTransform uses: direction (sin, cos) over (u, v). */
    double heading(double radians) {
        double dx = Math.sin(radians), dz = Math.cos(radians);
        return Math.atan2(u(dx, dz), v(dx, dz));
    }

    private static boolean valid(String a) {
        return "x".equals(a) || "-x".equals(a) || "z".equals(a) || "-z".equals(a);
    }
}
