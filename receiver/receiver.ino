/*
 * ESP32 Range Test - RECEIVER (the one that stays with you and the iPad)
 *
 * - Listens for the sender's ESP-NOW packets and measures their strength (RSSI)
 *   and how many packets are lost.
 * - Replies to every packet so the sender's side of the link can be measured too.
 * - Creates its own Wi-Fi network. Join it from the iPad and open
 *   http://192.168.4.1 in Safari to see live signal strength.
 * - Shows the sender's solar panel readings (INA219) and keeps a one-minute
 *   solar log for the last 24 hours, which the iPad can download as a CSV file.
 *
 * Hardware: ESP32-U module (external antenna) + battery. Nothing else.
 * Board:    "ESP32 Dev Module" in Arduino IDE, esp32 core 3.x by Espressif.
 */

#include <WiFi.h>
#include <WebServer.h>
#include <esp_now.h>
#include <esp_wifi.h>

// ---- Settings (channel and magic must match sender.ino) ----
#define WIFI_CHANNEL   1
#define AP_SSID        "ESP32-RangeTest"
#define AP_PASSWORD    "rangetest"     // at least 8 characters
#define PACKET_MAGIC   0x52414E47UL
#define SEND_INTERVAL  100             // must match the sender, used for loss %
#define LOSS_WINDOW_MS 5000            // packet loss is measured over the last 5 s
#define SOLAR_LOG_MIN  1440            // keep 24 hours of one-minute solar averages

// ---- Web page shown on the iPad (served at http://192.168.4.1) ----
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
.sun{color:#f5b301}
h2{margin:0 0 10px;font-size:18px}
.head{display:flex;justify-content:space-between;align-items:center;gap:10px;flex-wrap:wrap;margin-bottom:8px}
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
  <h2>&#9728;&#65038; Solar panel</h2>
  <div class="big sun"><span id="watts">--</span> <small>W</small></div>
  <div class="hint" id="solarNote" style="margin-top:6px"></div>
</div>
<div class="grid">
  <div class="card stat"><div class="k">Voltage</div><div class="v" id="volts">--</div></div>
  <div class="card stat"><div class="k">Current</div><div class="v" id="amps">--</div></div>
  <div class="card stat"><div class="k">Energy collected</div><div class="v" id="energy">--</div></div>
  <div class="card stat"><div class="k">Peak power</div><div class="v" id="peak">--</div></div>
</div>
<div class="card">
  <div class="head">
    <div class="hint">Solar power, one-minute averages (up to 24 hours)</div>
    <button class="sec" id="csv">Download CSV</button>
  </div>
  <canvas id="solarChart"></canvas>
  <div class="hint" id="logInfo" style="margin-top:6px"></div>
</div>

<div class="card">
  <div class="row" style="margin-bottom:10px">
    <button id="mark">Log this spot</button>
    <button class="sec" id="reset">Reset stats</button>
    <button class="sec" id="clear">Clear log</button>
  </div>
  <div class="hint">Walk the sender away, stop, and tap "Log this spot" to save the reading with a distance or note.</div>
  <table><thead><tr><th>Time</th><th>Note</th><th>RSSI</th><th>Back</th><th>Loss</th><th>Solar</th></tr></thead>
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
  const sOk=d.solarOk&&d.have;
  $('watts').textContent=sOk?(d.mw/1000).toFixed(2):'--';
  $('volts').textContent=sOk?d.v.toFixed(2)+' V':'--';
  $('amps').textContent=sOk?(d.ma>=1000?(d.ma/1000).toFixed(2)+' A':d.ma.toFixed(0)+' mA'):'--';
  $('energy').textContent=sOk?(d.mwh>=1000?(d.mwh/1000).toFixed(2)+' Wh':d.mwh.toFixed(1)+' mWh'):'--';
  $('peak').textContent=sOk?(d.peak/1000).toFixed(2)+' W':'--';
  $('solarNote').textContent=!d.have?'Waiting for sender':!d.solarOk?'Sender has no INA219 sensor (check wiring)':
    (ok?'Live from the sender':'Last reading before signal was lost')+'. Energy counts from when the sender was switched on.';
  if(d.logN!==logN){logN=d.logN;loadSolarLog()}
  hist.push(ok?d.avg:null);if(hist.length>MAXH)hist.shift();
  draw();
}

