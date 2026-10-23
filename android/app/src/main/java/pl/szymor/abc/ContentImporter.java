package pl.szymor.abc;

import android.content.ContentResolver;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;

import org.json.JSONObject;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Copies a user-selected Storage Access Framework tree into app-private
 * storage.  The native engine and FFmpeg need ordinary filesystem paths, so
 * the app deliberately does not try to read game data directly from a SAF
 * content:// tree.
 */
final class ContentImporter {
    private static final String[] ABC_TREES = {"FRM", "WAV", "AVI", "MIDI"};
    private static final String[] ADDON_TREES = {"AVI", "OVR", "WAV", "MIDI"};

    private ContentImporter() {}

    interface Listener {
        void progress(long doneBytes, long totalBytes, int doneFiles, String currentName);
    }

    static final class Root {
        final Uri treeUri;
        final String docId;
        final String name;

        Root(Uri treeUri, String docId, String name) {
            this.treeUri = treeUri;
            this.docId = docId;
            this.name = name;
        }
    }

    static final class Plan {
        Root abc;
        Root addon;
        int frmCount;
        int wavCount;
        int aviCount;
        int midiCount;
        int addonFileCount;
        long bytes;
        String error;

        boolean hasAbc() {
            return abc != null;
        }

        boolean hasAddon() {
            return addon != null;
        }
    }

    static final class Result {
        final boolean ok;
        final String message;
        final int frmCount;
        final long bytes;

        Result(boolean ok, String message, int frmCount, long bytes) {
            this.ok = ok;
            this.message = message;
            this.frmCount = frmCount;
            this.bytes = bytes;
        }
    }

    private static final class Progress {
        final long totalBytes;
        final int totalFiles;
        long doneBytes;
        int doneFiles;
        long lastUpdate;

        Progress(long totalBytes, int totalFiles) {
            this.totalBytes = totalBytes;
            this.totalFiles = totalFiles;
        }
    }

    static Plan inspect(Context context, Uri treeUri) {
        Plan plan = new Plan();
        if (treeUri == null) {
            plan.error = "Nie wybrano folderu.";
            return plan;
        }
        try {
            String treeDocId = DocumentsContract.getTreeDocumentId(treeUri);
            String rootName = displayName(context, treeUri, treeDocId);

            String frmId = findChild(context, treeUri, treeDocId, "FRM");
            if (frmId != null) {
                plan.abc = new Root(treeUri, treeDocId, rootName);
            } else {
                String abcId = findChild(context, treeUri, treeDocId, "abc");
                if (abcId != null && findChild(context, treeUri, abcId, "FRM") != null) {
                    plan.abc = new Root(treeUri, abcId, displayName(context, treeUri, abcId));
                }
            }

            String addonId = findChild(context, treeUri, treeDocId, "addon");
            if (addonId != null) {
                plan.addon = new Root(treeUri, addonId, displayName(context, treeUri, addonId));
            } else if (plan.abc == null && looksLikeAddon(context, treeUri, treeDocId)) {
                plan.addon = new Root(treeUri, treeDocId, rootName);
            }

            if (plan.abc != null) {
                Stats frm = statsForChild(context, treeUri, plan.abc.docId, "FRM");
                Stats wav = statsForChild(context, treeUri, plan.abc.docId, "WAV");
                Stats avi = statsForChild(context, treeUri, plan.abc.docId, "AVI");
                Stats midi = statsForChild(context, treeUri, plan.abc.docId, "MIDI");
                plan.frmCount = frm.files;
                plan.wavCount = wav.files;
                plan.aviCount = avi.files;
                plan.midiCount = midi.files;
                plan.bytes += frm.bytes + wav.bytes + avi.bytes + midi.bytes;
            }
            if (plan.addon != null) {
                Stats addon = statsForTree(context, treeUri, plan.addon.docId);
                plan.addonFileCount = addon.files;
                plan.bytes += addon.bytes;
            }

            if (plan.abc == null && plan.addon == null) {
                plan.error = "W tym folderze nie ma danych gry (FRM/WAV/AVI/MIDI) ani dodatków.";
            } else if (plan.abc != null && plan.frmCount == 0) {
                plan.error = "Folder FRM jest pusty.";
            }
            return plan;
        } catch (Exception e) {
            plan.error = "Nie można odczytać folderu: " + e.getMessage();
            return plan;
        }
    }

