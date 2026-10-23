package pl.szymor.abc;

import android.content.ContentValues;
import android.content.Context;
import android.content.Intent;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.MediaStore;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.Toast;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

/** SDL host activity with Android lifecycle, immersive mode and audio focus. */
public class AbcActivity extends SDLActivity {
    private AudioManager audioManager;
    private AudioFocusRequest focusRequest;
    private AudioManager.OnAudioFocusChangeListener focusListener;

    @Override
    protected String[] getLibraries() {
        return new String[]{"SDL3", "SDL3_image", "SDL3_mixer", "main"};
    }

    @Override
    protected String[] getArguments() {
        String addon = ContentStore.addonRoot(this);
        String start = getIntent().getStringExtra("start");
        if (start == null || start.isEmpty())
            start = "100";
        return new String[]{
                "--data", ContentStore.dataRoot(this),
                "--addon", addon,
                "--start", start
        };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (!ContentStore.hasContent(this)) {
            startActivity(new Intent(this, SetupActivity.class));
            finish();
            return;
        }
        ContentStore.ensureBundledAddon(this);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        audioManager = (AudioManager) getSystemService(Context.AUDIO_SERVICE);
        applyImmersiveMode();
    }

    /** Called from native code through platform_open_content_manager(). */
    public void openContentManagerFromNative() {
        runOnUiThread(() -> PlatformBridge.openContentManager(this));
    }

    /** Called from native code after kPrint/F12 exported a PNG. */
    public void saveImageToGalleryFromNative(String path) {
        runOnUiThread(() -> saveImageToGallery(path));
    }

    private void saveImageToGallery(String path) {
        File source = new File(path);
        if (!source.isFile()) {
            return;
        }
        if (Build.VERSION.SDK_INT >= 29) {
            ContentValues values = new ContentValues();
            values.put(MediaStore.Images.Media.DISPLAY_NAME, source.getName());
            values.put(MediaStore.Images.Media.MIME_TYPE, "image/png");
            values.put(MediaStore.Images.Media.RELATIVE_PATH,
                    Environment.DIRECTORY_PICTURES + "/Moje Pierwsze ABC");
            values.put(MediaStore.Images.Media.IS_PENDING, 1);
            Uri uri = getContentResolver().insert(
                    MediaStore.Images.Media.EXTERNAL_CONTENT_URI, values);
            if (uri == null) {
                Toast.makeText(this, "Nie udało się zapisać obrazu.", Toast.LENGTH_LONG).show();
                return;
            }
            try (InputStream in = new FileInputStream(source);
                 OutputStream out = getContentResolver().openOutputStream(uri)) {
                if (out == null) {
                    throw new java.io.IOException("no output stream");
                }
                byte[] buffer = new byte[65536];
                int n;
                while ((n = in.read(buffer)) > 0) {
                    out.write(buffer, 0, n);
                }
                values.clear();
                values.put(MediaStore.Images.Media.IS_PENDING, 0);
                getContentResolver().update(uri, values, null, null);
                Toast.makeText(this, "Zapisano obraz w Galerii.", Toast.LENGTH_LONG).show();
            } catch (Exception e) {
                getContentResolver().delete(uri, null, null);
                Toast.makeText(this, "Nie udało się zapisać obrazu.", Toast.LENGTH_LONG).show();
            }
        } else {
            File dir = new File(getExternalFilesDir(Environment.DIRECTORY_PICTURES), "Moje Pierwsze ABC");
            if (!dir.exists() && !dir.mkdirs()) {
                return;
            }
            File target = new File(dir, source.getName());
            try (InputStream in = new FileInputStream(source);
                 OutputStream out = new FileOutputStream(target)) {
                byte[] buffer = new byte[65536];
                int n;
                while ((n = in.read(buffer)) > 0) {
                    out.write(buffer, 0, n);
                }
                Toast.makeText(this, "Zapisano obraz: " + target.getAbsolutePath(),
                        Toast.LENGTH_LONG).show();
            } catch (Exception ignored) {
            }
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        requestAudioFocus();
        applyImmersiveMode();
    }

    @Override
    protected void onPause() {
        abandonAudioFocus();
        super.onPause();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            applyImmersiveMode();
        }
    }

    private void applyImmersiveMode() {
        if (Build.VERSION.SDK_INT >= 30) {
            getWindow().setDecorFitsSystemWindows(false);
            WindowInsetsController controller = getWindow().getInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.systemBars());
                controller.setSystemBarsBehavior(
                        WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            getWindow().getDecorView().setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_FULLSCREEN
                            | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                            | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                            | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                            | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
        }
    }

    private void requestAudioFocus() {
        if (audioManager == null) {
            return;
        }
        focusListener = focusChange -> {
            boolean has = focusChange == AudioManager.AUDIOFOCUS_GAIN;
            try {
                PlatformBridge.nativeAudioFocus(has);
            } catch (UnsatisfiedLinkError ignored) {
            }
        };
        if (Build.VERSION.SDK_INT >= 26) {
            AudioAttributes attributes = new AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_GAME)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                    .build();
            focusRequest = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
                    .setAudioAttributes(attributes)
                    .setOnAudioFocusChangeListener(focusListener)
                    .build();
            audioManager.requestAudioFocus(focusRequest);
        } else {
            audioManager.requestAudioFocus(focusListener, AudioManager.STREAM_MUSIC,
                    AudioManager.AUDIOFOCUS_GAIN);
        }
    }

    private void abandonAudioFocus() {
        if (audioManager == null) {
            return;
        }
        if (Build.VERSION.SDK_INT >= 26 && focusRequest != null) {
            audioManager.abandonAudioFocusRequest(focusRequest);
        } else if (focusListener != null) {
            audioManager.abandonAudioFocus(focusListener);
        }
        focusRequest = null;
        focusListener = null;
    }
}
