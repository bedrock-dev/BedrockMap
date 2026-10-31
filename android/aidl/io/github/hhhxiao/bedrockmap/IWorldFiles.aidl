package io.github.hhhxiao.bedrockmap;

import android.os.ParcelFileDescriptor;

interface IWorldFiles {
    ParcelFileDescriptor openRead(String path, out int[] error) = 0;
    long[] stat(String path) = 1;
    String[] list(String path) = 2;
}
