#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Resolve Firefox design tokens from a directory of generated sheets.

  token-resolve.py DIR --name <token>...       per-layer declarations and values
  token-resolve.py DIR --effective <token>...  the winning value in every environment
  token-resolve.py DIR --name|--effective <token>... --at REV
                                               the same, sheets committed at REV
  token-resolve.py DIR --diff DIR2             resolved-value diff, per realm
  token-resolve.py DIR --before REV            the same diff, sheets committed at REV vs DIR
  token-resolve.py --history <token> [<range>] per-revision declarations, every sheet
  token-resolve.py --declared <token> <file>.. which of these sheets declare it

A realm is two sheets: chrome is tokens-shared.css + tokens-platform.css,
in-content is tokens-shared.css + tokens-brand.css. --name and --diff pair them
for you; --history reads all three, since a declaration moves between sheets.
--before reads the three sheets at REV out of git into a temporary directory and
diffs them against DIR's working copies, so the tree is not touched; DIR must
hold freshly generated sheets (`./mach buildtokens`) or the diff is empty.
--at reads the sheets at REV the same way, for --name or --effective.

A value is resolved in the regime its declaration sits in: a reference is
followed to the declaration that wins where this one applies, which is the
last-declared candidate whose media and pref conditions this context also
carries. A foundation-layer value therefore never resolves through a
forced-colors or Nova declaration. Selector specificity is not modelled: a
declaration under a narrower root selector is offered to one under a plain
`:root`, which no token in the tree currently exercises. An important
declaration outranks every normal one; layer order among important
declarations is not modelled either.

An environment is one combination of the media features and prefs the sheets
test (forced colors implying a contrast preference), and --effective resolves
each token as the cascade does there: the highest declaration whose conditions
hold wins, and its references resolve in the same environment. Selectors are
assumed to match, so a lightweight theme, which drops the in-app layer by
selector, paints the `-moz-native-theme` environment, labelled
`-moz-native-theme/lightweight theme` while the in-app layer's wrapper is
the only condition on the native theme (a warning says when it is not). A row
is labelled by what sets it apart from the shipped configuration: `default` has
browser.nova.enabled on and no media feature, a pref that defaults on shows as
`<pref>=false` where it is off (for Nova, the unmaintained pref-off path), and
any other pref by its name where it is on.

--diff and --before print, per realm, a count line and then one line per
declaration that differs, keyed by (context, name). The context is the
declaration's at-rules (layer, media query, -moz-pref()) plus any selector
narrower than :root, as --name prints it; two contexts that print alike get
`in <full context>` appended. CHANGED is a key on both sides whose value,
resolved in the regime the declaration sits in, differs beyond whitespace;
REMOVED is a key only the old side has (DIR for --diff, the sheets at REV for
--before), ADDED one only the new side has. A declaration that moves to another
layer or condition therefore prints as a REMOVED and an ADDED. The EFFECTIVE
lines come last: every token and environment whose winning value changes, so a
removal or a layer move with no EFFECTIVE line is value-neutral. Each change
prints on one line, so a line filter keeps a change with its label.

A resolved value that is a CSS-wide keyword prints `[empty: unset]`: on
`:root` it leaves the property guaranteed-invalid, so a `var(--x)` reader
without a fallback acts as `unset`, which inherits for an inherited property
such as `color` and takes the initial value for one such as `background-color`.
Such a keyword inside a function, as in `light-dark(inherit, #fff)`, prints
`[invalid: unset]`: it makes the whole value invalid in both schemes. A bare
`revert-layer` prints `[revert-layer]`, since the token sheets are layered and
an earlier layer can supply the value; the script does not resolve it.

