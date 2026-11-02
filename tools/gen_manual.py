#!/usr/bin/env python3
"""Build docs/manual.html and docs/manual/*.png from the REAL window screenshot (docs/screenshot.png, rendered by the
compiled editor through tools/snapshot.cpp), the mockup layout data (docs/mockup/layout.json, the same file the editor
layout is generated from), the factory presets in src/editor/Presets.cpp and the measured numbers in docs/measured.txt.
Run from the repo root. CI turns manual.html into the PDF with headless Chromium. Needs Pillow."""
import json, html, re, os, sys
from PIL import Image, ImageDraw, ImageFont

VER = sys.argv[1] if len(sys.argv) > 1 else '0.1.0'
L = json.load(open('docs/mockup/layout.json'))
shot = Image.open('docs/screenshot.png').convert('RGB')
S = shot.width / 1200.0
os.makedirs('docs/manual', exist_ok=True)
T = L['tips']

def R(k): return tuple(L[k])

# the 10 knob cells in knob order, with their tips (same text as the info bubbles)
knob_names = ['Low frequency', 'Low boost', 'Low atten', 'High boost frequency', 'High boost', 'High boost width', 'High atten frequency', 'High atten', 'Tube', 'Output']
knob_keys = ['lowF', 'lowB', 'lowA', 'hiF', 'hiB', 'bw', 'attF', 'hiA', 'tube', 'out']
knob_range = ['20, 30, 60 or 100 Hz', '0 to 10 (0 to +13.5 dB)', '0 to 10 (0 to -17.5 dB)', '3, 4, 5, 8, 10, 12 or 16 kHz', '0 to 10 (0 to +18 dB)', '0 to 10 (Q 2.2 down to 0.3)',
              '5, 10 or 20 kHz', '0 to 10 (0 to -16 dB)', '0 to 10', '0 to 10 (-12 to +12 dB)']
knob_def = ['60 Hz', '0', '0', '10 kHz', '0', '5', '20 kHz', '0', '0', '5 (0 dB)']
# the knob tips and rects come from the generated C++ layout (the same data the editor uses)
lay = open('src/editor/Layout.h').read()
cells_block = lay[lay.index('constexpr Cell cells[10]'):]
cell_tips = re.findall(r'\{ "(\w+)", \d, "[^"]*", \{[^}]*\}, \{ ([\d.f, ]+) \}, \{[^}]*\}, "((?:[^"\\]|\\.)*)" \}', cells_block)
assert len(cell_tips) == 10, len(cell_tips)
knob_rect = {}
tip_of = {}
for cid, rect, tip in cell_tips:
    v = [float(x.replace('f', '')) for x in rect.split(',')]
    knob_rect[cid] = tuple(v)
    tip_of[cid] = tip.replace('\\"', '"')

rows_header = [
    ('Preset bar', R('pName'), T['pName'], '', 'Start Here'),
    ('Bypass', R('bBypass'), T['bBypass'], 'on / off', 'off'),
    ('Guide', R('bGuide'), T['bGuide'], '', ''),
    ('Settings', R('bSettings'), T['bSettings'], '', 'Look C, bubbles on'),
    ('About & Support', R('bAbout'), T['bAbout'], '', ''),
]
rows_disp = [
    ('Response curve', R('cvResp'), T['cvResp'], '20 Hz to 20 kHz, +/-20 dB', ''),
    ('Spectrum', R('cvSpec'), T['cvSpec'], '20 Hz to 20 kHz, 0 to -90 dB', ''),
]
rows_knobs = [(knob_names[i], knob_rect[knob_keys[i]], tip_of[knob_keys[i]], knob_range[i], knob_def[i]) for i in range(10)]
rows_meter = [('In and Out meters', R('cvMet'), T['cvMet'], '-60 to +6 dB', '')]
rows = rows_header + rows_disp + rows_knobs + rows_meter

def anchor(name, r):
    x, y, w, h = r
    if name in ('Preset bar', 'Bypass', 'Guide', 'Settings', 'About & Support'): return (x + w / 2, y - 1)
    if name == 'Response curve': return (x + w - 14, y + 14)
    if name == 'Spectrum': return (x + w - 14, y + 14)
    if name == 'In and Out meters': return (x + w / 2, y + 150)
    return (x + 8, y - 4)   # knobs: top left of the dial, next to its label

