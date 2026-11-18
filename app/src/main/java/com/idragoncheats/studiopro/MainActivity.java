package com.idragoncheats.studiopro;

import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.SpannableString;
import android.text.Spanned;
import android.text.style.ForegroundColorSpan;
import android.text.style.StyleSpan;
import android.graphics.Typeface;
import android.view.View;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.FileOutputStream;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

/**
 * Launcher + actual root injector UI.
 *
 * What this build really does, step by step, honestly:
 *   1. ACCESS  — confirms libmain.so is packaged in this APK, and that root
 *      is actually grantable right now (`su -c id` returns uid=0). Both are
 *      real filesystem/process checks, not cosmetic.
 *   2. PREPARE — launches com.dts.freefiremax (Free Fire MAX only, per your
 *      instruction) if it isn't already running, then polls for its pid as
 *      root, and copies libmain.so + the injector binary out to
 *      /data/local/tmp as root so they're reachable.
 *   3. LAUNCH  — runs the injector binary (jni/injector/injector.cpp — a
 *      ptrace-based remote dlopen, see that file's own header comment for
 *      exactly how it works) as root against the game's pid.
 *
 * Every step's real result is mirrored into /sdcard/gsmodz_debug.log (the
 * same file the native DebugLog.h and injector.cpp both already write to),
 * because that's the one file that tells you which exact step failed if
 * this doesn't work on your device — I have no device here to test this
 * against, so I'm not going to claim it's guaranteed to work; the ptrace
 * mechanism itself is a standard, correct technique, but SELinux policy on
 * your specific device/ROM is the one variable I can't verify from here
 * (see injector.cpp's SELINUX NOTE for what to try if dlopen comes back
 * NULL in the target).
 */
public class MainActivity extends Activity {

    // Free Fire MAX only, per your instruction — confirmed real from the
    // module's own OBFUSCATE'd target list in zygisk/cheat.cpp /
    // Android.mk's compiled source, not guessed.
    private static final String TARGET_PACKAGE = "com.dts.freefiremax";

    private static final String LOG_PATH = "/sdcard/gsmodz_debug.log";
    private static final String REMOTE_DIR = "/data/local/tmp";
    private static final String REMOTE_LIB = REMOTE_DIR + "/libmain_inject.so";
    private static final String REMOTE_INJECTOR = REMOTE_DIR + "/gsmodz_injector";

    private View segAccess, segPrepare, segLaunch;
    private View btnPlay;
    private TextView txtPlayLabel;
    private TextView btnToggleGuide;
    private View guideSection;
    private boolean guideVisible = true;
    private boolean isBusy = false;

    private final Handler mainHandler = new Handler(Looper.getMainLooper());

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        segAccess = findViewById(R.id.segAccess);
        segPrepare = findViewById(R.id.segPrepare);
        segLaunch = findViewById(R.id.segLaunch);
        btnPlay = findViewById(R.id.btnPlay);
        txtPlayLabel = findViewById(R.id.txtPlayLabel);
        btnToggleGuide = findViewById(R.id.btnToggleGuide);
        guideSection = findViewById(R.id.guideSection);

        bindGuideStep(R.id.guideStep1, "01", getString(R.string.guide_01_title), getString(R.string.guide_01_body));
        bindGuideStep(R.id.guideStep2, "02", getString(R.string.guide_02_title), getString(R.string.guide_02_body));
        bindGuideStep(R.id.guideStep3, "03", getString(R.string.guide_03_title), getString(R.string.guide_03_body));

