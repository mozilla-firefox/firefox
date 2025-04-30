# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = ახალი ჩანართი
newtab-settings-button =
    .title = მოირგეთ ახალი ჩანართის გვერდი
newtab-personalize-settings-icon-label =
    .title = ახალი ჩანართის მორგება
    .aria-label = პარამეტრები
newtab-settings-dialog-label =
    .aria-label = პარამეტრები
newtab-personalize-icon-label =
    .title = ახალი ჩანართის მორგება
    .aria-label = ახალი ჩანართის მორგება
newtab-personalize-dialog-label =
    .aria-label = მორგება
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = ძიება
    .aria-label = ძიება
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = მოძებნეთ { $engine } საძიებოთი ან შეიყვანეთ მისამართი
newtab-search-box-handoff-text-no-engine = მოძებნეთ ან შეიყვანეთ მისამართი
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = მოძებნეთ { $engine } საძიებოთი ან შეიყვანეთ მისამართი
    .title = მოძებნეთ { $engine } საძიებოთი ან შეიყვანეთ მისამართი
    .aria-label = მოძებნეთ { $engine } საძიებოთი ან შეიყვანეთ მისამართი
newtab-search-box-handoff-input-no-engine =
    .placeholder = მოძებნეთ ან შეიყვანეთ მისამართი
    .title = მოძებნეთ ან შეიყვანეთ მისამართი
    .aria-label = მოძებნეთ ან შეიყვანეთ მისამართი
newtab-search-box-text = ძიება ინტერნეტში
newtab-search-box-input =
    .placeholder = ინტერნეტში ძიება
    .aria-label = ინტერნეტში ძიება

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = საძიებო სისტემის დამატება
newtab-topsites-add-shortcut-header = ახალი მალსახმობი
newtab-topsites-edit-topsites-header = რჩეული საიტის ჩასწორება
newtab-topsites-edit-shortcut-header = მალსახმობის ჩასწორება
newtab-topsites-add-shortcut-label = მალსახმობის დამატება
newtab-topsites-title-label = დასახელება
newtab-topsites-title-input =
    .placeholder = სათაურის შეყვანა
newtab-topsites-url-label = URL-ბმული
newtab-topsites-url-input =
    .placeholder = აკრიფეთ ან ჩასვით URL-ბმული
newtab-topsites-url-validation = საჭიროა მართებული URL
newtab-topsites-image-url-label = სასურველი სურათის URL-ბმული
newtab-topsites-use-image-link = სასურველი სურათის გამოყენება…
newtab-topsites-image-validation = სურათი ვერ ჩაიტვირთა. სცადეთ სხვა URL-ბმული.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = გაუქმება
newtab-topsites-delete-history-button = ისტორიიდან ამოშლა
newtab-topsites-save-button = შენახვა
newtab-topsites-preview-button = შეთვალიერება
newtab-topsites-add-button = დამატება

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = ნამდვილად გსურთ, ამ გვერდის ყველა ჩანაწერის ისტორიიდან ამოშლა?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = ეს ქმედება შეუქცევადია.

## Top Sites - Sponsored label

