# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Iccer amaynut
newtab-settings-button =
    .title = Sagen asebter n yiccer-ik amaynut
newtab-settings-dialog-label =
    .aria-label = Iɣewwaṛen
newtab-personalize-icon-label =
    .title = Sagen iccer amaynut
    .aria-label = Sagen iccer amaynut
newtab-personalize-dialog-label =
    .aria-label = Sagen
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Nadi
    .aria-label = Nadi
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Nadi s { $engine } neɣ sekcem tansa
newtab-search-box-handoff-text-no-engine = Nadi neɣ sekcem tansa
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Nadi s { $engine } neɣ sekcem tansa
    .title = Nadi s { $engine } neɣ sekcem tansa
    .aria-label = Nadi s { $engine } neɣ sekcem tansa
newtab-search-box-handoff-input-no-engine =
    .placeholder = Nadi neɣ sekcem tansa
    .title = Nadi neɣ sekcem tansa
    .aria-label = Nadi neɣ sekcem tansa
newtab-search-box-text = Nadi di web
newtab-search-box-input =
    .placeholder = Nadi di web
    .aria-label = Nadi di web

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Rnu amsedday n unadi
newtab-topsites-add-shortcut-header = Anegzum amaynut
newtab-topsites-edit-topsites-header = Ẓreg asmel ifazen
newtab-topsites-edit-shortcut-header = Ẓreg anegzum
newtab-topsites-add-shortcut-label = Rnu anegzum
newtab-topsites-title-label = Azwel
newtab-topsites-title-input =
    .placeholder = Sekcem azwel
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Aru neɣ sekcem tansa URL
newtab-topsites-url-validation = Tansa URL tameɣtut tettwasra
newtab-topsites-image-url-label = Tugna tudmawant URL
newtab-topsites-use-image-link = Seqdec tugna tudmawant…
newtab-topsites-image-validation = Tugna ur d-uli ara. Ɛreḍ tansa-nniḍen URL.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Sefsex
newtab-topsites-delete-history-button = Kkes seg umazray
newtab-topsites-save-button = Sekles
newtab-topsites-preview-button = Taskant
newtab-topsites-add-button = Rnu

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Tebɣiḍ ad tekksed yal tummant n usebter-agi seg umazray-ik?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Tigawt-agi ur tettuɣal ara ar deffir.

## Top Sites - Sponsored label

newtab-topsite-sponsored = S lmendad

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Ldi umuɣ
    .aria-label = Ldi umuɣ
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Kkes
    .aria-label = Kkes
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Ldi umuɣ
    .aria-label = Ldi umuɣ asatal i { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Ẓreg asmel-agi
    .aria-label = Ẓreg asmel-agi

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Ẓreg
newtab-menu-open-new-window = Ldi deg usfaylu amaynut
newtab-menu-open-new-private-window = Ldi deg usfaylu uslig amaynut
newtab-menu-dismiss = Kkes
newtab-menu-pin = Senteḍ
newtab-menu-unpin = Serreḥ
newtab-menu-delete-history = Kkes seg umazray
newtab-menu-save-to-pocket = Sekles ɣer { -pocket-brand-name }
newtab-menu-delete-pocket = Kkes si { -pocket-brand-name }
newtab-menu-archive-pocket = Ḥrez di { -pocket-brand-name }
newtab-menu-show-privacy-info = Wid yettbeddan fell-aɣ akked tudert-ik tabaḍnit
newtab-menu-about-fakespot = Γef { -fakespot-brand-name }
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = Sewḥel

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Immed
newtab-privacy-modal-button-manage = Sefrek iɣewwaren n ugbur yettwarefden
newtab-privacy-modal-header = Aqadeṛ n tudert-ik tabaḍnit yeɛna-aɣ.
newtab-privacy-modal-paragraph-2 = Ɣer tama n beṭṭu n teqsiḍin ijebbden, ad ak-d-nesken daɣen igburen usdiden akked wid yettbeddan fell-ak i d-nefren s telqay. <strong>Kkes aɣilif imi isefka-ik n tunigin ur teffɣen ara segunqal i tḥerzeḍ n { -brand-product-name }</strong> — ur ten-nettwali ara, ula d wid i yettbeddan fell-aɣ.
newtab-privacy-modal-link = Lmed amek tettedu tbaḍnit deg yiccer amaynut

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Kkes tacreṭ-agi
# Bookmark is a verb here.
newtab-menu-bookmark = Creḍ asebter-agi

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Nɣel tansa n useɣwen n usali
newtab-menu-go-to-download-page = Ddu ɣer usebter n usader
newtab-menu-remove-download = Kkes seg umazray

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Sken deg Finder
       *[other] Ldi akaram deg yella ufaylu
    }
