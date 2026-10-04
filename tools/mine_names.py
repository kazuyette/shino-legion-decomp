#!/usr/bin/env python3
"""Mine original-address -> name knowledge from the PC port and the docs.

Writes docs/ghidra_names.csv (program,address,kind,name,comment,aliases,source)
consumed by ghidra_scripts/ApplyPortNames.java (see tools/apply_ghidra_names.sh)
and docs/ghidra_names_conflicts.csv (every address that got >1 distinct name).

program: A.BIN (addr < 0x06040000), SHINOBI or DIRECTOR (overlay at 0x06040000,
decided by an explicit "SHINOBI"/"DIRECTOR" next to the address, else by the
file: director*/movie* -> DIRECTOR, everything else -> SHINOBI).
Re-run: python3 tools/mine_names.py   (deterministic, no side effects besides
the two CSV files).
"""
import csv, os, re, sys, collections

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'pc-port', 'src')
DOCS = os.path.join(ROOT, 'docs')
OUT = os.path.join(DOCS, 'ghidra_names.csv')
OUT_CONF = os.path.join(DOCS, 'ghidra_names_conflicts.csv')

OVL = 0x06040000
HEX = r'0x(06[0-9A-Fa-f]{6})u?'
PFX = r'(?:(A\.BIN|SHINOBI(?:\.dec\.bin|\.PPB)?|DIRECTOR(?:\.dec\.bin|\.PPB)?|enemy code|orig(?:inal)?)\s+)?'
DEFAULT_RE = re.compile(r'^(FUN|DAT|LAB|sub|thunk_FUN|PTR|UNK|SUB|func|f)_[0-9A-Fa-f]{6,8}$')
KEYWORDS = {'if', 'while', 'for', 'switch', 'return', 'sizeof', 'case', 'else', 'do', 'goto', 'defined',
            # SH-2 mnemonics quoted in the docs
            'braf', 'bsrf', 'bsr', 'bra', 'jsr', 'jmp', 'rts', 'rte', 'bt', 'bf', 'mova', 'trapa'}
# port-side RAM/helper macros that never correspond to an original routine
HELPERS = {'W8', 'W16', 'W32', 'R8', 'R16', 'R32', 'RS8', 'RS16', 'RS32', 'RU8', 'RU16', 'RU32',
           'Ram_Ptr', 'RAM', 'PTR', 'memcpy', 'memset', 'memmove', 'printf', 'fprintf', 'abs',
           'ADDR', 'U16', 'U32', 'S16', 'S32', 'BE16', 'BE32', 'SDL_Log', 'assert', 'MIN', 'MAX',
           'CLAMP', 'snprintf', 'strcmp', 'strlen', 'Ram_Base', 'H', 'L', 'add32', 'sub32'}

PRIO = {'c_def': 100, 'c_def_loose': 95, 'c_proto': 90, 'doc_table': 80, 'c_call': 70,
        'doc_prose': 60, 'c_data': 50, 'doc_data': 45, 'c_sub': 10}

cands = []   # dicts
# module prefixes that only exist in the SHINOBI (gameplay) overlay
SHINOBI_ONLY = r'^(Player|Game|Stage|Enemy|Obj|Proj|Ninja|Soldier|Coll|Camera|Map|Hud|Guard\d)_'


def prog_for(addr, ctx, default_ovl):
    if addr < OVL:
        return 'A.BIN'
    c = ctx[-48:] if ctx else ''
    i_d, i_s = c.rfind('DIRECTOR'), c.rfind('SHINOBI')
    if i_d > i_s:
        return 'DIRECTOR'
    if i_s > i_d:
        return 'SHINOBI'
    return default_ovl


def safe(name):
    n = re.sub(r'[^A-Za-z0-9_]', '_', name)
    if n and n[0].isdigit():
        n = '_' + n
    return n


def add(prog, addr, kind, name, comment, src, how, mtime, ambig=False):
    if kind == 'function' and addr & 1:
        return
    if not (0x06000000 <= addr <= 0x060FFFFF):
        return
    cands.append(dict(prog=prog, addr=addr, kind=kind, name=safe(name) if name else '',
                      comment=' '.join(comment.split())[:240], src=src, how=how,
                      prio=PRIO[how], mtime=mtime, ambig=ambig))


