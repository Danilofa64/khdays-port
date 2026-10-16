"""The pause's HD pictures for a mod (ui/pause.cpp draws them): its title, its lines and the
"Are you sure?" question, in each of the game's five languages.

  python tools/hd/hd_pause.py PIECES FONTS [--mod "mods/HD HUD"]

PIECES is a folder of the player's own pictures (from their copies of the KH games):
  pause_label.png   the title's two bars, without a word (1024 x 256)
  select2.png       a line's capsule (1024 x 193)
  command2.png      the same capsule lit (red)
  select.png        the hand that points at the lit line
FONTS is Kingdom Hearts Re:Collection's Fonts folder (Televo's free fonts): KHGummi.otf for the
title, KHMenu.otf for the lines.

Written into the mod's layouts/pause/: hand.png, and for each language <lang>/title.png,
<lang>/question.png and each line's <key>.png and <key>_lit.png. The words are the game's (its
pause/etcicon.NSCR strips over pause/pausebg_&.pbg) and the port's Retry
(host/screens/ui_text.c TXT_PAUSE_RETRY).
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage

ROOT = Path(__file__).resolve().parents[2]
LANGS = ('en', 'fr', 'de', 'it', 'es')
# The pause's words: the title, "Are you sure?", and its lines by the port's keys
WORDS = {
    'en': {'title': 'PAUSE', 'question': 'Are you sure?', 'continue': 'Continue', 'skip': 'Skip',
           'withdraw': 'Withdraw', 'retry': 'Retry', 'yes': 'Yes', 'no': 'No'},
    'fr': {'title': 'PAUSE', 'question': 'En êtes-vous sûr ?', 'continue': 'Reprendre', 'skip': 'Passer',
           'withdraw': 'Abandonner', 'retry': 'Réessayer', 'yes': 'Oui', 'no': 'Non'},
    'de': {'title': 'PAUSE', 'question': 'Sicher?', 'continue': 'Fortfahren', 'skip': 'überspringen',
           'withdraw': 'Abbrechen', 'retry': 'Wiederholen', 'yes': 'Ja', 'no': 'Nein'},
    'it': {'title': 'PAUSA', 'question': 'Sei sicuro?', 'continue': 'Continua', 'skip': 'Salta',
           'withdraw': 'Ritirati', 'retry': 'Riprova', 'yes': 'Sì', 'no': 'No'},
    'es': {'title': 'PAUSA', 'question': '¿Seguro?', 'continue': 'Continuar', 'skip': 'Omitir',
           'withdraw': 'Retirarse', 'retry': 'Reintentar', 'yes': 'Sí', 'no': 'No'},
}
LINES = ('continue', 'skip', 'withdraw', 'retry', 'yes', 'no')

# The title's look, measured on the player's PAUSE picture (1024 x 256): letters 82 dots tall,
# 0.84 as wide as the font's, white shading to grey over their last 6 dots, a black outline 9
# dots thick fading out over 3 more.
TITLE_HEIGHT, TITLE_SQUEEZE = 82, 0.84
# A line's words: the capitals 0.34 of the capsule's height, white, a black edge.
LINE_CAPS = 0.34
# A line's picture is its strip on the DS (11 x 2 cells), the capsule's rim where the game's is
# in it (dots 9-85 across, 2-13 down; the hand's
# room on its left); the question's its 14 x 2 cells, at the same scale.
STRIP_W, STRIP_H, QUESTION_W = 88, 16, 112
RIM_X0, RIM_X1, RIM_Y0, RIM_Y1 = 9, 86, 2, 14   # [x0, x1) x [y0, y1)


def word_mask(font: ImageFont.FreeTypeFont, text: str, cap_height: int, squeeze: float = 1.0) -> np.ndarray:
    """The text's coverage (0-1), its capitals `cap_height` dots tall, squeezed across."""
    probe = font.getbbox('H')
    size = font.size * cap_height / (probe[3] - probe[1])
    f = ImageFont.truetype(font.path, max(1, int(round(size))))
    box = f.getbbox(text)
    w, h = box[2] - box[0] + 8, box[3] - box[1] + 8
    im = Image.new('L', (w, h), 0)
    ImageDraw.Draw(im).text((4 - box[0], 4 - box[1]), text, font=f, fill=255)
    if squeeze != 1.0:
        im = im.resize((max(1, int(round(w * squeeze))), h), Image.LANCZOS)
    return np.asarray(im).astype(np.float32) / 255.0


def styled(mask: np.ndarray, shade: float, outline: float, fade: float, pad: int) -> np.ndarray:
    """White letters shading to grey over `shade` dots at their edge, a black outline `outline`
    dots thick fading out over `fade` more: RGBA, `pad` dots of room around."""
    m = np.pad(mask, pad)
    inside = m >= 0.5
    d_in = ndimage.distance_transform_edt(inside)
    d_out = ndimage.distance_transform_edt(~inside)
    grey = np.clip(167 + (251 - 167) * d_in / max(shade, 1e-3), 167, 251)
    edge = np.clip((outline + fade - d_out) / max(fade, 1e-3), 0, 1)
    rgba = np.zeros(m.shape + (4,), np.float32)
    rgba[..., 3] = np.maximum(edge, m) * 255
    rgba[..., :3] = (grey * m)[..., None]
    return rgba


