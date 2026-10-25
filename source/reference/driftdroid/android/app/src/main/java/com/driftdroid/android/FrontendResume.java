package com.driftdroid.android;

import java.util.List;

/** Select an existing task only; never create or reset a native game Activity. */
public final class FrontendResume {
    private FrontendResume() {}

    public static final class Task {
        public final int id;
        public final String packageName, activity;
        public Task(int id, String packageName, String activity) {
            this.id = id; this.packageName = packageName; this.activity = activity;
        }
    }

    private static String suffix(FrontendGame game) {
        switch (game) {
            case NSMB: return ":nsmbw_game";
            case MSMBWII: return ":msmbwii_game";
            case SUNSHINE: return ":sms_game";
            case ECLIPSE: return ":eclipse_game";
            case GALAXY: return ":galaxy_game";
            case KAIZO: return ":galaxy_kaizo_game";
            default: return null;
        }
    }

    private static String activity(FrontendGame game) {
        switch (game) {
            case NSMB: return "com.wither.nsmbw.GameActivity";
            case MSMBWII: return "com.wither.nsmbw.MsmbwiiActivities$Game";
            case SUNSHINE: return "dev.chasem.smslauncher.GameActivity";
            case ECLIPSE: return "dev.chasem.eclipselauncher.GameActivity";
            case GALAXY: return "dev.thor.galaxy.GalaxyActivity";
            case KAIZO: return "dev.thor.galaxy.KaizoActivities$Game";
            default: return null;
        }
    }

    public static boolean isRuntimeProcess(String owner, String process) {
        for (FrontendGame game : FrontendGame.values()) {
            String suffix = suffix(game);
            if (suffix != null && (owner + suffix).equals(process)) return true;
        }
        return false;
    }

    public static Integer existingTask(FrontendGame game, String owner,
                                       List<String> processes, List<Task> tasks) {
        String suffix = suffix(game), activity = activity(game);
        if (suffix == null) return null;
        boolean running = false;
        for (String process : processes) {
            if (!isRuntimeProcess(owner, process)) continue;
            if (!(owner + suffix).equals(process)) return null;
            running = true;
        }
        if (!running) return null;
        Integer found = null;
        for (Task task : tasks) {
            if (!owner.equals(task.packageName) || !activity.equals(task.activity)) continue;
            if (found != null) return null; // ambiguous/stale task list
            found = task.id;
        }
        return found;
    }
}
