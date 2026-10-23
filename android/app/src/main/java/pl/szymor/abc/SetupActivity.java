package pl.szymor.abc;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Bundle;
import android.os.StatFs;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * First-run / content management screen.  The APK deliberately contains no
 * game data, so the user selects the folder extracted from their own CD and
 * the app copies it into private storage.
 */
public class SetupActivity extends Activity {
    private static final int REQ_DATA = 1001;
    private static final int REQ_ADDON = 1002;

    private final ExecutorService executor = Executors.newSingleThreadExecutor();
    private volatile AtomicBoolean cancel = new AtomicBoolean(false);

    private boolean manage;
    private ContentImporter.Plan plan;
    private TextView summary;
    private TextView status;
    private ProgressBar progress;
    private Button pickData;
    private Button pickAddon;
    private Button importButton;
    private Button playButton;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        manage = getIntent().getBooleanExtra("manage", false);
        if (savedInstanceState == null) {
            // Leftovers from an interrupted import.  An in-progress import is
            // cancelled in onDestroy, so this is safe on a fresh activity.
            ContentStore.deleteRecursive(ContentStore.stagingAbc(this));
            ContentStore.deleteRecursive(ContentStore.stagingAddon(this));
            ContentStore.deleteRecursive(ContentStore.backupAbc(this));
            ContentStore.deleteRecursive(ContentStore.backupAddon(this));
        }

        if (!manage && ContentStore.hasContent(this)) {
            startGame();
            return;
        }

