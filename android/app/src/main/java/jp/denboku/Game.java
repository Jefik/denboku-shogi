package jp.denboku;

import android.content.Context;
import android.content.pm.PackageManager;
import android.system.Os;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Locale;
import java.util.Map;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/* The game program (src/core/android.c) on its X server (Xvfb), both run from the library folder.
 * The files they read are unpacked from runtime.zip into files/usr, the prefix they were built for
 * (tools/android-stage.py), and files/denboku. */
final class Game {
    interface Listener {
        void frame(int x, int y, int w, int h);
        void message(String line); /* kb, back, snd, url, install… */
        void started();
        void exited();
    }

    static final String DISPLAY = ":0";
    private static OutputStream input;
    /* The activity on screen: Android may close it and open another while the game goes on. */
    private static volatile Listener listener;

    static void setListener(Listener l) { listener = l; }

    static boolean running() { return input != null; }

    static synchronized void send(String line) {
        if (input == null)
            return;
        try {
            input.write((line + "\n").getBytes(StandardCharsets.UTF_8));
            input.flush();
        } catch (IOException e) {
            input = null;
        }
    }

    private static void unpack(Context c, File files) throws IOException {
        File stamp = new File(files, ".runtime");
        String version;
        try {
            version = Long.toString(c.getPackageManager().getPackageInfo(c.getPackageName(), 0).lastUpdateTime);
        } catch (PackageManager.NameNotFoundException e) {
            version = "";
        }
        if (stamp.exists() && new String(Files.readAllBytes(stamp.toPath())).equals(version))
            return;
        delete(new File(files, "usr"));
        delete(new File(files, "denboku"));
        byte[] buf = new byte[65536];
        try (ZipInputStream z = new ZipInputStream(c.getAssets().open("runtime.zip"))) {
            for (ZipEntry e; (e = z.getNextEntry()) != null;) {
                File f = new File(files, e.getName());
                f.getParentFile().mkdirs();
                try (OutputStream o = new FileOutputStream(f)) {
                    for (int n; (n = z.read(buf)) > 0;)
                        o.write(buf, 0, n);
                }
            }
        }
        try (OutputStream o = new FileOutputStream(stamp)) {
            o.write(version.getBytes());
        }
    }

    private static void delete(File f) {
        File[] kids = f.isDirectory() && !Files.isSymbolicLink(f.toPath()) ? f.listFiles() : null;
        if (kids != null)
            for (File k : kids)
                delete(k);
        f.delete();
    }

    private static void link(String target, File link) {
        link.delete();
        try {
            Os.symlink(target, link.getPath());
        } catch (Exception e) {
            throw new RuntimeException(e);
        }
    }

    /* A process left from an earlier run of the app (its pid in `file`) is stopped. */
    private static void stop(File file) {
        try {
            int pid = Integer.parseInt(new String(Files.readAllBytes(file.toPath())).trim());
            android.os.Process.killProcess(pid);
        } catch (Exception e) {
        }
        file.delete();
    }

    private static Process run(File pid, File log, Map<String, String> env, String... argv) throws IOException {
        StringBuilder cmd = new StringBuilder("echo $$ > '" + pid + "'; exec");
        for (String a : argv)
            cmd.append(" '").append(a).append("'");
        ProcessBuilder pb = new ProcessBuilder("/system/bin/sh", "-c", cmd.toString());
        pb.environment().putAll(env);
        pb.redirectError(log);
        return pb.start();
    }

