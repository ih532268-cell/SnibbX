package org.libsdl.app;

import android.app.AlertDialog;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.view.Gravity;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.TextUtils;
import android.util.TypedValue;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.RelativeLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.util.List;

/**
 * SDLActivity + an always-available log viewer.
 *
 *  - small "LOG" button in the top-right corner (kept tiny so it does not cover the tracker UI)
 *  - if the previous run crashed (native or Java) the report is opened automatically at start
 *  - the report can be copied to the clipboard or shared as text
 */
public class SnibbeActivity extends SDLActivity {

    private String loadError = null;
    private SongTransfer transfer;
    private final Handler ui = new Handler(Looper.getMainLooper());

    @Override
    protected String[] getLibraries() {
        // "main" is our native code (SDL_main + android_log.c). Same list SDL's template uses.
        return new String[] { "SDL2", "main" };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        SnibbeLog.installJavaCrashHandler(getApplicationContext());
        super.onCreate(savedInstanceState);

        if (mBrokenLibraries) {
            // SDL has already shown its own (uncopyable) dialog and returned. Show ours as well.
            loadError = "SDL reported that the native libraries could not be loaded.\n"
                      + "Most common causes: wrong CPU ABI in the APK, or a missing .so file.";
            addLogButton();
            showReport(true);
            return;
        }

        SnibbeLog.markNativeLoaded();
        addLogButton();

        // If a previous run left a crash file, show it straight away.
        String prevNative = SnibbeLog.readFile(SnibbeLog.nativeCrashFile(this));
        // A previous run "failed" if it crashed, OR if it never reached the main loop
        // (e.g. window/renderer creation failed and the process exited quietly).
        boolean hadCrash = prevNative != null
            && (prevNative.contains("NATIVE CRASH") || !prevNative.contains("STARTUP OK"));
        boolean hadJavaCrash = SnibbeLog.readFile(SnibbeLog.javaCrashFile(this)) != null;
        if (hadCrash || hadJavaCrash) {
            ui.postDelayed(new Runnable() {
                @Override public void run() { showReport(true); }
            }, 800);
        }
    }

    /**
     * Same look and size as the "KB" button drawn by the C touch layer (touch_kb.c):
     * height/9 wide, two thirds of that plus 8 px tall, dark translucent box, 6 px from the corner.
     * The size follows the screen height, exactly like the C code, so they stay identical on any phone.
     */
    private Button cornerButton(String label, int widthPx, int heightPx) {
        Button b = new Button(this);
        b.setText(label);
        b.setAllCaps(false);
        b.setTextColor(Color.WHITE);
        b.setTypeface(Typeface.MONOSPACE);
        b.setTextSize(TypedValue.COMPLEX_UNIT_PX, heightPx * 0.34f);
        b.setBackgroundColor(0xC8282828);   // rgb(40,40,40) alpha 200, same as KB
        b.setPadding(0, 0, 0, 0);
        b.setMinWidth(0);
        b.setMinHeight(0);
        b.setMinimumWidth(0);
        b.setMinimumHeight(0);
        b.setGravity(Gravity.CENTER);
        b.setStateListAnimator(null);       // no elevation shadow: KB is flat
        return b;
    }

