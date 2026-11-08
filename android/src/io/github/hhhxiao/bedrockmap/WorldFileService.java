package io.github.hhhxiao.bedrockmap;

import android.os.ParcelFileDescriptor;
import android.system.ErrnoException;
import android.system.Os;
import android.system.OsConstants;
import android.system.StructStat;

import java.io.File;
import java.io.FileDescriptor;
import java.io.IOException;

/** Privileged filesystem operations shared by Shizuku and root. */
public final class WorldFileService extends IWorldFiles.Stub {
    public WorldFileService() {}

    @Override
    public ParcelFileDescriptor open(String path, int flags, int mode, int[] error) {
        error[0] = 0;
        try {
            FileDescriptor file = Os.open(path, flags | OsConstants.O_CLOEXEC, mode);
            try {
                if ((flags & OsConstants.O_CREAT) != 0) adoptParentOwner(path, file);
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
    public String[] list(String path, int[] error) {
        error[0] = 0;
        String[] entries = new File(path).list();
        if (entries != null) return entries;
        // File.list() hides errno; recover the most likely cause.
        try {
            error[0] = OsConstants.S_ISDIR(Os.lstat(path).st_mode) ? OsConstants.EACCES : OsConstants.ENOTDIR;
        } catch (ErrnoException exception) {
            error[0] = exception.errno;
        }
        return null;
    }

    @Override
    public int mkdir(String path, int mode) {
        try {
            Os.mkdir(path, mode);
            adoptParentOwner(path, null);
            return 0;
        } catch (ErrnoException exception) {
            return -exception.errno;
        }
    }

    @Override
    public int rename(String from, String to) {
        try {
            Os.rename(from, to);
            return 0;
        } catch (ErrnoException exception) {
            return -exception.errno;
        }
    }

    @Override
    public int remove(String path) {
        try {
            Os.remove(path);
            return 0;
        } catch (ErrnoException exception) {
            return -exception.errno;
        }
    }

    /**
     * New entries created by root would otherwise belong to uid 0 and become
     * unwritable for the app that owns the directory. Shizuku runs as shell and
     * cannot change ownership, so its entries keep the shell owner.
     */
    private static void adoptParentOwner(String path, FileDescriptor file) {
        if (Os.getuid() != 0) return;
        try {
            String parent = new File(path).getParent();
            if (parent == null) return;
            StructStat owner = Os.stat(parent);
            StructStat current = file != null ? Os.fstat(file) : Os.lstat(path);
            if (current.st_uid != 0) return;
            if (file != null) Os.fchown(file, owner.st_uid, owner.st_gid);
            else Os.lchown(path, owner.st_uid, owner.st_gid);
        } catch (ErrnoException exception) {
            AppLog.w("BedrockMapFiles", "Cannot adopt the parent owner for " + path, exception);
        }
    }
}
