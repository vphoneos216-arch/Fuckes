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

function toast(msg){
  const t = $("#toast");
  t.textContent = msg;
  t.classList.add("show");
  clearTimeout(t._h);
  t._h = setTimeout(() => t.classList.remove("show"), 2200);
}

const $ = (sel) => document.querySelector(sel);

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

/* ---------- state ---------- */
let pollTimer = null;
let allApps = [];

function showLogin(){
  $("#view-login").style.display = "flex";
  $("#view-home").style.display = "none";
  if (pollTimer) { clearInterval(pollTimer); pollTimer = null; }
}
function showHome(){
  $("#view-login").style.display = "none";
  $("#view-home").style.display = "flex";
  refreshStatus();
  loadApps();
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
  if (!key) { setMsg("Enter your license key.", "err"); sfx.err(); input.parentElement.classList.add("shake"); setTimeout(() => input.parentElement.classList.remove("shake"), 400); return; }
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
    showHome();
  } else {
    setMsg(r.error || "Login failed. Please try again.", "err");
    sfx.err();
    const card = document.querySelector("#view-login .card");
    card.classList.add("shake"); setTimeout(() => card.classList.remove("shake"), 400);
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
  const pill = $("#connPill");
  if (!kdHasBridge()) {
    pill.textContent = "NO BRIDGE"; pill.className = "pill pill-off";
    return;
  }
  pill.textContent = "CONNECTED"; pill.className = "pill pill-on";

  const dot = $("#statusDot"), title = $("#statusTitle"), sub = $("#statusSub");
  const toggleBtn = $("#toggleBtn"), autoToggle = $("#autoToggle");

  autoToggle.checked = !!r.watcher_running;
  toggleBtn.style.display = r.watcher_running ? "none" : "";

  if (r.running) {
    dot.className = "dot live";
    title.textContent = "Running";
    sub.textContent = r.watcher_running ? "Launched automatically — target app is open" : "Overlay is live on this device";
    toggleBtn.textContent = "Stop";
    toggleBtn.className = "btn btn-stop pill-btn";
  } else {
    dot.className = r.watcher_running ? "dot alert" : "dot";
    title.textContent = r.watcher_running ? "Standing by" : "Stopped";
    sub.textContent = r.watcher_running
      ? "Waiting for the target app to open"
      : (r.has_saved_key ? "Ready to start" : "No saved license — login again");
    toggleBtn.textContent = "Start";
    toggleBtn.className = "btn btn-start pill-btn";
  }

  if (r.driver) $("#driverSelect").value = r.driver;
  if (r.menu_open) {
    $("#segBar").classList.toggle("active", r.menu_open === "bar");
    $("#segVol").classList.toggle("active", r.menu_open === "vol");
  }

  const tname = $("#targetName"), tpkg = $("#targetPkg");
  if (r.target_package) {
    const found = allApps.find((a) => a.package === r.target_package);
    tname.textContent = found ? found.name : r.target_package;
    tpkg.textContent = r.target_package;
  } else {
    tname.textContent = "Not selected";
    tpkg.textContent = "Tap to choose an app";
  }
}

async function toggleRun(){
  sfx.tap();
  const btn = $("#toggleBtn");
  const running = btn.textContent.trim() === "Stop";
  btn.disabled = true;
  await kd(running ? "stop" : "start");
  running ? sfx.stop() : sfx.start();
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
  } else {
    await kd("watch-stop");
    toast("Auto mode off");
  }
  refreshStatus();
}

async function setDriver(name){ sfx.tap(); await kd("set-driver " + name); }
async function setMenu(mode){
  sfx.tap();
  $("#segBar").classList.toggle("active", mode === "bar");
  $("#segVol").classList.toggle("active", mode === "vol");
  await kd("set-menu " + mode);
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
      `<div class="app-ic">${(a.name || "?").charAt(0).toUpperCase()}</div>
       <div class="app-meta">
         <div class="n"></div>
         <div class="p"></div>
       </div>`;
    item.querySelector(".n").textContent = a.name;
    item.querySelector(".p").textContent = a.package;
    item.addEventListener("click", () => pickApp(a.package));
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
async function pickApp(pkg){
  sfx.ok();
  await kd("set-target " + pkg);
  closeAppPicker();
  refreshStatus();
  toast("Target app set");
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
  $("#driverSelect").addEventListener("change", (e) => setDriver(e.target.value));
  document.querySelectorAll(".seg-opt[data-mode]").forEach((b) => b.addEventListener("click", () => setMode(b.dataset.mode)));
  document.querySelectorAll(".seg-opt[data-menu]").forEach((b) => b.addEventListener("click", () => setMenu(b.dataset.menu)));
}

/* ---------- boot ---------- */
(function init(){
  bind();
  const savedMode = localStorage.getItem("kd_mode") || "local";
  setMode(savedMode);
  if (localStorage.getItem("kd_logged_in") === "1") {
    showHome();
  } else {
    showLogin();
  }
})();
