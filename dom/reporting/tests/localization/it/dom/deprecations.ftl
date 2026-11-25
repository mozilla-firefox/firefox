# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

## Test-only messages, used to detect whether a deprecation report was
## formatted with the app locale or with en-US. They only need to differ from
## the en-US messages.
## Variables:
##   $url (String) - URL of a page with more information.

dom-deprecated-capture-events = [test-locale] dom-deprecated-capture-events { $url }
dom-deprecated-release-events = [test-locale] dom-deprecated-release-events { $url }
dom-deprecated-idb-object-store-moz-get-all = [test-locale] dom-deprecated-idb-object-store-moz-get-all
