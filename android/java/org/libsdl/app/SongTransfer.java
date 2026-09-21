package org.libsdl.app;

import android.app.Activity;
import android.content.ContentResolver;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * Moves songs between the app's private folder and the rest of the phone, using only the system
 * "Storage Access Framework" screens (no storage permission and no FileProvider needed).
 *
 *   export: ACTION_CREATE_DOCUMENT  -> the user picks where to save (Downloads, Drive, ...)
 *   import: ACTION_OPEN_DOCUMENT    -> the user picks a .snibb file to bring in
 *
 * The engine itself is not touched: it lists whatever ".snibb" files are in its files folder.
 * All decisions about names and validity live in SongFiles (pure Java, unit tested).
 */
public final class SongTransfer {
    public static final int REQ_EXPORT = 0x5101;
    public static final int REQ_IMPORT = 0x5102;

    /** Result of an operation, ready to show to the user. */
    public static final class Outcome {
        public final boolean ok;
        public final String message;
        Outcome(boolean ok, String message) { this.ok = ok; this.message = message; }
    }

    private final Activity activity;
    private final File songDir;
    /** The song chosen for export, remembered until the system "save as" screen returns. */
    private File pendingExport;

    public SongTransfer(Activity activity, File songDir) {
        this.activity = activity;
        this.songDir = songDir;
    }

    // ------------------------------------------------------------------ listing

    /** Saved songs, alphabetical. Only regular ".snibb" files directly inside the song folder. */
    public List<File> listSongs() {
        File[] all = songDir.listFiles();
        List<File> out = new ArrayList<File>();
        if (all == null) return out;
        for (File f : all) {
            if (f.isFile() && f.getName().endsWith(SongFiles.EXT)) out.add(f);
        }
        java.util.Collections.sort(out);
        return out;
    }

    // ------------------------------------------------------------------ export

    /** Step 1: open the system "save as" screen for the chosen song. */
    public Outcome startExport(File song) {
        if (song == null || !song.isFile()) return new Outcome(false, "Song file not found");
        pendingExport = song;
        try {
            Intent i = new Intent(Intent.ACTION_CREATE_DOCUMENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);
            i.setType("application/octet-stream");
            i.putExtra(Intent.EXTRA_TITLE, song.getName());
            activity.startActivityForResult(i, REQ_EXPORT);
            return new Outcome(true, "");
        } catch (Throwable t) {
            pendingExport = null;
            return new Outcome(false, "Cannot open the save screen: " + t);
        }
    }

    /** Step 2: the user picked a destination; copy the bytes there. */
    public Outcome finishExport(int resultCode, Intent data) {
        File song = pendingExport;
        pendingExport = null;
        if (resultCode != Activity.RESULT_OK || data == null || data.getData() == null) {
            return new Outcome(false, "Export cancelled");
        }
        if (song == null || !song.isFile()) return new Outcome(false, "Song file not found");

        Uri dest = data.getData();
        ContentResolver cr = activity.getContentResolver();
        InputStream in = null;
        OutputStream out = null;
        try {
            in = new FileInputStream(song);
            // "wt" truncates: without it, exporting a smaller song over an older bigger file
            // would leave stale bytes at the end and corrupt it.
            out = cr.openOutputStream(dest, "wt");
            if (out == null) return new Outcome(false, "Cannot write to that location");
            byte[] buf = new byte[16 * 1024];
            long total = 0;
            int n;
            while ((n = in.read(buf)) > 0) {
                out.write(buf, 0, n);
                total += n;
            }
            out.flush();
            return new Outcome(true, "Exported " + song.getName() + " (" + total + " bytes)");
        } catch (Throwable t) {
            return new Outcome(false, "Export failed: " + t);
        } finally {
            closeQuietly(in);
            closeQuietly(out);
        }
    }

    // ------------------------------------------------------------------ import

    /** Step 1: open the system file picker. */
    public Outcome startImport() {
        try {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);
            // .snibb has no registered MIME type, so let the user see every file and validate ourselves
            i.setType("*/*");
            activity.startActivityForResult(i, REQ_IMPORT);
            return new Outcome(true, "");
        } catch (Throwable t) {
            return new Outcome(false, "Cannot open the file picker: " + t);
        }
    }

    /** Step 2: the user picked a file; check it and copy it into the song folder under a safe name. */
    public Outcome finishImport(int resultCode, Intent data) {
        if (resultCode != Activity.RESULT_OK || data == null || data.getData() == null) {
            return new Outcome(false, "Import cancelled");
        }
        Uri src = data.getData();
        ContentResolver cr = activity.getContentResolver();

        byte[] bytes;
        InputStream in = null;
        try {
            in = cr.openInputStream(src);
            if (in == null) return new Outcome(false, "Cannot read that file");
            bytes = readAtMost(in, SongFiles.MAX_BYTES + 1);
        } catch (Throwable t) {
            return new Outcome(false, "Import failed: " + t);
        } finally {
            closeQuietly(in);
        }

        String why = SongFiles.validate(bytes);
        if (why != null) return new Outcome(false, "Not imported: " + why);

        String base = SongFiles.sanitizeBaseName(displayName(cr, src));
        File target = SongFiles.uniqueTarget(songDir, base);

        // write to a temp file first and rename, so a crash mid-copy never leaves a half song
        File tmp = new File(songDir, target.getName() + ".part");
        FileOutputStream out = null;
        try {
            out = new FileOutputStream(tmp);
            out.write(bytes);
            out.getFD().sync();
            out.close();
            out = null;
            if (!tmp.renameTo(target)) {
                tmp.delete();
                return new Outcome(false, "Could not save the imported song");
            }
        } catch (Throwable t) {
            tmp.delete();
            return new Outcome(false, "Import failed: " + t);
        } finally {
            closeQuietly(out);
        }
        return new Outcome(true, "Imported as " + target.getName());
    }

    // ------------------------------------------------------------------ helpers

    private static String displayName(ContentResolver cr, Uri uri) {
        Cursor c = null;
        try {
            c = cr.query(uri, new String[] { OpenableColumns.DISPLAY_NAME }, null, null, null);
            if (c != null && c.moveToFirst()) {
                int idx = c.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (idx >= 0) return c.getString(idx);
            }
        } catch (Throwable ignored) {
        } finally {
            if (c != null) c.close();
        }
        String last = uri.getLastPathSegment();
        return last == null ? "" : last;
    }

    /** Reads up to `limit` bytes, so a huge or endless file can never fill the phone's memory. */
    static byte[] readAtMost(InputStream in, int limit) throws IOException {
        ByteArrayOutputStream bo = new ByteArrayOutputStream(64 * 1024);
        byte[] buf = new byte[16 * 1024];
        int total = 0;
        while (total < limit) {
            int want = Math.min(buf.length, limit - total);
            int n = in.read(buf, 0, want);
            if (n < 0) break;
            bo.write(buf, 0, n);
            total += n;
        }
        return bo.toByteArray();
    }

    private static void closeQuietly(java.io.Closeable c) {
        try { if (c != null) c.close(); } catch (Throwable ignored) {}
    }

    /** For tests / diagnostics. */
    public static String describe(List<File> songs) {
        String[] names = new String[songs.size()];
        for (int i = 0; i < names.length; i++) names[i] = songs.get(i).getName();
        return Arrays.toString(names);
    }
}
