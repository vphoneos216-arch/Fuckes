/* ===========================================================
   INVISIBLEX — WebUI logic
   Talks to kd_ctl (a compiled native binary) exclusively through the
   root manager's exec bridge (window.ksu.exec / window.apatch.exec).
   No real control logic lives in this file — it only renders state
   and forwards button presses to kd_ctl, which does the actual
   login check / start / stop / driver-write / watcher work natively.
   =========================================================== */

const KD_CTL = "/data/adb/modules/invisiblex_loader/bin/kd_ctl";
const REMOTE_PANEL_URL = "https://adminshop.myvipsite.fun/kd/agent_panel.php";

const DRIVERS = [
  { id: "auto",     name: "Auto",     sub: "Detect automatically" },
  { id: "qx1",      name: "QX1",      sub: "Kernel-level" },
  { id: "gt",       name: "GT",       sub: "Kernel-level" },
  { id: "rt-dev",   name: "RT-DEV",   sub: "/dev read/write" },
  { id: "rt-proc",  name: "RT-PROC",  sub: "/proc read/write" },
  { id: "qx2",      name: "QX2",      sub: "Kernel-level" },
  { id: "tb-kpm",   name: "TB-KPM",   sub: "KernelPatch module" },
  { id: "ditpro",   name: "DitPro",   sub: "KernelPatch module" },
  { id: "kma",      name: "KMA-KPM",  sub: "KernelPatch module" },
  { id: "ldk",      name: "LDK-KPM",  sub: "KernelPatch module" },
  { id: "paradise", name: "Paradise", sub: "KernelPatch module" },
  { id: "bing",     name: "Bing",     sub: "/proc/pid/mem read/write" },
  { id: "ditkpm",   name: "Dit-KPM/Dits-KO", sub: "Kernel module (auto: syscall or ioctl)" },
];

/* ---------- tiny WebAudio sound effects (no asset files needed) ---------- */
let audioCtx = null;
function actx(){
  if (!audioCtx) { try { audioCtx = new (window.AudioContext || window.webkitAudioContext)(); } catch (e) {} }
  return audioCtx;
}
function beep(freq, dur, type, vol){
  const ctx = actx(); if (!ctx) return;
  const osc = ctx.createOscillator(), gain = ctx.createGain();
  osc.type = type || "sine"; osc.frequency.value = freq;
  gain.gain.value = vol || 0.06;
  osc.connect(gain); gain.connect(ctx.destination);
  const t = ctx.currentTime;
  gain.gain.setValueAtTime(gain.gain.value, t);
  gain.gain.exponentialRampToValueAtTime(0.0001, t + dur);
  osc.start(t); osc.stop(t + dur);
}
const sfx = {
  tap:   () => beep(520, .05, "square", .04),
  ok:    () => { beep(660, .09, "sine", .06); setTimeout(() => beep(880, .12, "sine", .05), 90); },
  err:   () => beep(180, .18, "sawtooth", .05),
  start: () => { beep(440, .08, "triangle", .06); setTimeout(() => beep(660, .14, "triangle", .06), 80); },
  stop:  () => { beep(500, .10, "triangle", .05); setTimeout(() => beep(300, .16, "triangle", .05), 70); },
};

const $ = (sel) => document.querySelector(sel);

function toast(msg){
  const t = $("#toast");
  t.textContent = msg;
  t.classList.add("show");
  clearTimeout(t._h);
  t._h = setTimeout(() => t.classList.remove("show"), 2200);
}

/* ---------- activity log (local, rolling, last 12 entries) ---------- */
function logEvent(text){
  let log = [];
  try { log = JSON.parse(localStorage.getItem("kd_log") || "[]"); } catch (e) {}
  log.unshift({ t: text, at: Date.now() });
  log = log.slice(0, 12);
  localStorage.setItem("kd_log", JSON.stringify(log));
  renderLog();
}
function renderLog(){
  let log = [];
  try { log = JSON.parse(localStorage.getItem("kd_log") || "[]"); } catch (e) {}
  const el = $("#activityLog");
  if (!log.length) { el.innerHTML = '<li class="log-empty">No activity yet.</li>'; return; }
  el.innerHTML = "";
  log.forEach((e) => {
    const li = document.createElement("li");
    const d = new Date(e.at);
    const hh = String(d.getHours()).padStart(2, "0"), mm = String(d.getMinutes()).padStart(2, "0");
    li.innerHTML = `<span class="ldot"></span><span class="ltxt"></span><span class="ltime">${hh}:${mm}</span>`;
    li.querySelector(".ltxt").textContent = e.t;
    el.appendChild(li);
  });
}

