const fs = require('fs');
const zlib = require('zlib');
const { decodeBinarySchema, compileSchema } = require('kiwi-schema');

const raw = fs.readFileSync('canvas.fig');
let p = 12; const ch = [];
while (p + 4 <= raw.length) {
  const len = raw.readUInt32LE(p); p += 4;
  if (!len || p + len > raw.length) break;
  ch.push(raw.slice(p, p + len)); p += len;
}
const infl = b => { for (const f of [zlib.inflateRawSync, zlib.zstdDecompressSync]) { try { return f(b); } catch (e) {} } return b; };
const schema = decodeBinarySchema(infl(ch[0]));
const c = compileSchema(schema);
const data = infl(ch[1]);
const msg = c.decodeMessage(data);

const TYPE = schema.definitions.find(x => x.name === 'NodeType').fields.reduce((a, f) => (a[f.value] = f.name, a), {});
const PAINT = schema.definitions.find(x => x.name === 'PaintType').fields.reduce((a, f) => (a[f.value] = f.name, a), {});
const STYLE = schema.definitions.find(x => x.name === 'StyleType').fields.reduce((a, f) => (a[f.value] = f.name, a), {});

const guidStr = g => g ? `${g.sessionID}:${g.localID}` : null;
const num = n => (n && typeof n === 'object' && 'value' in n) ? { v: Number(n.value), u: n.units } : n;

function paint(pn) {
  if (!pn) return null;
  const o = { type: PAINT[pn.type] };
  if (pn.color) o.color = { r: +pn.color.r.toFixed(4), g: +pn.color.g.toFixed(4), b: +pn.color.b.toFixed(4), a: +(pn.color.a ?? 1).toFixed(4) };
  if (pn.opacity != null) o.opacity = pn.opacity;
  if (pn.visible === false) o.hidden = true;
  if (pn.stops) o.stops = pn.stops.map(s => ({ pos: s.position, color: s.color ? { r: +s.color.r.toFixed(3), g: +s.color.g.toFixed(3), b: +s.color.b.toFixed(3), a: +(s.color.a ?? 1).toFixed(3) } : null }));
  return o;
}

const nodes = msg.nodeChanges.map(n => {
  const o = {
    id: guidStr(n.guid),
    type: TYPE[n.type] || n.type,
    name: n.name,
  };
  if (n.parentIndex) o.parent = guidStr(n.parentIndex.guid);
  if (n.size) o.size = { w: +n.size.x.toFixed(2), h: +n.size.y.toFixed(2) };
  if (n.transform) o.pos = { x: +n.transform.m02.toFixed(2), y: +n.transform.m12.toFixed(2) };
  if (n.cornerRadius != null) o.radius = n.cornerRadius;
  if (n.rectangleCornerRadiiIndependent) {
    o.radii = [n.rectangleTopLeftCornerRadius, n.rectangleTopRightCornerRadius, n.rectangleBottomRightCornerRadius, n.rectangleBottomLeftCornerRadius];
  }
  if (n.cornerSmoothing != null) o.smoothing = n.cornerSmoothing;
  if (n.fillPaints && n.fillPaints.length) o.fills = n.fillPaints.map(paint);
  if (n.strokePaints && n.strokePaints.length) o.strokes = n.strokePaints.map(paint);
  if (n.strokeWeight) o.strokeW = n.strokeWeight;
  if (n.strokeAlign != null) o.strokeAlign = n.strokeAlign;
  if (n.effects && n.effects.length) o.effects = n.effects.map(e => ({ type: e.type, radius: e.radius, spread: e.spread, offset: e.offset ? { x: e.offset.x, y: e.offset.y } : null, color: e.color ? { r: +e.color.r.toFixed(3), g: +e.color.g.toFixed(3), b: +e.color.b.toFixed(3), a: +(e.color.a ?? 1).toFixed(3) } : null, visible: e.visible }));
  if (n.fontSize != null) o.fontSize = n.fontSize;
  if (n.fontName) o.font = `${n.fontName.family}/${n.fontName.style}`;
  if (n.lineHeight) o.lineHeight = num(n.lineHeight);
  if (n.letterSpacing) o.letterSpacing = num(n.letterSpacing);
  if (n.textAlignHorizontal != null) o.textAlign = n.textAlignHorizontal;
  if (n.textAlignVertical != null) o.textAlignV = n.textAlignVertical;
  if (n.textCase) o.textCase = n.textCase;
  if (n.opacity != null && n.opacity !== 1) o.opacity = +n.opacity.toFixed(3);
  if (n.stackMode) {
    o.stack = {
      mode: n.stackMode, gap: n.stackSpacing,
      pad: n.stackPadding,
      padL: n.stackHorizontalPadding, padV: n.stackVerticalPadding,
      padR: n.stackPaddingRight, padB: n.stackPaddingBottom,
      align: n.stackAlign, justify: n.stackJustify,
      w: n.stackWidth, h: n.stackHeight,
      primarySizing: n.stackPrimarySizing, counterSizing: n.stackCounterSizing,
      primaryAlign: n.stackPrimaryAlignItems, counterAlign: n.stackCounterAlignItems,
    };
  }
  if (n.styleType) o.styleType = STYLE[n.styleType];
  if (n.variableResolvedType != null) o.varType = n.variableResolvedType;
  if (n.variableTokenName) o.varToken = n.variableTokenName;
  if (n.variableDataValues) o.varValues = n.variableDataValues;
  if (n.description) o.desc = n.description;
  if (n.visible === false) o.hidden = true;
  return o;
});

fs.writeFileSync('nodes.json', JSON.stringify(nodes));
console.log('nodes', nodes.length, 'bytes', fs.statSync('nodes.json').size);

// 变量
const vars = msg.nodeChanges.filter(n => n.type === 28).map(n => {
  const vals = {};
  const vdv = n.variableDataValues;
  if (vdv && vdv.values) {
    for (const e of vdv.values) {
      const k = e.modeID ? `${e.modeID.sessionID}:${e.modeID.localID}` : 'default';
      vals[k] = e.value;
    }
  }
  return { id: guidStr(n.guid), name: n.name, type: n.variableResolvedType, token: n.variableTokenName, values: vals, desc: n.description };
});
fs.writeFileSync('variables_raw.json', JSON.stringify(vars, null, 1));
console.log('vars', vars.length);

// 变量集
const varSets = msg.nodeChanges.filter(n => n.type === 31).map(n => ({ id: guidStr(n.guid), name: n.name, modes: n.variableSetModes, id2: n.variableSetID }));
fs.writeFileSync('varsets.json', JSON.stringify(varSets, null, 1));
console.log('varsets', varSets.length, JSON.stringify(varSets).slice(0, 500));

// 样式
const styles = msg.nodeChanges.filter(n => n.styleType || n.sharedStyleMasterData).map(n => ({
  id: guidStr(n.guid), name: n.name, styleType: STYLE[n.styleType], type: TYPE[n.type],
  master: n.sharedStyleMasterData, desc: n.description, isSoftDeleted: n.isSoftDeletedStyle,
}));
fs.writeFileSync('styles_raw.json', JSON.stringify(styles, null, 1));
console.log('styles', styles.length);

// 页面
const pages = msg.nodeChanges.filter(n => n.type === 2).map(n => ({ id: guidStr(n.guid), name: n.name }));
console.log('pages', JSON.stringify(pages));

// 统计各类型
const cnt = {};
for (const n of msg.nodeChanges) { const t = TYPE[n.type] || n.type; cnt[t] = (cnt[t] || 0) + 1; }
console.log('typeCounts', JSON.stringify(cnt));
