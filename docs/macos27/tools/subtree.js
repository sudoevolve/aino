// 打印某个组件的子树（含解析后的真实颜色）
const fs = require('fs');
const zlib = require('zlib');
const { decodeBinarySchema, compileSchema } = require('kiwi-schema');

const raw = fs.readFileSync('canvas.fig');
let p = 12; const ch = [];
while (p + 4 <= raw.length) { const len = raw.readUInt32LE(p); p += 4; if (!len || p + len > raw.length) break; ch.push(raw.slice(p, p + len)); p += len; }
const infl = b => { for (const f of [zlib.inflateRawSync, zlib.zstdDecompressSync]) { try { return f(b); } catch (e) {} } return b; };
const msg = compileSchema(decodeBinarySchema(infl(ch[0]))).decodeMessage(infl(ch[1]));
const g = x => x ? `${x.sessionID}:${x.localID}` : null;
const byId = new Map(msg.nodeChanges.map(n => [g(n.guid), n]));
const kids = new Map();
for (const n of msg.nodeChanges) {
  const pid = n.parentIndex ? g(n.parentIndex.guid) : null;
  if (pid) { if (!kids.has(pid)) kids.set(pid, []); kids.get(pid).push(n); }
}
const modeName = new Map();
for (const s of msg.nodeChanges.filter(n => n.type === 'VARIABLE_SET'))
  for (const m of (s.variableSetModes || [])) modeName.set(g(m.id || m.modeID || m.guid), m.name);
const hex = c => {
  const f = v => Math.round(Math.max(0, Math.min(1, v)) * 255).toString(16).padStart(2, '0').toUpperCase();
  return `#${f(c.r)}${f(c.g)}${f(c.b)}` + ((c.a ?? 1) < 0.999 ? Math.round(c.a * 255).toString(16).padStart(2, '0').toUpperCase() : '');
};
function varValue(vid, wantMode, depth = 0) {
  if (depth > 6) return 'deep';
  const n = byId.get(vid); if (!n) return `?${vid}`;
  const entries = n.variableDataValues?.entries || [];
  const e = entries.find(en => modeName.get(g(en.modeID)) === wantMode) || entries[0];
  if (!e) return '∅';
  const val = e.variableData?.value || {};
  if (val.colorValue) return hex(val.colorValue);
  if (val.floatValue != null) return val.floatValue;
  if (val.aliasValue) return varValue(g(val.aliasValue.guid || val.aliasValue.id), wantMode, depth + 1);
  return JSON.stringify(val).slice(0, 30);
}
const paintDesc = pa => {
  if (pa.colorVar) return `V[L:${varValue(g(pa.colorVar.value?.aliasValue?.guid || pa.colorVar.value?.guid || pa.colorVar), 'Light')} D:${varValue(g(pa.colorVar.value?.aliasValue?.guid || pa.colorVar.value?.guid || pa.colorVar), 'Dark')}]`;
  if (pa.color) return hex(pa.color);
  return pa.type;
};
const names = process.argv.slice(2);
function walk(n, d) {
  const ind = '  '.repeat(d);
  const fills = (n.fillPaints || []).map(paintDesc).join(',');
  const strs = (n.strokePaints || []).map(paintDesc).join(',');
  const sz = n.size ? `${n.size.x.toFixed(0)}x${n.size.y.toFixed(0)}` : '';
  console.log(`${ind}${n.type} "${n.name}" ${sz} r=${n.cornerRadius ?? ''} ${fills ? 'F=' + fills : ''} ${strs ? 'S=' + strs + '/w' + n.strokeWeight : ''} ${n.opacity != null && n.opacity !== 1 ? 'op=' + n.opacity.toFixed(2) : ''}`);
  for (const k of (kids.get(g(n.guid)) || [])) if (d < 4) walk(k, d + 1);
}
const MAX = Number(process.env.MAX || 3);
const per = {};
for (const n of msg.nodeChanges) {
  if (n.type !== 'SYMBOL') continue;
  const hit = names.find(w => (n.name || '').startsWith(w));
  if (!hit) continue;
  per[hit] = (per[hit] || 0) + 1;
  if (per[hit] > MAX) continue;
  walk(n, 0);
  console.log('');
}
