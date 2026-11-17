package com.driftdroid.android.dualscreen;

import android.app.Activity;
import android.app.ActivityOptions;
import android.app.Application;
import android.app.NativeActivity;
import android.app.Presentation;
import android.content.Context;
import android.content.ContextWrapper;
import android.content.DialogInterface;
import android.content.Intent;
import android.view.Display;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewTreeObserver;
import android.view.Window;
import android.view.WindowId;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.WindowManager;
import android.view.inspector.WindowInspector;
import android.os.Bundle;
import java.lang.reflect.Proxy;
import java.lang.reflect.InvocationTargetException;
import java.lang.ref.WeakReference;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;

/** Existing companion views, hosted in a regular Activity when the target is not a presentation display. */
public class WiiDsPresentation extends Presentation {
    private static final Map<String, WeakReference<WiiDsPresentation>> pending = new HashMap<>();
    private final Context owner;
    private final boolean embedded;
    private String token;
    private CompanionHostActivity host;
    private boolean requested, started;
    private boolean ownerResumed;
    private View ownerDecor;
    private Window.Callback previousCallback, inputCallback;
    private Boolean deliveredFocus, localFocusTarget;
    private WindowId nativeWindow;
    private boolean addedLocalFocusFlag;
    private final WindowId.FocusObserver nativeFocusObserver = new WindowId.FocusObserver() {
        @Override public void onFocusGained(WindowId id) { syncOwnerFocus(); }
        @Override public void onFocusLost(WindowId id) { syncOwnerFocus(); }
    };
    private final View.OnAttachStateChangeListener nativeAttach = new View.OnAttachStateChangeListener() {
        @Override public void onViewAttachedToWindow(View view) { bindNativeWindow(); syncOwnerFocus(); }
        @Override public void onViewDetachedFromWindow(View view) { unbindNativeWindow(); }
    };
    private View activeModal;
    private View modalSelection;
    private final View.OnAttachStateChangeListener modalAttachment = new View.OnAttachStateChangeListener() {
        @Override public void onViewAttachedToWindow(View view) {}
        @Override public void onViewDetachedFromWindow(View view) {
            // The dismissing key can run before WindowManager has detached the dialog.
            // Recompute after removal so a native owner does not remain locally blurred.
            if (ownerDecor != null) ownerDecor.post(() -> syncOwnerFocus());
        }
    };
    private final ViewTreeObserver.OnGlobalFocusChangeListener modalFocus = (before, after) -> {
        if (after != null && activeModal != null && after.getRootView() == activeModal) modalSelection = after;
    };
    private final Application.ActivityLifecycleCallbacks ownerLifecycle = new Application.ActivityLifecycleCallbacks() {
        public void onActivityCreated(Activity a, Bundle state) {}
        public void onActivityStarted(Activity a) {}
        public void onActivityResumed(Activity a) {
            if (a == owner) { ownerResumed = true; syncOwnerFocus(); }
        }
        public void onActivityPrePaused(Activity a) { if (a == owner) ownerResumed = false; }
        public void onActivityPaused(Activity a) { if (a == owner) ownerResumed = false; }
        public void onActivityStopped(Activity a) { if (a == owner) { ownerResumed = false; dismiss(); } }
        public void onActivitySaveInstanceState(Activity a, Bundle state) {}
        public void onActivityDestroyed(Activity a) { if (a == owner) dismiss(); }
    };
    private DialogInterface.OnDismissListener dismissed;

    public WiiDsPresentation(Context context, Display display) {
        super(context, display);
        owner = context;
        embedded = (display.getFlags() & Display.FLAG_PRESENTATION) == 0;
    }

    @Override public void setOnDismissListener(DialogInterface.OnDismissListener listener) {
        if (embedded) dismissed = listener;
        else super.setOnDismissListener(listener);
    }

    @Override public boolean isShowing() { return embedded ? requested : super.isShowing(); }