/* ---------- root-WebUI exec bridge (KernelSU / APatch) ---------- */
function kdHasBridge(){
  return !!(window.ksu && window.ksu.exec) || !!(window.apatch && window.apatch.exec);
}
async function kdExec(cmd){
  if (window.ksu && window.ksu.exec) return await window.ksu.exec(cmd);
  if (window.apatch && window.apatch.exec) return await window.apatch.exec(cmd);
  throw new Error("no-bridge");
}
async function kd(args){
  if (!kdHasBridge()) return { ok:false, errno:-1, stdout:"", stderr:"no-bridge" };
  const r = await kdExec(KD_CTL + " " + args);
  let j = {};
  try { j = JSON.parse((r.stdout || "").trim().split("\n").pop()); } catch (e) {}
  return Object.assign({ errno: r.errno, stderr: r.stderr }, j);
}

/* ---------- clock ---------- */
function tickClock(){
  const d = new Date();
  let h = d.getHours(); const ampm = h >= 12 ? "PM" : "AM"; h = h % 12; if (h === 0) h = 12;
  const m = String(d.getMinutes()).padStart(2, "0");
  $("#clock").textContent = `${h}:${m} ${ampm}`;
}

/* ---------- state ---------- */
let pollTimer = null;
let allApps = [];
let lastStatus = {};

function showLogin(){
  $("#view-login").classList.remove("hidden");
  $("#shell").classList.add("hidden");
  if (pollTimer) { clearInterval(pollTimer); pollTimer = null; }
}
function showShell(){
  $("#view-login").classList.add("hidden");
  $("#shell").classList.remove("hidden");
  renderDriverList();
  renderLog();
  refreshStatus();
  loadApps();
  tickClock();
  setInterval(tickClock, 15000);
  pollTimer = setInterval(refreshStatus, 4000);
}

function setMsg(text, cls){
  const m = $("#loginMsg");
  m.textContent = text || "";
  m.className = "msg" + (cls ? " " + cls : "");
}

async function doLogin(){
  sfx.tap();
  const input = $("#keyInput");
  const key = input.value.trim().toUpperCase();
  if (!key) {
    setMsg("Enter your license key.", "err"); sfx.err();
    const card = $(".login-card"); card.classList.add("shake"); setTimeout(() => card.classList.remove("shake"), 400);
    return;
  }
  if (!kdHasBridge()) {
    setMsg("Open this page from the KernelSU/APatch manager app (not a normal browser).", "err");
    sfx.err();
    return;
  }
  const btn = $("#loginBtn");
  btn.classList.add("loading"); btn.disabled = true;
  setMsg("Checking with server…");
  const r = await kd("login " + key.replace(/[^A-Za-z0-9-]/g, ""));
  btn.classList.remove("loading"); btn.disabled = false;
  if (r.ok) {
    setMsg(""); sfx.ok();
    localStorage.setItem("kd_logged_in", "1");
    logEvent("License verified");
    showShell();
  } else {
    setMsg(r.error || "Login failed. Please try again.", "err");
    sfx.err();
    const card = $(".login-card"); card.classList.add("shake"); setTimeout(() => card.classList.remove("shake"), 400);
  }
}

function logout(){
  sfx.tap();
  localStorage.removeItem("kd_logged_in");
  $("#keyInput").value = "";
  showLogin();
}

