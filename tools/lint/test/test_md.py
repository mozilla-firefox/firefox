import mozunit

LINTER = "md"
# `fixed` is updated by the mozlint test harness (see conftest.py): when a
# linter returns a dict with a "fixed" key, the harness adds that count to
# this module-level variable. Same pattern as test_ruff_format.py.
fixed = 0


def test_lint_rumdl(lint, paths):
    results = lint(paths("bad.md"))
    assert len(results) >= 1

    rules = {r.rule for r in results}
    assert "MD022" in rules

    md022 = next(r for r in results if r.rule == "MD022")
    assert md022.relpath.endswith("bad.md")
    assert md022.lineno == 1


def test_lint_rumdl_fix(lint, create_temp_file):
    contents = "# foo\n# bar\n"
    path = create_temp_file(contents, "test_fix_rumdl.md")
    lint([path], fix=True)
    assert fixed >= 1


def test_lint_rumdl_no_markdown(lint, tmp_path):
    # rumdl reports a path that contains no .md files on stderr and prints an
    # empty JSON array; the linter should return an empty result.
    (tmp_path / "not_markdown.txt").write_text("hello")
    results = lint([str(tmp_path)])
    assert results == []


if __name__ == "__main__":
    mozunit.main()
