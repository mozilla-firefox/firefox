# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

## Unresponsive script dialog

dom-slow-script-dialog-title = Warning: Unresponsive script
dom-slow-script-dialog-message = A script on this page may be busy, or it may have stopped responding. You can stop the script now, or you can continue to see if the script will complete.

# Variables:
#   $location (String) - URL of the script, followed by a colon and the line number (e.g. “https://example.com/script.js:42”). Long URLs are shortened with an ellipsis in the middle.
dom-slow-script-dialog-location = Script: { $location }

# The & character marks the access key of the checkbox label.
dom-slow-script-dialog-dont-ask-again = &Don’t ask me again

dom-slow-script-dialog-stop-button = Stop script
dom-slow-script-dialog-continue-button = Continue

dom-slow-addon-script-dialog-title = Warning: Unresponsive add-on script

# Variables:
#   $extensionName (String) - Name of the extension running the script.
dom-slow-addon-script-dialog-message =
    A script from the extension “{ $extensionName }” is running on this page, and making { -brand-short-name } unresponsive.

    It may be busy, or it may have stopped responding permanently. You can stop the script now, or you can continue to see if it will complete.

dom-slow-addon-script-dialog-checkbox = Prevent the extension script from running on this page until it next reloads
