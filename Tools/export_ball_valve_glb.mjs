// 把气动球阀爆炸图（打包好的单个 html，three.js 和应用代码都压缩在里面）导出成 glb，给 Tools/import_model.py 导进 UE。
//
// 用法：node Tools/export_ball_valve_glb.mjs [球阀 html] [输出 glb]
//   html 默认 ITBasics/气动阀门爆炸图/气动球阀爆炸图.html，找不到再找 C:/work/codex_localAI/work/orig/pbv.html
//   需要一个有 three、esbuild、puppeteer-core 的 node_modules：环境变量 VALVE_NODE_MODULES，默认 C:/work/codex_localAI/work/node_modules
//
// 做法：不改源码，只在页面里插一行钩子，把应用闭包里的零件表（xi）、装配根节点（uo）、状态（xt）和
// "按状态摆零件"的函数（Bc）挂到 window 上；再额外打包一份 three + GLTFExporter（页面里的 three 是压缩过的，拿不到导出器），
// 用无头 Chrome 打开页面：状态拨到"装配、阀门全关"，把每个零件实例的网格按装配位置烘焙进顶点，用第二份 three 重建并导出。
// 导出约定和蝶阀 / 电机的 glb 一样（每个零件实例一个节点，P<编号>_<英文名>[_<第几个>]，米，Y 朝上；
// extras 里有 part_no / name_cn / material_spec / qty / desc / ex），导入脚本共用。
//
// 页面压缩后的变量名（xi / uo / xt / Bc）是这一版 html 的；网页重新打包后变量名会变，钩子插不上时这里会直接报错。
import { spawnSync } from "node:child_process";
import { createRequire } from "node:module";
import { access, mkdir, readFile, writeFile } from "node:fs/promises";
import path from "node:path";
import { pathToFileURL } from "node:url";

const exists = async (p) => access(p).then(() => true, () => false);
const candidates = [
  process.argv[2],
  path.resolve(process.env.ITBASICS_DIR || "C:/work/ITBasics", "气动阀门爆炸图", "气动球阀爆炸图.html"),
  "C:/work/codex_localAI/气动球阀爆炸图.html",
  "C:/work/codex_localAI/work/orig/pbv.html",
].filter(Boolean);
let htmlFile = null;
for (const c of candidates) if (await exists(c)) { htmlFile = path.resolve(c); break; }
if (!htmlFile) throw new Error("找不到球阀 html，试过：" + candidates.join("，"));
const modules = path.resolve(process.env.VALVE_NODE_MODULES || "C:/work/codex_localAI/work/node_modules");
const buildDir = path.resolve(path.dirname(process.argv[3] || path.join(modules, "..", ".build", "x")));
const outFile = path.resolve(process.argv[3] || path.join(buildDir, "ball_valve.glb"));
await mkdir(buildDir, { recursive: true });
const require = createRequire(path.join(modules, "noop.js"));
const puppeteer = require("puppeteer-core");

// 零件编号 → 英文节点名（UE 资产名只能用英文）
const EN = {
  1: "Body", 2: "EndCap", 3: "Ball", 4: "Seat", 5: "BodyGasket", 6: "Stud", 7: "HexNut", 8: "Stem", 9: "ThrustWasher",
  10: "StemPacking", 11: "Gland", 12: "DiscSpring", 13: "StemNut", 14: "Coupling", 15: "MountBolt", 16: "ActuatorBody",
  17: "Pinion", 18: "PistonRack", 19: "ReturnSpring", 20: "ActuatorCap", 21: "CapScrew", 22: "StrokeStop", 23: "Circlip",
  24: "PositionIndicator", 25: "SolenoidValve", 26: "Connector", 27: "Silencer", 28: "PushInFitting", 29: "SolenoidScrew",
};

// 1) 第二份 three + GLTFExporter + mergeGeometries，挂在 window.T2
const vendorSrc = path.join(buildDir, "ball_valve_vendor.js");
await writeFile(vendorSrc, `
import * as THREE from "three";
import { mergeGeometries } from "three/examples/jsm/utils/BufferGeometryUtils.js";
import { GLTFExporter } from "three/examples/jsm/exporters/GLTFExporter.js";
window.T2 = Object.assign({}, THREE, { mergeGeometries, GLTFExporter });
`);
const esbuild = path.join(modules, "@esbuild", "win32-x64", "esbuild.exe");
const vendorOut = path.join(buildDir, "ball_valve_vendor.bundle.js");
const r = spawnSync(esbuild, [vendorSrc, "--bundle", "--format=iife", "--target=es2020", "--charset=utf8", `--outfile=${vendorOut}`],
  { stdio: "inherit", cwd: path.dirname(modules) });
if (r.status !== 0) process.exit(r.status ?? 1);

