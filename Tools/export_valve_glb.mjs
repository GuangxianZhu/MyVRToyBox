// 把 three.js 写的气动蝶阀爆炸图（codex_localAI/work/valve）导出成 glb，给 Tools/import_model.py 导进 UE。
//
// 用法：node Tools/export_valve_glb.mjs [阀门工程目录] [输出 glb]
//   阀门工程目录默认 C:/work/codex_localAI/work/valve（里面有 src/、build.mjs，../node_modules 里有 three、esbuild、puppeteer-core）
//
// 做法：不改阀门工程的源码。照它的 build.mjs 把 src/*.js 拼成一个页面，额外打包 three 的 GLTFExporter，
// 在应用代码后面接一段导出代码（同一个闭包里，能直接拿到 PARTS / assembly），用无头 Chrome 打开页面跑导出。
// 导出约定和 ITBasics 的 Blender 脚本出的 motor.glb 一样，导入脚本可以共用：
//   - 每个零件实例一个节点，节点名 P<编号>_<英文名>[_<第几个>]，顶点在装配位置、单位米、Y 朝上，节点本身单位变换；
//   - 节点 extras：part_no / name_cn / name_en / material_spec / qty / desc / ex（爆炸路线 JSON，glTF 坐标，毫米）；
//   - 材质按颜色 + 金属度 + 粗糙度合并成标准 PBR 材质（清漆、光泽这些物理材质扩展丢掉）。
import { spawnSync } from "node:child_process";
import { createRequire } from "node:module";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import path from "node:path";
import { pathToFileURL } from "node:url";

const valveDir = path.resolve(process.argv[2] || "C:/work/codex_localAI/work/valve");
const outFile = path.resolve(process.argv[3] || path.join(valveDir, ".build", "valve.glb"));
const modules = path.join(valveDir, "..", "node_modules");
const require = createRequire(path.join(modules, "noop.js"));
const puppeteer = require("puppeteer-core");
const buildDir = path.join(valveDir, ".build");
await mkdir(buildDir, { recursive: true });

// 1) vendor：和阀门工程一样的 three 全家桶，再加 GLTFExporter
const vendorSrc = path.join(buildDir, "vendor_export.js");
await writeFile(vendorSrc, `
import * as THREE from "three";
import { OrbitControls } from "three/examples/jsm/controls/OrbitControls.js";
import { RoomEnvironment } from "three/examples/jsm/environments/RoomEnvironment.js";
import { mergeGeometries, toCreasedNormals } from "three/examples/jsm/utils/BufferGeometryUtils.js";
import { GLTFExporter } from "three/examples/jsm/exporters/GLTFExporter.js";
window.THREE = Object.assign({}, THREE, { OrbitControls, RoomEnvironment, mergeGeometries, toCreasedNormals, GLTFExporter });
`);
const esbuild = path.join(modules, "@esbuild", "win32-x64", "esbuild.exe");
const vendorOut = path.join(buildDir, "vendor_export.bundle.js");
const r = spawnSync(esbuild, [vendorSrc, "--bundle", "--format=iife", "--target=es2020", "--charset=utf8", `--outfile=${vendorOut}`],
  { stdio: "inherit", cwd: valveDir });
if (r.status !== 0) process.exit(r.status ?? 1);

