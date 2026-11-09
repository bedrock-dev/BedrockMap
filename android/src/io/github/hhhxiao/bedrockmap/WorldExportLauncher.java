package io.github.hhhxiao.bedrockmap;

import android.content.ActivityNotFoundException;
import android.content.ClipData;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ResolveInfo;
import android.net.Uri;
import androidx.core.content.FileProvider;
import java.io.File;
import java.util.List;

/** Opens an exported world through an Android content URI and application chooser. */
public final class WorldExportLauncher {
    private static final String TAG = "BedrockMapExport";
    private static final String MIME_TYPE = "application/octet-stream";

    private WorldExportLauncher() {}

    public static String open(Context context, String path, String chooserTitle) {
        try {
            File file = new File(path);
            if (!file.isFile()) return "The exported world file is unavailable.";

            Context appContext = context.getApplicationContext();
            Uri uri = FileProvider.getUriForFile(
                    appContext, appContext.getPackageName() + ".qtprovider", file);
            Intent view = new Intent(Intent.ACTION_VIEW)
                    .setDataAndType(uri, MIME_TYPE)
                    .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            view.setClipData(ClipData.newRawUri(file.getName(), uri));

            // Some OEM chooser implementations do not propagate the temporary
            // URI grant. Grant read access explicitly to every visible handler.
            List<ResolveInfo> handlers = appContext.getPackageManager()
                    .queryIntentActivities(view, 0);
            for (ResolveInfo handler : handlers) {
                String packageName = handler.activityInfo.packageName;
                if (!packageName.equals(appContext.getPackageName())) {
                    appContext.grantUriPermission(packageName, uri,
                            Intent.FLAG_GRANT_READ_URI_PERMISSION);
                }
            }

            Intent chooser = Intent.createChooser(view, chooserTitle);
            chooser.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                    | Intent.FLAG_ACTIVITY_NEW_TASK);
            chooser.putExtra(Intent.EXTRA_EXCLUDE_COMPONENTS, new ComponentName[] {
                    new ComponentName(appContext, BedrockMapActivity.class)
            });
            appContext.startActivity(chooser);
            return "";
        } catch (ActivityNotFoundException exception) {
            AppLog.w(TAG, "No application can open the exported world.", exception);
            return "No installed application can open mcworld files.";
        } catch (RuntimeException exception) {
            AppLog.w(TAG, "Cannot open the exported world.", exception);
            return exception.getMessage() != null ? exception.getMessage()
                                                   : exception.getClass().getSimpleName();
        }
    }
}
