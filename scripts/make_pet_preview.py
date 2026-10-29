#!/usr/bin/env python3
"""Writes the PET_PREVIEW block of web-sync/index.html: first idle pose + body color of each pet species,
read from sketch/src/pets/species/*.cpp (claude-desktop-buddy, MIT). Order = species numbers ("n") in pets.cpp."""
import json, re, pathlib, sys
root = pathlib.Path(__file__).resolve().parent.parent
pets = root / 'sketch/src/pets'
src = (pets / 'pets.cpp').read_text()
arr = re.search(r'SPECIES\[\]\s*=\s*\{(.*?)\};', src, re.S).group(1)
names = re.findall(r'([A-Z]+)_SPECIES', arr)
small = set(re.findall(r'&([A-Z]+)_SPECIES', arr))  # carried by every build
out = []
for n in names:
    s = (pets / 'species' / (n.lower() + '.cpp')).read_text()
    m = re.search(r'extern const Species \w+ = \{\s*"([^"]+)",\s*(0x[0-9A-Fa-f]+)', s)
    idle = s[s.index('static void doIdle'):]
    first = re.search(r'=\s*\{((?:\s*"(?:[^"\\]|\\.)*"\s*,?)+)\}', idle).group(1)
    lines = [json.loads('"' + x + '"') for x in re.findall(r'"((?:[^"\\]|\\.)*)"', first)]
    c = int(m.group(2), 16)
    rgb = '#%02x%02x%02x' % (((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31)
    out.append({'name': m.group(1), 'c': rgb, 'l': lines, 'all': n not in small})
block = '// PET_PREVIEW start (scripts/make_pet_preview.py)\nconst PET_PREVIEW = ' + json.dumps(out, ensure_ascii=False, separators=(',', ':')) + ';\n// PET_PREVIEW end'
page = root / 'web-sync/index.html'
h = page.read_text()
if '// PET_PREVIEW start' in h:
    h = re.sub(r'// PET_PREVIEW start.*?// PET_PREVIEW end', lambda _: block, h, flags=re.S)
else:
    h = h.replace('const PREVIEW_ART = {', block + '\nconst PREVIEW_ART = {', 1)
page.write_text(h)
print(len(out), 'species,', sum(not o['all'] for o in out), 'in every build')
