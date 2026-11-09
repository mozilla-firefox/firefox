import os

import mozunit
import pytest

LINTER = "ipdl-uriparams"


@pytest.mark.lint_config(name="avoid-ipdl-uriparams")
def test_avoid_ipdl_uriparams(lint, create_temp_file):
    create_temp_file(
        "include URIParams;\n"
        "struct Foo {\n"
        "  URIParams uri;\n"
        "  URIParams? maybeUri;\n"
        "};\n",
        "Types.ipdlh",
    )
    create_temp_file(
        "protocol PFoo {\nparent:\n  async Bar(URIParams aURI);\n};\n",
        "PFoo.ipdl",
    )
    create_temp_file(
        '[RefCounted] using class nsIURI from "nsIURI.h";\n'
        "struct Clean {\n"
        "  nullable nsIURI uri;\n"
        "  SimpleURIParams simple;\n"
        "  URIParamsFoo other;\n"
        "};\n",
        "Clean.ipdlh",
    )
    txt = create_temp_file("URIParams uri;\n", "Other.cpp")

    # Lint the whole temp directory so the `extensions` filter is exercised
    # (explicitly-passed files bypass extension filtering).
    results = lint([os.path.dirname(txt)])

    flagged = sorted((os.path.basename(r.path), r.lineno) for r in results)
    assert flagged == [
        ("PFoo.ipdl", 3),
        ("Types.ipdlh", 1),
        ("Types.ipdlh", 3),
        ("Types.ipdlh", 4),
    ]
    for r in results:
        assert r.level == "error"
        assert "nsIURI" in r.message


if __name__ == "__main__":
    mozunit.main()
