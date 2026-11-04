# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

## Variables:
##   $downloadsCount (Number): The current downloads count.

download-ui-confirm-cancel-button = Cancel

download-ui-confirm-quit-title =
    { PLATFORM() ->
        [windows]
            { $downloadsCount ->
                [1] Stop 1 download and exit { -brand-short-name }?
               *[other] Stop { $downloadsCount } downloads and exit { -brand-short-name }?
            }
       *[other]
            { $downloadsCount ->
                [1] Stop 1 download and quit { -brand-short-name }?
               *[other] Stop { $downloadsCount } downloads and quit { -brand-short-name }?
            }
    }
download-ui-confirm-quit-message = { -brand-short-name } will try to resume downloads when it reopens, but progress may be lost.
download-ui-confirm-quit-button =
    { PLATFORM() ->
        [windows] Exit { -brand-short-name }
       *[other] Quit { -brand-short-name }
    }

download-ui-confirm-offline-title =
    { $downloadsCount ->
        [1] Stop 1 download and go offline?
       *[other] Stop { $downloadsCount } downloads and go offline?
    }
download-ui-confirm-offline-message = { -brand-short-name } will try to resume downloads when you’re back online, but progress may be lost.
download-ui-confirm-offline-button = Go offline

download-ui-confirm-private-browsing-title =
    { $downloadsCount ->
        [1] Stop 1 download and close this Private Window?
       *[other] Stop { $downloadsCount } downloads and close this Private Window?
    }
download-ui-confirm-private-browsing-message =
    { $downloadsCount ->
        [1] You won’t be able to resume this download.
       *[other] You won’t be able to resume these downloads.
    }
download-ui-confirm-private-browsing-button = Close window

##

download-ui-file-executable-security-warning-title = Open Executable File?
# Variables:
#   $executable (String): The executable file to be opened.
download-ui-file-executable-security-warning = “{ $executable }” is an executable file. Executable files may contain viruses or other malicious code that could harm your computer. Use caution when opening this file. Are you sure you want to launch “{ $executable }”?
