# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Նոր ներդիր
newtab-settings-button =
    .title = Յարմարեցրէք Ձեր նոր ներդիր էջը
newtab-personalize-icon-label =
    .title = Անհատականացնել նոր ներդիրը
    .aria-label = Անհատականացնել նոր ներդիրը
newtab-personalize-dialog-label =
    .aria-label = Անհատականացնել

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Որոնում
    .aria-label = Որոնում
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Որոնել { $engine }-ով կամ մուտքագրել հասցէն
newtab-search-box-handoff-text-no-engine = Որոնել կամ մուտքագրել հասցէն
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder =
        Որոնել { $engine }֊ով կամ մուտքագրել հասցէն
        որոնում 
        որոնում 
        Որոնել { $engine } կամ մուտքագրել հասցէն
    .title = Որոնել { $engine }֊ով կամ մուտքագրել հասցէն
    .aria-label = Որոնել { $engine }֊ով  կամ մուտքագրել հասցէն
newtab-search-box-handoff-input-no-engine =
    .placeholder = Որոնել կամ մուտքագրել հասցէն
    .title = Որոնել կամ մուտքագրել հասցէն
    .aria-label = Որոնել կամ մուտքագրել հասցէն
newtab-search-box-text = Որոնել համացանցում
newtab-search-box-input =
    .placeholder = Որոնել առցանց
    .aria-label = Որոնել առցանց

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Աւելացնել որոնիչ
newtab-topsites-add-shortcut-header = Նոր դիւրանցում
newtab-topsites-edit-topsites-header = Խմբագրել լաւագոյն կայքերը
newtab-topsites-edit-shortcut-header = Խմբագրել դիւրանցումը
newtab-topsites-title-label = Անուանում
newtab-topsites-title-input =
    .placeholder = Մուտքագրեք անուանում
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Մուտքագրեք կամ տեղադրեք URL
newtab-topsites-url-validation = Անհրաժեշտ է վաւեր URL
newtab-topsites-image-url-label = Հարմարեցուած նկարի URL
newtab-topsites-use-image-link = Աւգտագործել հարմարեցուած նկար…
newtab-topsites-image-validation = Նկարը չհաջողուեց բեռնել։ Փորձեք այլ URL։

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Չեղարկել
newtab-topsites-delete-history-button = Ջնջել Պատմութիւնից
newtab-topsites-save-button = Պահպանել
newtab-topsites-preview-button = Նախադիտել
newtab-topsites-add-button = Աւելացնել

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Վստա՞հ եք, որ ցանկանում եք ջնջել այս էջի ամեն մի աւրինակ Ձեր պատմութիւնից։
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Այս գործողութիւնը չի կարող վերացուել։

## Top Sites - Sponsored label

newtab-topsite-sponsored = Հովանաւորուում է

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Բացել ցանկը
    .aria-label = Բացել ցանկը
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Հեռացնել
    .aria-label = Հեռացնել
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Բացել ցանկը
    .aria-label = Բացել համատեքստի ցանկը { $title }-ի համար
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Խմբագրել այս կայքը
    .aria-label = Խմբագրել այս կայքը

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Խմբագրել
newtab-menu-open-new-window = Բացել նոր պատուհանով
newtab-menu-open-new-private-window = Բացել նոր գաղտնի դիտարկմամբ
newtab-menu-dismiss = Բաց թողնել
newtab-menu-pin = Ամրացնել
newtab-menu-unpin = Ապամրացնել
newtab-menu-delete-history = Ջնջել Պատմութիւնից
newtab-menu-save-to-pocket = Պահպանել { -pocket-brand-name }-ում
newtab-menu-delete-pocket = Ջնջել { -pocket-brand-name }-ից
newtab-menu-archive-pocket = Արխիւացնել { -pocket-brand-name }-ում
newtab-menu-show-privacy-info = Մեր հովանաւորները եւ Ձեր գաղտնիութիւնը

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Արուած
newtab-privacy-modal-button-manage = Կառավարել հովանաւորուած բովանդակութեան կարգաւորումները
newtab-privacy-modal-header = Ձեր գաղտնիութիւնը կարեւոր է։
newtab-privacy-modal-paragraph-2 = Հետաքրքրաշարժ հոդուածներ առաջարկելուց բացի, մենք նաեւ ցուցադրում ենք մեր հիմնական հովանաւորների կողմից ներկայացուող բարձրակարգ նիւթ։ Համոզուած եղէք, <strong> որ Ձեր դիտարկած տուեալները երբեք դուրս չեն գալիս { -brand-product-name }</strong> — -ի ձեր անձնական պատճէնից։ Դրանք չենք տեսնում ինչպէս մենք, այնպէս էլ մեր հովանաւորները:
newtab-privacy-modal-link = Իմացեք թե ինչպէս է գաղտնիութիւնն աշխատում նոր ներդիրում

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Հեռացնել էջանիշը
# Bookmark is a verb here.
newtab-menu-bookmark = Էջանիշ

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Պատճէնել ներբեռնելու յղումը
newtab-menu-go-to-download-page = Անցնել ներբեռնելու էջին
newtab-menu-remove-download = Ջնջել պատմութիւնից

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Ցուցադրել Որոնիչում
       *[other] Բացել պարունակութեան պանակը
    }