img = shot.copy(); d = ImageDraw.Draw(img)
font = ImageFont.truetype('assets/fonts/BarlowCondensed-Bold.ttf', int(13 * S))
for i, (n, r, t, rg, df) in enumerate(rows, 1):
    cx0, cy0 = anchor(n, r)
    cx, cy, rad = cx0 * S, cy0 * S, 8 * S
    d.ellipse((cx - rad, cy - rad, cx + rad, cy + rad), fill=(2, 235, 186), outline=(0, 0, 0), width=max(1, int(S)))
    w = d.textlength(str(i), font=font)
    d.text((cx - w / 2, cy - 8 * S), str(i), fill=(11, 11, 13), font=font)
img.save('docs/manual/tour.png')

def crop(name, box): shot.crop(tuple(int(v * S) for v in box)).save('docs/manual/%s.png' % name)
crop('header', (52, 0, 1148, 46)); crop('displays', (52, 46, 1056, 376)); crop('knobs', (52, 376, 1056, 562)); crop('meters', (1056, 46, 1148, 562))
# the same crops with badges, so a page can show numbers next to the picture
def badge_crop(name, box, idx):
    im = shot.copy(); dd = ImageDraw.Draw(im)
    for i in idx:
        n, r, t, rg, df = rows[i]
        cx0, cy0 = anchor(n, r); cx, cy, rad = cx0 * S, cy0 * S, 8 * S
        dd.ellipse((cx - rad, cy - rad, cx + rad, cy + rad), fill=(2, 235, 186), outline=(0, 0, 0), width=max(1, int(S)))
        w = dd.textlength(str(i + 1), font=font); dd.text((cx - w / 2, cy - 8 * S), str(i + 1), fill=(11, 11, 13), font=font)
    im.crop(tuple(int(v * S) for v in box)).save('docs/manual/%s.png' % name)
badge_crop('header_b', (52, 0, 1148, 46), range(0, 5))
badge_crop('displays_b', (52, 46, 1056, 376), range(5, 7))
badge_crop('knobs_b', (52, 376, 1056, 562), range(7, 17))
badge_crop('meters_b', (1056, 46, 1148, 562), range(17, 18))

# factory presets, read from the source
src = open('src/editor/Presets.cpp').read()
LOW = [20, 30, 60, 100]; HI = [3, 4, 5, 8, 10, 12, 16]; ATT = [5, 10, 20]
def pdesc(m):
    v = dict(lowF=2, lowB=0, lowA=0, hiF=4, hiB=0, bw=5, attF=2, hiA=0, tube=0, out=5)
    for k, x in re.findall(r'\{ "(\w+)", ([\d.]+) \}', m): v[k] = float(x)
    return v
pres_rows = []
for m in re.finditer(r'mk \("([^"]+)", "([^"]+)", \{ ([^}]*) \}, \{ (.*?) \}\)', src):
    name, typ, tags, vals = m.groups()
    v = pdesc(vals)
    desc = 'Low %d Hz, boost %g, atten %g; high %g kHz boost %g, width %g; atten %d kHz %g; tube %g' % (LOW[int(v['lowF'])], v['lowB'], v['lowA'], HI[int(v['hiF'])], v['hiB'], v['bw'], ATT[int(v['attF'])], v['hiA'], v['tube'])
    pres_rows.append((name, typ, desc))

measured = [l.rstrip() for l in open('docs/measured.txt') if l.strip()]
e = html.escape
def table(lo, hi):
    return '\n'.join('<tr><td>%d</td><th>%s</th><td>%s</td><td>%s</td><td>%s</td></tr>' % (i + 1, e(n), e(t), e(rg), e(df)) for i, (n, r, t, rg, df) in list(enumerate(rows))[lo:hi])
ptable = '\n'.join('<tr><th>%s</th><td>%s</td><td>%s</td></tr>' % (e(n), e(t), e(dd)) for n, t, dd in pres_rows)
mtext = e('\n'.join(measured))

page = open('tools/manual_template.html').read()
for k, v in {'@VER@': VER, '@TABLE_HEADER@': table(0, 5), '@TABLE_DISP@': table(5, 7), '@TABLE_KNOBS@': table(7, 17), '@TABLE_METER@': table(17, 18),
             '@PRESETS@': ptable, '@NPRESETS@': str(len(pres_rows)), '@MEASURED@': mtext}.items():
    page = page.replace(k, v)
open('docs/manual.html', 'w').write(page)
print('manual ok', len(rows), 'controls', len(pres_rows), 'presets')
