package eternal.future.tefkernel;

import android.app.Activity;
import android.app.Application;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.os.Bundle;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;

import java.lang.reflect.Field;
import java.util.Map;

/**
 * In-process TEFKernel UI. This class is packaged into the loader DEX.
 * It deliberately uses the current Activity decor view instead of a system
 * overlay window, so it needs no SYSTEM_ALERT_WINDOW permission.
 */
public final class TefUiBridge {
    private static final int BLUE = Color.rgb(0, 83, 190);
    private static final int BLUE_DARK = Color.rgb(0, 52, 128);
    private static final int PANEL = Color.rgb(35, 38, 51);
    private static final int PANEL_2 = Color.rgb(46, 49, 64);
    private static final int TEXT = Color.rgb(242, 243, 250);
    private static final int MUTED = Color.rgb(171, 174, 192);

    private static volatile boolean initialized;
    private static Application application;
    private static volatile Activity currentActivity;
    private static TefActivityCallbacks callbacks;
    private static volatile TefOverlayView overlay;

    private TefUiBridge() {}

    public static native int nativeGetFeatureCount();
    public static native String nativeGetFeatureId(int index);
    public static native String nativeGetFeatureName(int index);
    public static native boolean nativeIsFeatureEnabled(String featureId);
    public static native void nativeOnFeatureToggle(String featureId, boolean enabled);

    public static synchronized void initialize() {
        if (initialized) {
            refresh();
            return;
        }
        application = getApplication();
        if (application == null) return;
        currentActivity = findCurrentActivity();
        callbacks = new TefActivityCallbacks();
        application.registerActivityLifecycleCallbacks(callbacks);
        initialized = true;
        if (currentActivity != null) attach(currentActivity);
    }

    public static synchronized void shutdown() {
        if (!initialized) return;
        detach();
        if (application != null && callbacks != null) {
            application.unregisterActivityLifecycleCallbacks(callbacks);
        }
        callbacks = null;
        application = null;
        currentActivity = null;
        initialized = false;
    }

    public static synchronized void refresh() {
        if (overlay == null) return;
        final Activity activity = currentActivity;
        if (activity == null) return;
        activity.runOnUiThread(() -> {
            if (overlay != null) overlay.invalidate();
        });
    }

    private static Application getApplication() {
        try {
            Class<?> activityThread = Class.forName("android.app.ActivityThread");
            Object thread = activityThread.getDeclaredMethod("currentActivityThread").invoke(null);
            return (Application) activityThread.getDeclaredMethod("getApplication").invoke(thread);
        } catch (Throwable ignored) {
            return null;
        }
    }

    /** Best-effort lookup for the case where the native plugin initializes after onResume. */
    private static Activity findCurrentActivity() {
        try {
            Class<?> activityThreadClass = Class.forName("android.app.ActivityThread");
            Object thread = activityThreadClass.getDeclaredMethod("currentActivityThread").invoke(null);
            Field activitiesField = activityThreadClass.getDeclaredField("mActivities");
            activitiesField.setAccessible(true);
            Object activities = activitiesField.get(thread);
            if (activities instanceof Map) {
                for (Object record : ((Map<?, ?>) activities).values()) {
                    Field activityField = record.getClass().getDeclaredField("activity");
                    activityField.setAccessible(true);
                    Object activity = activityField.get(record);
                    if (activity instanceof Activity && !((Activity) activity).isFinishing()) {
                        return (Activity) activity;
                    }
                }
            }
        } catch (Throwable ignored) {
            // The lifecycle callback will still attach the overlay on the next resume.
        }
        return null;
    }

