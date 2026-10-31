package org.bedrockmap;

import android.content.Intent;
import android.os.IBinder;
import com.topjohnwu.superuser.ipc.RootService;

/** libsu hosts the same filesystem Binder used by Shizuku. */
public final class RootFileService extends RootService {
    @Override
    public IBinder onBind(Intent intent) { return new WorldFileService(); }
}