function draw(){
  const c=$('chart'),dpr=window.devicePixelRatio||1,w=c.clientWidth,h=c.clientHeight;
  c.width=w*dpr;c.height=h*dpr;const g=c.getContext('2d');g.scale(dpr,dpr);
  const top=-30,bot=-105,y=v=>(top-v)/(top-bot)*h;
  g.font='11px -apple-system,sans-serif';g.fillStyle=col('--muted');g.strokeStyle=col('--line');g.lineWidth=1;
  for(let v=-40;v>=-100;v-=20){g.beginPath();g.moveTo(32,y(v));g.lineTo(w,y(v));g.stroke();g.fillText(v,0,y(v)+4)}
  const x=i=>32+(w-32)*i/(MAXH-1);
  g.lineWidth=2.5;g.strokeStyle='#3b82f6';g.beginPath();let pen=false;
  hist.forEach((v,i)=>{if(v===null){pen=false;return}
    const px=x(i+MAXH-hist.length),py=y(Math.max(bot,Math.min(top,v)));
    pen?g.lineTo(px,py):g.moveTo(px,py);pen=true});
  g.stroke();
}

// 24-hour solar log, stored on the receiver
let solar=[],logN=-1,logUp=0;
async function loadSolarLog(){
  try{const r=await fetch('/solarlog',{cache:'no-store'});solar=await r.json();logUp=last?last.up:0;drawSolar()}catch(e){}
}
const realTime=t=>new Date(Date.now()-(logUp/1000-t)*1000);
function drawSolar(){
  const c=$('solarChart'),dpr=window.devicePixelRatio||1,w=c.clientWidth,h=c.clientHeight;
  c.width=w*dpr;c.height=h*dpr;const g=c.getContext('2d');g.scale(dpr,dpr);
  const pad=18,max=Math.max(1,...solar.map(e=>e[3]/1000))*1.1,y=v=>(h-pad)-(v/max)*(h-pad-6);
  g.font='11px -apple-system,sans-serif';g.fillStyle=col('--muted');g.strokeStyle=col('--line');g.lineWidth=1;
  for(let i=0;i<=3;i++){const v=max*i/3;g.beginPath();g.moveTo(36,y(v));g.lineTo(w,y(v));g.stroke();g.fillText(v.toFixed(v<10?1:0)+'W',0,y(v)+4)}
  $('logInfo').textContent=solar.length?solar.length+' minutes recorded. The log is kept while the receiver stays powered on.':'No solar data recorded yet (first point after 1 minute).';
  if(solar.length<1)return;
  const t0=solar[0][0],t1=Math.max(t0+60,solar[solar.length-1][0]),x=t=>36+(w-40)*(t-t0)/(t1-t0);
  const fmt=t=>realTime(t).toLocaleTimeString([],{hour:'2-digit',minute:'2-digit'});
  g.fillText(fmt(t0),36,h-2);const e=fmt(t1);g.fillText(e,w-g.measureText(e).width,h-2);
  g.lineWidth=2.5;g.strokeStyle='#f5b301';g.beginPath();
  solar.forEach((p,i)=>{const px=x(p[0]),py=y(p[3]/1000);
    (i&&p[0]-solar[i-1][0]<=150)?g.lineTo(px,py):g.moveTo(px,py)});
  if(solar.length===1){g.arc(x(solar[0][0]),y(solar[0][3]/1000),3,0,7)}
  g.stroke();
}
$('csv').onclick=()=>{
  const rows=['time,voltage_V,current_mA,power_W,energy_since_sender_on_Wh'];
  solar.forEach(p=>rows.push([realTime(p[0]).toLocaleString().replace(/,/g,''),p[1].toFixed(3),p[2].toFixed(1),(p[3]/1000).toFixed(3),(p[4]/1000).toFixed(4)].join(',')));
  const url=URL.createObjectURL(new Blob([rows.join('\n')],{type:'text/csv'}));
  const a=document.createElement('a');a.href=url;a.download='solar-log.csv';document.body.appendChild(a);a.click();a.remove();
  setTimeout(()=>URL.revokeObjectURL(url),5000);
};

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
  $('log').innerHTML=log.map(r=>`<tr><td>${r.t}</td><td>${r.n}</td><td>${r.r}</td><td>${r.a}</td><td>${r.l}</td><td>${r.s||'--'}</td></tr>`).join('');
}
const esc=s=>s.replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
$('mark').onclick=()=>{
  if(!last)return;
  const n=prompt('Distance or note for this spot (e.g. 150 m):','')||'';
  log.unshift({t:new Date().toLocaleTimeString([], {hour:'2-digit',minute:'2-digit'}),n:esc(n),
    r:last.connected?last.avg+' dBm':'no signal',a:last.ackOk?last.ack+' dBm':'--',l:last.loss.toFixed(0)+'%',
    s:last.solarOk?(last.mw/1000).toFixed(2)+' W':'--'});
  saveLog();showLog();
};
$('clear').onclick=()=>{if(confirm('Clear the spot log?')){log=[];saveLog();showLog()}};
$('reset').onclick=()=>fetch('/reset');
window.addEventListener('resize',()=>{draw();drawSolar()});
showLog();poll();
</script></body></html>)HTML";

