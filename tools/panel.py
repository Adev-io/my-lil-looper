#!/usr/bin/env python3
"""panel.py: writes res/Looper.svg, the pad SVGs and src/layout.hpp from one layout table.

Labels become paths (DejaVu Sans from the Rack install, or the font named by RACK_FONT), so the SVG has no <text>,
as the VCV panel guide asks. The widgets read the same table through layout.hpp, so art and components cannot drift.
House style as in My lil Acid Bass and My lil Toolbox. Run from anywhere: py -3.11 tools/panel.py   (needs fontTools).
Author: Ari Klopfer / A_Development. MIT
"""
import math
import os
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

H = 128.5
W = 5.08 * 26
BAND = 8.6
INK, INK2 = '#16161a', '#202027'
LIME, MAGENTA, BLUE, WHITE, DIM = '#c8ff3a', '#ff3d9a', '#5ac8e6', '#f2f2f4', '#8d8d96'
PAD_FILL, PAD_DOWN, PAD_EDGE = '#3a3a44', '#4a4a56', '#55555f'
FONT = os.environ.get('RACK_FONT', r'C:\Program Files\VCV\Rack2Free\res\fonts\DejaVuSans.ttf')
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Round parts: radius in mm, from the Rack component SVGs: knob Rogan1PS, button VCVButton / VCVLightLatch (6.1 mm),
# out = output jack. Input jacks by role, ring coloured as in My lil Acid Bass: seq = clock / reset / triggers (blue),
# play = audio in (lime), cv = modulation (magenta).
RADIUS = {'knob': 6.7, 'button': 3.1, 'seq': 4.0, 'play': 4.0, 'cv': 4.0, 'out': 4.0}
LABEL_SIZE = {'knob': 2.6, 'button': 2.0, 'seq': 2.4, 'play': 2.4, 'cv': 2.4, 'out': 2.6, 'pad': 2.4, 'padlow': 2.4}
RING_COLOR = {'seq': BLUE, 'play': LIME, 'cv': MAGENTA}
PAD_R = 4.8                   # dark plate behind an output jack
RING_R = RADIUS['cv'] + 0.7   # ring around an input jack
# Square pads (src/Pad.hpp): width, height in mm. Their ring light sits PAD_RING mm outside the pad.
PADS = {'pad': (17.0, 14.0), 'padlow': (17.0, 7.0)}
PAD_RING = 1.0

font = TTFont(FONT)
glyphs, cmap, upm = font.getGlyphSet(), font.getBestCmap(), font['head'].unitsPerEm


def width(s, size):
    """Advance width of the string in mm at this em size."""
    return sum(glyphs[cmap[ord(c)]].width for c in s) * size / upm


def text(s, x, y, size, fill, anchor='middle', stroke=0.0):
    """One <path> for the string: baseline at y, em size in mm, optional stroke for a bolder face."""
    k = size / upm
    w = width(s, size)
    pen_x = x - w / 2 if anchor == 'middle' else x - w if anchor == 'end' else x
    d = []
    for c in s:
        g = glyphs[cmap[ord(c)]]
        pen = SVGPathPen(glyphs)
        g.draw(TransformPen(pen, (k, 0, 0, -k, pen_x, y)))
        d.append(pen.getCommands())
        pen_x += g.width * k
    bold = ' stroke="%s" stroke-width="%g" stroke-linejoin="round"' % (fill, stroke) if stroke else ''
    return '<path fill="%s"%s d="%s"/>' % (fill, bold, ' '.join(d))


# ---- the title band, as in My lil Toolbox: Rack's screws sit in its corners (5.08 mm squares at x 5.08 and
# W - 10.16), so the title starts right of the left one and the face sits left of the right one
TITLE, TITLE_X, TITLE_Y, TITLE_SIZE = 'LOOPER', 11.2, 6.3, 4.3
MASCOT_X = W - 14.46
MASCOT_R = BAND * 0.36


