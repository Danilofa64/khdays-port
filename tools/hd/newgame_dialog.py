#!/usr/bin/env python3
"""Make the new game's dialogs from Dream Drop Distance HD, over the theater's menu, from your own copies
(DDD, MARE, HAND: docs/HD_SCREENS.md, "Making them"), beside the title's pictures.
  python tools/hd/newgame_dialog.py DDD MARE HAND [--mod NAME] [--lang es|en|fr|de|it ...] [--preview DIR]
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import theater_title as tt  # noqa: E402

# DDD's message folders are the port's language names; its fonts are English's for all five.
LANGUAGES = ('es', 'en', 'fr', 'de', 'it')
# The words, by file and (group, id) in it (read in each language's tables).
WORDS = {
    'title': ('ct00000', 5, 0x300),            # New Game
    'difficulty_prompt': ('ct00000', 5, 0x400),
    'difficulty': [('ct00000', 5, 0x200), ('ct00000', 5, 0x201), ('ct00000', 5, 0x202)],
    'difficulty_help': [('ct00200', 0x6e, 0x8500), ('ct00200', 0x6e, 0x8501), ('ct00200', 0x6e, 0x8502)],
    'subtitles_prompt': ('ct00000', 5, 0xa00),
    'subtitles': [('ct00200', 0x6e, 0x8a01), ('ct00200', 0x6e, 0x8a02)],       # yes, no
    'subtitles_help': [('ct00200', 0x6e, 0x8b00), ('ct00200', 0x6e, 0x8b01)],
    'mode_label': ('ct00000', 5, 0x800),
    'subtitles_label': ('ct00000', 5, 0x803),
    'proceed': ('ct00200', 0x6e, 0x8900),
    'proceed_buttons': [('ct00200', 0x6e, 0x8901), ('ct00200', 0x6e, 0x8902)],
}
LAYOUTS = 'original/menu/en/title/title_menu/bin/title_menu_n.l2d'
TEXTURE = 'US_title_menu.dds'
FONT = 'font/en/bin/helpfont.bcfnt'
SHADE, HEADING, THREE, TWO, SETTINGS = 1, 2, 3, 5, 7
BUTTONS = {THREE: (3, 4, 5), TWO: (3, 4), SETTINGS: (5, 6)}     # their node ids, in order
ROWS = (41, 42, 43)                                            # the settings' rows; the third unused
HELP_NODES = {THREE: 10, TWO: 6, SETTINGS: 10}   # where each window's help goes
STILL, APPEAR, SELECTED = 0, 1, 4
# Measured (see above): the words' scales of the HD sheet, labels and help; their colours; where
# the words' cells begin against their anchors (the picture's texels, 1920 x 1080).
LABEL_SCALE, HELP_SCALE = 1.45, 1.58
WHITE, CYAN, YELLOW = (255, 255, 255), (20, 182, 234), (228, 230, 44)
HEADING_TOP = -33.5      # under the heading node's y
PROMPT_TOP = 2.2         # under the window's band (node 2's y)
BUTTON_TOP = 16.4        # under a button node's y
HELP_TOP = -10.0         # under the help node's y; its lines 57.6 apart, centred as a block
HELP_PITCH = 57.6
ROW_TOP = -4.9           # under a row's label and value anchors
HAND_OFFSET = (2.0, -5.0)  # the glove's middle from a button's hand anchor (its node at 7,23)
HAND_SCALE = 0.71        # the KH2 glove (133 texels wide) as wide as DDD's in the captures
HAND_RECT = (117, 5, 250, 96)  # the glove in KH2's title sheet (its texels)
ATLAS = 'newgame_atlas.png'
# The colours (--colours): DDD's own, or Days's (DDD's
# dialogs in greys, every button ringed in silver, the one selected red). Days's: each of DDD's
# tints as the grey of its lightness ((max + min) / 2); the buttons' rings (DDD's selected one's,
# yellow, pulsing) silver, the selected one's pulse kept and the others' at its middle; the
# selected button's red DDD's, darker; the words grey and white.
SILVER = 1.5             # the unselected buttons' rings (the pulse goes 1.0 to 1.99)
RED_DEPTH = 0.75
GREY_WORDS = (200, 200, 200)


# --- DDD's files ----------------------------------------------------------------------------

def ctd(path: Path):
    """A CTD message table: {(group, id): text} (8-byte entries: id, group, text, style)."""
    b = path.read_bytes()
    if b[:4] != b'@CTD':
        raise ValueError(f'{path}: not a CTD')
    count = struct.unpack_from('<H', b, 0x0e)[0]
    table = struct.unpack_from('<I', b, 0x10)[0]
    out = {}
    for i in range(count):
        mid, group, text, _ = struct.unpack_from('<HHHH', b, table + 8 * i)
        end = text
        while end + 1 < len(b) and b[end:end + 2] != b'\0\0':
            end += 2
        out[(group, mid)] = b[text:end].decode('utf-16-le')
    return out


def plain(text: str) -> str:
    """The words without DDD's control codes (U+E30E and U+E30F around a title, in italics)."""
    return ''.join(c for c in text if not 0xe000 <= ord(c) <= 0xf8ff)


