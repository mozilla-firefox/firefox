# In-tree agent skills

This directory holds agent skills that ship as part of the tree. They are
available to every developer who works in the tree, and an AI agent may load any
of them automatically when a task looks relevant.

## What belongs here

In-tree skills are for **broadly useful, tree-wide workflows** — the kind of thing
that helps Firefox developers regardless of which team or component they work on.
Good candidates are general utilities such as filing a bug, splitting commits,
running a linter, or reorganizing patches for review.

Because these skills live in the tree, they carry an always-on cost: a skill's
`description` is loaded into the agent's context on every session so the agent can
decide when to invoke it. A description that is too broad can trigger the skill
where it shouldn't. Keep this directory reserved for skills that are genuinely
worth that cost for the whole project.

## What does not belong here

Skills that are useful but not for everyone — team-specific or component-specific
workflows — should be published to a marketplace instead of being added in-tree,
so developers can browse and enable only what they need:

- **Useful for Firefox developers, but not everyone:**
  [firefox-aidev-plugins](https://github.com/mozilla/firefox-aidev-plugins). This
  marketplace is enabled by default in the Firefox tree, so developers just run
  `/plugin` in Claude Code to browse and enable plugins — no extra setup needed.
- **Not Firefox-specific, useful for any Mozilla developer:**
  [aidev-plugins](https://github.com/mozilla/aidev-plugins), the public
  marketplace for general-purpose Mozilla plugins.

When in doubt, prefer a marketplace plugin: it reaches the people who need it
without adding context cost for everyone else.

## Reducing a skill's listing cost instead

A skill's body costs nothing until it is invoked; only the `description` is
always-on. So a family whose subject is component-specific but whose audience is
large can still earn a place here by keeping that cost down.

Two levers bring it down. The first is the description itself: most of the waste
is framing that restates the trigger words it already lists, and cutting it
leaves the triggers intact. The second is `skillOverrides` in
`.claude/settings.json`, which sets a skill's listing mode without editing its
`SKILL.md`:

```json
{
  "skillOverrides": {
    "some-skill": "name-only"
  }
}
```

`name-only` lists the name without the description, so the skill no longer
auto-triggers but stays invocable by name and in the `/` menu. It suits a skill
that is only ever reached *from* another skill — a family's shared mechanics, or
the reference half of a pair — because the referring skill's body names it and no
trigger text is needed. Confirm a sibling actually refers to it before relying on
this. `desktop-theme-probing` is the worked example.

## Adding a skill

Each skill lives in its own subdirectory and is defined by a `SKILL.md` file with
YAML frontmatter (at minimum a `name` and a `description`). Any supporting scripts
or assets the skill needs can live alongside it in the same subdirectory. See the
existing skills in this directory for examples.

`.claude/skills/` and `.agents/skills/` mirror each other and must be
byte-identical; the `agent-skills-sync` linter enforces it. It compares every
tracked file byte for byte, scripts and assets included. Run
`./mach lint -l agent-skills-sync .claude/skills` from the checkout root before
submitting, or add `--fix` to propagate whichever side changed. Any path inside
either directory runs the comparison over both whole mirrors. Mozlint skips the
linter for a path outside them, and with no paths it lints only outgoing and
working-copy changes.

The linter reads tracked files only, so an untracked file on one side is invisible
to it. `diff -rq -x __pycache__ .claude/skills .agents/skills` is the backstop; it
excludes `__pycache__` because importing a bundled script, as a test does, writes
one beside it on whichever side it was imported from.