    @Override public void show() {
        if (!embedded) { super.show(); return; }
        if (requested) return;
        create(); // Build exactly the existing companion view hierarchy, with target-display resources.
        token = UUID.randomUUID().toString();
        requested = true;
        if (owner instanceof Activity) {
            Activity source = (Activity)owner;
            ownerResumed = true; // callers show companions only while the owner is resumed
            source.getApplication().registerActivityLifecycleCallbacks(ownerLifecycle);
            ownerDecor = source.getWindow().getDecorView();
            previousCallback = source.getWindow().getCallback();
            if (source instanceof NativeActivity) {
                ownerDecor.addOnAttachStateChangeListener(nativeAttach);
                bindNativeWindow(); // first onResume can precede the owner's initial decor attachment
                addedLocalFocusFlag = (source.getWindow().getAttributes().flags & WindowManager.LayoutParams.FLAG_LOCAL_FOCUS_MODE) == 0;
                source.getWindow().addFlags(WindowManager.LayoutParams.FLAG_LOCAL_FOCUS_MODE);
            }
            deliveredFocus = source.hasWindowFocus();
            final Window.Callback delegate = previousCallback;
            inputCallback = (Window.Callback)Proxy.newProxyInstance(Window.Callback.class.getClassLoader(),
                    new Class<?>[]{Window.Callback.class}, (proxy, method, arguments) -> {
                        // Delegate every public Window callback unchanged, except the focus
                        // signal while our host actually receives input for the guest.
                        if (method.getName().equals("onWindowFocusChanged")) {
                            arguments = arguments.clone();
                            boolean focused = source instanceof NativeActivity ? nativeOwnsFocus()
                                    : ((Boolean)arguments[0] || ownsInput()) && modal() == null;
                            if (deliveredFocus != null && deliveredFocus == focused) return null;
                            deliveredFocus = focused;
                            arguments[0] = focused;
                        }
                        try { return method.invoke(delegate, arguments); }
                        catch (InvocationTargetException failure) { throw failure.getCause(); }
                    });
            source.getWindow().setCallback(inputCallback);
        }
        pending.put(token, new WeakReference<>(this));
        Intent request = new Intent(owner, CompanionHostActivity.component(Application.getProcessName(), owner.getPackageName()))
                .putExtra("companion_token", token)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_MULTIPLE_TASK);
        try {
            owner.startActivity(request, ActivityOptions.makeBasic().setLaunchDisplayId(getDisplay().getDisplayId()).toBundle());
        } catch (RuntimeException refused) {
            dismiss();
            android.view.WindowManager.InvalidDisplayException unavailable =
                    new android.view.WindowManager.InvalidDisplayException("Companion screen unavailable");
            unavailable.initCause(refused);
            throw unavailable;
        }
    }

    static WiiDsPresentation claim(String token) {
        WeakReference<WiiDsPresentation> ref = pending.get(token);
        WiiDsPresentation result = ref == null ? null : ref.get();
        return result != null && result.requested ? result : null;
    }

    void attach(CompanionHostActivity activity) {
        if (!requested || !getDisplay().isValid()
                || WiiDsScreens.current(activity).getDisplayId() != getDisplay().getDisplayId()) { dismiss(); activity.finish(); return; }
        host = activity;
        View decor = getWindow().getDecorView();
        if (decor.getParent() instanceof ViewGroup) ((ViewGroup)decor.getParent()).removeView(decor);
        activity.getWindow().addFlags(getWindow().getAttributes().flags);
        activity.getWindow().clearFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE);
        activity.setContentView(decor, new ViewGroup.LayoutParams(-1, -1));
        if (!started) { started = true; onStart(); }
        syncOwnerFocus();
    }

    private boolean ownsInput() {
        return requested && ownerResumed && host != null && !host.isFinishing() && host.hasWindowFocus();
    }

    /** NativeActivity consumes controller events before Java callbacks. Keep its own input
     * pipeline locally focused while this app-owned companion supplies those events. */
    private boolean nativeOwnsFocus() {
        // WindowId is the window manager's focus, independent of setLocalFocus's ViewRoot state.
        return requested && ownerResumed && modal() == null
                && (ownsInput() || nativeWindow != null && nativeWindow.isFocused());
    }

    private void bindNativeWindow() {
        if (nativeWindow != null || ownerDecor == null) return;
        nativeWindow = ownerDecor.getWindowId();
        if (nativeWindow != null) nativeWindow.registerFocusObserver(nativeFocusObserver);
    }

    private void unbindNativeWindow() {
        if (nativeWindow != null) { nativeWindow.unregisterFocusObserver(nativeFocusObserver); nativeWindow = null; }
    }

    private void nativeLocalFocus(boolean focused) {
        if (!(owner instanceof NativeActivity) || ownerDecor == null || !ownerDecor.isAttachedToWindow()) return;
        Activity source = (Activity)owner;
        if (source.isDestroyed()) return;
        if (localFocusTarget == null || localFocusTarget != focused || source.hasWindowFocus() != focused) {
            localFocusTarget = focused;
            source.getWindow().setLocalFocus(focused, false);
        }
    }

    private View modal() {
        if (!(owner instanceof Activity)) return null;
        View candidate = null;
        for (View root : WindowInspector.getGlobalWindowViews()) {
            if (root == ownerDecor || !root.isAttachedToWindow() || root.getVisibility() != View.VISIBLE
                    || root.getWindowVisibility() != View.VISIBLE) continue;
            Context context = root.getContext();
            while (context instanceof ContextWrapper && !(context instanceof Activity)) {
                Context next = ((ContextWrapper)context).getBaseContext();
                if (next == context) break;
                context = next;
            }
            boolean belongs = context == owner;
            if (root.getLayoutParams() instanceof WindowManager.LayoutParams) {
                WindowManager.LayoutParams params = (WindowManager.LayoutParams)root.getLayoutParams();
                if ((params.flags & WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE) != 0) continue;
                // DecorContext can deliberately replace the Activity context. Application
                // dialogs still carry the exact owning Activity's app-window token.
                belongs |= ownerDecor != null && params.type < WindowManager.LayoutParams.FIRST_SYSTEM_WINDOW
                        && params.token != null && (params.token.equals(ownerDecor.getApplicationWindowToken())
                        || params.token.equals(((Activity)owner).getWindow().getAttributes().token));
            }
            if (!belongs) continue;
            candidate = root;
            if (root.hasWindowFocus()) return root;
        }
        return candidate;
    }

    public static boolean windowHasInputFocus(View view) {
        if (view.hasWindowFocus()) return true;
        for (WeakReference<WiiDsPresentation> ref : pending.values()) {
            WiiDsPresentation panel = ref.get();
            if (panel != null && panel.ownsInput() && panel.modal() == view) return true;
        }
        return false;
    }

    void syncOwnerFocus() {
        if (!requested || !ownerResumed || inputCallback == null) return;
        View next = modal();
        if (activeModal != next && activeModal != null && activeModal.isAttachedToWindow())
            activeModal.dispatchWindowFocusChanged(activeModal.hasWindowFocus());
        if (activeModal != next && activeModal != null)
            com.driftdroid.android.ThorDialogs.INSTANCE.refreshFocus(activeModal);
        if (activeModal != next) {
            if (activeModal != null && activeModal.getViewTreeObserver().isAlive())
                activeModal.getViewTreeObserver().removeOnGlobalFocusChangeListener(modalFocus);
            if (activeModal != null) activeModal.removeOnAttachStateChangeListener(modalAttachment);
            activeModal = next;
            modalSelection = next == null ? null : next.findFocus();
            if (next != null) { next.getViewTreeObserver().addOnGlobalFocusChangeListener(modalFocus); next.addOnAttachStateChangeListener(modalAttachment); }
        } else if (next != null && next.findFocus() != null) modalSelection = next.findFocus();
        boolean focused = next == null && (((Activity)owner).hasWindowFocus() || ownsInput());
        if (owner instanceof NativeActivity) {
            focused = nativeOwnsFocus();
            nativeLocalFocus(focused);
        }
        inputCallback.onWindowFocusChanged(focused);
        if (next != null) {
            next.dispatchWindowFocusChanged(next.hasWindowFocus() || ownsInput());
            com.driftdroid.android.ThorDialogs.INSTANCE.refreshFocus(next);
        }
    }

    public static boolean hasInputFocus(Activity activity) {
        if (activity.hasWindowFocus()) return true;
        for (WeakReference<WiiDsPresentation> ref : pending.values()) {
            WiiDsPresentation panel = ref.get();
            if (panel != null && panel.owner == activity && panel.ownsInput() && panel.modal() == null) return true;
        }
        return false;
    }

    boolean key(KeyEvent event) {
        if (!ownsInput() || inputCallback == null) return false;
        View dialog = modal();
        // Touching the lower panel can switch Android into touch mode and clear a focused
        // upper button. Restore the player's existing menu selection when controller input returns.
        int code = event.getKeyCode();
        boolean controller = KeyEvent.isGamepadButton(code) || (code >= KeyEvent.KEYCODE_DPAD_UP && code <= KeyEvent.KEYCODE_DPAD_CENTER)
                || code == KeyEvent.KEYCODE_ENTER || code == KeyEvent.KEYCODE_BACK;
        if (dialog != null && dialog.findFocus() == null && controller) {
            if (modalSelection != null && modalSelection.isAttachedToWindow() && modalSelection.getRootView() == dialog)
                modalSelection.requestFocusFromTouch();
            else dialog.requestFocusFromTouch();
        }
        if (dialog == null && owner instanceof NativeActivity) {
            nativeLocalFocus(true);
            ((Activity)owner).getWindow().injectInputEvent(new KeyEvent(event));
            return true;
        }
        boolean handled = dialog != null ? dialog.dispatchKeyEvent(event) : inputCallback.dispatchKeyEvent(event);
        syncOwnerFocus();
        return dialog != null || handled;
    }

    boolean motion(MotionEvent event) {
        if (!ownsInput() || inputCallback == null) return false;
        View dialog = modal();
        if (dialog == null && owner instanceof NativeActivity) {
            nativeLocalFocus(true);
            ((Activity)owner).getWindow().injectInputEvent(MotionEvent.obtain(event));
            return true;
        }
        boolean handled = dialog != null ? dialog.dispatchGenericMotionEvent(event) : inputCallback.dispatchGenericMotionEvent(event);
        return dialog != null || handled;
    }

    void back() {
        if (!ownsInput()) return;
        View dialog = modal();
        if (dialog != null) {
            dialog.dispatchKeyEvent(new KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_BACK));
            dialog.dispatchKeyEvent(new KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_BACK));
            syncOwnerFocus();
        } else if (owner instanceof Activity) ((Activity)owner).onBackPressed();
    }

    @Override public void dismiss() {
        if (!embedded) { super.dismiss(); return; }
        if (!requested) return;
        requested = false;
        if (owner instanceof NativeActivity && localFocusTarget != null) {
            nativeLocalFocus(ownerResumed && nativeWindow != null && nativeWindow.isFocused() && modal() == null);
        }
        unbindNativeWindow();
        if (ownerDecor != null) ownerDecor.removeOnAttachStateChangeListener(nativeAttach);
        if (addedLocalFocusFlag) { ((Activity)owner).getWindow().clearFlags(WindowManager.LayoutParams.FLAG_LOCAL_FOCUS_MODE); addedLocalFocusFlag = false; }
        ownerDecor = null;
        if (owner instanceof Activity) ((Activity)owner).getApplication().unregisterActivityLifecycleCallbacks(ownerLifecycle);
        if (owner instanceof Activity) {
            Activity source = (Activity)owner;
            if (source.getWindow().getCallback() == inputCallback) {
                source.getWindow().setCallback(previousCallback);
                if (!source.isDestroyed() && previousCallback != null)
                    previousCallback.onWindowFocusChanged(source.hasWindowFocus());
            }
        }
        inputCallback = null; previousCallback = null; deliveredFocus = null; localFocusTarget = null;
        if (activeModal != null && activeModal.getViewTreeObserver().isAlive())
            activeModal.getViewTreeObserver().removeOnGlobalFocusChangeListener(modalFocus);
        if (activeModal != null) activeModal.removeOnAttachStateChangeListener(modalAttachment);
        activeModal = null; modalSelection = null;
        pending.remove(token);
        if (started) { started = false; onStop(); }
        CompanionHostActivity previous = host;
        host = null;
        if (previous != null && !previous.isFinishing()) previous.finish();
        if (dismissed != null) dismissed.onDismiss(this);
    }
}
