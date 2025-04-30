# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

## XSLT and XPath specific errors.

xslt-parse-failure = Parsing an XSLT stylesheet failed.
xpath-parse-failure = Parsing an XPath expression failed.
xslt-execution-failure = XSLT transformation failed.
xpath-unknown-function = Invalid XSLT/XPath function.
xslt-bad-recursion = XSLT Stylesheet (possibly) contains a recursion.
xslt-bad-value = Attribute value illegal in XSLT 1.0.
xslt-nodeset-expected = An XPath expression was expected to return a NodeSet.
xslt-aborted = XSLT transformation was terminated by <xsl:message>.
xslt-network-error = A network error occurred loading an XSLT stylesheet:
xslt-wrong-mime-type = An XSLT stylesheet does not have an XML mimetype:
xslt-load-recursion = An XSLT stylesheet directly or indirectly imports or includes itself:
xpath-bad-argument-count = An XPath function was called with the wrong number of arguments.
xpath-bad-extension-function = An unknown XPath extension function was called.
xpath-paren-expected = XPath parse failure: ‘)’ expected:
xpath-invalid-axis = XPath parse failure: invalid axis:
xpath-no-node-type-test = XPath parse failure: Name or Nodetype test expected:
xpath-bracket-expected = XPath parse failure: ‘]’ expected:
xpath-invalid-var-name = XPath parse failure: invalid variable name:
xpath-unexpected-end = XPath parse failure: unexpected end of expression:
xpath-operator-expected = XPath parse failure: operator expected:
xpath-unclosed-literal = XPath parse failure: unclosed literal:
xpath-bad-colon = XPath parse failure: ‘:’ unexpected:
xpath-bad-bang = XPath parse failure: ‘!’ unexpected, negation is not():
xpath-illegal-char = XPath parse failure: illegal character found:
xpath-binary-expected = XPath parse failure: binary operator expected:
xslt-load-blocked-error = An XSLT stylesheet load was blocked for security reasons.
xpath-invalid-expression-evaluated = Evaluating an invalid expression.
xpath-unbalanced-curly-brace = Unbalanced curly brace.
xslt-bad-node-name = Creating an element with an invalid QName.
xslt-var-already-set = Variable binding shadows variable binding within the same template.
xslt-call-to-key-not-allowed = Call to the key function not allowed.

# Other failures, not found in the previous ones.
# Variables:
#   $errorCode (String) - The error code (formatted in hexadecimal)
xslt-unknown-error = An unknown error has occurred ({ $errorCode })

## Messages for the XML error page.
##
## Variables:
##   $error (string) - the specific XSLT or XPath error (a translated string
##   from the previous section)

xslt-loading-error = Error loading stylesheet: { $error }
xslt-transform-error = Error during XSLT transformation: { $error }
