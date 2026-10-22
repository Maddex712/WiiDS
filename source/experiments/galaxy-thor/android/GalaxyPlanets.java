package dev.thor.galaxy;

import android.graphics.RectF;
import android.opengl.Matrix;
import android.util.Log;
import java.io.File;
import java.io.RandomAccessFile;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.channels.FileChannel;
import java.util.HashMap;

/**
 * The planet Mario stands on, as a 3D model the player can turn on the lower touchscreen (like
 * Captain Toad's dioramas), with what stands on and around it. The meshes are the game's own
 * models with their own textures, normals and vertex colours, baked privately by
 * Bake-LowerPlanets.py into files/ThorHud/planets/<Galaxy>.planets ('GPL4'): each model once,
 * and each placed object as a model and its placement.
 *
 * What is drawn, and where, follows the game: the live world state (port/thor_world.h, through
 * GalaxyActivity.nativeWorldState) says for each placed object, by zone and l_id, whether it
 * exists and shows now and where its model is, so pieces a cutscene or switch brings appear when
 * they do, moving and turning parts move, and broken or collected ones go; a flower group's
 * flowers stand where the game put them. Without the live state (an older game library), each
 * piece stands at its placement, and those a switch or cutscene makes appear stay hidden.
 *
 * This class reads the file (memory-mapped: vertex data goes straight to the GPU) and runs the
 * camera; GalaxyPlanetRenderer draws on the GPU on its own thread. On its own the view looks down
 * on Mario from above his head and follows him round the planet; a drag turns it, a pinch zooms,
 * a double tap returns to Mario, and three seconds after the last touch it drifts back to him.
 */
final class GalaxyPlanets {
    private static final String TAG = "GalaxyLower";
    static final int VERTEX_BYTES = 28;
    // batch flags (lowermap/j3dmesh.py)
    static final int F_LIT = 1, F_ALPHA_TEST = 2, F_BLEND = 4, F_ADDITIVE = 8, F_TWO_SIDED = 16, F_TEXTURED = 32;
    // piece gates (Bake-LowerPlanets.py)
    static final int GATE_SWITCH = 1, GATE_CUTSCENE = 2, GATE_LIVE_ONLY = 4;
    // live piece flags (port/thor_world.h)
    static final int LIVE_ALIVE = 1, LIVE_HIDDEN = 2, LIVE_MATRIX = 4;
    static final int WORLD_HEADER = 16 + 64 * 48, WORLD_PIECE = 72, WORLD_PIECES = 4096;
    /** Triangles one diorama may hold: far below what the GPU draws in a frame. */
    static final int TRIANGLE_BUDGET = 250000;

    /** One model, stored once. */
    static final class Mesh {
        String name;
        float cx, cy, cz, r;                 // model space
        int vertexOffset, vertexCount, indexOffset, indexCount;
        int[] batches;                       // per batch: first index, index count, texture (-1 none), flags
    }

    /** One placed object: a mesh where the game has it. */
    static final class Planet {
        String name;
        Mesh mesh;
        final float[] placement = new float[16];  // column-major, with scale (the bake's)
        float cx, cy, cz, r;                 // world, at placement
        int zone, layer;                     // layer -1: common (in every mission)
        boolean planet;                      // from the game's planet table
        boolean ground;                      // solid (else scenery)
        int linkId, gate, child;
        int tris;
        // live (UI thread)
        int live = -1;                       // its entry in the world state, or -1
        boolean visible;
        final float[] model = new float[16];  // column-major, where it is drawn now
        float lx, ly, lz;                    // its centre now
    }

    final String galaxy;
    final String[] zones;
    final Mesh[] meshes;
    final Planet[] planets;
    final ByteBuffer data;            // the whole file, little-endian
    final int[] textureOffset, textureLength, textureWrapS, textureWrapT;
    private final int[] missions;     // scenario numbers
    private final int[][] masks;      // [mission][zone] layer mask
    private final HashMap<Long, Planet> byKey = new HashMap<>();
    private int worldGeneration = -1;
    private boolean worldValid;

