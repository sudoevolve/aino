// 组件目录：列出 canvas.fig 里所有顶层 COMPONENT_SET / COMPONENT 的名字与尺寸，
// 供"逐组件对齐"时建立清单（每个顶层组件 = 一类控件，变体在名字里）。
// usage: node catalog.js [过滤词]
const fs = require('fs');
const zlib = require('zlib');
const { decodeBinarySchema, compileSchema } = require('kiwi-schema');

const figPath = ['./canvas.fig', __dirname + '/canvas.fig', __dirname + '/../../../.workbuddy/figma/canvas.fig']
  .find(f => fs.existsSync(f));
if (!figPath) { console.error('找不到 canvas.fig'); process.exit(1); }
const raw = fs.readFileSync(figPath);
let p = 12; const ch = [];
while (p + 4 <= raw.length) { const len = raw.readUInt32LE(p); p += 4; if (!len || p + len > raw.length) break; ch.push(raw.slice(p, p + len)); p += len; }
const infl = b => { for (const f of [zlib.inflateRawSync, zlib.zstdDecompressSync]) { try { return f(b); } catch (e) { } } return b; };
const msg = compileSchema(decodeBinarySchema(infl(ch[0]))).decodeMessage(infl(ch[1]));

const byId = new Map();
for (const n of msg.nodeChanges) byId.set(`${n.sessionID}:${n.localID}`, n);

// 顶层 = 父节点不是 COMPONENT/SYMBOL（即它的父是 FRAME）
const comps = msg.nodeChanges.filter(n => n.type === 'COMPONENT' || n.type === 'SYMBOL');
const isTop = n => {
  const par = n.parentIndex ? (msg.nodeChanges[n.parentIndex.guid ? 0 : 0], null) : null;
  return true;
};
// parentIndex 是 nodeChanges 数组下标
const out = [];
for (let i = 0; i < msg.nodeChanges.length; i++) {
  const n = msg.nodeChanges[i];
  if (n.type !== 'COMPONENT' && n.type !== 'SYMBOL') continue;
  const pi = n.parentIndex ? n.parentIndex.guid : null;
  let par = null;
  if (n.parentIndex && typeof n.parentIndex === 'object') {
    const idx = n.parentIndex.guid;
    par = typeof idx === 'number' ? msg.nodeChanges[idx] : null;
  }
  if (par && (par.type === 'COMPONENT' || par.type === 'SYMBOL')) continue;
  const bb = n.size || {};
  out.push({ name: n.name, w: Math.round(bb.x || 0), h: Math.round(bb.y || 0), type: n.type });
}
const filt = process.argv[2];
const rows = out.filter(c => !filt || c.name.toLowerCase().includes(filt.toLowerCase()))
  .sort((a, b) => a.name.localeCompare(b.name));
for (const r of rows) console.log(`${r.name}  [${r.type}]  ${r.w}x${r.h}`);
console.log(`-- ${rows.length} / ${out.length}`);
