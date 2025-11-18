#ifndef WEB_HTML_H
#define WEB_HTML_H

#include <pgmspace.h>
#include "config.h"

// ============================================================================
// HTML CONTENT (PROGMEM)
// ============================================================================

static const char INDEX_HTML_PART1[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32(x) Controller</title>
<style>
* {box-sizing:border-box;margin:0;padding:0}
body {font-family:'Segoe UI',Tahoma,Geneva,Verdana,sans-serif;background:#0d1117;color:#c9d1d9;min-height:100vh;padding:8px}
.container {max-width:1400px;margin:0 auto}
h1 {text-align:center;margin-bottom:12px;font-size:1.8em;background:linear-gradient(90deg,#58a6ff,#bc8cff);-webkit-background-clip:text;-webkit-text-fill-color:transparent;background-clip:text}
.card {background:#161b22;border:1px solid #30363d;border-radius:8px;padding:12px;margin-bottom:10px;box-shadow:0 4px 8px rgba(0,0,0,0.4)}
.card h3 {margin-bottom:8px;color:#58a6ff;font-size:1.1em;border-bottom:1px solid #21262d;padding-bottom:6px}
.card.condensed { padding: 8px; }
.card.condensed h3 { font-size: 1.05em; margin-bottom: 6px; padding-bottom: 4px; }
.card.condensed .form-group { margin-bottom: 6px; }
.card.condensed .form-group label { margin-bottom: 2px; font-size: 0.8em; }
.card.condensed input[type="text"], .card.condensed input[type="password"] { padding: 6px; font-size: 0.85em; }
.card.condensed .btn { padding: 6px 10px; font-size: 0.85em; }
.card.condensed .alert { padding: 6px; margin-bottom: 6px; font-size: 0.8em; }
.card.condensed .pill { padding: 3px 8px; font-size: 0.75em; }
.card.condensed .row { margin-bottom: 4px; }
h4 {color:#58a6ff;font-size:0.95em;margin:6px 0 4px 0}
.row {display:flex;gap:6px;flex-wrap:wrap;align-items:center;margin-bottom:6px}
.pill {background:#21262d;padding:4px 10px;border-radius:12px;font-size:0.8em;white-space:nowrap;border:1px solid #30363d}
.core-badge {padding:3px 8px;border-radius:10px;font-size:0.75em;font-weight:600}
.core-0 {background:#1f6feb;color:#fff}
.core-1 {background:#9e6a03;color:#fff}
.core-any {background:#6e7681;color:#fff}
.status-dot {display:inline-block;width:10px;height:10px;border-radius:50%;margin-right:6px}
.status-dot.on {background:#00ff41;box-shadow:0 0 10px 2px #00ff41}
.status-dot.off {background:#6e7681}
input[type="text"],input[type="password"],textarea {width:100%;padding:8px;border:1px solid #30363d;border-radius:4px;font-size:0.9em;margin-bottom:6px;background:#0d1117;color:#c9d1d9}
textarea {resize:vertical;font-family:monospace}
button,.btn {padding:8px 16px;border:none;border-radius:4px;font-size:0.9em;cursor:pointer;transition:all 0.3s;font-weight:500}
.btn.primary {background:#238636;color:#fff}
.btn.primary:hover {background:#2ea043}
.btn.secondary {background:#21262d;color:#c9d1d9;border:1px solid #30363d}
.btn.secondary:hover {background:#30363d}
.btn.danger {background:#da3633;color:#fff}
.small {font-size:0.75em;color:#8b949e;font-weight:normal}
pre {background:#0d1117;color:#c9d1d9;padding:10px;border-radius:4px;overflow-x:auto;font-size:0.8em;border:1px solid #30363d}
.form-group {margin-bottom:8px}
.form-group label {display:block;margin-bottom:3px;color:#8b949e;font-size:0.85em}
.grid {display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:10px}
.task-table {width:100%;border-collapse:collapse;margin-top:6px;font-size:0.85em}
.task-table th {background:#21262d;color:#c9d1d9;padding:6px 8px;text-align:left;font-size:0.85em;border:1px solid #30363d}
.task-table td {padding:5px 8px;border:1px solid #30363d;font-size:0.8em}
.task-table tr:hover {background:#161b22}
.task-name {font-weight:bold;font-family:monospace;color:#58a6ff;font-size:0.8em}
.task-state {padding:3px 6px;border-radius:3px;font-size:0.75em;font-weight:bold}
.state-running {background:#238636;color:#fff}
.state-ready {background:#1f6feb;color:#fff}
.state-blocked {background:#9e6a03;color:#fff}
.health-good {color:#3fb950;font-weight:bold}
.health-ok {color:#58a6ff;font-weight:bold}
.health-low {color:#d29922;font-weight:bold}
.health-critical {color:#f85149;font-weight:bold}
.progress-bar {width:100%;height:6px;background:#21262d;border-radius:3px;overflow:hidden;border:1px solid #30363d}
.progress-fill {height:100%;background:linear-gradient(90deg,#238636,#3fb950);transition:width 0.3s}
.cpu-badge {display:inline-block;padding:2px 5px;border-radius:3px;font-size:0.7em;font-weight:bold}
.cpu-low {background:#238636;color:#fff}
.cpu-med {background:#d29922;color:#000}
.cpu-high {background:#da3633;color:#fff}
.alert {padding:8px;border-radius:4px;margin-bottom:8px;border-left:3px solid;font-size:0.85em}
.alert.success {background:#0d1117;border-color:#238636;color:#3fb950}
.alert.error {background:#0d1117;border-color:#da3633;color:#f85149}
.alert.info {background:#0d1117;border-color:#1f6feb;color:#58a6ff}
.task-table-wrapper {
  max-height: 500px;
  overflow-y: auto;
  overflow-x: auto;
  margin-top: 6px;
  border: 1px solid #30363d;
  border-radius: 4px;
}
.task-table {
  width:100%;
  border-collapse:collapse;
  font-size:0.85em;
  margin:0;
}
.task-table thead {
  position: sticky;
  top: 0;
  z-index: 10;
}
.debug-list {
  background:#0d1117;
  border:1px solid #30363d;
  border-radius:4px;
  padding:4px;
  margin-bottom:6px;
  max-height:120px;
  overflow-y:auto;
  font-size:0.75em;
}
</style>
</head>
<body>
<div class="container">
<h1>ESP32(x) Controller</h1>
<div class="card">
 <h3>System Status <span class="small"></span></h3>
 <div class="row">
  <div><span class="status-dot off" id="bleDot"></span><span>BLE</span></div>
  <div><span class="status-dot off" id="wifiDot"></span><span>WiFi</span></div>
  <div class="pill" id="ipInfo">IP: -</div>
  <div class="pill" id="rssiInfo">RSSI: --</div>
  <div class="pill" id="timeInfo" style="min-width:180px">Time: Not synced</div>
  <div class="pill">Up: <span id="upt">--:--:--</span></div>
  <div class="pill">Free Heap: <span id="heap">--</span>/<span id="heapTot">--</span></div>
  <div class="pill">Temp: <span id="tempC">--</span></div>
)rawliteral";

#if DEBUG_MODE
static const char INDEX_HTML_PART1_DEBUG[] PROGMEM = R"rawliteral(
  <div class="pill"><span class="core-badge core-0">C0</span> <span id="c0">-</span>% (<span id="c0Tasks">-</span> tasks)</div>
  <div class="pill" id="c1pill"><span class="core-badge core-1">C1</span> <span id="c1">-</span>% (<span id="c1Tasks">-</span> tasks)</div>
  <div class="pill">Tasks: <span id="taskCount">-</span></div>
)rawliteral";
#endif

static const char INDEX_HTML_PART1_END[] PROGMEM = R"rawliteral(
  <div class="pill"><span id="chip">-</span> @ <span id="cpuFreq">-</span>MHz</div>
  <div class="pill">Flash: <span id="flashSize">--</span>MB</div>
  <div class="pill" id="psramPill" style="display:none">PSRAM: <span id="psramUsed">--</span>/<span id="psramTotal">--</span></div>
 </div>
</div>
)rawliteral";

static const char INDEX_HTML_BIZ_CARD[] PROGMEM = R"rawliteral(
<div class="card condensed">
 <h3>Business Module</h3>
 <div id="bizStatus" class="alert info">Status: <span id="bizState">Loading...</span></div>
 <div class="row">
  <button class="btn primary" onclick="startBiz()">Start</button>
  <button class="btn danger" onclick="stopBiz()">Stop</button>
  <div class="pill">Q:<span id="bizQueue">-</span> | Proc:<span id="bizProcessed">-</span></div>
 </div>
</div>
)rawliteral";

static const char INDEX_HTML_CMD_CARD[] PROGMEM = R"rawliteral(
<div class="card condensed">
 <h3>Command Exec</h3>
 <div class="form-group">
  <input type="text" id="execCmd" placeholder="Enter command...">
 </div>
 <button class="btn primary" onclick="submitCommand()">Execute</button>
 <div id="execResult"></div>
</div>
)rawliteral";

static const char INDEX_HTML_WIFI_CARD[] PROGMEM = R"rawliteral(
<div class="card condensed">
 <h3>WiFi Config</h3>
 <div class="grid">
  <div>
   <div class="form-group">
    <label>SSID</label>
    <input type="text" id="wifiSsid" placeholder="Network name">
   </div>
   <div class="form-group">
    <label>Password</label>
    <input type="password" id="wifiPass" placeholder="Password">
   </div>
   <div class="form-group">
    <label><input type="checkbox" id="wifiDhcp" checked> DHCP</label>
   </div>
  </div>
  <div id="staticIpFields" style="display:none">
   <div style="display:grid;grid-template-columns:1fr 1fr;gap:6px">
    <div class="form-group" style="margin-bottom:4px">
     <label style="font-size:0.75em">Static IP</label>
     <input type="text" id="staticIp" placeholder="192.168.1.100" style="padding:5px;font-size:0.8em">
    </div>
    <div class="form-group" style="margin-bottom:4px">
     <label style="font-size:0.75em">Gateway</label>
     <input type="text" id="gateway" placeholder="192.168.1.1" style="padding:5px;font-size:0.8em">
    </div>
    <div class="form-group" style="margin-bottom:4px">
     <label style="font-size:0.75em">Subnet</label>
     <input type="text" id="subnet" placeholder="255.255.255.0" style="padding:5px;font-size:0.8em">
    </div>
    <div class="form-group" style="margin-bottom:4px">
     <label style="font-size:0.75em">DNS</label>
     <input type="text" id="dns" placeholder="8.8.8.8" style="padding:5px;font-size:0.8em">
    </div>
   </div>
  </div>
 </div>
 <button class="btn primary" onclick="saveNetwork()">Save & Connect</button>
 <div id="networkResult"></div>
</div>
)rawliteral";

#if DEBUG_MODE
static const char INDEX_HTML_TASKS[] PROGMEM = R"rawliteral(
<div class="card">
 <h3>Debug Info</h3>

 <h4>Tasks <span class="small">Per-Core CPU%</span></h4>
 <div id="taskMonitor"><p style="text-align:center;color:#8b949e;padding:12px">Loading...</p></div>

 <h4>Reboot Log <span class="small">(non-user driven)</span></h4>
 <div id="rebootLog" class="debug-list"></div>

 <h4>WiFi Reconnects</h4>
 <div id="wifiLog" class="debug-list"></div>

 <h4>Error Log</h4>
 <div id="errorLog" class="debug-list"></div>

 <button class="btn secondary" onclick="clearDebugLogs()">Clear Logs</button>
 <div id="debugClearResult"></div>
</div>
)rawliteral";
#endif

#if ENABLE_OTA
static const char INDEX_HTML_OTA[] PROGMEM = R"rawliteral(
<div class="card condensed">
 <h3>OTA Firmware Update</h3>
 <div id="otaAvailable" style="display:none">
  <div class="form-group">
   <label>Firmware URL</label>
   <input type="text" id="otaUrl" placeholder="http://example.com/firmware.bin">
  </div>
   <div class="row" style="font-size: 0.9em; gap: 8px;">
    <style>#otaAvailable .pill strong { color: #58a6ff; }</style>
    <div class="pill">Free Space for OTA: <strong id="freeSpace">-</strong></div>
   </div>
  <div class="row">
   <button class="btn primary" id="otaUpdateBtn" onclick="startOTAUpdate()">Start Update</button>
  </div>
  <div id="otaUpdateResult" style="margin-top: 8px;"></div>
 </div>
 <div id="otaNotAvailable" style="display:none">
   <p style="color:#f85149;font-size:0.9em;">OTA updates are not available (no 'ota' partition found).</p>
 </div>
</div>
)rawliteral";

static const char OTA_BUSY_HTML[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>OTA Updating...</title>
<style>
body{font-family:system-ui,-apple-system,Segoe UI,Roboto,Ubuntu,'Helvetica Neue',Arial;
background:#0d1117;color:#c9d1d9;display:flex;align-items:center;justify-content:center;
min-height:100vh;margin:0}
.card{background:#161b22;border:1px solid #30363d;border-radius:10px;padding:18px;max-width:420px;width:90%}
.h{color:#58a6ff;margin:0 0 8px 0}
.p{margin:0 0 12px 0;color:#8b949e;font-size:0.9em}
.bar{height:10px;background:#21262d;border:1px solid #30363d;border-radius:6px;overflow:hidden}
.fill{height:100%;width:0%;background:linear-gradient(90deg,#238636,#3fb950);transition:width 0.4s ease;}
.small{color:#8b949e;font-size:12px;margin-top:8px;text-align:center;min-height:1em}
</style></head><body>
<div class="card">
 <h3 class="h" id="otaTitle">Updating firmware...</h3>
 <p class="p" id="otaStatusText">Device is applying an update. Please wait...</p>
 <div class="bar"><div class="fill" id="otaFill"></div></div>
 <p class="small" id="otaProgressText">Connecting...</p>
</div>
<script>
function fmB(b){if(!b||b<0)return'0B';if(b<1024)return`${b}B`;if(b<1048576)return`${(b/1024).toFixed(1)}KB`;return`${(b/1048576).toFixed(2)}MB`;}
const fill=document.getElementById('otaFill');
const prog=document.getElementById('otaProgressText');
const title=document.getElementById('otaTitle');
const status=document.getElementById('otaStatusText');
let isFlashing = false;
function pollForReboot() {
 setTimeout(() => {
   fetch('/')
     .then(r => {
       if (r.ok) {
         prog.textContent = 'Reboot complete. Reloading...';
         location.reload();
       } else {
         prog.textContent = 'Server error. Retrying...';
         pollForReboot();
       }
     })
     .catch(e => {
       prog.textContent = 'Device rebooting... Reconnecting...';
       pollForReboot();
     });
 }, 2000);
}
async function pollProgress(){
 if (isFlashing) return;
 try{
   const r = await fetch('/api/ota/status');
   if (!r.ok) {
     prog.textContent = 'Reconnecting...';
     setTimeout(pollProgress, 1500);
     return;
   }
   const j = await r.json();
   if(!j) { setTimeout(pollProgress, 1500); return; }
   if(j.state===0 || j.state===4 || j.state===5){
     prog.textContent = 'Update complete. Reloading...';
     location.reload();
     return;
   }
   if(j.state===1 || j.state===2) {
     title.textContent = (j.state===1) ? 'Checking...' : 'Downloading...';
     status.textContent = (j.state===1) ? 'Checking for firmware update...' : 'Downloading new firmware. Do not unplug.';
     const p = j.progress || 0;
     fill.style.width = p + '%';
     let pText = p + '%';
     if (j.file_size > 0) {
       const downloaded = fmB((j.file_size * p) / 100);
       const total = fmB(j.file_size);
       pText = `${p}% (${downloaded} / ${total})`;
     }
     prog.textContent = pText;
   } else if (j.state===3) {
     isFlashing = true;
     title.textContent = 'Flashing...';
     status.textContent = 'Download complete. Writing to flash...';
     prog.textContent = 'This may take a minute. Device will reboot.';
     fill.style.width = '100%';
     pollForReboot();
     return;
   }
 }catch(e){
   prog.textContent = 'Connection lost. Retrying...';
 }
 if (!isFlashing) {
   setTimeout(pollProgress, 1000);
 }
}
pollProgress();
</script>
</body></html>
)HTML";
#endif

static const char INDEX_HTML_PART2[] PROGMEM = R"rawliteral(
</div>
<script>
const I=id=>document.getElementById(id);
let otaInterval = null;

async function refreshOTA(){
 const j = await api('/api/ota/status');
 if(j.error)return;

 const otaAvail = document.getElementById('otaAvailable');
 const otaNotAvail = document.getElementById('otaNotAvailable');
 
 if(!j.available){
   if(otaAvail) otaAvail.style.display = 'none';
   if(otaNotAvail) otaNotAvail.style.display = 'block';
   return;
 }

 if(otaAvail) otaAvail.style.display = 'block';
 if(otaNotAvail) otaNotAvail.style.display = 'none';

 if(j.free_sketch_space) I('freeSpace').textContent = fmB(j.free_sketch_space);
 
 const otaStatusEl = I('otaUpdateResult');
 if (j.state === 5) {
   if (otaStatusEl) otaStatusEl.innerHTML = `<div class="alert error">Failed: ${j.error || 'Unknown'}. <a href="javascript:void(0)" onclick="resetOTA()">Reset?</a></div>`;
 }
}

async function startOTAUpdate(){
 const url = I('otaUrl').value.trim();
 if(!url){ alert('Enter firmware URL'); return; }
 if(!url.startsWith('http')){ alert('URL must start with http:// or https://'); return; }
 if(!confirm('Start OTA update? The device will reboot after a successful update.')){ return; }
 
 const res = I('otaUpdateResult');
 const r = await api('/api/ota/update','POST',{url});
 
 if(r.error || r.err){
    const msg = (r.error || r.err);
    if (res) res.innerHTML = `<div class="alert error">Error: ${msg}</div>`;
 } else {
    if(res) res.innerHTML = `<div class="alert success">Update started! Reloading to show progress...</div>`;
    setTimeout(() => { location.reload(); }, 1000);
 }
}

async function resetOTA(){
 const res = I('otaUpdateResult');
 if(res) res.innerHTML = '';
 const r = await api('/api/ota/reset','POST');
 if(!r.error){
   refreshOTA();
 }
}

async function api(path,method='GET',body=null){
 try{
  const opts={method};
  if(body){opts.headers={'Content-Type':'application/json'};opts.body=JSON.stringify(body);}
  const r=await fetch(path,opts);
  if(r.status === 503) {
    return { error: 'ota_active' };
  }
  if(!r.ok) {
    const errText = await r.text();
    return {error: `HTTP ${r.status}: ${errText}`};
  }
  return await r.json();
 }catch(e){return{error:e.message}}
}

function fmU(ms){
 const s=Math.floor(ms/1000);
 const h=Math.floor(s/3600);
 const m=Math.floor((s%3600)/60);
 const ss=s%60;
 return`${String(h).padStart(2,'0')}:${String(m).padStart(2,'0')}:${String(ss).padStart(2,'0')}`
}
function fmB(b){
 if(b<1024)return`${b}B`;
 if(b<1048576)return`${(b/1024).toFixed(1)}KB`;
 return`${(b/1048576).toFixed(2)}MB`;
}
function cpuCls(v){return v<30?'cpu-low':v<70?'cpu-med':'cpu-high'}
function healthCls(v){return'health-'+v}
function stCls(v){return'state-'+v.toLowerCase()}
function coreCls(c){
 if(c==='ANY')return'core-any';
 if(c==='0')return'core-0';
 if(c==='1')return'core-1';
 return'core-any';
}

I('wifiDhcp').addEventListener('change',()=>{I('staticIpFields').style.display=I('wifiDhcp').checked?'none':'block';});

async function startBiz(){
 const r=await api('/api/biz/start','POST');
 if(r.error)return;
 I('bizState').textContent='RUNNING';
 refresh();
}
async function stopBiz(){
 const r=await api('/api/biz/stop','POST');
 if(r.error)return;
 I('bizState').textContent='STOPPED';
 refresh();
}

async function submitCommand(){
 const cmd=I('execCmd').value.trim();
 if(!cmd){alert('Enter a command');return;}
 const r=await api('/api/exec','POST',{cmd});
 const res=I('execResult');
 if(r.error||r.err){
   res.innerHTML='<div class="alert error">'+(r.error||r.err)+'</div>';
 } else {
   res.innerHTML='<div class="alert success">'+(r.msg||'OK')+'</div>';
   I('execCmd').value='';
 }
 setTimeout(()=>res.innerHTML='',3000);
}

async function saveNetwork(){
 const ssid=I('wifiSsid').value.trim();
 const pass=I('wifiPass').value;
 const dhcp=I('wifiDhcp').checked;
 if(!ssid){alert('Enter SSID');return;}
 const body={ssid,pass,dhcp};
 if(!dhcp){
  body.static_ip=I('staticIp').value;
  body.gateway=I('gateway').value;
  body.subnet=I('subnet').value;
  body.dns=I('dns').value;
 }
 const r=await api('/api/network','POST',body);
 const res=I('networkResult');
 if(r.error||r.err){
   res.innerHTML='<div class="alert error">'+(r.error||r.err)+'</div>';
 }else{
   res.innerHTML='<div class="alert success">'+(r.msg||'Saved')+'</div>';
 }
 setTimeout(()=>res.innerHTML='',5000);
}
)rawliteral";

#if DEBUG_MODE
static const char INDEX_HTML_DEBUG_FUNCTIONS[] PROGMEM = R"rawliteral(
async function refreshTasks(){
 const j=await api('/api/tasks');
 if(j.error)return;

 I('taskCount').textContent=j.task_count||'-';
 if(j.core_summary){
  if(j.core_summary['0']){
    I('c0Tasks').textContent=j.core_summary['0'].tasks||'0';
  }
  if(j.core_summary['1']){
    I('c1Tasks').textContent=j.core_summary['1'].tasks||'0';
  }
 }

 const taskMonitorEl = I('taskMonitor');
 if(!taskMonitorEl) return;

 if(!j.tasks){
   taskMonitorEl.innerHTML='<p style="text-align:center;color:#8b949e">No tasks</p>';
   return;
 }
 j.tasks.sort((a,b)=>(b.runtime||0)-(a.runtime||0));
 let h='<div class="task-table-wrapper"><table class="task-table"><thead><tr><th>Task</th><th>Core</th><th>Pri</th><th>Stack</th><th>State</th><th>CPU%</th><th>Runtime</th></tr></thead><tbody>';
 j.tasks.forEach(t=>{
  const cpuPct = Math.min(100, t.cpu_percent || 0);
  h+=`<tr>
    <td class="task-name">${t.name}</td>
    <td><span class="core-badge ${coreCls(t.core)}">${t.core}</span></td>
    <td>${t.priority}</td>
    <td><span class="${healthCls(t.stack_health)}">${t.stack_hwm}</span></td>
    <td><span class="task-state ${stCls(t.state)}">${t.state}</span></td>
    <td><span class="cpu-badge ${cpuCls(cpuPct)}">${cpuPct}%</span>
    <div class="progress-bar"><div class="progress-fill" style="width:${cpuPct}%"></div></div></td>
    <td>${fmU((t.runtime||0)*1000)}</td>
  </tr>`;
 });
 h+='</tbody></table></div>';
 taskMonitorEl.innerHTML=h;
}

async function refreshDebugLogs(){
 const j = await api('/api/debug/logs');
 if(j.error) return;

 const fmt = (sec)=>fmU((sec||0)*1000);
 const fmtEpoch = (epoch) => {
   if(!epoch || epoch===0) return '';
   const d = new Date(epoch * 1000);
   return ` (${d.toISOString().replace('T',' ').substring(0,19)} UTC)`;
 };
 const renderList = (elId, arr, empty) => {
   const el = I(elId);
   if(!el) return;
   if(!arr || arr.length===0){
     el.innerHTML = `<span style="color:#8b949e">${empty}</span>`;
     return;
   }
   let h = '<ul style="list-style:none;padding-left:0;margin:0;">';
   arr.forEach(e=>{
     const t = fmt(e.t);
     const epochStr = fmtEpoch(e.epoch);
     const m = e.msg || '';
     h += `<li>[${t}]${epochStr} ${m}</li>`;
   });
   h += '</ul>';
   el.innerHTML = h;
 };
 renderList('rebootLog', j.reboots, 'No unexpected reboots');
 renderList('wifiLog', j.wifi, 'No reconnects recorded');
 renderList('errorLog', j.errors, 'No errors recorded');
}

async function clearDebugLogs(){
 const res = I('debugClearResult');
 const r = await api('/api/debug/clear','POST');
 if(r.error){
   if(res) res.innerHTML = `<div class="alert error">${r.error}</div>`;
 } else {
   if(res) res.innerHTML = `<div class="alert success">${r.msg||'Logs cleared'}</div>`;
 }
 setTimeout(()=>{ if(res) res.innerHTML=''; },3000);
 refreshDebugLogs();
}
)rawliteral";
#endif

static const char INDEX_HTML_REFRESH[] PROGMEM = R"rawliteral(
async function refresh(){
 const j=await api('/api/status');
 if(j.error) {
  if (j.error === 'ota_active') {
    location.reload();
  }
  return;
 }
 if(j.ota_active) {
  location.reload();
  return;
 }
 I('bleDot').classList.toggle('on',!!j.ble);
 I('wifiDot').classList.toggle('on',!!j.connected);
 I('ipInfo').textContent=`IP: ${j.ip||'-'}`;
 I('rssiInfo').textContent=`RSSI: ${j.rssi||'--'}`;
 if(j.time_synced && j.current_time){
  I('timeInfo').textContent=`Time: ${j.current_time} UTC`;
 }else{
  I('timeInfo').textContent='Time: Not synced';
 }
 I('upt').textContent=fmU(j.uptime_ms||0);
 I('heap').textContent=fmB(j.heap_free||0);
 I('heapTot').textContent=fmB(j.heap_total||0);
)rawliteral";

#if DEBUG_MODE
static const char INDEX_HTML_REFRESH_DEBUG[] PROGMEM = R"rawliteral(
 I('c0').textContent=j.core0_load??'-';
 I('c1').textContent=j.core1_load??'-';
)rawliteral";
#endif

static const char INDEX_HTML_REFRESH_END[] PROGMEM = R"rawliteral(
 I('chip').textContent=j.chip_model||'-';
 I('cpuFreq').textContent=j.cpu_freq||'-';
 if(j.temp_c===null||j.temp_c===undefined){I('tempC').textContent='n/a';}
 else{I('tempC').textContent=(j.temp_c.toFixed?j.temp_c.toFixed(1):j.temp_c)+'C';}
 
 if(j.memory){
  I('flashSize').textContent=j.memory.flash_mb||'--';
  if(j.memory.has_psram){
    I('psramPill').style.display='inline-block';
    const psramUsed = (j.memory.psram_total - j.memory.psram_free);
    I('psramUsed').textContent=fmB(psramUsed);
    I('psramTotal').textContent=fmB(j.memory.psram_total);
  }else{
    I('psramPill').style.display='none';
  }
 }
 if(j.num_cores===1){I('c1pill').style.display='none';}else{I('c1pill').style.display='inline-block';}
 if(j.biz){
  I('bizState').textContent=j.biz.running?'RUNNING':'STOPPED';
  I('bizQueue').textContent=j.biz.queue??'-';
  I('bizProcessed').textContent=j.biz.processed??'-';
 }
}
)rawliteral";

static const char INDEX_HTML_END[] PROGMEM = R"rawliteral(
const refreshInterval = setInterval(refresh,2000);
)rawliteral";

#if DEBUG_MODE
static const char INDEX_HTML_END_DEBUG[] PROGMEM = R"rawliteral(
const taskInterval = setInterval(refreshTasks,3000);
const debugInterval = setInterval(refreshDebugLogs,4000);
refreshTasks();
refreshDebugLogs();
)rawliteral";
#endif

static const char INDEX_HTML_END_FINAL[] PROGMEM = R"rawliteral(
refresh();
if(document.getElementById('otaAvailable')){
 otaInterval = setInterval(refreshOTA,2000);
 refreshOTA();
}
I('execCmd').addEventListener('keyup', e => { if (e.key === 'Enter') submitCommand(); });
</script>
</body>
</html>
)rawliteral";

#endif // WEB_HTML_H