    private static void attach(Activity activity) {
        if (!initialized || activity == null) return;
        activity.runOnUiThread(() -> {
            if (!initialized || currentActivity != activity) return;
            detachOnUiThread();
            ViewGroup decor = (ViewGroup) activity.getWindow().getDecorView();
            overlay = new TefOverlayView(activity);
            decor.addView(overlay, new ViewGroup.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.MATCH_PARENT));
        });
    }

    private static void detach() {
        Activity activity = currentActivity;
        if (activity != null) activity.runOnUiThread(TefUiBridge::detachOnUiThread);
    }

    private static void detachOnUiThread() {
        if (overlay == null) return;
        ViewParentHelper.remove(overlay);
        overlay = null;
    }

    private static final class ViewParentHelper {
        static void remove(View view) {
            if (view.getParent() instanceof ViewGroup) {
                ((ViewGroup) view.getParent()).removeView(view);
            }
        }
    }

    private static final class TefActivityCallbacks implements Application.ActivityLifecycleCallbacks {
        @Override public void onActivityCreated(Activity activity, Bundle state) {}
        @Override public void onActivityStarted(Activity activity) {}
        @Override public void onActivityResumed(Activity activity) {
            currentActivity = activity;
            attach(activity);
        }
        @Override public void onActivityPaused(Activity activity) {}
        @Override public void onActivityStopped(Activity activity) {
            if (currentActivity == activity) detach();
        }
        @Override public void onActivitySaveInstanceState(Activity activity, Bundle state) {}
        @Override public void onActivityDestroyed(Activity activity) {
            if (currentActivity == activity) {
                detach();
                currentActivity = null;
            }
        }
    }

    private static final class TefOverlayView extends View {
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final RectF button = new RectF();
        private final RectF panel = new RectF();
        private boolean expanded;
        private float downX;
        private float downY;
        private float buttonX;
        private float buttonY;
        private boolean moved;

        TefOverlayView(Context context) {
            super(context);
            setLayerType(View.LAYER_TYPE_SOFTWARE, null);
            buttonX = 24f;
            buttonY = 0f;
        }

        private float dp(float value) {
            return value * getResources().getDisplayMetrics().density;
        }

        private void setupText(float size, int color, boolean bold) {
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(color);
            paint.setTextSize(dp(size));
            paint.setTypeface(bold ? Typeface.DEFAULT_BOLD : Typeface.DEFAULT);
        }

        @Override protected void onDraw(Canvas canvas) {
            super.onDraw(canvas);
            final float density = getResources().getDisplayMetrics().density;
            final float size = dp(54);
            if (buttonY == 0f) buttonY = getHeight() * 0.22f;
            button.set(buttonX, buttonY, buttonX + size, buttonY + size);

            paint.setShadowLayer(dp(8), 0, dp(3), 0x66000000);
            paint.setColor(BLUE);
            canvas.drawRoundRect(button, dp(17), dp(17), paint);
            paint.clearShadowLayer();
            setupText(24, TEXT, true);
            paint.setTextAlign(Paint.Align.CENTER);
            canvas.drawText("T", button.centerX(), button.centerY() + dp(8), paint);
            paint.setTextAlign(Paint.Align.LEFT);

            if (!expanded) return;
            drawPanel(canvas, density);
        }

        private void drawPanel(Canvas canvas, float density) {
            final float margin = dp(18);
            final float left = margin;
            final float right = getWidth() - margin;
            final float top = Math.max(dp(70), button.bottom + dp(14));
            final int count = Math.max(1, nativeGetFeatureCount());
            final float rowHeight = dp(58);
            final float height = dp(76) + count * rowHeight;
            panel.set(left, top, right, Math.min(getHeight() - dp(24), top + height));

            paint.setColor(PANEL);
            paint.setShadowLayer(dp(14), 0, dp(5), 0x88000000);
            canvas.drawRoundRect(panel, dp(22), dp(22), paint);
            paint.clearShadowLayer();

            setupText(20, TEXT, true);
            canvas.drawText("TEF 模组控制", panel.left + dp(22), panel.top + dp(35), paint);
            setupText(12, MUTED, false);
            canvas.drawText("点击开关即可启用或关闭模组功能", panel.left + dp(22), panel.top + dp(57), paint);

            for (int i = 0; i < nativeGetFeatureCount(); i++) {
                final float rowTop = panel.top + dp(76) + i * rowHeight;
                paint.setColor(PANEL_2);
                canvas.drawRoundRect(new RectF(panel.left + dp(12), rowTop,
                        panel.right - dp(12), rowTop + rowHeight - dp(8)),
                        dp(14), dp(14), paint);
                String name = nativeGetFeatureName(i);
                String id = nativeGetFeatureId(i);
                setupText(15, TEXT, true);
                canvas.drawText(name == null ? "未命名功能" : name,
                        panel.left + dp(26), rowTop + dp(24), paint);
                setupText(11, MUTED, false);
                canvas.drawText(id == null ? "" : id,
                        panel.left + dp(26), rowTop + dp(43), paint);

                final boolean enabled = id != null && nativeIsFeatureEnabled(id);
                final float switchRight = panel.right - dp(28);
                final float switchLeft = switchRight - dp(48);
                paint.setColor(enabled ? BLUE : Color.rgb(90, 93, 108));
                canvas.drawRoundRect(new RectF(switchLeft, rowTop + dp(16),
                        switchRight, rowTop + dp(40)), dp(12), dp(12), paint);
                paint.setColor(TEXT);
                canvas.drawCircle(enabled ? switchRight - dp(12) : switchLeft + dp(12),
                        rowTop + dp(28), dp(9), paint);
            }
        }

        @Override public boolean onTouchEvent(MotionEvent event) {
            final float x = event.getX();
            final float y = event.getY();
            switch (event.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    if (!expanded && !button.contains(x, y)) return false;
                    downX = x;
                    downY = y;
                    moved = false;
                    return true;
                case MotionEvent.ACTION_MOVE:
                    if (Math.abs(x - downX) > dp(8) || Math.abs(y - downY) > dp(8)) moved = true;
                    if (!expanded && moved) {
                        buttonX = clamp(x - dp(27), 0, getWidth() - dp(54));
                        buttonY = clamp(y - dp(27), 0, getHeight() - dp(54));
                        invalidate();
                    }
                    return true;
                case MotionEvent.ACTION_UP:
                    if (moved) return true;
                    if (button.contains(x, y)) {
                        expanded = !expanded;
                        invalidate();
                        return true;
                    }
                    if (expanded && panel.contains(x, y)) {
                        toggleFeatureAt(x, y);
                        return true;
                    }
                    if (expanded) {
                        expanded = false;
                        invalidate();
                    }
                    return true;
                default:
                    return true;
            }
        }

        private void toggleFeatureAt(float x, float y) {
            final float rowHeight = dp(58);
            final int index = (int) ((y - panel.top - dp(76)) / rowHeight);
            if (index < 0 || index >= nativeGetFeatureCount()) return;
            final String id = nativeGetFeatureId(index);
            if (id != null) nativeOnFeatureToggle(id, !nativeIsFeatureEnabled(id));
            invalidate();
        }

        private float clamp(float value, float min, float max) {
            return Math.max(min, Math.min(max, value));
        }
    }
}
