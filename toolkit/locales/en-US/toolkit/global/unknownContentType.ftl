# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

unknowncontenttype-handleinternally =
    .label = Open with { -brand-short-name }
    .accesskey = e

unknowncontenttype-settingschange =
    .value =
        { PLATFORM() ->
            [windows] Settings can be changed in { -brand-short-name }’s Options.
           *[other] Settings can be changed in { -brand-short-name }’s Preferences.
        }

unknowncontenttype-intro = You have chosen to open:
unknowncontenttype-which-is = which is:
unknowncontenttype-from = from:
unknowncontenttype-prompt = Would you like to save this file?
unknowncontenttype-action-question = What should { -brand-short-name } do with this file?
unknowncontenttype-open-with =
    .label = Open with
    .accesskey = O
unknowncontenttype-other =
    .label = Other…
unknowncontenttype-choose-handler =
    .label =
        { PLATFORM() ->
            [macos] Choose…
           *[other] Browse…
        }
    .accesskey =
        { PLATFORM() ->
            [macos] C
           *[other] B
        }
unknowncontenttype-save-file =
    .label = Save File
    .accesskey = S
# Label of the accept button, used when saving the file is the only option.
unknowncontenttype-accept-save-file =
    .label = Save File
unknowncontenttype-remember-choice =
    .label = Do this automatically for files like this from now on.
    .accesskey = a
