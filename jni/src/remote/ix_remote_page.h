#pragma once
// The remote control page, served from memory by ix_remote.h.
// Designed and tested against a mock of the same API before being
// embedded, so the layout and the control set are already verified.

static const char* const IX_REMOTE_PAGE = R"IXPAGE(
<!doctype html>
<html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>InvisibleX Remote</title>
<style>
:root{
  --accent:#E8263A; --accent-hi:#FF4A5C; --ember:#FF6E50; --accent-deep:#961220;
  --bg:#0B0B0E; --card:#16161B; --raise:rgba(245,245,248,.05);
  --hair:rgba(245,245,248,.09); --txt:#F5F5F8; --mid:#A8A8B2; --low:#70707A;
  --ok:#76C4A0;
}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
html,body{margin:0;background:var(--bg);color:var(--txt);
  font:500 15px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
body{padding:0 0 32px;max-width:720px;margin:0 auto}
header{position:sticky;top:0;z-index:10;background:rgba(11,11,14,.94);
  backdrop-filter:blur(12px);border-bottom:1px solid var(--hair);padding:14px 16px 0}
.brand{display:flex;align-items:center;gap:10px;margin-bottom:12px}
.mark{width:34px;height:34px;flex:0 0 34px;position:relative}
.mark svg{display:block}
.name{font:700 17px/1 system-ui;letter-spacing:.2px}
.name i{font-style:normal;color:var(--accent)}
.sub{font:600 10.5px/1 system-ui;color:var(--low);margin-top:4px;letter-spacing:.6px}
.pill{margin-left:auto;display:flex;align-items:center;gap:6px;padding:5px 11px;border-radius:13px;
  background:rgba(118,196,160,.13);border:1px solid rgba(118,196,160,.38);
  font:700 10.5px/1 system-ui;color:var(--ok)}
.dot{width:6px;height:6px;border-radius:50%;background:var(--ok)}
nav{display:flex;gap:6px;overflow-x:auto;padding-bottom:10px;scrollbar-width:none}
nav::-webkit-scrollbar{display:none}
nav button{flex:0 0 auto;border:1px solid var(--hair);background:rgba(245,245,248,.05);
  color:var(--mid);font:600 13px/1 system-ui;padding:9px 16px;border-radius:17px;cursor:pointer;
  transition:color .18s,border-color .18s}
nav button.on{color:#fff;border-color:transparent;
  background:linear-gradient(90deg,var(--accent),var(--ember));
  box-shadow:0 0 14px rgba(232,38,58,.45)}
main{padding:16px}
section{display:none}section.on{display:block}
.row{display:flex;align-items:center;gap:14px;background:var(--raise);
  border:1px solid var(--hair);border-radius:13px;padding:14px 16px;margin-bottom:10px}
.row .lab{flex:1;min-width:0}
.row .t{font:600 14.5px/1.25 system-ui}
.row .d{font:500 11.5px/1.3 system-ui;color:var(--low);margin-top:3px}
/* switch */
.sw{flex:0 0 auto;width:50px;height:29px;border-radius:15px;background:#26262E;
  border:none;padding:0;position:relative;cursor:pointer;transition:background .22s}
.sw::after{content:"";position:absolute;top:3.5px;left:3.5px;width:22px;height:22px;
  border-radius:11px;background:#fff;
  transition:left .22s cubic-bezier(.22,.9,.3,1),width .22s cubic-bezier(.22,.9,.3,1)}
.sw[aria-checked="true"]{background:linear-gradient(90deg,var(--accent),var(--ember));
  box-shadow:0 0 12px rgba(232,38,58,.5)}
.sw[aria-checked="true"]::after{left:24.5px}
.sw:active::after{width:28px}
.sw[aria-checked="true"]:active::after{left:18.5px}
/* slider */
.sl{background:var(--raise);border:1px solid var(--hair);border-radius:13px;
  padding:14px 16px 18px;margin-bottom:10px}
.sl .top{display:flex;align-items:baseline;margin-bottom:12px}
.sl .t{font:600 14.5px/1 system-ui;flex:1}
.sl .v{font:700 13px/1 system-ui;color:var(--accent)}
input[type=range]{-webkit-appearance:none;appearance:none;width:100%;height:5px;border-radius:3px;
  background:rgba(245,245,248,.13);outline:none;margin:0}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:18px;height:18px;border-radius:50%;
  background:#fff;box-shadow:0 0 10px rgba(232,38,58,.65);cursor:pointer}
input[type=range]::-moz-range-thumb{width:18px;height:18px;border:none;border-radius:50%;background:#fff}
/* segmented */
.seg{display:flex;gap:8px;margin-top:2px}
.seg button{flex:1;border:1px solid var(--hair);background:rgba(245,245,248,.05);color:var(--mid);
  font:600 13px/1 system-ui;padding:11px 0;border-radius:11px;cursor:pointer;transition:all .18s}
.seg button.on{color:#fff;border-color:transparent;
  background:linear-gradient(90deg,var(--accent),var(--ember));box-shadow:0 0 12px rgba(232,38,58,.4)}
/* info */
.info{background:var(--raise);border:1px solid var(--hair);border-radius:13px;overflow:hidden}
.info div{display:flex;padding:13px 16px;border-bottom:1px solid var(--hair);font-size:13.5px}
.info div:last-child{border-bottom:none}
.info b{font-weight:600;color:var(--mid);flex:1}
.info span{font-weight:600}
.hint{font:500 11.5px/1.5 system-ui;color:var(--low);margin:14px 2px 0}
#toast{position:fixed;left:50%;bottom:22px;transform:translateX(-50%) translateY(90px);
  background:#1C1C22;border:1px solid var(--hair);color:var(--txt);padding:11px 18px;border-radius:12px;
  font:600 13px/1 system-ui;transition:transform .26s;z-index:50;box-shadow:0 8px 26px rgba(0,0,0,.5)}
#toast.on{transform:translateX(-50%) translateY(0)}
#toast.bad{border-color:rgba(232,38,58,.55);color:var(--accent-hi)}
/* login gate */
#gate{display:none;padding:34px 16px 0}
#gate.on{display:block}
.lcard{background:var(--card);border:1px solid var(--hair);border-radius:16px;padding:22px 18px 20px;
  box-shadow:0 10px 34px rgba(0,0,0,.42)}
.lcard h2{font:700 16px/1 system-ui;margin:0 0 6px}
.lcard p{font:500 12.5px/1.55 system-ui;color:var(--low);margin:0 0 18px}
.lcard label{display:block;font:600 11px/1 system-ui;color:var(--mid);
  letter-spacing:.5px;margin:0 0 8px}
#lkey{width:100%;background:rgba(245,245,248,.05);border:1px solid var(--hair);border-radius:11px;
  color:var(--txt);font:600 15px/1 ui-monospace,SFMono-Regular,Menlo,monospace;
  padding:14px 14px;outline:none;letter-spacing:1px;text-transform:uppercase}
