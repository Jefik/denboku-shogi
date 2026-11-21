package jp.denboku;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Bundle;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.graphics.Rect;
import android.view.ActionMode;
import android.view.Menu;
import android.view.MenuItem;
import android.widget.FrameLayout;

public class MainActivity extends Activity implements Game.Listener {
    private Screen screen;
    private final Sounds sounds = new Sounds();
    private WifiManager.MulticastLock multicast;

    private boolean notch; /* the game's view also beside the camera */
    private ActionMode textMode; /* Android's text toolbar over a field of the game */

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        screen = new Screen(this);
        /* The game's view keeps out of a notch or a punch hole (the safe insets of the display
         * cutout), black around it: Android 15 lays apps out under the cutout whatever they ask.
         * With the game's "Use the space beside the camera" it goes under it too. */
        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(0xff000000);
        root.addView(screen, new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
        setContentView(root);
        if (Build.VERSION.SDK_INT >= 28)
            getWindow().getAttributes().layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);
        root.setOnApplyWindowInsetsListener((v, insets) -> {
            int l = 0, t = 0, r = 0, b = 0;
            if (Build.VERSION.SDK_INT >= 28 && insets.getDisplayCutout() != null && !notch) {
                android.view.DisplayCutout c = insets.getDisplayCutout();
                l = c.getSafeInsetLeft();
                t = c.getSafeInsetTop();
                r = c.getSafeInsetRight();
                b = c.getSafeInsetBottom();
            }
            FrameLayout.LayoutParams p = (FrameLayout.LayoutParams) screen.getLayoutParams();
            if (p.leftMargin != l || p.topMargin != t || p.rightMargin != r || p.bottomMargin != b) {
                p.setMargins(l, t, r, b);
                screen.setLayoutParams(p);
            }
            /* Android 11+ does not resize a full-screen view for the keyboard: the game's view is cut instead. */
            if (Build.VERSION.SDK_INT >= 30)
                screen.setKeyboardHeight(Math.max(0, insets.getInsets(WindowInsets.Type.ime()).bottom - b));
            return insets;
        });
        fullScreen();
        /* LAN rooms are found by broadcast, which Wi-Fi drops without this lock. */
        WifiManager wifi = (WifiManager) getApplicationContext().getSystemService(Context.WIFI_SERVICE);
        multicast = wifi.createMulticastLock("denboku");
        multicast.acquire();
        Pad.watch(this);
        Game.setListener(this);
        installed(getIntent()); /* the installer's answer, if the activity was made again for it */
        if (Game.running())
            return;
        DisplayMetrics m = new DisplayMetrics();
        getWindowManager().getDefaultDisplay().getRealMetrics(m);
        float density = m.density;
        new Thread(() -> {
            try {
                Game.start(this, m.widthPixels, m.heightPixels, density);
            } catch (Exception e) {
                Log.e("denboku", "start", e);
                exited();
            }
        }).start();
    }

    @Override protected void onDestroy() {
        multicast.release();
        sounds.release();
        super.onDestroy();
    }

    /* Controllers first, whichever view has the focus. */
    @Override public boolean dispatchKeyEvent(KeyEvent e) { return Pad.key(e) || super.dispatchKeyEvent(e); }

    @Override public boolean dispatchGenericMotionEvent(MotionEvent e) {
        return Pad.motion(e) || super.dispatchGenericMotionEvent(e);
    }

    @Override public void onWindowFocusChanged(boolean focus) {
        super.onWindowFocusChanged(focus);
        if (focus)
            fullScreen();
    }

    /* The game fills the screen, as on a computer; a swipe from the edge shows the bars. */
    private void fullScreen() {
        if (Build.VERSION.SDK_INT >= 30) {
            WindowInsetsController c = getWindow().getInsetsController();
            c.hide(WindowInsets.Type.systemBars());
            c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
        } else
            getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY |
                View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
    }

    /* The view may be smaller than the screen (a notch, the keyboard). */
    @Override public void started() {
        runOnUiThread(screen::sendView);
    }

    @Override public void frame(int x, int y, int w, int h) { screen.frame(x, y, w, h); }

    @Override public void message(String line) {
        if (line.startsWith("snd ")) { /* at once, not after the screen's work on the UI thread */
            sounds.play(line.substring(4));
            return;
        }
        runOnUiThread(() -> {
            if (line.equals("kb 1") || line.equals("kb 0"))
                screen.keyboard(line.equals("kb 1"));
            else if (line.equals("kbreset"))
                screen.resetInput();
            else if (line.equals("back"))
                moveTaskToBack(true);
            else if (line.startsWith("url "))
                startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(line.substring(4))));
            else if (line.startsWith("export "))
                export(line.substring(7));
            else if (line.startsWith("install "))
                install(line.substring(8));
            else if (line.equals("allowinstall"))
                allowInstall();
            else if (line.startsWith("vib "))
                vibrate(line.substring(4));
            else if (line.equals("paste"))
                paste();
            else if (line.startsWith("textmenu "))
                textMenu(line.substring(9));
            else if (line.startsWith("copy "))
                copy(line.substring(5));
            else if (line.startsWith("rumble "))
                Pad.rumble(line.substring(7));
            else if (line.startsWith("online "))
                online(line.equals("online 1"));
            else if (line.startsWith("notch ")) {
                notch = line.equals("notch 1");
                getWindow().getDecorView().requestApplyInsets();
            }
        });
    }

    /* "X Y W H TEXT SELECTED": Android's own floating text toolbar over a field of the game (a long press
     * on it), with the system's choices as a text field has them. */
    private void textMenu(String args) {
        String[] a = args.split(" ");
        if (a.length < 6 || Build.VERSION.SDK_INT < 23)
            return;
        int x = Integer.parseInt(a[0]), y = Integer.parseInt(a[1]), w = Integer.parseInt(a[2]), h = Integer.parseInt(a[3]);
        boolean text = a[4].equals("1"), selected = a[5].equals("1");
        if (textMode != null)
            textMode.finish(); /* one toolbar: the new one has the choices for now */
        textMode = screen.startActionMode(new ActionMode.Callback2() {
            @Override public boolean onCreateActionMode(ActionMode mode, Menu menu) {
                if (selected) {
                    menu.add(Menu.NONE, android.R.id.cut, 0, android.R.string.cut);
                    menu.add(Menu.NONE, android.R.id.copy, 1, android.R.string.copy);
                }
                menu.add(Menu.NONE, android.R.id.paste, 2, android.R.string.paste);
                if (text)
                    menu.add(Menu.NONE, android.R.id.selectAll, 3, android.R.string.selectAll);
                return true;
            }
            @Override public boolean onPrepareActionMode(ActionMode mode, Menu menu) { return false; }
            @Override public boolean onActionItemClicked(ActionMode mode, MenuItem item) {
                int id = item.getItemId();
                if (id == android.R.id.paste)
                    paste();
                else
                    Game.send(id == android.R.id.cut ? "tm cut" : id == android.R.id.copy ? "tm copy" : "tm selectall");
                mode.finish();
                return true;
            }
            @Override public void onDestroyActionMode(ActionMode mode) {
                if (textMode == mode)
                    textMode = null;
            }
            @Override public void onGetContentRect(ActionMode mode, View view, Rect out) { out.set(x, y, x + w, y + h); }
        }, ActionMode.TYPE_FLOATING);
    }

    /* A saved game (a file in the app's private folder) copied to Documents/Denboku, which other apps and the
     * file manager see; from Android 10 on, MediaStore does it with no permission. */
    private void export(String path) {
        if (Build.VERSION.SDK_INT < 29)
            return;
        java.io.File f = new java.io.File(path);
        try (java.io.InputStream in = new java.io.FileInputStream(f)) {
            android.content.ContentValues v = new android.content.ContentValues();
            v.put(android.provider.MediaStore.MediaColumns.DISPLAY_NAME, f.getName());
            v.put(android.provider.MediaStore.MediaColumns.MIME_TYPE, "application/octet-stream");
            v.put(android.provider.MediaStore.MediaColumns.RELATIVE_PATH, "Documents/Denboku");
            Uri uri = getContentResolver().insert(android.provider.MediaStore.Files.getContentUri("external"), v);
            if (uri == null)
                return;
            try (java.io.OutputStream out = getContentResolver().openOutputStream(uri)) {
                byte[] buf = new byte[8192];
                for (int n; (n = in.read(buf)) > 0; )
                    out.write(buf, 0, n);
            }
        } catch (Exception e) {
            Log.w("denboku", "export " + path, e);
        }
    }

    /* An update the game downloaded and checked (src/core/updater.c), handed to the system's installer, which
     * asks the player (and to allow installs from this app, if allowInstall() did not get it); its answer comes back in
     * onNewIntent(). */
    private static final String INSTALLED = "jp.denboku.INSTALLED";

    private void install(String path) {
        if (allowing) { /* after the player's answer on the page of allowInstall() */
            waiting = path;
            return;
        }
        new Thread(() -> {
            android.content.pm.PackageInstaller installer = getPackageManager().getPackageInstaller();
            try {
                int id = installer.createSession(new android.content.pm.PackageInstaller.SessionParams(
                    android.content.pm.PackageInstaller.SessionParams.MODE_FULL_INSTALL));
                try (android.content.pm.PackageInstaller.Session s = installer.openSession(id)) {
                    java.io.File f = new java.io.File(path);
                    try (java.io.InputStream in = new java.io.FileInputStream(f); java.io.OutputStream out = s.openWrite("denboku.apk", 0, f.length())) {
                        byte[] buf = new byte[65536];
                        for (int n; (n = in.read(buf)) > 0; )
                            out.write(buf, 0, n);
                        s.fsync(out);
                    }
                    Intent back = new Intent(this, MainActivity.class).setAction(INSTALLED);
                    int flags = android.app.PendingIntent.FLAG_UPDATE_CURRENT | (Build.VERSION.SDK_INT >= 31 ? android.app.PendingIntent.FLAG_MUTABLE : 0);
                    s.commit(android.app.PendingIntent.getActivity(this, 0, back, flags).getIntentSender());
                }
            } catch (Exception e) {
                Log.w("denboku", "install " + path, e);
                Game.send("installfail");
            }
        }).start();
    }

    /* Updates turned on: the system's page to allow installs from this app, if not allowed yet (before
     * Android 8 the installer asks for it itself). */
    private boolean allowing;
    private String waiting; /* an update downloaded while that page was open */

    private void allowInstall() {
        if (Build.VERSION.SDK_INT < 26 || getPackageManager().canRequestPackageInstalls())
            return;
        allowing = true;
        startActivity(new Intent(android.provider.Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:" + getPackageName())));
    }

    @Override protected void onResume() {
        super.onResume();
        if (!allowing)
            return;
        allowing = false;
        if (waiting != null)
            install(waiting);
        waiting = null;
    }

    @Override protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        installed(intent);
    }

    /* The system installer's answer: its question for the player, or a failure (the game says so). */
    private void installed(Intent intent) {
        if (intent == null || !INSTALLED.equals(intent.getAction()))
            return;
        int status = intent.getIntExtra(android.content.pm.PackageInstaller.EXTRA_STATUS, -999);
        intent.setAction(null); /* answered once, not again when the activity is made again */
        if (status == android.content.pm.PackageInstaller.STATUS_PENDING_USER_ACTION) {
            Intent ask = intent.getParcelableExtra(Intent.EXTRA_INTENT);
            if (ask != null)
                startActivity(ask.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
        } else if (status != android.content.pm.PackageInstaller.STATUS_SUCCESS)
            Game.send("installfail");
    }

    /* Text the game copied, into Android's clipboard. */
    private void copy(String text) {
        android.content.ClipboardManager cm = (android.content.ClipboardManager) getSystemService(CLIPBOARD_SERVICE);
        if (cm != null)
            cm.setPrimaryClip(android.content.ClipData.newPlainText("Denboku", text));
    }

    /* The keyboard's Paste: into the field being typed in. */
    void pasteHere() { paste(); }

    /* The clipboard's text, on one line, for the field that asked. */
    private void paste() {
        android.content.ClipboardManager cm = (android.content.ClipboardManager) getSystemService(CLIPBOARD_SERVICE);
        if (cm == null || !cm.hasPrimaryClip() || cm.getPrimaryClip().getItemCount() == 0)
            return;
        CharSequence text = cm.getPrimaryClip().getItemAt(0).coerceToText(this);
        String line = text == null ? "" : text.toString().replaceAll("\\s+", " ").trim();
        if (!line.isEmpty())
            Game.send("clip " + line);
    }

    private void online(boolean on) {
        /* Without this permission the service still runs, only its notification is hidden. */
        if (on && Build.VERSION.SDK_INT >= 33 && checkSelfPermission(android.Manifest.permission.POST_NOTIFICATIONS) != android.content.pm.PackageManager.PERMISSION_GRANTED)
            requestPermissions(new String[] {android.Manifest.permission.POST_NOTIFICATIONS}, 0);
        Online.set(this, on);
    }

    /* "MS STRENGTH", strength 1-255. */
    private void vibrate(String args) {
        String[] a = args.split(" ");
        android.os.Vibrator v = (android.os.Vibrator) getSystemService(VIBRATOR_SERVICE);
        if (v == null || !v.hasVibrator())
            return;
        int ms = Integer.parseInt(a[0]), strength = Integer.parseInt(a[1]);
        if (Build.VERSION.SDK_INT >= 26)
            v.vibrate(android.os.VibrationEffect.createOneShot(ms, v.hasAmplitudeControl() ? strength : android.os.VibrationEffect.DEFAULT_AMPLITUDE));
        else
            v.vibrate(ms);
    }

    @Override public void exited() {
        runOnUiThread(() -> {
            Online.set(this, false);
            finishAndRemoveTask();
            System.exit(0);
        });
    }
}
