# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

containers-window-new3 =
    .title = Add New Container
    .style = min-width: 32em
# Variables
#   $name (String) - Name of the container
containers-window-update-settings3 =
    .title = { $name } Container Settings
    .style = min-width: 32em

containers-window-close =
    .key = w

containers-name-label2 =
    .label = Name
    .placeholder = Add a name

containers-icon-label2 =
    .label = Icon

containers-color-label2 =
    .label = Color

containers-dialog2 =
    .buttonlabelaccept = Save
    .buttonaccesskeyaccept = S

## Dialog that binds a website to a container.

containers-site-window =
    .title = Add Website

containers-site-label =
    .label = Website
    .placeholder = example.com

containers-site-container-label =
    .label = Container

containers-site-dialog =
    .buttonlabelaccept = Save
    .buttonaccesskeyaccept = S

# Shown below the website field when the typed website is not a host, or when
# it is a URL that does not use the https scheme.
containers-site-invalid-error = Enter a valid, secure website
# Shown below the website field when the typed website is already bound to a
# container.
containers-site-duplicate-error = This website is already in your list

## Container creation panel, anchored to the URL bar.

containers-panel-title = Add New Container
containers-panel-create-button =
    .label = Done
containers-panel-cancel-button =
    .label = Cancel
