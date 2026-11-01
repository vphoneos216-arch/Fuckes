package com.invisiblex.loader

import android.app.PendingIntent
import android.content.Intent
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.service.quicksettings.Tile
import android.service.quicksettings.TileService
import android.util.Log
import java.util.concurrent.Executors

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
class InvisibleXTileService : TileService() {

    private val executor = Executors.newSingleThreadExecutor()

    override fun onStartListening() {
        super.onStartListening()
        // [BUG FIX] AppPrefs was only ever init()'d from MainActivity.onCreate().
        // A Quick Settings tile tap after the app process was killed in the
        // background (very common — Android kills background app processes
        // freely) starts THIS service cold, with MainActivity never having
        // run in that process lifetime. AppPrefs.sPrefs was then still
        // uninitialized, so isSessionValid()/getSessionToken() etc. threw
        // immediately — the exact cause of the tile always showing
        // "Not Authenticated" on a fresh tile-triggered launch, even with a
        // perfectly valid saved session. init() is idempotent (no-ops once
        // already set up), so calling it here is always safe.
        AppPrefs.init(applicationContext)
        updateTileState()
    }

    override fun onClick() {
        super.onClick()
        AppPrefs.init(applicationContext) // same cold-start guard as onStartListening()
        Log.i(TAG, "Tile tapped — starting security gate")

        qsTile?.apply {
            state = Tile.STATE_ACTIVE
            label = getString(R.string.tile_verifying)
            updateTile()
        }

        // ── SECURITY GATE — Step 1: local check ──────────────
        if (!AppPrefs.isSessionValid()) {
            Log.w(TAG, "Local session invalid — blocking execution")
            setTileError(getString(R.string.session_expired))
            return
        }

        // ── SECURITY GATE — Step 2: server validation ─────────
        AuthManager.get().securityGate(applicationContext) { authOk, authMsg ->
            if (!authOk) {
                Log.w(TAG, "Server auth rejected — blocking execution: $authMsg")
                setTileError(getString(R.string.auth_required_tile))
                return@securityGate
            }

            // ── BOTH GATES PASSED — execute binary ────────────
            Log.i(TAG, "Double auth passed — launching binary")
            executor.execute {
                BinaryExecutor.extractAndExecute(applicationContext) { ok, msg ->
                    Log.i(TAG, "Binary result: $ok — $msg")
                    if (ok) {
                        setTileActive(getString(R.string.tile_running))
                        // Open post-launch overlay menu
                        openLaunchMenu()
                    } else {
                        setTileError(getString(R.string.tile_exec_error))
                    }
                }
            }
        }
    }

    /**
     * Open the post-launch overlay menu via startActivityAndCollapse.
     * This collapses the quick-settings shade and shows our overlay on top.
     *
     * CRITICAL Android 14+ bug fix: on targetSdk 34 (this app's target),
     * the Intent-overload of startActivityAndCollapse() is not just
     * deprecated — it THROWS UnsupportedOperationException at runtime.
     * That exception, happening right after the binary had already
     * launched successfully, is exactly why the tile looked like it
     * "stopped" with no overlay ever showing: the engine was running
     * fine, but the overlay launch call itself crashed. Only the
     * PendingIntent overload is valid on API 34+.
     */
    @Suppress("DEPRECATION")
    private fun openLaunchMenu() {
        val intent = Intent(applicationContext, LaunchMenuActivity::class.java)
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP)

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            val pi = PendingIntent.getActivity(
                applicationContext,
                0,
                intent,
                PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
            )
            startActivityAndCollapse(pi)
        } else {
            // Pre-Android-14: Intent overload is fine (just deprecated, not removed)
            startActivityAndCollapse(intent)
        }
    }

    override fun onStopListening() {
        super.onStopListening()
    }

    // ── Tile state helpers ────────────────────────────────────

    private fun updateTileState() {
        val tile = qsTile ?: return
        if (AppPrefs.isSessionValid()) {
            tile.state = Tile.STATE_INACTIVE
            tile.label = getString(R.string.tile_label)
            tile.contentDescription = getString(R.string.tile_ready_desc)
        } else {
            tile.state = Tile.STATE_UNAVAILABLE
            tile.label = getString(R.string.tile_not_auth)
            tile.contentDescription = getString(R.string.tile_not_auth)
        }
        tile.updateTile()
    }

    private fun setTileActive(label: String) {
        val tile = qsTile ?: return
        tile.state = Tile.STATE_ACTIVE
        tile.label = label
        tile.updateTile()
    }

    private fun setTileError(label: String) {
        val tile = qsTile ?: return
        tile.state = Tile.STATE_UNAVAILABLE
        tile.label = label
        tile.updateTile()

        // Reset to default after 3 seconds
        Handler(Looper.getMainLooper()).postDelayed({ updateTileState() }, 3000)
    }

    companion object {
        private const val TAG = "IX_Tile"
    }
}
