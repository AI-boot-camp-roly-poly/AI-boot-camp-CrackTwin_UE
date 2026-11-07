"""Artistic recessed bevels from supplied crack opacity; no measured depth implied.

Separate assets keep original PNGs and the previous maps intact.
"""
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw
from scipy.ndimage import distance_transform_edt, gaussian_filter
from prepare_crack_textures import SOURCE, SELECTED

OUTPUT = Path(__file__).parent / 'CrackTextureBevel'

def build(rgba):
    alpha = rgba[:, :, 3].astype(np.float32) / 255
    mask = alpha > 0.08
    inside = distance_transform_edt(mask)
    outside = distance_transform_edt(~mask)
    # 7px inner bevel and 3px broken rim: slope remains visible under the decal alpha.
    depth = np.clip((inside - outside + 3.0) / 10.0, 0, 1)
    depth = depth * depth * (3 - 2 * depth)
    depth = gaussian_filter(depth, 0.65)
    height = 1 - depth * 0.8
    dy, dx = np.gradient(height)
    normal = np.stack((-dx * 14, dy * 14, np.ones_like(dx)), axis=-1)
    normal /= np.linalg.norm(normal, axis=-1, keepdims=True)
    opacity = np.maximum(alpha, np.clip(1 - outside / 3.5, 0, 1) * 0.9)
    # A visible concrete fracture face receives light; fully black RGB hid the normal.
    face = 48 + 145 * (1 - depth) ** 0.8
    original = rgba[:, :, :3].astype(np.float32)
    color = np.maximum(original * 0.45, face[:, :, None] * np.array([1.0, 0.96, 0.89]))
    color[opacity < 0.001] = 185
    out = np.dstack((np.clip(color, 0, 255), opacity * 255)).round().astype(np.uint8)
    orm = np.zeros_like(rgba[:, :, :3])
    orm[:, :, 0] = ((1 - depth * 0.45) * 255).round().astype(np.uint8)
    orm[:, :, 1] = 190
    encoded_normal = ((normal * 0.5 + 0.5) * 255).round().astype(np.uint8)
    return out, encoded_normal, (height * 65535).round().astype(np.uint16), orm

def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    preview = Image.new('RGB', (960, 640), (228, 226, 220))
    draw = ImageDraw.Draw(preview)
    for slot, filename in enumerate(SELECTED, 4):
        rgba = np.asarray(Image.open(SOURCE / filename).convert('RGBA'))
        d, n, h, orm = build(rgba)
        for kind, data in [('D', d), ('N', n), ('H', h), ('ORM', orm)]:
            Image.fromarray(data).save(OUTPUT / f'T_Inspection_{slot:02}_Bevel_{kind}.png')
        tile = Image.fromarray(d).resize((300, 300))
        x, y = ((slot - 4) % 3) * 320, ((slot - 4) // 3) * 320
        preview.paste(tile, (x, y), tile)
        draw.text((x + 6, y + 302), f'Crack {slot}', fill='black')
        vectors = n.astype(np.float32) / 127.5 - 1
        length_error = np.abs(np.linalg.norm(vectors, axis=-1) - 1).max()
        assert length_error < 0.008
        assert d[:, :, 3].min() == 0 and d[:, :, 3].max() >= rgba[:, :, 3].max()
        assert np.all(orm[:, :, 2] == 0)
        print(f'{slot}: RGBA preserved dimensions {d.shape[:2]}, normal unit error {length_error:.5f}, metallic=0')
    preview.save(OUTPUT / 'preview.jpg')

if __name__ == '__main__':
    main()