newtab-menu-open-file = Բացել նիշը

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Այցելած
newtab-label-bookmarked = Էջանշուած
newtab-label-removed-bookmark = Էջանիշը հեռացուել է
newtab-label-recommended = Միտում
newtab-label-saved = Պահպանուած է { -pocket-brand-name }-ում
newtab-label-download = Ներբեռնուած է
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource }: Հովանաւորուած
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Հովանաւորուած { $sponsor }֊ի կողմից
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } րոպէ

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Հեռացնել բաժինը
newtab-section-menu-collapse-section = Կոծկել բաժինը
newtab-section-menu-expand-section = Ընդարձակել բաժինը
newtab-section-menu-manage-section = Կառավարել բաժինը
newtab-section-menu-manage-webext = Կառավարել ընդլայնումը
newtab-section-menu-add-topsite = Աւելացնել լաւագոյն կայքերին
newtab-section-menu-add-search-engine = Աւելացնել որոնիչին
newtab-section-menu-move-up = Վեր
newtab-section-menu-move-down = Տեղաշարժային վնասուածք
newtab-section-menu-privacy-notice = Գաղտնիութեան դրոյթներ

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Կոծկել բաժինը
newtab-section-expand-section-label =
    .aria-label = Ընդարձակել բաժինը

## Section Headers.

newtab-section-header-topsites = Լաւագոյն կայքեր
newtab-section-header-recent-activity = Վերջին գործողութիւնը
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Առաջարկուում է { $provider }

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Սկսէք դիտարկել եւ մենք կը ցուցադրենք որոշ հիանալի յաւդուածներ, տեսանիւթեր եւ այլ էջեր, որ դուք այցելել կամ էջանշել էք վերջերս։
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Ամեն ինչ պատրաստ է։ Ստուգեք աւելի ուշ՝ աւելի շատ պատմութիւններ ստանալու համար { $provider } մատակարարից։ Չեք կարող սպասել։ Ընտրեք հանրաճանաչ թէմա՝ համացանցից աւելի հիանալի պատմութիւններ գտնելու համար։

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Ամէն ինչ պատրաստ է։
newtab-discovery-empty-section-topstories-content = Վերադարձեք աւելի ուշ՝ այլ պատմութիւնների համար։
newtab-discovery-empty-section-topstories-try-again-button = Կրկին փորձել
newtab-discovery-empty-section-topstories-loading = Բեռնում…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Վայ մենք գրեթե բեռնում ենք այս հատուածը, բայց ոչ ամբողջովին։

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Հանրաճանաչ թէմաներ.
newtab-pocket-new-topics-title = Ցանկանու՞մ եք աւելի շատ պատմութիւններ: Դիտեք աւելի յայտնի թեմաները { -pocket-brand-name }-ով
newtab-pocket-more-recommendations = Լրացուցիչ առաջարկութիւններ
newtab-pocket-learn-more = Իմանալ աւելին
newtab-pocket-cta-button = Ստանալ { -pocket-brand-name }
newtab-pocket-cta-text = Խնայեք Ձեր սիրած պատմութիւնները { -pocket-brand-name }, եւ Ձեր միտքը վառեցրեք հետաքրքրաշարժ ընթերցանութեամբ։
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } պատկանում է { -brand-product-name } ընտանիքին

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.


## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.


## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Վայ, ինչ-որ սխալ է տեղի ունեցել այս բովանդակութիւնը բեռնելու համար։
newtab-error-fallback-refresh-link = Թարմացրեք էջը՝ կրկին փորձելու համար։

## Customization Menu

newtab-custom-shortcuts-title = Դիւրանցումներ
newtab-custom-shortcuts-subtitle = Կայքեր, որոնք Դուք պահում էք կամ այցելում
newtab-custom-shortcuts-toggle =
    .label = Դիւրանցումներ
    .description = Կայքեր, որոնք Դուք պահում էք կամ այցելում
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } տող
       *[other] { $num } տողեր
    }
newtab-custom-sponsored-sites = Հովանաւորուած դիւրանցումներ
newtab-custom-pocket-title = Առաջարկուում է { -pocket-brand-name } -ի կողմից
newtab-custom-pocket-subtitle = Բացառիկ բովանդակութիւն, որը համադրել է { -brand-product-name }-ի ընտանիքի մաս կազմող { -pocket-brand-name }-ը
newtab-custom-pocket-sponsored = Հովանաւորուած պատմութիւններ
newtab-custom-recent-title = Վերջին գործողութիւնը
newtab-custom-recent-subtitle = Վերջին կայքերի եւ բովանդակութեան ընտրութիւն
newtab-custom-recent-toggle =
    .label = Վերջին գործողութիւնը
    .description = Վերջին կայքերի եւ բովանդակութեան ընտրութիւն
newtab-custom-close-button = Փակել
newtab-custom-settings = Կառավարէք աւելի շատ կարգաւորումներ

## New Tab Wallpapers


## Solid Colors


## Abstract


## Celestial


## Celestial


## New Tab Weather


## Topic Labels


## Topic Selection Modal


## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.


## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.


## Confirmation modal for blocking a section


## Panel in the Customize menu section to manage followed and blocked topics