        setContentView(buildUi());
        refreshUi();
    }

    private View buildUi() {
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setBackgroundColor(Color.rgb(255, 248, 225));

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(24), dp(24), dp(24), dp(24));
        scroll.addView(root, new ScrollView.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        TextView title = new TextView(this);
        title.setText(R.string.setup_title);
        title.setTextColor(Color.rgb(198, 40, 40));
        title.setTextSize(30);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        root.addView(title, matchWrap());

        TextView intro = new TextView(this);
        intro.setText(ContentStore.hasContent(this)
                ? R.string.setup_manage_intro : R.string.setup_intro);
        intro.setTextColor(Color.rgb(40, 40, 40));
        intro.setTextSize(16);
        intro.setPadding(0, dp(12), 0, dp(12));
        root.addView(intro, matchWrap());

        summary = new TextView(this);
        summary.setTextSize(15);
        summary.setTextColor(Color.rgb(60, 60, 60));
        summary.setPadding(0, dp(8), 0, dp(8));
        root.addView(summary, matchWrap());

        status = new TextView(this);
        status.setTextSize(15);
        status.setTextColor(Color.rgb(60, 60, 60));
        status.setPadding(0, dp(4), 0, dp(8));
        root.addView(status, matchWrap());

        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progress.setMax(1000);
        progress.setVisibility(View.GONE);
        root.addView(progress, matchWrap());

        playButton = button(R.string.setup_play, v -> startGame());
        root.addView(playButton, buttonParams());

        pickData = button(R.string.setup_pick_data, v -> pickFolder(REQ_DATA));
        root.addView(pickData, buttonParams());

        pickAddon = button(R.string.setup_pick_addon, v -> pickFolder(REQ_ADDON));
        root.addView(pickAddon, buttonParams());

        importButton = button(R.string.setup_import, v -> startImport());
        root.addView(importButton, buttonParams());

        Button remove = button(R.string.setup_remove, v -> confirmRemove());
        root.addView(remove, buttonParams());

        TextView helpTitle = new TextView(this);
        helpTitle.setText(R.string.setup_help_title);
        helpTitle.setTextSize(17);
        helpTitle.setTypeface(Typeface.DEFAULT_BOLD);
        helpTitle.setPadding(0, dp(20), 0, dp(6));
        root.addView(helpTitle, matchWrap());

        TextView help = new TextView(this);
        help.setText(R.string.setup_help_text);
        help.setTextSize(15);
        help.setTextColor(Color.rgb(70, 70, 70));
        root.addView(help, matchWrap());

        return scroll;
    }

    private Button button(int text, View.OnClickListener listener) {
        Button b = new Button(this);
        b.setText(text);
        b.setAllCaps(false);
        b.setTextSize(16);
        b.setOnClickListener(listener);
        return b;
    }

    private LinearLayout.LayoutParams matchWrap() {
        return new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
    }

    private LinearLayout.LayoutParams buttonParams() {
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(52));
        p.topMargin = dp(10);
        return p;
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private void pickFolder(int requestCode) {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION
                | Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);
        try {
            startActivityForResult(intent, requestCode);
        } catch (Exception e) {
            toast("Brak wyboru folderów na tym urządzeniu.");
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (resultCode != RESULT_OK || data == null || data.getData() == null) {
            return;
        }
        Uri uri = data.getData();
        try {
            getContentResolver().takePersistableUriPermission(uri,
                    Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (Exception ignored) {
        }

        ContentImporter.Plan inspected = ContentImporter.inspect(this, uri);
        if (requestCode == REQ_DATA) {
            if (!inspected.hasAbc() && !inspected.hasAddon()) {
                toast(inspected.error != null ? inspected.error : "Nieprawidłowy folder.");
                return;
            }
            if (!inspected.hasAbc()) {
                toast("To wygląda na folder dodatków, nie danych gry.");
                return;
            }
            plan = inspected;
        } else if (requestCode == REQ_ADDON) {
            if (!inspected.hasAddon()) {
                toast("W tym folderze nie ma dodatków (OVR/WAV/AVI/MIDI).");
                return;
            }
            if (plan == null) {
                plan = new ContentImporter.Plan();
            }
            plan.addon = inspected.addon;
            plan.addonFileCount = inspected.addonFileCount;
            plan.bytes += inspected.bytes;
        }
        refreshUi();
    }

    private void refreshUi() {
        boolean has = ContentStore.hasContent(this);
        if (has) {
            summary.setText(ContentStore.describe(this));
        } else if (plan != null) {
            StringBuilder sb = new StringBuilder();
            if (plan.hasAbc()) {
                sb.append("Dane gry: ").append(plan.frmCount).append(" scen FRM");
                sb.append(" • ").append(plan.wavCount).append(" WAV");
                sb.append(" • ").append(plan.aviCount).append(" AVI");
                sb.append(" • ").append(plan.midiCount).append(" MIDI");
            }
            if (plan.hasAddon()) {
                if (sb.length() > 0) {
                    sb.append('\n');
                }
                sb.append("Dodatki: ").append(plan.addonFileCount).append(" plików");
            }
            if (plan.bytes > 0) {
                sb.append("\nRozmiar do skopiowania: ")
                        .append(String.format(Locale.ROOT, "%.1f MB", plan.bytes / (1024.0 * 1024.0)));
            }
            if (plan.error != null) {
                sb.append('\n').append(plan.error);
            }
            summary.setText(sb.toString());
        } else {
            summary.setText(R.string.setup_no_selection);
        }

        importButton.setEnabled(plan != null && (plan.hasAbc() || plan.hasAddon()));
        playButton.setEnabled(has);
        pickData.setEnabled(true);
        pickAddon.setEnabled(true);
        importButton.setVisibility(has && plan == null ? View.GONE : View.VISIBLE);
        playButton.setVisibility(has ? View.VISIBLE : View.GONE);
        progress.setVisibility(View.GONE);
    }

    private void startImport() {
        if (plan == null || (!plan.hasAbc() && !plan.hasAddon())) {
            toast(getString(R.string.setup_no_selection));
            return;
        }

        long existing = ContentStore.treeSize(ContentStore.abcDir(this))
                + ContentStore.treeSize(ContentStore.addonDir(this));
        long incoming = plan.bytes > 0 ? plan.bytes : 650L * 1024 * 1024;
        long required = incoming + existing + 100L * 1024 * 1024;
        try {
            StatFs stat = new StatFs(ContentStore.baseDir(this).getAbsolutePath());
            long available = stat.getAvailableBytes();
            if (available < required) {
                toast(getString(R.string.setup_space_warning));
                return;
            }
        } catch (Exception ignored) {
        }

        final AtomicBoolean cancelFlag = new AtomicBoolean(false);
        cancel = cancelFlag;
        setBusy(true);
        status.setText(R.string.setup_importing);
        progress.setVisibility(View.VISIBLE);
        progress.setProgress(0);

        executor.execute(() -> {
            ContentImporter.Result result = ContentImporter.importPlan(this, plan,
                    (done, total, files, name) -> runOnUiThread(() -> {
                        if (total > 0) {
                            progress.setProgress((int) Math.min(1000, done * 1000 / total));
                        }
                        status.setText(getString(R.string.setup_importing) + " " + name);
                    }), cancelFlag);
            runOnUiThread(() -> {
                setBusy(false);
                progress.setVisibility(View.GONE);
                if (result.ok) {
                    status.setText(result.message);
                    plan = null;
                    refreshUi();
                    toast(result.message);
                } else {
                    status.setText(getString(R.string.setup_import_failed) + " " + result.message);
                    toast(getString(R.string.setup_import_failed));
                }
            });
        });
    }

    private void setBusy(boolean busy) {
        pickData.setEnabled(!busy);
        pickAddon.setEnabled(!busy);
        importButton.setEnabled(!busy);
        playButton.setEnabled(!busy && ContentStore.hasContent(this));
        if (busy) {
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        } else {
            getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        }
    }

    private void confirmRemove() {
        new AlertDialog.Builder(this)
                .setTitle(R.string.setup_remove)
                .setMessage("Usunąć zaimportowane dane gry z tego urządzenia? Będzie można zaimportować je ponownie.")
                .setNegativeButton(R.string.setup_cancel, null)
                .setPositiveButton(R.string.setup_remove, (d, w) -> {
                    ContentStore.removeContent(this);
                    plan = null;
                    status.setText("");
                    refreshUi();
                })
                .show();
    }

    private void startGame() {
        ContentStore.ensureBundledAddon(this);
        if (!ContentStore.hasContent(this)) {
            toast(getString(R.string.setup_no_selection));
            return;
        }
        Intent intent = new Intent(this, AbcActivity.class);
        String start = getIntent().getStringExtra("start");
        if (start != null && !start.isEmpty())
            intent.putExtra("start", start);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
        startActivity(intent);
        finish();
    }

    private void toast(String text) {
        Toast.makeText(this, text, Toast.LENGTH_LONG).show();
    }

    @Override
    protected void onDestroy() {
        cancel.set(true);
        executor.shutdownNow();
        super.onDestroy();
    }
}
