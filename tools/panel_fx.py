#!/usr/bin/env python3
"""panel_fx.py: writes res/LooperFX.svg, the scene pad SVGs and src/layout_fx.hpp for the My lil Looper FX expander (22 HP).

Shares the drawing and overlap helpers with panel.py (labels as paths, house colours, ring roles), as panel_cv.py does.
Five track columns (FILTER, SPEED, MUTE, STUT, TAPE), the 2 x 4 scene pads with STUT LEN under them, jacks in the zone.
Run from anywhere: py -3.11 tools/panel_fx.py   (needs fontTools).
Author: Ari Klopfer / A_Development. MIT
"""
import os
import panel as P

W, H = 5.08 * 22, P.H
TITLE, TITLE_X, TITLE_Y, TITLE_SIZE = 'MY LIL LOOPER FX', 11.2, 6.3, 4.3
FACE_X, FACE_Y, FACE_R = W - 14.46, P.BAND / 2, P.BAND * 0.36
ZONE_Y = 92.0
TX = (12.0, 26.0, 40.0, 54.0, 68.0)
SEP_X = 76.5
SX = (87.5, 100.5)                    # scene pad columns
SY = (24.0, 37.0, 50.0, 63.0)         # scene pad rows
HEAD = 'SCENES'
ROW_NUM, ROW_FILTER, ROW_SPEED, ROW_MUTE, ROW_STUT, ROW_TAPE, ROW_JACK = 16.4, 28.0, 47.0, 61.0, 72.5, 84.0, 106.0

P.RADIUS['switch'] = 4.0              # CKSSThree, vertical: 7.6 mm tall
P.LABEL_SIZE['switch'] = 2.0
P.PADS['scene'] = (10.0, 8.0)
P.LABEL_SIZE['scene'] = 2.4

LAYOUT = [part for i, x in enumerate(TX) for part in (
    ('FILTER%d_PARAM' % i, 'knob', x, ROW_FILTER, 'FILTER'),
    ('SPEED%d_PARAM' % i, 'switch', x, ROW_SPEED, 'x.5 x1 x2'),
    ('MUTE%d_PARAM' % i, 'button', x, ROW_MUTE, 'MUTE'),
    ('STUT%d_PARAM' % i, 'button', x, ROW_STUT, 'STUT'),
    ('TAPE%d_PARAM' % i, 'button', x, ROW_TAPE, 'TAPE'),
)] + [('SCENE%d_PARAM' % (r * 2 + c), 'scene', SX[c], y, str(r * 2 + c + 1)) for r, y in enumerate(SY) for c in range(2)] + [
    ('STUTLEN_PARAM', 'knob', (SX[0] + SX[1]) / 2, 82.0, 'STUT LEN'),
    ('SCENE_INPUT', 'cv', 12.0, ROW_JACK, 'SCENE'),
    ('STUT_INPUT', 'seq', 30.0, ROW_JACK, 'STUT'),
    ('TAPE_INPUT', 'seq', 48.0, ROW_JACK, 'TAPE'),
    ('FILTER_INPUT', 'cv', 66.0, ROW_JACK, 'FILTER'),
]


def screws():
    return [(x, y, x + P.SCREW, y + P.SCREW) for x in (P.SCREW, W - 2 * P.SCREW) for y in (0, H - P.SCREW)]


def label_boxes():
    rb = P.row_bottoms(LAYOUT)
    out = []
    for n, k, x, y, label in LAYOUT:
        if label:
            base, size, _, _ = P.label_at(k, y, rb)
            out.append((n + ' label', P.label_box(x, base, size, label)))
    out += [('track %d number' % (i + 1), P.label_box(x, ROW_NUM, 3.0, str(i + 1))) for i, x in enumerate(TX)]
    out.append(('scene heading', P.label_box((SX[0] + SX[1]) / 2, ROW_NUM, 3.0, HEAD)))
    out.append(('footer', P.label_box(W / 2, 126.4, 2.4, 'A_DEVELOPMENT')))
    return out


