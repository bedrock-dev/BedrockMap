package io.github.hhhxiao.bedrockmap;

import android.os.ParcelFileDescriptor;

// Bump the Shizuku UserService version in ShizukuPermission when this changes.
// Status results are 0 on success or a negated errno.
interface IWorldFiles {
    ParcelFileDescriptor open(String path, int flags, int mode, out int[] error) = 0;
    long[] stat(String path) = 1;
    String[] list(String path, out int[] error) = 2;
    int mkdir(String path, int mode) = 3;
    int rename(String from, String to) = 4;
    int remove(String path) = 5;
}