--history walks the sheets by wildcard pathspec so it follows the directory
moves, which costs about three minutes of `git log` over full history; pass a range
(`--history --some-token HEAD~500..`, or `--since=<date>`) to bound it.
"""

import contextlib
import itertools
import os
import re
import subprocess
import sys
import tempfile

SHEETS = ("tokens-shared.css", "tokens-platform.css", "tokens-brand.css")
REALMS = (("chrome", "tokens-platform.css"), ("in-content", "tokens-brand.css"))
IMPORTANT = re.compile(r"!\s*important\s*$", re.IGNORECASE)
WIDE = re.compile(r"(?<![\w-])(?:inherit|initial|unset|revert-layer|revert)(?![\w-])")


def decls(css):
    """{(enclosing selector/at-rule chain, name): (value, important)}, in file order."""
    out, stack, buf, depth, i, n = {}, [], "", 0, 0, len(css)
    while i < n:
        ch = css[i]
        if css.startswith("/*", i):
            j = css.find("*/", i + 2)
            i = (j + 2) if j != -1 else n
            continue
        if ch in "\"'":
            j = i + 1
            while j < n and css[j] != ch:
                j += 2 if css[j] == "\\" else 1
            buf += css[i : j + 1]
            i = j + 1
            continue
        if ch == "(":
            depth += 1
            buf += ch
        elif ch == ")":
            depth = max(depth - 1, 0)
            buf += ch
        elif ch == "{" and not depth:
            stack.append(re.sub(r"\s+", " ", buf).strip())
            buf = ""
        elif ch in ";}" and not depth:
            m = re.match(r"(--[\w-]+)\s*:\s*(.*)$", buf.strip(), re.DOTALL)
            if m:
                value = re.sub(r"\s+", " ", m.group(2)).strip()
                bang = IMPORTANT.search(value)
                if bang:
                    value = value[: bang.start()].strip()
                out[(" > ".join(stack), m.group(1))] = (value, bool(bang))
            buf = ""
            if ch == "}" and stack:
                stack.pop()
        else:
            buf += ch
        i += 1
    return out


def layer_order(css):
    """Layer names in cascade order, from the sheet's own `@layer a, b, c;`."""
    names = []
    css = re.sub(r"/\*.*?\*/", "", css, flags=re.DOTALL)
    for stmt in re.findall(r"@layer\s+([^{;]+);", css):
        for part in stmt.split(","):
            name = part.strip()
            if name and name not in names:
                names.append(name)
    return names


def conditions(ctx):
    """The media and support conditions a declaration in this context needs."""
    return frozenset(
        p
        for p in ctx.split(" > ")
        if p.startswith(("@media", "@supports", "@container", "@scope"))
    )


def regime(ctx):
    """The conditions that hold where this context applies, implications included.

    Forced colors imply a contrast preference: Gecko reports `prefers-contrast`
    as `no-preference` only when neither the accessibility theme nor a
    document-colors override is in play (`Gecko_MediaFeatures_PrefersContrast`),
    so a forced-colors declaration resolves through prefers-contrast ones. A
    negated query carries no such implication and is left alone.
    """
    conds = set(conditions(ctx))
    for cond in list(conds):
        if "(forced-colors)" in cond and not re.search(r"\bnot\b", cond):
            conds.add(cond.replace("(forced-colors)", "(prefers-contrast)"))
    return frozenset(conds)


def layer_of(ctx):
    layers = [p[7:].strip() for p in ctx.split(" > ") if p.startswith("@layer ")]
    return layers[-1] if layers else None


def rank_of(ctx, order):
    """Cascade rank: unlayered beats every layer, a later layer beats an earlier."""
    layer = layer_of(ctx)
    if layer is None:
        return len(order) + 1
    return order.index(layer) if layer in order else len(order)


FEATURE = re.compile(
    r'-moz-pref\(\s*"[^"]*"[^)]*\)|\(\s*-?[a-z][\w-]*(?:\s*:[^()]*)?\)'
)
FORCED, CONTRAST = "(forced-colors)", "(prefers-contrast)"
NATIVE = "(-moz-native-theme)"
IN_APP = "@media not ((forced-colors) or (-moz-native-theme))"


