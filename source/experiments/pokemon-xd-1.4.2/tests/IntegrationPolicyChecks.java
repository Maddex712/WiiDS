import com.driftdroid.android.*;
import dev.wiids.xd.XdDisc;
import java.io.*;
import java.nio.file.*;
import java.util.*;
import java.util.concurrent.atomic.AtomicInteger;

public final class IntegrationPolicyChecks {
    static int checks;
    static void check(boolean value, String why) { checks++; if (!value) throw new AssertionError(why); }
    public static void main(String[] args) throws Exception {
        String owner = "io.github.maddex712.wiids";
        FrontendGame[] games = {FrontendGame.NSMB, FrontendGame.MSMBWII, FrontendGame.SUNSHINE,
            FrontendGame.ECLIPSE, FrontendGame.GALAXY, FrontendGame.KAIZO, FrontendGame.BRAWL,
            FrontendGame.PROJECT_PLUS, FrontendGame.MKDD, FrontendGame.MKDD_PLUS, FrontendGame.POKEMON_XD};
        String[] suffixes = {":nsmbw_game", ":msmbwii_game", ":sms_game", ":eclipse_game", ":galaxy_game",
            ":galaxy_kaizo_game", ":brawl_game", ":brawl_plus_game", ":mkdd_game", ":mkdd_plus_game", ":xd_game"};
        String[] activities = {"com.wither.nsmbw.GameActivity", "com.wither.nsmbw.MsmbwiiActivities$Game",
            "dev.chasem.smslauncher.GameActivity", "dev.chasem.eclipselauncher.GameActivity",
            "dev.thor.galaxy.GalaxyActivity", "dev.thor.galaxy.KaizoActivities$Game",
            "dev.wiids.brawl.GameActivity", "dev.wiids.brawl.ProjectPlusGameActivity",
            "dev.thor.mkdd.WiiDsMkddGameActivity", "dev.thor.mkdd.WiiDsMkddPlusGameActivity", "dev.wiids.xd.XdGameActivity"};
        for (int i=0; i<games.length; i++) {
            String process=owner+suffixes[i];
            FrontendResume.Task task=new FrontendResume.Task(42,owner,activities[i]);
            List<String> running=List.of(owner,process,"foreign:xd_game");
            check(FrontendResume.isRuntimeProcess(owner,process), "runtime identity "+games[i]);
            check(Integer.valueOf(42).equals(FrontendResume.existingTask(games[i],owner,running,List.of(task))), "same task resume "+games[i]);
            for (FrontendGame other:FrontendGame.values()) if (other!=games[i])
                check(FrontendResume.existingTask(other,owner,running,List.of(task))==null,"cross game launch blocked "+other);
            check(FrontendResume.existingTask(games[i],owner,List.of(),List.of(task))==null,"stale task");
            check(FrontendResume.existingTask(games[i],owner,running,List.of())==null,"orphan runtime");
            check(FrontendResume.existingTask(games[i],owner,running,List.of(task,task))==null,"ambiguous tasks");
            check(FrontendResume.existingTask(games[i],owner,List.of(process,owner+suffixes[(i+1)%games.length]),List.of(task))==null,"multiple runtimes");
            check(FrontendResume.existingTask(games[i],owner,running,List.of(new FrontendResume.Task(43,"foreign",activities[i])))==null,"foreign task");
        }
        check(!FrontendResume.isRuntimeProcess(owner,"foreign:xd_game"),"foreign process");
        check(!FrontendResume.isRuntimeProcess(owner,owner+":xd_import"),"import is not resumable");
        check(FrontendGame.fromId("pokemon-xd")==FrontendGame.POKEMON_XD,"stable XD id");
        check(FrontendGame.fromBackupFilename("pokemon-xd-2026-11-15.zip")==FrontendGame.POKEMON_XD,"backup identity");
        check(FrontendGame.fromBackupFilename("mkdd-plus-2026-11-15.zip")==FrontendGame.MKDD_PLUS,"existing Plus identity");
        check(XdDisc.validateImage(new File(args[0]),()->false)==null,"exact owner disc accepted read-only");
        AtomicInteger blocks=new AtomicInteger();
        check("Import cancelled.".equals(XdDisc.validateImage(new File(args[0]),()->blocks.incrementAndGet()>4)),"cancel checksum scan");
        Path temp=Files.createTempFile(Path.of(args[1]),"invalid-disc-", ".iso");
        try {
            Files.write(temp,new byte[0x440]);
            check(XdDisc.validateImage(temp.toFile(),()->false)!=null,"truncated image rejected");
        } finally { Files.delete(temp); }
        check(XdDisc.validateImage(new File(args[0]+".missing"),()->false)!=null,"missing image rejected");
        System.out.println("PASS "+checks+" actual production resume, identity and exact-disc checks; no game executed");
    }
}
