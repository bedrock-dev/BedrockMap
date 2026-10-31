package io.github.hhhxiao.bedrockmap;

import android.os.ParcelFileDescriptor;
import android.system.ErrnoException;
import android.system.Os;
import android.system.OsConstants;
import android.system.StructStat;

import java.io.FileDescriptor;
import java.io.IOException;

/** Privileged read-only filesystem operations shared by Shizuku and root. */
public final class WorldFileService extends IWorldFiles.Stub {
    public WorldFileService() {}

    @Override
    public ParcelFileDescriptor openRead(String path, int[] error) {
        error[0] = 0;
        try {
            FileDescriptor file = Os.open(path, OsConstants.O_RDONLY | OsConstants.O_CLOEXEC, 0);
            try {
                return ParcelFileDescriptor.dup(file);
            } finally {
                Os.close(file);
            }
        } catch (ErrnoException exception) {
            error[0] = exception.errno;
        } catch (IOException exception) {
            error[0] = OsConstants.EIO;
        }
        return null;
    }

    @Override
    public long[] stat(String path) {
        try {
            StructStat info = Os.lstat(path);
            return new long[] {0, info.st_mode, info.st_size, info.st_mtime};
        } catch (ErrnoException exception) {
            return new long[] {-exception.errno, 0, 0, 0};
        }
    }

    @Override
    public String[] list(String path) {
        return new java.io.File(path).list();
    }
}