class Font:
    """A 3DS font (BCFNT: FINF, TGLP, CWDH, CMAP) and its HD sheets."""

    def __init__(self, path: Path, sheets):
        b = path.read_bytes()
        if b[:4] != b'CFNT':
            raise ValueError(f'{path}: not a BCFNT')
        hsize = struct.unpack_from('<H', b, 6)[0]
        (_, _, _, _, self.alt, dleft, dglyph, dchar, _, tglp, cwdh, cmap, _, _, _) = struct.unpack_from(
            '<4sIBBHbBBBIIIBBB', b, hsize)
        (_, _, self.cw, self.ch, _, _, _, _, _, self.cols, self.rows, self.sw, _, _) = struct.unpack_from(
            '<4sIBBBBIHHHHHHI', b, tglp - 8)
        self.default = (dleft, dglyph, dchar)
        self.widths, self.map = {}, {}
        at = cwdh - 8
        while at > 0:
            _, _, start, end, nxt = struct.unpack_from('<4sIHHI', b, at)
            for i in range(start, end + 1):
                self.widths[i] = struct.unpack_from('<bBB', b, at + 16 + 3 * (i - start))
            at = nxt - 8 if nxt else 0
        at = cmap - 8
        while at > 0:
            _, _, begin, end, method, _, nxt = struct.unpack_from('<4sIHHHHI', b, at)
            data = at + 20
            if method == 0:
                offset = struct.unpack_from('<H', b, data)[0]
                for c in range(begin, end + 1):
                    self.map[c] = offset + c - begin
            elif method == 1:
                for i, c in enumerate(range(begin, end + 1)):
                    idx = struct.unpack_from('<H', b, data + 2 * i)[0]
                    if idx != 0xffff:
                        self.map[c] = idx
            else:
                for i in range(struct.unpack_from('<H', b, data)[0]):
                    c, idx = struct.unpack_from('<HH', b, data + 2 + 4 * i)
                    self.map[c] = idx
            at = nxt - 8 if nxt else 0
        self.sheets = [np.asarray(Image.open(s).convert('RGBA')) for s in sheets]
        self.k = self.sheets[0].shape[1] / self.sw    # HD texels to the original's

    def line(self, text: str, scale: float, colour) -> np.ndarray:
        """The line as RGBA, its cells' top at row 0, `scale` times the HD sheet's size."""
        k, per = self.k, self.cols * self.rows
        placed, x = [], 0.0
        for ch in text:
            idx = self.map.get(ord(ch), self.alt)
            sheet, rest = divmod(idx, per)
            row, col = divmod(rest, self.cols)
            x0, y0 = int(round(col * (self.cw + 1) * k)), int(round(row * (self.ch + 1) * k))
            cell = self.sheets[sheet][y0:y0 + int(round(self.ch * k)), x0:x0 + int(round(self.cw * k))]
            left, _, char = self.widths.get(idx, self.default)
            placed.append((int(round(x + left * k)), cell))
            x += char * k
        alpha = np.zeros((int(round(self.ch * k)), int(np.ceil(x)) + 2), dtype=np.uint8)
        for gx, cell in placed:
            region = alpha[:cell.shape[0], gx:gx + cell.shape[1]]
            np.maximum(region, cell[:region.shape[0], :region.shape[1], 3], out=region)
        im = Image.fromarray(alpha, 'L')
        im = im.resize((max(1, int(round(im.size[0] * scale))), max(1, int(round(im.size[1] * scale)))), Image.LANCZOS)
        out = np.zeros((im.size[1], im.size[0], 4), dtype=np.uint8)
        out[..., :3] = colour
        out[..., 3] = np.asarray(im)
        return out