struct __attribute__((packed)) RangePacket {
  uint32_t magic;
  uint32_t seq;
  int8_t   ackRssi;
  uint8_t  ackFresh;
  uint32_t uptimeMs;
  uint8_t  solarOk;
  float    solarV;
  float    solarMa;
  float    solarMw;
  float    solarMwh;
  float    solarPeakMw;
};

struct __attribute__((packed)) AckPacket {
  uint32_t magic;
  uint32_t seq;
};

static const uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

WebServer server(80);
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;

// Updated by the ESP-NOW callback (inside the lock)
volatile int8_t   lastRssi     = 0;
volatile int8_t   lastAckRssi  = 0;
volatile uint8_t  lastAckFresh = 0;
volatile uint32_t lastSeq      = 0;
volatile uint32_t lastRxMs     = 0;
volatile uint32_t totalRx      = 0;
volatile uint32_t totalLost    = 0;
volatile bool     havePacket   = false;
volatile bool     ackPending   = false;
volatile uint32_t ackSeq       = 0;
volatile int32_t  rssiSum      = 0;   // for a smoothed average
volatile uint16_t rssiCount    = 0;

int8_t minRssi = 0, maxRssi = -127;

// Latest solar reading from the sender, plus sums for the one-minute average
volatile bool  solarOk = false;
volatile float solarV = 0, solarMa = 0, solarMw = 0, solarMwh = 0, solarPeakMw = 0;
volatile float minSumV = 0, minSumMa = 0, minSumMw = 0;
volatile uint32_t minCount = 0;

// One-minute solar log (lost if the receiver is switched off)
struct SolarEntry { uint32_t tSec; float v, ma, mw, mwh; };
SolarEntry solarLog[SOLAR_LOG_MIN];
uint16_t solarLogHead = 0, solarLogCount = 0;

// Each received packet's arrival time, used to compute recent packet loss
#define RX_LOG_SIZE 128
volatile uint32_t rxLog[RX_LOG_SIZE];
volatile uint16_t rxLogHead = 0;