    static Result importPlan(Context context, Plan plan, Listener listener, AtomicBoolean cancel) {
        if (plan == null || (plan.abc == null && plan.addon == null)) {
            return new Result(false, "Nie wybrano danych do importu.", 0, 0);
        }
        File stagingAbc = ContentStore.stagingAbc(context);
        File stagingAddon = ContentStore.stagingAddon(context);
        ContentStore.deleteRecursive(stagingAbc);
        ContentStore.deleteRecursive(stagingAddon);

        Progress progress = new Progress(plan.bytes, plan.frmCount + plan.wavCount + plan.aviCount
                + plan.midiCount + plan.addonFileCount);
        try {
            if (plan.abc != null) {
                if (!stagingAbc.mkdirs() && !stagingAbc.isDirectory()) {
                    throw new IOException("Nie można utworzyć katalogu tymczasowego.");
                }
                for (String name : ABC_TREES) {
                    String childId = findChild(context, plan.abc.treeUri, plan.abc.docId, name);
                    if (childId != null) {
                        copyDocument(context, plan.abc.treeUri, childId, new File(stagingAbc, name),
                                progress, listener, cancel);
                    }
                }
                int frm = ContentStore.countFilesWithSuffix(new File(stagingAbc, "FRM"), ".frm");
                if (frm == 0) {
                    throw new IOException("Skopiowany folder FRM jest pusty.");
                }
                if (frm < 294) {
                    // The port expects 294 scenes, but a partial/modified set can
                    // still be useful.  Keep it and let the UI warn about it.
                    // The warning is included in the final message.
                }
                writeMarker(stagingAbc, plan, frm);
            }

            if (plan.addon != null) {
                if (!stagingAddon.mkdirs() && !stagingAddon.isDirectory()) {
                    throw new IOException("Nie można utworzyć katalogu dodatków.");
                }
                List<Child> children = listChildren(context, plan.addon.treeUri, plan.addon.docId);
                for (Child child : children) {
                    copyDocument(context, plan.addon.treeUri, child.docId,
                            new File(stagingAddon, safeName(child.name)),
                            progress, listener, cancel);
                }
            }

            if (cancelled(cancel)) {
                throw new IOException("Import anulowany.");
            }

            File finalAbc = ContentStore.abcDir(context);
            File finalAddon = ContentStore.addonDir(context);
            if (plan.abc != null) {
                replaceDirectory(stagingAbc, finalAbc, ContentStore.backupAbc(context));
            }
            if (plan.addon != null) {
                replaceDirectory(stagingAddon, finalAddon, ContentStore.backupAddon(context));
            }

            int frm = ContentStore.countFrm(context);
            long bytes = ContentStore.treeSize(finalAbc) + ContentStore.treeSize(finalAddon);
            ContentStore.saveImported(context, frm, bytes);
            String message = plan.addon != null && plan.abc == null
                    ? "Zaimportowano dodatki."
                    : "Zaimportowano dane gry (" + frm + " scen).";
            if (plan.abc != null && frm != 294) {
                message += " Uwaga: oczekiwano 294 scen, znaleziono " + frm + ".";
            }
            return new Result(true, message, frm, bytes);
        } catch (Exception e) {
            ContentStore.deleteRecursive(stagingAbc);
            ContentStore.deleteRecursive(stagingAddon);
            String msg = e.getMessage();
            if (msg == null || msg.isEmpty()) {
                msg = e.getClass().getSimpleName();
            }
            return new Result(false, msg, 0, 0);
        }
    }