newtab-topsite-sponsored = დამკვეთებისგან

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = მენიუს გახსნა
    .aria-label = მენიუს გახსნა
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = მოცილება
    .aria-label = მოცილება
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = მენიუს გახსნა
    .aria-label = კონტექსტური მენიუს გახსნა { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = საიტის ჩასწორება
    .aria-label = საიტის ჩასწორება

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = ჩასწორება
newtab-menu-open-new-window = ახალ ფანჯარაში გახსნა
newtab-menu-open-new-private-window = ახალ პირად ფანჯარაში გახსნა
newtab-menu-dismiss = დამალვა
newtab-menu-pin = მიმაგრება
newtab-menu-unpin = მოხსნა
newtab-menu-delete-history = ისტორიიდან ამოშლა
newtab-menu-save-to-pocket = { -pocket-brand-name }-ში შენახვა
newtab-menu-delete-pocket = წაშლა { -pocket-brand-name }-იდან
newtab-menu-archive-pocket = დაარქივება { -pocket-brand-name }-ში
newtab-menu-show-privacy-info = ჩვენი დამკვეთები და თქვენი პირადულობა
newtab-menu-about-fakespot = გაიცანით { -fakespot-brand-name }
newtab-menu-report-content = ამ მასალის გასაჩივრება
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = აკრძალვა
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = თემის მიდევნების შეწყვეტა

## Context menu options for sponsored stories and new ad formats on New Tab.

newtab-menu-manage-sponsored-content = შეკვეთილი მასალების მართვა
newtab-menu-our-sponsors-and-your-privacy = ჩვენი დამკვეთები და თქვენი პირადულობა
newtab-menu-report-this-ad = ამ რეკლამის გასაჩივრება

## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = მზადაა
newtab-privacy-modal-button-manage = შეკვეთილი მასალის პარამეტრების მართვა
newtab-privacy-modal-header = თქვენი პირადულობა უმნიშვნელოვანესია.
newtab-privacy-modal-paragraph-2 =
    გარდა საინტერესო ამბებისა, ასევე მოგაწვდით მნიშვნელოვან, ღირებულ
    მასალას შერჩეული დამკვეთებისგან. ნუ იღელვებთ, რასაც ათვალიერებთ, <strong>მხოლოდ ინახავს თქვენი კუთვნილი { -brand-product-name }</strong>  — ვერც ჩვენ და ვერც ჩვენი
    დამკვეთები ვერაფერს იხილავენ.
newtab-privacy-modal-link = იხილეთ, როგორ მუშაობს პირადი მონაცემების უსაფრთხოება ახალ ჩანართზე

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = სანიშნებიდან ამოშლა
# Bookmark is a verb here.
newtab-menu-bookmark = ჩანიშვნა

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = ჩამოტვირთვის ბმულის ასლი
newtab-menu-go-to-download-page = გადასვლა ჩამოტვირთვის გვერდზე
newtab-menu-remove-download = ისტორიიდან ამოშლა

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] ჩვენება Finder-ში
       *[other] შემცველი საქაღალდის გახსნა
    }
newtab-menu-open-file = ფაილის გახსნა

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = მონახულებული
newtab-label-bookmarked = ჩანიშნული
newtab-label-removed-bookmark = სანიშნი მოცილებულია
newtab-label-recommended = ფართოდ გავრცელებული
newtab-label-saved = შენახულია { -pocket-brand-name }-ში
newtab-label-download = ჩამოტვირთული
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · დაკვეთილი
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = დამკვეთია { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } წთ

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = ამ ნაწილის მოცილება
newtab-section-menu-collapse-section = ამ ნაწილის აკეცვა
newtab-section-menu-expand-section = ამ ნაწილის გაშლა
newtab-section-menu-manage-section = გვერდის ნაწილების მართვა
newtab-section-menu-manage-webext = გაფართოების მართვა
newtab-section-menu-add-topsite = რჩეული საიტის დამატება
newtab-section-menu-add-search-engine = საძიებო სისტემის დამატება
newtab-section-menu-move-up = აწევა
newtab-section-menu-move-down = ჩამოწევა
newtab-section-menu-privacy-notice = პირადი მონაცემების დაცვის განაცხადი

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = ამ ნაწილის აკეცვა
newtab-section-expand-section-label =
    .aria-label = ამ ნაწილის გაშლა

## Section Headers.

