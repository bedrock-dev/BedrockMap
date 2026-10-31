package org.bedrockmap;

import android.content.pm.PackageManager;
import android.content.ComponentName;
import android.content.Context;
import android.content.ServiceConnection;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.SystemClock;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

import rikka.shizuku.Shizuku;

/** Shizuku connection and permission handling; never requests access on startup. */
public final class ShizukuPermission {
    private static final String TAG = "BedrockMapShizuku";
    private static final int REQUEST_CODE = 1002;
    private static final long BIND_TIMEOUT_SECONDS = 10;
    private static final long BIND_RETRY_DELAY_MS = 5000;
    private static final Handler MAIN = new Handler(Looper.getMainLooper());
    private static boolean initialized;
    private static volatile boolean requesting;
    private static volatile boolean requestFailed;
    private static volatile Shizuku.UserServiceArgs serviceArgs;
    private static ComponentName serviceComponent;
    private static FileConnection connection;
    private static long retryAfter;

    private static final class FileConnection implements ServiceConnection {
        final CountDownLatch ready = new CountDownLatch(1);
        volatile IWorldFiles files;

        @Override
        public void onServiceConnected(ComponentName name, IBinder binder) {
            synchronized (ShizukuPermission.class) {
                if (connection != this) {
                    AppLog.d(TAG, "Ignoring connection callback from an expired bind attempt: " + name);
                    return;
                }
                if (!binder.isBinderAlive()) {
                    failConnection(this, "Service returned a dead binder", null);
                    return;
                }
                files = IWorldFiles.Stub.asInterface(binder);
                retryAfter = 0;
                ready.countDown();
            }
            AppLog.i(TAG, "World file service connected: component=" + name + ", binderAlive=true");
        }

        @Override
        public void onServiceDisconnected(ComponentName name) {
            AppLog.w(TAG, "World file service disconnected: component=" + name);
            failConnection(this, "World file service disconnected", null);
        }
    }

    private ShizukuPermission() {}

    private static native void onStateChanged();

    public static void initialize(Context context) {
        MAIN.post(() -> {
            if (initialized) return;
            initialized = true;
            serviceComponent = new ComponentName(context, WorldFileService.class);
            serviceArgs = new Shizuku.UserServiceArgs(serviceComponent)
                    .daemon(false).processNameSuffix("world_files").version(7);
            AppLog.i(TAG, "Initialized: appUid=" + android.os.Process.myUid() + ", package=" + context.getPackageName());
            Shizuku.addBinderReceivedListenerSticky(() -> {
                requesting = false;
                requestFailed = false;
                clearService();
                AppLog.i(TAG, "Shizuku binder received: permission=" + status());
                onStateChanged();
            });
            Shizuku.addBinderDeadListener(() -> {
                requesting = false;
                requestFailed = false;
                clearService();
                AppLog.w(TAG, "Shizuku binder died");
                onStateChanged();
            });
            Shizuku.addRequestPermissionResultListener((requestCode, grantResult) -> {
                if (requestCode != REQUEST_CODE) return;
                requesting = false;
                clearService();
                AppLog.i(TAG, "Permission result: grantResult=" + grantResult + ", status=" + status());
                onStateChanged();
            });
        });
    }

    private static void clearService() {
        FileConnection previous;
        synchronized (ShizukuPermission.class) {
            previous = connection;
            connection = null;
            retryAfter = 0;
            if (previous != null) {
                previous.ready.countDown();
                releaseConnection(previous);
            }
        }
    }

    private static void releaseConnection(FileConnection previous) {
        // Detach SDK callbacks before a later bind, without requesting permission
        // or terminating a service which may still be starting.
        MAIN.post(() -> {
            try {
                Shizuku.unbindUserService(serviceArgs, previous, false);
            } catch (RuntimeException exception) {
                AppLog.w(TAG, "Cannot detach old world file service connection", exception);
            }
        });
    }

