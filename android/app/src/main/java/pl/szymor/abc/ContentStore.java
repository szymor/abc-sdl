package pl.szymor.abc;

import android.content.Context;
import android.content.SharedPreferences;
import android.content.res.AssetManager;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;

/** Locations and simple state for the user-supplied game data. */
final class ContentStore {
    static final String PREFS = "abc";
    static final String KEY_ABC = "abc_root";
    static final String KEY_ADDON = "addon_root";
    static final String KEY_IMPORTED_AT = "imported_at";
    static final String KEY_FRM_COUNT = "frm_count";
    static final String KEY_BYTES = "bytes";

    private ContentStore() {}

    static File baseDir(Context context) {
        File internal = context.getFilesDir();
        // A developer can push a test tree into internal storage with
        // `adb shell run-as ...`; prefer it only when it actually contains
        // game data.  Normal imports still go to app-specific external
        // storage, which is writable through the Storage Access Framework.
        if (new File(internal, "abc/FRM").isDirectory()) {
            return internal;
        }
        File external = context.getExternalFilesDir(null);
        return external != null ? external : internal;
    }

    static File abcDir(Context context) {
        return new File(baseDir(context), "abc");
    }

    static File addonDir(Context context) {
        return new File(baseDir(context), "addon");
    }

    static File stagingAbc(Context context) {
        return new File(baseDir(context), "abc.importing");
    }

    static File stagingAddon(Context context) {
        return new File(baseDir(context), "addon.importing");
    }

    static File backupAbc(Context context) {
        return new File(baseDir(context), "abc.old");
    }

    static File backupAddon(Context context) {
        return new File(baseDir(context), "addon.old");
    }

    static String dataRoot(Context context) {
        return abcDir(context).getAbsolutePath();
    }

    static String addonRoot(Context context) {
        return addonDir(context).getAbsolutePath();
    }

    static boolean hasContent(Context context) {
        File frm = new File(abcDir(context), "FRM");
        File[] files = frm.listFiles();
        if (files == null || files.length == 0) {
            return false;
        }
        for (File f : files) {
            if (f.isFile() && f.getName().toLowerCase(java.util.Locale.ROOT).endsWith(".frm")) {
                return true;
            }
        }
        return false;
    }

    static int countFrm(Context context) {
        return countFilesWithSuffix(new File(abcDir(context), "FRM"), ".frm");
    }

    static int countFilesWithSuffix(File dir, String suffix) {
        if (dir == null || !dir.isDirectory()) {
            return 0;
        }
        File[] files = dir.listFiles();
        if (files == null) {
            return 0;
        }
        int count = 0;
        for (File f : files) {
            if (f.isFile() && f.getName().toLowerCase(java.util.Locale.ROOT).endsWith(suffix)) {
                count++;
            }
        }
        return count;
    }

    static long treeSize(File file) {
        if (file == null || !file.exists()) {
            return 0;
        }
        if (file.isFile()) {
            return file.length();
        }
        File[] children = file.listFiles();
        if (children == null) {
            return 0;
        }
        long size = 0;
        for (File child : children) {
            size += treeSize(child);
        }
        return size;
    }

    static void writeText(File file, String text) throws java.io.IOException {
        try (java.io.FileOutputStream out = new java.io.FileOutputStream(file)) {
            out.write(text.getBytes(java.nio.charset.StandardCharsets.UTF_8));
        }
    }

    static void saveImported(Context context, int frmCount, long bytes) {
        SharedPreferences prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        prefs.edit()
                .putString(KEY_ABC, dataRoot(context))
                .putString(KEY_ADDON, addonRoot(context))
                .putLong(KEY_IMPORTED_AT, System.currentTimeMillis())
                .putInt(KEY_FRM_COUNT, frmCount)
                .putLong(KEY_BYTES, bytes)
                .apply();
    }

    static String describe(Context context) {
        int frm = countFrm(context);
        long bytes = treeSize(abcDir(context)) + treeSize(addonDir(context));
        return "FRM: " + frm + " • " + String.format(java.util.Locale.ROOT, "%.1f MB", bytes / (1024.0 * 1024.0));
    }

    static void deleteRecursive(File file) {
        if (file == null || !file.exists()) {
            return;
        }
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children != null) {
                for (File child : children) {
                    deleteRecursive(child);
                }
            }
        }
        //noinspection ResultOfMethodCallIgnored
        file.delete();
    }

    /**
     * The small addon (IGLO artwork, its WAV, and the pre-rendered MIDI
     * OGGs) is bundled in the APK.  Copy any missing files into app storage;
     * never overwrite a user-imported addon.
     */
    static void ensureBundledAddon(Context context) {
        File dest = addonDir(context);
        if (!dest.exists() && !dest.mkdirs()) {
            return;
        }
        copyAssetTree(context, "addon", dest);
    }

    private static void copyAssetTree(Context context, String assetPath, File dest) {
        AssetManager assets = context.getAssets();
        try {
            String[] children = assets.list(assetPath);
            if (children == null || children.length == 0) {
                File file = dest;
                if (file.exists()) {
                    return;
                }
                try (InputStream in = assets.open(assetPath);
                     BufferedInputStream bin = new BufferedInputStream(in, 65536);
                     FileOutputStream fos = new FileOutputStream(file);
                     BufferedOutputStream bout = new BufferedOutputStream(fos, 65536)) {
                    byte[] buffer = new byte[65536];
                    int n;
                    while ((n = bin.read(buffer)) > 0) {
                        bout.write(buffer, 0, n);
                    }
                }
                return;
            }
            if (!dest.exists() && !dest.mkdirs()) {
                return;
            }
            for (String child : children) {
                copyAssetTree(context, assetPath + "/" + child, new File(dest, child));
            }
        } catch (IOException ignored) {
        }
    }

    static void removeContent(Context context) {
        deleteRecursive(abcDir(context));
        deleteRecursive(addonDir(context));
        deleteRecursive(stagingAbc(context));
        deleteRecursive(stagingAddon(context));
        deleteRecursive(backupAbc(context));
        deleteRecursive(backupAddon(context));
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().clear().apply();
    }
}
