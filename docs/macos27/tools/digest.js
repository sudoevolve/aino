// 紧凑规格摘要：给定符号名，打印 尺寸/圆角/平滑/填充/描边/阴影 + 前两层子图层的相对几何与文字。
// 专供"逐组件对齐"时读参数用，输出控制在一屏内。
// usage: node digest.js "<symbol name>" [more...]      （名字用 includes 匹配，只取第一个命中）
const fs = require('fs');
const zlib = require('zlib');
const { decodeBinarySchema, compileSchema } = require('kiwi-schema');

const figPath = ['./canvas.fig', __dirname + '/canvas.fig']
  .find(f => fs.existsSync(f));
if (!figPath) { console.error('找不到 canvas.fig'); process.exit(1); }
const raw = fs.readFileSync(figPath);
let p = 12; const ch = [];
while (p + 4 <= raw.length) { const len = raw.readUInt32LE(p); p += 4; if (!len || p + len > raw.length) break; ch.push(raw.slice(p, p + len)); p += len; }
const infl = b => { for (const f of [zlib.inflateRawSync, zlib.zstdDecompressSync]) { try { return f(b); } catch (e) { } } return b; };
const msg = compileSchema(decodeBinarySchema(infl(ch[0]))).decodeMessage(infl(ch[1]));

const g = x => x ? `${x.sessionID}:${x.localID}` : null;
const byId = new Map(msg.nodeChanges.map(n => [g(n.guid), n]));
const modeName = new Map();
for (const s of msg.nodeChanges.filter(n => n.type === 'VARIABLE_SET'))
  for (const m of (s.variableSetModes || [])) modeName.set(g(m.id || m.modeID || m.guid), m.name);

const hex = c => {
  if (!c) return '?';
  const f = v => Math.round(Math.max(0, Math.min(1, v)) * 255).toString(16).padStart(2, '0').toUpperCase();
  const a = c.a == null ? 1 : c.a;
  return `#${f(c.r)}${f(c.g)}${f(c.b)}` + (a < 0.999 ? f(a) : '');
};
const round = v => Math.round(v * 100) / 100;
function varValue(vid, mode, depth = 0) {
  if (!vid || depth > 8) return '?';
  const n = byId.get(vid); if (!n) return '?';
  const entries = n.variableDataValues?.entries || [];
  const e = entries.find(en => modeName.get(g(en.modeID)) === mode) || entries[0];
  if (!e) return '∅';
  const val = e.variableData?.value || {};
  if (val.colorValue) return hex(val.colorValue);
  if (val.floatValue != null) return String(round(val.floatValue));
  const al = val.alias || val.aliasValue;
  if (al) return varValue(g(al.guid || al.id || al), mode, depth + 1);
  return '?';
}
const resolveColor = (pa, mode) => {
  if (pa.colorVar) {
    const a = pa.colorVar.value || {};
    const al = a.alias || a.aliasValue;
    if (al) return varValue(g(al.guid || al.id || al), mode);
    if (a.colorValue) return hex(a.colorValue);
  }
  return hex(pa.color);
};
const paintDesc = (pa, mode) => {
  if (pa.type === 'SOLID') {
    let s = resolveColor(pa, mode);
    if (pa.opacity != null && pa.opacity < 0.999) s += ' @' + round(pa.opacity);
    return s;
  }
  if (pa.type && pa.type.startsWith('GRADIENT')) {
    const stops = (pa.stopsVar && pa.stopsVar.length ? pa.stopsVar.map(st => {
      const al = st.colorVar?.value?.alias || st.colorVar?.value?.aliasValue;
      return `${al ? varValue(g(al.guid || al.id || al), mode) : hex(st.color)}@${round(st.position)}`;
    }) : (pa.stops || []).map(st => `${hex(st.color)}@${round(st.position)}`)).join('→');
    return 'grad[' + stops + ']';
  }
  return pa.type;
};
const effectDesc = (e, mode) => {
  const a = e.colorVar?.value || {};
  const al = a.alias || a.aliasValue;
  const col = al ? varValue(g(al.guid || al.id || al), mode) : hex(e.color || { r: 0, g: 0, b: 0, a: 1 });
  const rad = e.radiusVar ? varValue(g(e.radiusVar.value?.alias?.guid || e.radiusVar.value?.alias), mode) : round(e.radius || 0);
  const ox = e.offset ? round(e.offset.x) : 0, oy = e.offset ? round(e.offset.y) : 0;
  const nm = e.type === 'INNER_SHADOW' ? 'IN' : e.type === 'DROP_SHADOW' ? 'DROP' : e.type;
  return `${nm}(x${ox} y${oy} b${rad} ${col})`;
};