    private static boolean cancelled(AtomicBoolean cancel) {
        return cancel != null && cancel.get();
    }

    private static void writeMarker(File root, Plan plan, int frm) throws Exception {
        JSONObject json = new JSONObject();
        json.put("source", plan.abc != null ? plan.abc.name : "");
        json.put("imported_at", System.currentTimeMillis());
        json.put("frm_count", frm);
        json.put("bytes", plan.bytes);
        ContentStore.writeText(new File(root, ".content.json"), json.toString());
    }

    private static void replaceDirectory(File staging, File target, File backup) throws IOException {
        if (!staging.isDirectory()) {
            return;
        }
        ContentStore.deleteRecursive(backup);
        if (target.exists()) {
            if (!target.renameTo(backup)) {
                throw new IOException("Nie można odłożyć poprzednich danych.");
            }
        }
        if (!staging.renameTo(target)) {
            if (backup.exists()) {
                //noinspection ResultOfMethodCallIgnored
                backup.renameTo(target);
            }
            throw new IOException("Nie można zainstalować nowych danych.");
        }
        ContentStore.deleteRecursive(backup);
    }

    private static void copyDocument(Context context, Uri treeUri, String docId, File out,
                                     Progress progress, Listener listener, AtomicBoolean cancel)
            throws IOException {
        if (cancelled(cancel)) {
            throw new IOException("Import anulowany.");
        }
        if (isDirectory(context, treeUri, docId)) {
            if (!out.mkdirs() && !out.isDirectory()) {
                throw new IOException("Nie można utworzyć katalogu " + out.getName());
            }
            List<Child> children = listChildren(context, treeUri, docId);
            for (Child child : children) {
                if (child.name.startsWith(".")) {
                    continue;
                }
                copyDocument(context, treeUri, child.docId, new File(out, safeName(child.name)),
                        progress, listener, cancel);
            }
            return;
        }

        if (!out.getParentFile().exists() && !out.getParentFile().mkdirs()) {
            throw new IOException("Nie można utworzyć katalogu " + out.getParentFile().getName());
        }
        Uri docUri = DocumentsContract.buildDocumentUriUsingTree(treeUri, docId);
        ContentResolver resolver = context.getContentResolver();
        try (InputStream in = resolver.openInputStream(docUri);
             BufferedInputStream bin = in != null ? new BufferedInputStream(in, 131072) : null;
             FileOutputStream fos = new FileOutputStream(out);
             BufferedOutputStream bout = new BufferedOutputStream(fos, 131072)) {
            if (bin == null) {
                throw new IOException("Nie można otworzyć " + out.getName());
            }
            byte[] buffer = new byte[131072];
            int n;
            while ((n = bin.read(buffer)) > 0) {
                if (cancelled(cancel)) {
                    throw new IOException("Import anulowany.");
                }
                bout.write(buffer, 0, n);
                progress.doneBytes += n;
                notifyProgress(progress, listener, out.getName(), false);
            }
            progress.doneFiles++;
            notifyProgress(progress, listener, out.getName(), true);
        }
    }

    private static void notifyProgress(Progress p, Listener listener, String name, boolean force) {
        if (listener == null) {
            return;
        }
        long now = System.currentTimeMillis();
        if (!force && now - p.lastUpdate < 80) {
            return;
        }
        p.lastUpdate = now;
        listener.progress(p.doneBytes, p.totalBytes, p.doneFiles, name);
    }

    private static final class Stats {
        int files;
        long bytes;
    }

    private static final class Child {
        final String docId;
        final String name;
        final boolean directory;
        final long size;

        Child(String docId, String name, boolean directory, long size) {
            this.docId = docId;
            this.name = name;
            this.directory = directory;
            this.size = size;
        }
    }