def mascot(cx, cy, r):
    """The My lil face: magenta disc, dot eyes, a flat mouth (src/TalkingFace.hpp paints the live mouth over it)."""
    e = r * 0.37
    return ''.join([
        '<circle cx="%g" cy="%g" r="%g" fill="%s"/>' % (cx, cy, r, MAGENTA),
        '<circle cx="%g" cy="%g" r="%g" fill="%s"/>' % (cx - e, cy - r * 0.32, r * 0.145, INK),
        '<circle cx="%g" cy="%g" r="%g" fill="%s"/>' % (cx + e, cy - r * 0.32, r * 0.145, INK),
        '<line x1="%g" y1="%g" x2="%g" y2="%g" stroke="%s" stroke-width="%g" stroke-linecap="round"/>'
        % (cx - r * 0.58, cy + r * 0.45, cx + r * 0.58, cy + r * 0.45, INK, r * 0.16),
    ])


def band_boxes():
    return [('title', (TITLE_X, TITLE_Y - 0.73 * TITLE_SIZE, TITLE_X + width(TITLE, TITLE_SIZE), TITLE_Y)),
            ('mascot', (MASCOT_X - MASCOT_R, BAND / 2 - MASCOT_R, MASCOT_X + MASCOT_R, BAND / 2 + MASCOT_R))]


# ---- layout: five track columns 21 mm apart, then the global column pair
TX = (13.0, 34.0, 55.0, 76.0, 97.0)
GX = (113.5, 124.5)
GPAD_X = (GX[0] + GX[1]) / 2
SEP_X = 107.5
ROW_NUM, ROW_REC, ROW_STOP, ROW_SMALL, ROW_LEVEL, ROW_IN, ROW_OUT = 16.4, 25.0, 42.0, 56.0, 72.0, 101.0, 115.3
SMALL_DX = 6.5
ZONE_X, ZONE_Y = 2.5, 92.0

LOOPER = [part for i, x in enumerate(TX) for part in (
    ('REC%d_PARAM' % i, 'pad', x, ROW_REC, 'REC/PLAY'),
    ('STOP%d_PARAM' % i, 'padlow', x, ROW_STOP, 'STOP'),
    ('UNDO%d_PARAM' % i, 'button', x - SMALL_DX, ROW_SMALL, 'UNDO'),
    ('REV%d_PARAM' % i, 'button', x, ROW_SMALL, 'REV'),
    ('ONESHOT%d_PARAM' % i, 'button', x + SMALL_DX, ROW_SMALL, '1SHOT'),
    ('LEVEL%d_PARAM' % i, 'knob', x, ROW_LEVEL, 'LEVEL'),
    ('TRACK%d_INPUT' % i, 'play', x, ROW_IN, 'IN'),
    ('TRACK%d_OUTPUT' % i, 'out', x, ROW_OUT, 'OUT'),
)] + [
    ('ALL_PARAM', 'pad', GPAD_X, ROW_REC, 'ALL'),
    ('CLOCK_INPUT', 'seq', GX[0], 45.0, 'CLOCK'), ('RESET_INPUT', 'seq', GX[1], 45.0, 'RESET'),
    ('REC_INPUT', 'seq', GX[0], 59.0, 'REC'), ('STOP_INPUT', 'seq', GX[1], 59.0, 'STOP'),
    ('CLEAR_INPUT', 'seq', GX[0], 73.0, 'CLEAR'), ('LEVEL_INPUT', 'cv', GX[1], 73.0, 'LEVEL'),
    ('IN_L_INPUT', 'play', GX[0], ROW_IN, 'IN L'), ('IN_R_INPUT', 'play', GX[1], ROW_IN, 'IN R'),
    ('OUT_L_OUTPUT', 'out', GX[0], ROW_OUT, 'OUT L'), ('OUT_R_OUTPUT', 'out', GX[1], ROW_OUT, 'OUT R'),
]


def extras():
    out = ['<rect x="%g" y="%g" width="%g" height="%g" rx="2" fill="%s"/>' % (ZONE_X, ZONE_Y, W - 2 * ZONE_X, 123.7 - ZONE_Y, INK2),
           '<rect x="%g" y="13.5" width="0.4" height="%g" fill="%s"/>' % (SEP_X, 123.7 - 13.5, DIM)]
    out += [text(str(i + 1), x, ROW_NUM, 3.0, LIME, stroke=0.25) for i, x in enumerate(TX)]
    return out