// 2) 页面：插钩子 + 第二份 three + 导出函数
let html = await readFile(htmlFile, "utf8");
const anchor = "var xi=[],Hs=[],gn=[0,150,0,.16];";
if (!html.includes(anchor)) throw new Error("球阀页面里找不到零件表的定义（" + anchor + "），网页可能重新打包过，要更新这个脚本里的钩子");
html = html.replace(anchor, anchor + "window.__pbv={parts:()=>xi,root:()=>uo,state:()=>xt,apply:()=>Bc()};");
const exportJs = `
window.__exportGLB = async (EN) => {
  const T = window.T2;
  // 装配、阀门全关（球孔和管道垂直，活塞在中间）
  const st = window.__pbv.state();
  st.t = 0; st.tTarget = null; st.th = 0; st.thTarget = 0;
  window.__pbv.apply();
  const root = window.__pbv.root();
  root.updateMatrixWorld(true);
  const toRoot = new T.Matrix4().fromArray(root.matrixWorld.clone().invert().toArray());
  const out = new T.Scene();
  const mats = new Map();
  const getMat = (m) => {
    const key = [m.color.getHexString(), m.metalness.toFixed(2), m.roughness.toFixed(2)].join("_");
    if (!mats.has(key)) {
      // 和蝶阀一样：UE 工作室里没有反射探针，镜面金属会反射成黑块，金属度封顶 0.8、粗糙度至少 0.25
      const s = new T.MeshStandardMaterial({ color: new T.Color(m.color.getHex()), metalness: Math.min(m.metalness, 0.8),
        roughness: Math.max(m.roughness, 0.25), side: T.DoubleSide });
      s.name = "M_BallValve_" + key;
      mats.set(key, s);
    }
    return mats.get(key);
  };
  for (const p of window.__pbv.parts()) {
    p.inst.forEach((r, k) => {
      const geos = [], list = [];
      r.g.updateMatrixWorld(true);
      r.g.traverse((o) => {
        if (!o.isMesh || !o.visible) return;
        let src = o.geometry.index ? o.geometry.toNonIndexed() : o.geometry;
        const g = new T.BufferGeometry();
        g.setAttribute("position", new T.BufferAttribute(new Float32Array(src.attributes.position.array), 3));
        if (src.attributes.normal) g.setAttribute("normal", new T.BufferAttribute(new Float32Array(src.attributes.normal.array), 3));
        else g.computeVertexNormals();
        const m = new T.Matrix4().fromArray(o.matrixWorld.toArray());
        g.applyMatrix4(new T.Matrix4().multiplyMatrices(toRoot, m));
        g.scale(0.001, 0.001, 0.001);   // 毫米 → 米
        geos.push(g);
        list.push(getMat(Array.isArray(o.material) ? o.material[0] : o.material));
      });
      if (!geos.length) return;
      const mesh = new T.Mesh(T.mergeGeometries(geos, true), list);
      const base = "P" + String(p.no).padStart(2, "0") + "_" + EN[p.no];
      mesh.name = p.inst.length > 1 ? base + "_" + (k + 1) : base;
      mesh.userData = {
        part_no: p.no, name_cn: p.cn, name_en: EN[p.no], material_spec: p.m, qty: p.qty, desc: p.desc, group: "", spin: 0,
        ex: JSON.stringify(r.ex.map((e) => [e.v.x, e.v.y, e.v.z, e.s])),
      };
      out.add(mesh);
    });
  }
  const buf = await new T.GLTFExporter().parseAsync(out, { binary: true });
  const bytes = new Uint8Array(buf);
  let s = "";
  for (let i = 0; i < bytes.length; i += 0x8000) s += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
  return { b64: btoa(s), nodes: out.children.length, materials: mats.size };
};`;
const vendorJs = await readFile(vendorOut, "utf8");
html = html.replace("</body>", () => "<script>" + vendorJs + "</script><script>" + exportJs + "</script></body>");
const page = path.join(buildDir, "ball_valve_export.html");
await writeFile(page, html, "utf8");

// 3) 无头浏览器跑导出
const exe = ["C:/Program Files/Google/Chrome/Application/chrome.exe", "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe"];
let chrome = null;
for (const e of exe) if (await exists(e)) { chrome = e; break; }
const browser = await puppeteer.launch({ executablePath: chrome, headless: true, args: ["--use-angle=swiftshader", "--enable-unsafe-swiftshader"] });
try {
  const tab = await browser.newPage();
  tab.on("pageerror", (e) => console.error("[pageerror]", e.message));
  await tab.goto(pathToFileURL(page).href, { waitUntil: "load", timeout: 60000 });
  const ok = await tab.evaluate(() => !!(window.__pbv && window.T2 && window.__exportGLB));
  if (!ok) throw new Error("页面钩子没生效");
  const res = await tab.evaluate((en) => window.__exportGLB(en), EN);
  await writeFile(outFile, Buffer.from(res.b64, "base64"));
  console.log(`[OK] ${htmlFile} → ${outFile}  ${res.nodes} 个零件节点，${res.materials} 种材质，${Math.round(res.b64.length * 0.75 / 1024)} KB`);
} finally {
  await browser.close();
}