    private GalaxyPlanets(String galaxy, String[] zones, Mesh[] meshes, Planet[] planets, ByteBuffer data, int[][] textures,
                          int[] missions, int[][] masks) {
        this.galaxy = galaxy; this.zones = zones; this.meshes = meshes; this.planets = planets; this.data = data;
        this.missions = missions; this.masks = masks;
        textureOffset = textures[0]; textureLength = textures[1]; textureWrapS = textures[2]; textureWrapT = textures[3];
        for (Planet p : planets) {
            byKey.put(key(p.zone, p.linkId, p.child), p);
            System.arraycopy(p.placement, 0, p.model, 0, 16);
            p.lx = p.cx; p.ly = p.cy; p.lz = p.cz;
            p.visible = (p.gate & (GATE_SWITCH | GATE_CUTSCENE | GATE_LIVE_ONLY)) == 0;
        }
    }

    private static long key(int zone, int linkId, int child) {
        return ((long) zone << 48) | ((long) (child & 0xffff) << 32) | (linkId & 0xffffffffL);
    }

    /** Whether [p] exists in mission [scenario] (unknown missions show everything). */
    boolean active(Planet p, int scenario) {
        if (p.layer < 0) return true;
        for (int i = 0; i < missions.length; i++) {
            if (missions[i] != scenario) continue;
            int[] m = masks[i];
            return p.zone >= m.length || ((m[p.zone] >> p.layer) & 1) != 0;
        }
        return true;
    }

    /** Whether [p] is to be drawn now: in this mission and showing. */
    boolean shown(Planet p, int scenario) { return p.visible && active(p, scenario); }

    private static String text(ByteBuffer b, int at, int max) {
        StringBuilder s = new StringBuilder();
        for (int k = 0; k < max && b.get(at + k) != 0; k++) s.append((char) b.get(at + k));
        return s.toString();
    }

    /** Reads a baked galaxy; null when it is missing or not a planets file. */
    static GalaxyPlanets load(File file, String galaxy) {
        try (RandomAccessFile raf = new RandomAccessFile(file, "r"); FileChannel channel = raf.getChannel()) {
            ByteBuffer b = channel.map(FileChannel.MapMode.READ_ONLY, 0, channel.size()).order(ByteOrder.LITTLE_ENDIAN);
            if (b.getInt(0) != 0x344c5047) return null;  // 'GPL4'
            int zoneCount = b.getInt(4), at = 8;
            String[] zones = new String[zoneCount];
            for (int z = 0; z < zoneCount; z++, at += 48) zones[z] = text(b, at, 48);
            int missionCount = b.getInt(at); at += 4;
            int[] missions = new int[missionCount];
            int[][] masks = new int[missionCount][zoneCount];
            for (int i = 0; i < missionCount; i++) {
                missions[i] = b.getInt(at); at += 4;
                for (int z = 0; z < zoneCount; z++) { masks[i][z] = b.getInt(at); at += 4; }
            }
            int textureCount = b.getInt(at); at += 4;
            int[][] textures = new int[4][textureCount];
            for (int i = 0; i < textureCount; i++) {
                textures[2][i] = b.get(at + 4) & 0xff; textures[3][i] = b.get(at + 5) & 0xff;
                int length = b.getInt(at + 8);
                textures[0][i] = at + 12; textures[1][i] = length;
                at += 12 + ((length + 3) & ~3);
            }
            int meshCount = b.getInt(at); at += 4;
            Mesh[] meshes = new Mesh[meshCount];
            for (int i = 0; i < meshCount; i++) {
                Mesh m = new Mesh();
                m.name = text(b, at, 48);
                m.cx = b.getFloat(at + 48); m.cy = b.getFloat(at + 52); m.cz = b.getFloat(at + 56); m.r = b.getFloat(at + 60);
                m.vertexCount = b.getInt(at + 64); m.indexCount = b.getInt(at + 68);
                int batches = b.getInt(at + 72);
                at += 76;
                m.batches = new int[batches * 4];
                for (int k = 0; k < batches * 4; k++) m.batches[k] = b.getInt(at + 4 * k);
                at += 16 * batches;
                m.vertexOffset = at; at += m.vertexCount * VERTEX_BYTES;
                m.indexOffset = at; at += m.indexCount * 4;
                meshes[i] = m;
            }
            int count = b.getInt(at); at += 4;
            Planet[] out = new Planet[count];
            for (int i = 0; i < count; i++, at += 128) {
                Planet p = new Planet();
                p.name = text(b, at, 48);
                p.mesh = meshes[b.getInt(at + 48)];
                rowMajorToGl(b, at + 52, p.placement);
                p.cx = b.getFloat(at + 100); p.cy = b.getFloat(at + 104); p.cz = b.getFloat(at + 108); p.r = b.getFloat(at + 112);
                p.zone = b.get(at + 116) & 0xff; p.layer = b.get(at + 117); p.planet = b.get(at + 118) != 0;
                p.ground = b.get(at + 119) == 0;
                p.linkId = b.getInt(at + 120);
                p.gate = b.getShort(at + 124) & 0xffff; p.child = b.getShort(at + 126) & 0xffff;
                p.tris = p.mesh.indexCount / 3;
                out[i] = p;
            }
            Log.i(TAG, "planets: " + galaxy + " " + count + " pieces, " + meshCount + " models, " + textureCount + " textures");
            return new GalaxyPlanets(galaxy, zones, meshes, out, b, textures, missions, masks);
        } catch (Exception broken) {
            Log.w(TAG, "planets " + galaxy + " unreadable", broken);
            return null;
        }
    }