    private static List<Child> listChildren(Context context, Uri treeUri, String parentDocId) {
        List<Child> result = new ArrayList<>();
        Uri childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, parentDocId);
        String[] projection = {
                DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                DocumentsContract.Document.COLUMN_MIME_TYPE,
                DocumentsContract.Document.COLUMN_SIZE
        };
        try (Cursor cursor = context.getContentResolver().query(childrenUri, projection,
                null, null, null)) {
            if (cursor == null) {
                return result;
            }
            while (cursor.moveToNext()) {
                String id = cursor.getString(0);
                String name = cursor.getString(1);
                String mime = cursor.getString(2);
                long size = cursor.isNull(3) ? 0 : cursor.getLong(3);
                if (id != null && name != null) {
                    result.add(new Child(id, name,
                            DocumentsContract.Document.MIME_TYPE_DIR.equals(mime), size));
                }
            }
        } catch (Exception ignored) {
        }
        return result;
    }

    private static String findChild(Context context, Uri treeUri, String parentDocId, String wanted) {
        if (parentDocId == null || wanted == null) {
            return null;
        }
        for (Child child : listChildren(context, treeUri, parentDocId)) {
            if (child.name.equalsIgnoreCase(wanted)) {
                return child.docId;
            }
        }
        return null;
    }

    private static String displayName(Context context, Uri treeUri, String docId) {
        if (docId == null) {
            return "";
        }
        Uri uri = DocumentsContract.buildDocumentUriUsingTree(treeUri, docId);
        try (Cursor cursor = context.getContentResolver().query(uri,
                new String[]{DocumentsContract.Document.COLUMN_DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                String name = cursor.getString(0);
                return name != null ? name : "";
            }
        } catch (Exception ignored) {
        }
        return "";
    }

    private static boolean isDirectory(Context context, Uri treeUri, String docId) {
        Uri uri = DocumentsContract.buildDocumentUriUsingTree(treeUri, docId);
        try (Cursor cursor = context.getContentResolver().query(uri,
                new String[]{DocumentsContract.Document.COLUMN_MIME_TYPE}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                return DocumentsContract.Document.MIME_TYPE_DIR.equals(cursor.getString(0));
            }
        } catch (Exception ignored) {
        }
        return false;
    }

    private static boolean looksLikeAddon(Context context, Uri treeUri, String docId) {
        if (findChild(context, treeUri, docId, "FRM") != null) {
            return false;
        }
        return findChild(context, treeUri, docId, "OVR") != null
                || findChild(context, treeUri, docId, "MIDI") != null
                || (findChild(context, treeUri, docId, "WAV") != null
                    && findChild(context, treeUri, docId, "AVI") != null);
    }

    private static Stats statsForChild(Context context, Uri treeUri, String rootDocId,
                                        String subdir) {
        String id = findChild(context, treeUri, rootDocId, subdir);
        return id == null ? new Stats() : statsForTree(context, treeUri, id);
    }

    private static Stats statsForTree(Context context, Uri treeUri, String docId) {
        Stats stats = new Stats();
        if (!isDirectory(context, treeUri, docId)) {
            stats.files = 1;
            stats.bytes = fileSize(context, treeUri, docId);
            return stats;
        }
        for (Child child : listChildren(context, treeUri, docId)) {
            if (child.directory) {
                Stats sub = statsForTree(context, treeUri, child.docId);
                stats.files += sub.files;
                stats.bytes += sub.bytes;
            } else {
                stats.files++;
                stats.bytes += child.size;
            }
        }
        return stats;
    }

    private static long fileSize(Context context, Uri treeUri, String docId) {
        Uri uri = DocumentsContract.buildDocumentUriUsingTree(treeUri, docId);
        try (Cursor cursor = context.getContentResolver().query(uri,
                new String[]{DocumentsContract.Document.COLUMN_SIZE}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst() && !cursor.isNull(0)) {
                return cursor.getLong(0);
            }
        } catch (Exception ignored) {
        }
        return 0;
    }

    private static String safeName(String name) {
        String cleaned = name.replace('/', '_').replace('\\', '_').replace("..", "_");
        return cleaned.isEmpty() ? "file" : cleaned;
    }
}