#lkey:focus{border-color:rgba(232,38,58,.55);box-shadow:0 0 0 3px rgba(232,38,58,.12)}
#lgo{width:100%;margin-top:14px;border:none;border-radius:11px;color:#fff;cursor:pointer;
  font:700 14px/1 system-ui;padding:15px 0;
  background:linear-gradient(90deg,var(--accent),var(--ember));box-shadow:0 0 14px rgba(232,38,58,.38)}
#lgo:disabled{opacity:.55;cursor:default;box-shadow:none}
#lmsg{min-height:17px;margin-top:12px;font:600 12px/1.4 system-ui;color:var(--accent-hi)}
#shell{display:none}
#shell.on{display:block}
.logout{margin:18px 2px 0;text-align:center}
.logout button{background:none;border:1px solid var(--hair);color:var(--mid);
  font:600 12px/1 system-ui;padding:11px 20px;border-radius:11px;cursor:pointer}
</style></head>
<body>
<header>
  <div class="brand">
    <div class="mark"><svg width="34" height="34" viewBox="0 0 34 34">
      <polygon points="17,2.6 30,10.3 30,23.7 17,31.4 4,23.7 4,10.3" fill="#0F0F13"
               stroke="#E8263A" stroke-width="2"/>
      <text x="17" y="21.2" text-anchor="middle" font-family="system-ui" font-size="10"
            font-weight="700" fill="#F5F5F8">IN<tspan fill="#E8263A">X</tspan></text>
    </svg></div>
    <div>
      <div class="name">Invisible<i>X</i></div>
      <div class="sub">REMOTE CONTROL</div>
    </div>
    <div class="pill"><span class="dot"></span><span id="link">Locked</span></div>
  </div>
  <nav id="tabs" style="display:none"></nav>
