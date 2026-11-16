# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
import mozunit

LINTER = "fluent-lint"


def assert_results(results, expected):
    """Compare lint results with a list of (rule, lineno, column) tuples."""
    assert [(r.rule, r.lineno, r.column) for r in results] == expected


def test_lint_exclusions(lint, paths):
    assert_results(lint(paths("excluded.ftl")), [("TE01", 6, 20)])


def test_lint_single_file(lint, paths):
    results = lint(paths("bad.ftl"))
    assert_results(
        results,
        [
            ("ID01", 1, 1),
            ("ID01", 3, 1),
            ("TE01", 5, 20),
            ("TE01", 6, 24),
            ("TE02", 7, 20),
            ("TE03", 8, 20),
            ("TE04", 11, 20),
            ("TE05", 13, 16),
            ("TE03", 17, 20),
            ("TE03", 25, 18),
            ("ID02", 32, 1),
            ("VC01", 34, 1),
            ("VC01", 44, 1),
        ],
    )
    assert "$tabCount" in results[11].message
    assert "$url" in results[12].message


def test_comment_group(lint, paths):
    assert_results(
        lint(paths("comment-group1.ftl")),
        [
            ("GC03", 12, 1),
            ("GC02", 16, 1),
            ("GC04", 21, 1),
            ("GC03", 26, 1),
            ("GC02", 30, 1),
            ("GC01", 35, 1),
        ],
    )
    assert_results(lint(paths("comment-group2.ftl")), [])


def test_comment_resource(lint, paths):
    assert_results(lint(paths("comment-resource1.ftl")), [("RC01", 9, 1)])
    assert_results(lint(paths("comment-resource2.ftl")), [("RC03", 4, 1)])
    assert_results(lint(paths("comment-resource3.ftl")), [("RC02", 5, 1)])
    assert_results(lint(paths("comment-resource4.ftl")), [("RC03", 6, 1)])
    assert_results(lint(paths("comment-resource5.ftl")), [("RC02", 5, 1)])
    assert_results(lint(paths("comment-resource6.ftl")), [])


def test_brand_names(lint, paths):
    results = lint(paths("brand-names.ftl"), {"brand-files": ["test-brands.ftl"]})
    assert_results(
        results,
        [
            ("CO01", 1, 16),
            ("CO01", 4, 16),
            ("CO01", 5, 17),
            ("CO01", 6, 16),
            ("CO01", 9, 16),
            ("CO01", 10, 16),
            ("CO01", 11, 17),
            ("CO01", 12, 16),
            ("CO01", 14, 20),
            ("CO01", 15, 20),
            ("CO01", 16, 20),
            ("CO01", 32, 21),
            ("CO01", 33, 21),
        ],
    )
    assert "Firefox" in results[0].message
    assert "Mozilla" not in results[0].message
    assert "Thunderbird" not in results[0].message
    assert "Smart Window)" in results[11].message
    assert "Smart Windows" not in results[11].message
    assert "Smart Windows" in results[12].message
    assert "Smart Window," not in results[12].message

    assert_results(lint(paths("brand-names-excluded.ftl")), [])


def test_comment_variables(lint, paths):
    results = lint(paths("comment-variables1.ftl"))
    assert_results(
        results,
        [
            ("VC01", 8, 1),
            ("VC01", 19, 1),
            ("VC01", 25, 1),
            ("VC01", 48, 1),
        ],
    )
    assert "$var" in results[0].message
    assert "$select1" in results[1].message
    assert "$select2" in results[2].message
    assert "$attr" in results[3].message

    results = lint(paths("comment-variables2.ftl"))
    assert_results(results, [("VC01", 27, 1)])
    assert "$term-message" in results[0].message


def test_valid_attributes(lint, paths):
    results = lint(paths("valid-attributes.ftl"))
    assert_results(results, [("VA01", 16, 5)])
    assert ".extralabel" in results[0].message


def test_quotes_around_placeables(lint, paths):
    assert_results(
        lint(paths("quotes-placeables.ftl")),
        [
            ("TE04", 2, 29),
            ("TE03", 4, 29),
            ("TE04", 5, 25),
            ("TE03", 6, 28),
            ("TE01", 7, 29),
            ("TE04", 9, 14),
            ("TE04", 11, 27),
            ("TE04", 17, 11),
            ("TE04", 20, 25),
            ("TE04", 22, 1),
        ],
    )


def test_typography_exclusions(lint, paths):
    assert_results(
        lint(paths("typography-excluded.ftl")),
        [
            ("TE03", 1, 33),
            ("TE05", 4, 29),
        ],
    )
    assert_results(
        lint(paths("typography-excluded-messages.ftl")),
        [
            ("TE03", 2, 38),
            ("TE04", 3, 33),
            ("TE04", 4, 26),
        ],
    )


if __name__ == "__main__":
    mozunit.main()
