"""Converte frames exportados pelo motor visual real em PNG/GIF.
Brilho normalizado por frame (os valores reais são ~0-70 de 255, escuros num ecrã)."""
import sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont

src, dst = sys.argv[1], sys.argv[2]
frames = []
for linha in open(src):
    partes = linha.rstrip("\n").split(";")
    frames.append((partes[0], [tuple(map(int, p.split(","))) for p in partes[1:]]))

def fonte(t):
    for f in ("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",):
        try: return ImageFont.truetype(f, t)
        except OSError: pass
    return ImageFont.load_default()

def led_img(px, cel=28, normalizar=True, ref=None):
    m = ref or max(1, max(max(c) for c in px))
    k = 255.0 / m if normalizar else 1.0
    im = Image.new("RGB", (cel * 8, cel * 8), (12, 12, 14))
    glow = Image.new("RGB", im.size, (0, 0, 0))
    d, g = ImageDraw.Draw(im), ImageDraw.Draw(glow)
    for i, (r, gg, b) in enumerate(px):
        x, y = (i % 8) * cel, (i // 8) * cel
        c = tuple(min(255, int(v * k)) for v in (r, gg, b))
        d.rounded_rectangle([x + 3, y + 3, x + cel - 3, y + cel - 3], 5, fill=(28, 28, 32))
        if any(c):
            d.ellipse([x + 5, y + 5, x + cel - 5, y + cel - 5], fill=c)
            g.ellipse([x + 1, y + 1, x + cel - 1, y + cel - 1], fill=c)
    glow = glow.filter(ImageFilter.GaussianBlur(5))
    return Image.blend(im, Image.composite(glow, im, glow.convert("L")), 0.35)

def folha(itens, colunas, titulo, nome):
    cel = 26; w = cel * 8; pad = 16; rot = 26
    linhas = (len(itens) + colunas - 1) // colunas
    W = colunas * (w + pad) + pad; H = 56 + linhas * (w + rot + pad) + pad
    S = Image.new("RGB", (W, H), (20, 24, 22)); d = ImageDraw.Draw(S)
    d.text((pad, 14), titulo, fill=(220, 240, 228), font=fonte(22))
    for n, (et, px) in enumerate(itens):
        cx = pad + (n % colunas) * (w + pad); cy = 56 + (n // colunas) * (w + rot + pad)
        S.paste(led_img(px, cel), (cx, cy))
        d.text((cx, cy + w + 4), et.split(":", 1)[-1], fill=(170, 210, 190), font=fonte(15))
    S.save(nome); print(nome)

sel = lambda p: [(e, px) for e, px in frames if e.startswith(p)]
folha(sel("rosto:") + sel("erro"), 5, "Mangue V7.6 - expressoes (frames reais do motor)", f"{dst}/rostos.png")
folha(sel("olhar:") + sel("aviso:"), 4, "Mangue V7.6 - olhar, respiracao (tamanho) e avisos", f"{dst}/olhar_avisos.png")
if sel("sinal:"): folha(sel("sinal:"), 4, "Mangue V7.6 - indicador de sistema (canto inferior direito)", f"{dst}/sinalizacao.png")
if sel("painel:"): folha(sel("painel:"), 3, "Mangue V7.7 - painel de estado na placa", f"{dst}/painel_estado.png")
if sel("rot:"): folha(sel("rot:"), 4, "Mangue V7.8 - rotacao automatica (LEDs fisicos)", f"{dst}/rotacao.png")
if sel("girar:"): folha(sel("girar:"), 4, "Mangue V7.10 - girar e fixar no botao BOOT", f"{dst}/girar_fixar.png")
amostra = [f for e in ("prot:mare", "prot:vida", "prot:vagalumes") for f in sel(e)[5::9][:4]]
folha(amostra, 4, "Mangue V7.6 - protetores de ecra (Mare / Vida / Vaga-lumes)", f"{dst}/protetores.png")

def gif(itens, nome, ms=100, legenda=True):
    imgs = []
    ref = None
    for et, px in itens:
        im = led_img(px, 30, ref=max(1, max(max(c) for c in px)) if True else ref)
        if legenda:
            F = Image.new("RGB", (im.width, im.height + 34), (20, 24, 22)); F.paste(im, (0, 34))
            ImageDraw.Draw(F).text((8, 8), et.split(":", 1)[-1].rsplit(" ", 1)[0] if et.startswith("prot") else et.split(":", 1)[-1],
                                   fill=(200, 235, 215), font=fonte(17))
            im = F
        imgs.append(im.convert("P", palette=Image.ADAPTIVE, colors=128))
    imgs[0].save(nome, save_all=True, append_images=imgs[1:], duration=ms, loop=0, optimize=True)
    print(nome, len(imgs), "quadros")

gif(sel("gif:"), f"{dst}/cena_v7.gif")
gif(sel("prot:"), f"{dst}/protetores.gif")

# Grelha de paletas: linhas = paletas, colunas = emoções
pal = sel("pal:")
if pal:
    nomes = []; [nomes.append(e.split(":")[1]) for e, _ in pal if e.split(":")[1] not in nomes]
    emos = []; [emos.append(e.split(":")[2]) for e, _ in pal if e.split(":")[2] not in emos]
    cel = 14; w = cel * 8; pad = 10; esq = 120; topo = 70
    W = esq + len(emos) * (w + pad) + pad; H = topo + len(nomes) * (w + pad) + pad
    S = Image.new("RGB", (W, H), (20, 24, 22)); d = ImageDraw.Draw(S)
    d.text((pad, 12), "Mangue V7.6 - paletas (frames reais do motor)", fill=(220, 240, 228), font=fonte(20))
    for j, e in enumerate(emos): d.text((esq + j * (w + pad), topo - 22), e, fill=(170, 210, 190), font=fonte(13))
    for i, n in enumerate(nomes):
        d.text((pad, topo + i * (w + pad) + w // 2 - 8), n.replace("_", " "), fill=(200, 230, 210), font=fonte(15))
        for j, e in enumerate(emos):
            px = dict(pal)["pal:%s:%s" % (n, e)]
            S.paste(led_img(px, cel), (esq + j * (w + pad), topo + i * (w + pad)))
    S.save(f"{dst}/paletas.png"); print(f"{dst}/paletas.png")