def strip_comment(c):
    c = c.strip()
    if c.startswith('/*'):
        c = c[2:]
    if c.endswith('*/'):
        c = c[:-2]
    if c.startswith('//'):
        c = c[2:]
    lines = [re.sub(r'^\s*\*?\s?', '', l) for l in c.split('\n')]
    return ' '.join(l.strip() for l in lines).strip()


def addr_from_comment(text, fname):
    """Address a comment attributes to the code it annotates, or None.
    Returns (addr, ctx, loose)."""
    t = text.strip().lstrip('-').strip()
    m = re.match(PFX + HEX + r'(?![0-9A-Fa-f])', t)
    if m:
        return int(m.group(2), 16), (m.group(1) or ''), False
    # "Name 0xADDR" / "setter 0xADDR" (<= 2 words then the address, nothing else, or a colon)
    m = re.match(r'^(?:[\w.\']+\s+){1,2}' + PFX + HEX + r'\s*(?::|$|\(|,)', t)
    if m:
        return int(m.group(2), 16), t[:m.start(2)], True
    # "Projectile 2 (0x06069CAC): ..." -- a lone parenthesised address in the first words
    m = re.match(r'^[^()]{0,50}?\(' + PFX + HEX + r'\)', t)
    if m:
        return int(m.group(2), 16), t[:m.start(2)], True
    return None


def default_ovl_for(fname):
    b = os.path.basename(fname)
    return 'DIRECTOR' if b.startswith(('director', 'movie')) else 'SHINOBI'


FUNC_DEF = re.compile(
    r'^(?P<sig>(?:(?:static|inline|extern|const|unsigned|signed|struct|volatile)\s+)*'
    r'[A-Za-z_][\w]*[\s\*]+(?P<name>[A-Za-z_]\w*)\s*\((?P<args>[^;{}()]*(?:\([^()]*\)[^;{}()]*)*)\))'
    r'[ \t]*(?P<trail>/\*[^\n]*?\*/|//[^\n]*)?\s*(?P<end>\{|;)', re.M)


def preceding_comments(text, pos, maxn=2):
    out = []
    i = pos
    for _ in range(maxn):
        j = i
        while j > 0 and text[j - 1] in ' \t\r\n':
            j -= 1
        if text[j - 2:j] == '*/':
            k = text.rfind('/*', 0, j)
            if k < 0:
                break
            # must start its own line
            ls = text.rfind('\n', 0, k) + 1
            if text[ls:k].strip():
                break
            out.append(text[k:j]); i = k
            continue
        # run of // lines
        ls = text.rfind('\n', 0, j) + 1
        line = text[ls:j]
        if line.lstrip().startswith('//'):
            k = ls
            while True:
                ps = text.rfind('\n', 0, k - 1) + 1
                if k > 0 and text[ps:k].lstrip().startswith('//'):
                    k = ps
                else:
                    break
            out.append(text[k:j]); i = k
            continue
        break
    return out


