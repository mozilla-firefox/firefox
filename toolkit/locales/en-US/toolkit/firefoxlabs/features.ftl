# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# The title of the experiment should be kept in English as it may be referenced
# by various online articles and is technical in nature.
experimental-features-media-jxl =
    .label = Media: JPEG XL
experimental-features-media-jxl-description = With this feature enabled, { -brand-short-name } supports the JPEG XL (JXL) format. This is an enhanced image file format that supports lossless transition from traditional JPEG files. See <a data-l10n-name="bugzilla">bug 1539075</a> for more details.

# JS JIT Warp project
experimental-features-js-warp =
    .label = JavaScript JIT: Warp
experimental-features-js-warp-description = Enable Warp, a project to improve JavaScript performance and memory usage.

# Search during IME
experimental-features-ime-search =
    .label = Address Bar: show results during IME composition
experimental-features-ime-search-description = An IME (Input Method Editor) is a tool that allows you to enter complex symbols, such as those used in East Asian or Indic written languages, using a standard keyboard. Enabling this experiment will keep the address bar panel open, showing search results and suggestions, while using IME to input text. Note that the IME might display a panel that covers the address bar results, therefore this preference is only suggested for IME not using this type of panel.

# Auto Picture-in-Picture
experimental-features-auto-pip =
    .label = Picture-in-Picture: auto-open on tab switch
experimental-features-auto-pip-description = Enable Picture-in-Picture on active videos when switching tabs.

experimental-features-group-developer-tools =
  .label = Developer Tools
experimental-features-group-webpage-display =
  .label = Webpage Display
experimental-features-group-customize-browsing =
  .label = Customize your browsing

# Contextual Password Manager in sidebar
experimental-features-contextual-password-manager =
    .label = Passwords in the sidebar
experimental-features-contextual-password-manager-description = Access your passwords right from the sidebar. No more searching around or resetting passwords when you’re trying to sign in. To use this feature, select passwords in the sidebar. Then go to your general settings and under browser layout, select show sidebar. Then, customize your sidebar by selecting passwords. If you try it, <a data-l10n-name="connect">let us know what you think</a>.

# New Tab Custom Wallpapers
experimental-features-custom-wallpaper =
    .label = Choose a custom wallpaper or color for New Tab
experimental-features-custom-wallpaper-description = Upload your own wallpaper or pick a custom color for your New Tab background.

# Link Previews with AI
experimental-features-link-previews =
    .label = Link previews
experimental-features-link-previews-description =
    { PLATFORM() ->
        [macos] To learn more about a webpage before you click, hover over a link and press Shift (⇧) plus Option (⌥) or Alt. Previews can include details like title and reading time. For some webpages, AI can also read the page text and generate key points. The AI is optimized to read and generate English text. To prioritize your privacy, the AI runs locally on your computer. <a data-l10n-name="connect">Share feedback</a>
       *[other] To learn more about a webpage before you click, hover over a link and press Shift + Alt. Previews can include details like title and reading time. For some webpages, AI can also read the page text and generate key points. The AI is optimized to read and generate English text. To prioritize your privacy, the AI runs locally on your computer. <a data-l10n-name="connect">Share feedback</a>
    }

# This version of the link previews description does not mention AI.
experimental-features-link-previews-description-no-ai =
    { PLATFORM() ->
        [macos] To learn more about a webpage before you click, hover over a link and press Shift (⇧) plus Option (⌥) or Alt. Previews can include details like title and reading time. <a data-l10n-name="connect">Share feedback</a>
       *[other] To learn more about a webpage before you click, hover over a link and press Shift + Alt. Previews can include details like title and reading time. <a data-l10n-name="connect">Share feedback</a>
    }