    /** A row-major 3x4 (12 floats at [at]) as a column-major 4x4. */
    private static void rowMajorToGl(ByteBuffer b, int at, float[] out) {
        for (int r = 0; r < 3; r++) for (int c = 0; c < 4; c++) out[c * 4 + r] = b.getFloat(at + 4 * (r * 4 + c));
        out[3] = 0; out[7] = 0; out[11] = 0; out[15] = 1;
    }

    /**
     * Applies the game's live world state ([world]: header and pieces, port/thor_world.h) to every
     * piece. Returns whether it described this galaxy; without it, pieces keep their placements.
     */
    boolean applyWorld(ByteBuffer world, boolean fresh) {
        if (world == null || world.getInt(0) != 1) return worldValid = false;
        int generation = world.getInt(4), count = Math.min(world.getInt(8), WORLD_PIECES), zoneCount = Math.min(world.getInt(12), 64);
        if (fresh || generation != worldGeneration) {
            worldGeneration = generation;
            // Map the live zones to this file's zones by name, then each live piece to its baked piece.
            int[] zoneMap = new int[zoneCount];
            boolean ours = false;
            for (int z = 0; z < zoneCount; z++) {
                String name = text(world, 16 + z * 48, 48);
                zoneMap[z] = -1;
                for (int k = 0; k < zones.length; k++) if (zones[k].equals(name)) { zoneMap[z] = k; break; }
                if (name.equals(galaxy)) ours = true;
            }
            for (Planet p : planets) p.live = -1;
            worldValid = ours && count > 0;
            if (!worldValid) return false;
            int matched = 0;
            for (int i = 0; i < count; i++) {
                int at = WORLD_HEADER + i * WORLD_PIECE, z = world.getShort(at);
                if (z < 0 || z >= zoneCount || zoneMap[z] < 0) continue;
                Planet p = byKey.get(key(zoneMap[z], world.getInt(at + 4), world.getShort(at + 8)));
                if (p != null) { p.live = i; matched++; }
            }
            Log.i(TAG, "world: generation " + generation + ", " + count + " live objects, " + matched + "/" + planets.length + " pieces matched");
        }
        if (!worldValid) return false;
        for (Planet p : planets) {
            if (p.live < 0) {
                // Not in the scene as a live object: at its placement, unless a switch, a
                // cutscene or the game itself would have placed it.
                p.visible = (p.gate & (GATE_SWITCH | GATE_CUTSCENE | GATE_LIVE_ONLY)) == 0;
                continue;
            }
            int at = WORLD_HEADER + p.live * WORLD_PIECE, flags = world.getShort(at + 2) & 0xffff;
            p.visible = (flags & LIVE_ALIVE) != 0 && (flags & LIVE_HIDDEN) == 0;
            float sx = world.getFloat(at + 60), sy = world.getFloat(at + 64), sz = world.getFloat(at + 68);
            float[] m = p.model;
            if ((flags & LIVE_MATRIX) != 0) {
                for (int r = 0; r < 3; r++) {
                    m[r] = world.getFloat(at + 12 + 4 * (r * 4)) * sx;
                    m[4 + r] = world.getFloat(at + 12 + 4 * (r * 4 + 1)) * sy;
                    m[8 + r] = world.getFloat(at + 12 + 4 * (r * 4 + 2)) * sz;
                    m[12 + r] = world.getFloat(at + 12 + 4 * (r * 4 + 3));
                }
            } else {
                trs(world.getFloat(at + 12), world.getFloat(at + 16), world.getFloat(at + 20), world.getFloat(at + 24),
                    world.getFloat(at + 40), world.getFloat(at + 56), sx, sy, sz, m);
            }
            m[3] = 0; m[7] = 0; m[11] = 0; m[15] = 1;
            Mesh mesh = p.mesh;
            p.lx = m[0] * mesh.cx + m[4] * mesh.cy + m[8] * mesh.cz + m[12];
            p.ly = m[1] * mesh.cx + m[5] * mesh.cy + m[9] * mesh.cz + m[13];
            p.lz = m[2] * mesh.cx + m[6] * mesh.cy + m[10] * mesh.cz + m[14];
        }
        return true;
    }

