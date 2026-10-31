package io.github.hhhxiao.bedrockmap;

import android.content.Context;
import android.content.UriPermission;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import java.util.Objects;

/** Metadata for individual provider documents, independent of world storage. */
public final class DocumentMetadata {
    private DocumentMetadata() {}

    /** Android grants distinguish URI encodings that Qt may normalize. */
    public static Uri grantedUri(Context context, String source) {
        Uri requested = Uri.parse(source);
        for (UriPermission permission : context.getContentResolver().getPersistedUriPermissions()) {
            Uri granted = permission.getUri();
            if (Objects.equals(requested.getScheme(), granted.getScheme())
                    && Objects.equals(requested.getAuthority(), granted.getAuthority())
                    && requested.getPathSegments().equals(granted.getPathSegments())
                    && Objects.equals(requested.getQuery(), granted.getQuery())
                    && Objects.equals(requested.getFragment(), granted.getFragment())) {
                return granted;
            }
        }
        return requested;
    }

    public static String displayName(Context context, String source) {
        try (Cursor cursor = context.getContentResolver().query(
                grantedUri(context, source), new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                int column = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (column >= 0 && !cursor.isNull(column)) {
                    String name = cursor.getString(column);
                    if (name != null) return name;
                }
            }
        } catch (RuntimeException exception) {
            AppLog.w("BedrockMapFiles", "Cannot query the document display name.", exception);
        }
        return "";
    }

    public static boolean isTreeUri(Context context, String source) {
        try {
            return DocumentsContract.isTreeUri(Uri.parse(source));
        } catch (RuntimeException exception) {
            AppLog.w("BedrockMapFiles", "Cannot inspect the content URI.", exception);
            return false;
        }
    }
}
