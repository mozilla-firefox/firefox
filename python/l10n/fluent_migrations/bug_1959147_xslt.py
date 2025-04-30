# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

import fluent.syntax.ast as FTL
from fluent.migrate.helpers import VARIABLE_REFERENCE
from fluent.migrate.transforms import COPY, REPLACE


def migrate(ctx):
    """Bug 1959147 - Migrate XSLT errors to Fluent, part {index}."""

    xslt_source = "dom/chrome/xslt/xslt.properties"
    global_strres_source = "dom/chrome/global-strres.properties"
    target = "dom/dom/xslt.ftl"

    xslt_errors = {
        "xslt-parse-failure": "1",
        "xpath-parse-failure": "2",
        # 3 (NS_ERROR_XSLT_ALREADY_SET) is empty in xslt.properties.
        # It seems it is always replaced with NS_ERROR_XSLT_VAR_ALREADY_SET.
        "xslt-execution-failure": "4",
        "xpath-unknown-function": "5",
        "xslt-bad-recursion": "6",
        "xslt-bad-value": "7",
        "xslt-nodeset-expected": "8",
        "xslt-aborted": "9",
        "xslt-network-error": "10",
        "xslt-wrong-mime-type": "11",
        "xslt-load-recursion": "12",
        "xpath-bad-argument-count": "13",
        "xpath-bad-extension-function": "14",
        "xpath-paren-expected": "15",
        "xpath-invalid-axis": "16",
        "xpath-no-node-type-test": "17",
        "xpath-bracket-expected": "18",
        "xpath-invalid-var-name": "19",
        "xpath-unexpected-end": "20",
        "xpath-operator-expected": "21",
        "xpath-unclosed-literal": "22",
        "xpath-bad-colon": "23",
        "xpath-bad-bang": "24",
        "xpath-illegal-char": "25",
        "xpath-binary-expected": "26",
        "xslt-load-blocked-error": "27",
        "xpath-invalid-expression-evaluated": "28",
        "xpath-unbalanced-curly-brace": "29",
        "xslt-bad-node-name": "30",
        "xslt-var-already-set": "31",
        "xslt-call-to-key-not-allowed": "32",
    }
    ctx.add_transforms(
        target,
        target,
        [
            FTL.Message(id=FTL.Identifier(ftl_id), value=COPY(xslt_source, prop_id))
            for ftl_id, prop_id in xslt_errors.items()
        ],
    )

    ctx.add_transforms(
        target,
        target,
        [
            FTL.Message(
                id=FTL.Identifier("xslt-unknown-error"),
                value=REPLACE(
                    global_strres_source,
                    "16389",
                    {"%1$S": VARIABLE_REFERENCE("errorCode")},
                ),
            ),
            FTL.Message(
                id=FTL.Identifier("xslt-loading-error"),
                value=REPLACE(
                    xslt_source, "LoadingError", {"%1$S": VARIABLE_REFERENCE("error")}
                ),
            ),
            FTL.Message(
                id=FTL.Identifier("xslt-transform-error"),
                value=REPLACE(
                    xslt_source, "TransformError", {"%1$S": VARIABLE_REFERENCE("error")}
                ),
            ),
        ],
    )
