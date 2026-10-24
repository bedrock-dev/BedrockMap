package org.bedrockmap;

import android.os.Bundle;

import org.qtproject.qt.android.bindings.QtActivity;

public class BedrockMapActivity extends QtActivity {
    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        StoragePermission.ensure(this);
    }
}
