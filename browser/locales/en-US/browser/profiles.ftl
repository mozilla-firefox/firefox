# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

profile-window-title-2 = { -brand-short-name } - Choose a profile

profile-window-logo =
    .alt = { -brand-short-name } logo
profile-window-heading = Choose a { -brand-short-name } profile
profile-window-body = Keep your work and personal browsing, including things like passwords and bookmarks, totally separate. Or create profiles for everyone who uses this device.
# This checkbox appears in the Choose profile window that appears when the browser is opened. "Show this" refers to this window, which is displayed when the checkbox is enabled.
profile-window-checkbox-label-2 =
    .label = Choose a profile when { -brand-short-name } opens
# This subcopy appears below the checkbox when it is unchecked
profile-window-checkbox-subcopy = { -brand-short-name } will open to your most recently used profile.
profile-window-create-profile = Create a profile
profile-card-edit-button =
    .title = Edit profile
    .aria-label = Edit profile
profile-card-delete-button =
    .title = Delete profile
    .aria-label = Delete profile

# Variables
#   $profileName (string) - The name of the profile
profile-card =
    .title = Open { $profileName }
    .aria-label = Open { $profileName }

# Variables
#   $number (number) - The number of the profile
default-profile-name = Profile { $number }

# The word 'original' is used in the sense that it is the initial or starting profile when you install Firefox.
original-profile-name = Original profile

default-desktop-shortcut-name = { -brand-short-name }

edit-profile-page-title = Edit profile
edit-profile-page-header = Edit your profile
edit-profile-page-profile-name-label = Profile name
edit-profile-page-theme-header-2 =
    .label = Theme
edit-profile-page-explore-themes = Explore more themes
edit-profile-page-desktop-shortcut-header = Create desktop shortcut
edit-profile-page-desktop-shortcut-toggle =
    .aria-label = Create desktop shortcut
edit-profile-page-avatar-header-2 =
    .label = Avatar
edit-profile-page-delete-button =
    .label = Delete

edit-profile-page-avatar-selector-opener-link = Edit
avatar-selector-icon-tab = Icon
avatar-selector-custom-tab = Custom
avatar-selector-cancel-button =
  .label = Cancel
avatar-selector-save-button =
  .label = Save
avatar-selector-upload-file = Upload a file
avatar-selector-drag-file = Or drag a file here
avatar-selector-add-image = Add an image
avatar-selector-crop = Crop

edit-profile-page-no-name = Name this profile to help you find it later. Rename it any time.
edit-profile-page-duplicate-name = Profile name already in use. Try a new name.

edit-profile-page-profile-saved = Saved

new-profile-page-title = New profile
new-profile-page-header = Customize your new profile
new-profile-page-header-description = Each profile keeps its unique browsing history and settings separate from your other profiles. Plus, { -brand-short-name }’s strong privacy protections are on by default.
new-profile-page-learn-more = Learn more
new-profile-page-input-placeholder =
    .placeholder = Pick a name like “Work” or “Personal”
new-profile-page-done-button =
    .label = Done editing

# Variables
#   $profilename (String) - The name of the copied profile.
copied-profile-page-header-2 = Your copy of “{ $profilename }” is ready to customize
copied-profile-page-header-description = We copied your data and settings into a new profile. Now give it a name, pick a look, and make it your own.

restored-profile-page-header = Customize your restored profile
restored-profile-page-header-description = Each profile keeps its unique browsing history and settings separate from your other profiles. Plus, { -brand-short-name }’s strong privacy protections are on by default.
restored-profile-page-learn-more = Learn more

## Delete profile dialogue that allows users to review what they will lose if they choose to delete their profile. Each item (open windows, etc.) is displayed in a table, followed by a column with the number of items.

# Variables
#   $profilename (String) - The name of the profile.
delete-profile-page-title-2 = Delete “{ $profilename }” profile

# Variables
#   $profilename (String) - The name of the profile.
delete-profile-header-2 = Delete "{ $profilename }" profile?
delete-profile-description = { -brand-short-name } will permanently delete the following data from this device:
# Open is an adjective, as in "browser windows currently open".
delete-profile-windows = Open windows
# Open is an adjective, as in "browser tabs currently open".
delete-profile-tabs = Open tabs
delete-profile-bookmarks = Bookmarks
delete-profile-history = History (visited pages, cookies, site data)
delete-profile-autofill = Autofill data (addresses, payment methods)
delete-profile-logins = Passwords

