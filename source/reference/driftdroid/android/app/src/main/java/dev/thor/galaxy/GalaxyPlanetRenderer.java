package dev.thor.galaxy;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.SurfaceTexture;
import android.opengl.EGL14;
import android.opengl.EGLConfig;
import android.opengl.EGLContext;
import android.opengl.EGLDisplay;
import android.opengl.EGLSurface;
import android.opengl.GLES20;
import android.opengl.GLES30;
import android.os.Handler;
import android.os.HandlerThread;
import android.util.Log;
import android.view.TextureView;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.HashMap;
import java.util.Map;

/**
 * Draws the lower screen's planet diorama on the GPU: the game's own models, textured, smooth
 * shaded, depth-tested and 4x multisampled, into a transparent TextureView laid over the lower
 * screen. Its own thread, at a lower priority than the game; it draws only when the view
 * changes (the camera moves, a piece moves, shows or goes), so a still view costs nothing. Each
 * model is uploaded once and drawn once per placed object, with that object's live matrix. Shader as in
 * the bake preview (experiments/galaxy-thor: planetgl preview).
 */
final class GalaxyPlanetRenderer implements TextureView.SurfaceTextureListener {
    private static final String TAG = "GalaxyLower";
    private static final int EGL_OPENGL_ES3_BIT = 0x40, MAX_ANISOTROPY = 0x84FE;

    private final HandlerThread thread = new HandlerThread("GalaxyPlanetGL", 2);  // a little below normal
    private final Handler gl;
    private final Runnable drawTask = this::draw;
    private volatile GalaxyPlanets.Frame pending;
    private volatile boolean failed;
    private boolean scheduled;
    private final Object lock = new Object();

    // GL thread only
    private EGLDisplay display = EGL14.EGL_NO_DISPLAY;
    private EGLContext context = EGL14.EGL_NO_CONTEXT;
    private EGLSurface surface = EGL14.EGL_NO_SURFACE;
    private EGLConfig config;
    private SurfaceTexture texture;
    private int surfaceW, surfaceH, program, uMvp, uModel, uOrigin, uTex, uFlags, uEye, uLight, uNear, uFar;
    private boolean anisotropic;
    private GalaxyPlanets loadedFor;
    private final Map<GalaxyPlanets.Mesh, int[]> meshes = new HashMap<>();
    private final Map<Integer, Integer> textures = new HashMap<>();
    private GalaxyPlanets.Frame drawn;
    private long frames, drawNanos, lastDraw;
    /** At most 60 pictures a second, though the lower screen refreshes at 120 Hz. */
    private static final long FRAME_NANOS = 16_000_000L;

    GalaxyPlanetRenderer() {
        thread.start();
        gl = new Handler(thread.getLooper());
    }

    boolean failed() { return failed; }

    /** Draws [frame] when the GL thread is next free (a newer frame replaces a waiting one). */
    void submit(GalaxyPlanets.Frame frame) {
        pending = frame;
        synchronized (lock) {
            if (scheduled) return;
            scheduled = true;
        }
        gl.post(drawTask);
    }

    void release() {
        gl.post(() -> {
            freeScene();
            destroySurface();
            if (context != EGL14.EGL_NO_CONTEXT) EGL14.eglDestroyContext(display, context);
            if (display != EGL14.EGL_NO_DISPLAY) EGL14.eglTerminate(display);
            context = EGL14.EGL_NO_CONTEXT; display = EGL14.EGL_NO_DISPLAY;
        });
        thread.quitSafely();
    }

    // ---------------------------------------------------------------- TextureView
    @Override public void onSurfaceTextureAvailable(SurfaceTexture st, int w, int h) {
        gl.post(() -> { texture = st; surfaceW = w; surfaceH = h; drawn = null; createSurface(); });
    }

    @Override public void onSurfaceTextureSizeChanged(SurfaceTexture st, int w, int h) {
        gl.post(() -> { surfaceW = w; surfaceH = h; drawn = null; });
    }

