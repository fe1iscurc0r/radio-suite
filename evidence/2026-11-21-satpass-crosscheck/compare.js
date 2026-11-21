// A-2.1 交叉核对：用 satellite.js（独立 SGP4 实现）复算过境窗口，
// 与仓内 radio::satellite::find_passes 的输出（satpass.exe）逐过境比对。
const fs = require("fs");
const sat = require("satellite.js");

const EL_FLOOR_DEG = 5.0;
const OBS = {
  longitude: sat.degreesToRadians(116.4),
  latitude: sat.degreesToRadians(39.9),
  height: 0.05, // km
};

function tleOf(path) {
  const lines = fs.readFileSync(path, "utf8").replace(/\r/g, "").split("\n");
  return { l1: lines[1].trim(), l2: lines[2].trim() };
}

function epochMs(l1) {
  const yy = 2000 + parseInt(l1.slice(18, 20), 10);
  const doy = parseFloat(l1.slice(20, 32));
  return Date.UTC(yy, 0, 1) + (doy - 1) * 86400000;
}

function elDegAt(satrec, ms) {
  const pv = sat.propagate(satrec, new Date(ms));
  if (!pv || !pv.position) return NaN;
  const gmst = sat.gstime(new Date(ms));
  const ecf = sat.eciToEcf(pv.position, gmst);
  const la = sat.ecfToLookAngles(OBS, ecf);
  return {
    el: sat.radiansToDegrees(la.elevation),
    range: la.rangeSat,
    az: sat.radiansToDegrees(la.azimuth),
  };
}

function bisect(f, a, b, iters = 40) {
  // f(a) 与 f(b) 异号时收敛到零点；这里用「el - floor」
  let fa = f(a);
  for (let i = 0; i < iters; i++) {
    const m = (a + b) / 2;
    const fm = f(m);
    if ((fa <= 0 && fm <= 0) || (fa > 0 && fm > 0)) { a = m; fa = fm; }
    else b = m;
  }
  return (a + b) / 2;
}

function findPasses(satrec, t0, t1) {
  const step = 10_000; // 10 s 粗扫
  const out = [];
  let inPass = false, aos = 0, prevEl = elDegAt(satrec, t0).el;
  for (let t = t0 + step; t <= t1; t += step) {
    const cur = elDegAt(satrec, t).el;
    if (!inPass && prevEl < EL_FLOOR_DEG && cur >= EL_FLOOR_DEG) {
      aos = bisect((x) => elDegAt(satrec, x).el - EL_FLOOR_DEG, t - step, t);
      inPass = true;
    } else if (inPass && prevEl >= EL_FLOOR_DEG && cur < EL_FLOOR_DEG) {
      const los = bisect((x) => elDegAt(satrec, x).el - EL_FLOOR_DEG, t - step, t);
      // 1 s 细扫定 TCA / max_el / min_range
      let best = { el: -90, t: 0 }, minR = Infinity;
      for (let s = aos; s <= los; s += 1000) {
        const v = elDegAt(satrec, s);
        if (v.el > best.el) best = { el: v.el, t: s };
        if (v.range < minR) minR = v.range;
      }
      out.push({ aos, tca: best.t, los, maxEl: best.el, minRange: minR });
      inPass = false;
    }
    prevEl = cur;
  }
  return out;
}

function parseOurs(path) {
  return fs.readFileSync(path, "utf8").split("\n")
    .filter((l) => l.startsWith("PASS "))
    .map((l) => {
      const g = (k) => {
        const m = l.match(new RegExp(k + "=([0-9.\\-]+)"));
        return m ? parseFloat(m[1]) : NaN;
      };
      const iso = (k) => {
        const m = l.match(new RegExp(k + "=([0-9\\-]+ [0-9:.]+) UTC"));
        return Date.parse(m[1].replace(" ", "T") + "Z");
      };
      return { aos: iso("aos"), tca: iso("tca"), los: iso("los"),
               maxEl: g("max_el"), minRange: g("min_range") };
    });
}

function report(name, tlePath, oursPath) {
  const { l1, l2 } = tleOf(tlePath);
  const satrec = sat.twoline2satrec(l1, l2);
  const ep = epochMs(l1);
  const ours = parseOurs(oursPath);
  // 窗口与 satpass.exe 对齐：从 495.6 min 起的 24 h（ISS）；SO-50 用 1119.9 min
  const fromMin = name === "ISS" ? 495.6 : 1119.9;
  const t0 = ep + fromMin * 60_000;
  const mine = findPasses(satrec, t0, t0 + 24 * 3600 * 1000);

  console.log(`\n== ${name}  ours=${ours.length}  satjs=${mine.length}`);
  console.log("  #   dAOS(s)  dTCA(s)  dLOS(s)  dMaxEl(deg)  dMinRange(km)");
  for (let i = 0; i < Math.min(ours.length, mine.length); i++) {
    const o = ours[i], m = mine[i];
    console.log(
      `  ${i + 1}  ${((m.aos - o.aos) / 1000).toFixed(1).padStart(8)}` +
      `  ${((m.tca - o.tca) / 1000).toFixed(1).padStart(8)}` +
      `  ${((m.los - o.los) / 1000).toFixed(1).padStart(8)}` +
      `  ${(m.maxEl - o.maxEl).toFixed(3).padStart(12)}` +
      `  ${(m.minRange - o.minRange).toFixed(1).padStart(13)}`);
  }
  if (ours.length !== mine.length) console.log("  !! 条数不一致，人工核对窗口边界");
}

report("ISS", "iss.tle", "ours_iss.txt");
report("SO-50", "so50.tle", "ours_so50.txt");