newtab-section-header-topsites = რჩეული საიტები
newtab-section-header-recent-activity = ბოლო მოქმედებები
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = { $provider } გირჩევთ
newtab-section-header-stories = ფიქრების აღმძვრელი ამბები
# "picks" refers to recommended articles
newtab-section-header-todays-picks = დღეს შერჩეული თქვენთვის

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = დაიწყეთ გვერდების დათვალიერება და აქ გამოჩნდება თქვენთვის სასურველი სტატიები, ვიდეოები და ბოლოს მონახულებული ან ჩანიშნული საიტები.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = უკვე ყველაფერს გაეცანით. მოგვიანებით შემოიარეთ მეტი რჩეული სტატიის სანახავად, რომელსაც { $provider } მოგაწვდით. ვერ ითმენთ? აირჩიეთ რომელიმე ფართოდ გავრცელებული საკითხი, ახალი საინტერესო სტატიების მოსაძიებლად.
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = უკვე ყველაფერს გაეცანით. მოგვიანებით შემოიარეთ მეტი რჩეული სტატიის სანახავად. ვერ ითმენთ? აირჩიეთ რომელიმე ფართოდ გავრცელებული საკითხი ახალი საინტერესო სტატიების მოსაძიებლად.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = ყველაფერი წაკითხულია!
newtab-discovery-empty-section-topstories-content = ახალი ამბების სანახავად, შეამოწმეთ მოგვიანებით.
newtab-discovery-empty-section-topstories-try-again-button = ხელახლა ცდა
newtab-discovery-empty-section-topstories-loading = იტვირთება...
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = ჰმ! თითქმის ჩაიტვირთა, მაგრამ სრულად არა.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = მეტად გავრცელებული საკითხები:
newtab-pocket-new-topics-title = გსურთ, მეტი ამბების მონახვა? იხილეთ, გავრცელებული თემებისთვის { -pocket-brand-name }
newtab-pocket-more-recommendations = მეტი შემოთავაზებები
newtab-pocket-learn-more = იხილეთ ვრცლად
newtab-pocket-cta-button = გამოიყენეთ { -pocket-brand-name }
newtab-pocket-cta-text = გადაინახეთ სასურველი შიგთავსი { -pocket-brand-name }-ში და მიეცით გონებას საკვები, შთამბეჭდავი საკითხავი მასალის სახით.
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } ეკუთვნის { -brand-product-name }-ოჯახს
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = შენახვა
newtab-pocket-saved = შენახულია

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = სხვა ამის მსგავსი
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = ჩემთვის არაა
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = გმადლობთ. თქვენი გამოხმაურება დაგვეხმარება სიახლეების არხის გაუმჯობესებაში.
newtab-toast-dismiss-button =
    .title = აცილება
    .aria-label = აცილება

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = აღმოაჩინეთ ვებსამყაროს საუკეთესო მხარე
newtab-pocket-onboarding-cta = { -pocket-brand-name } მოიძიებს მრავალფეროვან მასალებს, რათა თქვენს { -brand-product-name }-ბრაუზერში იხილოთ ყველაზე საინტერესო, შთამაგონებელი და სანდო შიგთავსი.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = სამწუხაროდ, შიგთავსის ჩატვირთვისას რაღაც ხარვეზი წარმოიქმნა.
newtab-error-fallback-refresh-link = განაახლეთ გვერდი და სცადეთ ხელახლა.

## Customization Menu

newtab-custom-shortcuts-title = მალსახმობები
newtab-custom-shortcuts-subtitle = საიტები, რომლებსაც ინახავთ ან სტუმრობთ
newtab-custom-shortcuts-toggle =
    .label = მალსახმობები
    .description = საიტები, რომლებსაც ინახავთ ან სტუმრობთ
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } რიგი
       *[other] { $num } რიგი
    }
newtab-custom-sponsored-sites = მალსახმობები დამკვეთებისგან
newtab-custom-pocket-title = გთავაზობთ { -pocket-brand-name }
newtab-custom-pocket-subtitle = გამორჩეული მასალები, რომელთაც { -pocket-brand-name } გთავაზობთ, { -brand-product-name }-ოჯახის ნაწილი.
newtab-custom-stories-toggle =
    .label = შემოთავაზებული ამბები
    .description = გამორჩეული მასალები, რომელთაც შეგირჩევთ { -brand-product-name }