newtab-menu-open-file = Ldi afaylu

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Yettwarza
newtab-label-bookmarked = Yettwacreḍ
newtab-label-removed-bookmark = Tacreṭ n usebter tettwakkes
newtab-label-recommended = Tiddin
newtab-label-saved = Yettwakles ɣer { -pocket-brand-name }
newtab-label-download = Yuli-d
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · Yettwarfed
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Ddaw leɛnaya n { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } tsd

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Kkes tigezmi
newtab-section-menu-collapse-section = Fneẓ tigezmi
newtab-section-menu-expand-section = Snefli tigezmi
newtab-section-menu-manage-section = Sefrek tigezmi
newtab-section-menu-manage-webext = Sefrek asiɣzef
newtab-section-menu-add-topsite = Rnu asmel ifazen
newtab-section-menu-add-search-engine = Rnu amsedday n unadi
newtab-section-menu-move-up = Ali
newtab-section-menu-move-down = Ader
newtab-section-menu-privacy-notice = Tasertit n tbaḍnit

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Fneẓ tigezmi
newtab-section-expand-section-label =
    .aria-label = Snefli tigezmi

## Section Headers.

newtab-section-header-topsites = Ismal ifazen
newtab-section-header-recent-activity = Armud n melmi kan
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Iwelleh-it-id { $provider }
newtab-section-header-stories = Tiqsiḍin i ijebbden lwelha
# "picks" refers to recommended articles
newtab-section-header-todays-picks = Tafrant-nneɣ n wass

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Bdu tuniginn sakin nekkni ad k-n-sken imagraden, tividyutin, akked isebtar nniḍen i γef terziḍ yakan neγ i tceṛḍeḍ dagi.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Ulac wiyaḍ. Uɣal-d ticki s wugar n imagraden seg { $provider }. Ur tebɣiḍ ara ad terǧuḍ? Fren asentel seg wid yettwasnen akken ad twaliḍ imagraden yelhan di Web.
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = Ulac wiyaḍ. Uɣal-d ticki s wugar n imagraden. Ur tebɣiḍ ara ad terǧuḍ? Fren asentel seg wid yettwasnen  akken ad twaliḍ imagraden yelhan di Web.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Ulac d acu yellan.
newtab-discovery-empty-section-topstories-content = Uɣal-d ticki akken ad tafeḍ ugar n teqsiḍin.
newtab-discovery-empty-section-topstories-try-again-button = Ɛreḍ tikkelt-nniḍen
newtab-discovery-empty-section-topstories-loading = Asali…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Ihuh! Waqil tigezmi ur d-tuli ara akken iwata.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Isental ittwasnen aṭas:
newtab-pocket-new-topics-title = Tebɣiḍ ugar n yimagraden? Wali isental-a yettwassnen aṭas seg { -pocket-brand-name }
newtab-pocket-more-recommendations = Ugar n iwellihen
newtab-pocket-learn-more = Issin ugar
newtab-pocket-cta-button = Awi-d { -pocket-brand-name }
newtab-pocket-cta-text = Sekles tiqṣiḍin i tḥemmleḍ deg { -pocket-brand-name }, sedhu allaɣ-ik s tɣuri ifazen.
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } d yiwen seg twacult n { -brand-product-name }
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = Sekles
newtab-pocket-saved = Yettwasekles

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = Ugar am wagi
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = Mačči i nekk
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = Tanemmirt. Tamawt-ik ad ak-tall ad tesnerniḍ asuddem-ik.
newtab-toast-dismiss-button =
    .title = Zgel
    .aria-label = Zgel

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Snirem ayen akk igerrzen deg web

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Ihuh, yella wayen yeḍran deg usali n ugbur-a.
newtab-error-fallback-refresh-link = Sali-d aseter akken ad talseḍ aɛraḍ.

## Customization Menu

newtab-custom-shortcuts-title = Inegzumen
newtab-custom-shortcuts-subtitle = Ismal i teskelseḍ neɣ wuɣur terziḍ
newtab-custom-shortcuts-toggle =
    .label = Inegzumen
    .description = Ismal i teskelseḍ neɣ wuɣur terziḍ
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } yizirig
       *[other] { $num } yizirigen
    }