    /** MR::makeMtxTR with scale (rotation in degrees, Z * Y * X), column-major into [m]. */
    private static void trs(float rxd, float ryd, float rzd, float tx, float ty, float tz, float sx, float sy, float sz, float[] m) {
        double rx = Math.toRadians(rxd), ry = Math.toRadians(ryd), rz = Math.toRadians(rzd);
        float a = (float) Math.sin(rx), ca = (float) Math.cos(rx), b = (float) Math.sin(ry), cb = (float) Math.cos(ry);
        float c = (float) Math.sin(rz), cc = (float) Math.cos(rz);
        m[0] = cc * cb * sx;                 m[4] = (cc * b * a - c * ca) * sy; m[8] = (cc * b * ca + c * a) * sz;
        m[1] = c * cb * sx;                  m[5] = (c * b * a + cc * ca) * sy; m[9] = (c * b * ca - cc * a) * sz;
        m[2] = -b * sx;                      m[6] = cb * a * sy;                m[10] = cb * ca * sz;
        m[12] = tx; m[13] = ty; m[14] = tz;
    }

    /** The ground Mario is on: the shown solid piece whose surface sphere he is nearest. */
    Planet nearest(float x, float y, float z, Planet current, int scenario) {
        Planet best = null;
        float bestGap = Float.MAX_VALUE, currentGap = Float.MAX_VALUE;
        for (Planet p : planets) {
            if (!p.ground || !shown(p, scenario)) continue;
            float dx = x - p.lx, dy = y - p.ly, dz = z - p.lz;
            float gap = Math.max(0, (float) Math.sqrt(dx * dx + dy * dy + dz * dz) - p.r);
            if (p == current) currentGap = gap;
            if (gap < bestGap || (gap == bestGap && best != null && p.r < best.r)) { bestGap = gap; best = p; }
        }
        // Keep the current planet unless another is clearly nearer (no flicker between neighbours).
        if (current != null && currentGap - bestGap < 150) return current;
        return best;
    }

