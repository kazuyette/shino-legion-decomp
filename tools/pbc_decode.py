#!/usr/bin/env python3
"""Decode DIRECTOR graphics (.PBC files and the images embedded in DIRECTOR.PPB) to PNG.

Format (see docs/director.md, "Graphics"):
  * A .PBC file is ONE block of the word-LZ used everywhere in the game
    (tools/ppb_decompress.py: u32 0x80|size_in_words, flag word + 16 items).
    It has NO header: the decompressed bytes are raw VDP1 character data,
    concatenated, loaded at VDP1 VRAM 0x25C04740 (Res_Request kind 1 = "LZ on load").
  * Dimensions / colour mode come from a character list stored in DIRECTOR.PPB
    and registered by Char_RegisterList (A.BIN 0x06006E64):
        u16 0, u16 count, then count x {u16 offset/32, u16 size/32, u16 mode, u8 w/8, u8 h}
    mode 0x28 = RGB555 (bit 15 = opaque, R bits 0-4, G 5-9, B 10-14),
    mode 0x00 = 4bpp colour bank (high nibble = left pixel, index 0 = transparent).
  * Palettes (RGB555, 16 colours per bank) also live in DIRECTOR.PPB and are copied
    to the shadow CRAM with Pal_Copy (A.BIN 0x06005C30).

Usage:
  pbc_decode.py sheet NAME OUT.png        -> every char of TITLE|STGCLEAR|STAFFROL|LOGO|CONTINUE|FONT
  pbc_decode.py title OUT.png             -> composed title screen (bg + menu, as Title_* draws it)
  pbc_decode.py stgclear OUT.png          -> composed stage-clear tally screen (STGCLEAR + font)
Needs extracted/DIRECTOR.dec.bin and extracted/track1.iso (tools/iso_extract.py), Pillow.
"""
import os, sys, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ppb_decompress import decompress
from iso_extract import read as iso_read
from PIL import Image

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'extracted')
B = 0x06040000
D = open(os.path.join(ROOT, 'DIRECTOR.dec.bin'), 'rb').read()

def u16(a): return struct.unpack('>H', D[a - B:a - B + 2])[0]

# name: (source, char list, palette address, palette size)
# source = PBC file name on the disc, or an address of an LZ block inside DIRECTOR.PPB
ASSETS = {
    'TITLE':    ('TITLE.PBC',    0x06087230, 0x060871B0, 64),
    'STGCLEAR': ('STGCLEAR.PBC', 0x06087354, 0x06087314, 32),
    'STAFFROL': ('STAFFROL.PBC', 0x06087BB0, 0x06087B90, 16),
    'LOGO':     (0x0608466A,     0x060870B0, None, 0),
    'CONTINUE': (0x0608742C,     0x060873D0, 0x060873B0, 16),
    'FONT':     (0x06043F9C,     0x06044272, 0x060443AE, 32),
}

def charlist(a):
    n = (u16(a) << 16) | u16(a + 2)
    out = []
    for i in range(n):
        e = a + 4 + 8 * i
        out.append(dict(off=u16(e) * 32, size=u16(e + 2) * 32, mode=u16(e + 4),
                        w=D[e + 6 - B] * 8, h=D[e + 7 - B]))
    return out

def palette(a, n):
    return [u16(a + 2 * i) for i in range(n)]

