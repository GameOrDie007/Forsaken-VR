"""The Steam Frame library art, cut from the splash (art/vrsplash.png): the cover (600x900), the
wide capsule (920x430), the hero (1920x620) and the icon (256x256). The steam-frame kit puts them in
the Frame's library (Frame-CopyOurs, Frame-EnsureEntry). The splash has no logo without its
background, so the entry has none (the kit copies only the files there are).
Written into package/quest/frame-art/. Usage: python tools/make-frame-art.py
"""
import os

from PIL import Image, ImageEnhance, ImageFilter

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPLASH = os.path.join(HERE, "art", "vrsplash.png")
PLATE = (980, 0, 1680, 700)   # the heart and its banner, a square of the 1920x1080 splash


def backdrop(src, size):
    """The splash filling size (cropped to its shape), blurred and darkened: the ground the art sits on."""
    w, h = src.size
    W, H = size
    if w * H > W * h:
        cw = h * W // H
        bg = src.crop(((w - cw) // 2, 0, (w - cw) // 2 + cw, h))
    else:
        ch = w * H // W
        bg = src.crop((0, (h - ch) // 2, w, (h - ch) // 2 + ch))
    bg = bg.resize(size, Image.LANCZOS).filter(ImageFilter.GaussianBlur(radius=max(W, H) // 40))
    return ImageEnhance.Brightness(bg).enhance(0.55)


def contained(src, part, size):
    """part scaled whole into size, centred on the backdrop: nothing cut."""
    W, H = size
    pw, ph = part.size
    k = min(W / pw, H / ph)
    p = part.resize((max(1, round(pw * k)), max(1, round(ph * k))), Image.LANCZOS)
    out = backdrop(src, size)
    out.paste(p, ((W - p.size[0]) // 2, (H - p.size[1]) // 2))
    return out


src = Image.open(SPLASH).convert("RGB")
plate = src.crop(PLATE)
out = os.path.join(HERE, "package", "quest", "frame-art")
os.makedirs(out, exist_ok=True)
contained(src, plate, (600, 900)).save(os.path.join(out, "cover.jpg"), quality=92)
contained(src, src, (920, 430)).save(os.path.join(out, "wide.jpg"), quality=92)
contained(src, src, (1920, 620)).save(os.path.join(out, "hero.jpg"), quality=92)
plate.resize((256, 256), Image.LANCZOS).save(os.path.join(out, "icon.png"))
print("made", out)
