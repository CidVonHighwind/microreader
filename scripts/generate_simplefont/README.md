Generate SimpleGFXfont headers
================================

This package provides a small CLI to generate C++ font headers compatible
with `SimpleGFXfont` used by the project's `TextRenderer`.

Quick start
-----------

1. Install the runtime dependency:

```powershell
python -m pip install -r scripts/generate_simplefont/requirements.txt
```

2. Generate a header for ASCII space character (example):

```powershell
python -m scripts.generate_simplefont.cli --name FreeSans12pt7b --size 8 --chars 32 --out src/Fonts/FreeSans12pt7b.h
```

3. Generate preview images by passing `--preview-output`.

4. Use the GUI to preview individual glyphs:

```powershell
python scripts/generate_simplefont/gui.py
```

Notes
-----
- The implementation uses Pillow to rasterize fonts when a TTF is provided.
- The package exposes `main()` so other scripts can import and call it.