async function refreshStatus(){
  const r = await kd("status");
  lastStatus = r;
  const bridgeOk = kdHasBridge();
  $("#avatarDot").classList.toggle("on", bridgeOk && !!r.running);

  // ---- chips ----
  $("#chipConnDot").className = "chip-ic " + (bridgeOk ? "ok" : "bad");
  $("#chipConn").textContent = bridgeOk ? "Online" : "No bridge";
  $("#chipKeyDot").className = "chip-ic " + (r.has_saved_key ? "ok" : "bad");
  $("#chipKey").textContent = r.has_saved_key ? "Active" : "Invalid";
  $("#chipRunDot").className = "chip-ic " + (r.running ? "ok" : "bad");
  const driverMeta = DRIVERS.find((d) => d.id === r.driver);
  $("#chipDriver").textContent = driverMeta ? driverMeta.name : (r.driver || "Auto");

  // ---- hero ----
  const toggleBtn = $("#toggleBtn"), toggleLabel = $("#toggleBtnLabel"), heroSub = $("#heroSub");
  toggleBtn.style.display = r.watcher_running ? "none" : "";
  if (r.running) {
    toggleLabel.textContent = "Stop";
    heroSub.textContent = r.watcher_running ? "Running — launched automatically for the target app." : "Overlay is live on this device.";
  } else {
    toggleLabel.textContent = "Start";
    heroSub.textContent = r.watcher_running ? "Standing by — waiting for the target app to open." : "Discipline turns plans into reality.";
  }

  // ---- overview panel ----
  const found = allApps.find((a) => a.package === r.target_package);
  $("#ovTarget").textContent = r.target_package ? (found ? found.name : r.target_package) : "None";
  $("#ovAuto").textContent = r.watcher_running ? "On" : "Off";
  $("#ovControl").textContent = (localStorage.getItem("kd_mode") || "local") === "remote" ? "Remote" : "Local";
  $("#ovStatus").textContent = r.running ? "Operational" : (r.watcher_running ? "Standing by" : "Idle");

  $("#dbName").textContent = driverMeta ? driverMeta.name : (r.driver || "Auto");

  // ---- driver tab list state ----
  document.querySelectorAll(".driver-opt").forEach((el) => {
    el.classList.toggle("active", el.dataset.id === (r.driver || "auto"));
  });

  // ---- settings tab ----
  $("#autoToggle").checked = !!r.watcher_running;
  if (r.menu_open) {
    $("#segBar").classList.toggle("active", r.menu_open === "bar");
    $("#segVol").classList.toggle("active", r.menu_open === "vol");
  }
  const tname = $("#targetName"), tpkg = $("#targetPkg");
  if (r.target_package) {
    tname.textContent = found ? found.name : r.target_package;
    tpkg.textContent = r.target_package;
  } else {
    tname.textContent = "Not selected";
    tpkg.textContent = "Tap to choose an app";
  }
  $("#acctStatus").textContent = r.has_saved_key ? "Verified" : "Not verified";
}

async function toggleRun(){
  sfx.tap();
  const btn = $("#toggleBtn");
  const running = $("#toggleBtnLabel").textContent.trim() === "Stop";
  btn.disabled = true;
  await kd(running ? "stop" : "start");
  running ? sfx.stop() : sfx.start();
  logEvent(running ? "Stopped manually" : "Started manually");
  btn.disabled = false;
  refreshStatus();
}

async function toggleAuto(on){
  sfx.tap();
  if (on) {
    const r = await kd("status");
    if (!r.target_package) {
      toast("Pick a target app first");
      $("#autoToggle").checked = false;
      openAppPicker();
      return;
    }
    await kd("watch");
    toast("Auto mode on — waiting for the target app");
    logEvent("Auto mode enabled");
  } else {
    await kd("watch-stop");
    toast("Auto mode off");
    logEvent("Auto mode disabled");
  }
  refreshStatus();
}

async function setDriver(id){
  sfx.tap();
  await kd("set-driver " + id);
  const meta = DRIVERS.find((d) => d.id === id);
  logEvent("Driver set to " + (meta ? meta.name : id));
  refreshStatus();
}
async function setMenu(mode){
  sfx.tap();
  $("#segBar").classList.toggle("active", mode === "bar");
  $("#segVol").classList.toggle("active", mode === "vol");
  await kd("set-menu " + mode);
  logEvent("Menu-open mode: " + (mode === "bar" ? "Bottom bar" : "Volume key"));
}

/* ---------- driver tab list ---------- */
function renderDriverList(){
  const el = $("#driverList");
  el.innerHTML = "";
  DRIVERS.forEach((d) => {
    const row = document.createElement("div");
    row.className = "driver-opt";
    row.dataset.id = d.id;
    row.innerHTML = `
      <div>
        <div class="driver-opt-name"></div>
        <div class="driver-opt-id"></div>
      </div>
      <span class="driver-opt-check"></span>`;
    row.querySelector(".driver-opt-name").textContent = d.name;
    row.querySelector(".driver-opt-id").textContent = d.sub;
    row.addEventListener("click", () => setDriver(d.id));
    el.appendChild(row);
  });
}

