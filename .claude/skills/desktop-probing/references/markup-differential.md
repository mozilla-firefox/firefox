# Running a markup differential

For a change with no inline toggle (`SKILL.md` §4), the sweep runs once per
side of the change.

Restoring the touched files to their pre-change contents with the VCS puts the
old CSS on the live paths (`SKILL.md` §6 has the commands), but not always the
old markup: `browser.xhtml` and the `.inc.xhtml` files it includes are
preprocessed copies (§2 "What is live at the next launch" in
[`build-freshness.md`](build-freshness.md)).

1. Restore the files, run `./mach build faster` when a touched file is
   preprocessed or the host copies jar.mn entries (its platform file, section
   PATHS), and restart, with `MOZ_PURGE_CACHES=1`
   on the MCP path so the startup cache does not serve the other side's scripts
   or markup.
2. Run the sweep and save the table.
3. Put the new contents back, rebuild and restart the same way, run it again.
4. Diff the two tables cell by cell.

Two things turn the diff into noise:

- The sweep has to hold the window at one CSS size per cell
  (`desktop-theme-layout`'s environment sweep).
- Never swap the files while a mochitest is running against the same tree.
