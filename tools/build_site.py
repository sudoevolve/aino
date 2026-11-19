"""Assemble the static website with the repository's original Wiki Markdown.

Run from any directory. Output is always <repository>/build/site; no npm needed.
"""
from pathlib import Path
import shutil


def main():
    root = Path(__file__).resolve().parents[1]
    build = (root / "build").resolve()
    output = build / "site"
    # Only replace the dedicated generated directory, never a caller-supplied path.
    if output.is_symlink() or output.resolve().parent != build:
        raise RuntimeError(f"Refusing to replace output outside build: {output}")
    if output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True)
    for filename in ("index.html", "style.css", "wiki.js"):
        shutil.copy2(root / "site" / filename, output / filename)
    shutil.copytree(root / "site/vendor", output / "vendor")
    shutil.copytree(root / "docs/wiki", output / "docs/wiki")
    shutil.copytree(root / "screenshots", output / "screenshots")
    (output / "assets").mkdir()
    shutil.copy2(root / "assets/aino_icon.png", output / "assets/aino_icon.png")
    index = output / "index.html"
    source = index.read_text(encoding="utf-8")
    source = source.replace('name="aino-repository-base" content="../"',
                            'name="aino-repository-base" content="./"')
    source = source.replace('href="../assets/', 'href="assets/')
    index.write_text(source, encoding="utf-8")
    (output / ".nojekyll").touch()
    print(f"Website ready: {output}")


if __name__ == "__main__":
    main()