</header>

<section id="gate">
  <div class="lcard">
    <h2>Enter your licence key</h2>
    <p>The same key you logged into the module with. The link alone does not
       unlock the controls.</p>
    <label for="lkey">LICENCE KEY</label>
    <input id="lkey" type="password" autocomplete="off" autocapitalize="characters"
           spellcheck="false" inputmode="text" enterkeyhint="go" placeholder="XXXX-XXXX-XXXX">
    <button id="lgo">Unlock</button>
    <div id="lmsg"></div>
  </div>
</section>

<main id="shell">
  <div id="pages"></div>
  <div class="logout"><button id="lout">Lock again</button></div>
</main>
<div id="toast"></div>
<script>
const TOKEN = new URLSearchParams(location.search).get('t') || '';
const api = (p) => p + (p.includes('?') ? '&' : '?') + 't=' + encodeURIComponent(TOKEN);

// Sounds are served by the binary from the same clips the menu uses.
// /snd/* needs the session now, so these cannot be built at script load
// time any more - preload would fire before login, come back 401, and
// leave every element permanently in an error state. armSounds() is
// called once the session exists.
const snd = {};
function armSounds(){
  if (snd.welcome) return;
  for (const k of ['on','off','welcome']) {
    const a = new Audio(api('/snd/' + k + '.wav'));
    a.preload = 'auto'; a.volume = 0.9; snd[k] = a;
  }
}
function play(k){ try { const a = snd[k]; if (!a) return; a.currentTime = 0; a.play().catch(()=>{}); } catch(e){} }

let toastT = null;
function toast(msg, bad){
  const t = document.getElementById('toast');
  t.textContent = msg; t.className = 'on' + (bad ? ' bad' : '');
  clearTimeout(toastT); toastT = setTimeout(()=>t.className = bad ? 'bad' : '', 2200);
}

let STATE = null, PAGES = [], current = 0;

// Thrown when the server says the session is gone, so callers can drop
// straight back to the login screen instead of showing an error.
class NeedLogin extends Error {}

async function pull(){
  const r = await fetch(api('/api/state'), {cache:'no-store', credentials:'same-origin'});
  if (r.status === 401) throw new NeedLogin();
  if (!r.ok) throw new Error('HTTP ' + r.status);
  return r.json();
}

// POST, not GET: a control change must not be reachable as a plain URL,
// or any page in the browser could fire one with an <img> tag. The session
// cookie is SameSite=Strict, so only this page can send it.
async function push(id, value){
  const body = 'id=' + encodeURIComponent(id) + '&v=' + encodeURIComponent(value);
  const r = await fetch(api('/api/set'), {
    method:'POST', cache:'no-store', credentials:'same-origin',
    headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body
  });
  if (r.status === 401) throw new NeedLogin();
  if (!r.ok) throw new Error('HTTP ' + r.status);
  return r.json();
}

function buildTabs(){
  const nav = document.getElementById('tabs');
  nav.innerHTML = '';
  PAGES.forEach((p,i)=>{
    const b = document.createElement('button');
    b.textContent = p; b.className = i === current ? 'on' : '';
    b.onclick = ()=>{ current = i; render(); };
    nav.appendChild(b);
  });
}

