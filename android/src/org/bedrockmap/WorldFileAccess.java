package org.bedrockmap;

import android.os.ParcelFileDescriptor;
import android.os.RemoteException;
import android.system.OsConstants;

import java.io.IOException;

/** Read-only privileged source retained for scanning and importing a world. */
public final class WorldFileAccess {
    private final IWorldFiles files;

    private WorldFileAccess(IWorldFiles files) { this.files = files; }

    // Native workers call this only after ordinary local access is unavailable.
    public static WorldFileAccess connect(String path) {
        IWorldFiles service = ShizukuPermission.files();
        WorldFileAccess access = accessible(service, path, "Shizuku");
        if (access != null) return access;
        return accessible(RootPermission.files(), path, "root");
    }

    private static WorldFileAccess accessible(IWorldFiles service, String path, String backend) {
        if (service == null) return null;
        try {
            long[] info = service.stat(path);
            if (info != null && info.length == 4 && info[0] == 0
                    && OsConstants.S_ISDIR((int) info[1]) && service.list(path) != null) {
                AppLog.d("BedrockMapFiles", "Using " + backend + ": " + path);
                return new WorldFileAccess(service);
            }
        } catch (RemoteException | RuntimeException exception) {
            AppLog.w("BedrockMapFiles", "Cannot access directory: " + path, exception);
        }
        return null;
    }

    public int openRead(String path) {
        int[] error = {0};
        try (ParcelFileDescriptor descriptor = files.openRead(path, error)) {
            if (descriptor == null) return error[0] > 0 ? -error[0] : -OsConstants.EIO;
            return descriptor.detachFd();
        } catch (RemoteException | IOException | RuntimeException exception) {
            AppLog.w("BedrockMapFiles", "Cannot open file: " + path, exception);
            return -OsConstants.EIO;
        }
    }

    public long[] stat(String path) {
        try {
            return files.stat(path);
        } catch (RemoteException | RuntimeException exception) {
            return new long[] {-OsConstants.EIO, 0, 0, 0};
        }
    }

    public String[] list(String path) {
        try {
            return files.list(path);
        } catch (RemoteException | RuntimeException exception) {
            AppLog.w("BedrockMapFiles", "Cannot list directory: " + path, exception);
            return null;
        }
    }

}