    @Override public boolean onSurfaceTextureDestroyed(SurfaceTexture st) {
        // The GL thread lets go of the surface first, then releases it.
        gl.post(() -> { destroySurface(); texture = null; st.release(); });
        return false;
    }

    @Override public void onSurfaceTextureUpdated(SurfaceTexture st) { }

    // ---------------------------------------------------------------- EGL
    private boolean init() {
        if (context != EGL14.EGL_NO_CONTEXT) return true;
        if (failed) return false;
        display = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY);
        int[] version = new int[2];
        if (!EGL14.eglInitialize(display, version, 0, version, 1)) return fail("eglInitialize");
        int[][] wanted = {
            {EGL14.EGL_RED_SIZE, 8, EGL14.EGL_GREEN_SIZE, 8, EGL14.EGL_BLUE_SIZE, 8, EGL14.EGL_ALPHA_SIZE, 8, EGL14.EGL_DEPTH_SIZE, 24,
                EGL14.EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL14.EGL_SAMPLE_BUFFERS, 1, EGL14.EGL_SAMPLES, 4, EGL14.EGL_NONE},
            {EGL14.EGL_RED_SIZE, 8, EGL14.EGL_GREEN_SIZE, 8, EGL14.EGL_BLUE_SIZE, 8, EGL14.EGL_ALPHA_SIZE, 8, EGL14.EGL_DEPTH_SIZE, 24,
                EGL14.EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL14.EGL_NONE}};
        EGLConfig[] configs = new EGLConfig[1];
        int[] found = new int[1];
        for (int[] attribs : wanted) {
            if (EGL14.eglChooseConfig(display, attribs, 0, configs, 0, 1, found, 0) && found[0] > 0) { config = configs[0]; break; }
        }
        if (config == null) return fail("no EGL config");
        context = EGL14.eglCreateContext(display, config, EGL14.EGL_NO_CONTEXT, new int[]{EGL14.EGL_CONTEXT_CLIENT_VERSION, 3, EGL14.EGL_NONE}, 0);
        if (context == EGL14.EGL_NO_CONTEXT) return fail("eglCreateContext");
        return true;
    }

    private void createSurface() {
        if (texture == null || !init()) return;
        destroySurface();
        surface = EGL14.eglCreateWindowSurface(display, config, texture, new int[]{EGL14.EGL_NONE}, 0);
        if (surface == EGL14.EGL_NO_SURFACE) { fail("eglCreateWindowSurface"); return; }
        EGL14.eglMakeCurrent(display, surface, surface, context);
        EGL14.eglSwapInterval(display, 0);  // never wait on the compositor: the UI paces us
        if (program == 0) buildProgram();
    }

    private void destroySurface() {
        if (surface == EGL14.EGL_NO_SURFACE) return;
        EGL14.eglMakeCurrent(display, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_CONTEXT);
        EGL14.eglDestroySurface(display, surface);
        surface = EGL14.EGL_NO_SURFACE;
    }

    private boolean fail(String what) {
        Log.w(TAG, "planet renderer: " + what + " failed (0x" + Integer.toHexString(EGL14.eglGetError()) + ")");
        failed = true;
        return false;
    }

    // ---------------------------------------------------------------- shaders
    private static final String VS = "#version 300 es\n"
        + "precision highp float;\n"
        + "in vec3 aPos; in vec3 aNrm; in vec2 aUv; in vec4 aCol;\n"
        + "uniform mat4 uMvp; uniform mat4 uModel; uniform vec3 uOrigin;\n"
        + "out vec3 vN; out vec2 vUv; out vec4 vCol; out vec3 vW;\n"
        + "void main() {\n"
        + "    vN = mat3(uModel) * aNrm; vUv = aUv; vCol = aCol;\n"
        + "    vW = (uModel * vec4(aPos, 1.0)).xyz - uOrigin;\n"
        + "    gl_Position = uMvp * vec4(vW, 1.0);\n"
        + "}\n";
    private static final String FS = "#version 300 es\n"
        + "precision highp float; precision highp int;\n"
        + "in vec3 vN; in vec2 vUv; in vec4 vCol; in vec3 vW;\n"
        + "uniform sampler2D uTex; uniform int uFlags; uniform vec3 uEye; uniform vec3 uLight; uniform float uFar; uniform float uNear;\n"
        + "out vec4 frag;\n"
        + "void main() {\n"
        + "    vec4 base = vCol;\n"
        + "    if ((uFlags & 32) != 0) base *= texture(uTex, vUv);\n"
        + "    base.rgb = min(base.rgb * float(1 << ((uFlags >> 8) & 3)), vec3(1.0));\n"
        + "    if ((uFlags & 2) != 0 && base.a < 0.5) discard;\n"
        + "    if ((uFlags & 4) == 0) base.a = 1.0;\n"
        + "    vec3 n = normalize(vN);\n"
        + "    if (!gl_FrontFacing) n = -n;\n"
        + "    vec3 v = normalize(uEye - vW);\n"
        + "    float diff = clamp((dot(n, uLight) + 0.25) / 1.25, 0.0, 1.0);\n"
        + "    float lit = (uFlags & 1) != 0 ? 1.0 : 0.55;\n"
        + "    vec3 amb = mix(vec3(0.30, 0.26, 0.36), vec3(0.62, 0.70, 0.92), dot(n, v) * 0.5 + 0.5);\n"
        + "    vec3 c = base.rgb * mix(vec3(1.0), amb * 0.70 + vec3(1.0, 0.97, 0.9) * diff * 0.62, lit);\n"
        + "    vec3 h = normalize(uLight + v);\n"
        + "    c += vec3(1.0, 0.98, 0.9) * pow(max(dot(n, h), 0.0), 28.0) * 0.18 * lit;\n"
        + "    float rim = pow(1.0 - max(dot(n, v), 0.0), 3.0);\n"
        + "    c += vec3(0.45, 0.62, 1.0) * rim * 0.45;\n"
        + "    float depth = clamp((length(uEye - vW) - uNear) / max(uFar - uNear, 1.0), 0.0, 1.0);\n"
        + "    c = mix(c, vec3(0.16, 0.22, 0.45), depth * 0.25);\n"
        + "    vec3 over = max(c - 0.8, 0.0);\n"
        + "    c = min(c, 0.8) + over / (1.0 + over * 2.5);\n"
        + "    frag = vec4(c * base.a, base.a);\n"
        + "}\n";

    private void buildProgram() {
        int vs = compile(GLES20.GL_VERTEX_SHADER, VS), fs = compile(GLES20.GL_FRAGMENT_SHADER, FS);
        if (vs == 0 || fs == 0) { failed = true; return; }
        int p = GLES20.glCreateProgram();
        GLES20.glAttachShader(p, vs);
        GLES20.glAttachShader(p, fs);
        GLES20.glBindAttribLocation(p, 0, "aPos");
        GLES20.glBindAttribLocation(p, 1, "aNrm");
        GLES20.glBindAttribLocation(p, 2, "aUv");
        GLES20.glBindAttribLocation(p, 3, "aCol");
        GLES20.glLinkProgram(p);
        int[] ok = new int[1];
        GLES20.glGetProgramiv(p, GLES20.GL_LINK_STATUS, ok, 0);
        if (ok[0] == 0) { Log.w(TAG, "planet shader link: " + GLES20.glGetProgramInfoLog(p)); failed = true; return; }
        program = p;
        uMvp = GLES20.glGetUniformLocation(p, "uMvp");
        uModel = GLES20.glGetUniformLocation(p, "uModel");
        uOrigin = GLES20.glGetUniformLocation(p, "uOrigin");
        uTex = GLES20.glGetUniformLocation(p, "uTex");
        uFlags = GLES20.glGetUniformLocation(p, "uFlags");
        uEye = GLES20.glGetUniformLocation(p, "uEye");
        uLight = GLES20.glGetUniformLocation(p, "uLight");
        uNear = GLES20.glGetUniformLocation(p, "uNear");
        uFar = GLES20.glGetUniformLocation(p, "uFar");
        String ext = GLES20.glGetString(GLES20.GL_EXTENSIONS);
        anisotropic = ext != null && ext.contains("GL_EXT_texture_filter_anisotropic");
        Log.i(TAG, "planet renderer ready: " + GLES20.glGetString(GLES20.GL_RENDERER) + (anisotropic ? ", anisotropic" : ""));
    }

    private static int compile(int type, String source) {
        int s = GLES20.glCreateShader(type);
        GLES20.glShaderSource(s, source);
        GLES20.glCompileShader(s);
        int[] ok = new int[1];
        GLES20.glGetShaderiv(s, GLES20.GL_COMPILE_STATUS, ok, 0);
        if (ok[0] == 0) { Log.w(TAG, "planet shader: " + GLES20.glGetShaderInfoLog(s)); return 0; }
        return s;
    }

    // ---------------------------------------------------------------- scene
    private void freeScene() {
        if (context == EGL14.EGL_NO_CONTEXT || surface == EGL14.EGL_NO_SURFACE) { meshes.clear(); textures.clear(); return; }
        for (int[] m : meshes.values()) GLES20.glDeleteBuffers(2, m, 0);
        int[] one = new int[1];
        for (int t : textures.values()) { one[0] = t; GLES20.glDeleteTextures(1, one, 0); }
        meshes.clear();
        textures.clear();
    }

    private int[] mesh(GalaxyPlanets source, GalaxyPlanets.Mesh p) {
        int[] m = meshes.get(p);
        if (m != null) return m;
        m = new int[2];
        GLES20.glGenBuffers(2, m, 0);
        ByteBuffer v = slice(source.data, p.vertexOffset, p.vertexCount * GalaxyPlanets.VERTEX_BYTES);
        GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, m[0]);
        GLES20.glBufferData(GLES20.GL_ARRAY_BUFFER, v.remaining(), v, GLES20.GL_STATIC_DRAW);
        ByteBuffer i = slice(source.data, p.indexOffset, p.indexCount * 4);
        GLES20.glBindBuffer(GLES20.GL_ELEMENT_ARRAY_BUFFER, m[1]);
        GLES20.glBufferData(GLES20.GL_ELEMENT_ARRAY_BUFFER, i.remaining(), i, GLES20.GL_STATIC_DRAW);
        meshes.put(p, m);
        return m;
    }

    private static ByteBuffer slice(ByteBuffer data, int offset, int length) {
        ByteBuffer b = data.duplicate();
        b.position(offset);
        b.limit(offset + length);
        return b.slice().order(ByteOrder.LITTLE_ENDIAN);
    }

    private int texture(GalaxyPlanets source, int index) {
        Integer known = textures.get(index);
        if (known != null) return known;
        int id = 0;
        if (index >= 0 && index < source.textureOffset.length) {
            byte[] png = new byte[source.textureLength[index]];
            ByteBuffer b = source.data.duplicate();
            b.position(source.textureOffset[index]);
            b.get(png);
            BitmapFactory.Options o = new BitmapFactory.Options();
            o.inPremultiplied = false;  // the shader premultiplies after lighting
            o.inPreferredConfig = Bitmap.Config.ARGB_8888;
            Bitmap bmp = BitmapFactory.decodeByteArray(png, 0, png.length, o);
            if (bmp != null) {
                ByteBuffer px = ByteBuffer.allocateDirect(bmp.getWidth() * bmp.getHeight() * 4).order(ByteOrder.nativeOrder());
                bmp.copyPixelsToBuffer(px);
                px.position(0);
                int[] t = new int[1];
                GLES20.glGenTextures(1, t, 0);
                id = t[0];
                GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, id);
                GLES20.glTexImage2D(GLES20.GL_TEXTURE_2D, 0, GLES20.GL_RGBA, bmp.getWidth(), bmp.getHeight(), 0, GLES20.GL_RGBA,
                    GLES20.GL_UNSIGNED_BYTE, px);
                GLES20.glGenerateMipmap(GLES20.GL_TEXTURE_2D);
                GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR_MIPMAP_LINEAR);
                GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR);
                GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_S, wrap(source.textureWrapS[index]));
                GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_T, wrap(source.textureWrapT[index]));
                if (anisotropic) GLES20.glTexParameterf(GLES20.GL_TEXTURE_2D, MAX_ANISOTROPY, 4f);
                bmp.recycle();
            }
        }
        textures.put(index, id);
        return id;
    }

    private static int wrap(int gx) {
        return gx == 1 ? GLES20.GL_REPEAT : gx == 2 ? GLES20.GL_MIRRORED_REPEAT : GLES20.GL_CLAMP_TO_EDGE;
    }

    // ---------------------------------------------------------------- drawing
    private void draw() {
        synchronized (lock) { scheduled = false; }
        GalaxyPlanets.Frame f = pending;
        if (f == null || failed || surface == EGL14.EGL_NO_SURFACE || program == 0 || surfaceW <= 0) return;
        if (f.sameView(drawn)) return;  // nothing moved: keep the last picture
        long start = System.nanoTime();
        long wait = lastDraw + FRAME_NANOS - start;
        if (wait > 1_000_000L) {  // too soon: draw the newest frame when the 60 Hz slot comes
            synchronized (lock) {
                if (scheduled) return;
                scheduled = true;
            }
            gl.postDelayed(drawTask, wait / 1_000_000L);
            return;
        }
        lastDraw = start;
        if (f.source != loadedFor) { freeScene(); loadedFor = f.source; }

        GLES20.glViewport(0, 0, surfaceW, surfaceH);
        GLES20.glDisable(GLES20.GL_SCISSOR_TEST);
        GLES20.glDepthMask(true);
        GLES20.glClearColor(0, 0, 0, 0);
        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT | GLES20.GL_DEPTH_BUFFER_BIT);
        // The area in surface pixels, fitted as GalaxyLowerScreen fits W x H.
        float scale = Math.min(surfaceW / GalaxyLowerScreen.W, surfaceH / GalaxyLowerScreen.H);
        float ox = (surfaceW - GalaxyLowerScreen.W * scale) / 2, oy = (surfaceH - GalaxyLowerScreen.H * scale) / 2;
        int vx = Math.round(ox + f.area.left * scale), vw = Math.round(f.area.width() * scale);
        int vh = Math.round(f.area.height() * scale), vy = surfaceH - Math.round(oy + f.area.top * scale) - vh;
        GLES20.glViewport(vx, vy, vw, vh);
        GLES20.glEnable(GLES20.GL_SCISSOR_TEST);
        GLES20.glScissor(vx, vy, vw, vh);

        GLES20.glUseProgram(program);
        GLES20.glUniformMatrix4fv(uMvp, 1, false, f.mvp, 0);
        GLES20.glUniform3f(uOrigin, f.ox, f.oy, f.oz);
        GLES20.glUniform3f(uEye, f.ex, f.ey, f.ez);
        GLES20.glUniform3f(uLight, f.lx, f.ly, f.lz);
        GLES20.glUniform1f(uNear, f.near);
        GLES20.glUniform1f(uFar, f.far);
        GLES20.glUniform1i(uTex, 0);
        GLES20.glActiveTexture(GLES20.GL_TEXTURE0);
        GLES20.glEnable(GLES20.GL_DEPTH_TEST);
        GLES20.glDepthFunc(GLES20.GL_LEQUAL);
        GLES20.glFrontFace(GLES20.GL_CCW);
        GLES20.glCullFace(GLES20.GL_BACK);
        for (int i = 0; i < 4; i++) GLES20.glEnableVertexAttribArray(i);
        for (int pass = 0; pass < 2; pass++) {
            for (int pi = 0; pi < f.pieces.length; pi++) {
                GalaxyPlanets.Mesh p = f.pieces[pi].mesh;
                int[] m = mesh(f.source, p);
                GLES20.glUniformMatrix4fv(uModel, 1, false, f.models, pi * 16);
                // A mirrored placement turns the winding over.
                float[] x = f.models;
                int o = pi * 16;
                float det = x[o] * (x[o + 5] * x[o + 10] - x[o + 9] * x[o + 6]) - x[o + 4] * (x[o + 1] * x[o + 10] - x[o + 9] * x[o + 2])
                    + x[o + 8] * (x[o + 1] * x[o + 6] - x[o + 5] * x[o + 2]);
                GLES20.glFrontFace(det < 0 ? GLES20.GL_CW : GLES20.GL_CCW);
                GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, m[0]);
                GLES20.glBindBuffer(GLES20.GL_ELEMENT_ARRAY_BUFFER, m[1]);
                int stride = GalaxyPlanets.VERTEX_BYTES;
                GLES20.glVertexAttribPointer(0, 3, GLES20.GL_FLOAT, false, stride, 0);
                GLES20.glVertexAttribPointer(1, 3, GLES20.GL_BYTE, true, stride, 12);
                GLES20.glVertexAttribPointer(2, 2, GLES20.GL_FLOAT, false, stride, 16);
                GLES20.glVertexAttribPointer(3, 4, GLES20.GL_UNSIGNED_BYTE, true, stride, 24);
                int[] b = p.batches;
                for (int k = 0; k < b.length; k += 4) {
                    int flags = b[k + 3];
                    boolean blend = (flags & GalaxyPlanets.F_BLEND) != 0;
                    if (blend != (pass == 1)) continue;
                    if ((flags & GalaxyPlanets.F_TEXTURED) != 0) {
                        int t = texture(f.source, b[k + 2]);
                        if (t == 0) flags &= ~GalaxyPlanets.F_TEXTURED;
                        else GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, t);
                    }
                    if ((flags & GalaxyPlanets.F_TWO_SIDED) != 0) GLES20.glDisable(GLES20.GL_CULL_FACE);
                    else GLES20.glEnable(GLES20.GL_CULL_FACE);
                    if (blend) {
                        GLES20.glEnable(GLES20.GL_BLEND);
                        if ((flags & GalaxyPlanets.F_ADDITIVE) != 0) GLES20.glBlendFunc(GLES20.GL_ONE, GLES20.GL_ONE);
                        else GLES20.glBlendFunc(GLES20.GL_ONE, GLES20.GL_ONE_MINUS_SRC_ALPHA);
                        GLES20.glDepthMask(false);
                    }
                    GLES20.glUniform1i(uFlags, flags);
                    GLES30.glDrawElements(GLES20.GL_TRIANGLES, b[k + 1], GLES20.GL_UNSIGNED_INT, b[k] * 4);
                    if (blend) { GLES20.glDisable(GLES20.GL_BLEND); GLES20.glDepthMask(true); }
                }
            }
        }
        for (int i = 0; i < 4; i++) GLES20.glDisableVertexAttribArray(i);
        if (!EGL14.eglSwapBuffers(display, surface)) Log.w(TAG, "planet swap failed 0x" + Integer.toHexString(EGL14.eglGetError()));
        drawn = f;
        drawNanos += System.nanoTime() - start;
        if (++frames % 600 == 0) {
            Log.i(TAG, String.format(java.util.Locale.ROOT, "planet renderer: %d frames, %.2f ms CPU each, %d meshes, %d textures",
                frames, drawNanos / 600 / 1e6, meshes.size(), textures.size()));
            drawNanos = 0;
        }
    }
}