    /** The largest shown planet of [scenario]: what a preview shows without Mario. */
    Planet largest(int scenario) {
        Planet best = null;
        for (Planet p : planets) {
            if (!p.ground || !shown(p, scenario)) continue;
            if (best == null || (p.planet && !best.planet) || (p.planet == best.planet && p.r > best.r)) best = p;
        }
        return best;
    }

    /**
     * The diorama around [main]: it and the pieces of this mission close to it (a planet's
     * platforms and scenery, a course's neighbouring blocks), nearest first, within a triangle
     * budget. Whether each shows is decided every frame, from the live state.
     */
    Planet[] neighbourhood(Planet main, int scenario, float[] outRadius) {
        float reach = Math.max(main.r * 1.25f, 3000);
        java.util.ArrayList<Planet> near = new java.util.ArrayList<>();
        java.util.ArrayList<Float> gaps = new java.util.ArrayList<>();
        for (Planet p : planets) {
            if (p == main || !active(p, scenario)) continue;
            if (!p.ground && p.r > main.r * 2.5f) continue;  // a backdrop larger than the world (desert, asteroids)
            float dx = p.lx - main.lx, dy = p.ly - main.ly, dz = p.lz - main.lz;
            float d = (float) Math.sqrt(dx * dx + dy * dy + dz * dz);
            if (d - p.r > reach) continue;
            int at = 0;
            while (at < gaps.size() && gaps.get(at) < d) at++;
            near.add(at, p); gaps.add(at, d);
        }
        java.util.ArrayList<Planet> group = new java.util.ArrayList<>();
        group.add(main);
        int budget = TRIANGLE_BUDGET - main.tris;
        float radius = main.r;
        for (int i = 0; i < near.size() && group.size() < 1500; i++) {
            Planet p = near.get(i);
            if (p.tris > budget) continue;
            budget -= p.tris;
            group.add(p);
            if (p.ground) radius = Math.max(radius, gaps.get(i) + p.r);
        }
        outRadius[0] = Math.min(radius, Math.max(main.r * 1.6f, 3500));
        return group.toArray(new Planet[0]);
    }

    /** What the renderer draws: one camera over one diorama. Immutable once submitted. */
    static final class Frame {
        GalaxyPlanets source;
        Planet[] pieces;             // the shown pieces
        float[] models;              // each piece's model matrix (16 floats, column-major)
        final float[] mvp = new float[16];
        float ox, oy, oz;            // world origin of the view (the main planet's centre)
        float ex, ey, ez;            // eye, relative to the origin
        float lx, ly, lz;            // unit light direction
        float near, far;
        final RectF area = new RectF();  // lower-screen units (GalaxyLowerScreen.W x H)

        /** Takes the shown pieces of [group] and where they are now. */
        void capture(GalaxyPlanets source, Planet[] group, int scenario) {
            int n = 0;
            for (Planet p : group) if (source.shown(p, scenario)) n++;
            pieces = new Planet[n];
            models = new float[n * 16];
            int i = 0;
            for (Planet p : group) {
                if (!source.shown(p, scenario)) continue;
                pieces[i] = p;
                System.arraycopy(p.model, 0, models, i * 16, 16);
                i++;
            }
        }

        boolean sameView(Frame o) {
            if (o == null || o.source != source || !java.util.Arrays.equals(o.pieces, pieces) || !o.area.equals(area)) return false;
            return java.util.Arrays.equals(o.mvp, mvp) && java.util.Arrays.equals(o.models, models);
        }
    }

    // ------------------------------------------------------------------ the camera
    /** One lower screen's orbit camera over a planet. */
    static final class View {
        private static final float FOV = 30f;
        private Planet shown;
        // Camera: e points from the planet's centre toward the viewer; u is the screen's up.
        private float ex = 0, ey = 1, ez = 0, ux = 0, uy = 0, uz = -1;
        private float zoom = 1, fade;
        private long lastTouch, lastFrame;
        private final float[] proj = new float[16], view = new float[16], clip = new float[4], point = new float[4];