    private static void failConnection(FileConnection failed, String reason, Throwable exception) {
        synchronized (ShizukuPermission.class) {
            if (connection != failed) return;
            connection = null;
            retryAfter = SystemClock.elapsedRealtime() + BIND_RETRY_DELAY_MS;
            failed.ready.countDown();
            // Enqueue removal while holding the lock so a later bind cannot
            // register its SDK callback before the old callback is removed.
            releaseConnection(failed);
        }
        String message = "World file service bind failed: " + reason + "; retry available after " + BIND_RETRY_DELAY_MS + " ms";
        if (exception == null) AppLog.w(TAG, message);
        else AppLog.w(TAG, message, exception);
    }

    private static void bind(FileConnection pending) {
        synchronized (ShizukuPermission.class) {
            if (connection != pending) return;
        }
        AppLog.i(TAG, "Binding world file service UserService: component=" + serviceComponent);
        try {
            AppLog.i(TAG, "Shizuku server: uid=" + Shizuku.getUid() + ", version=" + Shizuku.getVersion()
                    + ", context=" + Shizuku.getSELinuxContext());
        } catch (RuntimeException exception) {
            AppLog.w(TAG, "Cannot read Shizuku server diagnostics", exception);
        }
        try {
            Shizuku.bindUserService(serviceArgs, pending);
            AppLog.d(TAG, "UserService bind request returned; waiting for connection callback");
        } catch (RuntimeException exception) {
            failConnection(pending, "bindUserService threw " + exception.getClass().getSimpleName(), exception);
        }
    }

    // Called only from native workers and uses existing authorization.
    public static IWorldFiles files() {
        if (!status().equals("Granted") || Looper.myLooper() == Looper.getMainLooper()) return null;
        try {
            FileConnection pending;
            synchronized (ShizukuPermission.class) {
                if (serviceArgs == null) return null;
                if (connection != null && connection.files != null && !connection.files.asBinder().isBinderAlive()) {
                    failConnection(connection, "Cached file service binder died", null);
                }
                if (connection == null) {
                    if (retryAfter > SystemClock.elapsedRealtime()) return null;
                    pending = new FileConnection();
                    connection = pending;
                    MAIN.post(() -> bind(pending));
                } else {
                    pending = connection;
                }
            }
            boolean ready = pending.ready.await(BIND_TIMEOUT_SECONDS, TimeUnit.SECONDS);
            synchronized (ShizukuPermission.class) {
                if (connection != pending) return null;
                if (!ready && pending.files == null) {
                    failConnection(pending, "No onServiceConnected callback within " + BIND_TIMEOUT_SECONDS
                            + " seconds; inspect logcat tags ShizukuServiceStarter, UserServiceManager, "
                            + "UserServiceRecord and AndroidRuntime for service startup errors", null);
                    return null;
                }
                return pending.files;
            }
        } catch (InterruptedException exception) {
            Thread.currentThread().interrupt();
        } catch (RuntimeException exception) {
            AppLog.w(TAG, "Cannot connect file service through Shizuku", exception);
        }
        return null;
    }

    public static String status() {
        try {
            if (!Shizuku.pingBinder()) return "Unavailable";
            if (Shizuku.isPreV11()) return "Unsupported";
            if (Shizuku.checkSelfPermission() == PackageManager.PERMISSION_GRANTED) return "Granted";
            if (Shizuku.shouldShowRequestPermissionRationale()) return "Denied";
            if (requesting) return "Requesting";
            return requestFailed ? "Error" : "NotGranted";
        } catch (RuntimeException exception) {
            AppLog.w(TAG, "Cannot read Shizuku permission state", exception);
            return "Unavailable";
        }
    }

    public static void request() {
        MAIN.post(() -> {
            final String current = status();
            AppLog.i(TAG, "Permission button pressed: status=" + current);
            if (!current.equals("NotGranted") && !current.equals("Error")) {
                onStateChanged();
                return;
            }
            requesting = true;
            requestFailed = false;
            onStateChanged();
            try {
                Shizuku.requestPermission(REQUEST_CODE);
            } catch (RuntimeException exception) {
                requesting = false;
                requestFailed = true;
                AppLog.w(TAG, "Cannot request Shizuku permission", exception);
                onStateChanged();
            }
        });
    }
}
