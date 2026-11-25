# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

## Messages reported to the Web Console by DOM code.

dom-get-element-by-id-empty-param = Empty string passed to getElementById().
dom-document-write-ignored = A call to document.write() from an asynchronously-loaded external script was ignored.

# Variables:
#   $url (String) - URL of a page with more information about speculative parsing.
dom-parser-speculation-failed = An unbalanced tree was written using document.write() causing data from the network to be reparsed. More information: { $url }

# Do not translate "EditContext.updateCharacterBounds()" and "characterboundsupdate".
# Variables:
#   $url (String) - URL of the documentation page for EditContext.updateCharacterBounds().
dom-edit-context-character-bounds-warning = EditContext.updateCharacterBounds() was not called synchronously in characterboundsupdate event listener. IME interface may move around unexpectedly for some users. More information: { $url }

# Do not translate "entryTypes".
# Variables:
#   $entryTypes (String) - Comma-separated list of the entry types that were ignored.
dom-performance-observer-unsupported-entry-types = Ignoring unsupported entryTypes: { $entryTypes }.

# Do not translate "entryTypes".
dom-performance-observer-no-valid-entry-types = No valid entryTypes; aborting registration.
