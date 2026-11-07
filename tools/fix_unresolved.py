#!/usr/bin/env python3
"""Post-codegen fixer for ReXGlue 0.10.0 "conditional branch to unknown address" stubs.

Codegen sometimes emits
    // ERROR: conditional branch to unknown address 0xY
    if (cond) REX_FATAL("Unresolved branch from 0xX to 0xY");
even when loc_Y is a label in the same function. This rewrites the stub to
    if (cond) goto loc_Y;                          (label in the same function)
    if (cond) { sub_Y(ctx, base); return; }        (Y is a registered function start)
and leaves anything else untouched. Idempotent. Run after every `rexglue codegen`.

  python3 -I tools/fix_unresolved.py generated/default
"""
import glob, re, sys

gen = sys.argv[1] if len(sys.argv) > 1 else 'generated/default'
files = sorted(glob.glob(gen + '/*_recomp.*.cpp'))
func_re = re.compile(r'^DEFINE_REX_FUNC\(sub_([0-9A-F]{8})\)', re.M)
jt_re = re.compile(r'REX_FATAL\("Jump target 0x(?P<y>[0-9A-F]{8}) unresolved at bctr 0x[0-9A-F]{8}"\);')
stub_re = re.compile(r'if \((?P<cond>[^\n]*?)\) REX_FATAL\("Unresolved branch from 0x[0-9A-F]{8} to 0x(?P<y>[0-9A-F]{8})"\);')

starts = set()
for f in files:
    starts |= set(func_re.findall(open(f, errors='ignore').read()))

fixed_goto = fixed_call = left = 0
for f in files:
    src = open(f, errors='ignore').read()
    parts = re.split(r'(?=^DEFINE_REX_FUNC\()', src, flags=re.M)
    changed = False
    for i, body in enumerate(parts):
        if 'REX_FATAL("Unresolved branch' not in body and 'REX_FATAL("Jump target' not in body:
            continue
        labels = set(re.findall(r'^loc_([0-9A-F]{8}):', body, re.M))

        def repl(m):
            global fixed_goto, fixed_call, left
            y, cond = m.group('y'), m.group('cond')
            if y in labels:
                fixed_goto += 1
                return f'if ({cond}) goto loc_{y};'
            if y in starts:
                fixed_call += 1
                return f'if ({cond}) {{ sub_{y}(ctx, base); return; }}'
            left += 1
            return m.group(0)

        def repl_jt(m):
            global fixed_goto, fixed_call, left
            y = m.group('y')
            if y in labels:
                fixed_goto += 1
                return f'goto loc_{y};'
            if y in starts:
                fixed_call += 1
                return f'{{ sub_{y}(ctx, base); return; }}'
            left += 1
            return m.group(0)

        new = stub_re.sub(repl, body)
        new = jt_re.sub(repl_jt, new)
        if new != body:
            parts[i] = new
            changed = True
    if changed:
        open(f, 'w').write(''.join(parts))

print(f'goto={fixed_goto} tailcall={fixed_call} left_unresolved={left}')