newtab-custom-pocket-sponsored = ამბები დამკვეთებისგან
newtab-custom-pocket-show-recent-saves = ბოლოს შენახულის ჩვენება
newtab-custom-recent-title = ბოლო მოქმედებები
newtab-custom-recent-subtitle = ბოლოს ნანახი საიტებისა და მასალებიდან შერჩეული
newtab-custom-recent-toggle =
    .label = ბოლო მოქმედებები
    .description = ბოლოს ნანახი საიტებისა და მასალებიდან შერჩეული
newtab-custom-weather-toggle =
    .label = ამინდი
    .description = დღევანდელი ამინდისთვის თვალის შევლება
newtab-custom-close-button = დახურვა
newtab-custom-settings = დამატებითი პარამეტრების მართვა

## New Tab Wallpapers

newtab-wallpaper-title = ფონები
newtab-wallpaper-reset = ნაგულისხმევზე ჩამოყრა
newtab-wallpaper-upload-image = სურათის ატვირთვა
newtab-wallpaper-custom-color = ფერის არჩევა
# Variables
#   $file_size (number) - The number of the maximum image file size (in MB) that may be uploaded
newtab-wallpaper-error-max-file-size = სურათის ფაილის ზომა აღემატება ზღვარს { $file_size }ᲛᲑ. გთხოვთ, სცადოთ უფრო მცირე ფაილის ატვირთვა.
newtab-wallpaper-error-file-type = ვერ მოხერხდა თქვენი ფაილის ატვირთვა. გთხოვთ, კვლავ სცადოთ სხვა სახის ფაილით.
newtab-wallpaper-light-red-panda = წითელი პანდა
newtab-wallpaper-light-mountain = თეთრი მთა
newtab-wallpaper-light-sky = ცა მოიისფრო და მოვარდისფრო ღრუბლებით
newtab-wallpaper-light-color = ლურჯი, ვარდისფერი და ყვითელი ფორმები
newtab-wallpaper-light-landscape = ცისფერი ნისლი მთის ხედით
newtab-wallpaper-light-beach = სანაპირო პალმის ხით
newtab-wallpaper-dark-aurora = ჩრდილოეთის ციალი
newtab-wallpaper-dark-color = წითელი და ლურჯი ფორმები
newtab-wallpaper-dark-panda = წითელი პანდა იმალება ტყეში
newtab-wallpaper-dark-sky = ქალაქის ხედი ღამის ცით
newtab-wallpaper-dark-mountain = მთის ხედი
newtab-wallpaper-dark-city = ქალაქის მოიისფრო ხედი
newtab-wallpaper-dark-fox-anniversary = მელა ქვაფენილზე ტყის მახლობლად
newtab-wallpaper-light-fox-anniversary = მელა მდელოზე დაბურული მთის ხედით

## Solid Colors

newtab-wallpaper-category-title-colors = ერთგვაროვანი ფერები
newtab-wallpaper-blue = ლურჯი
newtab-wallpaper-light-blue = ცისფერი
newtab-wallpaper-light-purple = ღია იისფერი
newtab-wallpaper-light-green = ღია მწვანე
newtab-wallpaper-green = მწვანე
newtab-wallpaper-beige = ჩალისფერი
newtab-wallpaper-yellow = ყვითელი
newtab-wallpaper-orange = ნარინჯისფერი
newtab-wallpaper-pink = ვარდისფერი
newtab-wallpaper-light-pink = ღია ვარდისფერი
newtab-wallpaper-red = წითელი
newtab-wallpaper-dark-blue = მუქი ლურჯი
newtab-wallpaper-dark-purple = მუქი იისფერი
newtab-wallpaper-dark-green = მუქი მწვანე
newtab-wallpaper-brown = ყავისფერი

## Abstract

