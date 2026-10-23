package pl.szymor.abc;

import android.app.Activity;
import android.content.Intent;

/** Small Java entry points used by the native SDL layer. */
public final class PlatformBridge {
    private PlatformBridge() {}

    public static native void nativeAudioFocus(boolean hasFocus);

    public static void openContentManager(Activity activity) {
        Intent intent = new Intent(activity, SetupActivity.class);
        intent.putExtra("manage", true);
        activity.startActivity(intent);
    }

    public static void restartActivity(Activity activity) {
        activity.recreate();
    }
}