const children = new Map();
for (const n of msg.nodeChanges) {
  const pid = n.parentIndex ? g(n.parentIndex.guid) : null;
  if (!pid) continue;
  if (!children.has(pid)) children.set(pid, []);
  children.get(pid).push({ n, pos: n.parentIndex.position ?? 0 });
}
for (const arr of children.values()) arr.sort((a, b) => a.pos - b.pos);

const out = [];
function dump(node, depth, mode, base) {
  const pad = '  '.repeat(depth);
  const sz = node.size || { x: 0, y: 0 };
  const x = (node.transform?.m02 || 0) - base.x, y = (node.transform?.m12 || 0) - base.y;
  const ex = [];
  if (node.cornerRadius) ex.push('r=' + round(node.cornerRadius));
  if (node.rectangleCornerRadiiIndependent) {
    const rr = [node.rectangleTopLeftCornerRadius, node.rectangleTopRightCornerRadius, node.rectangleBottomRightCornerRadius, node.rectangleBottomLeftCornerRadius].map(v => round(v || 0));
    if (rr.some(v => v !== rr[0])) ex.push('r4=[' + rr.join(',') + ']');
  }
  if (node.cornerSmoothing) ex.push('sm=' + round(node.cornerSmoothing));
  if (node.opacity != null && node.opacity < 0.999) ex.push('op=' + round(node.opacity));
  if (node.visible === false) ex.push('HIDDEN');
  if (node.stackMode && node.stackMode !== 'NONE') {
    ex.push(`stack pad=${round(node.stackPadding || 0)}(${round(node.stackHorizontalPadding || 0)}/${round(node.stackVerticalPadding || 0)}) gap=${round(node.stackSpacing || 0)}`);
  }
  out.push(`${pad}${node.name} [${node.type}] ${round(sz.x)}×${round(sz.y)} @${round(x)},${round(y)} ${ex.join(' ')}`);
  for (const pa of (node.fillPaints || []).filter(a => a.visible !== false)) out.push(`${pad}   fill ${paintDesc(pa, mode)}`);
  for (const pa of (node.strokePaints || []).filter(a => a.visible !== false)) out.push(`${pad}   stroke ${paintDesc(pa, mode)} w=${round(node.strokeWeight || 0)} ${node.strokeAlign || ''}`);
  for (const e of (node.effects || [])) if (e.visible !== false) out.push(`${pad}   fx ${effectDesc(e, mode)}`);
  if (node.type === 'TEXT') {
    const fn = node.fontName || {};
    out.push(`${pad}   text ${fn.style} ${round(node.fontSize || 0)}px lh=${node.lineHeight ? round(node.lineHeight.value) + (node.lineHeight.units === 'PERCENT' ? '%' : '') : '-'} "${(node.textData?.characters || '').replace(/\n/g, '\\n').slice(0, 24)}"`);
  }
  if (depth >= 2) return;
  let kids = (children.get(g(node.guid)) || []).map(o => o.n);
  if (node.type === 'INSTANCE' && node.symbolData) {
    const mid = g(node.symbolData.symbolID);
    const master = byId.get(mid);
    if (master && master.type === 'SYMBOL') kids = (children.get(mid) || []).map(o => o.n);
  }
  for (const k of kids) dump(k, depth + 1, mode, base);
}

const want = process.argv.slice(2);
for (const w of want) {
  const sym = msg.nodeChanges.find(n => n.type === 'SYMBOL' && (n.name || '').includes(w));
  if (!sym) { console.log('!! 未找到 ' + w); continue; }
  const sz = sym.size || { x: 0, y: 0 };
  console.log(`\n### ${sym.name}  ${round(sz.x)}×${round(sz.y)}`);
  for (const mode of ['Light', 'Dark']) {
    console.log(`--${mode}--`);
    dump(sym, 0, mode, { x: sym.transform?.m02 || 0, y: sym.transform?.m12 || 0 });
    console.log(out.join('\n'));
    out.length = 0;
  }
}
