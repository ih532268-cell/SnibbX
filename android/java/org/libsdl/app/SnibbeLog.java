package org.libsdl.app;

import android.content.Context;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.nio.charset.StandardCharsets;

/**
 * Bridge to the native ring-buffer log (android_log.c) plus a Java-side crash file.
 *
 * Two persisted files inside the app's private files dir:
 *   crash_log.txt      written by the native signal handler
 *   java_crash.txt     written by our UncaughtExceptionHandler
 */
public final class SnibbeLog {
    private SnibbeLog() {}

    public static native String nativeGetLog();
    public static native void nativeSetLogPath(String path);

    private static boolean nativeLoaded = false;

    /** Called after System.loadLibrary has succeeded for "main". */
    static void markNativeLoaded() { nativeLoaded = true; }

    /** Live log from the running native code; empty string if native is not up. */
    public static String liveLog() {
        if (!nativeLoaded) return "(native library not loaded yet)";
        try {
            return nativeGetLog();
        } catch (Throwable t) {
            return "(nativeGetLog failed: " + t + ")";
        }
    }

    public static File nativeCrashFile(Context c) {
        // SDL_GetPrefPath() on Android is exactly <getFilesDir()>/ (verified in SDL 2.30 source),
        // and android_log.c writes "<prefpath>crash_log.txt".
        return new File(c.getFilesDir(), "crash_log.txt");
    }

    public static File javaCrashFile(Context c) {
        return new File(c.getFilesDir(), "java_crash.txt");
    }

    public static String readFile(File f) {
        if (f == null || !f.exists()) return null;
        try (FileInputStream in = new FileInputStream(f)) {
            byte[] b = new byte[(int) Math.min(f.length(), 256 * 1024)];
            int n = in.read(b);
            if (n <= 0) return null;
            return new String(b, 0, n, StandardCharsets.UTF_8);
        } catch (Throwable t) {
            return "(could not read " + f + ": " + t + ")";
        }
    }

    public static void deleteFile(File f) {
        try { if (f != null) f.delete(); } catch (Throwable ignored) {}
    }

    /** Install a handler that stores Java crashes to a file before the process dies. */
    public static void installJavaCrashHandler(final Context c) {
        final Thread.UncaughtExceptionHandler prev = Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler((t, e) -> {
            try {
                StringWriter sw = new StringWriter();
                e.printStackTrace(new PrintWriter(sw));
                String text = "JAVA CRASH in thread " + t.getName() + "\n" + sw;
                try (FileOutputStream out = new FileOutputStream(javaCrashFile(c))) {
                    out.write(text.getBytes(StandardCharsets.UTF_8));
                }
            } catch (Throwable ignored) {}
            if (prev != null) prev.uncaughtException(t, e);
        });
    }

    /** Everything worth sending to a developer, in one string. */
    public static String fullReport(Context c, String loadError) {
        StringBuilder sb = new StringBuilder();
        sb.append("=== snibbetracker-android report ===\n");
        sb.append("device: ").append(android.os.Build.MANUFACTURER).append(' ')
          .append(android.os.Build.MODEL).append('\n');
        sb.append("android: ").append(android.os.Build.VERSION.RELEASE)
          .append(" (API ").append(android.os.Build.VERSION.SDK_INT).append(")\n");
        sb.append("abis: ").append(java.util.Arrays.toString(android.os.Build.SUPPORTED_ABIS)).append('\n');
        if (loadError != null) sb.append("\n--- native load error ---\n").append(loadError).append('\n');

        String jc = readFile(javaCrashFile(c));
        if (jc != null) sb.append("\n--- previous JAVA crash ---\n").append(jc).append('\n');

        String nc = readFile(nativeCrashFile(c));
        if (nc != null) sb.append("\n--- previous NATIVE log/crash ---\n").append(nc).append('\n');

        sb.append("\n--- current native log ---\n").append(liveLog()).append('\n');
        return sb.toString();
    }
}
