package dev.chasem.smslauncher;

import java.util.ArrayDeque;
import java.util.LinkedHashMap;
import java.util.Map;

/** Bounded, screen-local input history; independent of Android for host checks. */
final class InputMonitorLog {
    private static final int LIMIT = 256;
    private final ArrayDeque<String> history = new ArrayDeque<>();
    private final Map<String, String> held = new LinkedHashMap<>();
    private final Map<Integer, String> axes = new LinkedHashMap<>();
    private final Map<Integer, Long> lastMotion = new LinkedHashMap<>();
    private long keys, motions;

    void note(long ms, String text) {
        if (history.size() == LIMIT) history.removeFirst();
        history.addLast(ms + " ms  " + text);
    }
    void key(long ms, int device, int code, int scan, String action, String description) {
        ++keys;
        String id = device + "/" + code + "/" + scan;
        if ("UP".equals(action)) held.remove(id);
        else if ("DOWN".equals(action)) held.put(id, description);
        note(ms, description);
    }
    void motion(long ms, int device, String description) {
        ++motions;
        axes.put(device, description);
        Long previous = lastMotion.get(device);
        if (previous == null || ms - previous >= 100) {
            note(ms, description);
            lastMotion.put(device, ms);
        }
    }
    void removeDevice(long ms, int device) {
        held.keySet().removeIf(id -> id.startsWith(device + "/"));
        axes.remove(device); lastMotion.remove(device);
        note(ms, "Device " + device + " removed");
    }
    void release() { held.clear(); axes.clear(); lastMotion.clear(); }
    void clear() { history.clear(); release(); keys = motions = 0; }
    String live() {
        StringBuilder out = new StringBuilder("Key events: " + keys + "   Motion events: " + motions + "\n\nHeld keys/buttons:\n");
        if (held.isEmpty()) out.append("None\n");
        else for (String value : held.values()) out.append(value).append('\n');
        out.append("\nLatest controller axes:\n");
        if (axes.isEmpty()) out.append("No stick/trigger events received.\n");
        else for (String value : axes.values()) out.append(value).append('\n');
        return out.toString();
    }
    String recent() {
        StringBuilder out = new StringBuilder("Recent events (newest first):\n");
        int n = 0;
        for (java.util.Iterator<String> i = history.descendingIterator(); i.hasNext() && n++ < 40;)
            out.append(i.next()).append("\n\n");
        return out.toString();
    }
    String report(String inventory) {
        StringBuilder out = new StringBuilder(inventory).append("\n\n").append(live()).append("\nEvents (last 256, oldest first):\n");
        for (String event : history) out.append(event).append('\n');
        return out.toString();
    }
}
