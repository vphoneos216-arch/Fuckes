document.addEventListener('DOMContentLoaded', async() => {
    (function detectStatusBarHeight() {
        const root = document.documentElement;

        const envTest = getComputedStyle(document.documentElement)
            .getPropertyValue('--safe-area-test');
        let statusBarH = 0;
        let gestureBarH = 0;

        const dummy = document.createElement('div');
        dummy.style.cssText = 'position:fixed;top:env(safe-area-inset-top,0px);width:1px;height:1px;pointer-events:none;visibility:hidden;';
        document.body.appendChild(dummy);
        const topFromEnv = dummy.getBoundingClientRect()
            .top;
        document.body.removeChild(dummy);

        if (topFromEnv > 8) {
            statusBarH = Math.round(topFromEnv);
        } else {
            const dp = window.devicePixelRatio || 1;
            const screenOffset = window.screenTop || window.screenY || 0;
            if (screenOffset > 8 && screenOffset < 100) {
                statusBarH = screenOffset;
            } else {
                statusBarH = Math.round(28 * Math.min(dp, 3));
            }
        }

        const dummyBottom = document.createElement('div');
        dummyBottom.style.cssText = 'position:fixed;bottom:env(safe-area-inset-bottom,0px);width:1px;height:1px;pointer-events:none;visibility:hidden;';
        document.body.appendChild(dummyBottom);
        const bottomFromEnv = window.innerHeight - dummyBottom.getBoundingClientRect()
            .bottom;
        document.body.removeChild(dummyBottom);
        if (bottomFromEnv > 4) gestureBarH = Math.round(bottomFromEnv);

        root.style.setProperty('--status-bar-h', statusBarH + 'px');
        root.style.setProperty('--gesture-bar-h', gestureBarH + 'px');
    })();
    const notificationContainer = document.getElementById('notification-container');

    let notificationTimeout;

    const themeToggleBtn = document.getElementById('theme-toggle-btn');

    const bottomNav = document.getElementById('bottom-nav');
    const navButtons = bottomNav.querySelectorAll('.nav-btn');
    const viewContents = document.querySelectorAll('.view-content');
    const settingsNavBtn = document.getElementById('settings-nav-btn');
    const settingsPages = document.querySelectorAll('.settings-page');

    const loginKeyButton = document.getElementById('login-key-button');
    const inputKeyField = document.getElementById('input-key-field');

    const binarySelection = document.getElementById("binary-selection-bar");
    const binaryValue = document.getElementById("binary-selected-value");
    const binaryOptions = document.getElementById("binary-available-options");
    const binaryApplyButton = document.getElementById("binary-apply-button");
    let selectedBinary = "optimum_bar";
    let appliedBinary = "optimum_bar";
    if (binaryApplyButton) binaryApplyButton.dataset.param = selectedBinary;

    const LogDurationEl = document.getElementById("deamon-log-duration");
    const durationValue = document.getElementById("duration-selected-value");
    const durationOptions = document.getElementById("duration-available-options");
    const durationApplyButton = document.getElementById("duration-apply-button");
    let selectedDuration = "1d";
    if (durationApplyButton) durationApplyButton.dataset.param = selectedDuration;

    function showNotification(message, type = 'success', duration = 4000) {
        if (notificationTimeout) clearTimeout(notificationTimeout);
        notificationContainer.innerHTML = message.replace(/\n/g, '<br>');
        notificationContainer.className = 'show ' + type;
        notificationTimeout = setTimeout(() => {
            notificationContainer.className = notificationContainer.className.replace('show', '');
        }, duration);
    }

    function animateGroup(el, show, instant) {
        if (!el) return;

        if (instant) {
            el.style.transition = 'none';
            el.style.height = show ? 'auto' : '0px';
            el.style.opacity = show ? '1' : '0';
            if (show) el.classList.remove('group-hidden');
            else el.classList.add('group-hidden');
            return;
        }


        if (el._animFrame) cancelAnimationFrame(el._animFrame);
        clearTimeout(el._animTimeout);

        const EASE = 'cubic-bezier(0.4, 0, 0.2, 1)';

        if (show) {
            el.style.transition = 'none';
            el.style.height = 'auto';
            el.style.opacity = '1';
            const targetH = el.scrollHeight;

            el.style.height = '0px';
            el.style.opacity = '0';
            el.classList.remove('group-hidden');

            el._animFrame = requestAnimationFrame(() => {
                el._animFrame = requestAnimationFrame(() => {
                    const dur = Math.min(180 + targetH * 0.22, 380);
                    el.style.transition = `height ${dur}ms ${EASE}, opacity ${Math.round(dur * 0.7)}ms ease`;
                    el.style.height = targetH + 'px';
                    el.style.opacity = '1';

                    el._animTimeout = setTimeout(() => {
                        el.style.height = 'auto';
                        el.style.transition = '';
                    }, dur + 16);
                });
            });
        } else {
            const curH = el.scrollHeight;
            el.style.height = curH + 'px';
            el.style.transition = 'none';

            el._animFrame = requestAnimationFrame(() => {
                el._animFrame = requestAnimationFrame(() => {
                    const dur = Math.min(140 + curH * 0.18, 300);
                    el.style.transition = `height ${dur}ms ${EASE}, opacity ${Math.round(dur * 0.6)}ms ease`;
                    el.style.height = '0px';
                    el.style.opacity = '0';
                    el.classList.add('group-hidden');

                    el._animTimeout = setTimeout(() => {
                        el.style.transition = '';
                    }, dur + 16);
                });
            });
        }
    }


    let _themeOverlayBusy = false;

    function applyTheme(isLight, instant) {
        if (instant) {
            document.body.classList.toggle('light-theme', isLight);
            themeToggleBtn.innerHTML = isLight ? '<i class="fas fa-moon"></i>' : '<i class="fas fa-sun"></i>';
            localStorage.setItem('theme', isLight ? 'light' : 'dark');
            return;
        }
        if (_themeOverlayBusy) return;
        _themeOverlayBusy = true;

        document.body.style.transition = 'opacity 120ms ease';
        document.body.style.opacity = '0.35';

        setTimeout(() => {
            document.body.classList.toggle('light-theme', isLight);
            themeToggleBtn.innerHTML = isLight ? '<i class="fas fa-moon"></i>' : '<i class="fas fa-sun"></i>';
            localStorage.setItem('theme', isLight ? 'light' : 'dark');

            requestAnimationFrame(() => {
                requestAnimationFrame(() => {
                    document.body.style.transition = 'opacity 200ms ease';
                    document.body.style.opacity = '1';
                    setTimeout(() => {
                        document.body.style.transition = '';
                        document.body.style.opacity = '';
                        _themeOverlayBusy = false;
                    }, 220);
                });
            });
        }, 130);
    }
    themeToggleBtn.addEventListener('click', () => applyTheme(!document.body.classList.contains('light-theme')));

    const VIEW_ORDER = ['home-view', 'gamelist-view', 'settings-view'];
    let currentViewIndex = 0;


    const slidesWrapper = document.createElement('div');
    slidesWrapper.className = 'slides-wrapper';
    slidesWrapper.id = 'slides-wrapper';
    const mainContent = document.getElementById('main-content');
    Array.from(viewContents)
        .forEach(v => slidesWrapper.appendChild(v));
    mainContent.appendChild(slidesWrapper);


    const navIndicator = document.createElement('div');
    navIndicator.className = 'nav-indicator';
    navIndicator.id = 'nav-indicator';
    bottomNav.insertBefore(navIndicator, bottomNav.firstChild);

    let _lastSettingsPage = 'general-page';

    function updateOffscreenViews(activeIdx) {
        Array.from(viewContents)
            .forEach((v, i) => {
            v.classList.toggle('view-offscreen', i !== activeIdx);
        });
        // Setelah view offscreen hilang dari DOM, pastikan scroll tidak nyangkut
        window.scrollTo(0, 0);
        document.documentElement.scrollTop = 0;
        document.body.scrollTop = 0;
    }

    function slideToView(viewId, skipAnimate) {
        const idx = VIEW_ORDER.indexOf(viewId);
        if (idx === -1) return;
        currentViewIndex = idx;

        // Reset scroll dulu sebelum tampilkan semua view
        window.scrollTo(0, 0);
        document.documentElement.scrollTop = 0;
        document.body.scrollTop = 0;

        // Tampilkan semua view sebelum slide agar transisi smooth
        Array.from(viewContents)
            .forEach(v => v.classList.remove('view-offscreen'));

        const dur = skipAnimate ? 0 : 220;
        slidesWrapper.style.transition = dur ? `transform ${dur}ms cubic-bezier(0.4, 0, 0.2, 1)` : 'none';
        slidesWrapper.style.transform = `translateX(-${idx * 100}%)`;
        if (dur === 0) {
            requestAnimationFrame(() => {
                slidesWrapper.style.transition = '';
            });
        }

        navButtons.forEach(btn => btn.classList.remove('active'));
        if (idx === 2) {
            settingsNavBtn.classList.add('active');
        } else {
            const btn = bottomNav.querySelector(`[data-view="${viewId}"]`);
            if (btn) btn.classList.add('active');
        }
        navIndicator.style.transition = dur ? `transform ${dur}ms cubic-bezier(0.4, 0, 0.2, 1)` : 'none';
        navIndicator.style.transform = `translateX(${idx * 100}%)`;

        // Setelah transisi selesai, sembunyikan view offscreen dari DOM flow
        const hideDelay = dur > 0 ? dur + 20 : 0;
        if (dur > 0) mainContent.classList.add('is-swiping');
        setTimeout(() => {
            mainContent.classList.remove('is-swiping');
            updateOffscreenViews(idx);
        }, hideDelay);
    }


    slideToView('home-view', true);

    let _sx = 0, _sy = 0, _swipeOk = false, _swipeLocked = false;
    document.addEventListener('touchstart', e => {
        _sx = e.touches[0].clientX;
        _sy = e.touches[0].clientY;
        _swipeOk = true;
        _swipeLocked = false;
    }, {
        passive: true
    });
    document.addEventListener('touchmove', e => {
        if (!_swipeOk || _swipeLocked) return;
        const dx = Math.abs(e.touches[0].clientX - _sx);
        const dy = Math.abs(e.touches[0].clientY - _sy);
        // Tunggu gerakan cukup sebelum tentukan arah
        if (dx < 8 && dy < 8) return;
        if (dy > dx) {
            _swipeOk = false;
        } // vertikal — batalkan
        else {
            _swipeLocked = true;
        } // horizontal — kunci arah
    }, {
        passive: true
    });
    document.addEventListener('touchend', e => {
        if (!_swipeOk) return;
        _swipeOk = false;
        _swipeLocked = false;
        const dx = e.changedTouches[0].clientX - _sx;
        const dy = e.changedTouches[0].clientY - _sy;
        if (Math.abs(dx) > 44 && Math.abs(dx) > Math.abs(dy) * 1.2) {
            // Jangan swipe kalau sedang di dalam scrollable horizontal (dropdown)
            const inDropdown = e.target.closest('.available-options, .options-content');
            if (inDropdown) return;
            if (dx < 0 && currentViewIndex < VIEW_ORDER.length - 1) {
                slideToView(VIEW_ORDER[currentViewIndex + 1]);
            } else if (dx > 0 && currentViewIndex > 0) {
                slideToView(VIEW_ORDER[currentViewIndex - 1]);
            }
        }
    }, {
        passive: true
    });


    navButtons.forEach(button => {
        if (button.id === 'settings-nav-btn') return;
        button.addEventListener('click', () => slideToView(button.dataset.view));
    });
    settingsNavBtn.addEventListener('click', () => {
        slideToView('settings-view');
    });

    function progressButton(buttonElement) {
        if (buttonElement) {
            buttonElement.classList.add('loading');
            buttonElement.disabled = true;
        }
    }

    function dismissProgress(buttonElement) {
        if (buttonElement) {
            buttonElement.classList.remove('loading');
            buttonElement.disabled = false;
        }
    }

    const serviceBanner = document.getElementById('service-banner');
    const serviceStatus = document.getElementById('service-status');
    const currentProfileMode = document.getElementById('current-profile-mode');

    const actionStartButton = document.getElementById('action-start-button');
    if (actionStartButton) {
        actionStartButton.addEventListener('click', async() => {
            progressButton(actionStartButton);
            try {
                // svc.sh detaches the watchdog and daemon itself and returns
                // immediately, so this call cannot sit holding the page.
                await execCommand(`sh ${SVC} start </dev/null 2>&1`);

                // The watchdog needs a moment; check once, then let the
                // periodic refresh correct the card if it comes up later.
                await new Promise(r => setTimeout(r, 1500));
                const running = await isDaemonRunning();
                showNotification(
                    running ? 'Service started' : 'Service starting — check Daemon Logs',
                    running ? 'success' : 'info',
                    2500
                );
                await updateHeroCard(running);
            } catch (e) {
                showNotification(`Start failed: ${e}`, 'error', 3500);
                await updateHeroCard(null);
            } finally {
                dismissProgress(actionStartButton);
            }
        });
    }
    
    const actionReloadButton = document.getElementById("action-reload-button");
    if (actionReloadButton) {
        actionReloadButton.addEventListener('click', async() => {
            progressButton(actionReloadButton);
            await new Promise(r => setTimeout(r, 100));
            await reloadAll();
            dismissProgress(actionReloadButton);
        });
    }

    const actionUninstallButton = document.getElementById('action-uninstall-button');
    if (actionUninstallButton) {
        actionUninstallButton.addEventListener('click', async() => {
            progressButton(actionUninstallButton);
            try {
                // svc.sh stops the watchdog before the daemon; stopping the
                // daemon alone did nothing, the watchdog brought it back.
                await execCommand(`sh ${SVC} stop </dev/null 2>&1`);
                await new Promise(r => setTimeout(r, 1200));
                const still = await isDaemonRunning();
                showNotification(
                    still ? 'Service still running — check Daemon Logs' : 'Service stopped',
                    still ? 'error' : 'success',
                    2500
                );
                await updateHeroCard(still);
            } catch (e) {
                showNotification(`Stop failed: ${e}`, 'error', 3500);
                await updateHeroCard(null);
            } finally {
                dismissProgress(actionUninstallButton);
            }
        });
    }
    
const actionCheatButton = document.getElementById("action-cheat-button");

if (actionCheatButton) {

    actionCheatButton.addEventListener('click', async () => {
        progressButton(actionCheatButton);
        try {
            await new Promise(r => setTimeout(r, 50));

            // Same contract as the daemon: the verified key from config.json
            // is passed as argv[1], so the binary re-verifies it itself.
            const cfg = await readConfig();
            if (cfg.status !== 'success' || !cfg.key) {
                showNotification("Login first", "error", 2500);
                return;
            }
            const bin = 'optimum_bar'; // single binary; Bar/Volume is config menu_open

            if (await isCheatRunning(bin)) {
                showNotification("Cheat already running", "info", 2500);
                return;
            }

            // Output goes to webroot/cheat.log so a failure is visible.
            // The PID is written to config/<bin>.pid so the daemon sees this
            // instance as running and does not start a second copy.
            const r = await runCommand(`
                BIN=${MODULE_DIR}/bin/arm64-v8a/${bin}
                PIDF=${MODULE_DIR}/config/${bin}.pid
                LOG=${MODULE_DIR}/webroot/cheat.log
                [ -f "$BIN" ] || { echo "Binary missing: $BIN"; exit 1; }
                chmod 0755 "$BIN" && chown 0:0 "$BIN" || { echo "Permission change failed"; exit 1; }
                mkdir -p ${MODULE_DIR}/config
                nohup "$BIN" ${shQuote(cfg.key)} > "$LOG" 2>&1 &
                PID=$!
                echo "$PID" > "$PIDF"
                sleep 4
                if kill -0 "$PID" 2>/dev/null; then echo "STARTED $PID"; else echo "EXITED"; exit 1; fi
            `);

            const out = r.out.trim();
            if (out.startsWith('STARTED')) {
                showNotification("Cheat started", "success", 2500);
            } else {
                console.log(out, r.err);
                showNotification(out === 'EXITED' ? "Cheat exited, see cheat.log" : (out || "Cheat did not start"), "error", 4000);
            }
        } catch (e) {
            console.error(e);
            showNotification("Failed to start cheat", "error", 2500);
        } finally {
            dismissProgress(actionCheatButton);
        }
    });

}


    const MODULE_DIR = "/data/adb/modules/inx_mods";
    const SVC = `${MODULE_DIR}/svc.sh`;
    // Upper bound for any single shell call made from this page.
    const EXEC_TIMEOUT_MS = 15000;
    // The daemon writes a heartbeat every poll (2 s in game, 5 s idle),
    // so anything older than this means the service is not running.
    const HEARTBEAT_MAX_AGE_SECS = 20;

    function shQuote(value) {
        // POSIX single-quote: makes any key or path inert inside sh.
        return "'" + String(value).replace(/'/g, "'\\''") + "'";
    }

    // Always read fresh; a cached config.json left Key Info showing N/A.
    async function readConfig() {
        const res = await fetch(`config.json?t=${Date.now()}`, { cache: "no-store" });
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        return res.json();
    }

    async function writeConfig(cfg) {
        const body = JSON.stringify(cfg, null, 4);
        await execCommand(
            `printf '%s' ${shQuote(body)} > ${MODULE_DIR}/webroot/config.json && chmod 644 ${MODULE_DIR}/webroot/config.json`
        );
    }

    // [SIMPLIFIED 2026-11-08] Seller/expiry rows removed from the UI per
    // request — only the key itself is shown. Hides each row's label too
    // (the label is the plain <strong> immediately before the value div in
    // index.html), so nothing but "Key :" is left in the Key Info box.
    let _keyInfoRowsHidden = false;
    function hideSellerExpiryRows() {
        if (_keyInfoRowsHidden) return;
        ["validity-date", "seller-name"].forEach(id => {
            const el = document.getElementById(id);
            if (!el) return;
            el.style.display = "none";
            const label = el.previousElementSibling;
            if (label && label.tagName === "STRONG") label.style.display = "none";
        });
        _keyInfoRowsHidden = true;
    }

    function applyKeyInfo(info) {
        document.getElementById("key-name").textContent = info.key || "N/A";
        hideSellerExpiryRows();
    }

    // ── Remote Control ──────────────────────────────────────
    // Writes "remote_control" into the same config.json the binary reads.
    // When it is on the binary serves its page on 127.0.0.1 and does not draw
    // the in-game menu; it writes the URL (with its one-time token) back into
    // "remote_url", which is what gets shown below.
    const remoteToggle = document.getElementById('remote-control-toggle');
    const remoteHint   = document.getElementById('remote-control-hint');
    const remoteBox    = document.getElementById('remote-url-box');
    const remoteText   = document.getElementById('remote-url-text');
    const remoteCopy   = document.getElementById('remote-url-copy');

    function describeRemote(on, url) {
        if (remoteHint) {
            remoteHint.textContent = on
                ? 'On: the in-game menu stays hidden and everything is controlled from the link below. Starts the next time the game opens.'
                : 'Off: controls stay in the in-game menu.';
        }
        if (remoteBox) {
            remoteBox.hidden = !on;
            if (on && remoteText) {
                remoteText.textContent = url || 'Open the game once to get the link.';
            }
        }
    }

    if (remoteCopy) {
        remoteCopy.addEventListener('click', async () => {
            const url = remoteText ? remoteText.textContent.trim() : '';
            if (!url || !url.startsWith('http')) {
                showNotification('No link yet — open the game once', 'info', 2500);
                return;
            }
            try {
                await navigator.clipboard.writeText(url);
                showNotification('Link copied', 'success', 2000);
            } catch (e) {
                showNotification('Could not copy — long-press the link instead', 'info', 3000);
            }
        });
    }

    if (remoteToggle) {
        remoteToggle.addEventListener('change', async () => {
            const on = remoteToggle.checked;
            try {
                const cfg = await readConfig();
                cfg.remote_control = on;
                if (!on) cfg.remote_url = "";      // the old token is dead once it stops
                await writeConfig(cfg);
                describeRemote(on, on ? cfg.remote_url : "");
                showNotification(
                    on ? 'Remote control on — starts next time the game opens'
                       : 'Remote control off — menu is back',
                    'success', 3000);
            } catch (e) {
                remoteToggle.checked = !on;
                showNotification(`Could not save: ${e}`, 'error', 3500);
            }
        });
    }

    // ── Driver switches ─────────────────────────────────────
    // Both write into the same config.json everything else reads.
    //   driver_load_on_boot : service.sh runs the driver path at boot
    //   driver_keep_alive   : the daemon reloads the driver and retries at
    //                         once when the binary starts without one
    const driverBootToggle = document.getElementById('driver-boot-toggle');
    const driverKeepToggle = document.getElementById('driver-keepalive-toggle');

    function wireConfigToggle(el, key, onText, offText) {
        if (!el) return;
        el.addEventListener('change', async () => {
            const on = el.checked;
            try {
                const cfg = await readConfig();
                cfg[key] = on;
                await writeConfig(cfg);
                showNotification(on ? onText : offText, 'success', 2600);
            } catch (e) {
                el.checked = !on;
                showNotification(`Could not save: ${e}`, 'error', 3500);
            }
        });
    }

    wireConfigToggle(driverBootToggle, 'driver_load_on_boot',
                     'Driver will load on boot', 'Driver will not load on boot');
    wireConfigToggle(driverKeepToggle, 'driver_keep_alive',
                     "Don't Stop on — driver reloads automatically",
                     "Don't Stop off");

    async function loadConfigs() {
        try {
            const data = await readConfig();
            const volMode = data.menu_open === 'vol';
            currentProfileMode.textContent = volMode ? 'Volume' : 'Bar';
            binaryValue.textContent = volMode ? 'Volume' : 'Bar';
            selectedBinary = volMode ? 'optimum_vol' : 'optimum_bar';
            appliedBinary = selectedBinary;
            durationValue.textContent = (data.duration === "1d") ? "24 Hours" : (data.duration === "2d") ? "2 Days" : "3 Days";
            selectedDuration = data.duration;
            inputKeyField.value = data.key || "";
            applyKeyInfo(data);
            if (driverBootToggle) driverBootToggle.checked = data.driver_load_on_boot === true;
            if (driverKeepToggle) driverKeepToggle.checked = data.driver_keep_alive === true;
            restoreDriverSelection(data.driver || "");
            if (remoteToggle) {
                const on = data.remote_control === true;
                remoteToggle.checked = on;
                describeRemote(on, data.remote_url || "");
            }
        } catch (error) {
            showNotification('Server error', 'error', 2500);
            console.error("Fetch error:", error);
        }
    }

    async function updateHeroCard(isActive) {
        let isServiceRunning = false;
        if (isActive == null) {
            isServiceRunning = await isDaemonRunning();
        } else {
            isServiceRunning = isActive;
        }
        if (serviceStatus) {
            serviceStatus.textContent = isServiceRunning ? 'Active' : 'Stopped';
            serviceStatus.className = isServiceRunning ? `hero-status status-running` : `hero-status status-stopped`;
        }

        if (serviceBanner) {
            const targetImage = isServiceRunning ? 'img/running.webp' : 'img/stopped.webp';
            serviceBanner.src = targetImage;
        }
    }


    // [CRASH WORKAROUND 2026-11-08] inx_login_@api can SIGBUS (exit 135,
    // empty stdout -> "Login output not JSON") from a bad obfuscator-emitted
    // jump target inside run_silent() — confirmed on-device via tombstone,
    // a bug in the compiled binary itself, not in this page. A plain PIE
    // binary gets a fresh randomized load address every exec, so the exact
    // same binary + exact same key can crash on one run and run clean on
    // the next; this is already fixed at the source and queued for the
    // next build. Until that build lands, re-running the SAME verification
    // call a few times gives each attempt a fresh load address and, in
    // practice, a real chance to get past it. This changes NOTHING about
    // security: every attempt is still the real binary doing the real
    // server-side key check — nothing is cached, skipped, or assumed.
    // A genuine server answer (right key, wrong key, expired, ...) always
    // stops the loop immediately; only the crash/no-JSON case retries.
    const LOGIN_MAX_ATTEMPTS = 4;

    async function runLoginBinaryOnce(key_value) {
        const r = await runCommand(`
            chmod 0755 ${MODULE_DIR}/api/arm64-v8a/inx_login_@api &&
            chown 0:0 ${MODULE_DIR}/api/arm64-v8a/inx_login_@api &&
            ${MODULE_DIR}/api/arm64-v8a/inx_login_@api ${shQuote(key_value)}
        `);
        const jsonLine = r.out.split('\n').map(s => s.trim()).filter(s => s.startsWith('{')).pop() || '';
        try {
            return { ok: true, data: JSON.parse(jsonLine), r };
        } catch (parseErr) {
            return { ok: false, r };
        }
    }

    if (loginKeyButton) {
        loginKeyButton.addEventListener('click', async() => {
            progressButton(loginKeyButton);
            await new Promise(r => setTimeout(r, 50));
            try {
                const key_value = inputKeyField.value.trim();
                if (!key_value) {
                    showNotification('Enter your key first', 'error', 2500);
                    return;
                }

                let outcome = null;
                for (let attempt = 1; attempt <= LOGIN_MAX_ATTEMPTS; attempt++) {
                    outcome = await runLoginBinaryOnce(key_value);
                    if (outcome.ok) break; // got a real JSON answer, success or error
                    if (attempt < LOGIN_MAX_ATTEMPTS) {
                        showNotification(`Verifying... (retry ${attempt}/${LOGIN_MAX_ATTEMPTS - 1})`, 'info', 1200);
                        await new Promise(res => setTimeout(res, 400));
                    }
                }

                if (!outcome.ok) {
                    const { r } = outcome;
                    console.log(r.out, r.err);
                    const rawOut = (r.out || '').trim().slice(0, 160) || '(empty stdout)';
                    const rawErr = (r.err || '').trim().slice(0, 160) || '(no stderr)';
                    showNotification(`Login crashed ${LOGIN_MAX_ATTEMPTS}x in a row [exit ${r.code}] out: ${rawOut} | err: ${rawErr}. Try pressing Login again.`, 'error', 9000);
                    return;
                }

                const data = outcome.data;
                if (data.status !== "success") {
                    showNotification(data.message, data.status, 3000);
                    return;
                }
                // The binary already verified the key AND already wrote
                // key/status into config.json itself (update_web_config() in
                // inx_login.cpp). This is only a fallback for the rare case
                // it could not write the file itself (data.saved === false) -
                // e.g. a permissions problem, not a verification problem.
                if (data.saved === false) {
                    try {
                        const cfg = await readConfig();
                        cfg.key = data.key;
                        cfg.status = 'success';
                        await writeConfig(cfg);
                    } catch (saveErr) {
                        console.log(saveErr);
                        showNotification(`Key verified, but save failed. Binary: ${data.save_error}`, 'error', 6000);
                        return;
                    }
                }
                applyKeyInfo(data);
                showNotification(data.message, 'success', 3000);
            } catch (e) {
                console.log(e);
                showNotification('Login failed', 'error', 2500);
            } finally {
                dismissProgress(loginKeyButton);
            }
        });
    }

    function setupBarSelection(barElement, onSelectCallback) {
        const closeButton = barElement.querySelector('.options-close-btn');
        const dropdown = barElement.querySelector('.available-options');
        let detached = false;

        function positionDropdown() {
            if (!dropdown) return;
            const r = barElement.getBoundingClientRect();
            const spaceBelow = window.innerHeight - r.bottom;
            const openUp = spaceBelow < 250 && r.top > spaceBelow;
            dropdown.style.position = 'fixed';
            dropdown.style.left = r.left + 'px';
            dropdown.style.minWidth = r.width + 'px';
            dropdown.style.maxWidth = '320px';
            dropdown.style.top = openUp ? 'auto' : (r.bottom + 4) + 'px';
            dropdown.style.bottom = openUp ? (window.innerHeight - r.top + 4) + 'px' : 'auto';
        }

        let _scrollHandler = null;

        function detachDropdown() {
            if (!dropdown || detached) return;
            const placeholder = document.createElement('span');
            placeholder.id = '_dd_placeholder_' + barElement.id;
            placeholder.style.display = 'none';
            barElement.appendChild(placeholder);
            document.body.appendChild(dropdown);
            detached = true;
            _scrollHandler = () => {
                const r = barElement.getBoundingClientRect();
                if (r.top < -10 || r.bottom > window.innerHeight + 10) {
                    close();
                    return;
                }
                positionDropdown();
            };
            window.addEventListener('scroll', _scrollHandler, {
                passive: true,
                capture: true
            });
        }

        function reattachDropdown() {
            if (!dropdown || !detached) return;
            if (_scrollHandler) {
                window.removeEventListener('scroll', _scrollHandler, {
                    capture: true
                });
                _scrollHandler = null;
            }
            const ph = barElement.querySelector('[id^="_dd_placeholder_"]');
            if (ph) {
                barElement.insertBefore(dropdown, ph);
                ph.remove();
            } else barElement.appendChild(dropdown);
            dropdown.style.cssText = '';
            detached = false;
        }

        function close() {
            barElement.classList.remove('active');
            dropdown && (dropdown.style.display = 'none');
            reattachDropdown();
        }

        barElement.addEventListener('click', (e) => {
            if (barElement.closest('.disabled') || barElement.classList.contains('invisible')) return;
            if (closeButton && closeButton.contains(e.target)) {
                e.stopPropagation();
                close();
                return;
            }
            if (e.target.closest('.option-button') || e.target.closest('.options-close-btn')) return;
            e.stopPropagation();
            const isActive = barElement.classList.contains('active');
            document.querySelectorAll('.bar-selection.active')
                .forEach(b => b._barClose && b._barClose());
            if (!isActive) {
                barElement.classList.add('active');
                detachDropdown();
                positionDropdown();
                if (dropdown) dropdown.style.display = 'flex';
            }
        });

        barElement._barClose = close;

        // barElement.querySelectorAll('.option-button')
        dropdown.querySelectorAll('.option-button')
            .forEach(button => {
            button.addEventListener('click', (e) => {
                e.stopPropagation();
                onSelectCallback(button);
                close();
            });
        });
    }

    if (binarySelection) {
        setupBarSelection(binarySelection, (button) => {
            selectedBinary = button.dataset.param;
            if (binaryValue) binaryValue.textContent = button.textContent;
            if (binaryApplyButton) binaryApplyButton.dataset.param = button.dataset.param;
        });
    }
    if (binaryApplyButton) binaryApplyButton.addEventListener('click', async(e) => {
        progressButton(binaryApplyButton);
        await new Promise(r => setTimeout(r, 100));
        try {
            const param = binaryApplyButton.dataset.param || 'optimum_bar';
            const cfg = await readConfig();
            cfg.binary = 'optimum_bar';
            cfg.menu_open = (param === 'optimum_vol') ? 'vol' : 'bar';
            await writeConfig(cfg);
            await loadConfigs();
            showNotification('Toggler applied', 'success', 2500);
            dismissProgress(binaryApplyButton);
        } catch (err) {
            console.log(err);
            showNotification('Failed to apply', 'error', 2500);
            dismissProgress(binaryApplyButton);
        }
    });


    if (LogDurationEl) {
        setupBarSelection(LogDurationEl, (button) => {
            if (durationValue) durationValue.textContent = button.textContent;
            if (durationApplyButton) durationApplyButton.dataset.param = button.dataset.param;
        });
    }
    if (durationApplyButton) durationApplyButton.addEventListener('click', async(e) => {
        progressButton(durationApplyButton);
        await new Promise(r => setTimeout(r, 100));
        try {
            const param = durationApplyButton.dataset.param || '1d';
            const out = await execCommand(`
                sed -i 's/"duration": *"[^"]*"/"duration": "${param}"/' /data/adb/modules/inx_mods/webroot/config.json
            `);
            showNotification('Duration applied', 'success', 2500);
            dismissProgress(durationApplyButton);
        } catch (err) {
            console.log(err);
            showNotification('Failed to apply', 'error', 2500);
            dismissProgress(durationApplyButton);
        }
    });

    const driverExecuteButton = document.getElementById("driver-execute-button");

    // Paradise Version (old/new) and Load Method (1=insmod/2=ksud) bars.
    // Old build has no ksud support at all (it never reads an argument),
    // so picking Jailbreak with Old selected is a no-op there -- the hint
    // text under the Load Method bar says so.
    const driverVariantBar   = document.getElementById("driver-paradise-variant-bar");
    const driverVariantValue = document.getElementById("driver-paradise-variant-value");
    let   driverVariant      = 'new';   // default: New
    if (driverVariantBar) {
        setupBarSelection(driverVariantBar, (button) => {
            driverVariant = button.dataset.param || 'new';
            if (driverVariantValue) driverVariantValue.textContent = button.textContent;
        });
    }

    const driverMethodBar   = document.getElementById("driver-load-method-bar");
    const driverMethodValue = document.getElementById("driver-load-method-value");
    let   driverLoadMethod  = '1';      // default: Normal (insmod)
    if (driverMethodBar) {
        setupBarSelection(driverMethodBar, (button) => {
            driverLoadMethod = button.dataset.param || '1';
            if (driverMethodValue) driverMethodValue.textContent = button.textContent;
        });
    }

    // config.json "driver" holds the preset name ("paradise"/"ditpro") picked
    // via the preset bar below. No free-text path is accepted any more --
    // Execute Driver always runs the kernel-matched Paradise loader.

    // Puts the saved driver back on screen on every load. Without this the
    // Preset bar always read "Select" and the Use button carried no param, so
    // a driver had to be picked again every single time the WebUI was opened.
    // A path goes back into the text field; a name is matched against the
    // option buttons already in the page.
    function restoreDriverSelection(saved) {
        const value = (saved || '').trim();
        const isPath = value.charAt(0) === '/';


        // By id, not through the bar: while the dropdown is open it is moved
        // to document.body, so a search inside the bar would find nothing.
        const options = document.getElementById('driver-preset-available-options');
        let match = null;
        if (options && value && !isPath) {
            options.querySelectorAll('.option-button').forEach(b => {
                if (b.dataset.param === value) match = b;
            });
        }
        if (match) {
            if (driverPresetValue) driverPresetValue.textContent = match.textContent;
            if (driverPresetUseButton) driverPresetUseButton.dataset.param = match.dataset.param;
        } else if (driverPresetUseButton && !driverPresetUseButton.dataset.param) {
            // Nothing saved and nothing picked yet. A pick that has not been
            // applied with Use is left alone rather than reset to Select.
            if (driverPresetValue) driverPresetValue.textContent = 'Select';
        }
    }

    // Paradise loader for the running kernel. Two parallel folders:
    //   drivers/paradise_old/  — the original driver build (insmod only,
    //                            no ksud support, 5.10/5.15/6.1/6.12/6.6)
    //   drivers/paradise_new/  — latest build (English, Normal/Jailbreak
    //                            menu, auto-retry on "File exists",
    //                            5.10/5.15/6.1/6.12/6.18/6.6)
    // variant ("old"/"new") picks the folder; method ("1"=insmod/"2"=ksud)
    // is passed as the script's own argv[1] — the Old build never reads
    // argv at all so passing it there is harmless and simply ignored.
    // [BUG FIX] This case was missing 6.12 and 6.18 even though both .ko.sh
    // files are shipped — a device on kernel 6.12 hit "driver not found"
    // here and could never reach the file that actually matches its kernel.
    async function execParadiseForKernel(variant, method) {
        const folder = (variant === 'old') ? 'paradise_old' : 'paradise_new';
        const methodArg = (method === '2') ? '2' : '1';
        const r = await runCommand(`
            KVER=$(uname -r | cut -d. -f1,2)
            echo "KVER=$KVER"
            case "$KVER" in
                5.10|5.15|6.1|6.6|6.12|6.18) ;;
                *) echo "driver not found (kernel $KVER). Note: use ditpro kpm folder path and a patch root need"; exit 4 ;;
            esac
            F="${MODULE_DIR}/drivers/${folder}/$KVER.ko.sh"
            if [ ! -f "$F" ]; then echo "driver not found ($F). Note: use ditpro kpm folder path and a patch root need"; exit 2; fi
            sh "$F" ${methodArg}
        `);
        const out = (r.out || '').trim();
        const err = (r.err || '').trim();
        const kverLine = out.split('\n').find(l => l.startsWith('KVER=')) || '';
        const kver = kverLine.replace('KVER=', '').trim();
        const ok = r.code === 0;
        return { ok, kver, out, err, code: r.code };
    }

    // Shows the real Execute Driver output in the terminal box -- not a
    // toast that disappears. Nothing here is simulated: this is exactly
    // what execParadiseForKernel got back from the real insmod attempt.
    // ANSI color codes the .ko.sh script prints (e.g. "\x1b[0;32m...\x1b[0m")
    // are terminal-only escape sequences; a <pre> in a web page shows them
    // as literal garbage instead of color, so they're stripped here before
    // display -- the text itself is untouched, only the escape bytes go.
    function stripAnsi(text) {
        return (text || '').replace(/\x1b\[[0-9;]*m/g, '');
    }

    const driverTerminal = document.getElementById("driver-terminal");
    function renderDriverTerminal(result) {
        if (!driverTerminal) return;
        const lines = [];
        lines.push(`$ Execute Driver — kernel ${result.kver || 'unknown'}`);
        lines.push('');
        const body = stripAnsi(result.out)
            .split('\n')
            .filter(l => !l.trim().startsWith('KVER='))
            .map(l => l.trimEnd())
            .filter(l => l.length > 0);
        lines.push(...body);
        const errBody = stripAnsi(result.err).trim();
        if (errBody) lines.push(errBody);
        lines.push('');
        lines.push(result.ok
            ? `[OK] Paradise driver EXECUTED for kernel ${result.kver}.`
            : `[FAILED] Paradise driver did NOT execute (exit ${result.code}). Note: use ditpro kpm folder path and a patch root need`);
        driverTerminal.textContent = lines.join('\n');
        driverTerminal.classList.remove('driver-terminal-error', 'driver-terminal-running');
        if (!result.ok) driverTerminal.classList.add('driver-terminal-error');
    }

    if (driverExecuteButton) {
        driverExecuteButton.addEventListener('click', async() => {
            progressButton(driverExecuteButton);
            if (driverTerminal) {
                driverTerminal.textContent = '$ Execute Driver — running...';
                driverTerminal.classList.remove('driver-terminal-error');
                driverTerminal.classList.add('driver-terminal-running');
            }
            await new Promise(r => setTimeout(r, 50));
            try {
                const result = await execParadiseForKernel(driverVariant, driverLoadMethod);
                renderDriverTerminal(result);
                if (result.ok) {
                    showNotification(`Paradise loaded for kernel ${result.kver}`, 'success', 2500);
                } else {
                    showNotification('Paradise driver not executed. See terminal below.', 'error', 3000);
                }
            } catch (e) {
                console.log(e);
                if (driverTerminal) {
                    driverTerminal.textContent = `[FAILED] ${String(e && e.message ? e.message : e)}\nNote: use ditpro kpm folder path and a patch root need`;
                    driverTerminal.classList.remove('driver-terminal-running');
                    driverTerminal.classList.add('driver-terminal-error');
                }
                showNotification(String(e && e.message ? e.message : e), 'error', 3000);
            } finally {
                dismissProgress(driverExecuteButton);
            }
        });
    }

    const driverPresetSelection = document.getElementById("driver-preset-selection-bar");
    const driverPresetValue = document.getElementById("driver-preset-selected-value");
    const driverPresetUseButton = document.getElementById("driver-preset-use-button");

    if (driverPresetSelection) {
        setupBarSelection(driverPresetSelection, (button) => {
            if (driverPresetValue) driverPresetValue.textContent = button.textContent;
            if (driverPresetUseButton) driverPresetUseButton.dataset.param = button.dataset.param;
        });
    }
    // Reads the device kernel release (uname -r) as {major, minor, raw}.
    async function getKernelVersion() {
        const r = await runCommand('uname -r');
        const raw = (r.out || '').trim();
        const m = raw.match(/^(\d+)\.(\d+)/);
        return m ? { major: +m[1], minor: +m[2], raw } : null;
    }

    // True when kernel major.minor sits between from and to, both inclusive.
    function kernelInRange(kv, fromMaj, fromMin, toMaj, toMin) {
        const v = kv.major * 1000 + kv.minor;
        return v >= fromMaj * 1000 + fromMin && v <= toMaj * 1000 + toMin;
    }

    if (driverPresetUseButton) driverPresetUseButton.addEventListener('click', async() => {
        const driverName = driverPresetUseButton.dataset.param || '';
        if (!driverName) {
            showNotification('Select a driver first', 'error', 2500);
            return;
        }
        progressButton(driverPresetUseButton);
        await new Promise(r => setTimeout(r, 100));
        try {
            // Kernel gate: Paradise runs on kernel 5.10 through 6.18.
            // [BUG FIX] Upper bound was 6.6, which rejected 6.12/6.18 even
            // though drivers/paradise/ ships a .ko.sh for both — raised to
            // match the actual files shipped.
            // Checked here, before anything is saved, so an unsupported
            // kernel never gets Paradise written into config.json.
            const kv = await getKernelVersion();
            if (!kv) throw new Error('kernel unknown');
            if (driverName === 'paradise' && !kernelInRange(kv, 5, 10, 6, 18)) {
                showNotification(`Paradise sirf kernel 5.10 se 6.18 tak chalta hai (aapka: ${kv.raw})`, 'error', 4000);
                return;
            }
            if (kernelInRange(kv, 4, 4, 5, 4)) {
                showNotification(`Kernel ${kv.raw}: KPM recommended`, 'info', 3500);
            }
            // Selecting a preset writes its name into config.json "driver".
            // The binary reads that key itself and picks the backend, so no
            // external file is executed here.
            const cfg = await readConfig();
            cfg.driver = driverName;
            await writeConfig(cfg);
            showNotification(`Driver set: ${driverName}`, 'success', 2500);
        } catch (e) {
            console.log(e);
            showNotification('Failed to set driver', 'error', 2500);
        } finally {
            dismissProgress(driverPresetUseButton);
        }
    });

    // Reads a plain-text log from the module webroot. Lines look like
    // "[HH:MM:SS] message" (daemon, watchdog); anything else (binary output
    // in cheat.log) is shown without a time. Returns newest-last entries.
    async function fetchLogLines(file) {
        try {
            const res = await fetch(`${file}?t=${Date.now()}`, { cache: "no-store" });
            if (!res.ok) return [];
            const text = await res.text();
            return text
                .split('\n')
                .filter(line => line.trim() !== '')
                .map(line => {
                    const m = line.match(/^\[(\d{2}:\d{2}:\d{2})\]\s?(.*)$/);
                    return m ? { time: m[1], message: m[2] } : { time: '', message: line };
                });
        } catch (e) {
            return [];
        }
    }

    // textContent (never innerHTML) so log text can't inject markup.
    function renderLogList(listId, entries) {
        const list = document.getElementById(listId);
        if (!list) return;
        list.innerHTML = "";
        if (entries.length === 0) {
            const empty = document.createElement('div');
            empty.className = 'log-item';
            empty.textContent = 'No logs yet';
            list.appendChild(empty);
            return;
        }
        entries.slice().reverse().forEach(entry => {
            const item = document.createElement('div');
            item.className = 'log-item';
            const header = document.createElement('div');
            header.className = 'log-header';
            const time = document.createElement('span');
            time.className = 'log-time';
            time.textContent = entry.time;
            header.appendChild(time);
            const msg = document.createElement('span');
            msg.className = 'log-message';
            msg.textContent = entry.message;
            item.appendChild(header);
            item.appendChild(msg);
            list.appendChild(item);
        });
    }

    // Daemon Logs = boot.log + daemon.log + watchdog.log, merged by time,
    // newest first. boot.log is what makes a failure at boot visible: without
    // it, "the service did not start" looks the same as "nothing happened".
    async function loadDaemonLogs() {
        const [boot, daemon, watchdog] = await Promise.all([
            fetchLogLines('boot.log'),
            fetchLogLines('daemon.log'),
            fetchLogLines('watchdog.log')
        ]);
        const all = boot.concat(daemon, watchdog)
            .sort((a, b) => a.time.localeCompare(b.time));
        renderLogList('daemon-logs-list', all);
    }

    // Cheat Logs = output of the launched binary (cheat.log).
    async function loadCheatLogs() {
        renderLogList('cheat-logs-list', await fetchLogLines('cheat.log'));
    }

    // Keep both panels live while the WebUI is open.
    setInterval(() => {
        loadDaemonLogs();
        loadCheatLogs();
        updateHeroCard(null);
    }, 5000);

    async function reloadAll() {
        await updateHeroCard();
        await loadConfigs();
        await loadDaemonLogs();
        await loadCheatLogs();
    }    
    await reloadAll();



    // Like execCommand, but always resolves with {code, out, err} so the
    // caller can read the output of a failing command too (the login binary
    // exits 1 on a wrong key but still prints its JSON reason to stdout).
    // One shell call, with an upper bound on how long it may take.
    //
    // ksu.exec takes a callback. If that callback never fires the promise
    // never settles and the page sits on a spinner for ever — that is what
    // "stuck" looked like after pressing Install. The timer turns a silent
    // hang into a normal rejection the caller can report.
    function ksuExec(command) {
        return new Promise((resolve, reject) => {
            if (typeof ksu !== "object" || typeof ksu.exec !== "function") {
                reject("KernelSU exec not available");
                return;
            }

            let timer = null;
            const cb = `cb_${Date.now()}_${Math.floor(Math.random() * 1000)}`;

            const finish = (fn, value) => {
                if (timer !== null) { clearTimeout(timer); timer = null; }
                delete window[cb];
                fn(value);
            };

            window[cb] = (code, out, err) => finish(resolve, {
                code: Number(code),
                out: out || "",
                err: err || ""
            });

            timer = setTimeout(() => {
                if (window[cb]) {
                    finish(reject, `timed out after ${EXEC_TIMEOUT_MS / 1000}s`);
                }
            }, EXEC_TIMEOUT_MS);

            try {
                ksu.exec(command, "{}", cb);
            } catch (error) {
                finish(reject, error);
            }
        });
    }

    // Always resolves with {code, out, err} so the caller can read the output
    // of a failing command too (the login binary exits 1 on a wrong key but
    // still prints its JSON reason to stdout).
    function runCommand(command) {
        return ksuExec(command);
    }

    // Resolves with stdout, rejects when the command exits non-zero.
    async function execCommand(command) {
        const r = await ksuExec(command);
        if (r.code !== 0) throw (r.err || "Command failed");
        return r.out;
    }

    // Service status comes from the heartbeat the daemon writes into
    // webroot/status.json on every poll, read with fetch().
    //
    // It used to be a shell call, and the status card refreshed on a timer,
    // so the page fired a shell command every few seconds. Those queue up in
    // the WebUI bridge and the one behind them -- the Install command -- never
    // got its answer, which is what froze the page. Nothing on a timer runs a
    // shell command any more.
    //
    // (`pidof daemon_service_@api` could never have worked either: the kernel
    // truncates /proc/<pid>/comm to 15 characters and that name is 19.)
    async function readStatus() {
        try {
            const res = await fetch(`status.json?t=${Date.now()}`, { cache: "no-store" });
            if (!res.ok) return null;
            return await res.json();
        } catch (e) {
            return null;
        }
    }

    async function isDaemonRunning() {
        const st = await readStatus();
        if (!st || typeof st.updated !== "number") return false;
        const age = Math.floor(Date.now() / 1000) - st.updated;
        return age >= 0 && age <= HEARTBEAT_MAX_AGE_SECS;
    }

    async function isCheatRunning(binary) {
        const pidFile = `${MODULE_DIR}/config/${binary}.pid`;
        const script =
            `p=$(cat ${shQuote(pidFile)} 2>/dev/null); ` +
            `if [ -n "$p" ] && [ -r "/proc/$p/cmdline" ] && ` +
            `tr '\\0' '\\n' < "/proc/$p/cmdline" | grep -q ${shQuote(binary)}; ` +
            `then echo yes; else echo no; fi`;
        try {
            const out = await execCommand(script);
            return out.trim() === "yes";
        } catch (e) {
            return false;
        }
    }

});