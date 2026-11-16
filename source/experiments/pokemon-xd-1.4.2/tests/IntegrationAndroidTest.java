package com.driftdroid.android;

import android.app.*;
import android.content.*;
import android.view.*;
import android.widget.*;
import com.driftdroid.android.rom.*;
import com.driftdroid.android.saves.SaveArchive;
import com.driftdroid.android.storage.SafeOffload;
import dev.wiids.xd.*;
import java.io.*;
import java.nio.file.*;
import java.util.*;
import org.junit.*;
import org.junit.runner.RunWith;
import org.robolectric.*;
import org.robolectric.android.controller.ActivityController;
import org.robolectric.annotation.Config;
import org.robolectric.shadows.ShadowActivityManager;
import static org.junit.Assert.*;
import static org.robolectric.Shadows.shadowOf;

@RunWith(RobolectricTestRunner.class)
@Config(sdk=33, manifest=Config.NONE)
public class IntegrationAndroidTest {
    Context context;
    @Before public void setup() { context=RuntimeEnvironment.getApplication(); }

    @Test public void catalogRequiresTheSupportedDisc() {
        GameCatalog.Disc disc=GameCatalog.Disc.POKEMON_XD_USA;
        assertEquals(XdStorage.game(context),disc.installDir(context));
        assertEquals(XdStorage.iso(context),disc.installedFile(context));
        for(String region:new String[]{"GXXE01","GXXP01","GXXJ01"})
            for(int revision=0;revision<3;revision++)
                assertEquals(region.equals("GXXE01")&&revision==0,disc.matches(new WiiDsDisc.Info("ISO",WiiDsDisc.Platform.GAMECUBE,region,0,revision,"USA",XdDisc.BYTES,"Pokemon XD")));
        assertFalse(disc.matches(new WiiDsDisc.Info("NKit",WiiDsDisc.Platform.GAMECUBE,"GXXE01",0,0,"USA",XdDisc.BYTES,"Pokemon XD")));
        assertFalse(disc.isInstalled(context));
        assertFalse(GameCatalog.INSTANCE.isPlayable(context,GameCatalog.Game.POKEMON_XD));
    }

    @Test public void truncatedImportDoesNotBecomePlayable() throws Exception {
        File iso=XdStorage.iso(context);iso.getParentFile().mkdirs();Files.write(iso.toPath(),new byte[64]);
        assertFalse(XdStorage.imported(context));
        assertFalse(XdStorage.ready(context));
        assertFalse(GameCatalog.Disc.POKEMON_XD_USA.isInstalled(context));
        assertFalse(GameStorage.INSTANCE.ready(context,FrontendGame.POKEMON_XD));
    }

    @Test public void savesAndOffloadUseSeparateRoots() throws Exception {
        SaveArchive.Profile profile=SaveManagerProfiles.INSTANCE.profile(context,FrontendGame.POKEMON_XD);
        assertEquals("pokemon-xd",profile.id);
        assertEquals(XdStorage.saves(context).getCanonicalFile(),profile.roots.get("main"));
        SafeOffload.Plan plan=GameStorage.INSTANCE.plan(context,FrontendGame.POKEMON_XD);
        assertEquals(1,plan.targets.size());
        assertEquals(XdStorage.game(context).getCanonicalFile(),plan.targets.get("disc").file.getCanonicalFile());
        assertTrue(plan.protectedRoots.contains(XdStorage.saves(context).getCanonicalFile()));
        File save=new File(XdStorage.saves(context),"USA/Card A/01-GXXE-PokemonXD.gci");
        save.getParentFile().mkdirs();Files.write(save.toPath(),new byte[]{2,4,6,8});
        File config=new File(XdStorage.root(context),"settings.ini");Files.write(config.toPath(),new byte[]{7});
        File disc=new File(XdStorage.game(context),"game.iso");disc.getParentFile().mkdirs();Files.write(disc.toPath(),new byte[]{1});
        SafeOffload.offload(plan,()->{},(bytes,files)->{});
        assertFalse(disc.exists());assertArrayEquals(new byte[]{2,4,6,8},Files.readAllBytes(save.toPath()));
        assertArrayEquals(new byte[]{7},Files.readAllBytes(config.toPath()));
    }

