package io.github.hhhxiao.bedrockmap;

import android.content.Context;
import android.content.ComponentName;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.SystemClock;

import com.topjohnwu.superuser.Shell;
import com.topjohnwu.superuser.ipc.RootService;

import java.io.IOException;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/** A root shell is created only by an explicit permission button press. */
public final class RootPermission {
    private static final String TAG = "BedrockMapRoot";
    private static volatile Shell shell;
    private static volatile String state = "NotRequested";
    private static Context appContext;
    private static final Handler MAIN = new Handler(Looper.getMainLooper());
    private static FileConnection connection;
    private static long retryAfter;

    private static final class FileConnection implements ServiceConnection {
        final CountDownLatch ready = new CountDownLatch(1);
        volatile IWorldFiles files;

        @Override
        public void onServiceConnected(ComponentName name, IBinder binder) {
            synchronized (RootPermission.class) {
                if (connection != this) return;
                if (!binder.isBinderAlive()) {
                    failed(this, "Root file service returned a dead binder");
                    return;
                }
                files = IWorldFiles.Stub.asInterface(binder);
                ready.countDown();
            }
            AppLog.i(TAG, "Root file service connected");
        }

        @Override
        public void onServiceDisconnected(ComponentName name) { failed(this, "Root file service disconnected"); }
    }

    private static synchronized void failed(FileConnection failed, String message) {
        if (connection != failed) return;
        connection = null;
        retryAfter = SystemClock.elapsedRealtime() + 5000;
        failed.ready.countDown();
        MAIN.post(() -> RootService.unbind(failed));
        AppLog.w(TAG, message);
    }

    private RootPermission() {}

    private static native void onStateChanged();

    public static String status() {
        Shell current = shell;
        if (state.equals("Granted") && (current == null || !current.isAlive())) return "NotRequested";
        return state;
    }

    private static void setState(String value) {
        state = value;
        AppLog.i(TAG, "Root permission state: " + value);
        onStateChanged();
    }

    public static synchronized void request(Context context) {
        if (state.equals("Requesting")) return;
        appContext = context.getApplicationContext();
        setState("Requesting");
        new Thread(() -> {
            try {
                Shell current = shell;
                if (current == null || !current.isAlive()) {
                    // Do not use an API that creates a shell from the scanning path.
                    current = Shell.Builder.create().setCommands("su").setTimeout(60).build();
                }
                if (current.isRoot()) {
                    shell = current;
                    setState("Granted");
                } else {
                    current.close();
                    shell = null;
                    setState("Denied");
                }
            } catch (IOException | RuntimeException exception) {
                shell = null;
                AppLog.w(TAG, "Cannot obtain root shell", exception);
                String failure = "Denied";
                for (Throwable cause = exception; cause != null; cause = cause.getCause()) {
                    if (cause instanceof TimeoutException) failure = "Timeout";
                    if (cause instanceof IOException && cause.getMessage() != null
                            && cause.getMessage().contains("error=2")) failure = "Unavailable";
                }
                setState(failure);
            }
        }, "Root permission request").start();
    }

    public static IWorldFiles files() {
        Shell current = shell;
        if (!status().equals("Granted") || current == null || !current.isRoot()
                || Looper.myLooper() == Looper.getMainLooper()) return null;
        FileConnection pending;
        synchronized (RootPermission.class) {
            if (connection != null && connection.files != null && !connection.files.asBinder().isBinderAlive()) {
                failed(connection, "Cached root file service binder died");
            }
            if (connection == null) {
                if (retryAfter > SystemClock.elapsedRealtime()) return null;
                pending = new FileConnection();
                connection = pending;
                MAIN.post(() -> {
                    synchronized (RootPermission.class) {
                        if (connection != pending) return;
                    }
                    try {
                        // Run libsu's launch task on the explicitly authorized
                        // shell; RootService.bind() would obtain a new global shell.
                        Shell.Task task = RootService.bindOrTask(new Intent(appContext, RootFileService.class), MAIN::post, pending);
                        if (task != null) Shell.EXECUTOR.execute(() -> {
                            try {
                                current.execTask(task);
                            } catch (IOException | RuntimeException exception) {
                                failed(pending, "Cannot start root file service: " + exception);
                            }
                        });
                    } catch (RuntimeException exception) {
                        failed(pending, "Cannot bind root file service: " + exception);
                    }
                });
            } else {
                pending = connection;
            }
        }
        try {
            if (!pending.ready.await(10, TimeUnit.SECONDS)) failed(pending, "Root file service connection timed out");
            synchronized (RootPermission.class) {
                return connection == pending ? pending.files : null;
            }
        } catch (InterruptedException exception) {
            Thread.currentThread().interrupt();
            return null;
        }
    }
}