def boolean_query(cond):
    """The @media query as features joined by not/and/or, or None."""
    if not cond.startswith("@media "):
        return None
    query = cond[len("@media ") :]
    rest = FEATURE.sub(" ", query)
    if re.sub(r"\b(?:not|and|or)\b|[(),\s]", "", rest):
        return None
    return query


def features(cond):
    """What a condition tests: its media features, or itself when not boolean."""
    query = boolean_query(cond)
    return [cond] if query is None else FEATURE.findall(query)


def holds(cond, env):
    query = boolean_query(cond)
    if query is None:
        return cond in env
    expr = FEATURE.sub(lambda m: f" {m.group(0) in env} ", query).replace(",", " or ")
    return eval(expr, {"__builtins__": {}})


# Prefs that -moz-pref() tests and that default to true in the shipped
# configuration (browser/app/profile/firefox.js).
ON_BY_DEFAULT = ("browser.nova.enabled",)


def pref_of(feat):
    if feat.startswith("-moz-pref"):
        return feat[len('-moz-pref("') :].split('"')[0]
    return None


def defaults(feats):
    """The environment of the shipped configuration: the default-on prefs only."""
    return frozenset(f for f in feats if pref_of(f) in ON_BY_DEFAULT)


def environments(feats):
    """Every combination of the features, forced colors implying a contrast
    preference, the shipped configuration first, then by how far each leaves it."""
    out = []
    for bits in itertools.product((False, True), repeat=len(feats)):
        env = frozenset(f for f, on in zip(feats, bits) if on)
        if FORCED in env and CONTRAST in feats and CONTRAST not in env:
            continue
        out.append(env)
    base = defaults(feats)
    return sorted(
        out, key=lambda e: (len(e ^ base), sorted(feats.index(f) for f in e ^ base))
    )


def env_label(env, feats, lightweight=False):
    """What sets env apart from the shipped configuration, or "default"."""
    names = []
    for f in feats:
        pref = pref_of(f)
        if pref in ON_BY_DEFAULT:
            if f not in env:
                names.append(f"{pref}=false")
        elif f not in env or (f == CONTRAST and FORCED in env):
            continue
        elif pref:
            names.append(pref)
        elif f == NATIVE and lightweight:
            names.append("-moz-native-theme/lightweight theme")
        else:
            names.append(f.strip("()"))
    return " + ".join(names) or "default"