        void reset() { lastTouch = 0; zoom = 1; }

        float fade() { return fade; }

        void drag(float dx, float dy) {
            lastTouch = System.currentTimeMillis();
            float k = 0.006f / zoom;
            // yaw about the screen's up, pitch about its right
            float rx = uy * ez - uz * ey, ry = uz * ex - ux * ez, rz = ux * ey - uy * ex;
            rotate(ux, uy, uz, -dx * k);
            rotate(rx, ry, rz, -dy * k);
        }

        /** A slow turn about the screen's up (the preview's showcase spin). */
        void spin(float angle) { rotate(ux, uy, uz, angle); }

        void pinch(float factor) {
            lastTouch = System.currentTimeMillis();
            zoom = Math.max(0.6f, Math.min(3.5f, zoom * factor));
        }

        /** Turns the camera (e and u) about the unit axis (ax, ay, az) by [angle]. */
        private void rotate(float ax, float ay, float az, float angle) {
            float len = (float) Math.sqrt(ax * ax + ay * ay + az * az);
            if (len < 1e-6f) return;
            ax /= len; ay /= len; az /= len;
            float[] e = rodrigues(ex, ey, ez, ax, ay, az, angle), u = rodrigues(ux, uy, uz, ax, ay, az, angle);
            ex = e[0]; ey = e[1]; ez = e[2]; ux = u[0]; uy = u[1]; uz = u[2];
            orthonormalise();
        }

        private static float[] rodrigues(float vx, float vy, float vz, float ax, float ay, float az, float a) {
            float c = (float) Math.cos(a), s = (float) Math.sin(a), d = (vx * ax + vy * ay + vz * az) * (1 - c);
            return new float[]{vx * c + (ay * vz - az * vy) * s + ax * d, vy * c + (az * vx - ax * vz) * s + ay * d,
                vz * c + (ax * vy - ay * vx) * s + az * d};
        }

        private void orthonormalise() {
            float len = (float) Math.sqrt(ex * ex + ey * ey + ez * ez);
            ex /= len; ey /= len; ez /= len;
            float d = ux * ex + uy * ey + uz * ez;
            ux -= d * ex; uy -= d * ey; uz -= d * ez;
            len = (float) Math.sqrt(ux * ux + uy * uy + uz * uz);
            if (len < 1e-4f) {  // u fell onto e: any perpendicular
                ux = Math.abs(ex) < 0.9f ? 1 : 0; uy = Math.abs(ex) < 0.9f ? 0 : 1; uz = 0;
                d = ux * ex + uy * ey + uz * ez; ux -= d * ex; uy -= d * ey; uz -= d * ez;
                len = (float) Math.sqrt(ux * ux + uy * uy + uz * uz);
            }
            ux /= len; uy /= len; uz /= len;
        }