newtab-wallpaper-category-title-abstract = წარმოსახვითი
newtab-wallpaper-abstract-green = მწვანე ფორმები
newtab-wallpaper-abstract-blue = ლურჯი ფორმები
newtab-wallpaper-abstract-purple = იისფერი ფორმები
newtab-wallpaper-abstract-orange = ნარინჯისფერი ფორმები
newtab-wallpaper-gradient-orange = ნარინჯისფერი ვარდისფერში გადასული
newtab-wallpaper-abstract-blue-purple = ლურჯი და იისფერი ფორმები
newtab-wallpaper-abstract-white-curves = თეთრი ფერის ჩამუქებული მრუდებით
newtab-wallpaper-abstract-purple-green = იისფრიდან თანდათანობით მწვანე ნათებაში გადასული
newtab-wallpaper-abstract-blue-purple-waves = ლურჯი და იისფერი ტალღოვანი ფორმები
newtab-wallpaper-abstract-black-waves = შავი ტალღოვანი ფორმები

## Celestial

newtab-wallpaper-category-title-photographs = ფოტოსურათები
newtab-wallpaper-beach-at-sunrise = სანაპირო მზის ამოსვლისას
newtab-wallpaper-beach-at-sunset = სანაპირო მზის ჩასვლისას
newtab-wallpaper-storm-sky = ქარიშხლის ცა
newtab-wallpaper-sky-with-pink-clouds = ცა მოვარდისფრო ღრუბლებით
newtab-wallpaper-red-panda-yawns-in-a-tree = წითელი პანდა ამთქნარებს ხეზე
newtab-wallpaper-white-mountains = თოვლიანი მთები
newtab-wallpaper-hot-air-balloons = ფერადი საჰაერო ბუშტები დღისით
newtab-wallpaper-starry-canyon = ლურჯვარსკვლავთა ღამე
newtab-wallpaper-suspension-bridge = ნაცრისფერი კიდული ხიდის დღისით გადაღებული სურათი
newtab-wallpaper-sand-dunes = თეთრი ქვიშიანი ბორცვები
newtab-wallpaper-palm-trees = ქოქოსის პალმის ხეების მოხაზულობა შეღამებისას
newtab-wallpaper-blue-flowers = ახლო ხედით გადაღებული ლურჯგვირვინა ყვავილები გაფურჩქნისას
newtab-wallpaper-forest-trees = ნისლით დაფარული ხეები
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = სურათის გადამღებია <a data-l10n-name="name-link">{ $author_string }</a> საიტიდან <a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-header = შეაფერადეთ
newtab-wallpaper-feature-highlight-content = მიანიჭეთ განსხვავებული იერსახე თქვენს ახალ ჩანართს ფონის შეცვლით.
newtab-wallpaper-feature-highlight-button = გასაგებია
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = აცილება
    .aria-label = ამომხტომის დახურვა
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial

# “Celestial” referring to astronomy; positioned in or relating to the sky,
# or outer space as observed in astronomy.
# Not to be confused with religious definition of the word.
newtab-wallpaper-category-title-celestial = ციური სხეულები
newtab-wallpaper-celestial-lunar-eclipse = მთვარის დაბნელება
newtab-wallpaper-celestial-earth-night = ღამის სურათი დედამიწის ახლო ორბიტიდან
newtab-wallpaper-celestial-starry-sky = ვარსკვლავებიანი ცა
newtab-wallpaper-celestial-eclipse-time-lapse = მთვარის მიმდევრობითი დაბნელება
newtab-wallpaper-celestial-black-hole = შავი ხვრელის გალაქტიკური გამოსახულება
newtab-wallpaper-celestial-river = მდინარის თანამგზავრული გამოსახულება

## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = ამინდს გთავაზობთ { $provider }
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ დაკვეთილი
newtab-weather-menu-change-location = მდებარეობის შეცვლა
newtab-weather-change-location-search-input-placeholder =
    .placeholder = მდებარეობის მოძიება
    .aria-label = მდებარეობის მოძიება
