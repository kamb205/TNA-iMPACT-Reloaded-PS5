import re, glob, bisect, sys
gen = 'generated/default'
gotos, labels = set(), set()
for f in glob.glob(gen + '/tnaimpact_recomp.*.cpp'):
    s = open(f, errors='ignore').read()
    for body in re.split(r'^DEFINE_REX_FUNC\(', s, flags=re.M)[1:]:
        g = set(re.findall(r'goto loc_([0-9A-F]{8})', body))
        l = set(re.findall(r'^\s*loc_([0-9A-F]{8}):', body, re.M))
        gotos |= (g - l)
missing = sorted(int(x, 16) for x in gotos)
man = open('tnaimpact_manifest.toml').read()
batch_start = man.index('# Batch seeds')
seeds = sorted(int(x, 16) for x in re.findall(r'^0x([0-9A-F]{8}) = \{\}', man[batch_start:], re.M))
bad = set()
for m in missing:
    i = bisect.bisect_right(seeds, m) - 1
    if i >= 0: bad.add(seeds[i])
print(f'missing labels={len(missing)} bad seeds={len(bad)} of {len(seeds)} batch seeds')
head, tail = man[:batch_start], man[batch_start:]
tail = '\n'.join(l for l in tail.split('\n') if not (re.match(r'^0x([0-9A-F]{8}) =', l) and int(l[2:10], 16) in bad))
open('tnaimpact_manifest.toml', 'w').write(head + tail)
