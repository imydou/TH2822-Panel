#!/usr/bin/env python3
"""Package local build outputs and selected source files; excludes backups and user references."""
from pathlib import Path
import hashlib
import json
import shutil
import zipfile
root = Path(__file__).resolve().parents[1]
meta = json.loads((root/'build/project_description.json').read_text())
version = meta['project_version']
destination = root/'release'/version
destination.mkdir(parents=True, exist_ok=True)
outputs = {
 'build/th2822_panel.bin': 'th2822_panel.bin',
 'build/th2822_panel.elf': 'th2822_panel.elf',
 'build/bootloader/bootloader.bin': 'bootloader.bin',
 'build/partition_table/partition-table.bin': 'partition-table.bin',
 'sdkconfig': 'sdkconfig', 'dependencies.lock': 'dependencies.lock',
}
for source, target in outputs.items():
 shutil.copy2(root/source, destination/target)
manifest = {'version': version, 'target': meta['target'], 'idf': meta['git_revision'],
 'flash_mode': 'dio', 'flash_frequency': '80m', 'flash_size': '16MB',
 'images': {'0x0': 'bootloader.bin', '0x8000': 'partition-table.bin', '0x10000': 'th2822_panel.bin'},
 'hardware_status': 'See docs/VALIDATION.md. No TH2822 end-to-end instrument test yet.'}
(destination/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
files = [root/name for name in ['CMakeLists.txt','sdkconfig','sdkconfig.defaults','dependencies.lock','README.md','THIRD_PARTY_NOTICES.md','.gitignore']]
for directory in ['main','core','tests','scripts','resources','licenses','docs','fonts']:
 files += [p for p in (root/directory).rglob('*') if p.is_file() and p.suffix not in {'.ttf','.ppm','.pyc'} and '__pycache__' not in p.parts]
files += list((root/'tools/font-builder').glob('package*.json'))
with zipfile.ZipFile(destination/f'th2822-panel-{version}-source.zip','w',zipfile.ZIP_DEFLATED) as archive:
 for p in sorted(set(files)):
  archive.write(p,p.relative_to(root))
checksums = []
for p in sorted(destination.iterdir()):
 if p.is_file() and p.name != 'SHA256SUMS':
  checksums.append(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.name)
(destination/'SHA256SUMS').write_text('\n'.join(checksums)+'\n')
print(destination)