void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(RangePacket)) return;
  RangePacket p;
  memcpy(&p, data, sizeof(p));
  if (p.magic != PACKET_MAGIC) return;

  int8_t rssi = info->rx_ctrl->rssi;
  uint32_t now = millis();

  portENTER_CRITICAL(&lock);
  if (havePacket && p.seq > lastSeq) {
    totalLost += p.seq - lastSeq - 1;
  } else if (havePacket && p.seq < lastSeq) {
    // Sender was restarted; start counting again
    totalLost = 0; totalRx = 0;
  }
  lastSeq      = p.seq;
  lastRssi     = rssi;
  lastAckRssi  = p.ackRssi;
  lastAckFresh = p.ackFresh;
  lastRxMs     = now;
  totalRx++;
  havePacket   = true;
  rssiSum     += rssi;
  rssiCount++;
  rxLog[rxLogHead] = now;
  rxLogHead = (rxLogHead + 1) % RX_LOG_SIZE;
  ackSeq     = p.seq;
  ackPending = true;
  solarOk    = p.solarOk;
  if (p.solarOk) {
    solarV = p.solarV; solarMa = p.solarMa; solarMw = p.solarMw;
    solarMwh = p.solarMwh; solarPeakMw = p.solarPeakMw;
    minSumV += p.solarV; minSumMa += p.solarMa; minSumMw += p.solarMw; minCount++;
  }
  portEXIT_CRITICAL(&lock);
}

void handleRoot() {
  server.send_P(200, "text/html", PAGE_HTML);
}