newtab-weather-change-location-search-input = მდებარეობის მოძიება
newtab-weather-menu-weather-display = ამინდის ჩვენება
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = მარტივი
newtab-weather-menu-change-weather-display-simple = მარტივ ხედზე გადართვა
newtab-weather-menu-weather-display-option-detailed = ვრცელი
newtab-weather-menu-change-weather-display-detailed = ვრცელ ხედზე გადართვა
newtab-weather-menu-temperature-units = ტემპერატურის ერთეულები
newtab-weather-menu-temperature-option-fahrenheit = ფარენჰაიტი
newtab-weather-menu-temperature-option-celsius = ცელსიუსი
newtab-weather-menu-change-temperature-units-fahrenheit = ფარენჰაიტზე გადართვა
newtab-weather-menu-change-temperature-units-celsius = ცელსიუსზე გადართვა
newtab-weather-menu-hide-weather = ამინდის დამალვა ახალ ჩანართში
newtab-weather-menu-learn-more = ვრცლად
# This message is shown if user is working offline
newtab-weather-error-not-available = ამინდის მონაცემები მიუწვდომელია ახლა.

## Topic Labels

newtab-topic-label-business = საქმიანობა
newtab-topic-label-career = წინსვლა საქმეში
newtab-topic-label-education = განათლება
newtab-topic-label-arts = გართობა
newtab-topic-label-food = საკვები
newtab-topic-label-health = ჯანმრთელობა
newtab-topic-label-hobbies = თამაშები
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = ფული
newtab-topic-label-society-parenting = აღზრდა
newtab-topic-label-government = პოლიტიკა
newtab-topic-label-education-science = მეცნიერება
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = ცხოვრებისეული ხრიკები
newtab-topic-label-sports = სპორტი
newtab-topic-label-tech = ტექნოლოგია
newtab-topic-label-travel = მოგზაურობა
newtab-topic-label-home = სახლი და მებაღეობა

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = აირჩიეთ დარგი სიახლეების არხის მოსარგებად
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = აირჩიეთ ორი ან მეტი საკითხი. ჩვენი გამოცდილი ზედამხედველები შეარჩევენ თქვენზე მორგებულ მასალებს. შეგიძლიათ ნებისმიერ დროს შეცვალოთ.
newtab-topic-selection-save-button = შენახვა
newtab-topic-selection-cancel-button = გაუქმება
newtab-topic-selection-button-maybe-later = მოგვიანებით გადავწყვეტ
newtab-topic-selection-privacy-link = იხილეთ როგორ დაიცვათ და მართოთ მონაცემები
newtab-topic-selection-button-update-interests = განაახლეთ შერჩეული მისწრაფებები
newtab-topic-selection-button-pick-interests = აირციეთ მისწრაფებები

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = თვალის მიდევნება
newtab-section-following-button = გამოწერილი
newtab-section-unfollow-button = თვალის მიდევნების შეწყვეტა

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = შეზღუდვა
newtab-section-blocked-button = შეზღუდულია
newtab-section-unblock-button = შეზღუდვის მოხსნა

## Confirmation modal for blocking a section

newtab-section-cancel-button = ახლა არა
newtab-section-confirm-block-topic-p1 = ნამდვილად გსურთ ამ თემის შეზღუდვა?
newtab-section-confirm-block-topic-p2 = შეზღუდული თემები აღარ გამოჩნდება თქვენს არხში.
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = შეიზღუდოს { $topic }

## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-mangage-topics-title = თემები
newtab-section-manage-topics-button-v2 =
    .label = თემების მართვა
newtab-section-mangage-topics-followed-topics = მიდევნებული
newtab-section-mangage-topics-followed-topics-empty-state = თქვენ ჯერ არცერთ თემას არ ადევნებთ თვალს.
newtab-section-mangage-topics-blocked-topics = შეზღუდული
newtab-section-mangage-topics-blocked-topics-empty-state = თქვენ ჯერ არცერთი თემა არ შეგიზღუდავთ.
newtab-custom-wallpaper-title = მორგებული ფონები აქაა
# 'Make firefox yours" means to customize or personalize
newtab-custom-wallpaper-subtitle = ატვირთეთ საკუთარი ფონი ან შეარჩიეთ სასურველი ფერი, რომ გახადოთ { -brand-product-name } მეტად თქვენებური.
newtab-custom-wallpaper-cta = მოსინჯვა