def mine_c(path):
    rel = os.path.relpath(path, ROOT).replace(os.sep, '/')
    fname = os.path.basename(path)
    mt = os.path.getmtime(path)
    text = open(path, encoding='utf-8', errors='replace').read()
    dov = default_ovl_for(fname)
    lines = text.split('\n')

    def lineno(pos):
        return text.count('\n', 0, pos) + 1

    # 1. function definitions / prototypes
    for m in FUNC_DEF.finditer(text):
        name = m.group('name')
        if name in KEYWORDS or m.group('sig').split()[0] in ('return', 'else', 'typedef'):
            continue
        is_def = m.group('end') == '{'
        found = None
        if m.group('trail'):
            found = addr_from_comment(strip_comment(m.group('trail')), fname)
            ctext = strip_comment(m.group('trail'))
        if not found:
            for c in preceding_comments(text, m.start()):
                ctext = strip_comment(c)
                found = addr_from_comment(ctext, fname)
                if found:
                    break
        src = f'{rel}:{lineno(m.start())}'
        if DEFAULT_RE.match(name):
            a = int(name.split('_')[-1], 16)
            add(prog_for(a, '', dov), a, 'function', '', f'port: {rel} {name}()', src, 'c_sub', mt)
            continue
        if not found:
            continue
        a, ctx, loose = found
        if loose and (re.search(r'\b(inline[d]?\s+in|part\s+of|called\s+from|caller)\b', ctext, re.I)
                      or (ctext.startswith('-') and name[:1].islower())):
            continue
        how = ('c_def_loose' if loose else 'c_def') if is_def else 'c_proto'
        add(prog_for(a, ctx, dov), a, 'function', name,
            f'port: {rel} {name}() -- {ctext}', src, how, mt)

    # 2. #define NAME 0x060xxxxx  and  declarations with a trailing address comment
    for i, l in enumerate(lines, 1):
        m = re.match(r'^\s*#\s*define\s+([A-Za-z_]\w*)\s+\(?\s*' + HEX + r'\s*\)?\s*(/\*.*|//.*)?$', l)
        if m:
            a = int(m.group(2), 16)
            cm = strip_comment(m.group(3) or '')
            add(prog_for(a, cm, dov), a, 'data', m.group(1), f'port: {rel} #define {m.group(1)}' + (f' -- {cm}' if cm else ''),
                f'{rel}:{i}', 'c_data', mt)
            continue
        # inline call annotation:  Foo(...);  /* 0xADDR ... */
        m = re.search(r';\s*(?:break;\s*)?/\*\s*(.*?)\s*(?:\*/|$)', l)
        if m and not l.lstrip().startswith(('/*', '*', '//', '#')):
            got = addr_from_comment(m.group(1), fname)
            if got and not got[2]:
                code = l[:m.start() + 1]
                calls = [c for c in re.findall(r'\b([A-Za-z_]\w*)\s*\(', code)
                         if c not in KEYWORDS and c not in HELPERS and not re.match(r'^u?int\d+_t$', c)]
                if len(calls) == 1 and '=' not in code.split(calls[0])[0].replace('==', '').split(';')[-1][:0] :
                    nm = calls[0]
                    a = got[0]
                    if DEFAULT_RE.match(nm) or len(nm) <= 3 or re.match(r'^[A-Z0-9_]+$', nm):
                        continue
                    add(prog_for(a, got[1], dov), a, 'function', nm,
                        f'port: {rel} calls {nm}() here -- {m.group(1)}', f'{rel}:{i}', 'c_call', mt)
        for s in re.findall(r'\bsub_(06[0-9A-Fa-f]{6})\b', l):
            a = int(s, 16)
            add(prog_for(a, l[:l.find(s)], dov), a, 'function', '', f'port: {rel} uses sub_{s.upper()}', f'{rel}:{i}', 'c_sub', mt)


def kind_of(name):
    if re.match(r'^(g_|G_|k[A-Z]|s_|d_|D_|V_)', name) or re.match(r'^[A-Z0-9_]+$', name):
        return 'data'
    return 'function'


def mine_doc(path):
    rel = os.path.relpath(path, ROOT).replace(os.sep, '/')
    fname = os.path.basename(path)
    mt = os.path.getmtime(path)
    dov = 'DIRECTOR' if fname == 'director.md' else 'SHINOBI'
    dec_era = fname != 'jump_table_functions.md'
    for i, l in enumerate(open(path, encoding='utf-8', errors='replace'), 1):
        l = l.rstrip('\n')
        if fname == 'jump_table_functions.md' and l.startswith('## Pad input FOUND'):
            dec_era = True   # before this point overlay addresses refer to the raw .PPB imports

        def ok(a):
            return a < OVL or dec_era

        m = re.match(r'^\|\s*`?' + PFX + HEX + r'`?\s*\|\s*`([A-Za-z_]\w*)(?:\([^`]*\))?`\s*\|(.*)$', l)
        if m:
            a = int(m.group(2), 16)
            if ok(a):
                nm = m.group(3)
                k = kind_of(nm)
                desc = m.group(4).split('|')[0].strip()
                ctx = m.group(1) or l[:m.start(2)]
                add(prog_for(a, ctx, dov), a, k, nm, f'doc: {rel} -- {desc}', f'{rel}:{i}',
                    'doc_table' if k == 'function' else 'doc_data', mt,
                    ambig=prog_for(a, ctx, 'SHINOBI') != prog_for(a, ctx, 'DIRECTOR'))
            continue
        pats = [
            r'`([A-Za-z_]\w*)(?:\([^`]*\))?`\s*(?:\(\s*|=\s*|@\s*|at\s+)?' + PFX + HEX + r'(?![0-9A-Fa-f])(?!\s*/\s*0x)(?!\s*-)',
            r'\*\*([A-Za-z_]\w*_\w+)\s*\(\s*' + PFX + HEX + r'\)\*\*',
            r'^\s*' + PFX + HEX + r'\s+([A-Z][A-Za-z0-9]*_[A-Za-z0-9_]+)\b',
        ]
        for pi, p in enumerate(pats):
            for m in re.finditer(p, l):
                if pi == 2:
                    nm, pf, ah = m.group(3), m.group(1), m.group(2)
                else:
                    nm, pf, ah = m.group(1), m.group(2), m.group(3)
                a = int(ah, 16)
                if not ok(a) or nm in KEYWORDS or DEFAULT_RE.match(nm):
                    continue
                if pi == 0 and l[m.end():m.end() + 3].startswith(('/', ' /')):
                    continue
                k = kind_of(nm)
                ctx = (pf or '') + l[:m.start()]
                add(prog_for(a, ctx, dov), a, k, nm,
                    f'doc: {rel} -- {l.strip()[:200]}', f'{rel}:{i}', 'doc_prose' if k == 'function' else 'doc_data', mt,
                    ambig=prog_for(a, ctx, 'SHINOBI') != prog_for(a, ctx, 'DIRECTOR'))


