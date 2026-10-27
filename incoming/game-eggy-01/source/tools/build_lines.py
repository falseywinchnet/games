"""Clean the sibling-model line drafts into the shipped line bank (assets/lines/*.txt)."""
import os, re, glob, json
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RAW = os.path.join(ROOT, 'lines', 'raw')
OUT = os.path.join(ROOT, 'assets', 'lines')
# Eggy meets nobody on the climb, and the lines must be original and kid-safe.
BANNED = re.compile(r"\b(bird|birds|bee|bees|owl|owls|wolf|wolves|deer|fox|bear|bears|bug|bugs|beetle|ant|ants|frog|frogs|fish|"
                    r"snail|slug|worm|squirrel|rabbit|bunny|hedgehog|puppy|kitten|cat|dog|moth|butterfly|butterflies|"
                    r"spider|goat|eagle|hawk|crow|raven|people|person|someone|somebody|stranger|friend|friends|mom|mother|"
                    r"dad|father|penguin|penguins|madagascar|skipper|kowalski|rico|private|gun|guns|kill|blood|dead|die|"
                    r"hate|stupid|shut up|damn|hell)\b", re.I)
os.makedirs(OUT, exist_ok=True)
seen = set()
report = {}
sources = [(p, os.path.basename(p)[:-4]) for p in sorted(glob.glob(os.path.join(RAW, '*.txt')))]
sources += [(p, 'ctx_' + os.path.basename(p)[:-4]) for p in sorted(glob.glob(os.path.join(ROOT, 'lines', 'raw_context', '*.txt')))]
for path, cat in sources:
    keep = []
    for line in open(path, encoding='utf-8', errors='ignore'):
        line = line.strip().strip('"').strip()
        line = (line.replace('’', "'").replace('‘', "'").replace('“', '"').replace('”', '"')
                    .replace('—', ' - ').replace('–', '-').replace('…', '...'))
        line = re.sub(r'^\s*(\d+[.)]|[-*])\s+', '', line)
        if not line or len(line) > 70 or len(line) < 3: continue
        if any(ord(c) > 126 for c in line): continue
        if BANNED.search(line): continue
        key = re.sub(r'[^a-z]', '', line.lower())
        if key in seen and cat != 'drink': continue
        seen.add(key)
        keep.append(line)
    open(os.path.join(OUT, cat + '.txt'), 'w').write('\n'.join(keep) + '\n')
    report[cat] = len(keep)
report['TOTAL'] = sum(report.values())
json.dump(report, open(os.path.join(ROOT, 'lines', 'line_counts.json'), 'w'), indent=1)
print(json.dumps(report, indent=1))