##

# Button label
delete-profile-cancel = Cancel
# Button label
delete-profile-confirm = Delete

## These strings are color themes available to select from the profile selection screen. Theme names should be localized.

profiles-gray-theme = Gray
profiles-gray-theme-title =
    .title = Apply gray theme
profiles-yellow-theme = Yellow
profiles-yellow-theme-title =
    .title = Apply yellow theme
profiles-orange-theme = Orange
profiles-orange-theme-title =
    .title = Apply orange theme
profiles-red-theme = Red
profiles-red-theme-title =
    .title = Apply red theme
profiles-pink-theme = Pink
profiles-pink-theme-title =
    .title = Apply pink theme
profiles-purple-theme = Purple
profiles-purple-theme-title =
    .title = Apply purple theme
profiles-violet-theme = Violet
profiles-violet-theme-title =
    .title = Apply violet theme
profiles-blue-theme = Blue
profiles-blue-theme-title =
    .title = Apply blue theme
profiles-green-theme = Green
profiles-green-theme-title =
    .title = Apply green theme
profiles-cyan-theme = Cyan
profiles-cyan-theme-title =
    .title = Apply cyan theme
profiles-custom-theme-title =
    .title = Apply custom theme

# The default system theme
profiles-system-theme = System
profiles-system-theme-title =
    .title = Apply system theme

## Data collection settings changed (multi-profile)

# Full infobar message with inline bold title followed by body text
multiprofile-data-collection-message = <strong>Data collection settings changed.</strong> The changes made in another profile apply to all profiles on this device.

# Primary button label to open the Data collection section in Settings
multiprofile-data-collection-view-settings = View settings

# Secondary button label to dismiss the infobar without action
multiprofile-data-collection-dismiss = Dismiss

## Alternative text for default profile icons

barbell-avatar-alt =
    .alt = Barbell
bike-avatar-alt =
    .alt = Bike
book-avatar-alt =
    .alt = Book
briefcase-avatar-alt =
    .alt = Briefcase
picture-avatar-alt =
    .alt = Picture
# Craft refers to hobby arts and crafts, represented by a button/fastener commonly found on clothing like shirts
craft-avatar-alt =
    .alt = Craft
custom-avatar-alt =
    .alt = Custom avatar
# Globe refers to the generic globe/world icon that appears in browser tabs when a website doesn't have its own favicon.
globe-avatar-alt =
    .alt = Globe
# Diamond refers to the precious stone, not the geometric shape
diamond-avatar-alt =
    .alt = Diamond
flower-avatar-alt =
    .alt = Flower
folder-avatar-alt =
    .alt = Folder
hammer-avatar-alt =
    .alt = Hammer
heart-avatar-alt =
    .alt = Heart
heart-rate-avatar-alt =
    .alt = Heart rate
clock-avatar-alt =
    .alt = Clock
leaf-avatar-alt =
    .alt = Leaf
lightbulb-avatar-alt =
    .alt = Lightbulb
makeup-avatar-alt =
    .alt = Makeup
# Message refers to a text message, not a traditional letter/envelope message
message-avatar-alt =
    .alt = Message
musical-note-avatar-alt =
    .alt = Musical note
palette-avatar-alt =
    .alt = Palette
paw-print-avatar-alt =
    .alt = Paw print
plane-avatar-alt =
    .alt = Plane
# Present refers to a gift box, not the current time period
present-avatar-alt =
    .alt = Present
shopping-avatar-alt =
    .alt = Shopping cart
soccer-ball-avatar-alt =
    .alt = Soccer ball
sparkle-single-avatar-alt =
    .alt = Sparkle
star-avatar-alt =
    .alt = Star
video-game-controller-avatar-alt =
    .alt = Video game controller

## Labels for default avatar icons