def style(n):
    """2 = Module_Verb style public name, 1 = other, 0 = generic (hex/number suffixed, abin_xxx)."""
    if re.search(r'_(?:0x)?[0-9A-Fa-f]{4,8}$', n) or n.startswith('abin_'):
        return 0
    if re.match(r'^[A-Z][A-Za-z0-9]*_[A-Za-z0-9_]+$', n):
        return 2
    return 1


def score_name(n):
    s = 0
    if '_' in n: s += 2
    if re.match(r'^[A-Z][a-z]', n): s += 1
    if len(n) <= 3: s -= 5
    return s + min(len(n), 40) / 100.0


def main():
    for fn in sorted(os.listdir(SRC)):
        if fn.endswith(('.c', '.h')):
            mine_c(os.path.join(SRC, fn))
    for fn in ('jump_table_functions.md', 'director.md', 'hud.md', 'sound.md', 'ninjutsu.md', 'ground_truth.md'):
        p = os.path.join(DOCS, fn)
        if os.path.exists(p):
            mine_doc(p)

    # a name seen "called here" at several different addresses is a local wrapper / pointer, not
    # the routine's name
    call_addrs = collections.defaultdict(set)
    for c in cands:
        if c['how'] == 'c_call':
            call_addrs[c['name']].add(c['addr'])
    dropped = [c for c in cands if c['how'] == 'c_call' and len(call_addrs[c['name']]) > 1]
    cands[:] = [c for c in cands if not (c['how'] == 'c_call' and len(call_addrs[c['name']]) > 1)]

    # director.md also describes SHINOBI routines: an overlay address it gives without saying
    # which overlay goes to SHINOBI when the port/docs know that address in SHINOBI
    shinobi_addrs = {c['addr'] for c in cands if c['prog'] == 'SHINOBI'}
    for c in cands:
        if c['ambig'] and c['prog'] == 'DIRECTOR' and (c['addr'] in shinobi_addrs or re.match(SHINOBI_ONLY, c['name'])):
            c['prog'] = 'SHINOBI'

    groups = collections.defaultdict(list)
    for c in cands:
        groups[(c['prog'], c['addr'])].append(c)

    rows, conflicts = [], []
    for (prog, addr), cs in sorted(groups.items()):
        named = [c for c in cs if c['name']]
        fn_c = [c for c in cs if c['kind'] == 'function']
        da_c = [c for c in cs if c['kind'] == 'data']
        if fn_c and da_c:
            kind = 'function' if max(c['prio'] for c in fn_c) >= max(c['prio'] for c in da_c) else 'data'
        else:
            kind = 'function' if fn_c else 'data'
        pool = [c for c in named if c['kind'] == kind]
        if kind == 'data' and len(pool) == 0:
            continue
        agg = {}
        for c in pool:
            key = c['name']
            e = agg.setdefault(key, dict(prio=0, files=set(), mtime=0, first=c))
            e['prio'] = max(e['prio'], c['prio'])
            e['files'].add(c['src'].split(':')[0])
            e['mtime'] = max(e['mtime'], c['mtime'])
            if c['prio'] > e['first']['prio']:
                e['first'] = c
        if agg:
            # best: highest source priority, then most files agreeing, then latest file, then most specific
            if kind == 'function':
                key = lambda kv: (style(kv[0]), kv[1]['prio'], len(kv[1]['files']), kv[1]['mtime'], score_name(kv[0]))
            else:
                key = lambda kv: (kv[1]['prio'], score_name(kv[0]) > 0, len(kv[1]['files']), kv[1]['mtime'], score_name(kv[0]))
            order = sorted(agg.items(), key=key, reverse=True)
            best, e = order[0]
            if kind == 'data' and len(best) <= 3 and len(order) == 1:
                continue
            aliases = [n for n, _ in order[1:] if n.lower() != best.lower()]
            comment = e['first']['comment']
            src = e['first']['src']
            if aliases:
                conflicts.append(dict(program=prog, address=f'0x{addr:08X}', kind=kind, chosen=best,
                                      chosen_src=src, chosen_prio=e['prio'],
                                      others=' | '.join(f"{n} ({','.join(sorted(v['files']))}; p{v['prio']})" for n, v in order[1:]),
                                      top_tie=int(len(order) > 1 and order[1][1]['prio'] == e['prio'] and kind == 'function'
                                                  and style(order[1][0]) == style(best))))
        else:
            best, aliases = '', []
            sub = [c for c in cs if c['kind'] == 'function']
            comment, src = sub[0]['comment'], sub[0]['src']
        rows.append(dict(program=prog, address=f'0x{addr:08X}', kind=kind, name=best, comment=comment,
                         aliases='|'.join(aliases), source=src, _prio=agg[best]['prio'] if agg else 0))

    # the same name chosen for several addresses (file-local statics such as init_12 in both
    # stage_inits.c and stage_hooks.c): qualify with the C file stem, then with the address
    # (A.BIN rows land in both overlay programs; SHINOBI and DIRECTOR never share a program)
    by_name = collections.defaultdict(list)
    for r in rows:
        if r['name']:
            for space in (('SHINOBI', 'DIRECTOR') if r['program'] == 'A.BIN' else (r['program'],)):
                by_name[(space, r['name'])].append(r)
    renamed_dups = []
    for (space, n), rs in by_name.items():
        rs = [r for r in rs if r['name'] == n]
        if len(rs) < 2:
            continue
        if rs[0]['kind'] == 'function' and style(n) == 2:
            # public-style name: the best-attested address keeps it, the others get _<addr>
            rs.sort(key=lambda r: (-r['_prio'], r['address']))
            for r in rs[1:]:
                new = f"{n}_{r['address'][2:]}"
                r['aliases'] = '|'.join([n] + ([r['aliases']] if r['aliases'] else []))
                renamed_dups.append((r['program'], r['address'], n, new))
                r['name'] = new
            continue
        for r in rs:
            stem = os.path.splitext(os.path.basename(r['source'].split(':')[0]))[0]
            q = n if n.lower().startswith(stem.lower()) else f'{stem}_{n}'
            r['_q'] = safe(q)
        qs = collections.Counter(r['_q'] for r in rs)
        for r in rs:
            new = r['_q'] if qs[r['_q']] == 1 else f"{r['_q']}_{r['address'][2:]}"
            r['aliases'] = '|'.join([n] + ([r['aliases']] if r['aliases'] else []))
            renamed_dups.append((r['program'], r['address'], n, new))
            r['name'] = new
            del r['_q']

    for r in rows:
        r.pop('_prio', None)
    with open(OUT, 'w', newline='', encoding='utf-8') as f:
        w = csv.DictWriter(f, fieldnames=['program', 'address', 'kind', 'name', 'comment', 'aliases', 'source'])
        w.writeheader(); w.writerows(rows)
    with open(OUT_CONF, 'w', newline='', encoding='utf-8') as f:
        w = csv.DictWriter(f, fieldnames=['program', 'address', 'kind', 'chosen', 'chosen_src', 'chosen_prio', 'others', 'top_tie'])
        w.writeheader(); w.writerows(conflicts)
    cnt = collections.Counter((r['program'], r['kind'], bool(r['name'])) for r in rows)
    for k in sorted(cnt):
        print(k, cnt[k])
    print('candidates', len(cands), 'rows', len(rows), 'conflicts', len(conflicts),
          'function conflicts', sum(1 for c in conflicts if c['kind'] == 'function'),
          'same-priority function conflicts', sum(c['top_tie'] for c in conflicts))
    print(collections.Counter(c['how'] for c in cands))
    print('dropped ambiguous call-site names', len(dropped), sorted({c['name'] for c in dropped}))
    print('qualified duplicate names', len(renamed_dups))
    for d in renamed_dups:
        print('  dup', *d)


if __name__ == '__main__':
    main()
