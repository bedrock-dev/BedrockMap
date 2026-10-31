package org.bedrockmap;

import android.Manifest;
import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.Settings;

import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

/**
 * Checks shared-storage access and requests it only on explicit user action.
 *
 * Android 11+ has no runtime dialog for MANAGE_EXTERNAL_STORAGE; the only way
 * to ask is the dedicated system settings page. Android 10 and older still use
 * the normal READ/WRITE_EXTERNAL_STORAGE runtime permissions.
 */
public final class StoragePermission {
    private static final int REQUEST_CODE = 1001;

    private StoragePermission() {}

    public static boolean hasAccess(Context context) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            return Environment.isExternalStorageManager();
        }
        return ContextCompat.checkSelfPermission(context, Manifest.permission.READ_EXTERNAL_STORAGE)
                        == PackageManager.PERMISSION_GRANTED
                && ContextCompat.checkSelfPermission(context, Manifest.permission.WRITE_EXTERNAL_STORAGE)
                        == PackageManager.PERMISSION_GRANTED;
    }

    public static void request(Context context) {
        if (!(context instanceof Activity) || hasAccess(context)) return;
        final Activity activity = (Activity) context;
        activity.runOnUiThread(() -> requestFromActivity(activity));
    }

    private static void requestFromActivity(Activity activity) {
        if (hasAccess(activity)) return;

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            final Intent appSettings = new Intent(
                    Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                    Uri.parse("package:" + activity.getPackageName()));
            try {
                activity.startActivity(appSettings);
                return;
            } catch (ActivityNotFoundException | SecurityException ignored) {
                // Some vendors do not expose the per-app page.
            }
            try {
                activity.startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
            } catch (ActivityNotFoundException | SecurityException ignored) {
                // Leave the app usable with only its internal storage.
            }
            return;
        }

        ActivityCompat.requestPermissions(
                activity,
                new String[] {
                        Manifest.permission.READ_EXTERNAL_STORAGE,
                        Manifest.permission.WRITE_EXTERNAL_STORAGE
                },
                REQUEST_CODE);
    }
}