class Sheet:
    """The declarations one realm sees, resolved in the regime each sits in."""

    def __init__(self, directory, importer):
        self.decls = {}
        order = []
        for name in ("tokens-shared.css", importer):
            path = os.path.join(directory, name)
            if not os.path.exists(path):
                sys.exit(f"no {name} in {directory}")
            with open(path, encoding="utf-8") as f:
                css = f.read()
            for key, entry in decls(css).items():
                # Reinsert, so a redeclaration takes the later cascade index.
                self.decls.pop(key, None)
                self.decls[key] = entry
            for layer in layer_order(css):
                if layer not in order:
                    order.append(layer)
        self.records = [
            (conditions(ctx), important, rank_of(ctx, order), i, name, val, ctx)
            for i, ((ctx, name), (val, important)) in enumerate(self.decls.items())
        ]
        self.features = []
        for rec in self.records:
            for cond in rec[0]:
                for feat in features(cond):
                    if feat not in self.features:
                        self.features.append(feat)
        # A lightweight theme drops the in-app layer by its selector, so it
        # paints the -moz-native-theme rows as long as that layer's wrapper is
        # the only condition on the native theme.
        stray = sorted({
            cond
            for conds, *_, ctx in self.records
            for cond in conds
            if NATIVE in cond and (cond != IN_APP or ":not([lwtheme])" not in ctx)
        })
        if stray:
            print(
                f"token-resolve: warning: the {NATIVE} rows do not stand for a"
                f" lightweight theme in {importer}: {', '.join(stray)}",
                file=sys.stderr,
            )
        self.lightweight = not stray
        self._lookups, self._effective = {}, {}

    def effective(self, env):
        """{name: (value, winning context)} as the cascade resolves it in env."""
        if env not in self._effective:
            best = {}
            for conds, important, rank, i, name, val, ctx in self.records:
                key = (important, rank, i)
                if all(holds(c, env) for c in conds) and (
                    name not in best or best[name][0] < key
                ):
                    best[name] = (key, val, ctx)
            lookup = {n: v for n, (_k, v, _c) in best.items()}
            self._effective[env] = {
                n: (resolve(v, lookup) + (" !important" if k[0] else ""), c)
                for n, (k, v, c) in best.items()
            }
        return self._effective[env]

    def lookup(self, ctx):
        """{name: value} as it resolves for a declaration in this context."""
        conds = regime(ctx)
        if conds not in self._lookups:
            best = {}
            for cand, important, rank, i, name, val, _ctx in self.records:
                key = (important, rank, i)
                if cand <= conds and best.get(name, ((False, -1, -1),))[0] < key:
                    best[name] = (key, val)
            self._lookups[conds] = {n: v for n, (_k, v) in best.items()}
        return self._lookups[conds]

    def resolved(self):
        """{(context label, name): value}, so a pure re-nesting is not a diff."""
        sharing = {}
        for ctx, name in self.decls:
            sharing.setdefault((label(ctx), name), set()).add(ctx)
        out = {}
        for (ctx, name), (val, important) in self.decls.items():
            key = (label(ctx), name)
            if len(sharing[key]) > 1:
                key = (f"{label(ctx)} in {ctx}", name)
            value = resolve(val, self.lookup(ctx))
            out[key] = f"{value} !important" if important else value
        return out

    def value(self, ctx, val):
        return resolve(val, self.lookup(ctx))


def resolve(val, lookup, seen=()):
    """Substitute every var() reference, following fallbacks.

    A reference that cycles back to a name already being resolved prints as
    `<cycle --name>`, since CSS makes such a property invalid rather than
    giving it the partly-substituted value. A chain deeper than 32 references
    prints as `<depth limit ...>`.
    """
    if len(seen) > 32:
        return f"<depth limit {val}>"
    out, i = [], 0
    while True:
        j = val.find("var(", i)
        if j == -1:
            out.append(val[i:])
            return "".join(out)
        out.append(val[i:j])
        k, level = j + 4, 1
        while k < len(val) and level:
            level += {"(": 1, ")": -1}.get(val[k], 0)
            k += 1
        name, _, fallback = val[j + 4 : k - 1].partition(",")
        name, fallback = name.strip(), fallback.strip()
        if name in seen:
            out.append(f"<cycle {name}>")
        elif name in lookup:
            out.append(resolve(lookup[name], lookup, seen + (name,)))
        elif fallback:
            out.append(resolve(fallback, lookup, seen))
        else:
            out.append(val[j:k])
        i = k


def label(ctx):
    """The context as at-rules plus any selector narrower than the root."""
    parts = [
        p.replace("@layer ", "").replace("@media ", "")
        for p in ctx.split(" > ")
        if p.startswith("@")
    ]
    selector = [p for p in ctx.split(" > ") if not p.startswith("@")]
    if selector:
        narrow = [
            s.strip()
            for s in selector[-1].split(",")
            if s.strip() not in (":root",) and not s.strip().startswith(":host(")
        ]
        parts += [", ".join(narrow)] if narrow else []
    return " > ".join(parts) or "(root)"


def git(*args):
    proc = subprocess.run(
        ("git",) + args, capture_output=True, text=True, encoding="utf-8", check=False
    )
    if proc.returncode:
        sys.exit(proc.stderr.strip() or f"git {args[0]} failed")
    return proc.stdout


