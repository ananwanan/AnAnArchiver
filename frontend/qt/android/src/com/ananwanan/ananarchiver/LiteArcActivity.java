package com.ananwanan.ananarchiver;

import android.Manifest;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.Settings;

import org.qtproject.qt.android.bindings.QtActivity;

/** Only platform storage consent lives here; archives are handled by the C++ core. */
public class LiteArcActivity extends QtActivity {
    private static final int STORAGE_PERMISSION_REQUEST = 4701;
    private volatile boolean storageRequestPending = false;
    private boolean allFilesSettingsOpened = false;

    public boolean hasStorageAccess() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            return Environment.isExternalStorageManager();
        }
        return checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED
            && checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED;
    }

    public boolean isStorageRequestPending() {
        return storageRequestPending;
    }

    /** Called on Android's UI thread after the QML explanation is accepted. */
    public boolean requestStorageAccess() {
        if (hasStorageAccess() || storageRequestPending) return true;
        storageRequestPending = true;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            // This is special app access, not a runtime permission dialog.
            Intent appSettings = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                Uri.parse("package:" + getPackageName()));
            try {
                allFilesSettingsOpened = true;
                startActivity(appSettings);
                return true;
            } catch (ActivityNotFoundException | SecurityException unavailable) {
                // Some vendor ROMs provide only the global all-files list.
                try {
                    startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
                    return true;
                } catch (ActivityNotFoundException | SecurityException alsoUnavailable) {
                    allFilesSettingsOpened = false;
                    storageRequestPending = false;
                    return false;
                }
            }
        }
        try {
            requestPermissions(new String[] {
                Manifest.permission.READ_EXTERNAL_STORAGE,
                Manifest.permission.WRITE_EXTERNAL_STORAGE
            }, STORAGE_PERMISSION_REQUEST);
            return true;
        } catch (SecurityException unavailable) {
            storageRequestPending = false;
            return false;
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (allFilesSettingsOpened) {
            allFilesSettingsOpened = false;
            storageRequestPending = false;
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == STORAGE_PERMISSION_REQUEST) storageRequestPending = false;
    }
}
