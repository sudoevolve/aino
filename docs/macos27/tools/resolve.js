// 解析 paint.colorVar 绑定，输出真实颜色（Light / Dark 双列）
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

// 模式 id -> 名称
const modeName = new Map();
for (const s of msg.nodeChanges.filter(n => n.type === 'VARIABLE_SET'))
  for (const m of (s.variableSetModes || [])) modeName.set(g(m.id || m.modeID || m.guid), m.name);

const hex = c => {
  const f = v => Math.round(Math.max(0, Math.min(1, v)) * 255).toString(16).padStart(2, '0').toUpperCase();
  return `#${f(c.r)}${f(c.g)}${f(c.b)}` + ((c.a ?? 1) < 0.999 ? (Math.round(c.a * 255).toString(16).padStart(2, '0').toUpperCase()) : '');
};

// 取变量在指定模式下的值（dfs 解析 alias）
function varValue(vid, wantMode, depth = 0) {
  if (depth > 6) return 'deep';
  const n = byId.get(vid);
  if (!n) return `?${vid}`;
  const entries = n.variableDataValues?.entries || [];
  let e = entries.find(en => modeName.get(g(en.modeID)) === wantMode) || entries[0];
  if (!e) return '∅';
  const val = e.variableData?.value || {};
  if (val.colorValue) return hex(val.colorValue);
  if (val.floatValue != null) return val.floatValue;
  if (val.aliasValue) return varValue(g(val.aliasValue.guid || val.aliasValue.id), wantMode, depth + 1);
  if (val.textValue) return JSON.stringify(val.textValue);
  return JSON.stringify(val).slice(0, 40);
}

function paintDesc(pa) {
  if (!pa) return null;
  const out = {};
  for (const mode of ['Light', 'Dark']) {
    if (pa.colorVar) out[mode] = varValue(g(pa.colorVar.value?.aliasValue?.guid || pa.colorVar.value?.guid || pa.colorVar), mode);
    else if (pa.color) out[mode] = hex(pa.color);
    else out[mode] = pa.type;
  }
  if (pa.opacity != null && pa.opacity !== 1) out.opacity = pa.opacity;
  if (pa.visible === false) out.hidden = true;
  return out;
}
const fmt = a => a ? (Array.isArray(a) ? a.map(x => JSON.stringify(x)).join(' | ') : JSON.stringify(a)) : '';

const want = process.argv.slice(2);
const names = ['Switch', 'Push Button', 'Text Field', 'Segmented Control', 'Menu Item', 'Checkbox', 'Radio Button', 'Slider', 'Progress', 'Scrollbar', 'Tooltip', 'List Item', 'Pop-Up', 'Combo Box', 'Disclosure Button', 'Stepper', 'Group Box', 'Tab'];
for (const n of msg.nodeChanges) {
  if (n.type !== 'SYMBOL') continue;
  const nm = n.name || '';
  if (want.length && !want.some(w => nm.includes(w))) continue;
  if (!want.length && !names.some(w => nm.startsWith(w))) continue;
  const fills = (n.fillPaints || []).map(paintDesc);
  const strokes = (n.strokePaints || []).map(paintDesc);
  if (!fills.length && !strokes.length) continue;
  console.log(`${nm}  ${n.size ? n.size.x.toFixed(0) + 'x' + n.size.y.toFixed(0) : ''}  r=${n.cornerRadius ?? (n.rectangleCornerRadiiIndependent ? [n.rectangleTopLeftCornerRadius, n.rectangleTopRightCornerRadius] : '')}`);
  if (fills.length) console.log('   fill ', fmt(fills));
  if (strokes.length) console.log('   strk ', fmt(strokes), 'w=', n.strokeWeight);
}