def is_pad(kind):
    return kind in PADS


def pad_box(x, y, kind, grow=0.0):
    w, h = PADS[kind]
    return x - w / 2 - grow, y - h / 2 - grow, x + w / 2 + grow, y + h / 2 + grow


def reach(kind):
    return RING_R if kind in RING_COLOR else PAD_R if kind == 'out' else RADIUS[kind]


def bottom(kind, y):
    """The lowest point of a part, ring light / plate included."""
    return pad_box(0, y, kind, PAD_RING)[3] if is_pad(kind) else y + reach(kind)


def label_at(kind, y, row_bottom):
    """Baseline, size, colour and stroke of the label under a part. Labels in a row share the row's lowest part."""
    colour, stroke = (LIME, 0.25) if kind == 'out' else (WHITE, 0.18)
    low = y + PAD_R if kind == 'out' else row_bottom[y]
    return low + 2.8, LABEL_SIZE[kind], colour, stroke


def row_bottoms(layout):
    rb = {}
    for _, kind, x, y, _ in layout:
        if kind != 'out':
            rb[y] = max(rb.get(y, 0), bottom(kind, y))
    return rb


def label_box(x, base, size, label):
    w = width(label, size)
    return x - w / 2, base - 0.73 * size, x + w / 2, base


SCREW = 5.08


def screws():
    return [(x, y, x + SCREW, y + SCREW) for x in (SCREW, W - 2 * SCREW) for y in (0, H - SCREW)]


def box_hits_circle(b, cx, cy, r):
    nx, ny = min(max(cx, b[0]), b[2]), min(max(cy, b[1]), b[3])
    return math.hypot(cx - nx, cy - ny) < r


def boxes_hit(a, b):
    return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]


def shape(kind, x, y):
    """('box', x0, y0, x1, y1) for a pad with its ring, ('circle', x, y, r) for anything round."""
    return ('box',) + pad_box(x, y, kind, PAD_RING) if is_pad(kind) else ('circle', x, y, reach(kind))


def shapes_hit(a, b):
    if a[0] == 'box' and b[0] == 'box':
        return boxes_hit(a[1:], b[1:])
    if a[0] == 'box':
        return box_hits_circle(a[1:], *b[1:])
    if b[0] == 'box':
        return box_hits_circle(b[1:], *a[1:])
    return math.hypot(a[1] - b[1], a[2] - b[2]) < a[3] + b[3]


def extent(s):
    return s[1:] if s[0] == 'box' else (s[1] - s[3], s[2] - s[3], s[1] + s[3], s[2] + s[3])


def problems(layout):
    """Everything on the panel that touches something else or leaves the panel: parts (rings, plates, pad lights),
    their labels, the band's title and face, Rack's four screws, the separator. Parts below the jack zone's top stay
    inside its side margin. Must be empty."""
    bad = []
    parts = [(n, shape(k, x, y)) for n, k, x, y, _ in layout]
    rb = row_bottoms(layout)
    labels = []
    for n, k, x, y, label in layout:
        if label:
            base, size, _, _ = label_at(k, y, rb)
            labels.append((n + ' label', label_box(x, base, size, label)))
    labels += [('track %d number' % (i + 1), label_box(x, ROW_NUM, 3.0, str(i + 1))) for i, x in enumerate(TX)]
    sep = (SEP_X, 13.5, SEP_X + 0.4, 123.7)
    for j, (a, sa) in enumerate(parts):
        e = extent(sa)
        for b, sb in parts[j + 1:]:
            if shapes_hit(sa, sb):
                bad.append('%s touches %s' % (a, b))
        if e[0] < 0 or e[2] > W or e[1] < BAND or e[3] > H:
            bad.append('%s leaves the panel or enters the band' % a)
        if e[3] > ZONE_Y and (e[0] < ZONE_X or e[2] > W - ZONE_X):
            bad.append('%s leaves the jack zone' % a)
        for name, box in labels:
            if name != a + ' label' and shapes_hit(sa, ('box',) + box):
                bad.append('%s covers %s' % (a, name))
        for s in screws() + [sep]:
            if shapes_hit(sa, ('box',) + s):
                bad.append('%s under a screw or on the separator' % a)
    boxes = labels + band_boxes()
    for j, (a, box) in enumerate(boxes):
        if box[0] < 0 or box[2] > W or box[3] > H:
            bad.append('%s leaves the panel' % a)
        for b, other in boxes[j + 1:]:
            if boxes_hit(box, other):
                bad.append('%s overlaps %s' % (a, b))
        for s in screws() + [sep]:
            if boxes_hit(box, s):
                bad.append('%s under a screw or on the separator' % a)
    return bad