def ddd_layouts(root: Path) -> tt.Layouts:
    """DDD's dialogs with their HD texture, the parts' texels scaled from the CTRT's size."""
    path = root / LAYOUTS
    b = path.read_bytes()
    saved = tt.tim2_picture
    tt.tim2_picture = lambda data, at: np.zeros((1, 1, 4), dtype=np.uint8)   # a CTRT, not a TIM2
    try:
        layouts = tt.Layouts(path)
    finally:
        tt.tim2_picture = saved
    n, o = struct.unpack_from('<2i', b, 0x20)
    for i, blk in enumerate(layouts.blocks):
        base = o + struct.unpack_from('<I', b, o + 4 * i)[0]
        ctrt = base + struct.unpack_from('<3I', b, base + 0x10)[2]
        if b[ctrt:ctrt + 4] != b'CTRT':
            raise ValueError(f'{path}: block {i} has no CTRT texture')
        width = struct.unpack_from('<H', b, ctrt + 0x20)[0]
        hd = np.asarray(Image.open(root / LAYOUTS.replace('original', 'remastered') / TEXTURE).convert('RGBA'))
        k = hd.shape[1] / width
        blk.texture = hd
        blk.parts = [(int(u0 * k), int(v0 * k), int(u1 * k), int(v1 * k), *c) for (u0, v0, u1, v1, *c) in blk.parts]
    return layouts


def node_places(layouts: tt.Layouts, layout_id: int):
    """Each node's place in the picture's texels (1920 x 1080, the layout's middle at its middle),
    by node id, as the layouts stand still."""
    mf, nidx, ncount, _, lx, ly = layouts.layouts[layouts.ids.index(layout_id)]
    unit = tt.PICTURE[0] / 960.0
    place, out = {}, {}
    for k in range(ncount):
        node = layouts.nodes[nidx + k]
        parent, x, y, nid = node[9], node[10], node[11], node[12]
        px, py = place.get(parent, (lx, ly))
        place[k] = (px + x, py + y)
        out[nid] = (tt.PICTURE[0] / 2 + place[k][0] * unit, tt.PICTURE[1] / 2 + place[k][1] * unit)
    return out


# --- The states -----------------------------------------------------------------------------

def tagged(pieces, tag, order):
    """Pieces of one source: their regions marked with it (two files' blocks share numbers) and
    their keys put in the drawing order."""
    out = []
    for p in pieces:
        region = p['region']
        out.append(dict(p, key=(order,) + p['key'], region=(tag,) + region[1:]))
    return out


def grey(c):
    """A tint as the grey of its lightness, its alpha kept."""
    v = (max(c[:3]) + min(c[:3])) / 2
    return (v, v, v) + tuple(c[3:])


def ring(p):
    """Whether the piece is a part of a selected button's ring: its key colour DDD's yellow."""
    r, g, b = p['colour'][:3]
    return r > 1.0 - 1e-6 and g == r and b == 0


