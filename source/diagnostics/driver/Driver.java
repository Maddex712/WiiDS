package dev.thor.mkw.driver;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.ComponentName;
import android.content.Intent;
import android.net.LocalServerSocket;
import android.net.LocalSocket;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.io.PrintWriter;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.nio.file.DirectoryStream;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.nio.file.attribute.BasicFileAttributes;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Locale;

/** Separate signed instrumentation, never packaged into the game. */
public final class Driver extends Instrumentation {
    private static native String nativeProbe();
    private static native String nativeProbeStage(int stage);
    private Activity activity;
    private View surface;
    private int device;
    private Method readState;
    private Method handleKey;
    private String product = "base";
    @Override public void onCreate(Bundle args) {
        super.onCreate(args);
        if (args != null) product = args.getString("product", "base");
        if (!product.equals("base") && !product.equals("retro_rewind")) throw new IllegalArgumentException("Unknown product");
        start();
    }
    @Override public void onStart() {
        try {
            System.load(getContext().getApplicationInfo().nativeLibraryDir + "/libthorprobe.so");
            Intent launch = new Intent().setComponent(new ComponentName("com.driftdroid.android.thor", "com.driftdroid.android.MainActivity"));
            launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK); launch.putExtra("product", product);
            activity = startActivitySync(launch);
            ClassLoader classes = getTargetContext().getClassLoader();
            Field field = Class.forName("org.libsdl.app.SDLActivity", true, classes).getDeclaredField("mSurface");
            field.setAccessible(true); surface = (View) field.get(null);
            handleKey = Class.forName("org.libsdl.app.SDLActivity", true, classes).getMethod("handleKeyEvent", View.class, int.class, KeyEvent.class, android.view.inputmethod.InputConnection.class);
            readState = Class.forName("com.driftdroid.android.dualscreen.RaceMapBridge", true, classes).getMethod("nativeRead", float[].class);
            for (int id : InputDevice.getDeviceIds()) {
                InputDevice d = InputDevice.getDevice(id);
                if (d != null && d.getName().equals("Odin Controller") && (d.getSources() & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK) device = id;
            }
            if (device == 0) throw new IllegalStateException("No physical Odin Controller found");
            try (LocalServerSocket server = new LocalServerSocket("thor-mkw-driver")) {
                for (;;) {
                    try (LocalSocket socket = server.accept()) {
                        BufferedReader in = new BufferedReader(new InputStreamReader(socket.getInputStream()));
                        PrintWriter out = new PrintWriter(socket.getOutputStream(), true);
                        String line;
                        while ((line = in.readLine()) != null) {
                            try {
                                String[] a = line.trim().split("\\s+");
                                if (a[0].equals("state")) {
                                    float[] state = new float[104];
                                    boolean active = (Boolean) readState.invoke(null, (Object) state);
                                    JSONArray values = new JSONArray(); for (float f : state) values.put((double) f);
                                    String identity = (String) readState.getDeclaringClass().getMethod("nativeCourseIdentity").invoke(null);
                                    out.println(new JSONObject().put("active", active).put("device", device).put("state", values).put("course_identity", identity));
                                } else if (a[0].equals("mapstats")) {
                                    JSONObject stats = new JSONObject();
                                    boolean reset = a.length > 1 && a[1].equals("reset");
                                    runOnMainSync(() -> {
                                        try {
                                            Object controller = field(activity, "thorScreens");
                                            Object presentation = field(controller, "presentation");
                                            Object map = field(presentation, "map");
                                            for (String name : new String[]{"callbackCount", "drawCount", "slowFrameCount", "maxFrameNanos"}) {
                                                Field f = map.getClass().getDeclaredField(name); f.setAccessible(true);
                                                stats.put(name, f.getLong(map));
                                                if (reset) f.setLong(map, 0);
                                            }
                                            if (reset) { Field f = map.getClass().getDeclaredField("lastFrameTime"); f.setAccessible(true); f.setLong(map, 0); }
                                        } catch (Exception e) { throw new RuntimeException(e); }
                                    });
                                    out.println(stats);
                                } else if (a[0].equals("probe")) {
                                    out.println(nativeProbe());
                                } else if (a[0].equals("filesnapshot")) {
                                    out.println(fileSnapshot());
                                } else if (a[0].equals("rumbleinfo")) {
                                    out.println(rumbleInfo());
                                } else if (a[0].equals("sustained")) {
                                    if (a.length != 2 || (!a[1].equals("true") && !a[1].equals("false"))) {
                                        throw new IllegalArgumentException("Usage: sustained true|false");
                                    }
                                    final boolean requested = a[1].equals("true");
                                    runOnMainSync(() -> activity.getWindow().setSustainedPerformanceMode(requested));
                                    out.println(new JSONObject().put("requested", requested)
                                            .put("device_grant_verified", false)
                                            .put("note", "Window request only; device grant or performance effect is not guaranteed."));
                                } else if (a[0].equals("probestage")) {
                                    out.println(nativeProbeStage(Integer.parseInt(a[1])));
                                } else if (a[0].equals("lower")) {
                                    String method = a[1].equals("on") ? "start" : "stop";
                                    runOnMainSync(() -> {
                                        try {
                                            Field screens = activity.getClass().getDeclaredField("thorScreens");
                                            screens.setAccessible(true);
                                            Object controller = screens.get(activity);
                                            controller.getClass().getMethod(method).invoke(controller);
                                        } catch (Exception e) { throw new RuntimeException(e); }
                                    });
                                    out.println("{\"ok\":true}");
                                } else if (a[0].equals("immersive")) {
                                    runOnMainSync(() -> {
                                        activity.getWindow().setDecorFitsSystemWindows(false);
                                        android.view.WindowInsetsController control = activity.getWindow().getInsetsController();
                                        if (control != null) {
                                            control.setSystemBarsBehavior(android.view.WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                                            control.hide(android.view.WindowInsets.Type.systemBars());
                                        }
                                    });
                                    out.println("{\"ok\":true}");
                                } else if (a[0].equals("heldprobe")) {
                                    int code = Integer.parseInt(a[1]);
                                    long down = SystemClock.uptimeMillis();
                                    try { key(code, KeyEvent.ACTION_DOWN, down); SystemClock.sleep(250); out.println(nativeProbe()); }
                                    finally { key(code, KeyEvent.ACTION_UP, down); }
                                } else if (a[0].equals("input")) {
                                    drive(a); out.println("{\"ok\":true,\"released\":true}");
                                } else if (a[0].equals("stop")) { out.println("{\"ok\":true}"); finish(0, new Bundle()); return; }
                                else out.println("{\"error\":\"Unknown command\"}");
                            } catch (Exception e) { out.println(new JSONObject().put("error", e.toString())); }
                        }
                    }
                }
            }
        } catch (Exception e) {
            Bundle result = new Bundle(); result.putString("error", e.toString()); finish(1, result);
        }
    }
    /** Private diagnostic response only: never logged, written or sent to JNI. */
    private JSONObject fileSnapshot() throws Exception {
        Path root = getTargetContext().getFilesDir().toPath().resolve("WiiCompiled");
        JSONArray files = new JSONArray(), directories = new JSONArray(), missing = new JSONArray(), errors = new JSONArray();
        JSONObject result = new JSONObject().put("schema", 1).put("base", "files/WiiCompiled")
                .put("files", files).put("directories", directories).put("missing", missing).put("errors", errors)
                .put("config_text", JSONObject.NULL).put("atomic_snapshot", false);
        long started = SystemClock.elapsedRealtime();
        // Only list names at the user root; never recurse into DiscData,
        // ThorMap or the mod pack, and never follow a symbolic link.
        if (!Files.isDirectory(root, LinkOption.NOFOLLOW_LINKS)) {
            errors.put("WiiCompiled is missing, non-directory or a symlink");
        } else {
            List<Path> selected = new ArrayList<>();
            selected.add(root.resolve("Config.toml"));
            selected.add(root.resolve("controller_ports.dat"));
            selected.add(root.resolve("keyboard_bindings.dat"));
            try (DirectoryStream<Path> children = Files.newDirectoryStream(root)) {
                for (Path child : children) if (child.getFileName().toString().endsWith(".controller")) selected.add(child);
            }
            selected.sort(Comparator.comparing(Path::toString));
            for (Path path : selected) snapshotFile(root, path, files, missing, errors, result);
            snapshotTree(root, root.resolve("NAND"), files, directories, missing, errors, result);
            Path riivolution = root.resolve("riivolution");
            if (Files.isSymbolicLink(riivolution)) errors.put("riivolution: symbolic link rejected");
            else snapshotTree(root, riivolution.resolve("save"), files, directories, missing, errors, result);
        }
        boolean stable = errors.length() == 0;
        for (int i = 0; i < files.length(); i++) stable &= files.getJSONObject(i).getBoolean("stable");
        return result.put("complete", stable).put("started_elapsed_ms", started)
                .put("finished_elapsed_ms", SystemClock.elapsedRealtime());
    }

    private static void snapshotTree(Path root, Path path, JSONArray files, JSONArray directories,
            JSONArray missing, JSONArray errors, JSONObject result) throws Exception {
        String relative = root.relativize(path).toString();
        if (!Files.exists(path, LinkOption.NOFOLLOW_LINKS)) { missing.put(relative); return; }
        if (Files.isSymbolicLink(path)) { errors.put(relative + ": symbolic link rejected"); return; }
        if (!Files.isDirectory(path, LinkOption.NOFOLLOW_LINKS)) { errors.put(relative + ": expected directory"); return; }
        directories.put(relative);
        List<Path> children = new ArrayList<>();
        try (DirectoryStream<Path> stream = Files.newDirectoryStream(path)) {
            for (Path child : stream) children.add(child);
        } catch (Exception e) { errors.put(relative + ": " + e.getClass().getSimpleName()); return; }
        children.sort(Comparator.comparing(Path::toString));
        for (Path child : children) {
            if (Files.isDirectory(child, LinkOption.NOFOLLOW_LINKS))
                snapshotTree(root, child, files, directories, missing, errors, result);
            else snapshotFile(root, child, files, missing, errors, result);
        }
    }

    private static void snapshotFile(Path root, Path path, JSONArray files, JSONArray missing,
            JSONArray errors, JSONObject result) throws Exception {
        String relative = root.relativize(path).toString();
        if (!Files.exists(path, LinkOption.NOFOLLOW_LINKS)) { missing.put(relative); return; }
        if (!Files.isRegularFile(path, LinkOption.NOFOLLOW_LINKS)) {
            errors.put(relative + ": nonregular file rejected"); return;
        }
        try {
            BasicFileAttributes before = Files.readAttributes(path, BasicFileAttributes.class, LinkOption.NOFOLLOW_LINKS);
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            boolean config = relative.equals("Config.toml");
            if (config && before.size() > 4 * 1024 * 1024) throw new IllegalArgumentException("oversized Config.toml");
            ByteArrayOutputStream text = config ? new ByteArrayOutputStream() : null;
            long bytes = 0;
            try (InputStream in = Files.newInputStream(path, StandardOpenOption.READ, LinkOption.NOFOLLOW_LINKS)) {
                byte[] buffer = new byte[64 * 1024];
                for (int n; (n = in.read(buffer)) != -1;) {
                    bytes += n; digest.update(buffer, 0, n);
                    if (text != null) {
                        if (bytes > 4 * 1024 * 1024) throw new IllegalArgumentException("growing Config.toml");
                        text.write(buffer, 0, n);
                    }
                }
            }
            BasicFileAttributes after = Files.readAttributes(path, BasicFileAttributes.class, LinkOption.NOFOLLOW_LINKS);
            StringBuilder hash = new StringBuilder(64);
            for (byte value : digest.digest()) hash.append(String.format(Locale.ROOT, "%02x", value & 255));
            boolean stable = after.isRegularFile() && before.size() == bytes && after.size() == bytes
                    && before.lastModifiedTime().equals(after.lastModifiedTime())
                    && java.util.Objects.equals(before.fileKey(), after.fileKey());
            files.put(new JSONObject().put("path", relative).put("bytes", bytes).put("sha256", hash.toString()).put("stable", stable));
            if (text != null) result.put("config_text", StandardCharsets.UTF_8.newDecoder().decode(ByteBuffer.wrap(text.toByteArray())).toString());
        } catch (Exception e) { errors.put(relative + ": " + e.getClass().getSimpleName()); }
    }

    private JSONObject rumbleInfo() throws Exception {
        JSONArray devices = new JSONArray();
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice input = InputDevice.getDevice(id);
            if (input == null || !("Odin Controller".equals(input.getName()) ||
                    (input.getVendorId() == 0x2020 && input.getProductId() == 0x0111))) continue;
            JSONObject info = new JSONObject().put("device_id", id).put("name", input.getName())
                    .put("vendor_id", input.getVendorId()).put("product_id", input.getProductId())
                    .put("sources", input.getSources()).put("has_vibrator", input.getVibrator().hasVibrator());
            JSONArray ids = new JSONArray(), capabilities = new JSONArray();
            if (android.os.Build.VERSION.SDK_INT >= 31) {
                android.os.VibratorManager manager = input.getVibratorManager();
                for (int vibratorId : manager.getVibratorIds()) {
                    ids.put(vibratorId);
                    android.os.Vibrator vibrator = manager.getVibrator(vibratorId);
                    capabilities.put(new JSONObject().put("id", vibratorId)
                            .put("has_vibrator", vibrator.hasVibrator())
                            .put("has_amplitude_control", vibrator.hasAmplitudeControl()));
                }
            }
            devices.put(info.put("vibrator_ids", ids).put("vibrators", capabilities));
        }
        // Metadata only: never emit vibration, change preferences, or call the
        // native probe/getters (which can initialize unrelated controller state).
        return new JSONObject().put("api", android.os.Build.VERSION.SDK_INT)
                .put("selected_input_device_id", device).put("devices", devices);
    }