// 2) 导出代码：接在应用代码后面，所以能直接用 PARTS（零件定义 + 实例）、assembly、scene、T
const exportJs = `
window.__exportGLB = async () => {
  // 回到装配状态：位置回 base、阀门全关、弹簧原长
  for (const p of PARTS) for (const r of p.insts) {
    r.g.position.copy(r.base);
    r.g.rotation.y = r.baseRot;
    r.g.scale.set(1, 1, 1);
  }
  scene.updateMatrixWorld(true);
  const toAssembly = assembly.matrixWorld.clone().invert();
  const out = new T.Scene();
  const mats = new Map();
  const getMat = (m) => {
    const key = [m.color.getHexString(), m.metalness.toFixed(2), m.roughness.toFixed(2)].join("_");
    if (!mats.has(key)) {
      // UE 工作室里没有反射探针 / 天光，金属度 1、粗糙度 0.07 的"镜面"会反射出一片黑。金属度封顶 0.8、粗糙度至少 0.25，看起来是抛光金属而不是黑块
      const s = new T.MeshStandardMaterial({ color: m.color.clone(), metalness: Math.min(m.metalness, 0.8), roughness: Math.max(m.roughness, 0.25), side: T.DoubleSide });
      s.name = "M_Valve_" + key;
      mats.set(key, s);
    }
    return mats.get(key);
  };
  for (const p of PARTS) {
    p.insts.forEach((r, k) => {
      const geos = [];
      const list = [];
      r.g.traverse((o) => {
        if (!o.isMesh || !o.visible) return;
        let g = o.geometry.clone();
        if (g.index) g = g.toNonIndexed();
        for (const a of Object.keys(g.attributes)) if (a !== "position" && a !== "normal") g.deleteAttribute(a);
        if (!g.attributes.normal) g.computeVertexNormals();
        g.clearGroups();
        g.applyMatrix4(new T.Matrix4().multiplyMatrices(toAssembly, o.matrixWorld));
        g.scale(0.001, 0.001, 0.001);   // 毫米 → 米
        geos.push(g);
        list.push(getMat(Array.isArray(o.material) ? o.material[0] : o.material));
      });
      if (!geos.length) return;
      const mesh = new T.Mesh(T.mergeGeometries(geos, true), list);
      const base = "P" + String(p.no).padStart(2, "0") + "_" + p.en.replace(/[^A-Za-z0-9]+/g, "");
      mesh.name = p.insts.length > 1 ? base + "_" + (k + 1) : base;
      mesh.userData = {
        part_no: p.no, name_cn: p.cn, name_en: p.en, material_spec: p.mat, qty: p.qty, desc: p.desc, group: "", spin: 0,
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
};
`;

// 3) 照 build.mjs 拼页面
const src = (p) => path.join(valveDir, "src", p);
let appJs = "";
for (const f of ["00-scene.js", "10-parts.js", "20-ui.js"]) appJs += (await readFile(src(f), "utf8")).replace(/\n+$/, "") + "\n\n";
appJs = "(() => {\n'use strict';\ntry {\n" + appJs + exportJs +
  "\n} catch (e) { document.documentElement.dataset.fatal = (e && e.stack) || String(e); console.error(e); }\n})();\n";
const vendorJs = await readFile(vendorOut, "utf8");
const css = await readFile(src("style.css"), "utf8");
const html = (await readFile(src("index.html"), "utf8"))
  .replace("{{CSS}}", () => css)
  .replace("{{VENDOR}}", () => vendorJs)
  .replace("{{APP}}", () => appJs);
const page = path.join(buildDir, "export.html");
await writeFile(page, html, "utf8");

// 4) 无头浏览器跑导出
const exe = ["C:/Program Files/Google/Chrome/Application/chrome.exe", "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe"]
  .find((p) => { try { require("node:fs").accessSync(p); return true; } catch { return false; } });
const browser = await puppeteer.launch({ executablePath: exe, headless: true, args: ["--use-angle=swiftshader", "--enable-unsafe-swiftshader"] });
try {
  const tab = await browser.newPage();
  tab.on("pageerror", (e) => console.error("[pageerror]", e.message));
  await tab.goto(pathToFileURL(page).href, { waitUntil: "load", timeout: 60000 });
  const fatal = await tab.evaluate(() => document.documentElement.dataset.fatal || "");
  if (fatal) throw new Error("页面出错：" + fatal);
  const res = await tab.evaluate(() => window.__exportGLB());
  await writeFile(outFile, Buffer.from(res.b64, "base64"));
  console.log(`[OK] ${outFile}  ${res.nodes} 个零件节点，${res.materials} 种材质，${Math.round(res.b64.length * 0.75 / 1024)} KB`);
} finally {
  await browser.close();
}