def days_colours(p):
    """A piece of DDD's in Days's colours (see SILVER)."""
    if ring(p):
        v = SILVER if p.get('steady') else p['colour'][0]
        return dict(p, colour=(v, v, v) + tuple(p['colour'][3:]))
    if any(c[0] > 0 and c[1] == 0 and c[2] == 0 for c in p['corners']):   # a selected button's red
        return dict(p, corners=[(c[0] * RED_DEPTH,) + tuple(c[1:]) for c in p['corners']])
    return dict(p, corners=[grey(c) for c in p['corners']], colour=grey(p['colour']))


def word_piece(key, image, x, y, order):
    return {'key': (order,) + key, 'region': ('word',) + key, 'image': image, 'blend': 0, 'flip': (False, False),
            'rect': (x, y, x + image.shape[1], y + image.shape[0]), 'corners': [(1.0, 1.0, 1.0, 1.0)] * 4,
            'colour': (1.0, 1.0, 1.0, 1.0)}


class Dialogs:
    def __init__(self, ddd: Path, mare: Path, hand: Path, lang: str, colours: str = 'days'):
        self.days = colours == 'days'
        self.title_colour, self.help_colour, self.label_colour = (
            (GREY_WORDS, GREY_WORDS, GREY_WORDS) if self.days else (CYAN, CYAN, YELLOW))
        folder, options = tt.LANGUAGES[lang]
        self.theater = tt.Layouts(mare / folder / 'l2d' / 'days_theatre.l2d')
        self.menu_font = tt.Font(mare / folder / 'font')
        self.story = options['story']   # NEW GAME, CONTINUE: the level the dialogs come from
        self.options = 2
        self.ddd = ddd_layouts(ddd)
        self.font = Font(ddd / 'original' / FONT, sorted((ddd / 'remastered' / FONT).glob('US_*.png')))
        tables = {name: ctd(ddd / 'original' / 'message' / lang / 'system' / 'bin' / f'{name}.ctd')
                  for name in ('ct00000', 'ct00200')}

        def word(spec):
            return plain(tables[spec[0]][(spec[1], spec[2])])
        self.words = {k: [word(s) for s in v] if isinstance(v, list) else word(v) for k, v in WORDS.items()}
        sheet = np.asarray(Image.open(hand).convert('RGBA'))
        x0, y0, x1, y1 = HAND_RECT
        glove = Image.fromarray(sheet[y0:y1, x0:x1])
        self.hand = np.asarray(glove.resize((int(round(glove.size[0] * HAND_SCALE)), int(round(glove.size[1] * HAND_SCALE))),
                                            Image.LANCZOS))
        self.images = {}

    def text(self, key, words, scale, colour):
        image = self.font.line(words, scale, colour)
        self.images[('word',) + key] = image
        return image

    def background(self, frame):
        """The theater's main menu as the story level stands when NEW GAME is chosen: its first
        `options` options (theater_title.py's), NEW GAME selected, still."""
        controls = {nid: None for nid in tt.LEFT_OUT_NODES}
        words = {}
        for slot, nid in enumerate(tt.OPTION_NODES):
            if slot >= self.options:
                controls[nid] = None
                continue
            controls[nid] = tt.SELECTED if slot == 0 else tt.NOT_SELECTED
            for part, grey in zip(tt.OPTION_WORD_PARTS[slot], ('black', 'white', 'black')):
                key = ('menu', self.story[slot], grey)
                if ('word', key) not in self.images:
                    self.images[('word', key)] = self.menu_font.word(self.story[slot], tt.WORD_GREYS[grey])
                words[part] = (key, self.images[('word', key)])
        out = []
        for p in self.theater.pieces(tt.MENU_LAYOUT, 0.0, controls, words):
            out += [dict(p, key=(0,) + p['key'])] if p['region'][0] == 'word' else tagged([p], 'mare', 0)
        return out

    def window(self, layout, selected, frame, appearing, hidden=(), shade_appearing=None):
        """The shade, the heading's tab and a window, its button `selected` selected; the shade and
        the tab appearing with the window unless `shade_appearing` says otherwise (from another of
        the dialogs they are there already, as DDD keeps them between its own)."""
        controls = {}
        for nid in BUTTONS[layout]:
            controls[nid] = APPEAR if appearing else (SELECTED if nid == BUTTONS[layout][selected] else STILL)
        for nid in hidden:
            controls[nid] = None
        common = APPEAR if (appearing if shade_appearing is None else shade_appearing) else STILL
        shade = self.ddd.pieces(SHADE, float(frame), {1: common}, {})
        heading = self.ddd.pieces(HEADING, float(frame), {1: common}, {})
        controls.setdefault(1, APPEAR if appearing else STILL)
        body = self.ddd.pieces(layout, float(frame), controls, {})
        if self.days:
            if not appearing:
                body = self.ringed(layout, body, selected, hidden)
            shade, heading, body = ([days_colours(p) for p in ps] for ps in (shade, heading, body))
        return tagged(shade, 'ddd', 1) + tagged(heading, 'ddd', 2) + tagged(body, 'ddd', 3)

    def ringed(self, layout, body, selected, hidden):
        """The window with every button's ring: the unselected ones' taken from their selected
        control, held still, each after its button's own pieces (the order DDD draws its own)."""
        mf, nidx, ncount, *_ = self.ddd.layouts[self.ddd.ids.index(layout)]
        others = {k for k in range(ncount) if self.ddd.nodes[nidx + k][12] in BUTTONS[layout]
                  and self.ddd.nodes[nidx + k][12] not in hidden
                  and self.ddd.nodes[nidx + k][12] != BUTTONS[layout][selected]}
        every = {nid: SELECTED for nid in BUTTONS[layout] if nid not in hidden}
        every[1] = STILL
        rings = {}
        for p in self.ddd.pieces(layout, 0.0, every, {}):
            if p['key'][0] in others and ring(p):
                rings.setdefault(p['key'][0], []).append(dict(p, steady=True))
        out = []
        for i, p in enumerate(body):
            out.append(p)
            k = p['key'][0]
            if k in rings and (i + 1 == len(body) or body[i + 1]['key'][0] != k):
                out += rings.pop(k)
        return out

    def words_of(self, layout, step, state, selected):
        """The dialog's words and the glove, where DDD's nodes anchor them."""
        places = node_places(self.ddd, layout)
        heading = node_places(self.ddd, HEADING)[1]
        w = self.words
        out = []
        title = self.text(('title',), w['title'], LABEL_SCALE, self.title_colour)
        out.append(word_piece(('title',), title, heading[0] - title.shape[1] / 2, heading[1] + HEADING_TOP, 4))
        if layout != SETTINGS:
            prompt = w['difficulty_prompt'] if layout == THREE else w['subtitles_prompt']
            image = self.text(('prompt', step), prompt, LABEL_SCALE, WHITE)
            band = places[2] if 2 in places else places[5]
            out.append(word_piece(('prompt', step), image, tt.PICTURE[0] / 2 - image.shape[1] / 2, band[1] + PROMPT_TOP, 4))
        labels = w['difficulty'] if layout == THREE else w['subtitles'] if layout == TWO else w['proceed_buttons']
        for n, nid in enumerate(BUTTONS[layout]):
            x, y = places[nid]
            image = self.text(('button', step, n), labels[n], LABEL_SCALE, WHITE)
            middle = self.button_middle(layout, nid)
            out.append(word_piece(('button', step, n), image, middle - image.shape[1] / 2, y + BUTTON_TOP, 4))
        if layout == SETTINGS:
            difficulty, subtitles = state
            for n, (label, value) in enumerate(((w['mode_label'], w['difficulty'][difficulty]),
                                                (w['subtitles_label'], w['subtitles'][subtitles]))):
                row = places[ROWS[n]]
                at_label, at_value = (row[0] + 19 * 2, row[1] + 4 * 2), (row[0] + 215 * 2, row[1] + 4 * 2)
                a = self.text(('label', n), label, LABEL_SCALE, self.label_colour)
                b = self.text(('value', n, difficulty if n == 0 else subtitles), value, LABEL_SCALE, WHITE)
                out.append(word_piece(('label', n), a, at_label[0], at_label[1] + ROW_TOP, 4))
                out.append(word_piece(('value', n, difficulty if n == 0 else subtitles), b, at_value[0], at_value[1] + ROW_TOP, 4))
            help_lines = [w['proceed']]
        elif layout == THREE:
            help_lines = w['difficulty_help'][selected].split('\n')
        else:
            help_lines = w['subtitles_help'][selected].split('\n')
        images = [self.text(('help', step, selected, i, line), line, HELP_SCALE, self.help_colour)
                  for i, line in enumerate(help_lines)]
        widest = max(im.shape[1] for im in images)
        hx, hy = places[HELP_NODES[layout]]
        for i, (line, im) in enumerate(zip(help_lines, images)):
            out.append(word_piece(('help', step, selected, i, line), im, hx - widest / 2, hy + HELP_TOP + i * HELP_PITCH, 4))
        ax, ay = places[{THREE: (20, 22, 24), TWO: (20, 22), SETTINGS: (20, 22)}[layout][selected]]
        self.images[('word', 'hand')] = self.hand
        gx, gy = ax + HAND_OFFSET[0] - self.hand.shape[1] / 2, ay + HAND_OFFSET[1] - self.hand.shape[0] / 2
        out.append(word_piece(('hand',), self.hand, gx, gy, 5))
        return out

    def button_middle(self, layout, nid):
        """A button's middle (its bar's, sprite 1 of its still control)."""
        pieces = [p for p in self.ddd.pieces(layout, 0.0, {}, {})]
        mf, nidx, ncount, *_ = self.ddd.layouts[self.ddd.ids.index(layout)]
        k = next(i for i in range(ncount) if self.ddd.nodes[nidx + i][12] == nid)
        mine = [p for p in pieces if p['key'][0] == k]
        x0 = min(p['rect'][0] for p in mine)
        x1 = max(p['rect'][2] for p in mine)
        return (x0 + x1) / 2


