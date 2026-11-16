// N1 定因：保持亮屏时，WSJT-X 单播能否持续到达手机内核。
// 不抢前台、不改设备设置：只用 input keyevent KEYCODE_WAKEUP 维持亮屏，
// 并采样 /proc/net/udp 里 0.0.0.0:2237（0x08BD）那一行的 rx_queue / drops。
//
// 判读：rx_queue 单调增长 = 包在到；涨到 socket 缓冲上限后 drops 才开始动。
//      rx_queue 与 drops 都不动 = 包根本没到手机。
// 用法：node udp-awake-probe.js [秒数]
const dgram = require("dgram");
const { execFile } = require("child_process");

const ADB = process.env.ADB || "D:/Android/sdk/platform-tools/adb.exe";
const PHONE = process.env.PHONE || "192.168.137.177";
const LOCAL = process.env.LOCAL || "192.168.137.1";
const SECONDS = Number(process.argv[2] || 90);

function qstr(s) {
  const b = Buffer.from(s, "utf16le").swap16();
  const len = Buffer.alloc(4);
  len.writeUInt32BE(b.length);
  return Buffer.concat([len, b]);
}
function decodeMsg(ident, timeMs, snr, dt, df, mode, message) {
  const head = Buffer.alloc(12);
  head.writeUInt32BE(0xadbccbda, 0);
  head.writeUInt32BE(3, 4);
  head.writeUInt32BE(1, 8); // MessageType::Decode
  const fixed = Buffer.alloc(21);
  fixed.writeUInt8(1, 0);
  fixed.writeUInt32BE(timeMs, 1);
  fixed.writeInt32BE(snr, 5);
  fixed.writeDoubleBE(dt, 9);
  fixed.writeInt32BE(df, 17);
  return Buffer.concat([head, qstr(ident), fixed, qstr(mode), qstr(message), Buffer.from([0, 0])]);
}

const adb = (args) =>
  new Promise((res) => execFile(ADB, args, { env: { ...process.env, MSYS_NO_PATHCONV: "1" } },
    (e, so) => res(e ? `ERR ${e.message}` : String(so))));

const ts = () => new Date().toTimeString().slice(0, 8);
const log = (...a) => console.log(ts(), ...a);

async function sample(tag) {
  const udp = await adb(["shell", "cat", "/proc/net/udp"]);
  const line = udp.split("\n").find((l) => l.includes(":08BD"));
  if (!line) return log(tag, "NO-SOCKET");
  const f = line.trim().split(/\s+/);
  const [tx, rx] = f[4].split(":");
  log(tag, `rx_queue=${parseInt(rx, 16)} tx_queue=${parseInt(tx, 16)} drops=${f[f.length - 1]}`);
}

(async () => {
  const sock = dgram.createSocket({ type: "udp4", reuseAddr: true });
  await new Promise((r) => sock.bind(0, LOCAL, r));
  log("start", `unicast -> ${PHONE}:2237 for ${SECONDS}s`);
  await sample("t=0");
  const base = Date.now();
  for (let i = 0; i < SECONDS; i++) {
    if (i % 10 === 0) {
      await adb(["shell", "input", "keyevent", "KEYCODE_WAKEUP"]);
      const p = await adb(["shell", "dumpsys", "power"]);
      const m = p.match(/mWakefulness=(\w+)/);
      log(`t=${i}`, "wakefulness=", m ? m[1] : "?");
    }
    const pkt = decodeMsg("RS120-PROBE", 0, -10, 0.2, 1000 + i, "FT8", `CQ N120 T${i}`);
    await new Promise((r) => sock.send(pkt, 2237, PHONE, r));
    if (i % 5 === 4) await sample(`t=${i + 1}`);
    await new Promise((r) => setTimeout(r, 1000));
  }
  await sample("final");
  log("sent_total", SECONDS, "elapsed_s", ((Date.now() - base) / 1000).toFixed(1));
  sock.close();
})();
