#!/usr/bin/env python3
"""Build docs/manual.html from tools/manual_template.html, the unit tables (docs/mockup/data.json, the same data the
plugin is generated from), the factory chains and the core test output. docs/screenshot.png is rendered by the real
compiled editor (tools/snapshot.cpp). CI turns manual.html into the PDF with headless Chromium. Run from the repo root."""
import json, html, subprocess, sys

VER = sys.argv[1] if len(sys.argv) > 1 else '0.1.0'
d = json.load(open('docs/mockup/data.json'))
BUILT = 20
e = html.escape
order = d['order']
names = {k: d['units'][k]['name'] for k in order}
built = order[:BUILT]

areas = [
    ('Signal path strip', 'IN and OUT trim knobs and one chip per row, with an arrow between rows that shows the level after each row. A parallel pair shows both units in one box.'),
    ('+ Add unit, Clear', 'Open the unit browser (search and six categories) or remove every unit.'),
    ('Rack rows', 'One card per unit: grip to drag, name, ON, Solo and Mute, a small display of what the unit does and its main controls. Click a card to select it.'),
    ('Unit panel', 'The selected unit in detail: six knobs, the big display, Earlier / Later to move the row, In series / Beside above to make or break a parallel pair, and Remove.'),
    ('Meters', 'Input pair and output pair, left and right channel, in dB from +6 to -48, with peak hold, clip lights and peak readouts. Click to clear.'),
    ('Header', 'Cat logo, preset bar (previous, name, next, Save), Bypass, Guide, Settings and About & Support.'),
    ('Wood cheeks', 'Decoration. Double-click either one to reset the window to 100%.'),
]
rows_a = ''.join('<tr><td>%s</td><td>%s</td></tr>' % (e(a), e(b)) for a, b in areas)

units = []
for k in built:
    u = d['units'][k]
    rows = ''
    for p in u['params']:
        if p.get('names'):
            rng = ', '.join(x for x in p['names'] if x)
        else:
            rng = '%g to %g%s' % (p['min'], p['max'], p['u'])
        dv = '%g%s' % (p['def'], p['u']) if not p.get('names') else ''
        rows += '<tr><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>' % (e(p['l']), e(p['tip']), e(rng), e(dv))
    units.append('<h3>%s <span class="dim">(%s, %s)</span></h3><p>%s</p><table><tr><th>Control</th><th>What it does</th><th>Range</th><th>Default</th></tr>%s</table>' % (
        e(u['name']), e(u['kind']), e(u['cat']), e(u['blurb']), rows))

def flat(chain):
    for c in chain:
        if isinstance(c, dict):
            for x in c['par']: yield x[0]
        else: yield c[0]
def label(chain):
    out = []
    for c in chain:
        if isinstance(c, dict): out.append('[' + ' || '.join(names[x[0]] for x in c['par']) + ']')
        else: out.append(names[c[0]])
    return ' > '.join(out)
def chain_ok(p): return all(k in built for k in flat(p['chain']))
fac = [p for p in d['factory'] if chain_ok(p)]
prow = ''.join('<tr><td>%s</td><td>%s</td><td>%s</td></tr>' % (e(p['name']), e(p['type']), e(label(p['chain']))) for p in fac)

try:
    out = subprocess.run(['sh', 'tests/run.sh'], capture_output=True, text=True, timeout=300).stdout
    measured = e('\n'.join(l for l in out.splitlines() if l.strip()))
except Exception as ex:
    measured = 'core tests could not run here: %s' % e(str(ex))

t = open('tools/manual_template.html').read()
for k, v in {'@VER@': VER, '@TABLE_AREAS@': rows_a, '@UNITS@': ''.join(units), '@PRESETS@': prow, '@NPRESETS@': str(len(fac)),
             '@NBUILT@': str(BUILT), '@NTOTAL@': str(len(order)), '@MEASURED@': measured}.items():
    t = t.replace(k, v)
open('docs/manual.html', 'w').write(t)
print('docs/manual.html written;', len(fac), 'factory chains,', BUILT, 'units')