function rowSwitch(c){
  const row = document.createElement('div'); row.className = 'row';
  const lab = document.createElement('div'); lab.className = 'lab';
  const t = document.createElement('div'); t.className = 't'; t.textContent = c.label;
  lab.appendChild(t);
  if (c.note){ const d = document.createElement('div'); d.className = 'd'; d.textContent = c.note; lab.appendChild(d); }
  const sw = document.createElement('button');
  sw.className = 'sw'; sw.setAttribute('role','switch');
  sw.setAttribute('aria-checked', c.value ? 'true' : 'false');
  sw.onclick = async ()=>{
    const next = sw.getAttribute('aria-checked') !== 'true';
    sw.setAttribute('aria-checked', next ? 'true' : 'false');
    play(next ? 'on' : 'off');
    try { await push(c.id, next ? 1 : 0); }
    catch(e){
      sw.setAttribute('aria-checked', next ? 'false' : 'true');
      if (e instanceof NeedLogin) { showGate('Session expired. Enter your key again.'); return; }
      toast('Not saved: ' + e.message, true);
    }
  };
  row.appendChild(lab); row.appendChild(sw);
  return row;
}

function rowSlider(c){
  const box = document.createElement('div'); box.className = 'sl';
  const top = document.createElement('div'); top.className = 'top';
  const t = document.createElement('div'); t.className = 't'; t.textContent = c.label;
  const v = document.createElement('div'); v.className = 'v';
  const fmt = (x)=> c.step < 1 ? Number(x).toFixed(1) + (c.unit||'') : Math.round(x) + (c.unit||'');
  v.textContent = fmt(c.value);
  top.appendChild(t); top.appendChild(v);
  const r = document.createElement('input');
  r.type = 'range'; r.min = c.min; r.max = c.max; r.step = c.step; r.value = c.value;
  r.oninput = ()=> v.textContent = fmt(r.value);
  let tmr = null;
  r.onchange = r.onpointerup = ()=>{
    clearTimeout(tmr);
    tmr = setTimeout(async ()=>{
      try { await push(c.id, r.value); }
      catch(e){
        if (e instanceof NeedLogin) { showGate('Session expired. Enter your key again.'); return; }
        toast('Not saved: ' + e.message, true);
      }
    }, 60);
  };
  box.appendChild(top); box.appendChild(r);
  return box;
}

function rowSeg(c){
  const box = document.createElement('div'); box.className = 'sl';
  const top = document.createElement('div'); top.className = 'top';
  const t = document.createElement('div'); t.className = 't'; t.textContent = c.label;
  top.appendChild(t);
  const seg = document.createElement('div'); seg.className = 'seg';
  c.options.forEach(o=>{
    const b = document.createElement('button');
    b.textContent = o.label;
    b.className = String(c.value) === String(o.value) ? 'on' : '';
    b.onclick = async ()=>{
      [...seg.children].forEach(x=>x.className = '');
      b.className = 'on'; play('on');
      try { await push(c.id, o.value); }
      catch(e){
        if (e instanceof NeedLogin) { showGate('Session expired. Enter your key again.'); return; }
        toast('Not saved: ' + e.message, true);
      }
    };
    seg.appendChild(b);
  });
  box.appendChild(top); box.appendChild(seg);
  return box;
}

function rowInfo(items){
  const box = document.createElement('div'); box.className = 'info';
  items.forEach(it=>{
    const d = document.createElement('div');
    const b = document.createElement('b'); b.textContent = it.label;
    const s = document.createElement('span'); s.textContent = it.value;
    d.appendChild(b); d.appendChild(s); box.appendChild(d);
  });
  return box;
}

function render(){
  buildTabs();
  const host = document.getElementById('pages');
  host.innerHTML = '';
  const page = PAGES[current];
  const sec = document.createElement('section'); sec.className = 'on';

  if (page === 'Info'){
    sec.appendChild(rowInfo(STATE.info || []));
    const h = document.createElement('div'); h.className = 'hint';
    h.textContent = 'The in-game menu stays hidden while remote control is on. Turn it off in the module to get the menu back.';
    sec.appendChild(h);
  } else {
    (STATE.controls || []).filter(c => c.page === page).forEach(c=>{
      if (c.type === 'bool')   sec.appendChild(rowSwitch(c));
      else if (c.type === 'enum') sec.appendChild(rowSeg(c));
      else sec.appendChild(rowSlider(c));
    });
  }
  host.appendChild(sec);
}

