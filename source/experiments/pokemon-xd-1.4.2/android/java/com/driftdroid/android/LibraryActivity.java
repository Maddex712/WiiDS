package com.driftdroid.android;
import android.app.Activity;import android.os.Bundle;import android.content.Intent;import android.widget.*;
/** Small private XD-only test entry; XD runtime/UI classes remain unchanged. */
public final class LibraryActivity extends Activity {
 private Button play;
 @Override public boolean dispatchKeyEvent(android.view.KeyEvent e){int k=e.getKeyCode();if(k==96||k==108||k==23||k==66){if(e.getAction()==1&&play!=null)play.performClick();return true;}return super.dispatchKeyEvent(e);}
 public void onCreate(Bundle s){super.onCreate(s);
  LinearLayout l=new LinearLayout(this);l.setOrientation(1);l.setPadding(60,60,60,60);
  TextView t=new TextView(this);t.setText("WiiDS");t.setTextSize(28);l.addView(t);
  Button b=new Button(this);play=b;b.setText("Play Pokemon XD");b.setOnClickListener(v->startActivity(new Intent(this,dev.wiids.xd.XdLaunchActivity.class)));l.addView(b);setContentView(l);b.requestFocus();
 }
}
