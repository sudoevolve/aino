const fs = require('fs');
const zlib = require('zlib');
const { decodeBinarySchema, compileSchema } = require('kiwi-schema');

function load() {
  const raw = fs.readFileSync('canvas.fig');
  let p = 12; const ch = [];
  while (p + 4 <= raw.length) {
    const len = raw.readUInt32LE(p); p += 4;
    if (!len || p + len > raw.length) break;
    ch.push(raw.slice(p, p + len)); p += len;
  }
  const infl = b => { for (const f of [zlib.inflateRawSync, zlib.zstdDecompressSync]) { try { return f(b); } catch (e) {} } return b; };
  const schema = decodeBinarySchema(infl(ch[0]));
  return { schema, nodes: compileSchema(schema).decodeMessage(infl(ch[1])).nodeChanges };
}
const g = x => x ? `${x.sessionID}:${x.localID}` : null;
const hex = c => {
  const f = v => Math.round(Math.max(0, Math.min(1, v)) * 255).toString(16).padStart(2, '0').toUpperCase();
  return `#${f(c.r)}${f(c.g)}${f(c.b)}` + ((c.a ?? 1) < 0.999 ? f(c.a) : '');
};
const { nodes } = load();
const byId = new Map(nodes.map(n => [g(n.guid), n]));

// ---- 变量集与模式名 ----
const sets = nodes.filter(n => n.type === 'VARIABLE_SET');
const setInfo = new Map();
for (const s of sets) {
  const modes = (s.variableSetModes || []).map(m => ({ id: g(m.id || m.modeID || m.guid), name: m.name }));
  setInfo.set(g(s.guid), { name: s.name, modes });
}

const out = [];
for (const v of nodes.filter(n => n.type === 'VARIABLE')) {
  const setId = g(v.variableSetID && v.variableSetID.guid);
  const si = setInfo.get(setId) || { name: null, modes: [] };
  const modeName = id => (si.modes.find(m => m.id === id) || {}).name || id;
  const values = (v.variableDataValues?.entries || []).map(e => {
    const vd = e.variableData || {};
    const val = vd.value || {};
    let out2;
    if (val.colorValue) out2 = hex(val.colorValue);
    else if (val.floatValue != null) out2 = val.floatValue;
    else if (val.stringValue != null) out2 = val.stringValue;
    else if (val.aliasValue) out2 = { alias: g(val.aliasValue.guid || val.aliasValue.id || val.aliasValue) };
    else out2 = JSON.stringify(val).slice(0, 120);
    return { mode: modeName(g(e.modeID)), value: out2 };
  });
  out.push({ set: si.name, name: v.name, type: v.variableResolvedType, values });
}
fs.writeFileSync('variables.json', JSON.stringify(out, null, 1));
console.log('variables', out.length);
const bySet = {};
for (const v of out) (bySet[v.set || '(none)'] = bySet[v.set || '(none)'] || []).push(v);
console.log('sets:', Object.keys(bySet).map(k => `${k}(${(bySet[k] || []).length})`).join(', '));

// ---- 样式 ----
const styles = nodes.filter(n => n.styleType === 'TEXT' || n.styleType === 'FILL' || n.styleType === 'EFFECT' || n.styleType === 'STROKE');
const styleOut = styles.map(s => {
  const m = s.sharedStyleMasterData || {};
  const o = { name: s.name, type: s.styleType, desc: s.description };
  if (s.styleType === 'TEXT') {
    o.fontSize = m.fontSize; o.font = m.fontName ? `${m.fontName.family}/${m.fontName.style}` : undefined;
    o.lineHeight = m.lineHeight ? (m.lineHeight.units === 'PIXELS' ? m.lineHeight.value : `${m.lineHeight.value}%`) : undefined;
    o.letterSpacing = m.letterSpacing ? (m.letterSpacing.units === 'PERCENT' ? `${(m.letterSpacing.value * 100).toFixed(1)}%` : m.letterSpacing.value) : undefined;
  }
  if (m.fillPaints) o.fills = m.fillPaints.map(p => p.color ? { t: p.type, c: hex(p.color), a: p.opacity } : { t: p.type });
  if (m.effects) o.effects = m.effects.map(e => ({ type: e.type, r: e.radius, spread: e.spread, off: e.offset && { x: e.offset.x, y: e.offset.y }, c: e.color ? hex(e.color) : null }));
  return o;
});
fs.writeFileSync('styles.json', JSON.stringify(styleOut, null, 1));
const sc = {};
for (const s of styleOut) sc[s.type] = (sc[s.type] || 0) + 1;
console.log('styles', styleOut.length, JSON.stringify(sc));

// ---- 组件清单（SYMBOL） ----
const comps = nodes.filter(n => n.type === 'SYMBOL').map(n => ({
  id: g(n.guid), name: n.name,
  w: n.size ? +n.size.x.toFixed(1) : null, h: n.size ? +n.size.y.toFixed(1) : null,
  radius: n.cornerRadius, radii: n.rectangleCornerRadiiIndependent ? [n.rectangleTopLeftCornerRadius, n.rectangleTopRightCornerRadius, n.rectangleBottomRightCornerRadius, n.rectangleBottomLeftCornerRadius] : undefined,
  smoothing: n.cornerSmoothing,
  fills: n.fillPaints?.map(p => p.color ? hex(p.color) : p.type),
  strokes: n.strokePaints?.map(p => p.color ? hex(p.color) : p.type),
  strokeW: n.strokeWeight,
  pad: n.stackHorizontalPadding, padV: n.stackVerticalPadding, gap: n.stackSpacing,
  stackMode: n.stackMode, rx: n.stackPaddingRight, by: n.stackPaddingBottom,
  sw: n.stackWidth, sh: n.stackHeight,
}));
fs.writeFileSync('components.json', JSON.stringify(comps, null, 1));
console.log('components', comps.length);
