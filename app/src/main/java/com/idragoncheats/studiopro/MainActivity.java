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
 * Launcher UI only. This activity does NOT perform the hook/injection
 * itself — the native module (libmain.so, REGISTER_ZYGISK_MODULE(GsModz) in
 * zygisk/zygisk.h) attaches on its own at zygote fork time if it is loaded
 * as a real Zygisk module, or is picked up by whatever external injector you
 * point at this APK's extracted lib if you're using it that way instead.
 * What this screen actually does, honestly:
 *   1. Checks that libmain.so is really present in this APK's installed
 *      native lib directory (a real filesystem check, not a guess).
 *   2. Plays the Access/Prepare/Launch progress animation.
 *   3. Launches the game via an explicit package intent, trying each of the
 *      three package names the module itself already targets (confirmed
 *      from zygisk/zygisk.h, not invented here): com.dts.freefiremax,
 *      com.dts.freefireth, com.dts.freefireadv, in that order.
 *   4. Mirrors every step into /sdcard/gsmodz_debug.log — the same file
 *      DebugLog.h (native side) writes to — so a failure here shows up in
 *      the one log file you need to pull off the device.
 */
public class MainActivity extends Activity {

    // Confirmed from zygisk/zygisk.h (OBFUSCATE'd package allow-list in the
    // actual module source), not guessed.
    private static final String[] TARGET_PACKAGES = {
            "com.dts.freefiremax",
            "com.dts.freefireth",
            "com.dts.freefireadv"
    };

    private static final String LOG_PATH = "/sdcard/gsmodz_debug.log";

    private View segAccess, segPrepare, segLaunch;
    private View btnPlay;
    private TextView txtPlayLabel;
    private TextView btnToggleGuide;
    private View guideSection;
    private boolean guideVisible = true;
    private boolean isBusy = false;

    private final Handler handler = new Handler(Looper.getMainLooper());

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
                startLaunchSequence();
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

    private void startLaunchSequence() {
        if (isBusy) return;
        isBusy = true;
        txtPlayLabel.setText(R.string.cta_checking);
        resetSegments();
        appendLog("INFO", "Let's play tapped — starting sequence");

        // Step 1: ACCESS — check the module lib is really packaged in this APK.
        setSeg(segAccess, true);
        boolean libPresent = checkModuleLibPresent();
        appendLog(libPresent ? "INFO" : "ERROR", "libmain.so present=" + libPresent);
        if (!libPresent) {
            Toast.makeText(this, R.string.toast_module_missing, Toast.LENGTH_LONG).show();
            finishSequence();
            return;
        }

        handler.postDelayed(new Runnable() {
            @Override
            public void run() {
                // Step 2: PREPARE — cosmetic pause; the actual attach happens
                // automatically via Zygisk (or your external injector), not
                // from this button.
                setSeg(segPrepare, true);
                appendLog("INFO", "Prepare step complete");

                handler.postDelayed(new Runnable() {
                    @Override
                    public void run() {
                        // Step 3: LAUNCH — open the game.
                        setSeg(segLaunch, true);
                        launchGame();
                        finishSequence();
                    }
                }, 450);
            }
        }, 450);
    }

    private boolean checkModuleLibPresent() {
        try {
            String nativeLibDir = getApplicationInfo().nativeLibraryDir;
            if (nativeLibDir == null) return false;
            File lib = new File(nativeLibDir, "libmain.so");
            return lib.exists() && lib.length() > 0;
        } catch (Exception e) {
            appendLog("ERROR", "checkModuleLibPresent failed: " + e);
            return false;
        }
    }

    private void launchGame() {
        PackageManager pm = getPackageManager();
        for (String pkg : TARGET_PACKAGES) {
            Intent intent = pm.getLaunchIntentForPackage(pkg);
            if (intent != null) {
                appendLog("INFO", "Launching " + pkg);
                Toast.makeText(this, R.string.toast_launching, Toast.LENGTH_SHORT).show();
                intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                startActivity(intent);
                return;
            }
        }
        appendLog("ERROR", "None of the target packages are installed");
        Toast.makeText(this, R.string.toast_game_not_found, Toast.LENGTH_LONG).show();
    }

    private void finishSequence() {
        isBusy = false;
        txtPlayLabel.setText(R.string.cta_play);
    }

    private void resetSegments() {
        setSeg(segAccess, false);
        setSeg(segPrepare, false);
        setSeg(segLaunch, false);
    }

    private void setSeg(View seg, boolean active) {
        seg.setBackgroundResource(active ? R.drawable.progress_seg : R.drawable.progress_seg_dim);
    }

    /** Mirrors into the same log file DebugLog.h (native side) writes to. */
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
            // block this; never crash the UI over a log write failing.
        }
    }
}
