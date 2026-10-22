package dev.thor.galaxy;
import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.widget.*;
import java.io.*;
public final class GalaxyBackupActivity extends Activity {
    @Override protected void attachBaseContext(android.content.Context base){super.attachBaseContext(new GalaxyStorage(base));}
    private TextView text;
    @Override public void onCreate(Bundle state){super.onCreate(state);LinearLayout box=new LinearLayout(this);box.setOrientation(1);box.setPadding(64,48,64,48);text=new TextView(this);text.setTextSize(22);text.setText("Copy your legacy Galaxy profile into WiiDS\n\nChoose the backup ZIP made by Export-LegacyProfile.py before importing Galaxy here. The old helper and its files stay intact. Existing WiiDS Galaxy profiles are never overwritten.");box.addView(text);Button pick=new Button(this);pick.setText("Choose Galaxy backup ZIP");pick.setOnClickListener(v->startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT).setType("application/zip").addCategory(Intent.CATEGORY_OPENABLE),1));box.addView(pick);setContentView(box);}
    @Override public void onActivityResult(int request,int result,Intent data){super.onActivityResult(request,result,data);if(request!=1||result!=RESULT_OK||data==null||data.getData()==null)return;Uri uri=data.getData();text.setText("Copying and checking your Galaxy backup. Keep WiiDS open...");new Thread(()->{String message;try(RandomAccessFile guard=new RandomAccessFile(new File(getFilesDir(),".galaxy-runtime.lock"),"rw");java.nio.channels.FileLock lock=guard.getChannel().tryLock();InputStream in=getContentResolver().openInputStream(uri)){if(lock==null)throw new IOException("Close Galaxy/importer first.");GalaxyMigration.importBackup(getFilesDir(),getExternalFilesDir(null),in);message="Galaxy profile copied and verified. Return to the shelf to play. Your old helper and backup are unchanged.";}catch(Exception failure){message="Backup wasn't installed: "+failure.getMessage();}final String value=message;runOnUiThread(()->text.setText(value));},"GalaxyMigration").start();}
}
