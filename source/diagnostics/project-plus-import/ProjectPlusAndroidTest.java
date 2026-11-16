package com.driftdroid.android;

import android.app.*;
import android.content.*;
import android.database.Cursor;
import android.net.Uri;
import android.os.*;
import android.view.*;
import android.widget.*;
import com.driftdroid.android.rom.GameInstaller;
import dev.wiids.brawl.*;
import java.io.*;
import java.nio.file.*;
import java.util.*;
import java.util.concurrent.*;
import org.junit.*;
import org.junit.runner.RunWith;
import org.robolectric.*;
import org.robolectric.android.controller.ActivityController;
import org.robolectric.annotation.*;
import org.robolectric.shadows.*;
import static org.junit.Assert.*;
import static org.robolectric.Shadows.shadowOf;

@RunWith(RobolectricTestRunner.class)
@Config(sdk=33,manifest=Config.NONE,shadows=ProjectPlusAndroidTest.StoragePermissionShadow.class)
public class ProjectPlusAndroidTest {
    Context context;
    @Before public void setup()throws Exception {
        context=RuntimeEnvironment.getApplication();resetRecoveryError();
        shadowOf((ActivityManager)context.getSystemService(Context.ACTIVITY_SERVICE)).setProcesses(List.of(new ActivityManager.RunningAppProcessInfo(context.getPackageName(),1,new String[]{context.getPackageName()})));
    }
    @After public void cleanup()throws Exception {if(Provider.release!=null)Provider.release.countDown();resetRecoveryError();}
    static void resetRecoveryError()throws Exception {var field=GameInstaller.class.getDeclaredField("recoveryError");field.setAccessible(true);field.set(null,null);}
    @Implements(Environment.class) public static class StoragePermissionShadow extends ShadowEnvironment {
        @Implementation protected static boolean isExternalStorageManager(){return false;}
    }
    void brawl()throws Exception {
        Path root=BrawlStorage.data(context).toPath().resolve("game");Files.createDirectories(root.resolve("files"));Files.createDirectories(root.resolve("sys"));Files.write(root.resolve("sys/main.dol"),new byte[]{1});
        assertTrue("fast shelf check only; not a playable disc fixture",ThorGameLaunch.INSTANCE.brawlInstalled(context));
    }
    static List<TextView> labels(Activity a){List<TextView> list=new ArrayList<>();collect(a.getWindow().getDecorView(),list);return list;}
    static void collect(View v,List<TextView> list){if(v instanceof TextView)list.add((TextView)v);if(v instanceof ViewGroup){ViewGroup group=(ViewGroup)v;for(int i=0;i<group.getChildCount();i++)collect(group.getChildAt(i),list);}}
    static Button button(Activity a,String text){return (Button)labels(a).stream().filter(v->v instanceof Button&&v.getText().toString().equals(text)).findFirst().orElseThrow();}
    static void refresh(Activity a)throws Exception{var method=a.getClass().getDeclaredMethod("refresh");method.setAccessible(true);method.invoke(a);}
    @Test public void missingBrawlDisablesImport(){
        var controller=Robolectric.buildActivity(ProjectPlusImportActivity.class).create();
        assertFalse(button(controller.get(),"Choose Project+ ZIP").isEnabled());assertFalse(button(controller.get(),"Import selected files").isEnabled());controller.destroy();
    }
    @Test public void pickerAndPairSelectionSurviveRecreation()throws Exception {
        brawl();var c=Robolectric.buildActivity(ProjectPlusImportActivity.class).create();var a=c.get();
        for(String name:new String[]{"Choose Project+ ZIP","Choose sd.raw","Choose RSBE01.gct"}) {
            assertTrue(button(a,name).isEnabled());button(a,name).performClick();
            var request=shadowOf(a).getNextStartedActivityForResult();assertEquals(Intent.ACTION_OPEN_DOCUMENT,request.intent.getAction());
            assertTrue(request.intent.hasCategory(Intent.CATEGORY_OPENABLE));assertEquals("*/*",request.intent.getType());
        }
        a.onActivityResult(731,Activity.RESULT_OK,new Intent().setData(Uri.parse("content://pplus/raw")));
        assertFalse(button(a,"Import selected files").isEnabled());
        a.onActivityResult(732,Activity.RESULT_OK,new Intent().setData(Uri.parse("content://pplus/codes")));
        assertTrue(button(a,"Import selected files").isEnabled());
        Bundle saved=new Bundle();c.saveInstanceState(saved).destroy();
        var recreated=Robolectric.buildActivity(ProjectPlusImportActivity.class).create(saved);
        assertTrue(button(recreated.get(),"Import selected files").isEnabled());
        assertTrue(labels(recreated.get()).stream().anyMatch(v->v.getText().toString().equals("SD image selected")));recreated.destroy();
    }
    @Test public void fileUrisAreRejectedAndPickerCancelKeepsSelection()throws Exception {
        brawl();var c=Robolectric.buildActivity(ProjectPlusImportActivity.class).create();var a=c.get();
        a.onActivityResult(731,Activity.RESULT_OK,new Intent().setData(Uri.parse("file:///sdcard/sd.raw")));
        assertTrue(labels(a).stream().anyMatch(v->v.getText().toString().contains("document picker")));
        a.onActivityResult(731,Activity.RESULT_OK,new Intent().setData(Uri.parse("content://pplus/raw")));
        a.onActivityResult(732,Activity.RESULT_CANCELED,null);assertFalse(button(a,"Import selected files").isEnabled());c.destroy();
    }
    @Test public void targetedProjectPlusCardRoutesToImporterAndBrawl()throws Exception {
        Intent target=new Intent(context,GamesActivity.class).putExtra("storage_game","project-plus");
        var missing=Robolectric.buildActivity(GamesActivity.class,target).create();refresh(missing.get());
        button(missing.get(),"Import Brawl").performClick();Intent brawl=shadowOf(missing.get()).getNextStartedActivity();assertEquals("brawl",brawl.getStringExtra("storage_game"));missing.destroy();
        brawl();var ready=Robolectric.buildActivity(GamesActivity.class,target).create();refresh(ready.get());
        button(ready.get(),"Import Project+").performClick();assertEquals(ProjectPlusImportActivity.class.getName(),shadowOf(ready.get()).getNextStartedActivity().getComponent().getClassName());ready.destroy();
    }
    @Test public void setupGateBlocksGamesSavesUpdatesAndNewImports()throws Exception {
        brawl();GameStorage.INSTANCE.getPreparingProjectPlus().set(true);
        try {
            assertNotNull(SaveManagerProfiles.INSTANCE.launchBlocker(context));assertFalse(ThorGameLaunch.INSTANCE.brawlInstalled(context));
            try{GameStorage.INSTANCE.requireIdle(context);fail();}catch(Exception expected){assertTrue(expected.getMessage().contains("Project+"));}
            try{ProjectPlusInstaller.INSTANCE.start(context,null,Uri.parse("content://pplus/raw"),Uri.parse("content://pplus/codes"));fail();}catch(Exception expected){assertTrue(expected.getMessage().contains("Project+"));}
        }finally{GameStorage.INSTANCE.getPreparingProjectPlus().set(false);}
    }
    public static class Provider extends ContentProvider {
        static File raw,codes;static CountDownLatch entered,release;
        public boolean onCreate(){return true;}
        public ParcelFileDescriptor openFile(Uri uri,String mode)throws FileNotFoundException {
            if(uri.getLastPathSegment().equals("raw")&&release!=null){entered.countDown();try{if(!release.await(10,TimeUnit.SECONDS))throw new FileNotFoundException("timeout");}catch(InterruptedException e){throw new FileNotFoundException("interrupted");}}
            File selected=uri.getLastPathSegment().equals("raw")?raw:codes;
            if(!selected.isFile())throw new FileNotFoundException("Selected provider file is unavailable");
            return ParcelFileDescriptor.open(selected,ParcelFileDescriptor.MODE_READ_ONLY);
        }
        public Cursor query(Uri u,String[] p,String s,String[] a,String sort){throw new AssertionError("provider metadata must not block main thread");}
        public String getType(Uri u){return "application/octet-stream";}public Uri insert(Uri u,ContentValues v){throw new UnsupportedOperationException();}
        public int delete(Uri u,String s,String[] a){throw new UnsupportedOperationException();}public int update(Uri u,ContentValues v,String s,String[] a){throw new UnsupportedOperationException();}
    }
    void provider(boolean hold)throws Exception {
        Provider.raw=new File(context.getCacheDir(),"selected-sd.raw");Provider.codes=new File(context.getCacheDir(),"selected-RSBE01.gct");
        Files.write(Provider.raw.toPath(),ProjectPlusImportTest.card(2));Files.write(Provider.codes.toPath(),ProjectPlusImportTest.codes(2));
        Provider.entered=new CountDownLatch(1);Provider.release=hold?new CountDownLatch(1):null;
        Provider provider=new Provider();android.content.pm.ProviderInfo info=new android.content.pm.ProviderInfo();info.authority="pplus";provider.attachInfo(context,info);
        ShadowContentResolver.registerProviderInternal("pplus",provider);
    }
    void start(){ProjectPlusInstaller.INSTANCE.start(context,null,Uri.parse("content://pplus/raw"),Uri.parse("content://pplus/codes"));}
    void finished()throws Exception {long until=System.nanoTime()+TimeUnit.SECONDS.toNanos(10);while(ProjectPlusInstaller.INSTANCE.getState().getRunning()&&System.nanoTime()<until)Thread.sleep(10);assertFalse(ProjectPlusInstaller.INSTANCE.getState().getRunning());assertFalse(GameStorage.INSTANCE.getPreparingProjectPlus().get());}
    @Test public void actualWorkerImportsProviderFilesAndEnablesCase()throws Exception {
        brawl();provider(false);GameStorage.INSTANCE.disableProjectPlus(context);start();finished();
        assertNull(ProjectPlusInstaller.INSTANCE.getState().getError());assertTrue(BrawlStorage.projectPlusInstalled(context));assertFalse(GameStorage.INSTANCE.disabled(context,FrontendGame.PROJECT_PLUS));
        assertArrayEquals(ProjectPlusImportTest.card(2),Files.readAllBytes(new File(BrawlStorage.projectPlusDirectory(context),"sd.raw").toPath()));
        assertTrue(Provider.raw.isFile());assertTrue(Provider.codes.isFile());
    }
    @Test public void workerSurvivesActivityRecreationAndCancelsSafely()throws Exception {
        brawl();provider(true);Path root=BrawlStorage.projectPlusDirectory(context).toPath();Files.createDirectories(root.resolve("Codes"));Files.write(root.resolve("sd.raw"),new byte[]{7});Files.write(root.resolve("Codes/RSBE01.gct"),new byte[]{8});
        start();assertTrue(Provider.entered.await(5,TimeUnit.SECONDS));
        var c=Robolectric.buildActivity(ProjectPlusImportActivity.class).create();assertFalse(button(c.get(),"Choose Project+ ZIP").isEnabled());c.destroy();
        var again=Robolectric.buildActivity(ProjectPlusImportActivity.class).create();assertTrue(button(again.get(),"Cancel import").isEnabled());
        button(again.get(),"Cancel import").performClick();Provider.release.countDown();finished();refresh(again.get());
        assertTrue(ProjectPlusInstaller.INSTANCE.getState().getError().contains("cancelled"));assertArrayEquals(new byte[]{7},Files.readAllBytes(root.resolve("sd.raw")));assertArrayEquals(new byte[]{8},Files.readAllBytes(root.resolve("Codes/RSBE01.gct")));again.destroy();
    }
    @Test public void runningGamePreventsWorkerAndProviderFailureReleasesGate()throws Exception {
        brawl();provider(false);var am=(ActivityManager)context.getSystemService(Context.ACTIVITY_SERVICE);
        shadowOf(am).setProcesses(List.of(new ActivityManager.RunningAppProcessInfo(context.getPackageName()+":brawl_game",99,new String[]{context.getPackageName()})));
        try{start();fail();}catch(Exception expected){assertTrue(expected.getMessage().contains("running game"));}assertFalse(GameStorage.INSTANCE.getPreparingProjectPlus().get());
        shadowOf(am).setProcesses(List.of(new ActivityManager.RunningAppProcessInfo(context.getPackageName(),1,new String[]{context.getPackageName()})));Provider.raw=new File(context.getCacheDir(),"missing-provider-file");start();finished();assertNotNull(ProjectPlusInstaller.INSTANCE.getState().getError());assertFalse(BrawlStorage.projectPlusInstalled(context));
    }
    @Test public void offloadAndRecoveryErrorsPreventStartingWorker()throws Exception {
        brawl();GameStorage.INSTANCE.getBusy().set(true);
        try{start();fail();}catch(Exception expected){assertTrue(expected.getMessage().contains("Manage games"));}finally{GameStorage.INSTANCE.getBusy().set(false);}
        // Actual pending journal is hidden from readiness and legacy fallback until recovered.
        Path p=BrawlStorage.projectPlusDirectory(context).toPath();Files.createDirectories(p.resolve("Codes"));Files.write(p.resolve("sd.raw"),new byte[]{1});Files.write(p.resolve("Codes/RSBE01.gct"),new byte[]{2});Files.writeString(p.resolve(".project-plus-import.properties"),"malformed");
        assertFalse(BrawlStorage.projectPlusAvailable(context));GameInstaller.INSTANCE.recoverImports(context);assertNotNull(GameInstaller.INSTANCE.getRecoveryError());
        try{start();fail();}catch(Exception expected){assertTrue(expected.getMessage().contains("recovery"));}assertTrue(Files.exists(p.resolve(".project-plus-import.properties")));
    }
}
