package com.invisiblex.loader;

import android.content.Intent;
import android.graphics.drawable.Icon;
import android.service.quicksettings.Tile;
import android.service.quicksettings.TileService;
import android.util.Log;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * InvisibleXTileService — Quick Settings tile in the notification shade.
 *
 * Security contract (NEVER BYPASS):
 *   1. AppPrefs.isSessionValid()    — local token + expiry check (instant).
 *   2. AuthManager.securityGate()  — server round-trip re-validation.
 *   BOTH must pass before BinaryExecutor.extractAndExecute() is called.
 *   If either fails → tile shows error state, binary does NOT execute.
 *
 * This mirrors the binary's own auth check — double validation on both sides.
 */
public class InvisibleXTileService extends TileService {

    private static final String TAG = "IX_Tile";

    private final ExecutorService executor = Executors.newSingleThreadExecutor();

    @Override
    public void onStartListening() {
        super.onStartListening();
        updateTileState();
    }

    @Override
    public void onClick() {
        super.onClick();
        Log.i(TAG, "Tile tapped — starting security gate");

        Tile tile = getQsTile();
        if (tile != null) {
            tile.setState(Tile.STATE_ACTIVE);
            tile.setLabel(getString(R.string.tile_verifying));
            tile.updateTile();
        }

        // ── SECURITY GATE — Step 1: local check ──────────────
        if (!AppPrefs.isSessionValid()) {
            Log.w(TAG, "Local session invalid — blocking execution");
            setTileError(getString(R.string.session_expired));
            return;
        }

        // ── SECURITY GATE — Step 2: server validation ─────────
        AuthManager.get().securityGate(getApplicationContext(), (authOk, authMsg) -> {
            if (!authOk) {
                Log.w(TAG, "Server auth rejected — blocking execution: " + authMsg);
                setTileError(getString(R.string.auth_required_tile));
                return;
            }

            // ── BOTH GATES PASSED — execute binary ────────────
            Log.i(TAG, "Double auth passed — launching binary");
            executor.execute(() -> BinaryExecutor.extractAndExecute(
                getApplicationContext(),
                (ok, msg) -> {
                    Log.i(TAG, "Binary result: " + ok + " — " + msg);
                    if (ok) {
                        setTileActive(getString(R.string.tile_running));
                        // Open post-launch overlay menu
                        openLaunchMenu();
                    } else {
                        setTileError(getString(R.string.tile_exec_error));
                    }
                }
            ));
        });
    }

    /**
     * Open the post-launch overlay menu via startActivityAndCollapse.
     * This collapses the quick-settings shade and shows our overlay on top.
     */
    @SuppressWarnings("deprecation")
    private void openLaunchMenu() {
        Intent intent = new Intent(getApplicationContext(), LaunchMenuActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        startActivityAndCollapse(intent);
    }

    @Override
    public void onStopListening() {
        super.onStopListening();
    }

    // ── Tile state helpers ────────────────────────────────────

    private void updateTileState() {
        Tile tile = getQsTile();
        if (tile == null) return;

        if (AppPrefs.isSessionValid()) {
            tile.setState(Tile.STATE_INACTIVE);
            tile.setLabel(getString(R.string.tile_label));
            tile.setContentDescription(getString(R.string.tile_ready_desc));
        } else {
            tile.setState(Tile.STATE_UNAVAILABLE);
            tile.setLabel(getString(R.string.tile_not_auth));
            tile.setContentDescription(getString(R.string.tile_not_auth));
        }
        tile.updateTile();
    }

    private void setTileActive(String label) {
        Tile tile = getQsTile();
        if (tile == null) return;
        tile.setState(Tile.STATE_ACTIVE);
        tile.setLabel(label);
        tile.updateTile();
    }

    private void setTileError(String label) {
        Tile tile = getQsTile();
        if (tile == null) return;
        tile.setState(Tile.STATE_UNAVAILABLE);
        tile.setLabel(label);
        tile.updateTile();

        // Reset to default after 3 seconds
        new android.os.Handler(android.os.Looper.getMainLooper()).postDelayed(
            this::updateTileState, 3000);
    }
}
