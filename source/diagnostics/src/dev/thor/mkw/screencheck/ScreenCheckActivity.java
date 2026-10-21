package dev.thor.mkw.screencheck;

import android.app.Activity;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Gravity;
import android.view.SurfaceView;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.Button;
import com.driftdroid.android.dualscreen.ThorDualScreenController;

/** Synthetic surface used to verify the actual production display controller without game files. */
public final class ScreenCheckActivity extends Activity {
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private SurfaceView surface;
    private FrameLayout root;
    private ThorDualScreenController screens;
    private boolean running;
    private float angle;
    private int generation;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
        getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        root = new FrameLayout(this);
        surface = new SurfaceView(this);
        root.addView(surface, new FrameLayout.LayoutParams(-1, -1));
        setContentView(root);
        screens = new ThorDualScreenController(this, surface);
        Button calibrate = new Button(this);
        calibrate.setText("Choose map area");
        calibrate.setOnClickListener(view -> screens.calibrateMap());
        root.addView(calibrate, new FrameLayout.LayoutParams(-2, -2, Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL));
    }

    @Override protected void onResume() {
        super.onResume();
        running = true;
        screens.start();
        frame(++generation);
        Log.i("ThorScreenCheck", "RESUME");
    }

    @Override protected void onPause() {
        running = false;
        generation++;
        screens.stop();
        super.onPause();
        Log.i("ThorScreenCheck", "PAUSE");
    }

    private void frame(int token) {
        if (!running || token != generation) return;
        if (surface.getHolder().getSurface().isValid()) {
            Canvas canvas = surface.getHolder().lockCanvas();
            if (canvas != null) {
                try {
                    canvas.drawColor(Color.rgb(14, 22, 32));
                    int w = canvas.getWidth();
                    int h = canvas.getHeight();
                    paint.setColor(Color.WHITE);
                    paint.setStyle(Paint.Style.FILL);
                    paint.setTextSize(h * 0.045f);
                    canvas.drawText("THOR SCREEN CHECK · SYNTHETIC GRAPHICS", w * 0.04f, h * 0.12f, paint);
                    paint.setTextSize(h * 0.032f);
                    canvas.drawText("This is not Mario Kart Wii. No game code or assets loaded.", w * 0.04f, h * 0.19f, paint);
                    canvas.drawText("Upper: display " + surface.getDisplay().getDisplayId()
                            + " · " + w + " × " + h, w * 0.04f, h * 0.27f, paint);
                    canvas.drawText("Lower screen: map only. Calibration is on the upper screen.", w * 0.04f, h * 0.35f, paint);
                    paint.setColor(Color.rgb(40, 74, 86));
                    paint.setStyle(Paint.Style.STROKE);
                    paint.setStrokeWidth(h * 0.075f);
                    canvas.drawOval(w * 0.08f, h * 0.46f, w * 0.62f, h * 0.88f, paint);
                    // A synthetic minimap region, selected through the same calibration UI as gameplay.
                    paint.setStyle(Paint.Style.FILL);
                    paint.setColor(Color.rgb(20, 36, 48));
                    canvas.drawRect(w * 0.73f, h * 0.43f, w * 0.96f, h * 0.89f, paint);
                    paint.setColor(Color.WHITE);
                    paint.setStyle(Paint.Style.STROKE);
                    paint.setStrokeWidth(8f);
                    canvas.drawOval(w * 0.76f, h * 0.49f, w * 0.93f, h * 0.83f, paint);
                    angle += 0.015f;
                    paint.setStyle(Paint.Style.FILL);
                    paint.setColor(Color.CYAN);
                    canvas.drawCircle(w * (0.845f + 0.085f * (float) Math.cos(angle)),
                            h * (0.66f + 0.17f * (float) Math.sin(angle)), h * 0.018f, paint);
                } finally {
                    surface.getHolder().unlockCanvasAndPost(canvas);
                }
            }
        }
        handler.postDelayed(() -> frame(token), 33);
    }

}