def revisions(rev_range):
    """[(sha, "<commit date and time> <subject>")] oldest first, plus every path the sheets have lived at."""
    globs = [f":(top,glob)**/{s}" for s in SHEETS]
    out = git(
        "log",
        "--reverse",
        "--date=format:%Y-%m-%d %H:%M",
        "--format=%x00%H %cd %s",
        "--name-only",
        *rev_range,
        "--",
        *globs,
    )
    revs, paths = [], set()
    for record in out.split("\x00"):
        if not record.strip():
            continue
        head, _, rest = record.partition("\n")
        sha, _, subject = head.partition(" ")
        revs.append((sha, subject))
        paths.update(p for p in rest.split("\n") if p.strip().endswith(SHEETS))
    if revs:
        # A sheet no revision in the range touches still holds declarations.
        listing = git(
            "ls-files", "--full-name", f"--with-tree={revs[-1][0]}", "--", *globs
        )
        paths.update(p for p in listing.split("\n") if p.strip().endswith(SHEETS))
    return revs, sorted(paths)


def read_blobs(specs):
    """{'<rev>:<path>': (blob id, text)} for the specs that exist."""
    heads = subprocess.run(
        ["git", "cat-file", "--batch-check"],
        input="\n".join(specs),
        capture_output=True,
        text=True,
        encoding="utf-8",
        check=True,
    ).stdout
    oids = {}
    for spec, line in zip(specs, heads.splitlines(), strict=True):
        parts = line.split()
        if len(parts) == 3 and parts[1] == "blob":
            oids[spec] = parts[0]
    want = sorted(set(oids.values()))
    if not want:
        return {}
    raw = subprocess.run(
        ["git", "cat-file", "--batch"],
        input="\n".join(want).encode(),
        capture_output=True,
        check=True,
    ).stdout
    bodies, i = {}, 0
    for oid in want:
        j = raw.index(b"\n", i)
        size = int(raw[i:j].split()[2])
        bodies[oid] = raw[j + 1 : j + 1 + size].decode("utf-8", "replace")
        i = j + 1 + size + 1
    return {spec: (oid, bodies[oid]) for spec, oid in oids.items()}


def history(name, rev_range):
    revs, paths = revisions(rev_range)
    if not revs:
        sys.exit("no revision touches a token sheet in that range")
    base = revs[0][0] + "^"
    blobs = read_blobs([
        f"{r}:{p}" for r in [base] + [sha for sha, _ in revs] for p in paths
    ])
    cache = {}

    def state_at(rev):
        out = {}
        for path in paths:
            found = blobs.get(f"{rev}:{path}")
            if found is None or name not in found[1]:
                continue
            oid, css = found
            if oid not in cache:
                cache[oid] = {
                    c: (f"{v} !important" if imp else v)
                    for (c, n), (v, imp) in decls(css).items()
                    if n == name
                }
            for ctx, val in cache[oid].items():
                out[(os.path.basename(path), label(ctx))] = val
        return out

    prev = state_at(base)
    reported, declared = False, dict(prev)
    for sha, subject in revs:
        state = state_at(sha)
        declared.update(state)
        if state == prev:
            continue
        reported = True
        print(f"{sha[:12]} {subject}")
        for key in sorted(set(prev) | set(state)):
            sheet, ctx = key
            if key not in state:
                print(f"  - {sheet} [{ctx}] = {prev[key]}")
            elif key not in prev:
                print(f"  + {sheet} [{ctx}] = {state[key]}")
            elif prev[key] != state[key]:
                print(f"  ~ {sheet} [{ctx}] = {prev[key]} -> {state[key]}")
        prev = state
    if reported:
        return
    where = "in that range" if rev_range else "in this history"
    if not declared:
        sys.exit(f"{name} is declared in no sheet {where}")
    print(f"{name} does not change {where}. It is declared as:")
    for (sheet, ctx), val in sorted(declared.items()):
        print(f"    {sheet} [{ctx}] = {val}")