function setPill(text, bad){
  document.getElementById('link').textContent = text;
  document.querySelector('.dot').style.background = bad ? '#FF4A5C' : '';
  document.querySelector('.pill').style.cssText = bad
    ? 'margin-left:auto;display:flex;align-items:center;gap:6px;padding:5px 11px;border-radius:13px;' +
      'background:rgba(232,38,58,.13);border:1px solid rgba(232,38,58,.4);font:700 10.5px/1 system-ui;color:#FF4A5C'
    : '';
}

function showGate(msg){
  document.getElementById('gate').classList.add('on');
  document.getElementById('shell').classList.remove('on');
  document.getElementById('tabs').style.display = 'none';
  setPill('Locked', false);
  document.getElementById('lmsg').textContent = msg || '';
  const k = document.getElementById('lkey');
  k.value = '';
  setTimeout(()=>k.focus(), 60);
}

async function showShell(){
  STATE = await pull();
  PAGES = [...new Set((STATE.controls||[]).map(c=>c.page))];
  if (STATE.info && STATE.info.length) PAGES.push('Info');
  document.getElementById('gate').classList.remove('on');
  document.getElementById('shell').classList.add('on');
  document.getElementById('tabs').style.display = '';
  setPill('Connected', false);
  armSounds();
  render();
}

async function doLogin(){
  const btn = document.getElementById('lgo');
  const msg = document.getElementById('lmsg');
  const key = document.getElementById('lkey').value.trim().toUpperCase();
  if (!key) { msg.textContent = 'Enter your licence key.'; return; }

  btn.disabled = true; btn.textContent = 'Checking\u2026'; msg.textContent = '';
  try {
    const r = await fetch(api('/api/login'), {
      method:'POST', cache:'no-store', credentials:'same-origin',
      headers:{'Content-Type':'application/x-www-form-urlencoded'},
      body: 'key=' + encodeURIComponent(key)
    });
    let j = {}; try { j = await r.json(); } catch(e){}
    if (r.ok && j.ok) {
      await showShell();
      play('welcome');
      return;
    }
    if (r.status === 429 || j.locked) {
      msg.textContent = 'Too many attempts. Wait a minute and try again.';
    } else {
      msg.textContent = 'That key does not match. Check it and try again.';
    }
  } catch(e){
    msg.textContent = 'Cannot reach the menu: ' + e.message;
  } finally {
    btn.disabled = false; btn.textContent = 'Unlock';
  }
}

async function doLogout(){
  try {
    await fetch(api('/api/logout'), {method:'POST', cache:'no-store', credentials:'same-origin'});
  } catch(e){}
  showGate('Locked. Enter your key to unlock again.');
}

async function boot(){
  document.getElementById('lgo').onclick = doLogin;
  document.getElementById('lout').onclick = doLogout;
  document.getElementById('lkey').addEventListener('keydown', e=>{
    if (e.key === 'Enter') doLogin();
  });

  try {
    const r = await fetch(api('/api/session'), {cache:'no-store', credentials:'same-origin'});
    if (!r.ok) throw new Error('HTTP ' + r.status);
    const sess = await r.json();
    if (!sess.module_logged_in) {
      document.getElementById('gate').classList.add('on');
      setPill('No licence', true);
      document.getElementById('lgo').disabled = true;
      document.getElementById('lmsg').textContent =
        'Log into the module first, then come back to this link.';
      return;
    }
    if (sess.authed) { await showShell(); return; }
    showGate('');
  } catch(e){
    setPill('Offline', true);
    toast('Cannot reach the menu: ' + e.message, true);
  }
}
boot();
</script>
</body></html>
)IXPAGE";