barbell-avatar = Barbell
bike-avatar = Bike
book-avatar = Book
briefcase-avatar = Briefcase
clock-avatar = Clock
# Craft refers to hobby arts and crafts, represented by a button/fastener commonly found on clothing like shirts
craft-avatar = Craft
custom-avatar = Custom avatar
# Diamond refers to the precious stone, not the geometric shape
diamond-avatar = Diamond
flower-avatar = Flower
folder-avatar = Folder
# Globe refers to the generic globe/world icon that appears in browser tabs when a website doesn't have its own favicon.
globe-avatar = Globe
hammer-avatar = Hammer
heart-avatar = Heart
heart-rate-avatar = Heart rate
leaf-avatar = Leaf
lightbulb-avatar = Lightbulb
makeup-avatar = Makeup
# Message refers to a text message, not a traditional letter/envelope message
message-avatar = Message
musical-note-avatar = Musical note
palette-avatar = Palette
paw-print-avatar = Paw print
picture-avatar = Picture
plane-avatar = Plane
# Present refers to a gift box, not the current time period
present-avatar = Present
shopping-avatar = Shopping cart
soccer-ball-avatar = Soccer ball
sparkle-single-avatar = Sparkle
star-avatar = Star
video-game-controller-avatar = Video game controller

## Tooltips for default avatar icons

barbell-avatar-tooltip =
    .tooltiptext = Apply barbell avatar
bike-avatar-tooltip =
    .tooltiptext = Apply bike avatar
book-avatar-tooltip =
    .tooltiptext = Apply book avatar
briefcase-avatar-tooltip =
    .tooltiptext = Apply briefcase avatar
picture-avatar-tooltip =
    .tooltiptext = Apply picture avatar
# Craft refers to hobby arts and crafts, represented by a button/fastener commonly found on clothing like shirts
craft-avatar-tooltip =
    .tooltiptext = Apply craft avatar
# Globe refers to the generic globe/world icon that appears in browser tabs when a website doesn't have its own favicon.
globe-avatar-tooltip =
    .tooltiptext = Apply globe avatar
diamond-avatar-tooltip =
    .tooltiptext = Apply diamond avatar
flower-avatar-tooltip =
    .tooltiptext = Apply flower avatar
folder-avatar-tooltip =
    .tooltiptext = Apply folder avatar
hammer-avatar-tooltip =
    .tooltiptext = Apply hammer avatar
heart-avatar-tooltip =
    .tooltiptext = Apply heart avatar
heart-rate-avatar-tooltip =
    .tooltiptext = Apply heart rate avatar
clock-avatar-tooltip =
    .tooltiptext = Apply clock avatar
leaf-avatar-tooltip =
    .tooltiptext = Apply leaf avatar
lightbulb-avatar-tooltip =
    .tooltiptext = Apply lightbulb avatar
makeup-avatar-tooltip =
    .tooltiptext = Apply makeup avatar
# Message refers to a text message, not a traditional letter/envelope message
message-avatar-tooltip =
    .tooltiptext = Apply message avatar
musical-note-avatar-tooltip =
    .tooltiptext = Apply musical note avatar
palette-avatar-tooltip =
    .tooltiptext = Apply palette avatar
paw-print-avatar-tooltip =
    .tooltiptext = Apply paw print avatar
plane-avatar-tooltip =
    .tooltiptext = Apply plane avatar
# Present refers to a gift box, not the current time period
present-avatar-tooltip =
    .tooltiptext = Apply present avatar
shopping-avatar-tooltip =
    .tooltiptext = Apply shopping cart avatar
soccer-ball-avatar-tooltip =
    .tooltiptext = Apply soccer ball avatar
sparkle-single-avatar-tooltip =
    .tooltiptext = Apply sparkle avatar
star-avatar-tooltip =
    .tooltiptext = Apply star avatar
video-game-controller-avatar-tooltip =
    .tooltiptext = Apply video game controller avatar


custom-avatar-crop-back-button =
  .aria-label = Back
custom-avatar-crop-view =
  .aria-label = Crop image view
custom-avatar-crop-area =
  .aria-label = Adjust crop area
custom-avatar-drag-handle =
  .aria-label = Resize crop area

profiles-appmenu-callout-tour-title = Your new profile is good to go
# "Spin up another" means creating another profile, “Hop between your digital lives" is referring to switching between different profiles such as work, personal, etc.
profiles-appmenu-callout-tour-subtitle = In the ☰ menu, tap your profile name to spin up another, edit this one, or hop between your digital lives.
profiles-appmenu-callout-tour-primary-button = Show me how
