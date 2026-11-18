// Web page shown on the iPad. Served by receiver.ino at http://192.168.4.1
#pragma once

const char PAGE_HTML[] PROGMEM = R"HTML(<!doctype html>
<html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="apple-mobile-web-app-capable" content="yes">
<title>Range Test</title>
<style>
:root{--bg:#0f1115;--card:#1a1d24;--text:#e8eaed;--muted:#8a919c;--line:#2a2e37;
--good:#2ecc71;--ok:#a3d13b;--fair:#f1c40f;--weak:#e67e22;--bad:#e74c3c}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:16px -apple-system,system-ui,sans-serif;
padding:16px;padding-top:max(16px,env(safe-area-inset-top))}
.wrap{max-width:900px;margin:0 auto;display:grid;gap:12px}
.card{background:var(--card);border-radius:14px;padding:16px}
.status{display:flex;align-items:center;gap:8px;color:var(--muted);font-size:14px}
.dot{width:10px;height:10px;border-radius:50%;background:var(--bad)}
.hero{display:flex;align-items:center;justify-content:space-between;gap:16px;flex-wrap:wrap}
.big{font-size:84px;font-weight:700;line-height:1;font-variant-numeric:tabular-nums}
.big small{font-size:24px;color:var(--muted);font-weight:500}
.quality{font-size:22px;font-weight:600;margin-top:6px}
.bars{display:flex;align-items:flex-end;gap:6px;height:80px}
.bars div{width:18px;border-radius:4px;background:var(--line)}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:12px}
.stat .k{font-size:13px;color:var(--muted)}
.stat .v{font-size:28px;font-weight:600;font-variant-numeric:tabular-nums}
canvas{width:100%;height:220px;display:block}
button{font:inherit;font-weight:600;border:0;border-radius:10px;padding:12px 18px;
background:#3b82f6;color:#fff}
button.sec{background:var(--line);color:var(--text)}
.row{display:flex;gap:10px;flex-wrap:wrap}
table{width:100%;border-collapse:collapse;font-size:15px;font-variant-numeric:tabular-nums}
td,th{text-align:left;padding:8px 6px;border-bottom:1px solid var(--line)}
th{color:var(--muted);font-weight:500;font-size:13px}
.hint{color:var(--muted);font-size:13px}
</style></head><body><div class="wrap">

<div class="status"><span class="dot" id="dot"></span><span id="status">Connecting...</span></div>

<div class="card hero">
  <div>
    <div class="big"><span id="rssi">--</span> <small>dBm</small></div>
    <div class="quality" id="quality">Waiting for sender</div>
  </div>
  <div class="bars" id="bars"></div>
</div>

<div class="grid">
  <div class="card stat"><div class="k">Packet loss (5 s)</div><div class="v" id="loss">--</div></div>
  <div class="card stat"><div class="k">Sender hears us</div><div class="v" id="ack">--</div></div>
  <div class="card stat"><div class="k">Best / worst</div><div class="v" id="minmax">--</div></div>
  <div class="card stat"><div class="k">Packets rx / lost</div><div class="v" id="counts">--</div></div>
</div>

<div class="card">
  <div class="k hint" style="margin-bottom:8px">Signal over the last 2 minutes</div>
  <canvas id="chart"></canvas>
</div>

<div class="card">
  <div class="row" style="margin-bottom:10px">
    <button id="mark">Log this spot</button>
    <button class="sec" id="reset">Reset stats</button>
    <button class="sec" id="clear">Clear log</button>
  </div>
  <div class="hint">Walk the sender away, stop, and tap "Log this spot" to save the reading with a distance or note.</div>
  <table><thead><tr><th>Time</th><th>Note</th><th>RSSI</th><th>Back</th><th>Loss</th></tr></thead>
  <tbody id="log"></tbody></table>
</div>

</div><script>
const $=id=>document.getElementById(id);
const hist=[];const MAXH=240; // 2 min at 2 readings/s
let last=null;

for(let i=0;i<5;i++){const b=document.createElement('div');b.style.height=(20+i*15)+'px';$('bars').appendChild(b)}

function grade(r){
  if(r>=-60)return['Excellent',5,'--good'];
  if(r>=-70)return['Good',4,'--ok'];
  if(r>=-80)return['Fair',3,'--fair'];
  if(r>=-88)return['Weak',2,'--weak'];
  return['Very weak',1,'--bad'];
}
const col=v=>getComputedStyle(document.documentElement).getPropertyValue(v);

function render(d){
  const ok=d.connected;
  $('dot').style.background=col(ok?'--good':'--bad');
  $('status').textContent=ok?'Receiving from sender':(d.have?'Signal lost '+(d.age/1000).toFixed(0)+' s ago':'No sender heard yet');
  let bars=0;
  if(ok){
    const [q,n,c]=grade(d.avg);bars=n;
    $('rssi').textContent=d.avg;$('rssi').style.color=col(c);
    $('quality').textContent=q;$('quality').style.color=col(c);
  }else{
    $('rssi').textContent='--';$('rssi').style.color='';
    $('quality').textContent=d.have?'Out of range':'Waiting for sender';$('quality').style.color=col('--bad');
  }
  [...$('bars').children].forEach((b,i)=>b.style.background=i<bars?col(grade(d.avg)[2]):col('--line'));
  $('loss').textContent=d.loss.toFixed(0)+'%';
  $('ack').textContent=d.ackOk?d.ack+' dBm':'--';
  $('minmax').textContent=d.max>-127?d.max+' / '+d.min:'--';
  $('counts').textContent=d.rx+' / '+d.lost;
  hist.push(ok?d.avg:null);if(hist.length>MAXH)hist.shift();
  draw();
}

function draw(){
  const c=$('chart'),dpr=window.devicePixelRatio||1,w=c.clientWidth,h=c.clientHeight;
  c.width=w*dpr;c.height=h*dpr;const g=c.getContext('2d');g.scale(dpr,dpr);
  const top=-30,bot=-100,y=v=>(top-v)/(top-bot)*h;
  g.font='11px -apple-system,sans-serif';g.fillStyle=col('--muted');g.strokeStyle=col('--line');g.lineWidth=1;
  for(let v=-40;v>=-100;v-=20){g.beginPath();g.moveTo(32,y(v));g.lineTo(w,y(v));g.stroke();g.fillText(v,0,y(v)+4)}
  const x=i=>32+(w-32)*i/(MAXH-1);
  g.lineWidth=2.5;g.strokeStyle='#3b82f6';g.beginPath();let pen=false;
  hist.forEach((v,i)=>{if(v===null){pen=false;return}
    const px=x(i+MAXH-hist.length),py=y(Math.max(bot,Math.min(top,v)));
    pen?g.lineTo(px,py):g.moveTo(px,py);pen=true});
  g.stroke();
}

async function poll(){
  try{
    const r=await fetch('/data',{cache:'no-store'});last=await r.json();render(last);
  }catch(e){
    $('dot').style.background=col('--bad');$('status').textContent='iPad lost connection to receiver Wi-Fi';
  }
  setTimeout(poll,500);
}

// Spot log, kept on this iPad
let log=[];
try{log=JSON.parse(localStorage.getItem('rangeLog')||'[]')}catch(e){}
function saveLog(){try{localStorage.setItem('rangeLog',JSON.stringify(log))}catch(e){}}
function showLog(){
  $('log').innerHTML=log.map(r=>`<tr><td>${r.t}</td><td>${r.n}</td><td>${r.r}</td><td>${r.a}</td><td>${r.l}</td></tr>`).join('');
}
const esc=s=>s.replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
$('mark').onclick=()=>{
  if(!last)return;
  const n=prompt('Distance or note for this spot (e.g. 150 m):','')||'';
  log.unshift({t:new Date().toLocaleTimeString([], {hour:'2-digit',minute:'2-digit'}),n:esc(n),
    r:last.connected?last.avg+' dBm':'no signal',a:last.ackOk?last.ack+' dBm':'--',l:last.loss.toFixed(0)+'%'});
  saveLog();showLog();
};
$('clear').onclick=()=>{if(confirm('Clear the spot log?')){log=[];saveLog();showLog()}};
$('reset').onclick=()=>fetch('/reset');
window.addEventListener('resize',draw);
showLog();poll();
</script></body></html>)HTML";
