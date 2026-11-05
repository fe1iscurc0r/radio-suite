/* radio-suite PWA 薄片（273 阶段二）——消费本仓总线 /api/state · /api/spectrum · /api/control */
'use strict';

const $ = id => document.getElementById(id);
const stateEl = $('state'), connEl = $('conn'), latEl = $('latency');

/* ---- 状态轮询（250ms）+ 延迟测量 ---- */
let lastRtt = null;
async function pollState() {
  const t0 = performance.now();
  try {
    const r = await fetch('/api/state');
    const j = await r.json();
    lastRtt = Math.round(performance.now() - t0);
    $('freq').textContent = fmtFreq(j.freq_hz);
    $('mode').textContent = j.mode + '/' + (j.filter ?? '');
    $('smeter').textContent = 'S' + (j.smeter_raw * 9 / 120).toFixed(1);
    $('pttstate').textContent = j.ptt_on ? 'TX' : 'RX';
    $('pttstate').className = j.ptt_on ? 'tx' : 'rx';
    connEl.textContent = '已连接 · RTT ' + lastRtt + 'ms';
  } catch {
    connEl.textContent = '总线不可达（重试中）';
  }
}
function fmtFreq(hz) {
  if (hz == null) return '—.———.———';
  const mhz = (hz / 1e6).toFixed(6).padStart(10, '0');
  return mhz.slice(0, 3) + '.' + mhz.slice(3, 6) + '.' + mhz.slice(6);
}

/* ---- 频谱：SSE 实时推送优先（273"M5 全量再上 SSE"提前落地），轮询兜底 ---- */
const cv = $('spectrum'), g = cv.getContext('2d');
let sseMode = false;
function drawSpectrum(d) {
  const n = d.length;
  g.fillStyle = '#070a10';
  g.fillRect(0, 0, cv.width, cv.height);
  g.strokeStyle = '#6f6';
  g.beginPath();
  for (let i = 0; i < n; i++) {
    const x = i / n * cv.width;
    const y = cv.height - Math.max(0, Math.min(1, (d[i] + 110) / 100)) * (cv.height - 4);
    i ? g.lineTo(x, y) : g.moveTo(x, y);
  }
  g.stroke();
}
async function pollSpectrum() {
  if (sseMode) return;  // SSE 已接管
  try {
    const r = await fetch('/api/spectrum');
    const j = await r.json();
    drawSpectrum(j.power_db);
  } catch { /* 断线时保留上一帧 */ }
}

/* SSE：服务端每帧推 power_db，断开自动重连（EventSource 原生） */
function openSse() {
  const es = new EventSource('/api/spectrum/stream');
  es.onmessage = e => {
    sseMode = true;
    const j = JSON.parse(e.data);
    if (!j.power_db) return;
    connEl.textContent = '频谱：SSE 实时推送';
    drawSpectrum(j.power_db);
  };
  es.onerror = () => { sseMode = false; };
}

/* ---- 控制出口（走 session 唯一出口，门禁在引擎侧） ---- */
async function control(action, params = {}) {
  const t0 = performance.now();
  const r = await fetch('/api/control', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ action, ...params }),
  });
  const j = await r.json();
  latEl.textContent = 'control RTT ' + Math.round(performance.now() - t0) + 'ms';
  return j;
}

/* PTT 两击确认（引擎侧 SafetyGate 语义）：第一次返回 confirm_pending，第二次 tx */
let pttArmed = false, pttTimer = null;
$('ptt').addEventListener('click', async () => {
  const j = await control('ptt_on');
  if ((j.result?.ptt ?? '') === 'confirm_pending') {
    pttArmed = true;
    $('ptt').textContent = '再按一次确认发射';
    clearTimeout(pttTimer);
    pttTimer = setTimeout(() => {
      pttArmed = false;
      $('ptt').textContent = 'PTT（按两次确认）';
    }, 5000);
    return;
  }
  if (j.result?.ptt === 'tx') {
    pttArmed = false;
    $('ptt').textContent = 'TX 发射中…（点击关）';
    $('ptt').classList.add('live');
    return;
  }
  pttArmed = false;
  $('ptt').textContent = 'PTT（按两次确认）';
  $('ptt').classList.remove('live');
});

/* 调谐 */
$('tunego').addEventListener('click', async () => {
  const hz = parseInt($('freqinput').value, 10);
  if (!Number.isFinite(hz)) return;
  await control('tune', { freq: hz });
});
document.querySelectorAll('#controls [data-step]').forEach(b =>
  b.addEventListener('click', async () => {
    const cur = await (await fetch('/api/state')).json();
    await control('tune', { freq: (cur.freq_hz ?? 0) + Number(b.dataset.step) });
  }));

/* QSO 录入（PWA 内存队列 → /api/qso.adif 导出） */
$('logqso').addEventListener('click', async () => {
  const callsign = $('callinput').value.trim().toUpperCase();
  if (!callsign) return;
  const cur = await (await fetch('/api/state')).json();
  const r = await fetch('/api/qso', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      qso: {
        callsign,
        freq_mhz: ((cur.freq_hz ?? 0) / 1e6).toFixed(6),
        mode: cur.mode ?? '',
        rst_sent: $('rstinput').value || '59',
      },
    }),
  });
  const j = await r.json();
  $('callinput').value = '';
  connEl.textContent = 'QSO 已入队（共 ' + (j.count ?? '?') + ' 条）';
});

/* ---- 调度：状态/频谱并行轮询 ---- */
// openSse 必须在这里点将：不叫它就永远是轮询在跑，上面那套 SSE 分支等于死代码
// （2026-11-04 真机浏览器实测：服务端 chunked 已经能推帧，页面标签却始终不切 SSE）
openSse();
setInterval(pollState, 250);
setInterval(pollSpectrum, 250);
pollState();
pollSpectrum();