    private static Object field(Object object, String name) throws Exception {
        Field f = object.getClass().getDeclaredField(name); f.setAccessible(true); return f.get(object);
    }
    private void drive(String[] args) throws Exception {
        int duration = Math.max(0, Math.min(30000, Integer.parseInt(args[1])));
        float x = Float.parseFloat(args[2]), y = Float.parseFloat(args[3]);
        long down = SystemClock.uptimeMillis();
        int pressed = 4;
        try {
            for (; pressed < args.length; pressed++) key(Integer.parseInt(args[pressed]), KeyEvent.ACTION_DOWN, down);
            do { axes(x, y, down); SystemClock.sleep(16); } while (SystemClock.uptimeMillis() - down < duration);
        } finally {
            axes(0, 0, down);
            for (int i = 4; i < pressed; i++) key(Integer.parseInt(args[i]), KeyEvent.ACTION_UP, down);
        }
    }
    private void key(int code, int action, long down) {
        runOnMainSync(() -> {
            try {
                KeyEvent event = new KeyEvent(down, SystemClock.uptimeMillis(), action, code,
                        0, 0, device, 0, 0, InputDevice.SOURCE_GAMEPAD);
                Object handled = handleKey.invoke(null, surface, code, event, null);
                android.util.Log.i("ThorDriver", "key=" + code + " action=" + action + " handled=" + handled
                        + " surfaceFocus=" + surface.hasFocus() + " focus=" + activity.getCurrentFocus());
            } catch (Exception e) { throw new RuntimeException(e); }
        });
    }
    private void axes(float x, float y, long down) {
        runOnMainSync(() -> {
            MotionEvent.PointerProperties p = new MotionEvent.PointerProperties(); p.id = 0;
            MotionEvent.PointerCoords c = new MotionEvent.PointerCoords();
            c.setAxisValue(MotionEvent.AXIS_X, x); c.setAxisValue(MotionEvent.AXIS_Y, y);
            MotionEvent e = MotionEvent.obtain(down, SystemClock.uptimeMillis(), MotionEvent.ACTION_MOVE,
                    1, new MotionEvent.PointerProperties[]{p}, new MotionEvent.PointerCoords[]{c},
                    0, 0, 1, 1, device, 0, InputDevice.SOURCE_JOYSTICK, 0);
            try { surface.dispatchGenericMotionEvent(e); } finally { e.recycle(); }
        });
    }
}