        /**
         * Moves the camera on a frame and fills [out] for the renderer; returns Mario's point on
         * the lower screen and whether he faces the viewer ({x, y, front}), or null without him.
         */
        float[] frame(Frame out, GalaxyPlanets source, RectF area, Planet planet, Planet[] group, float radius,
                      boolean hasMario, float mx, float my, float mz, float upx, float upy, float upz) {
            long now = System.currentTimeMillis();
            float dt = lastFrame == 0 ? 0 : Math.min(0.1f, (now - lastFrame) / 1000f);
            lastFrame = now;
            if (planet != shown) {
                shown = planet; fade = 0; zoom = 1;
                if (hasMario) { snapTo(planet, mx, my, mz, upx, upy, upz); }
            }
            fade = Math.min(1, fade + dt * 2.5f);
            boolean auto = now - lastTouch > 3000;
            if (auto && hasMario) follow(planet, mx, my, mz, upx, upy, upz, dt);
            if (auto) zoom += (1 - zoom) * Math.min(1, dt * 2);

            // Perspective, zoomed by narrowing the lens (the camera stays outside the diorama).
            float half = (float) Math.toRadians(FOV / 2);
            float dist = radius / (float) Math.sin(half) * 1.02f;
            float fov = (float) Math.toDegrees(2 * Math.atan(Math.tan(half) / zoom));
            float near = Math.max(dist - radius * 3, dist * 0.05f), far = dist + radius * 3;
            Matrix.perspectiveM(proj, 0, fov, area.width() / area.height(), near, far);
            Matrix.setLookAtM(view, 0, ex * dist, ey * dist, ez * dist, 0, 0, 0, ux, uy, uz);
            Matrix.multiplyMM(out.mvp, 0, proj, 0, view, 0);
            out.source = source;
            out.ox = planet.lx; out.oy = planet.ly; out.oz = planet.lz;
            out.ex = ex * dist; out.ey = ey * dist; out.ez = ez * dist;
            // Light from the viewer's upper left.
            float rx = uy * ez - uz * ey, ry = uz * ex - ux * ez, rz = ux * ey - uy * ex;  // screen right
            float lx = 0.55f * ex + 0.6f * ux - 0.35f * rx, ly = 0.55f * ey + 0.6f * uy - 0.35f * ry, lz = 0.55f * ez + 0.6f * uz - 0.35f * rz;
            float ll = (float) Math.sqrt(lx * lx + ly * ly + lz * lz);
            out.lx = lx / ll; out.ly = ly / ll; out.lz = lz / ll;
            out.near = dist - radius; out.far = dist + radius;
            out.area.set(area);
            if (!hasMario) return null;
            point[0] = mx - planet.lx; point[1] = my - planet.ly; point[2] = mz - planet.lz; point[3] = 1;
            Matrix.multiplyMV(clip, 0, out.mvp, 0, point, 0);
            if (clip[3] <= 0) return null;
            float front = (upx * ex + upy * ey + upz * ez);  // Mario faces the viewer when his up does
            return new float[]{area.left + (clip[0] / clip[3] * 0.5f + 0.5f) * area.width(),
                area.top + (0.5f - clip[1] / clip[3] * 0.5f) * area.height(), front};
        }

        /** The planet's radius on screen (lower-screen units), for the glow behind it. */
        float screenRadius(RectF area, float planetRadius, float radius) {
            float half = (float) Math.toRadians(FOV / 2);
            return planetRadius / radius * area.height() * 0.5f * zoom * (float) Math.cos(half) / 1.02f;
        }

        /** Looks straight down on Mario from above his head at once. */
        private void snapTo(Planet p, float mx, float my, float mz, float upx, float upy, float upz) {
            float[] d = viewDir(p, mx, my, mz, upx, upy, upz);
            ex = d[0]; ey = d[1]; ez = d[2];
            orthonormalise();
        }

        /** Eases the view toward looking down on Mario (from his up, leaning toward the centre line). */
        private void follow(Planet p, float mx, float my, float mz, float upx, float upy, float upz, float dt) {
            float[] d = viewDir(p, mx, my, mz, upx, upy, upz);
            float a = Math.min(1, dt / 0.45f);
            ex += (d[0] - ex) * a; ey += (d[1] - ey) * a; ez += (d[2] - ez) * a;
            orthonormalise();
        }

        private static float[] viewDir(Planet p, float mx, float my, float mz, float upx, float upy, float upz) {
            float dx = mx - p.lx, dy = my - p.ly, dz = mz - p.lz;
            float len = (float) Math.sqrt(dx * dx + dy * dy + dz * dz);
            if (len > 1e-3f) { dx /= len; dy /= len; dz /= len; }
            // Half his up direction, half the direction from the centre to him: on a round planet
            // these agree; on odd shapes it keeps him in view and the planet centred.
            float x = upx + dx, y = upy + dy, z = upz + dz;
            float l = (float) Math.sqrt(x * x + y * y + z * z);
            if (l < 1e-3f) return new float[]{0, 1, 0};
            return new float[]{x / l, y / l, z / l};
        }
    }
}
