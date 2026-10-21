package dev.chasem.smslauncher;

public final class InputMonitorLogTest {
    private static void check(boolean ok,String what) { if (!ok) throw new AssertionError(what); }
    public static void main(String[] args) {
        InputMonitorLog log = new InputMonitorLog();
        log.key(1,10,0,100,"DOWN","unknown button scan 100");
        log.key(2,10,0,101,"DOWN","unknown button scan 101");
        log.key(3,11,0,100,"DOWN","second device scan 100");
        log.key(4,10,0,100,"UP","released first");
        String live = log.live();
        check(!live.contains("unknown button scan 100"),"release exact key");
        check(live.contains("unknown button scan 101") && live.contains("second device scan 100"),"unknown scan codes and devices stay independent");
        log.removeDevice(5,10);
        check(!log.live().contains("unknown button scan 101") && log.live().contains("second device scan 100"),"disconnect releases only matching device");
        log.motion(6,11,"X=1.0"); log.motion(7,11,"X=0.0");
        check(log.live().contains("X=0.0") && !log.live().contains("X=1.0"),"axis state not lost to history throttling");
        check(log.report("inventory").contains("X=1.0") && !log.recent().contains("X=0.0"),"motion history throttled");
        log.motion(106,11,"X=-1.0");
        check(log.recent().contains("X=-1.0"),"later motion logged");
        for (int i=0;i<400;i++) log.note(200+i,"event-"+i);
        String report = log.report("inventory marker");
        check(report.startsWith("inventory marker"),"report includes device inventory");
        check(!report.contains("event-143\n") && report.contains("event-144\n") && report.contains("event-399\n"),"history retains exactly last 256 events");
        check(log.live().contains("second device scan 100"),"history eviction preserves held state");
        check(log.recent().indexOf("event-399") < log.recent().indexOf("event-398"),"newest events first");
        check(!log.recent().contains("event-359\n"),"visible history limited to 40 events");
        log.release();
        check(log.live().contains("Held keys/buttons:\nNone") && log.live().contains("No stick/trigger events"),"focus loss releases inputs");
        check(log.report("").contains("event-399"),"focus loss preserves report");
        log.clear();
        check(log.live().contains("Key events: 0   Motion events: 0") && !log.recent().contains("event-399"),"clear resets session");
        System.out.println("PASS input monitor: independent devices/scancodes, key release, disconnect, axes, bounded history, focus loss and clear");
    }
}
