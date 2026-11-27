# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# This is the title of the page
about-logging-title = About Logging
about-logging-page-title = Logging manager
about-logging-current-log-file = Current log file:
about-logging-new-log-file = New log file:
about-logging-currently-enabled-log-modules = Currently enabled log modules:
about-logging-log-tutorial =
    See <a data-l10n-name="logging">HTTP Logging</a>
    for instructions on how to use this tool.
# This message is used as a button label, "Open" indicates an action.
about-logging-open-log-file-dir = Open directory
about-logging-set-log-file = Set Log File
about-logging-set-log-modules = Set Log Modules
about-logging-start-logging = Start Logging
about-logging-stop-logging = Stop Logging
about-logging-copy-as-url = Copy current settings as URL
about-logging-url-copied = Logging settings copied to the clipboard as a preset URL
about-logging-buttons-disabled = Logging configured via environment variables, dynamic configuration unavailable.
about-logging-some-elements-disabled = Logging configured via URL, some configuration options are unavailable
about-logging-info = Info:
about-logging-log-modules-selection = Log module selection
about-logging-new-log-modules = New log modules:
about-logging-logging-output-selection = Logging output
about-logging-logging-to-file = Logging to a file
about-logging-logging-to-profiler = Logging to the { -profiler-brand-name }
about-logging-no-log-modules = None
about-logging-no-log-file = None
about-logging-logging-preset-selector-text = Logging preset:
about-logging-with-profiler-stacks-checkbox = Enable stack traces for log messages
about-logging-with-javascript-tracing-checkbox = Enable JavaScript tracing
about-logging-menu =
  .title = Advanced options

## Logging presets

about-logging-preset-networking-label = Networking
about-logging-preset-networking-description = Log modules to diagnose networking issues
about-logging-preset-networking-cookie-label = Cookies
about-logging-preset-networking-cookie-description = Log modules to diagnose cookie issues
about-logging-preset-networking-websocket-label = WebSockets
about-logging-preset-networking-websocket-description = Log modules to diagnose WebSocket issues
about-logging-preset-networking-http3-label = HTTP/3
about-logging-preset-networking-http3-description = Log modules to diagnose HTTP/3 and QUIC issues
about-logging-preset-networking-http3-upload-speed-label = HTTP/3 upload speed
about-logging-preset-networking-http3-upload-speed-description = Log modules to diagnose HTTP/3 upload speed issues
about-logging-preset-media-playback-label = Media playback
about-logging-preset-media-playback-description = Log modules to diagnose media playback issues (not video-conferencing issues)
about-logging-preset-webrtc-label = WebRTC
about-logging-preset-webrtc-description = Log modules to diagnose WebRTC calls
about-logging-preset-webcodecs-label = WebCodecs
about-logging-preset-webcodecs-description = Log modules to diagnose issues with WebCodecs audio/video decoders and encoders, and image decoders
about-logging-preset-ml-label = Machine Learning
about-logging-preset-ml-description = Log modules to diagnose machine learning issues
about-logging-preset-web-compat-label = Web Compat
about-logging-preset-web-compat-description = Log modules to diagnose web compatibility issues
about-logging-preset-navigation = Navigation
about-logging-preset-navigation-description = Log modules to diagnose navigation and session history issues
about-logging-preset-vpn-label = VPN
about-logging-preset-vpn-description = Log modules to diagnose IP Protection (VPN) issues
about-logging-preset-webgpu-label = WebGPU
about-logging-preset-webgpu-description = Log modules to diagnose WebGPU issues
about-logging-preset-gfx-label = Graphics
about-logging-preset-gfx-description = Log modules to diagnose graphics issues
# This is specifically "Microsoft Windows". Microsoft normally doesn't localize it, and we should follow their convention here.
about-logging-preset-windows-label = Windows
about-logging-preset-windows-description = Log modules to diagnose issues specific to Microsoft Windows
# Input Method refers to the IME.
# IME is a component of the operating system used to input Asian languages.
about-logging-preset-ime-label = Input Method
# IME is a component of the operating system used to input Asian languages.
about-logging-preset-ime-description = Log modules to diagnose keyboard issues, including Input Method Editor (IME)
about-logging-preset-custom-label = Custom
about-logging-preset-custom-description = Log modules manually selected

# Error handling
about-logging-error = Error:

## Variables:
##   $k (String) - Variable name
##   $v (String) - Variable value

about-logging-invalid-output = Invalid value “{ $v }“ for key “{ $k }“
about-logging-unknown-logging-preset = Unknown logging preset “{ $v }“
about-logging-unknown-profiler-preset = Unknown profiler preset “{ $v }“
about-logging-unknown-option = Unknown about:logging option “{ $k }“

##

about-logging-configuration-url-ignored = Configuration URL ignored
about-logging-file-and-profiler-override = Can’t force file output and override profiler options at the same time

# Variables:
#   $errorText (string) - The received error message, inserted as is.
about-logging-unknown-error = An error happened: { $errorText }

about-logging-configured-via-url = Option configured via URL

## The upload interface is shown only with the preference toolkit.aboutLogging.uploadProfileToCloud
## set to true. It is false by default, except on Android.

about-logging-upload-question = The profile data has been captured. Do you want to save or upload it?
about-logging-save-button = Save
about-logging-upload-button = Upload

# Variables:
#   $path (string) - The path where the profile can be found.
about-logging-saved = Saved to { $path }

# Variables:
#   $percent (number) - The upload completion progress, to be displayed as a percentage. This is a value between 0 and 1.
about-logging-uploading-progress = Uploading profile data: { NUMBER($percent, maximumSignificantDigits: 2, style: "percent") }

# Variables:
#   $url (string) - The URL where the profile can be found
about-logging-uploaded = Uploaded to <a data-l10n-name="uploaded-message-url">{ $url }</a>
about-logging-share-uploaded-url = <img data-l10n-name="share-image"/> Share URL
# Variables:
#   $errorText (string) - The received error message, inserted as is.
about-logging-upload-error = An error happened while uploading the profile: { $errorText }
# Variables:
#   $errorText (string) - The received error message, inserted as is.
about-logging-profile-storage-error = An error happened while storing the uploaded profile: { $errorText }
# Variables:
#   $errorText (string) - The received error message, inserted as is.
about-logging-save-error = An error happened while saving the file: { $errorText }

## Uploaded Profiles section

# This string is used as the default name for performance profiles when they are
# uploaded from about:logging and saved to the local database. The generated
# name will appear in the "Uploaded Profiles" section list, allowing users to
# identify when each profile was captured.
# Variables:
#   $date (date) - The date and time when the profile was uploaded
about-logging-uploaded-profile-name = Profile { DATETIME($date, dateStyle: "short", timeStyle: "medium") }

about-logging-uploaded-profiles-title = Uploaded Profiles
about-logging-no-uploaded-profiles = No profiles have been uploaded yet.
about-logging-delete-uploaded-profile = Delete
about-logging-view-uploaded-profile = View Profile
about-logging-delete-profile-confirm-title = Delete Profile

# Confirmation message shown when deleting an uploaded profile.
# Variables:
#   $profileName (string) - The name of the profile being deleted.
about-logging-delete-profile-confirm = Are you sure you want to delete the profile “{ $profileName }”? This cannot be undone.
about-logging-deleting-profile = Deleting…
