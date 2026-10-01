#!/usr/bin/env python3
from pathlib import Path
import json,re
root=Path(__file__).resolve().parents[1]
base=root/'resources/locales'
registry=json.loads((base/'registry.json').read_text())
english=json.loads((base/(registry['fallback']+'.json')).read_text())
for meta in registry['locales']:
 values=json.loads((base/(meta['id']+'.json')).read_text())
 for key,value in values.items():
  assert key in english
  assert set(re.findall(r'\{(\w+)\}',value))==set(re.findall(r'\{(\w+)\}',english[key])), (meta['id'],key)
# Every literal semantic UI/error key in application sources must be in the fallback resource.
for folder in ('core','main'):
 for p in (root/folder).glob('*.cpp'):
  for key in re.findall(r'"((?:app|model|status|button|reading|control|value|menu|stats|hint|error|usb)\.[a-z_]+)"',p.read_text()):
   assert key in english, (p.name,key)
print('PASS: registered locales, semantic keys and named placeholder sets')