    private void addLogButton() {
        if (mLayout == null) return;
        int screenH = mLayout.getResources().getDisplayMetrics().heightPixels;
        int sz = Math.max(screenH / 9, 48);
        final int w = sz;
        final int h = sz * 2 / 3 + 8;
        final int margin = 6;

        Button log = cornerButton("LOG", w, h);
        log.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { showReport(false); }
        });
        RelativeLayout.LayoutParams lpLog = new RelativeLayout.LayoutParams(w, h);
        lpLog.addRule(RelativeLayout.ALIGN_PARENT_TOP);
        lpLog.addRule(RelativeLayout.ALIGN_PARENT_RIGHT);
        lpLog.setMargins(0, margin, margin, 0);
        mLayout.addView(log, lpLog);

        // FILE sits just left of LOG so it never overlaps the KB button on the other side
        Button file = cornerButton("FILE", w, h);
        file.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { showFileMenu(); }
        });
        RelativeLayout.LayoutParams lpFile = new RelativeLayout.LayoutParams(w, h);
        lpFile.addRule(RelativeLayout.ALIGN_PARENT_TOP);
        lpFile.addRule(RelativeLayout.ALIGN_PARENT_RIGHT);
        lpFile.setMargins(0, margin, margin + w + margin, 0);
        mLayout.addView(file, lpFile);
    }

    // ------------------------------------------------------------------ songs: export / import

    private SongTransfer transfer() {
        if (transfer == null) {
            // The engine keeps songs in SDL_GetPrefPath(), which on Android is exactly getFilesDir().
            transfer = new SongTransfer(this, getFilesDir());
        }
        return transfer;
    }

    private void showFileMenu() {
        final String[] items = { "Export a song (save to phone / share)", "Import a song from a file" };
        new AlertDialog.Builder(this)
            .setTitle("Songs")
            .setItems(items, new DialogInterface.OnClickListener() {
                @Override public void onClick(DialogInterface d, int which) {
                    if (which == 0) chooseSongToExport(); else report(transfer().startImport());
                }
            })
            .setNegativeButton("Close", null)
            .show();
    }

    private void chooseSongToExport() {
        final List<File> songs = transfer().listSongs();
        if (songs.isEmpty()) {
            Toast.makeText(this, "No saved songs yet. Save one with Ctrl + S first.", Toast.LENGTH_LONG).show();
            return;
        }
        final String[] names = new String[songs.size()];
        for (int i = 0; i < names.length; i++) names[i] = songs.get(i).getName();
        new AlertDialog.Builder(this)
            .setTitle("Export which song?")
            .setItems(names, new DialogInterface.OnClickListener() {
                @Override public void onClick(DialogInterface d, int which) {
                    report(transfer().startExport(songs.get(which)));
                }
            })
            .setNegativeButton("Cancel", null)
            .show();
    }

    /** Shows the outcome of a step. Empty messages (a screen was opened successfully) stay silent. */
    private void report(SongTransfer.Outcome o) {
        if (o == null || o.message == null || o.message.length() == 0) return;
        Toast.makeText(this, o.message, o.ok ? Toast.LENGTH_SHORT : Toast.LENGTH_LONG).show();
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode == SongTransfer.REQ_EXPORT) {
            report(transfer().finishExport(resultCode, data));
        } else if (requestCode == SongTransfer.REQ_IMPORT) {
            SongTransfer.Outcome o = transfer().finishImport(resultCode, data);
            report(o);
            if (o.ok) {
                Toast.makeText(this, "Open the song list in the tracker with Ctrl + O", Toast.LENGTH_LONG).show();
            }
        } else {
            super.onActivityResult(requestCode, resultCode, data);
        }
    }

    private void showReport(final boolean fromCrash) {
        final String report = SnibbeLog.fullReport(this, loadError);

        TextView tv = new TextView(this);
        tv.setText(report);
        tv.setTextIsSelectable(true);
        tv.setTypeface(android.graphics.Typeface.MONOSPACE);
        tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, 10);
        tv.setPadding(24, 16, 24, 16);
        tv.setHorizontallyScrolling(false);

        ScrollView sv = new ScrollView(this);
        sv.addView(tv);

        AlertDialog.Builder d = new AlertDialog.Builder(this);
        d.setTitle(fromCrash ? "snibbetracker - crash report" : "snibbetracker - log");
        d.setView(sv);
        d.setPositiveButton("Copy", new DialogInterface.OnClickListener() {
            @Override public void onClick(DialogInterface dialog, int which) { copy(report); }
        });
        d.setNeutralButton("Share", new DialogInterface.OnClickListener() {
            @Override public void onClick(DialogInterface dialog, int which) { share(report); }
        });
        d.setNegativeButton(fromCrash ? "Clear & close" : "Close",
            new DialogInterface.OnClickListener() {
                @Override public void onClick(DialogInterface dialog, int which) {
                    if (fromCrash) {
                        SnibbeLog.deleteFile(SnibbeLog.nativeCrashFile(SnibbeActivity.this));
                        SnibbeLog.deleteFile(SnibbeLog.javaCrashFile(SnibbeActivity.this));
                    }
                }
            });
        d.show();
    }

    private void copy(String text) {
        try {
            ClipboardManager cm = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
            cm.setPrimaryClip(ClipData.newPlainText("snibbetracker log", text));
            Toast.makeText(this, "Copied " + text.length() + " chars", Toast.LENGTH_SHORT).show();
        } catch (Throwable t) {
            Toast.makeText(this, "Copy failed: " + t, Toast.LENGTH_LONG).show();
        }
    }

    private void share(String text) {
        try {
            Intent i = new Intent(Intent.ACTION_SEND);
            i.setType("text/plain");
            i.putExtra(Intent.EXTRA_SUBJECT, "snibbetracker log");
            i.putExtra(Intent.EXTRA_TEXT, TextUtils.isEmpty(text) ? "(empty)" : text);
            startActivity(Intent.createChooser(i, "Share log"));
        } catch (Throwable t) {
            Toast.makeText(this, "Share failed: " + t, Toast.LENGTH_LONG).show();
        }
    }
}