void handleData() {
  uint32_t now = millis();

  portENTER_CRITICAL(&lock);
  bool     have     = havePacket;
  int8_t   rssi     = lastRssi;
  int8_t   ackRssi  = lastAckRssi;
  uint8_t  ackFresh = lastAckFresh;
  uint32_t seq      = lastSeq;
  uint32_t rxMs     = lastRxMs;
  uint32_t rx       = totalRx;
  uint32_t lost     = totalLost;
  int32_t  sum      = rssiSum;
  uint16_t cnt      = rssiCount;
  rssiSum = 0; rssiCount = 0;
  bool  sOk = solarOk;
  float sV = solarV, sMa = solarMa, sMw = solarMw, sMwh = solarMwh, sPeak = solarPeakMw;
  uint16_t recent = 0;
  for (int i = 0; i < RX_LOG_SIZE; i++) {
    if (rxLog[i] != 0 && now - rxLog[i] < LOSS_WINDOW_MS) recent++;
  }
  portEXIT_CRITICAL(&lock);

  uint32_t age = have ? now - rxMs : 0;
  bool connected = have && age < 2000;

  // Average RSSI of all packets since the iPad last asked (smoother than one packet)
  int avg = cnt ? (int)(sum / cnt) : rssi;
  if (connected) {
    if (avg < minRssi || minRssi == 0) minRssi = avg;
    if (avg > maxRssi) maxRssi = avg;
  }

  // Loss over the last few seconds: packets we got vs. packets the sender sent
  uint32_t expected = LOSS_WINDOW_MS / SEND_INTERVAL;
  if (now < LOSS_WINDOW_MS) expected = now / SEND_INTERVAL;
  float loss = expected ? 100.0f * (1.0f - (float)recent / expected) : 0;
  if (loss < 0) loss = 0;
  if (!have) loss = 100;

  char json[512];
  snprintf(json, sizeof(json),
    "{\"have\":%s,\"connected\":%s,\"rssi\":%d,\"avg\":%d,\"min\":%d,\"max\":%d,"
    "\"ack\":%d,\"ackOk\":%s,\"loss\":%.1f,\"rx\":%lu,\"lost\":%lu,\"seq\":%lu,\"age\":%lu,"
    "\"up\":%lu,\"solarOk\":%s,\"v\":%.3f,\"ma\":%.1f,\"mw\":%.1f,\"mwh\":%.2f,\"peak\":%.1f,"
    "\"logN\":%u}",
    have ? "true" : "false", connected ? "true" : "false",
    rssi, avg, minRssi, maxRssi, ackRssi, (ackFresh && connected) ? "true" : "false",
    loss, (unsigned long)rx, (unsigned long)lost, (unsigned long)seq, (unsigned long)age,
    (unsigned long)now, sOk ? "true" : "false", sV, sMa, sMw, sMwh, sPeak, solarLogCount);

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

// Whole solar log as JSON: [[secondsSinceReceiverStart, V, mA, mW, mWh], ...] oldest first
void handleSolarLog() {
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  String chunk = "[";
  uint16_t start = (solarLogHead + SOLAR_LOG_MIN - solarLogCount) % SOLAR_LOG_MIN;
  for (uint16_t i = 0; i < solarLogCount; i++) {
    const SolarEntry &e = solarLog[(start + i) % SOLAR_LOG_MIN];
    char row[80];
    snprintf(row, sizeof(row), "%s[%lu,%.3f,%.1f,%.1f,%.2f]", i ? "," : "",
             (unsigned long)e.tSec, e.v, e.ma, e.mw, e.mwh);
    chunk += row;
    if (chunk.length() > 1200) { server.sendContent(chunk); chunk = ""; }
  }
  chunk += "]";
  server.sendContent(chunk);
  server.sendContent("");   // end of response
}

// Every minute, save the average solar reading of that minute
void updateSolarLog() {
  static uint32_t lastMinute = 0;
  uint32_t now = millis();
  if (now - lastMinute < 60000) return;
  lastMinute = now;

  portENTER_CRITICAL(&lock);
  uint32_t n = minCount;
  float v = minSumV, ma = minSumMa, mw = minSumMw, mwh = solarMwh;
  minSumV = minSumMa = minSumMw = 0; minCount = 0;
  portEXIT_CRITICAL(&lock);

  if (n == 0) return;   // nothing heard from the sender this minute
  solarLog[solarLogHead] = { now / 1000, v / n, ma / n, mw / n, mwh };
  solarLogHead = (solarLogHead + 1) % SOLAR_LOG_MIN;
  if (solarLogCount < SOLAR_LOG_MIN) solarLogCount++;
}

void handleReset() {
  portENTER_CRITICAL(&lock);
  totalRx = 0; totalLost = 0;
  portEXIT_CRITICAL(&lock);
  minRssi = 0; maxRssi = -127;
  server.send(200, "text/plain", "ok");
}

void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD, WIFI_CHANNEL);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.setSleep(false);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed, restarting");
    delay(1000);
    ESP.restart();
  }
  esp_now_register_recv_cb(onReceive);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, BROADCAST, 6);
  peer.channel = WIFI_CHANNEL;
  peer.ifidx   = WIFI_IF_AP;
  peer.encrypt = false;
  esp_now_add_peer(&peer);

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/reset", handleReset);
  server.on("/solarlog", handleSolarLog);
  server.begin();

  Serial.printf("Receiver ready. Join Wi-Fi \"%s\" (password \"%s\") and open http://%s\n",
                AP_SSID, AP_PASSWORD, WiFi.softAPIP().toString().c_str());
}

void loop() {
  server.handleClient();
  updateSolarLog();

  // Reply to the sender (done here, not in the callback, as ESP-NOW recommends)
  if (ackPending) {
    AckPacket ack;
    portENTER_CRITICAL(&lock);
    ack.seq = ackSeq;
    ackPending = false;
    portEXIT_CRITICAL(&lock);
    ack.magic = PACKET_MAGIC;
    esp_now_send(BROADCAST, (uint8_t *)&ack, sizeof(ack));
  }

  static uint32_t lastPrint = 0;
  if (millis() - lastPrint > 1000) {
    lastPrint = millis();
    Serial.printf("rssi %d dBm  rx %lu  lost %lu  solar %.2f V %.0f mA %.2f W\n", lastRssi,
                  (unsigned long)totalRx, (unsigned long)totalLost,
                  (float)solarV, (float)solarMa, (float)solarMw / 1000.0f);
  }
}
