package com.driftdroid.android;

import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;

/** Actual frontend identities, archive routing and retained-runtime selection for the two cases. */
public final class PlusCaseTest {
    private static int checks;
    private static void check(boolean ok, String why) {
        checks++;
        if (!ok) throw new AssertionError(why);
    }
    public static void main(String[] args) {
        FrontendGame base = FrontendGame.MKDD, plus = FrontendGame.MKDD_PLUS;
        check(FrontendGame.fromId("mkdd") == base, "original case identity");
        check(FrontendGame.fromId("mkdd-plus") == plus, "Plus case identity");
        check(base != plus && !base.artwork.equals(plus.artwork), "distinct cases and artwork");
        HashSet<String> ids = new HashSet<>();
        for (FrontendGame game : FrontendGame.values()) {
            check(ids.add(game.id), "unique stable ID: " + game.id);
            check(FrontendGame.fromBackupFilename(game.id + "-1793719200000-manual-test.zip") == game,
                    "own backup export: " + game.id);
        }
        check(FrontendGame.fromBackupFilename("mkdd-plus-1793719200000-manual-test.zip") != base,
                "Plus archive excluded from base backup list");
        check(FrontendGame.fromBackupFilename("mkdd-1793719200000-manual-test.zip") != plus,
                "base archive excluded from Plus backup list");
        check(FrontendGame.fromBackupFilename(null) == null, "absent backup name");
        check(FrontendGame.fromBackupFilename("mkdd-plus-test.tmp") == null, "non-archive name");
        String owner = "io.github.maddex712.wiids";
        List<FrontendResume.Task> tasks = Arrays.asList(
                new FrontendResume.Task(9, owner, "dev.thor.mkdd.WiiDsMkddGameActivity"),
                new FrontendResume.Task(10, owner, "dev.thor.mkdd.WiiDsMkddPlusGameActivity"));
        List<String> baseProcess = Collections.singletonList(owner + ":mkdd_game");
        List<String> plusProcess = Collections.singletonList(owner + ":mkdd_plus_game");
        check(FrontendResume.existingTask(base, owner, baseProcess, tasks) == 9, "resume original task");
        check(FrontendResume.existingTask(plus, owner, plusProcess, tasks) == 10, "resume Plus task");
        check(FrontendResume.existingTask(base, owner, plusProcess, tasks) == null, "base cannot resume Plus");
        check(FrontendResume.existingTask(plus, owner, baseProcess, tasks) == null, "Plus cannot resume base");
        check(FrontendResume.isRuntimeProcess(owner, owner + ":mkdd_plus_game"), "Plus runtime gates save operations");
        check(FrontendResume.existingTask(plus, owner, Collections.emptyList(), tasks) == null, "stale task ignored");
        check(FrontendResume.existingTask(plus, owner, plusProcess, Collections.emptyList()) == null, "no task invented");
        System.out.println("PASS " + checks + " actual frontend case/archive/resume checks");
    }
}