newtab-custom-sponsored-sites = Inegzumen yettwarefden
newtab-custom-pocket-title = yettuwelleh-d sɣur { -pocket-brand-name }
newtab-custom-pocket-subtitle = Agbur ufrin i s-yettusuddsen sɣur { -pocket-brand-name }, d aḥric seg twacult { -brand-product-name }
newtab-custom-pocket-sponsored = Tiqṣidin yettwarefden
newtab-custom-pocket-show-recent-saves = Sken iseklas akk ineggura
newtab-custom-recent-title = Armud n melmi kan
newtab-custom-recent-subtitle = Tafrant n yismal d ugbur n melmi kan
newtab-custom-recent-toggle =
    .label = Armud n melmi kan
    .description = Tafrant n yismal d ugbur n melmi kan
newtab-custom-close-button = Mdel
newtab-custom-settings = Sefrek ugar n yiɣewwaṛen

## New Tab Wallpapers

newtab-wallpaper-title = Tugniwin n ugilal
newtab-wallpaper-reset = Wennez ɣer umezwer
newtab-wallpaper-upload-image = Sali n tugna
newtab-wallpaper-custom-color = Fren ini
newtab-wallpaper-light-red-panda = Apunda azewwaɣ
newtab-wallpaper-light-mountain = Adrar amellal
newtab-wallpaper-dark-mountain = Tugna n yidurar

## Solid Colors

newtab-wallpaper-blue = Amidadi
newtab-wallpaper-green = Azegzaw
newtab-wallpaper-yellow = Awraɣ
newtab-wallpaper-orange = Ačinawi
newtab-wallpaper-pink = Axuxi
newtab-wallpaper-red = Azggaɣ
newtab-wallpaper-dark-blue = Amidadi iḥemqen
newtab-wallpaper-brown = Aqehwi

## Abstract

newtab-wallpaper-category-title-abstract = Amadwan

## Celestial

newtab-wallpaper-white-mountains = Idurar imellalen
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = Tawlaft s <a data-l10n-name="name-link">{ $author_string }</a> ɣef <a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-header = Ɛreḍ aṛuccu n yini
newtab-wallpaper-feature-highlight-button = Awi-t
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = Zgel
    .aria-label = Mdel asfaylu udhim
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial


## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ S lmendad
newtab-weather-menu-change-location = Ẓreg adig
newtab-weather-change-location-search-input-placeholder =
    .placeholder = Adig n unadi
    .aria-label = Adig n unadi
newtab-weather-change-location-search-input = Adig n unadi
newtab-weather-menu-weather-display = Askan n tegnawt
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = Aḥerfi
newtab-weather-menu-change-weather-display-simple = Uɣal ɣer uskan afessas
newtab-weather-menu-weather-display-option-detailed = S telqayt
newtab-weather-menu-change-weather-display-detailed = Uɣal ɣer uskan alqayan
newtab-weather-menu-temperature-option-fahrenheit = Fahrenheit
newtab-weather-menu-temperature-option-celsius = Celsius
newtab-weather-menu-change-temperature-units-fahrenheit = Beddel ɣer Fahrenheit
newtab-weather-menu-change-temperature-units-celsius = Beddel ɣer Celsius
newtab-weather-menu-learn-more = Issin ugar

## Topic Labels

newtab-topic-label-business = Amahil
newtab-topic-label-career = Axeddim
newtab-topic-label-education = Aselmed
newtab-topic-label-food = Tuččit
newtab-topic-label-health = Tazmert
newtab-topic-label-hobbies = Uraren
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = Tadrimt
newtab-topic-label-society-parenting = Timarawt
newtab-topic-label-government = Tasertit
newtab-topic-label-education-science = Tussna
newtab-topic-label-sports = Addal
newtab-topic-label-tech = Tatiknulujit
newtab-topic-label-travel = Tirza
newtab-topic-label-home = Axxam & Tibḥirt

## Topic Selection Modal

newtab-topic-selection-save-button = Sekles
newtab-topic-selection-cancel-button = Sefsex
newtab-topic-selection-button-maybe-later = Ahat ticki
newtab-topic-selection-privacy-link = Ẓer amek i nemmestan akked wamek i nessefrak isefka
newtab-topic-selection-button-update-interests = Leqqem ismenyaf-ik

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.


## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.


## Confirmation modal for blocking a section


## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-mangage-topics-blocked-topics = Iwḥel
