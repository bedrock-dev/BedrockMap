package io.github.hhhxiao.bedrockmap;

import android.util.Log;

/** Shared Java logging entry point for logcat and the application's log page. */
public final class AppLog {
    private static volatile boolean nativeReady;

    private AppLog() {}

    private static native void onMessage(int priority, String tag, String message);

    public static void setNativeEnabled(boolean enabled) { nativeReady = enabled; }

    public static void i(String tag, String message) { write(Log.INFO, tag, message); }

    public static void d(String tag, String message) { write(Log.DEBUG, tag, message); }

    public static void w(String tag, String message) { write(Log.WARN, tag, message); }

    public static void w(String tag, String message, Throwable exception) {
        write(Log.WARN, tag, message + "\n" + Log.getStackTraceString(exception));
    }

    public static void write(int priority, String tag, String message) {
        Log.println(priority, tag, message);
        if (nativeReady) {
            onMessage(priority, tag, message);
        }
    }
}