def panel(layout):
    out = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        '<svg xmlns="http://www.w3.org/2000/svg" width="%gmm" height="%gmm" viewBox="0 0 %g %g">' % (W, H, W, H),
        '<rect width="%g" height="%g" fill="%s"/>' % (W, H, INK),
        '<rect width="%g" height="%g" fill="%s"/>' % (W, BAND, LIME),
        text(TITLE, TITLE_X, TITLE_Y, TITLE_SIZE, INK, 'start', 0.39), mascot(MASCOT_X, BAND / 2, MASCOT_R),
        text('my lil looper', W / 2, 12.6, 2.4, WHITE, stroke=0.12),
    ] + extras()
    rb = row_bottoms(layout)
    for _, kind, x, y, label in layout:
        if kind in RING_COLOR:
            out.append('<circle cx="%g" cy="%g" r="%g" fill="none" stroke="%s" stroke-width="0.5"/>' % (x, y, RING_R, RING_COLOR[kind]))
        elif kind == 'out':
            out.append('<circle cx="%g" cy="%g" r="%g" fill="%s" stroke="%s" stroke-width="0.5"/>' % (x, y, PAD_R, INK, LIME))
        if label:
            base, size, colour, stroke = label_at(kind, y, rb)
            out.append(text(label, x, base, size, colour, stroke=stroke))
    out.append(text('A_DEVELOPMENT', W / 2, 126.4, 2.4, DIM))
    out.append('</svg>')
    return '\n'.join(out) + '\n'


def pad_svg(kind, down):
    w, h = PADS[kind]
    return ('<?xml version="1.0" encoding="UTF-8"?>\n'
            '<svg xmlns="http://www.w3.org/2000/svg" width="%gmm" height="%gmm" viewBox="0 0 %g %g">\n'
            '<rect x="0.25" y="0.25" width="%g" height="%g" rx="1.4" fill="%s" stroke="%s" stroke-width="0.5"/>\n'
            '</svg>\n' % (w, h, w, h, w - 0.5, h - 0.5, PAD_DOWN if down else PAD_FILL, PAD_EDGE))


def header(layout):
    lines = ['// layout.hpp: generated by tools/panel.py, do not edit. Component centres in mm.', '#pragma once',
             'namespace layout {', 'namespace looper {']
    lines += ['constexpr float %s[2] = {%.2ff, %.2ff};' % (i, x, y) for i, _, x, y, _ in layout]
    lines += ['constexpr float FACE[3] = {%.2ff, %.2ff, %.3ff};   // mascot centre x, y, radius (TalkingFace)' % (MASCOT_X, BAND / 2, MASCOT_R)]
    lines += ['constexpr float PAD_RING = %.2ff;   // ring light margin around a pad' % PAD_RING]
    lines += ['}', '}', '']
    return '\n'.join(lines)


if __name__ == '__main__':
    bad = problems(LOOPER)
    assert not bad, '; '.join(bad)
    files = {os.path.join('res', 'Looper.svg'): panel(LOOPER), os.path.join('src', 'layout.hpp'): header(LOOPER)}
    for kind, name in (('pad', 'Pad'), ('padlow', 'PadLow')):
        for down in (0, 1):
            files[os.path.join('res', '%s_%d.svg' % (name, down))] = pad_svg(kind, down)
    for rel, body in files.items():
        with open(os.path.join(ROOT, rel), 'w', encoding='utf-8') as f:
            f.write(body)
    print('wrote', ', '.join(files), '(%d components)' % len(LOOPER))