def problems():
    """Parts touching parts, labels, screws or the separator; anything off the panel, in the band, or below the zone's
    top outside its margin; the title touching the face. Must be empty."""
    bad = []
    parts = [(n, P.shape(k, x, y)) for n, k, x, y, _ in LAYOUT]
    labels = label_boxes()
    sep = (SEP_X, 13.5, SEP_X + 0.4, 123.7)
    for j, (a, sa) in enumerate(parts):
        e = P.extent(sa)
        for b, sb in parts[j + 1:]:
            if P.shapes_hit(sa, sb):
                bad.append('%s touches %s' % (a, b))
        if e[0] < 0 or e[2] > W or e[1] < P.BAND or e[3] > H:
            bad.append('%s leaves the panel or enters the band' % a)
        if e[3] > ZONE_Y and (e[0] < P.ZONE_X or e[2] > W - P.ZONE_X):
            bad.append('%s leaves the jack zone' % a)
        for name, box in labels:
            if name != a + ' label' and P.shapes_hit(sa, ('box',) + box):
                bad.append('%s covers %s' % (a, name))
        for s in screws() + [sep]:
            if P.shapes_hit(sa, ('box',) + s):
                bad.append('%s under a screw or on the separator' % a)
    band = [('title', (TITLE_X, TITLE_Y - 0.73 * TITLE_SIZE, TITLE_X + P.width(TITLE, TITLE_SIZE), TITLE_Y)),
            ('face', (FACE_X - FACE_R, FACE_Y - FACE_R, FACE_X + FACE_R, FACE_Y + FACE_R))]
    boxes = labels + band
    for j, (a, box) in enumerate(boxes):
        if box[0] < 0 or box[2] > W or box[3] > H:
            bad.append('%s leaves the panel' % a)
        for b, other in boxes[j + 1:]:
            if P.boxes_hit(box, other):
                bad.append('%s overlaps %s' % (a, b))
        for s in screws() + [sep]:
            if P.boxes_hit(box, s):
                bad.append('%s under a screw or on the separator' % a)
    return bad


def svg():
    out = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        '<svg xmlns="http://www.w3.org/2000/svg" width="%gmm" height="%gmm" viewBox="0 0 %g %g">' % (W, H, W, H),
        '<rect width="%g" height="%g" fill="%s"/>' % (W, H, P.INK),
        '<rect width="%g" height="%g" fill="%s"/>' % (W, P.BAND, P.LIME),
        P.text(TITLE, TITLE_X, TITLE_Y, TITLE_SIZE, P.INK, 'start', TITLE_SIZE * 0.09),
        P.mascot(FACE_X, FACE_Y, FACE_R),
        '<rect x="%g" y="%g" width="%g" height="%g" rx="2" fill="%s"/>' % (P.ZONE_X, ZONE_Y, W - 2 * P.ZONE_X, 123.7 - ZONE_Y, P.INK2),
        '<rect x="%g" y="13.5" width="0.4" height="%g" fill="%s"/>' % (SEP_X, 123.7 - 13.5, P.DIM),
    ]
    out += [P.text(str(i + 1), x, ROW_NUM, 3.0, P.LIME, stroke=0.25) for i, x in enumerate(TX)]
    out.append(P.text(HEAD, (SX[0] + SX[1]) / 2, ROW_NUM, 3.0, P.LIME, stroke=0.25))
    rb = P.row_bottoms(LAYOUT)
    for _, kind, x, y, label in LAYOUT:
        if kind in P.RING_COLOR:
            out.append('<circle cx="%g" cy="%g" r="%g" fill="none" stroke="%s" stroke-width="0.5"/>' % (x, y, P.RING_R, P.RING_COLOR[kind]))
        if label:
            base, size, colour, stroke = P.label_at(kind, y, rb)
            out.append(P.text(label, x, base, size, colour, stroke=stroke))
    out.append(P.text('A_DEVELOPMENT', W / 2, 126.4, 2.4, P.DIM))
    out.append('</svg>')
    return '\n'.join(out) + '\n'


def header():
    lines = ['// layout_fx.hpp: generated by tools/panel_fx.py, do not edit. Component centres in mm.', '#pragma once',
             'namespace layout {', 'namespace looper_fx {']
    lines += ['constexpr float %s[2] = {%.2ff, %.2ff};' % (i, x, y) for i, _, x, y, _ in LAYOUT]
    lines += ['constexpr float FACE[3] = {%.2ff, %.2ff, %.3ff};   // mascot centre x, y, radius (TalkingFace)' % (FACE_X, FACE_Y, FACE_R)]
    lines += ['constexpr float PAD_RING = %.2ff;   // ring light margin around a pad' % P.PAD_RING]
    lines += ['}', '}', '']
    return '\n'.join(lines)


if __name__ == '__main__':
    bad = problems()
    assert not bad, '; '.join(bad)
    files = {os.path.join('res', 'LooperFX.svg'): svg(), os.path.join('src', 'layout_fx.hpp'): header()}
    for down in (0, 1):
        files[os.path.join('res', 'ScenePad_%d.svg' % down)] = P.pad_svg('scene', down)
    for rel, body in files.items():
        with open(os.path.join(P.ROOT, rel), 'w', encoding='utf-8') as f:
            f.write(body)
    print('wrote', ', '.join(files), '(%d components)' % len(LAYOUT))
