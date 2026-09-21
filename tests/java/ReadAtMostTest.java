package org.libsdl.app;

import java.io.*;

public class ReadAtMostTest {
    static int fails = 0, passes = 0;
    static void check(boolean c, String m) { if (c) { passes++; System.out.println("ok   : " + m); } else { fails++; System.out.println("FAIL : " + m); } }

    /** A stream that never ends: returns bytes forever, counts how many were actually pulled. */
    static class Endless extends InputStream {
        long pulled = 0;
        @Override public int read() { pulled++; return 'x'; }
        @Override public int read(byte[] b, int off, int len) { java.util.Arrays.fill(b, off, off + len, (byte) 'x'); pulled += len; return len; }
    }

    /** A stream that hands out 1 byte per call, like a slow network provider. */
    static class Trickle extends InputStream {
        final byte[] d; int i = 0;
        Trickle(byte[] d) { this.d = d; }
        @Override public int read() { return i < d.length ? (d[i++] & 0xFF) : -1; }
        @Override public int read(byte[] b, int off, int len) { if (i >= d.length) return -1; b[off] = d[i++]; return 1; }
    }

    public static void main(String[] a) throws Exception {
        byte[] small = "hello".getBytes();
        check(new String(SongTransfer.readAtMost(new ByteArrayInputStream(small), 100)).equals("hello"), "short input is returned whole");
        check(SongTransfer.readAtMost(new ByteArrayInputStream(new byte[0]), 100).length == 0, "empty input gives empty array");

        byte[] exact = new byte[1000]; java.util.Arrays.fill(exact, (byte) 'y');
        check(SongTransfer.readAtMost(new ByteArrayInputStream(exact), 1000).length == 1000, "input exactly at the limit is fully read");
        check(SongTransfer.readAtMost(new ByteArrayInputStream(exact), 999).length == 999, "input over the limit is cut at the limit");

        Endless e = new Endless();
        byte[] r = SongTransfer.readAtMost(e, 2 * 1024 * 1024 + 1);
        check(r.length == 2 * 1024 * 1024 + 1, "endless stream stops at limit (" + r.length + " bytes)");
        check(e.pulled <= 2 * 1024 * 1024 + 1 + 16 * 1024, "endless stream: never pulled more than limit + one buffer (" + e.pulled + ")");

        byte[] data = new byte[50000]; new java.util.Random(7).nextBytes(data);
        byte[] got = SongTransfer.readAtMost(new Trickle(data), 100000);
        check(java.util.Arrays.equals(data, got), "1-byte-per-read stream is reassembled correctly (50000 bytes)");

        // the pipeline that finishImport runs: cap -> validate must refuse an oversize song without holding it all
        byte[] big = new byte[SongFiles.MAX_BYTES + 5000]; java.util.Arrays.fill(big, (byte) 'x'); big[0] = (byte) '{';
        byte[] capped = SongTransfer.readAtMost(new ByteArrayInputStream(big), SongFiles.MAX_BYTES + 1);
        String why = SongFiles.validate(capped);
        check(capped.length == SongFiles.MAX_BYTES + 1 && why != null && why.contains("large"),
              "oversize file: read capped at MAX_BYTES+1 and refused for size -> \"" + why + "\"");

        System.out.println("\n" + (fails == 0 ? "ALL PASSED" : "FAILED") + "  (" + passes + " ok, " + fails + " failed)");
        System.exit(fails == 0 ? 0 : 1);
    }
}