    @Test public void xdBackupRoundtripKeepsOtherGamesAndDisc() throws Exception {
        SaveArchive.Profile profile=SaveManagerProfiles.INSTANCE.profile(context,FrontendGame.POKEMON_XD);
        File save=new File(XdStorage.saves(context),"USA/Card A/test.gci");save.getParentFile().mkdirs();
        Files.write(save.toPath(),new byte[]{1,3,5});
        File disc=new File(XdStorage.game(context),"game.iso");disc.getParentFile().mkdirs();Files.write(disc.toPath(),new byte[]{9});
        File other=new File(context.getExternalFilesDir(null),"mkdd/saves/keep");other.getParentFile().mkdirs();Files.write(other.toPath(),new byte[]{8});
        File zip=new File(context.getCacheDir(),"pokemon-xd.zip");SaveArchive.backup(profile,zip,false);
        Files.write(save.toPath(),new byte[]{2,4});
        SaveArchive.restore(profile,zip,new File(context.getFilesDir(),"test-restore"),new File(context.getCacheDir(),"before.zip"));
        assertArrayEquals(new byte[]{1,3,5},Files.readAllBytes(save.toPath()));
        assertArrayEquals(new byte[]{9},Files.readAllBytes(disc.toPath()));
        assertArrayEquals(new byte[]{8},Files.readAllBytes(other.toPath()));
    }

    @Test public void activeXdBlocksSaveAndStorageOperations() throws Exception {
        ActivityManager am=(ActivityManager)context.getSystemService(Context.ACTIVITY_SERVICE);
        ShadowActivityManager shadow=shadowOf(am);
        shadow.setProcesses(List.of(new ActivityManager.RunningAppProcessInfo(context.getPackageName()+":xd_game",101,new String[]{context.getPackageName()})));
        try { SaveManagerProfiles.INSTANCE.requireStopped(context);fail("running XD must block saves"); }
        catch(Exception expected) { assertTrue(expected instanceof IOException); assertTrue(expected.getMessage().contains("running game")); }
        try { GameStorage.INSTANCE.requireIdle(context);fail("running XD must block offload"); }
        catch(Exception expected) { assertTrue(expected instanceof IOException); assertTrue(expected.getMessage().contains("running game")); }
    }

    @Test public void missingDiscLaunchDoesNotLoadSdl() {
        ActivityController<XdLaunchActivity> controller=Robolectric.buildActivity(XdLaunchActivity.class).create();
        assertTrue(controller.get().isFinishing());
        assertNull(shadowOf(controller.get()).getNextStartedActivity());
        controller.destroy();
    }

    @org.robolectric.annotation.Implements(android.os.Environment.class)
    public static class StoragePermissionShadow extends org.robolectric.shadows.ShadowEnvironment {
        @org.robolectric.annotation.Implementation
        protected static boolean isExternalStorageManager() { return false; }
    }
    @Test @Config(shadows=StoragePermissionShadow.class)
    public void targetedSetupCardOpensAndroidPicker() throws Exception {
        Intent request=new Intent(context,GamesActivity.class).putExtra("storage_game","pokemon-xd");
        ActivityController<GamesActivity> controller=Robolectric.buildActivity(GamesActivity.class,request).create();
        GamesActivity activity=controller.get();
        java.lang.reflect.Method refresh=GamesActivity.class.getDeclaredMethod("refresh");refresh.setAccessible(true);refresh.invoke(activity);
        List<TextView> labels=new ArrayList<>();collect(activity.getWindow().getDecorView(),labels);
        assertTrue(labels.stream().anyMatch(v->v.getText().toString().contains("GXXE01")));
        assertTrue(labels.stream().anyMatch(v->v.getText().toString().contains("USA")));
        Button choose=(Button)labels.stream().filter(v->v instanceof Button&&v.getText().toString().equals("Choose file")).findFirst().orElseThrow();
        assertTrue(choose.isEnabled());choose.performClick();
        Intent picker=shadowOf(activity).getNextStartedActivityForResult().intent;
        assertEquals(Intent.ACTION_OPEN_DOCUMENT,picker.getAction());
        assertTrue(picker.hasCategory(Intent.CATEGORY_OPENABLE));
        assertEquals("*/*",picker.getType());
        assertFalse(labels.stream().anyMatch(v->v.getText().toString().contains("Engineering preview")));
        controller.destroy();
    }
    static void collect(View view,List<TextView> labels) {
        if(view instanceof TextView)labels.add((TextView)view);
        if(view instanceof ViewGroup) { ViewGroup group=(ViewGroup)view;for(int i=0;i<group.getChildCount();i++)collect(group.getChildAt(i),labels); }
    }
}