/* ---------- app picker ---------- */
async function loadApps(){
  const r = await kd("list-apps");
  allApps = (r.apps || []).slice().sort((a, b) => a.name.localeCompare(b.name));
  renderApps(allApps);
  refreshStatus();
}
function renderApps(apps){
  const list = $("#appList");
  if (!apps.length) { list.innerHTML = '<div class="empty-note">No apps found.</div>'; return; }
  const frag = document.createDocumentFragment();
  apps.forEach((a) => {
    const item = document.createElement("div");
    item.className = "app-item";
    item.innerHTML =
      `<div class="app-ic"></div>
       <div class="app-meta"><div class="n"></div><div class="p"></div></div>`;
    item.querySelector(".app-ic").textContent = (a.name || "?").charAt(0).toUpperCase();
    item.querySelector(".n").textContent = a.name;
    item.querySelector(".p").textContent = a.package;
    item.addEventListener("click", () => pickApp(a.package, a.name));
    frag.appendChild(item);
  });
  list.innerHTML = "";
  list.appendChild(frag);
}
function filterApps(q){
  q = q.trim().toLowerCase();
  if (!q) { renderApps(allApps); return; }
  renderApps(allApps.filter((a) => a.name.toLowerCase().includes(q) || a.package.toLowerCase().includes(q)));
}
function openAppPicker(){
  sfx.tap();
  $("#sheetOverlay").classList.add("open");
  $("#appSearch").value = "";
  renderApps(allApps);
}
function closeAppPicker(){ sfx.tap(); $("#sheetOverlay").classList.remove("open"); }
async function pickApp(pkg, name){
  sfx.ok();
  await kd("set-target " + pkg);
  closeAppPicker();
  refreshStatus();
  toast("Target app set");
  logEvent("Target app: " + (name || pkg));
}

/* ---------- local / remote mode ---------- */
function setMode(mode){
  sfx.tap();
  $("#segLocal").classList.toggle("active", mode === "local");
  $("#segRemote").classList.toggle("active", mode === "remote");
  const link = $("#remoteLink");
  if (mode === "remote") {
    link.classList.remove("hidden");
    link.href = REMOTE_PANEL_URL;
    link.textContent = REMOTE_PANEL_URL;
  } else {
    link.classList.add("hidden");
  }
  localStorage.setItem("kd_mode", mode);
  refreshStatus();
}

/* ---------- tabs ---------- */
function switchTab(tab){
  sfx.tap();
  document.querySelectorAll(".tab-btn").forEach((b) => b.classList.toggle("active", b.dataset.tab === tab));
  document.querySelectorAll(".view").forEach((v) => v.classList.toggle("hidden", v.id !== "tab-" + tab));
}

/* ---------- wire up events ---------- */
function bind(){
  $("#loginBtn").addEventListener("click", doLogin);
  $("#keyInput").addEventListener("keydown", (e) => { if (e.key === "Enter") doLogin(); });
  $("#logoutBtn").addEventListener("click", logout);
  $("#toggleBtn").addEventListener("click", toggleRun);
  $("#autoToggle").addEventListener("change", (e) => toggleAuto(e.target.checked));
  $("#targetBox").addEventListener("click", openAppPicker);
  $("#sheetClose").addEventListener("click", closeAppPicker);
  $("#sheetOverlay").addEventListener("click", (e) => { if (e.target === e.currentTarget) closeAppPicker(); });
  $("#appSearch").addEventListener("input", (e) => filterApps(e.target.value));
  $("#clearLogBtn").addEventListener("click", () => { localStorage.setItem("kd_log", "[]"); renderLog(); sfx.tap(); });
  $("#goDriverBtn").addEventListener("click", () => switchTab("driver"));
  document.querySelectorAll(".seg-opt[data-mode]").forEach((b) => b.addEventListener("click", () => setMode(b.dataset.mode)));
  document.querySelectorAll(".seg-opt[data-menu]").forEach((b) => b.addEventListener("click", () => setMenu(b.dataset.menu)));
  document.querySelectorAll(".tab-btn").forEach((b) => b.addEventListener("click", () => switchTab(b.dataset.tab)));
}

/* ---------- boot ---------- */
(function init(){
  bind();
  const savedMode = localStorage.getItem("kd_mode") || "local";
  $("#segLocal").classList.toggle("active", savedMode === "local");
  $("#segRemote").classList.toggle("active", savedMode === "remote");
  if (savedMode === "remote") {
    $("#remoteLink").classList.remove("hidden");
    $("#remoteLink").href = REMOTE_PANEL_URL;
    $("#remoteLink").textContent = REMOTE_PANEL_URL;
  }
  if (localStorage.getItem("kd_logged_in") === "1") {
    showShell();
  } else {
    showLogin();
  }
})();
