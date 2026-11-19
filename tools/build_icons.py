"""从仓库 SVG 内嵌原图生成紧凑透明 PNG 和 Windows 多尺寸 ICO。

仅更新品牌资源时运行：python tools/build_icons.py。需要 Pillow，普通构建不需要。
SVG 保留原始素材；PNG 与所有 ICO 图层从同一份高分辨率素材生成。
"""
from base64 import b64decode
from io import BytesIO
from pathlib import Path
import xml.etree.ElementTree as ET

from PIL import Image


def main():
    assets = Path(__file__).resolve().parents[1] / "assets"
    root = ET.parse(assets / "aino_icon.svg").getroot()
    element = root.find("{http://www.w3.org/2000/svg}image")
    if element is None:
        raise ValueError("SVG 必须包含内嵌 PNG 原图")
    href = element.get("{http://www.w3.org/1999/xlink}href", "")
    if not href.startswith("data:image/png;base64,"):
        raise ValueError("只接受 SVG 内嵌 PNG，不读取外部资源")
    source = Image.open(BytesIO(b64decode(href.split(",", 1)[1]))).convert("RGBA")
    # 原素材带有极淡的背景残留；以可见轮廓定位，保留轮廓内的全部抗锯齿像素。
    bounds = source.getchannel("A").point(lambda alpha: 255 if alpha > 16 else 0).getbbox()
    if bounds is None:
        raise ValueError("原图没有可见内容")
    artwork = source.crop(bounds)
    canvas = Image.new("RGBA", (1024, 1024))
    # thumbnail 不会放大素材，会在画布上留下额外透明边距；显式等比缩放至最长边满幅。
    scale = 1024 / max(artwork.size)
    artwork = artwork.resize((round(artwork.width * scale), round(artwork.height * scale)),
                             Image.Resampling.LANCZOS)
    canvas.alpha_composite(artwork, ((1024 - artwork.width) // 2, (1024 - artwork.height) // 2))
    canvas.resize((256, 256), Image.Resampling.LANCZOS).save(assets / "aino_icon.png")
    canvas.save(assets / "aino_icon.ico", sizes=[(side, side) for side in
                (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)])
    print("Updated aino_icon.png and aino_icon.ico from the original SVG artwork")


if __name__ == "__main__":
    main()
