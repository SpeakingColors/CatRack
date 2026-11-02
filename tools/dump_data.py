#!/usr/bin/env python3
"""Read the unit tables, factory chains and layout facts from the approved mockup (docs/mockup/CatRack.html)
with headless Chromium and write docs/mockup/data.json. tools/gen_data.py turns that into C++ headers."""
import json, re
from playwright.sync_api import sync_playwright
src = open('docs/mockup/CatRack.html').read()
src = re.sub(r'<link[^>]*googleapis[^>]*>', '', src)
open('/tmp/catrack_wrapped.html', 'w').write('<!doctype html><html><head><meta charset="utf-8"></head><body>' + src + '</body></html>')
JS = r"""
() => {
  const c = window.__cr, o = {units: {}, factory: [], order: Object.keys(c.UNITS)};
  for (const [k, u] of Object.entries(c.UNITS)) {
    o.units[k] = {name: u.name, code: u.code, kind: u.kind, color: u.color, cat: u.cat, src: u.src, vz: u.vz, real: !!u.real, ana: !!u.ana, blurb: u.blurb,
      params: u.params.map(p => ({id: p.id, l: p.l, s: p.s, kl: p.kl || '', min: p.min, max: p.max, def: p.def, u: p.u, d: p.d, tip: p.tip, log: !!p.log, names: p.names || null, sn: p.sn || null}))};
  }
  o.factory = c.FACTORY.map(p => ({name: p.name, type: p.type, desc: p.desc, chain: p.chain}));
  return o;
}
"""
with sync_playwright() as p:
    b = p.chromium.launch()
    pg = b.new_page(viewport={'width': 1400, 'height': 900})
    pg.goto('file:///tmp/catrack_wrapped.html')
    pg.wait_for_timeout(1500)
    d = pg.evaluate(JS)
    json.dump(d, open('docs/mockup/data.json', 'w'), indent=1, ensure_ascii=False)
    print(len(d['units']), 'units', len(d['factory']), 'presets')
    b.close()
