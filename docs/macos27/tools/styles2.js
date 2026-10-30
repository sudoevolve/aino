const fs = require('fs');
const zlib = require('zlib');
const { decodeBinarySchema, compileSchema } = require('kiwi-schema');

const raw = fs.readFileSync('canvas.fig');
let p = 12; const ch = [];
while (p + 4 <= raw.length) { const len = raw.readUInt32LE(p); p += 4; if (!len || p + len > raw.length) break; ch.push(raw.slice(p, p + len)); p += len; }
const infl = b => { for (const f of [zlib.inflateRawSync, zlib.zstdDecompressSync]) { try { return f(b); } catch (e) {} } return b; };
const msg = compileSchema(decodeBinarySchema(infl(ch[0]))).decodeMessage(infl(ch[1]));
const ns = msg.nodeChanges;

const hex = c => {
  const f = v => Math.round(Math.max(0, Math.min(1, v)) * 255).toString(16).padStart(2, '0').toUpperCase();
  return `#${f(c.r)}${f(c.g)}${f(c.b)}` + ((c.a ?? 1) < 0.999 ? f(c.a) : '');
};
const N = n => n && typeof n === 'object' ? (n.units === 'PIXELS' ? n.value : `${n.value}${n.units === 'PERCENT' ? '%' : ''}`) : n;

const texts = ns.filter(n => n.styleType === 'TEXT').map(n => ({
  name: n.name, font: n.fontName ? `${n.fontName.family} ${n.fontName.style}` : null,
  size: n.fontSize, lineHeight: N(n.lineHeight), letterSpacing: N(n.letterSpacing), tracking: n.textTracking,
  weight: n.fontName && n.fontName.style,
}));
fs.writeFileSync('styles_text.json', JSON.stringify(texts, null, 1));
console.log('TEXT STYLES');
for (const t of texts) console.log(`  ${t.name.padEnd(26)} ${String(t.size).padStart(4)} / lh ${String(t.lineHeight).padStart(5)} / ls ${String(t.letterSpacing)}  ${t.font}`);

const effs = ns.filter(n => n.styleType === 'EFFECT').map(n => ({
  name: n.name,
  effects: (n.effects || []).map(e => ({
    type: e.type, radius: e.radius, spread: e.spread,
    offset: e.offset ? `${e.offset.x},${e.offset.y}` : null,
    color: e.color ? hex(e.color) : null, visible: e.visible,
  })),
}));
fs.writeFileSync('styles_effect.json', JSON.stringify(effs, null, 1));
console.log('\nEFFECT STYLES');
for (const e of effs) console.log('  ' + e.name.padEnd(32), JSON.stringify(e.effects));

const fills = ns.filter(n => n.styleType === 'FILL' || n.styleType === 'STROKE').map(n => ({
  name: n.name, type: n.styleType,
  paints: (n.fillPaints || n.strokePaints || []).map(p => p.color ? hex(p.color) : p.type),
}));
fs.writeFileSync('styles_fill.json', JSON.stringify(fills, null, 1));
console.log('\nFILL STYLES', fills.length);
for (const f of fills.slice(0, 60)) console.log('  ' + f.name.padEnd(40), f.paints.join(' '));
