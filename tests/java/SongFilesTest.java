import org.libsdl.app.SongFiles;
import java.io.File;
import java.nio.file.Files;
import java.nio.charset.StandardCharsets;

public class SongFilesTest {
    static int fails = 0, passes = 0;

    static void check(boolean cond, String msg) {
        if (cond) { passes++; System.out.println("ok   : " + msg); }
        else      { fails++;  System.out.println("FAIL : " + msg); }
    }

    static void eq(String actual, String expected, String label) {
        check(expected.equals(actual), label + "  -> \"" + actual + "\"" + (expected.equals(actual) ? "" : "  (expected \"" + expected + "\")"));
    }

    public static void main(String[] args) throws Exception {
        System.out.println("== sanitizeBaseName ==");
        eq(SongFiles.sanitizeBaseName("My Song.snibb"), "my_song", "spaces become underscores, extension dropped");
        eq(SongFiles.sanitizeBaseName("KISSEMISSE.SNIBB"), "kissemisse", "case-insensitive extension, lowercased");
        eq(SongFiles.sanitizeBaseName("../../etc/passwd"), "passwd", "path traversal reduced to the last component");
        eq(SongFiles.sanitizeBaseName("C:\\Users\\me\\track1.snibb"), "track1", "windows path reduced");
        eq(SongFiles.sanitizeBaseName("a\"b'c;d`e$f(g)h.snibb"), "abcdefgh", "shell / quote characters dropped");
        eq(SongFiles.sanitizeBaseName("\u0622\u0647\u0646\u06af \u062c\u062f\u06cc\u062f.snibb"), "imported", "persian only name falls back to imported");
        eq(SongFiles.sanitizeBaseName("\u0622\u0647\u0646\u06af song2.snibb"), "song2", "mixed persian/latin keeps the latin part");
        eq(SongFiles.sanitizeBaseName(""), "imported", "empty name");
        eq(SongFiles.sanitizeBaseName(null), "imported", "null name");
        eq(SongFiles.sanitizeBaseName("   "), "imported", "only spaces");
        eq(SongFiles.sanitizeBaseName("..."), "imported", "only dots");
        eq(SongFiles.sanitizeBaseName("-_-"), "-_-", "dash/underscore names are kept");
        eq(SongFiles.sanitizeBaseName("song.v2.final.snibb"), "song_v2_final", "inner dots become underscores");
        String longName = "a".repeat(200) + ".snibb";
        check(SongFiles.sanitizeBaseName(longName).length() == SongFiles.MAX_NAME, "200 char name is cut to MAX_NAME (" + SongFiles.MAX_NAME + ")");
        String weird = "x\u0000y\nz\t.snibb";
        eq(SongFiles.sanitizeBaseName(weird), "xyz", "control characters dropped");

        // property test: whatever comes in, the output obeys the rules the C engine depends on
        java.util.Random rnd = new java.util.Random(12345);
        boolean allOk = true; String firstBad = null;
        for (int i = 0; i < 20000 && allOk; i++) {
            StringBuilder sb = new StringBuilder();
            int n = rnd.nextInt(120);
            for (int j = 0; j < n; j++) sb.append((char) rnd.nextInt(0x3000));
            String out = SongFiles.sanitizeBaseName(sb.toString());
            boolean ok = !out.isEmpty() && out.length() <= SongFiles.MAX_NAME && out.matches("[a-z0-9_-]+");
            if (!ok) { allOk = false; firstBad = out; }
        }
        check(allOk, "20000 random unicode inputs always give 1.." + SongFiles.MAX_NAME + " chars of [a-z0-9_-]" + (allOk ? "" : "  bad=" + firstBad));

        System.out.println("\n== uniqueTarget ==");
        File dir = Files.createTempDirectory("songs").toFile();
        File a = SongFiles.uniqueTarget(dir, "song");
        eq(a.getName(), "song.snibb", "free name is used as is");
        Files.write(a.toPath(), new byte[]{'{'});
        File b = SongFiles.uniqueTarget(dir, "song");
        eq(b.getName(), "song_2.snibb", "existing name gets _2");
        Files.write(b.toPath(), new byte[]{'{'});
        eq(SongFiles.uniqueTarget(dir, "song").getName(), "song_3.snibb", "then _3");
        check(a.length() == 1, "existing file was not touched");

        String max = "a".repeat(SongFiles.MAX_NAME);
        Files.write(new File(dir, max + ".snibb").toPath(), new byte[]{'{'});
        File c = SongFiles.uniqueTarget(dir, max);
        String cbase = c.getName().substring(0, c.getName().length() - SongFiles.EXT.length());
        check(cbase.length() <= SongFiles.MAX_NAME && cbase.endsWith("_2"), "max-length name + collision stays within MAX_NAME  -> \"" + cbase + "\"");

        System.out.println("\n== validate: the REAL bundled demos must all pass ==");
        File demos = new File("res/demos");
        File[] list = demos.listFiles((d, n) -> n.endsWith(".snibb"));
        java.util.Arrays.sort(list);
        for (File f : list) {
            String r = SongFiles.validate(Files.readAllBytes(f.toPath()));
            check(r == null, f.getName() + " (" + f.length() + " bytes) accepted" + (r == null ? "" : " but got: " + r));
        }

        System.out.println("\n== validate: things that must be REFUSED ==");
        check(SongFiles.validate(null) != null, "null");
        check(SongFiles.validate(new byte[0]) != null, "empty file");
        check(SongFiles.validate("hello world".getBytes(StandardCharsets.UTF_8)) != null, "plain text");
        check(SongFiles.validate("{}".getBytes(StandardCharsets.UTF_8)) != null, "empty JSON object");
        check(SongFiles.validate("{\"a\":1}".getBytes(StandardCharsets.UTF_8)) != null, "unrelated JSON");
        check(SongFiles.validate("[1,2,3]".getBytes(StandardCharsets.UTF_8)) != null, "JSON array");
        byte[] png = {(byte) 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 0x0D};
        check(SongFiles.validate(png) != null, "PNG image");
        byte[] fakeBin = new byte[100]; fakeBin[0] = '{'; fakeBin[50] = 0;
        String rb = SongFiles.validate(fakeBin);
        check(rb != null && rb.contains("binary"), "binary starting with { -> \"" + rb + "\"");
        {   /* a valid-looking song that is simply too big: NO NUL bytes, so only the size check can refuse it */
            byte[] big = new byte[SongFiles.MAX_BYTES + 1];
            java.util.Arrays.fill(big, (byte) 'x');
            big[0] = (byte) '{';
            String rbig = SongFiles.validate(big);
            check(rbig != null && rbig.contains("large"), "file over " + SongFiles.MAX_BYTES + " bytes is refused for SIZE  -> \"" + rbig + "\"");
            byte[] okSize = new byte[SongFiles.MAX_BYTES];
            java.util.Arrays.fill(okSize, (byte) 'x');
            okSize[0] = (byte) '{';
            String rok = SongFiles.validate(okSize);
            check(rok == null || !rok.contains("large"), "exactly MAX_BYTES is not refused for size");
        }

        {   /* contains both song keys but does not start like a JSON object: only the start check can refuse it */
            String junk = "PK-not-json \"patterns\" \"wavetable\" {";
            String rj = SongFiles.validate(junk.getBytes(StandardCharsets.UTF_8));
            check(rj != null && rj.contains("not JSON"), "text with the right keys but not starting with { is refused -> \"" + rj + "\"");
        }

        System.out.println("\n== validate: things that must be ACCEPTED ==");
        byte[] demo = Files.readAllBytes(list[0].toPath());
        byte[] withBom = new byte[demo.length + 3];
        withBom[0] = (byte) 0xEF; withBom[1] = (byte) 0xBB; withBom[2] = (byte) 0xBF;
        System.arraycopy(demo, 0, withBom, 3, demo.length);
        check(SongFiles.validate(withBom) == null, "song with UTF-8 BOM");
        byte[] withWs = ("  \n\t" + new String(demo, StandardCharsets.UTF_8)).getBytes(StandardCharsets.UTF_8);
        check(SongFiles.validate(withWs) == null, "song with leading whitespace");

        System.out.println("\n== round trip: export bytes == import bytes ==");
        File src = list[0];
        byte[] orig = Files.readAllBytes(src.toPath());
        File out = SongFiles.uniqueTarget(dir, SongFiles.sanitizeBaseName(src.getName()));
        Files.write(out.toPath(), orig);
        check(java.util.Arrays.equals(orig, Files.readAllBytes(out.toPath())), "bytes identical after copy: " + out.getName());

        System.out.println("\n" + (fails == 0 ? "ALL PASSED" : "FAILED") + "  (" + passes + " ok, " + fails + " failed)");
        System.exit(fails == 0 ? 0 : 1);
    }
}