def sample(length, pieces_at):
    """The clip's pieces over its frames with their keys (theater_title.clip's, any pieces)."""
    pieces = {}
    for f in range(length):
        for p in pieces_at(f):
            entry = pieces.setdefault(p['key'], {'piece': p, 'samples': {}})
            entry['samples'][f] = tuple(p['rect']) + tuple(p['colour'])
    out = []
    for key in sorted(pieces):   # the drawing order: the source first (see tagged)
        samples = pieces[key]['samples']
        present = sorted(samples)
        for f in range(length):
            if f not in samples:
                near = min(present, key=lambda g: abs(g - f))
                samples[f] = samples[near][:7] + (0.0,)
        if all(s[7] <= 0.0 for s in samples.values()):
            continue
        eps = np.array([0.01] * 4 + [0.002] * 4)
        out.append({'piece': pieces[key]['piece'], 'keys': [(f, samples[f]) for f in tt.track(samples, eps)]})
    return out


def faded(pieces, frame, length):
    """Words appearing with the window: their alpha from 0 to 1 over the clip."""
    a = min(1.0, (frame + 1) / length)
    return [dict(p, colour=(1.0, 1.0, 1.0, a)) for p in pieces]


def states():
    """Each state: the story level's options behind it, its file's name, group, layout, selected
    button and settings."""
    out = []
    for options in (1, 2):
        menu = f'newgame{options}'
        out += [(options, f'{menu}_1_{s}', 'difficulty', THREE, s, None) for s in range(3)]
        out += [(options, f'{menu}_2_{c}', 'subtitles', TWO, c, None) for c in range(2)]
        out += [(options, f'{menu}_3_{d * 4 + s * 2 + c}', 'review', SETTINGS, c, (d, s))
                for d in range(3) for s in range(2) for c in range(2)]
    return out


