/*
 * HamClock mobile dashboard page, baked in as a C string.
 * Served on the R/W REST port as /dashboard.html.
 */

#include "HamClock.h"

const char dashboard_html[] = R"HCDB(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover" />
  <title>HamClock Mobile Dashboard</title>
  <style>
    :root{--bg:#090c10;--panel:#111821;--panel2:#0d131b;--text:#e8f0f7;--muted:#93a4b7;--line:#233142;--good:#3bd671;--warn:#ffd166;--bad:#ff5c70;--bar:#46a0ff;--barbg:#1d2a38}
    *{box-sizing:border-box} body{margin:0;background:radial-gradient(circle at top,#152033 0,#090c10 42%);color:var(--text);font:15px/1.35 system-ui,-apple-system,Segoe UI,Roboto,Helvetica,Arial,sans-serif} .wrap{width:min(520px,100%);margin:0 auto;padding:calc(env(safe-area-inset-top) + 12px) 12px calc(env(safe-area-inset-bottom) + 18px)}
    .card{background:linear-gradient(180deg,var(--panel),var(--panel2));border:1px solid var(--line);border-radius:18px;padding:14px;margin:10px 0;box-shadow:0 10px 28px #0008}.top{display:flex;justify-content:space-between;gap:10px;align-items:flex-start}.call{font-size:24px;font-weight:800;letter-spacing:.5px}.time{font-variant-numeric:tabular-nums;font-size:21px;font-weight:750}.sub{color:var(--muted);margin-top:4px}.grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}.metric{background:#0b1118;border:1px solid #1d2a38;border-radius:14px;padding:11px}.label{color:var(--muted);font-size:12px;text-transform:uppercase;letter-spacing:.08em}.value{font-size:22px;font-weight:800;margin-top:4px}.dot{display:inline-block;width:.72em;height:.72em;border-radius:50%;margin-left:.3em;vertical-align:.05em}.good{background:var(--good)}.warn{background:var(--warn)}.bad{background:var(--bad)}h2{font-size:13px;letter-spacing:.12em;color:#c9d6e3;margin:2px 0 12px;text-transform:uppercase}.band{display:grid;grid-template-columns:42px 1fr 42px;gap:9px;align-items:center;margin:10px 0}.bandname{font-weight:800}.pct{text-align:right;color:var(--muted);font-variant-numeric:tabular-nums}.bar{height:13px;background:var(--barbg);border-radius:99px;overflow:hidden}.fill{height:100%;width:0;background:linear-gradient(90deg,var(--bar),#8ec7ff);border-radius:99px}.spot{display:grid;grid-template-columns:1fr auto;gap:4px 10px;padding:9px 0;border-top:1px solid #1e2c3b}.spot:first-of-type{border-top:0}.spot b{font-size:16px}.spot .meta{color:var(--muted);font-size:13px}.footer{display:flex;justify-content:space-between;color:var(--muted);font-size:12px;margin:12px 3px}.err{color:#ff9baa}.pill{display:inline-block;border:1px solid #2b3d50;background:#0b1118;border-radius:999px;padding:2px 8px;color:var(--muted);font-size:12px}button,input{font:inherit}details{margin-top:8px}summary{color:var(--muted)}input{width:100%;margin-top:8px;background:#080d13;color:var(--text);border:1px solid #2b3d50;border-radius:12px;padding:10px}button{margin-top:8px;width:100%;border:0;border-radius:12px;background:#2265a8;color:white;padding:10px;font-weight:750}.small{font-size:12px;color:var(--muted);margin-top:7px}.scroll{display:flex;gap:10px;overflow-x:auto;scroll-snap-type:x proximity;padding-bottom:4px}.scroll .spot{min-width:210px;border-top:0;border:1px solid #1e2c3b;border-radius:14px;padding:10px;scroll-snap-align:start}.tag{display:inline-block;color:var(--muted);font-size:12px;border:1px solid #2b3d50;border-radius:999px;padding:1px 7px;margin-left:5px}
  </style>
</head>
<body>
  <main class="wrap">
    <section class="card top">
      <div><div class="call" id="call">HamClock</div><div class="sub" id="where">Loading DE...</div></div>
      <div style="text-align:right"><div class="time" id="utc">--:-- UTC</div><div class="sub" id="local">DE --:--</div></div>
    </section>

    <section class="card">
      <div class="grid">
        <div class="metric"><div class="label">Kp</div><div class="value"><span id="kp">--</span><span id="kpDot" class="dot"></span></div></div>
        <div class="metric"><div class="label">SFI / SSN</div><div class="value"><span id="sfi">--</span> <span class="sub" id="ssn"></span></div></div>
        <div class="metric"><div class="label">X-ray</div><div class="value" id="xray">--</div></div>
        <div class="metric"><div class="label">Grey line</div><div class="value" id="grey">--</div></div>
      </div>
    </section>

    <section class="card"><h2>Band Conditions</h2><div id="bands"></div></section>
    <section class="card"><h2>DX Spots</h2><div id="spots"><span class="sub">Loading...</span></div></section>
    <section class="card"><h2>On The Air</h2><div id="onta" class="scroll"><span class="sub">Loading...</span></div></section>
    <section class="card"><h2>Live PSK Stats</h2><div id="live"><span class="sub">Loading...</span></div></section>

    <details class="card">
      <summary>Connection</summary>
      <input id="base" placeholder="http://hamclock-ip:8080" />
      <button id="save">Save and refresh</button>
      <div class="small">Served by HamClock on the REST port. Leave blank/default unless you are intentionally pointing this page at a different HamClock instance.</div>
    </details>
    <div class="footer"><span id="status">Starting...</span><span>refresh 60s</span></div>
  </main>
<script>
(() => {
  const $ = id => document.getElementById(id);
  const preferredBands = ['40m','20m','15m','10m'];
  let base = localStorage.hamclockBase || (location.origin && location.origin !== 'null' ? location.origin : `${location.protocol}//${location.hostname || 'localhost'}:8080`);
  $('base').value = base;
  $('save').onclick = () => { base = $('base').value.replace(/\/$/,''); localStorage.hamclockBase = base; refresh(); };

  async function get(path){
    const r = await fetch(`${base}/${path}`, {cache:'no-store'});
    if(!r.ok) throw new Error(`${path}: HTTP ${r.status}`);
    return await r.text();
  }
  async function getOptional(path){
    const r = await fetch(`${base}/${path}`, {cache:'no-store'});
    if(!r.ok) return '';
    return await r.text();
  }
  function kv(txt){
    const out = {};
    for (const line of txt.split(/\r?\n/)) {
      const m = line.match(/^([A-Za-z0-9_]+)\s+(.+?)\s*$/);
      if (m) out[m[1]] = m[2];
    }
    return out;
  }
  function num(v){ const m = String(v||'').match(/[-+]?\d+(\.\d+)?/); return m ? Number(m[0]) : NaN; }
  function dotClass(kp){ return kp < 4 ? 'good' : kp < 6 ? 'warn' : 'bad'; }
  function hmFromISOish(s){ const m=String(s||'').match(/T(\d\d):(\d\d)/); return m ? `${m[1]}:${m[2]}` : '--:--'; }
  function timeToEvent(hhmm, nowTxt){
    if(!/^\d\d:\d\d$/.test(hhmm)) return '';
    const n = String(nowTxt||'').match(/T(\d\d):(\d\d)/); if(!n) return '';
    const now = Number(n[1])*60 + Number(n[2]);
    const ev = Number(hhmm.slice(0,2))*60 + Number(hhmm.slice(3));
    let d = ev - now; if (d < -720) d += 1440; if (d > 720) d -= 1440;
    const abs = Math.abs(d), h = Math.floor(abs/60), m = abs%60;
    return d >= 0 ? `in ${h? h+'h ':''}${m}m` : `${h? h+'h ':''}${m}m ago`;
  }
  function parseVoacap(txt){
    const rows = [];
    for (const line of txt.split(/\r?\n/)) {
      const m = line.match(/^\s*(\d{2,3}m)\s+((?:\s*\d+){24})\s+/);
      if (!m) continue;
      const vals = m[2].trim().split(/\s+/).map(Number);
      rows.push({band:m[1].replace(/^0/,''), now: vals[0] ?? 0});
    }
    return rows;
  }
  function renderBands(rows){
    const wanted = preferredBands.map(b => rows.find(r => r.band === b)).filter(Boolean);
    const show = wanted.length ? wanted : rows.slice(0,8);
    $('bands').innerHTML = show.map(r => {
      const p = Math.max(0, Math.min(100, Math.round(r.now) || 0));
      return `<div class="band"><div class="bandname">${r.band}</div><div class="bar"><div class="fill" style="width:${p}%"></div></div><div class="pct">${p}%</div></div>`;
    }).join('') || '<span class="sub">No VOACAP matrix</span>';
  }
  function parseSpots(txt, limit){
    const rows=[];
    for (const line of txt.split(/\r?\n/)) {
      if(!line.trim() || line.startsWith('#')) continue;
      const p = line.trim().split(/\s+/);
      if (p.length >= 4 && /^\d/.test(p[0])) rows.push({khz:p[0], call:p[1], utc:p[2], mode:p[3], grid:p[4]||''});
    }
    return rows.slice(0,limit);
  }
  function bandFromKhz(k){
    const f=Number(k); if(!isFinite(f)) return '';
    if(f<4000) return '80m'; if(f<6000) return '60m'; if(f<8000) return '40m'; if(f<11000) return '30m'; if(f<15000) return '20m'; if(f<19000) return '17m'; if(f<22000) return '15m'; if(f<26000) return '12m'; if(f<30000) return '10m'; if(f<54000) return '6m'; return '';
  }
  function renderSpots(rows){
    $('spots').innerHTML = rows.map(s => `<div class="spot"><b>${s.call}</b><span>${bandFromKhz(s.khz)} ${s.mode}</span><div class="meta">${s.khz} kHz - ${s.utc} UTC ${s.grid? ' - '+s.grid : ''}</div></div>`).join('') || '<span class="sub">No DX spots</span>';
  }
  function renderONTA(rows){
    $('onta').innerHTML = rows.map(s => {
      const program = s.grid && !/^[A-R]{2}[0-9]{2}/i.test(s.grid) ? `<span class="tag">${s.grid}</span>` : '';
      const id = s.utc && !/^\d{4}$/.test(s.utc) ? `<span class="meta">${s.utc}</span>` : '';
      return `<div class="spot"><b>${s.call}</b><span>${bandFromKhz(s.khz)} ${s.mode}${program}</span><div class="meta">${s.khz} kHz${s.utc && /^\d{4}$/.test(s.utc) ? ' - '+s.utc+' UTC' : ''}${s.grid && /^[A-R]{2}[0-9]{2}/i.test(s.grid) ? ' - '+s.grid : ''}</div>${id}</div>`;
    }).join('') || '<span class="sub">No ONTA spots. HamClock only exposes the current filtered ONTA list when the On The Air pane has loaded spots.</span>';
  }
  function renderLive(txt){
    const rows=[];
    for(const line of txt.split(/\r?\n/)){
      if(!line.trim() || line.startsWith('#')) continue;
      const p=line.trim().split(/\s+/); if(p.length>=2) rows.push({band:p[0],count:Number(p[1])||0});
    }
    const top=rows.filter(r=>r.count>0).sort((a,b)=>b.count-a.count).slice(0,5);
    $('live').innerHTML = top.map(r=>`<div class="spot"><b>${r.band}</b><span>${r.count.toLocaleString()} spots</span></div>`).join('') || '<span class="sub">No live stats enabled</span>';
  }
  async function refresh(){
    try{
      $('status').textContent = 'Refreshing...';
      const [deTxt, wxTxt, voaTxt, spotsTxt, ontaTxt, liveTxt, timeTxt] = await Promise.allSettled([get('get_de.txt'), get('get_spacewx.txt'), get('get_voacap.txt'), get('get_dxspots.txt'), getOptional('get_ontheair.txt'), getOptional('get_livestats.txt'), get('get_time.txt')]);
      const de = deTxt.status==='fulfilled' ? kv(deTxt.value) : {};
      const wx = wxTxt.status==='fulfilled' ? kv(wxTxt.value) : {};
      if (de.Call) $('call').textContent = de.Call;
      const loc = [de.DE_grid, de.DE_lat && de.DE_lng ? `${de.DE_lat.replace(' deg','')}, ${de.DE_lng.replace(' deg','')}` : ''].filter(Boolean).join(' - ');
      $('where').textContent = loc || 'DE location unavailable';
      $('local').textContent = `DE ${hmFromISOish(de.DE_time)}`;
      const utcKV = timeTxt.status==='fulfilled' ? kv(timeTxt.value) : {};
      $('utc').textContent = `${hmFromISOish(utcKV.Clock_UTC || new Date().toISOString())} UTC`;
      const kp = num(wx.KP); $('kp').textContent = isNaN(kp) ? '--' : kp.toFixed(1); $('kpDot').className = `dot ${isNaN(kp)?'':dotClass(kp)}`;
      $('sfi').textContent = isNaN(num(wx.FLUX)) ? '--' : Math.round(num(wx.FLUX)); $('ssn').textContent = isNaN(num(wx.SSN)) ? '' : `/ ${Math.round(num(wx.SSN))}`;
      $('xray').textContent = wx.XRAY || '--';
      const rise = de.DE_SunRise || 'none', set = de.DE_SunSet || 'none';
      const dRise = timeToEvent(rise, de.DE_time), dSet = timeToEvent(set, de.DE_time);
      const next = [rise !== 'none' && {name:'Sunrise', hh:rise, delta:dRise}, set !== 'none' && {name:'Sunset', hh:set, delta:dSet}].filter(Boolean).sort((a,b)=>Math.abs(parseInt(a.delta)||999)-Math.abs(parseInt(b.delta)||999))[0];
      $('grey').textContent = next ? `${next.name} ${next.hh} ${next.delta}` : '--';
      if(voaTxt.status==='fulfilled') renderBands(parseVoacap(voaTxt.value)); else $('bands').innerHTML = `<span class="err">${voaTxt.reason.message}</span>`;
      if(spotsTxt.status==='fulfilled') renderSpots(parseSpots(spotsTxt.value,200)); else $('spots').innerHTML = `<span class="err">${spotsTxt.reason.message}</span>`;
      if(ontaTxt.status==='fulfilled') renderONTA(parseSpots(ontaTxt.value,200)); else $('onta').innerHTML = `<span class="err">${ontaTxt.reason.message}</span>`;
      if(liveTxt.status==='fulfilled') renderLive(liveTxt.value); else $('live').innerHTML = `<span class="err">${liveTxt.reason.message}</span>`;
      $('status').textContent = `Updated ${new Date().toLocaleTimeString()}`;
    } catch(e) { $('status').textContent = e.message; }
  }
  refresh(); setInterval(refresh, 60000);
})();
</script>
</body>
</html>

)HCDB";
