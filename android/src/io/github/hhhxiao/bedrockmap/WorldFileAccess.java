package io.github.hhhxiao.bedrockmap;

import android.os.ParcelFileDescriptor;
import android.os.RemoteException;
import android.system.OsConstants;

import java.io.IOException;

/**
 * Native bridge to the privileged file service. Shizuku is preferred over root.
 * Called only from native worker threads; both providers return null on the
 * main looper and never request authorization themselves.
 */
public final class WorldFileAccess {
    private static final String TAG = "BedrockMapFiles";
    private static final long[] NO_SERVICE_STAT = {-OsConstants.ENOSYS, 0, 0, 0};

    private WorldFileAccess() {}

    private static IWorldFiles service() {
        IWorldFiles files = ShizukuPermission.files();
        return files != null ? files : RootPermission.files();
    }

    public static boolean available() { return service() != null; }

    /** Returns an owned descriptor, or a negated errno. */
    public static int open(String path, int flags, int mode) {
        IWorldFiles files = service();
        if (files == null) return -OsConstants.ENOSYS;
        int[] error = {0};
        try (ParcelFileDescriptor descriptor = files.open(path, flags, mode, error)) {
            if (descriptor == null) return error[0] > 0 ? -error[0] : -OsConstants.EIO;
            return descriptor.detachFd();
        } catch (RemoteException | IOException | RuntimeException exception) {
            AppLog.w(TAG, "Cannot open file: " + path, exception);
            return -OsConstants.EIO;
        }
    }

    public static long[] stat(String path) {
        IWorldFiles files = service();
        if (files == null) return NO_SERVICE_STAT.clone();
        try {
            long[] result = files.stat(path);
            return result != null ? result : new long[] {-OsConstants.EIO, 0, 0, 0};
        } catch (RemoteException | RuntimeException exception) {
            return new long[] {-OsConstants.EIO, 0, 0, 0};
        }
    }

    public static String[] list(String path, int[] error) {
        IWorldFiles files = service();
        if (files == null) {
            error[0] = OsConstants.ENOSYS;
            return null;
        }
        try {
            return files.list(path, error);
        } catch (RemoteException | RuntimeException exception) {
            AppLog.w(TAG, "Cannot list directory: " + path, exception);
            error[0] = OsConstants.EIO;
            return null;
        }
    }

    public static int mkdir(String path, int mode) {
        IWorldFiles files = service();
        if (files == null) return -OsConstants.ENOSYS;
        try {
            return files.mkdir(path, mode);
        } catch (RemoteException | RuntimeException exception) {
            return -OsConstants.EIO;
        }
    }

    public static int rename(String from, String to) {
        IWorldFiles files = service();
        if (files == null) return -OsConstants.ENOSYS;
        try {
            return files.rename(from, to);
        } catch (RemoteException | RuntimeException exception) {
            return -OsConstants.EIO;
        }
    }

    public static int remove(String path) {
        IWorldFiles files = service();
        if (files == null) return -OsConstants.ENOSYS;
        try {
            return files.remove(path);
        } catch (RemoteException | RuntimeException exception) {
            return -OsConstants.EIO;
        }
    }
}
