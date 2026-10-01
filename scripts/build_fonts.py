#!/usr/bin/env python3
"""Generate checked-in LVGL glyph subsets with pinned external tools.
Requires tools/font-env (FontTools4.60.1) and tools/font-builder (lv_font_conv1.5.3).
"""
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
subprocess.run(['python3','scripts/generate_i18n.py'],cwd=root,check=True)
subprocess.run([str(root/'tools/font-env/bin/python'),'-m','fontTools.varLib.instancer','fonts/NotoSansSC.ttf','wght=450','--output','fonts/NotoSansSC-Regular.ttf'],cwd=root,check=True)
chars=(root/'fonts/characters.txt').read_text()
for size in (14,20,24):
 subprocess.run(['node','tools/font-builder/node_modules/lv_font_conv/lv_font_conv.js','--font','fonts/NotoSansSC-Regular.ttf','--size',str(size),'--bpp','4','--format','lvgl','--range','0x20-0x7e','--symbols',chars,'--no-compress','--lv-include','lvgl.h','-o',f'fonts/panel_cjk_{size}.c'],cwd=root,check=True)