# The clips each dialog opens with: from the title (or nothing) the shade, the heading's tab and
# the window appear together; from another of the dialogs only the window does, the shade and the
# tab (and its words) staying as they were (no flash between them).
FROM_TITLE = 'after none intro root story mission'
FROM_DIALOG = {'difficulty': 'after subtitles review', 'subtitles': 'after difficulty review',
               'review': 'after difficulty subtitles'}


def faded_but_title(pieces, frame, length):
    """The words appearing with the window, the heading's own staying."""
    return [p for p in pieces if p['key'][1:2] == ('title',)] + \
        faded([p for p in pieces if p['key'][1:2] != ('title',)], frame, length)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('ddd', type=Path, help='Dream Drop Distance extracted: <2.8>/Image/dt/kh3d_first.hed_out')
    ap.add_argument('mare', type=Path, help='the theater extracted: <1.5+2.5>/Image/Mare.hed_out/original')
    ap.add_argument('hand', type=Path, help="the glove's picture: KH2's remastered/menu/fm/title.2ld/UK_title_2ld0.png")
    ap.add_argument('--mod', default='Theater title')
    ap.add_argument('--lang', nargs='*', default=list(LANGUAGES), choices=list(LANGUAGES))
    ap.add_argument('--preview', type=Path, help='also draw each state, as the port does, into DIR/<lang>/')
    ap.add_argument('--colours', choices=('days', 'ddd'), default='days', help="Days's greys and red, or DDD's own")
    args = ap.parse_args()
    for lang in args.lang:
        d = Dialogs(args.ddd, args.mare, args.hand, lang, args.colours)
        made = []
        for options, name, group, layout, selected, state in states():
            d.options = options
            step = int(name.split('_')[1])
            hidden = (ROWS[2],) if layout == SETTINGS else ()
            words = d.words_of(layout, step, state, selected)
            appear_len = 20
            appear = sample(appear_len, lambda f: d.background(f) + d.window(layout, selected, f, True, hidden) +
                            faded(words, f, appear_len))
            swap = sample(appear_len, lambda f: d.background(f) +
                          d.window(layout, selected, f, True, hidden, shade_appearing=False) +
                          faded_but_title(words, f, appear_len))
            hold = sample(60, lambda f: d.background(f) + d.window(layout, selected, f, False, hidden) + words)
            made.append((name, group, [(FROM_TITLE, appear_len, appear), (FROM_DIALOG[group], appear_len, swap),
                                       ('loop', 60, hold)]))
        atlas = tt.Atlas()
        for _, _, clips in made:
            for _, _, pieces in clips:
                for entry in pieces:
                    p = entry['piece']
                    region = p['region']
                    if region[0] == 'word':
                        atlas.add(region, d.images[region])
                    else:
                        source = d.theater if region[0] == 'mare' else d.ddd
                        _, base, ua, va, ub, vb = region
                        atlas.add(region, source.blocks[base].texture[va:vb, ua:ub])
        out_dir = tt.ROOT / 'mods' / args.mod / 'layouts' / 'title' / lang
        out_dir.mkdir(parents=True, exist_ok=True)
        for old in out_dir.glob('newgame_*.txt'):   # this tool's earlier names, with no menu behind
            old.unlink()
        pixels = atlas.pack()
        Image.fromarray(pixels, 'RGBA').save(out_dir / ATLAS)
        print(out_dir / ATLAS, pixels.shape[1], 'x', pixels.shape[0])
        for name, group, clips in made:
            tt.write_clips(out_dir / f'{name}.txt', group, clips, atlas, atlas_name=ATLAS)
            if args.preview:
                where = args.preview / lang
                where.mkdir(parents=True, exist_ok=True)
                pieces = clips[-1][2]
                canvas = tt.render(pixels, [atlas.texels(e['piece']) for e in pieces], pieces, 0.0, tt.PICTURE)
                Image.fromarray((canvas * 255 + 0.5).astype(np.uint8), 'RGB').save(where / f'{name}.png')
        print(lang, len(made), 'states')
    return 0


if __name__ == '__main__':
    sys.exit(main())