        btnToggleGuide.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                guideVisible = !guideVisible;
                guideSection.setVisibility(guideVisible ? View.VISIBLE : View.GONE);
                btnToggleGuide.setText(guideVisible ? R.string.hide_guide : R.string.show_guide);
            }
        });

        btnPlay.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                if (isBusy) return;
                isBusy = true;
                resetSegments();
                new Thread(new Runnable() {
                    @Override
                    public void run() {
                        runInjectionSequence();
                    }
                }, "gsmodz-inject").start();
            }
        });

        appendLog("INFO", "MainActivity created");
    }

    private void bindGuideStep(int includeRootId, String number, String title, String body) {
        View root = findViewById(includeRootId);
        TextView num = root.findViewById(R.id.txtStepNum);
        TextView text = root.findViewById(R.id.txtStepText);
        num.setText(number);

        String combined = title + " " + body;
        SpannableString spannable = new SpannableString(combined);
        spannable.setSpan(new StyleSpan(Typeface.BOLD), 0, title.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        spannable.setSpan(new ForegroundColorSpan(getResources().getColor(R.color.text_primary)), 0, title.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        text.setText(spannable);
    }

    // Runs entirely on the background thread started by the click listener.
    private void runInjectionSequence() {
        appendLog("INFO", "=== injection sequence start, target=" + TARGET_PACKAGE + " ===");
        setLabel(R.string.cta_checking);

        // ---- Step 1: ACCESS ----
        setSegUi(segAccess, true);

        String nativeLibDir = getApplicationInfo().nativeLibraryDir;
        File localLib = new File(nativeLibDir, "libmain.so");
        boolean libPresent = localLib.exists() && localLib.length() > 0;
        appendLog(libPresent ? "INFO" : "ERROR", "libmain.so present in APK=" + libPresent + " (" + localLib + ")");
        if (!libPresent) {
            toast(R.string.toast_module_missing);
            finishSequence();
            return;
        }

        boolean hasRoot = RootShell.hasRoot();
        appendLog(hasRoot ? "INFO" : "ERROR", "root access=" + hasRoot);
        if (!hasRoot) {
            toast(R.string.toast_no_root);
            finishSequence();
            return;
        }

        // ---- Step 2: PREPARE ----
        setSegUi(segPrepare, true);

        if (!isGameRunning()) {
            appendLog("INFO", "game not running yet, launching " + TARGET_PACKAGE);
            launchGameOnMain();
            toast(R.string.toast_waiting_process);
        }

        int pid = waitForPid(20, 500); // up to ~10s
        if (pid <= 0) {
            appendLog("ERROR", "timed out waiting for " + TARGET_PACKAGE + " to appear as a running process");
            toast(R.string.toast_game_not_found);
            finishSequence();
            return;
        }
        appendLog("INFO", "found target pid=" + pid);

        RootShell.Result mkdirR = RootShell.run("mkdir -p " + REMOTE_DIR);
        appendLog(mkdirR.ok() ? "INFO" : "ERROR", "mkdir " + REMOTE_DIR + " exit=" + mkdirR.exitCode);

        File injectorBin = new File(nativeLibDir, "injector");
        boolean injectorPresent = injectorBin.exists() && injectorBin.length() > 0;
        appendLog(injectorPresent ? "INFO" : "ERROR", "injector binary present in APK=" + injectorPresent + " (" + injectorBin + ")");
        if (!injectorPresent) {
            appendLog("ERROR", "injector executable missing from this build's native-lib dir — rebuild needed, this isn't a device-side problem");
            toast(R.string.toast_injected_fail);
            finishSequence();
            return;
        }

        RootShell.Result cpLib = RootShell.run("cp -f '" + localLib.getAbsolutePath() + "' '" + REMOTE_LIB + "' && chmod 644 '" + REMOTE_LIB + "'");
        appendLog(cpLib.ok() ? "INFO" : "ERROR", "copy libmain.so to " + REMOTE_LIB + " exit=" + cpLib.exitCode + " err=" + cpLib.stderr.trim());

        RootShell.Result cpInj = RootShell.run("cp -f '" + injectorBin.getAbsolutePath() + "' '" + REMOTE_INJECTOR + "' && chmod 755 '" + REMOTE_INJECTOR + "'");
        appendLog(cpInj.ok() ? "INFO" : "ERROR", "copy injector to " + REMOTE_INJECTOR + " exit=" + cpInj.exitCode + " err=" + cpInj.stderr.trim());

        if (!cpLib.ok() || !cpInj.ok()) {
            toast(R.string.toast_injected_fail);
            finishSequence();
            return;
        }

        // ---- Step 3: LAUNCH (actual injection) ----
        setSegUi(segLaunch, true);
        setLabel(R.string.cta_injecting);
        toast(R.string.toast_injecting);

        String cmd = REMOTE_INJECTOR + " " + pid + " " + REMOTE_LIB;
        appendLog("INFO", "running: " + cmd);
        RootShell.Result inject = RootShell.run(cmd);
        appendLog(inject.ok() ? "INFO" : "ERROR",
                "injector exit=" + inject.exitCode
                        + " stdout=[" + inject.stdout.trim() + "]"
                        + " stderr=[" + inject.stderr.trim() + "]");

        if (inject.ok()) {
            toast(R.string.toast_injected_ok);
        } else {
            toast(R.string.toast_injected_fail);
        }

        appendLog("INFO", "=== injection sequence end ===");
        finishSequence();
    }

    private boolean isGameRunning() {
        RootShell.Result r = RootShell.run("pidof " + TARGET_PACKAGE);
        return r.ok() && !r.stdout.trim().isEmpty();
    }

    private int waitForPid(int attempts, long delayMs) {
        for (int i = 0; i < attempts; i++) {
            RootShell.Result r = RootShell.run("pidof " + TARGET_PACKAGE);
            String out = r.stdout.trim();
            if (r.ok() && !out.isEmpty()) {
                try {
                    // pidof can print multiple pids if there are child
                    // processes; the first one is the main process.
                    return Integer.parseInt(out.split("\\s+")[0]);
                } catch (NumberFormatException ignored) {
                }
            }
            try {
                Thread.sleep(delayMs);
            } catch (InterruptedException ignored) {
            }
        }
        return -1;
    }

    private void launchGameOnMain() {
        final boolean[] launched = {false};
        final Object lock = new Object();
        mainHandler.post(new Runnable() {
            @Override
            public void run() {
                PackageManager pm = getPackageManager();
                Intent intent = pm.getLaunchIntentForPackage(TARGET_PACKAGE);
                if (intent != null) {
                    intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                    startActivity(intent);
                    launched[0] = true;
                }
                synchronized (lock) {
                    lock.notifyAll();
                }
            }
        });
        synchronized (lock) {
            try {
                lock.wait(2000);
            } catch (InterruptedException ignored) {
            }
        }
        appendLog(launched[0] ? "INFO" : "ERROR", "launch intent for " + TARGET_PACKAGE + " fired=" + launched[0]);
    }

    private void finishSequence() {
        isBusy = false;
        setLabel(R.string.cta_play);
    }

    private void resetSegments() {
        setSegUi(segAccess, false);
        setSegUi(segPrepare, false);
        setSegUi(segLaunch, false);
    }

    private void setSegUi(final View seg, final boolean active) {
        mainHandler.post(new Runnable() {
            @Override
            public void run() {
                seg.setBackgroundResource(active ? R.drawable.progress_seg : R.drawable.progress_seg_dim);
            }
        });
    }

    private void setLabel(final int resId) {
        mainHandler.post(new Runnable() {
            @Override
            public void run() {
                txtPlayLabel.setText(resId);
            }
        });
    }

    private void toast(final int resId) {
        mainHandler.post(new Runnable() {
            @Override
            public void run() {
                Toast.makeText(MainActivity.this, resId, Toast.LENGTH_LONG).show();
            }
        });
    }

    /** Mirrors into the same log file DebugLog.h / injector.cpp also write to. */
    private void appendLog(String level, String msg) {
        try {
            File f = new File(LOG_PATH);
            FileOutputStream fos = new FileOutputStream(f, true);
            String ts = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(new Date());
            String line = "[" + ts + "] [" + level + "] [UI] " + msg + "\n";
            fos.write(line.getBytes());
            fos.close();
        } catch (Exception e) {
            // best-effort only — scoped storage on some Android versions may
            // block this; never crash the flow over a log write failing.
        }
    }
}
