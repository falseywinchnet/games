#!/usr/bin/env python3
"""Package both delivered AAC batches unchanged, preserving source metadata."""
import hashlib
import json
import shutil
from pathlib import Path

root = Path(__file__).resolve().parents[1]
target = root / 'assets/audio'
target.mkdir(exist_ok=True)
merged = {'generated_by': 'Original Neo synthesis; two delivered batches',
          'sample_rate': 48000, 'music': [], 'stingers': [], 'sfx': [],
          'validation_summary': {}}
records = []
seen = set()
# Sticks & Stones was retired from the collection. Its delivered sounds stay in
# incoming/ as provenance but are no longer packaged.
RETIRED = 'sticks_stones'
for batch in ('01', '02'):
    source = root / f'incoming/audio-batch-{batch}/outputs'
    manifest = json.loads((source / 'audio_manifest.json').read_text())
    merged['validation_summary'][f'batch_{batch}'] = manifest.get('validation_summary', {})
    for category in ('music', 'stingers', 'sfx'):
        merged[category].extend(entry for entry in manifest[category] if RETIRED not in json.dumps(entry))
    for path in sorted((source / 'runtime').glob('*.m4a')):
        if RETIRED in path.name:
            continue
        if path.name in seen:
            raise ValueError(f'Duplicate delivered audio name: {path.name}')
        seen.add(path.name)
        destination = target / path.name
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        if not destination.exists() or hashlib.sha256(destination.read_bytes()).hexdigest() != digest:
            shutil.copy2(path, destination)
        records.append({'file': path.name, 'batch': int(batch), 'sha256': digest})
    shutil.copy2(source / 'AUDIO_MANIFEST.md', target / ('AUDIO_MANIFEST.md' if batch == '01' else 'AUDIO_BATCH_02.md'))
assert len(records) == 174
assert len(merged['music']) == 16
(target / 'audio_manifest.json').write_text(json.dumps(merged, indent=2) + '\n')
(target / 'PACKAGED_SHA256.json').write_text(json.dumps(records, indent=2) + '\n')
print(f'Packaged {len(records)} unchanged AAC files from both batches; 16 loops, 22 stingers, 136 effects.')
