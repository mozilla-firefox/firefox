# Token history: which revision changed a value, across all three sheets

A declaration can move between the three generated sheets with nothing in the
authored JSON changing shape, so one sheet's history shows a clean absence that
reads as the realm never having had a value.

```sh
./mach python .claude/skills/desktop-theme-tokens/scripts/token-resolve.py \
  --history --<token> [<range>]
```

It prints, for every revision that changes the token's declarations, the
contexts that changed across all three sheets, marked `+`, `-` or `~` against
the previous revision, with the at-rule chain and any selector narrower than the
root, since the name alone cannot show a layer move.

Revisions print oldest first with their commit time. A block omits the contexts
that did not change, so read the current state with `--name`. A landing, its
backout and its relanding share one subject and often one day; tell them apart
by the time and by which way the lines move. A revision range (`A..B`, or
`--since=<date>`) bounds the sweep, while a lone revision walks its whole
ancestry. The first revision reported is diffed against its own parent, so a
bounded range does not report the range boundary as an addition.

For a token a component's `*.tokens.css` declares, which the sweep reports as
"declared in no sheet", take its history from
`git log -p -G'<token>:' -- <the component's directory>`.
