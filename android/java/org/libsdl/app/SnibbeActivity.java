package org.libsdl.app;

import android.app.AlertDialog;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.graphics.Color;
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

/**
 * SDLActivity + an always-available log viewer.
 *
 *  - small "LOG" button in the top-right corner (kept tiny so it does not cover the tracker UI)
 *  - if the previous run crashed (native or Java) the report is opened automatically at start
 *  - the report can be copied to the clipboard or shared as text
 */
public class SnibbeActivity extends SDLActivity {

    private String loadError = null;
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

    private void addLogButton() {
        if (mLayout == null) return;
        Button b = new Button(this);
        b.setText("LOG");
        b.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11);
        b.setTextColor(Color.WHITE);
        b.setBackgroundColor(0x99000000);
        b.setAllCaps(false);
        b.setPadding(12, 4, 12, 4);
        b.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { showReport(false); }
        });
        RelativeLayout.LayoutParams lp = new RelativeLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.addRule(RelativeLayout.ALIGN_PARENT_TOP);
        lp.addRule(RelativeLayout.ALIGN_PARENT_RIGHT);
        lp.setMargins(0, 4, 4, 0);
        mLayout.addView(b, lp);
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
