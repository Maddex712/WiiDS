package dev.wiids.xd.sdl;


import android.content.Context;
import android.content.pm.ActivityInfo;
import android.graphics.Insets;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.os.Build;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Display;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.PointerIcon;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowManager;

import android.view.ScaleGestureDetector;

/**
    SDLSurface. This is what we draw on, so we need to know when it's created
    in order to do anything useful.

    Because of this, that's where we set up the SDL thread
*/
public class SDLSurface extends SurfaceView implements SurfaceHolder.Callback,
    View.OnApplyWindowInsetsListener, View.OnKeyListener,
    SensorEventListener {

    // Sensors
    protected SensorManager mSensorManager;
    protected Display mDisplay;

    // Keep track of the surface size for the game surface
    protected float mWidth, mHeight;

    // Is SurfaceView ready for rendering
    protected boolean mIsSurfaceReady;

    // Is on-screen keyboard visible
    protected boolean mKeyboardVisible;

    // Pinch events


    // Startup
    protected SDLSurface(Context context) {
        super(context);
        getHolder().addCallback(this);



        setFocusable(true);
        setFocusableInTouchMode(true);
        requestFocus();
        setOnApplyWindowInsetsListener(this);
        setOnKeyListener(this);


        mDisplay = ((WindowManager)context.getSystemService(Context.WINDOW_SERVICE)).getDefaultDisplay();
        mSensorManager = (SensorManager)context.getSystemService(Context.SENSOR_SERVICE);

        setOnGenericMotionListener(SDLActivity.getMotionListener());

        // Some arbitrary defaults to avoid a potential division by zero
        mWidth = 1.0f;
        mHeight = 1.0f;

        mIsSurfaceReady = false;
    }

    protected void handlePause() {
        enableSensor(Sensor.TYPE_ACCELEROMETER, false);
    }

    protected void handleResume() {
        setFocusable(true);
        setFocusableInTouchMode(true);
        requestFocus();
        setOnApplyWindowInsetsListener(this);
        setOnKeyListener(this);

        enableSensor(Sensor.TYPE_ACCELEROMETER, true);
    }

    protected Surface getNativeSurface() {
        return getHolder().getSurface();
    }

    // Called when we have a valid drawing surface
    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        Log.v("SDL", "surfaceCreated()");
        SDLActivity.onNativeSurfaceCreated();
    }

    // Called when we lose the surface
    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        Log.v("SDL", "surfaceDestroyed()");

        // Transition to pause, if needed
        SDLActivity.mNextNativeState = SDLActivity.NativeState.PAUSED;
        SDLActivity.handleNativeState();

        mIsSurfaceReady = false;
        SDLActivity.onNativeSurfaceDestroyed();
    }

    // Called when the surface is resized
    @Override
    public void surfaceChanged(SurfaceHolder holder,
                               int format, int width, int height) {
        Log.v("SDL", "surfaceChanged()");

        if (SDLActivity.mSingleton == null) {
            return;
        }

        mWidth = width;
        mHeight = height;
        int nDeviceWidth = width;
        int nDeviceHeight = height;
        float density = 1.0f;
        try
        {
            DisplayMetrics realMetrics = new DisplayMetrics();
            mDisplay.getRealMetrics( realMetrics );
            nDeviceWidth = realMetrics.widthPixels;
            nDeviceHeight = realMetrics.heightPixels;
            // Use densityDpi instead of density to more closely match what the UI scale is
            density = (float)realMetrics.densityDpi / 160.0f;
        } catch(Exception ignored) {
        }

        synchronized(SDLActivity.getContext()) {
            // In case we're waiting on a size change after going fullscreen, send a notification.
            SDLActivity.getContext().notifyAll();
        }

        Log.v("SDL", "Window size: " + width + "x" + height);
        Log.v("SDL", "Device size: " + nDeviceWidth + "x" + nDeviceHeight);
        SDLActivity.nativeSetScreenResolution(width, height, nDeviceWidth, nDeviceHeight, density, mDisplay.getRefreshRate());
        SDLActivity.onNativeResize();

        // Prevent a screen distortion glitch,
        // for instance when the device is in Landscape and a Portrait App is resumed.
        boolean skip = false;
        int requestedOrientation = SDLActivity.mSingleton.getRequestedOrientation();

        if (requestedOrientation == ActivityInfo.SCREEN_ORIENTATION_PORTRAIT || requestedOrientation == ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT) {
            if (mWidth > mHeight) {
               skip = true;
            }
        } else if (requestedOrientation == ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE || requestedOrientation == ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE) {
            if (mWidth < mHeight) {
               skip = true;
            }
        }

        // Special Patch for Square Resolution: Black Berry Passport
        if (skip) {
           double min = Math.min(mWidth, mHeight);
           double max = Math.max(mWidth, mHeight);

           if (max / min < 1.20) {
              Log.v("SDL", "Don't skip on such aspect-ratio. Could be a square resolution.");
              skip = false;
           }
        }

        // Don't skip if we might be multi-window or have popup dialogs
        if (skip) {
            if (Build.VERSION.SDK_INT >= 24 /* Android 7.0 (N) */) {
                skip = false;
            }
        }

        if (skip) {
           Log.v("SDL", "Skip .. Surface is not ready.");
           mIsSurfaceReady = false;
           return;
        }

        /* If the surface has been previously destroyed by onNativeSurfaceDestroyed, recreate it here */
        SDLActivity.onNativeSurfaceChanged();

        /* Surface is ready */
        mIsSurfaceReady = true;

        SDLActivity.mNextNativeState = SDLActivity.NativeState.RESUMED;
        SDLActivity.handleNativeState();
    }

    // Window inset
    @Override
    public WindowInsets onApplyWindowInsets(View v, WindowInsets insets) {
        if (Build.VERSION.SDK_INT >= 30 /* Android 11 (R) */) {
            Insets combined = insets.getInsets(WindowInsets.Type.systemBars() |
                                               WindowInsets.Type.systemGestures() |
                                               WindowInsets.Type.mandatorySystemGestures() |
                                               WindowInsets.Type.tappableElement() |
                                               WindowInsets.Type.displayCutout());

            SDLActivity.onNativeInsetsChanged(combined.left, combined.right, combined.top, combined.bottom);

            if (insets.isVisible(WindowInsets.Type.ime())) {
                if (!mKeyboardVisible) {
                    mKeyboardVisible = true;
                    SDLActivity.onNativeScreenKeyboardShown();
                }
            } else {
                if (mKeyboardVisible) {
                    mKeyboardVisible = false;
                    SDLActivity.onNativeScreenKeyboardHidden();
                }
            }
        }

        // Pass these to any child views in case they need them
        return insets;
    }

    // Key events
    @Override
    public boolean onKey(View v, int keyCode, KeyEvent event) {
        return SDLActivity.handleKeyEvent(v, keyCode, event, null);
    }

    private float getNormalizedX(float x)
    {
        if (mWidth <= 1) {
            return 0.5f;
        } else {
            return (x / (mWidth - 1));
        }
    }

    private float getNormalizedY(float y)
    {
        if (mHeight <= 1) {
            return 0.5f;
        } else {
            return (y / (mHeight - 1));
        }
    }




    // Sensor events
    protected void enableSensor(int sensortype, boolean enabled) {
        // TODO: This uses getDefaultSensor - what if we have >1 accels?
        if (enabled) {
            SDLSensorManager.registerListener(mSensorManager, this,
                            mSensorManager.getDefaultSensor(sensortype),
                            SensorManager.SENSOR_DELAY_GAME);
        } else {
            SDLSensorManager.unregisterListener(mSensorManager, this,
                            mSensorManager.getDefaultSensor(sensortype));
        }
    }

    @Override
    public void onAccuracyChanged(Sensor sensor, int accuracy) {
        // TODO
    }

    @Override
    public void onSensorChanged(SensorEvent event) {
        if (event.sensor.getType() == Sensor.TYPE_ACCELEROMETER) {

            // Since we may have an orientation set, we won't receive onConfigurationChanged events.
            // We thus should check here.
            int newRotation;

            float x, y;
            switch (mDisplay.getRotation()) {
                case Surface.ROTATION_0:
                default:
                    x = event.values[0];
                    y = event.values[1];
                    newRotation = 0;
                    break;
                case Surface.ROTATION_90:
                    x = -event.values[1];
                    y = event.values[0];
                    newRotation = 90;
                    break;
                case Surface.ROTATION_180:
                    x = -event.values[0];
                    y = -event.values[1];
                    newRotation = 180;
                    break;
                case Surface.ROTATION_270:
                    x = event.values[1];
                    y = -event.values[0];
                    newRotation = 270;
                    break;
            }

            if (newRotation != SDLActivity.mCurrentRotation) {
                SDLActivity.mCurrentRotation = newRotation;
                SDLActivity.onNativeRotationChanged(newRotation);
            }

            SDLActivity.onNativeAccel(-x / SensorManager.GRAVITY_EARTH,
                                      y / SensorManager.GRAVITY_EARTH,
                                      event.values[2] / SensorManager.GRAVITY_EARTH);


        }
    }

    // Prevent android internal NullPointerException (https://github.com/libsdl-org/SDL/issues/13306)
    @Override
    public PointerIcon onResolvePointerIcon(MotionEvent event, int pointerIndex) {
        try {
            return super.onResolvePointerIcon(event, pointerIndex);
        } catch (NullPointerException e) {
            return null;
        }
    }

    // Captured pointer events for API 26.
    @Override
    public boolean onCapturedPointerEvent(MotionEvent event)
    {
        return SDLActivity.getMotionListener().onGenericMotion(this, event);
    }







}