    /* Starts everything and reads the game's messages until it exits; runs on its own thread. */
    static void start(Context c, int viewW, int viewH, float density) throws IOException {
        File files = c.getFilesDir(), cache = c.getCacheDir(), usr = new File(files, "usr");
        String lib = c.getApplicationInfo().nativeLibraryDir;
        unpack(c, files);
        new File(usr, "bin").mkdirs();
        new File(usr, "tmp/.X11-unix").mkdirs();
        new File(usr, "share/X11/xkb/compiled").mkdirs();
        new File(files, "home").mkdirs();
        link("/system/bin/sh", new File(usr, "bin/sh"));
        link(lib + "/libxkbcomp.so", new File(usr, "bin/xkbcomp"));
        link(lib + "/libcurlbin.so", new File(usr, "bin/curl"));
        /* As many pixels as a computer's 96 dpi screen gives per Android dp (src/core/android.c). */
        int dpi = Math.round(96 * density);

        Map<String, String> env = new java.util.HashMap<>();
        env.put("LD_LIBRARY_PATH", lib);
        env.put("TMPDIR", cache.getPath());
        env.put("HOME", new File(files, "home").getPath());
        env.put("PATH", usr + "/bin:/system/bin");
        env.put("DISPLAY", DISPLAY);

        stop(new File(files, "game.pid"));
        stop(new File(files, "xvfb.pid"));
        File socket = new File(usr, "tmp/.X11-unix/X0"), fb = new File(cache, "fb");
        socket.delete();
        new File(usr, "tmp/.X0-lock").delete();
        fb.mkdirs();
        /* As large as the view can get, in either orientation; src/core/android.c sets its size. */
        int screen = Math.max(viewW, viewH) + 1;
        Map<String, String> xenv = new java.util.HashMap<>(env);
        xenv.put("LD_PRELOAD", lib + "/libfbshim.so"); /* the screen in memory (android/app/src/main/c/fbshim.c) */
        run(new File(files, "xvfb.pid"), new File(cache, "xvfb.log"), xenv, lib + "/libxvfb.so", DISPLAY, "-screen", "0",
            screen + "x" + screen + "x24", "-fbdir", fb.getPath(), "-nolisten", "tcp", "-extension", "GLX", "-extension", "MIT-SHM", "-nocursor",
            "-dpi", Integer.toString(dpi));
        for (int i = 0; i < 200 && !socket.exists(); i++)
            sleep(50);
        /* The screen's file can come a moment after the socket: until its header is there, the
         * game would stay black. */
        for (int i = 0; i < 200 && !Screen.map(new File(fb, "Xvfb_screen0").getPath()); i++)
            sleep(50);
        listener.frame(0, 0, screen, screen);

        Locale l = Locale.getDefault();
        env.put("LANG", l.getLanguage() + "_" + l.getCountry() + ".UTF-8");
        env.put("DENBOKU_DPI", Integer.toString(dpi));
        /* The name the other player sees by default: the phone's own, not "localhost". */
        String device = android.provider.Settings.Global.getString(c.getContentResolver(), "device_name");
        env.put("DENBOKU_DEVICE", device != null && !device.isEmpty() ? device : android.os.Build.MODEL);
        if (new File(files, "debug").exists()) /* adb shell run-as jp.denboku touch files/debug */
            env.put("DENBOKU_DEBUG", "1");
        File updateUrl = new File(files, "update-url"); /* a test server in place of GitHub (docs/BUILD.md) */
        if (updateUrl.exists())
            env.put("DENBOKU_UPDATE_URL", new String(Files.readAllBytes(updateUrl.toPath())).trim());
        if (new File(files, "dev").delete()) /* developer mode for this start: adb shell run-as jp.denboku touch files/dev */
            env.put("DENBOKU_DEV", "1");
        env.put("DENBOKU_BASE", new File(files, "denboku").getPath());
        env.put("DENBOKU_ENGINE", lib + "/libfairystockfish.so");
        env.put("DENBOKU_VIEW", viewW + "x" + viewH);
        /* The user's own piece and sound sets, in Android/data/jp.denboku/files/custom: a computer sees it over USB. */
        File external = c.getExternalFilesDir(null);
        if (external != null)
            env.put("DENBOKU_CUSTOM", new File(external, "custom").getPath());
        Process game = run(new File(files, "game.pid"), new File(cache, "denboku.log"), env, lib + "/libdenboku.so");
        input = game.getOutputStream();
        listener.started();
        read(game.getInputStream());
        input = null;
        stop(new File(files, "xvfb.pid"));
        listener.exited();
    }

    private static void read(InputStream in) throws IOException {
        BufferedReader r = new BufferedReader(new InputStreamReader(in, StandardCharsets.UTF_8));
        int x0 = Integer.MAX_VALUE, y0 = Integer.MAX_VALUE, x1 = 0, y1 = 0;
        for (String line; (line = r.readLine()) != null;) {
            if (line.startsWith("f ")) {
                String[] p = line.split(" ");
                int x = Integer.parseInt(p[1]), y = Integer.parseInt(p[2]);
                x0 = Math.min(x0, x);
                y0 = Math.min(y0, y);
                x1 = Math.max(x1, x + Integer.parseInt(p[3]));
                y1 = Math.max(y1, y + Integer.parseInt(p[4]));
            } else
                listener.message(line);
            /* A redraw comes as many areas: show them together once no more are waiting. */
            if (x1 > x0 && !r.ready()) {
                listener.frame(x0, y0, x1 - x0, y1 - y0);
                x0 = y0 = Integer.MAX_VALUE;
                x1 = y1 = 0;
            }
        }
    }

    private static void sleep(int ms) {
        try {
            Thread.sleep(ms);
        } catch (InterruptedException e) {
        }
    }
}
