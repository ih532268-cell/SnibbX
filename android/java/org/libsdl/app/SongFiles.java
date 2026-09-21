package org.libsdl.app;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.util.Locale;

/**
 * Pure logic for importing / exporting songs. No Android classes are used here on purpose,
 * so it can be unit tested on a plain JVM.
 *
 * Why the strictness: the C engine copies file names into fixed 256 byte buffers with sprintf
 * and its in-app file dialog only lets the user type letters, digits and a few symbols, at most
 * 40 characters. A file that does not follow those rules could overflow a buffer or simply never
 * be listed, so every imported name is normalised to the same rules.
 */
public final class SongFiles {
    private SongFiles() {}

    public static final String EXT = ".snibb";
    /** Same as file_settings->file_name_limit in main.c. */
    public static final int MAX_NAME = 40;
    /** Songs are small JSON files (the largest bundled demo is ~96 KB). Reject anything huge. */
    public static final int MAX_BYTES = 2 * 1024 * 1024;

    /**
     * Turns any display name ("My Song (final) .snibb", "../../etc/x") into a safe base name
     * without extension: lowercase a-z, 0-9, '_' and '-', at most MAX_NAME characters,
     * never empty.
     */
    public static String sanitizeBaseName(String displayName) {
        String s = displayName == null ? "" : displayName;

        // drop any directory part, however it is written
        int slash = Math.max(s.lastIndexOf('/'), s.lastIndexOf('\\'));
        if (slash >= 0) s = s.substring(slash + 1);

        // drop the extension (only our own)
        if (s.toLowerCase(Locale.ROOT).endsWith(EXT)) {
            s = s.substring(0, s.length() - EXT.length());
        }

        StringBuilder out = new StringBuilder();
        for (int i = 0; i < s.length() && out.length() < MAX_NAME; i++) {
            char c = Character.toLowerCase(s.charAt(i));
            if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
                out.append(c);
            } else if (c == ' ' || c == '.') {
                out.append('_');
            }
            // every other character (unicode, quotes, control chars, ...) is dropped
        }

        // trim leading/trailing underscores that come from spaces
        int a = 0, b = out.length();
        while (a < b && out.charAt(a) == '_') a++;
        while (b > a && out.charAt(b - 1) == '_') b--;
        String r = out.substring(a, b);
        return r.isEmpty() ? "imported" : r;
    }

    /**
     * Picks a file name that does not exist yet in `dir`. Never overwrites: "song" -> "song",
     * then "song_2", "song_3", ... The suffix is accounted for so the base name never exceeds
     * MAX_NAME.
     */
    public static File uniqueTarget(File dir, String baseName) {
        File f = new File(dir, baseName + EXT);
        if (!f.exists()) return f;
        for (int n = 2; n < 10000; n++) {
            String suffix = "_" + n;
            String head = baseName.length() + suffix.length() > MAX_NAME
                    ? baseName.substring(0, MAX_NAME - suffix.length())
                    : baseName;
            f = new File(dir, head + suffix + EXT);
            if (!f.exists()) return f;
        }
        return new File(dir, "imported_" + System.currentTimeMillis() + EXT);
    }

    /**
     * Cheap structural check so that importing a random file (a photo, a PDF) is refused instead
     * of appearing in the list and failing to load. A snibbetracker project is a JSON object.
     * Returns null when OK, otherwise a short human readable reason.
     */
    public static String validate(byte[] data) {
        if (data == null || data.length == 0) return "file is empty";
        if (data.length > MAX_BYTES) return "file is too large for a song";
        int i = 0;
        // tolerate a UTF-8 BOM and leading whitespace
        if (data.length >= 3 && (data[0] & 0xFF) == 0xEF && (data[1] & 0xFF) == 0xBB && (data[2] & 0xFF) == 0xBF) i = 3;
        while (i < data.length && (data[i] == ' ' || data[i] == '\n' || data[i] == '\r' || data[i] == '\t')) i++;
        if (i >= data.length || data[i] != '{') return "not a snibbetracker song (not JSON)";

        // NUL bytes never occur in JSON text; they mean a binary file that happens to start with '{'
        for (byte b : data) if (b == 0) return "not a snibbetracker song (binary data)";

        // The engine's own project format contains these keys (checked against the bundled demos).
        String text = new String(data, StandardCharsets.UTF_8);
        if (!text.contains("\"patterns\"") || !text.contains("\"wavetable")) {
            return "not a snibbetracker song (missing song data)";
        }
        return null;
    }
}
