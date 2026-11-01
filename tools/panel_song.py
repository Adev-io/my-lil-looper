#!/usr/bin/env python3
"""panel_song.py: writes res/LooperSong.svg and src/layout_song.hpp for the My lil Looper Song expander (10 HP).

Shares the drawing and overlap helpers with panel.py, as panel_cv.py and panel_fx.py do. 10 HP leaves no room for the title
and face between Rack's top screws, so the band is taller, as on Looper CV: title and face sit under the screw row.
Run from anywhere: py -3.11 tools/panel_song.py   (needs fontTools).
Author: Ari Klopfer / A_Development. MIT
"""
import os
import panel as P

W, H = 5.08 * 10, P.H
BAND = 13.0
TITLE, TITLE_X, TITLE_Y, TITLE_SIZE = 'MY LIL LOOPER SONG', 2.5, 10.2, 3.0
FACE_X, FACE_Y, FACE_R = W - 5.68, 8.3, P.BAND * 0.36
ZONE_Y = 92.0
DISPLAY = (40.0, 8.0)                 # the bar readout box, mm
ROW_JACK = 105.0

P.PADS['display'] = DISPLAY           # a box for the overlap check; drawn by svg(), no pad SVG

LAYOUT = [
    ('DISPLAY', 'display', W / 2, 21.5, ''),
    ('REC_PARAM', 'pad', 14.2, 40.0, 'REC'),
    ('PLAY_PARAM', 'pad', 36.6, 40.0, 'PLAY'),
    ('LOOP_PARAM', 'button', W / 2, 60.0, 'LOOP'),
    ('PLAY_INPUT', 'seq', 9.0, ROW_JACK, 'PLAY'),
    ('REC_INPUT', 'seq', 20.0, ROW_JACK, 'REC'),
    ('END_OUTPUT', 'out', 31.0, ROW_JACK, 'END'),
    ('BAR_OUTPUT', 'out', 42.0, ROW_JACK, 'BAR'),
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
    out.append(('footer', P.label_box(W / 2, 126.4, 2.4, 'A_DEVELOPMENT')))
    return out


def problems():
    """Parts touching parts, labels or screws; anything off the panel, in the band, or below the zone's top outside its
    margin; the title touching the face or a screw. Must be empty."""
    bad = []
    parts = [(n, P.shape(k, x, y)) for n, k, x, y, _ in LAYOUT]
    labels = label_boxes()
    for j, (a, sa) in enumerate(parts):
        e = P.extent(sa)
        for b, sb in parts[j + 1:]:
            if P.shapes_hit(sa, sb):
                bad.append('%s touches %s' % (a, b))
        if e[0] < 0 or e[2] > W or e[1] < BAND or e[3] > H:
            bad.append('%s leaves the panel or enters the band' % a)
        if e[3] > ZONE_Y and (e[0] < P.ZONE_X or e[2] > W - P.ZONE_X):
            bad.append('%s leaves the jack zone' % a)
        for name, box in labels:
            if name != a + ' label' and P.shapes_hit(sa, ('box',) + box):
                bad.append('%s covers %s' % (a, name))
        for s in screws():
            if P.shapes_hit(sa, ('box',) + s):
                bad.append('%s under a screw' % a)
    band = [('title', (TITLE_X, TITLE_Y - 0.73 * TITLE_SIZE, TITLE_X + P.width(TITLE, TITLE_SIZE), TITLE_Y)),
            ('face', (FACE_X - FACE_R, FACE_Y - FACE_R, FACE_X + FACE_R, FACE_Y + FACE_R))]
    for name, box in band:
        if box[0] < 0 or box[2] > W or box[3] > BAND:
            bad.append('%s leaves the band' % name)
    boxes = labels + band
    for j, (a, box) in enumerate(boxes):
        if box[0] < 0 or box[2] > W or box[3] > H:
            bad.append('%s leaves the panel' % a)
        for b, other in boxes[j + 1:]:
            if P.boxes_hit(box, other):
                bad.append('%s overlaps %s' % (a, b))
        for s in screws():
            if P.boxes_hit(box, s):
                bad.append('%s under a screw' % a)
    return bad


def svg():
    dx, dy = W / 2 - DISPLAY[0] / 2, 21.5 - DISPLAY[1] / 2
    out = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        '<svg xmlns="http://www.w3.org/2000/svg" width="%gmm" height="%gmm" viewBox="0 0 %g %g">' % (W, H, W, H),
        '<rect width="%g" height="%g" fill="%s"/>' % (W, H, P.INK),
        '<rect width="%g" height="%g" fill="%s"/>' % (W, BAND, P.LIME),
        P.text(TITLE, TITLE_X, TITLE_Y, TITLE_SIZE, P.INK, 'start', TITLE_SIZE * 0.09),
        P.mascot(FACE_X, FACE_Y, FACE_R),
        '<rect x="%g" y="%g" width="%g" height="%g" rx="1.5" fill="#0c0c10" stroke="%s" stroke-width="0.3"/>' % (dx, dy, DISPLAY[0], DISPLAY[1], P.DIM),
        '<rect x="%g" y="%g" width="%g" height="%g" rx="2" fill="%s"/>' % (P.ZONE_X, ZONE_Y, W - 2 * P.ZONE_X, 123.7 - ZONE_Y, P.INK2),
    ]
    rb = P.row_bottoms(LAYOUT)
    for _, kind, x, y, label in LAYOUT:
        if kind in P.RING_COLOR:
            out.append('<circle cx="%g" cy="%g" r="%g" fill="none" stroke="%s" stroke-width="0.5"/>' % (x, y, P.RING_R, P.RING_COLOR[kind]))
        elif kind == 'out':
            out.append('<circle cx="%g" cy="%g" r="%g" fill="%s" stroke="%s" stroke-width="0.5"/>' % (x, y, P.PAD_R, P.INK, P.LIME))
        if label:
            base, size, colour, stroke = P.label_at(kind, y, rb)
            out.append(P.text(label, x, base, size, colour, stroke=stroke))
    out.append(P.text('A_DEVELOPMENT', W / 2, 126.4, 2.4, P.DIM))
    out.append('</svg>')
    return '\n'.join(out) + '\n'


def header():
    lines = ['// layout_song.hpp: generated by tools/panel_song.py, do not edit. Component centres in mm.', '#pragma once',
             'namespace layout {', 'namespace looper_song {']
    lines += ['constexpr float %s[2] = {%.2ff, %.2ff};' % (i, x, y) for i, _, x, y, _ in LAYOUT]
    lines += ['constexpr float DISPLAY_SIZE[2] = {%.2ff, %.2ff};   // the readout box, mm' % DISPLAY]
    lines += ['constexpr float FACE[3] = {%.2ff, %.2ff, %.3ff};   // mascot centre x, y, radius (TalkingFace)' % (FACE_X, FACE_Y, FACE_R)]
    lines += ['constexpr float PAD_RING = %.2ff;   // ring light margin around a pad' % P.PAD_RING]
    lines += ['}', '}', '']
    return '\n'.join(lines)


if __name__ == '__main__':
    bad = problems()
    assert not bad, '; '.join(bad)
    files = {os.path.join('res', 'LooperSong.svg'): svg(), os.path.join('src', 'layout_song.hpp'): header()}
    for rel, body in files.items():
        with open(os.path.join(P.ROOT, rel), 'w', encoding='utf-8') as f:
            f.write(body)
    print('wrote', ', '.join(files), '(%d components)' % len(LAYOUT))