def rgb(c):
    return ((c & 31) * 255 // 31, (c >> 5 & 31) * 255 // 31, (c >> 10 & 31) * 255 // 31)

def load(name):
    src, cl, pa, pn = ASSETS[name]
    if isinstance(src, str):
        raw = iso_read(os.path.join(ROOT, 'track1.iso'), src)
    else:
        raw = D[src - B:]
    data = decompress(raw)
    need = max(c['off'] + c['size'] for c in charlist(cl))
    data += bytes(max(0, need - len(data)))   # LOGO block is one word short of its char
    return data, charlist(cl), (palette(pa, pn) if pa else [])

def char_image(data, c, pal, bank=0):
    w, h = c['w'], c['h']
    im = Image.new('RGBA', (w, h))
    px = im.load()
    p = c['off']
    if c['mode'] == 0x28:
        for y in range(h):
            for x in range(w):
                v = struct.unpack('>H', data[p:p + 2])[0]; p += 2
                px[x, y] = rgb(v) + ((255,) if v & 0x8000 else (0,))
    elif c['mode'] == 0x00:
        for y in range(h):
            for x in range(0, w, 2):
                b = data[p]; p += 1
                for k, n in enumerate((b >> 4, b & 15)):
                    if n:
                        i = bank * 16 + n
                        px[x + k, y] = rgb(pal[i] if i < len(pal) else 0x7FFF) + (255,)
    else:
        raise ValueError('mode %x not handled' % c['mode'])
    return im

def sheet(name, out):
    data, cl, pal = load(name)
    ims = [char_image(data, c, pal) for c in cl]
    W = max(max(i.width for i in ims), 320)
    H = sum(i.height + 4 for i in ims)
    s = Image.new('RGBA', (W, H), (40, 40, 60, 255)); y = 0
    for im in ims:
        s.alpha_composite(im, (0, y)); y += im.height + 4
    s.save(out)

class Screen:
    """Tiny model of the Saturn screen: VDP1 sprites drawn in call order."""
    def __init__(self):
        self.im = Image.new('RGBA', (320, 224), (0, 0, 0, 255))
    def spr(self, data, c, pal, x, y, bank=0):
        self.im.alpha_composite(char_image(data, c, pal, bank), (x, y))

# Font_DrawChar 0x06040368: '0'-'9' -> 0-9, 'A'-'Z' -> 10-35, '.' 36, ':' 37, '-' 38, ' ' skipped
def glyph(ch):
    o = ord(ch)
    if ch == ' ': return None
    if ch == '.': return 36
    if ch == ':': return 37
    if ch == '-': return 38
    return o - 48 if o <= 57 else o - 55

def text(scr, font, s, x, y):            # Font_DrawString 0x060403FA (8 px advance)
    data, cl, pal = font
    for ch in s:
        g = glyph(ch)
        if g is not None: scr.spr(data, cl[g], pal, x, y)
        x += 8

def number(scr, font, v, x, y, digits):  # Font_DrawNumber 0x0604042E (leading zeros kept)
    text(scr, font, str(v).rjust(digits, '0')[-digits:], x, y)

def title(out):
    data, cl, pal = load('TITLE')
    s = Screen()
    s.spr(data, cl[0], pal, 0, 0)              # Title_DrawBg 0x06041428
    s.spr(data, cl[2], pal, 88, 152)           # Title_DrawCursor 0x060415AA (bank 0)
    s.spr(data, cl[3], pal, 136, 152, bank=1)  # Title_DrawMenuItems 0x06041620: GAME START
    s.spr(data, cl[4], pal, 136, 172, bank=1)  #   OPTIONS
    s.spr(data, cl[6], pal, 85, 164 + 30, bank=3)  # "PRESS START" (85,164) is drawn instead of the menu
    s.im.save(out)

def stgclear(out):
    data, cl, pal = load('STGCLEAR')
    font = load('FONT')
    s = Screen()
    s.spr(data, cl[10], pal, 0, 0)             # StgClear_DrawBg 0x06042AA8: background
    s.spr(data, cl[0], pal, 128, 8, bank=1)    #   stage number ((idx>>1)-1), here stage 1
    s.spr(data, cl[9], pal, 16, 24)            #   "STAGE CLEAR" banner
    # StgClear_DrawText 0x06042B5C
    text(s, font, 'HIGH SCORE', 48, 88);  number(s, font, 123450, 208, 88, 8)
    text(s, font, 'SCORE', 88, 112);      number(s, font, 54320, 184, 112, 8)
    text(s, font, 'LIFE BONUS', 48, 144); number(s, font, 6000, 232, 144, 5)
    text(s, font, 'NO SHURIKEN BONUS', 48, 168); number(s, font, 5000, 232, 168, 5)
    text(s, font, 'NO DAMAGE BONUS', 48, 192);   number(s, font, 10000, 232, 192, 5)
    s.im.save(out)

if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'sheet': sheet(sys.argv[2], sys.argv[3])
    elif cmd == 'title': title(sys.argv[2])
    elif cmd == 'stgclear': stgclear(sys.argv[2])
    else: print(__doc__)