def diff(dir1, dir2):
    for realm, importer in REALMS:
        print(f"######## {realm} (tokens-shared.css + {importer})")
        sa, sb = Sheet(dir1, importer), Sheet(dir2, importer)
        a, b = sa.resolved(), sb.resolved()
        changed = [
            (k, a[k], b[k])
            for k in sorted(set(a) & set(b))
            if squash(a[k]) != squash(b[k])
        ]
        removed, added = sorted(set(a) - set(b)), sorted(set(b) - set(a))
        effective = effective_changes(sa, sb)
        print(
            f"changed {len(changed)}  removed {len(removed)}  added {len(added)}"
            f"  effective {len(effective)}"
        )
        for (ctx, name), before, after in changed:
            print(f"  CHANGED   {name} [{ctx}] = {marked(before)} -> {marked(after)}")
        for ctx, name in removed:
            print(f"  REMOVED   {name} [{ctx}] = {marked(a[(ctx, name)])}")
        for ctx, name in added:
            print(f"  ADDED     {name} [{ctx}] = {marked(b[(ctx, name)])}")
        for name, envs, before, after in effective:
            print(f"  EFFECTIVE {name} [{envs}] = {marked(before)} -> {marked(after)}")


def mark(value):
    """A flag for a CSS-wide keyword that leaves the value's var() readers unset."""
    bare = IMPORTANT.sub("", value).strip()
    if bare == "revert-layer":
        return "  [revert-layer]"
    if WIDE.fullmatch(bare):
        return "  [empty: unset]"
    for m in WIDE.finditer(bare):
        if bare.count("(", 0, m.start()) > bare.count(")", 0, m.start()):
            return "  [invalid: unset]"
    return ""


def marked(value):
    return value + mark(value)


def squash(value):
    return re.sub(r"\s+", " ", value)


def effective_changes(sa, sb):
    """[(name, environments, before, after)] for each winning value that moves."""
    feats = sa.features + [f for f in sb.features if f not in sa.features]
    envs = environments(feats)
    groups = {}
    for env in envs:
        ea, eb = sa.effective(env), sb.effective(env)
        for name in set(ea) | set(eb):
            before = ea[name][0] if name in ea else "(undeclared)"
            after = eb[name][0] if name in eb else "(undeclared)"
            if squash(before) != squash(after):
                groups.setdefault((name, before, after), []).append(env)
    out = []
    for (name, before, after), hit in groups.items():
        where = (
            "every environment"
            if len(hit) == len(envs)
            else ", ".join(
                env_label(e, feats, sa.lightweight and sb.lightweight) for e in hit
            )
        )
        out.append((name, where, before, after))
    return sorted(out)


def per_environment(directory, names):
    missing = set(names)
    for realm, importer in REALMS:
        print(f"######## {realm} (tokens-shared.css + {importer})")
        sheet = Sheet(directory, importer)
        envs = environments(sheet.features)
        width = max(len(env_label(e, sheet.features, sheet.lightweight)) for e in envs)
        if NATIVE in sheet.features:
            print(
                "  (no row is labelled in-app: every row without -moz-native-theme is the in-app theme; `default` is the shipped configuration, and a row names a pref only where it leaves the pref's default; a Linux user on the default theme gets the -moz-native-theme row, a Windows or macOS user the `default` row; the bracket names the layer of the winning declaration, so a token the in-app layer does not redefine shows its foundation layer in the in-app rows)"
            )
        for name in names:
            print(" ", name)
            for env in envs:
                found = sheet.effective(env).get(name)
                where = env_label(env, sheet.features, sheet.lightweight).ljust(width)
                if found is None:
                    print(f"    {where}  (undeclared)")
                    continue
                missing.discard(name)
                value, ctx = found
                print(f"    {where}  {marked(value)}   [{layer_of(ctx) or label(ctx)}]")
    if missing:
        sys.exit(f"declared in neither realm: {', '.join(sorted(missing))}")


@contextlib.contextmanager
def sheets_at(directory, rev):
    """A temporary directory holding the sheets committed at rev."""
    prefix = git("-C", directory, "rev-parse", "--show-prefix").strip()
    with tempfile.TemporaryDirectory() as tmp:
        for sheet in SHEETS:
            css = git("-C", directory, "show", f"{rev}:{prefix}{sheet}")
            with open(os.path.join(tmp, sheet), "w", encoding="utf-8") as f:
                f.write(css)
        yield tmp