def over(dst: np.ndarray, src: np.ndarray, x: int, y: int) -> None:
    """`src` (RGBA, float) over `dst` (RGBA, float) at (x, y), clipped."""
    h, w = src.shape[:2]
    x0, y0, x1, y1 = max(x, 0), max(y, 0), min(x + w, dst.shape[1]), min(y + h, dst.shape[0])
    if x0 >= x1 or y0 >= y1:
        return
    s = src[y0 - y:y1 - y, x0 - x:x1 - x]
    d = dst[y0:y1, x0:x1]
    sa = s[..., 3:4] / 255.0
    da = d[..., 3:4] / 255.0
    out_a = sa + da * (1 - sa)
    d[..., :3] = np.where(out_a > 0, (s[..., :3] * sa + d[..., :3] * da * (1 - sa)) / np.maximum(out_a, 1e-6), 0)
    d[..., 3:4] = out_a * 255


def rgba(path: Path) -> np.ndarray:
    return np.asarray(Image.open(path).convert('RGBA')).astype(np.float32)


def save(a: np.ndarray, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(np.clip(a, 0, 255).astype(np.uint8), 'RGBA').save(path)


def rim_box(capsule: np.ndarray):
    """The capsule's rim (its opaque bright dots: the shadow left out), [x0, x1) x [y0, y1)."""
    rim = (capsule[..., 3] > 128) & (capsule[..., :3].min(axis=2) > 150)
    ys, xs = np.nonzero(rim)
    return xs.min(), xs.max() + 1, ys.min(), ys.max() + 1


def line_picture(capsule: np.ndarray, rim, k: float, words: str, font: ImageFont.FreeTypeFont) -> np.ndarray:
    """The line's strip, `k` texels a DS dot: the capsule with its rim where the game's is, the
    words in its middle."""
    rx0, rx1, ry0, ry1 = rim
    out = np.zeros((int(round(STRIP_H * k)), int(round(STRIP_W * k)), 4), np.float32)
    x, y = int(round(RIM_X0 * k - rx0)), int(round((RIM_Y0 + RIM_Y1) / 2 * k - (ry0 + ry1) / 2))
    over(out, capsule, x, y)
    rh = ry1 - ry0
    text = styled(word_mask(font, words, int(rh * LINE_CAPS)), shade=2.0, outline=3.0, fade=1.5, pad=6)
    room = (rx1 - rx0) - 2 * int(rh * 0.45)
    if text.shape[1] > room:
        text = np.asarray(Image.fromarray(np.clip(text, 0, 255).astype(np.uint8), 'RGBA').resize(
            (room, text.shape[0]), Image.LANCZOS)).astype(np.float32)
    over(out, text, x + (rx0 + rx1 - text.shape[1]) // 2, y + (ry0 + ry1 - text.shape[0]) // 2)
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('pieces', type=Path, help='the folder of the pause\'s pictures (see above)')
    parser.add_argument('fonts', type=Path, help='Kingdom Hearts Re:Collection\'s Fonts folder')
    parser.add_argument('--mod', type=Path, default=ROOT / 'mods' / 'HD HUD', help='the mod to write into')
    args = parser.parse_args()
    need = ['pause_label.png', 'command2.png', 'select2.png', 'select.png']
    missing = [n for n in need if not (args.pieces / n).exists()]
    fonts = [args.fonts / 'KHGummi.otf', args.fonts / 'KHMenu.otf']
    missing += [str(f) for f in fonts if not f.exists()]
    if missing:
        sys.exit(f'missing: {", ".join(missing)}')
    label = rgba(args.pieces / 'pause_label.png')
    plain = rgba(args.pieces / 'select2.png')
    lit = rgba(args.pieces / 'command2.png')
    hand = rgba(args.pieces / 'select.png')
    gummi = ImageFont.truetype(str(fonts[0]), 100)
    menu = ImageFont.truetype(str(fonts[1]), 100)
    out = args.mod / 'layouts' / 'pause'
    save(hand, out / 'hand.png')
    rim = rim_box(plain)
    k = (rim[1] - rim[0]) / (RIM_X1 - RIM_X0)
    for lang in LANGS:
        words = WORDS[lang]
        title = label.copy()
        mask = word_mask(gummi, words['title'], TITLE_HEIGHT, TITLE_SQUEEZE)
        word = styled(mask, shade=6.0, outline=9.0, fade=3.0, pad=16)
        over(title, word, (title.shape[1] - word.shape[1]) // 2, (title.shape[0] - word.shape[0]) // 2)
        save(title, out / lang / 'title.png')
        q = np.zeros((int(round(STRIP_H * k)), int(round(QUESTION_W * k)), 4), np.float32)
        text = styled(word_mask(menu, words['question'], int((rim[3] - rim[2]) * LINE_CAPS)), shade=2.0, outline=3.0,
                      fade=1.5, pad=6)
        over(q, text, (q.shape[1] - text.shape[1]) // 2, (q.shape[0] - text.shape[0]) // 2)
        save(q, out / lang / 'question.png')
        for key in LINES:
            save(line_picture(plain, rim, k, words[key], menu), out / lang / f'{key}.png')
            save(line_picture(lit, rim, k, words[key], menu), out / lang / f'{key}_lit.png')
        print(f'{lang}: written to {out / lang}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
