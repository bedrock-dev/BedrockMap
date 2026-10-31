package org.bedrockmap;

import android.content.ClipData;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.Parcelable;

import org.qtproject.qt.android.bindings.QtActivity;

/** Normalizes Android share intents into the file-open path handled by Qt. */
public final class BedrockMapActivity extends QtActivity {
    @Override
    public void onCreate(Bundle savedInstanceState) {
        setIntent(asFileViewIntent(getIntent()));
        super.onCreate(savedInstanceState);
    }

    @Override
    public void onNewIntent(Intent intent) {
        Intent normalized = asFileViewIntent(intent);
        setIntent(normalized);
        super.onNewIntent(normalized);
    }

    @SuppressWarnings("deprecation")
    private Intent asFileViewIntent(Intent intent) {
        if (intent == null || !Intent.ACTION_SEND.equals(intent.getAction())) return intent;

        Uri uri = sentUri(intent);
        if (uri == null) return intent;

        String mimeType = intent.getType();
        if (mimeType == null || mimeType.isEmpty()) {
            try {
                mimeType = getContentResolver().getType(uri);
            } catch (RuntimeException exception) {
                AppLog.w("BedrockMapFiles", "Cannot query the shared document MIME type.", exception);
            }
        }

        Intent view = new Intent(intent);
        view.setAction(Intent.ACTION_VIEW);
        view.setDataAndType(uri, mimeType);
        view.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        return view;
    }

    @SuppressWarnings("deprecation")
    private static Uri sentUri(Intent intent) {
        if (intent.getData() != null) return intent.getData();

        Parcelable stream = intent.getParcelableExtra(Intent.EXTRA_STREAM);
        if (stream instanceof Uri) return (Uri) stream;

        ClipData clipData = intent.getClipData();
        if (clipData != null && clipData.getItemCount() > 0) return clipData.getItemAt(0).getUri();
        return null;
    }
}
