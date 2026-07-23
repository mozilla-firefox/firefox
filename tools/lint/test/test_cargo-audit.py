import mozunit

LINTER = "cargo-audit"

# The cargo-audit linter runs against the *live* RustSec advisory database, so
# the set of advisories matching the checked-in *.lock fixtures changes over
# time as new advisories are published (or the database format evolves). To
# avoid spurious "intermittent" failures we only assert that a known set of
# advisories is reported, instead of an exact count. A failure here is almost
# never a regression in this linter with a backout-able cause; it usually means
# the advisory database changed.
FILE_A_BUG = (
    "\n\nThis test runs against the live RustSec advisory database, which "
    "changes over time. This failure is most likely caused by the database "
    "changing rather than a regression in the cargo-audit linter. Please file "
    "a new bug blocking bug 1747536 (which added this test) instead of backing "
    "out a push."
)


def is_error(r):
    return r.level == "error"


def find_missing_vulnerabilities(expected, results):
    """Return the subset of ``expected`` advisories not present in ``results``.

    Args:
        expected: A list of ``(identifier, level)`` tuples that must be present.
        results: The lint results returned by the cargo-audit linter.

    Returns:
        The list of expected advisories that were not found in the results.
    """
    remaining = list(expected)
    for result in results:
        for index, (identifier, level) in enumerate(remaining):
            if level == result.level and identifier in result.message:
                del remaining[index]
                break
    return remaining


def test_lint_cargo_audit_errors(lint, paths):
    error = "error"
    warning = "warning"

    test_file = "error.lock"
    results = lint(paths(test_file))

    # Only assert that the known advisories are present. Extra results are
    # expected as new advisories get published against the fixture's crates, so
    # we deliberately do not assert an exact count here.
    expected_vulnerabilities = [
        ("RUSTSEC-2019-0014", error),
        ("RUSTSEC-2020-0144", warning),
        ("RUSTSEC-2020-0073", warning),
        ("RUSTSEC-2022-0004", error),
        ("yanked version of libc", warning),  # This one lacks an ID
    ]
    missing = find_missing_vulnerabilities(expected_vulnerabilities, results)
    assert missing == [], (
        f"Expected advisories were not reported for {test_file}: {missing}{FILE_A_BUG}"
    )

    for result in results:
        assert result.relpath == test_file


def test_lint_cargo_audit_clean(lint, paths):
    test_file = "clean.lock"
    results = lint(paths(test_file))
    assert not results, (
        f"Expected no advisories for {test_file}, but got "
        f"{len(results)}: {[r.message.splitlines()[0] for r in results]}"
        f"{FILE_A_BUG}"
    )


if __name__ == "__main__":
    mozunit.main()
