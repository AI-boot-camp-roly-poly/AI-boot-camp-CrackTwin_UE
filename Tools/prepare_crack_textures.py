"""Preserve supplied RGBA decals; derive recessed height, DirectX normals and ORM.

These maps are an artistic depth estimate from opacity, not a measured crack depth.
"""
from pathlib import Path
import shutil
import numpy as np
from PIL import Image
from scipy.ndimage import gaussian_filter

SOURCE = Path(r"C:\AA\Wall_Crack_Unreal_Decals\Textures")
OUTPUT = Path(__file__).parent / "CrackTextureSource"
SELECTED = [
    "T_complex_crack_0003_aug_3_Decal.png",
    "T_complex_crack_0343_aug_1_Decal.png",
    "T_complex_crack_0378_aug_2_Decal.png",
    "T_complex_crack_0417_aug_1_Decal.png",
    "T_complex_crack_0468_aug_3_Decal.png",
    "T_complex_crack_0749_aug_2_Decal.png",
]

def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    for slot, filename in enumerate(SELECTED, 4):
        source = SOURCE / filename
        rgba = np.asarray(Image.open(source).convert("RGBA"))
        alpha = rgba[:, :, 3].astype(np.float32) / 255
        height = 1 - 0.65 * gaussian_filter(alpha, 1.5)
        dy, dx = np.gradient(height)
        normal = np.stack((-dx * 8, dy * 8, np.ones_like(dx)), axis=-1)
        normal /= np.linalg.norm(normal, axis=-1, keepdims=True)
        orm = np.zeros_like(rgba[:, :, :3])
        orm[:, :, 0] = np.clip((1 - 0.6 * alpha) * 255, 0, 255).astype(np.uint8)
        orm[:, :, 1] = 210
        prefix = OUTPUT / f"T_Inspection_{slot:02}"
        shutil.copyfile(source, f"{prefix}_D.png")
        Image.fromarray(((normal * 0.5 + 0.5) * 255).round().astype(np.uint8)).save(f"{prefix}_N.png")
        Image.fromarray((height * 65535).round().astype(np.uint16)).save(f"{prefix}_H.png")
        Image.fromarray(orm).save(f"{prefix}_ORM.png")
        print(f"{slot}: {filename}, size={rgba.shape[1]}x{rgba.shape[0]}, alpha preserved, normal length error={abs(np.linalg.norm(normal,axis=-1)-1).max():.7f}")

if __name__ == "__main__":
    main()
