// Forsaken VR on Quest and Steam Frame (standalone): the game, in the headset.
// SDL2's activity runs the engine; its main() is the game's own.
package net.gameordie.forsakenvr;

import android.content.Context;
import android.os.Bundle;
import android.system.Os;
import android.util.Log;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import org.libsdl.app.SDLActivity;

public class ForsakenActivity extends SDLActivity {
    static final String TAG = "ForsakenVR";

    // The folders the game writes into. A folder or file the PC's Setup copies
    // in over adb belongs to adb: the game can read it but not rewrite it, so
    // the app makes these folders itself.
    static final String[] WRITABLE = { "configs", "pilots", "savegame", "logs" };	// the game lowercases every path off Windows

    // Where the game's files live: Android/data/<package>/files.
    static File dataDir(Context c) {
        File d = c.getExternalFilesDir(null);
        return d != null ? d : c.getFilesDir();
    }

    // Make the writable folders, and take in every "name.restore" that Setup
    // copied over adb (settings, pilots and saved games kept across a
    // reinstall) as the app's own "name", which the game can then rewrite.
    // Returns how many of the folders can be written.
    static int prepare(Context c) {
        File base = dataDir(c);
        int writable = 0;
        for (String sub : WRITABLE) {
            File d = new File(base, sub);
            if (!d.isDirectory() && !d.mkdirs())
                Log.w(TAG, "cannot make " + d);
            takeIn(d);
            File probe = new File(d, ".write-test");
            try {
                new FileOutputStream(probe).close();
                if (probe.delete())
                    writable++;
            } catch (Exception e) {
                Log.w(TAG, "cannot write in " + d + ": " + e);
            }
        }
        takeIn(base);	// Player.txt and anything else at the top
        return writable;
    }

    static void takeIn(File dir) {
        File[] files = dir.listFiles();
        if (files == null)
            return;
        for (File from : files) {
            String n = from.getName();
            if (!from.isFile() || !n.endsWith(".restore"))
                continue;
            File to = new File(dir, n.substring(0, n.length() - ".restore".length()));
            File tmp = new File(dir, to.getName() + ".new");
            try (InputStream in = new FileInputStream(from); OutputStream out = new FileOutputStream(tmp)) {
                byte[] buf = new byte[65536];
                int k;
                while ((k = in.read(buf)) > 0)
                    out.write(buf, 0, k);
            } catch (Exception e) {
                Log.w(TAG, "could not take in " + from + ": " + e);
                tmp.delete();
                continue;
            }
            to.delete();
            if (tmp.renameTo(to))
                from.delete();
            else
                Log.w(TAG, "could not take in " + from);
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        int w = prepare(this);
        Log.i(TAG, "data " + dataDir(this) + ", " + w + " of " + WRITABLE.length + " folders writable");
        try {
            Os.setenv("HOME", dataDir(this).getAbsolutePath(), true);
        } catch (Exception e) {
            Log.w(TAG, "setenv: " + e);
        }
        super.onCreate(savedInstanceState);
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "c++_shared", "SDL2", "openal", "main" };
    }

    // The game has ended (its main returned and SDL finished the activity): end
    // the process too. Android keeps it otherwise, with the audio stream still
    // open, and the headset shows nothing but black.
    @Override
    protected void onDestroy() {
        super.onDestroy();
        android.os.Process.killProcess(android.os.Process.myPid());
    }

    @Override
    protected String[] getArguments() {
        // -vr: the headset, as the PC launcher asks; -log keeps a log for
        // Collect logs.
        java.util.ArrayList<String> args = new java.util.ArrayList<>(java.util.Arrays.asList("-vr", "-log"));
        // Desk tests only: words in desk-args.txt beside the game are added to
        // its command line (a player never has the file).
        File desk = new File(dataDir(this), "desk-args.txt");
        if (desk.isFile()) {
            try {
                String t = new String(java.nio.file.Files.readAllBytes(desk.toPath()), "UTF-8").trim();
                if (!t.isEmpty())
                    args.addAll(java.util.Arrays.asList(t.split("\\s+")));
                Log.i(TAG, "desk-args.txt: " + t);
            } catch (Exception e) {
                Log.w(TAG, "desk-args.txt: " + e);
            }
        }
        return args.toArray(new String[0]);	// the game moves into HOME itself (main.c)
    }
}