def before(directory, rev):
    """Diff the sheets committed at rev against the working copies in directory."""
    with sheets_at(directory, rev) as tmp:
        diff(tmp, directory)


def per_layer(directory, names):
    missing = set(names)
    for realm, importer in REALMS:
        print(f"######## {realm} (tokens-shared.css + {importer})")
        sheet = Sheet(directory, importer)
        for name in names:
            print(" ", name)
            found = [
                (c, v, imp) for (c, n), (v, imp) in sheet.decls.items() if n == name
            ]
            for ctx, val, important in found:
                resolved = sheet.value(ctx, val)
                shown = f"{val} !important" if important else val
                suffix = "" if resolved == val else f" => {resolved}"
                print(f"    [{label(ctx)}] {shown}{suffix}{mark(resolved)}")
            if found:
                missing.discard(name)
            else:
                print("    (not declared in this realm)")
    if missing:
        sys.exit(f"declared in neither realm: {', '.join(sorted(missing))}")


def declared(name, paths):
    """Which of these sheets declare a token at all.

    When a Nova override disappears the question is not whether its own sheet
    still defines it, but whether the ungated foundation layer carries the same
    value -- so compare the sheets rather than reading one."""
    for path in paths:
        if not os.path.exists(path):
            print(f"--   {path} (no such file)")
            continue
        with open(path, encoding="utf-8") as f:
            names = {n for _, n in decls(f.read())}
        print(f"{'yes' if name in names else 'no ':<4} {path}")


def usage(message):
    sys.exit(f"{message}\n{__doc__}")


def main(argv):
    sys.stdout.reconfigure(encoding="utf-8")
    if len(argv) > 1 and argv[1] in ("-h", "--help"):
        print(__doc__.strip())
        return
    if len(argv) < 3:
        usage("too few arguments")
    if argv[1] == "--declared":
        if not argv[2].startswith("--"):
            usage(f"not a token name (token names start with --): {argv[2]}")
        if len(argv) < 4:
            usage("--declared takes a token and at least one file")
        declared(argv[2], argv[3:])
        return
    if argv[1] == "--history":
        rest = argv[2:]
        names = [a for a in rest if a.startswith("--") and "=" not in a]
        if len(names) != 1:
            usage("--history takes one token (token names start with --) and a range")
        rest.remove(names[0])
        history(names[0], rest)
        return
    directory, mode, rest = argv[1], argv[2], argv[3:]
    if directory.startswith("--"):
        usage(f"the directory comes first, then {directory}")
    if not os.path.isdir(directory):
        usage(f"not a directory: {directory}")
    if mode in ("--history", "--declared"):
        usage(f"{mode} takes no directory; put it first: token-resolve.py {mode} ...")
    if mode == "--diff":
        if len(rest) != 1:
            usage("--diff takes one directory")
        if not os.path.isdir(rest[0]):
            usage(f"not a directory: {rest[0]}")
        diff(directory, rest[0])
    elif mode == "--before":
        if len(rest) != 1:
            usage("--before takes one revision")
        before(directory, rest[0])
    elif mode in ("--name", "--effective"):
        rev = None
        if "--at" in rest:
            i = rest.index("--at")
            if i + 1 == len(rest):
                usage("--at takes a revision")
            rev = rest[i + 1]
            del rest[i : i + 2]
        if not rest:
            usage(f"{mode} takes at least one token")
        for name in rest:
            if not name.startswith("--"):
                usage(f"not a token name (token names start with --): {name}")
        run = per_layer if mode == "--name" else per_environment
        if rev:
            with sheets_at(directory, rev) as tmp:
                run(tmp, rest)
        else:
            run(directory, rest)
    else:
        usage(f"unknown mode: {mode}")


if __name__ == "__main__":
    main(sys.argv)
