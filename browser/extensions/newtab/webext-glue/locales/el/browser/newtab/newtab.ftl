# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Νέα καρτέλα
newtab-settings-button =
    .title = Προσαρμογή της σελίδας Νέας Καρτέλας
newtab-personalize-settings-icon-label =
    .title = Εξατομίκευση νέας καρτέλας
    .aria-label = Ρυθμίσεις
newtab-settings-dialog-label =
    .aria-label = Ρυθμίσεις
newtab-personalize-icon-label =
    .title = Εξατομίκευση νέας καρτέλας
    .aria-label = Εξατομίκευση νέας καρτέλας
newtab-personalize-dialog-label =
    .aria-label = Εξατομίκευση
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Αναζήτηση
    .aria-label = Αναζήτηση
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Αναζήτηση με { $engine } ή εισαγωγή διεύθυνσης
newtab-search-box-handoff-text-no-engine = Αναζήτηση ή εισαγωγή διεύθυνσης
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Αναζήτηση με { $engine } ή εισαγωγή διεύθυνσης
    .title = Αναζήτηση με { $engine } ή εισαγωγή διεύθυνσης
    .aria-label = Αναζήτηση με { $engine } ή εισαγωγή διεύθυνσης
newtab-search-box-handoff-input-no-engine =
    .placeholder = Αναζήτηση ή εισαγωγή διεύθυνσης
    .title = Αναζήτηση ή εισαγωγή διεύθυνσης
    .aria-label = Αναζήτηση ή εισαγωγή διεύθυνσης
newtab-search-box-text = Αναζήτηση στο διαδίκτυο
newtab-search-box-input =
    .placeholder = Αναζήτηση στο διαδίκτυο
    .aria-label = Αναζήτηση στο διαδίκτυο

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Προσθήκη μηχανής αναζήτησης
newtab-topsites-add-shortcut-header = Νέα συντόμευση
newtab-topsites-edit-topsites-header = Επεξεργασία κορυφαίου ιστοτόπου
newtab-topsites-edit-shortcut-header = Επεξεργασία συντόμευσης
newtab-topsites-add-shortcut-label = Προσθήκη συντόμευσης
newtab-topsites-title-label = Τίτλος
newtab-topsites-title-input =
    .placeholder = Εισαγωγή τίτλου
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Εισαγωγή ή επικόλληση URL
newtab-topsites-url-validation = Απαιτείται έγκυρο URL
newtab-topsites-image-url-label = URL προσαρμοσμένης εικόνας
newtab-topsites-use-image-link = Χρήση προσαρμοσμένης εικόνας…
newtab-topsites-image-validation = Αποτυχία φόρτωσης εικόνας. Δοκιμάστε ένα διαφορετικό URL.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Ακύρωση
newtab-topsites-delete-history-button = Διαγραφή από ιστορικό
newtab-topsites-save-button = Αποθήκευση
newtab-topsites-preview-button = Προεπισκόπηση
newtab-topsites-add-button = Προσθήκη

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Θέλετε σίγουρα να διαγράψετε κάθε παρουσία της σελίδας από το ιστορικό σας;
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Δεν είναι δυνατή η αναίρεση αυτής της ενέργειας.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Χορηγία

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Άνοιγμα μενού
    .aria-label = Άνοιγμα μενού
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Αφαίρεση
    .aria-label = Αφαίρεση
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Άνοιγμα μενού
    .aria-label = Άνοιγμα μενού επιλογών για το { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Επεξεργασία ιστοτόπου
    .aria-label = Επεξεργασία ιστοτόπου

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Επεξεργασία
newtab-menu-open-new-window = Άνοιγμα σε νέο παράθυρο
newtab-menu-open-new-private-window = Άνοιγμα σε νέο ιδιωτικό παράθυρο
newtab-menu-dismiss = Απόρριψη
newtab-menu-pin = Καρφίτσωμα
newtab-menu-unpin = Ξεκαρφίτσωμα
newtab-menu-delete-history = Διαγραφή από ιστορικό
newtab-menu-save-to-pocket = Αποθήκευση στο { -pocket-brand-name }
newtab-menu-delete-pocket = Διαγραφή από το { -pocket-brand-name }
newtab-menu-archive-pocket = Αρχειοθέτηση στο { -pocket-brand-name }
newtab-menu-show-privacy-info = Οι χορηγοί μας και το απόρρητό σας
newtab-menu-about-fakespot = Σχετικά με το { -fakespot-brand-name }
newtab-menu-report-content = Αναφορά περιεχομένου
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = Φραγή
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = Άρση παρακολούθησης θέματος

## Context menu options for sponsored stories and new ad formats on New Tab.

newtab-menu-manage-sponsored-content = Διαχείριση χορηγούμενου περιεχομένου
newtab-menu-our-sponsors-and-your-privacy = Οι χορηγοί μας και το απόρρητό σας
newtab-menu-report-this-ad = Αναφορά διαφήμισης

## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Τέλος
newtab-privacy-modal-button-manage = Διαχείριση ρυθμίσεων χορηγούμενου περιεχομένου
newtab-privacy-modal-header = Το απόρρητό σας έχει σημασία.
newtab-privacy-modal-paragraph-2 =
    Εκτός από την παράδοση μαγευτικών ιστοριών, σας εμφανίζουμε σχετικό,
    υψηλής ποιότητας περιεχόμενο από επιλεγμένους χορηγούς. Μην ανησυχείτε, <strong>τα δεδομένα
    περιήγησής σας δεν φεύγουν ποτέ από το προσωπικό σας αντίγραφο του { -brand-product-name }</strong> — δεν τα βλέπουμε ούτε εμείς, ούτε
    οι χορηγοί μας.
newtab-privacy-modal-link = Μάθετε πώς λειτουργεί το απόρρητο στη νέα καρτέλα

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Αφαίρεση σελιδοδείκτη
# Bookmark is a verb here.
newtab-menu-bookmark = Προσθήκη σελιδοδείκτη

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Αντιγραφή συνδέσμου λήψης
newtab-menu-go-to-download-page = Μετάβαση στη σελίδα λήψης
newtab-menu-remove-download = Αφαίρεση από το ιστορικό

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Εμφάνιση στο Finder
       *[other] Άνοιγμα φακέλου λήψης
    }
newtab-menu-open-file = Άνοιγμα αρχείου

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Από ιστορικό
newtab-label-bookmarked = Από σελιδοδείκτες
newtab-label-removed-bookmark = Ο σελιδοδείκτης αφαιρέθηκε
newtab-label-recommended = Τάσεις
newtab-label-saved = Αποθηκεύτηκε στο { -pocket-brand-name }
newtab-label-download = Λήψεις
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · Χορηγία
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Χορηγία από { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } λεπ.

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Αφαίρεση ενότητας
newtab-section-menu-collapse-section = Σύμπτυξη ενότητας
newtab-section-menu-expand-section = Επέκταση ενότητας
newtab-section-menu-manage-section = Διαχείριση ενότητας
newtab-section-menu-manage-webext = Διαχείριση επέκτασης
newtab-section-menu-add-topsite = Προσθήκη κορυφαίου ιστοτόπου
newtab-section-menu-add-search-engine = Προσθήκη μηχανής αναζήτησης
newtab-section-menu-move-up = Μετακίνηση πάνω
newtab-section-menu-move-down = Μετακίνηση κάτω
newtab-section-menu-privacy-notice = Σημείωση απορρήτου

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Σύμπτυξη ενότητας
newtab-section-expand-section-label =
    .aria-label = Επέκταση ενότητας

## Section Headers.

newtab-section-header-topsites = Κορυφαίοι ιστότοποι
newtab-section-header-recent-activity = Πρόσφατη δραστηριότητα
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Προτάσεις του { $provider }
newtab-section-header-stories = Άρθρα που σας βάζουν σε σκέψεις
# "picks" refers to recommended articles
newtab-section-header-todays-picks = Σημερινές επιλογές για εσάς

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Ξεκινήστε την περιήγηση και θα σας δείξουμε μερικά υπέροχα άρθρα, βίντεο και άλλες σελίδες που έχετε επισκεφθεί πρόσφατα ή έχετε προσθέσει στους σελιδοδείκτες σας.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Δεν υπάρχει κάτι νεότερο. Ελέγξτε αργότερα για περισσότερες ιστορίες από τον πάροχο { $provider }. Δεν μπορείτε να περιμένετε; Διαλέξτε κάποιο από τα δημοφιλή θέματα και ανακαλύψτε ενδιαφέρουσες ιστορίες από όλο τον Ιστό.
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = Δεν υπάρχει κάτι νεότερο. Ελέγξτε αργότερα για περισσότερα άρθρα. Δεν μπορείτε να περιμένετε; Επιλέξτε κάποιο δημοφιλές θέμα και βρείτε ακόμα περισσότερα ενδιαφέροντα άρθρα από όλο το διαδίκτυο.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Τελειώσατε!
newtab-discovery-empty-section-topstories-content = Ελέγξτε ξανά αργότερα για περισσότερες ιστορίες.
newtab-discovery-empty-section-topstories-try-again-button = Δοκιμή ξανά
newtab-discovery-empty-section-topstories-loading = Φόρτωση…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Ωχ! Αυτή η ενότητα σχεδόν φορτώθηκε, αλλά όχι πλήρως.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Δημοφιλή θέματα:
newtab-pocket-new-topics-title = Θέλετε περισσότερα άρθρα; Δείτε αυτά τα δημοφιλή θέματα από το { -pocket-brand-name }
newtab-pocket-more-recommendations = Περισσότερες προτάσεις
newtab-pocket-learn-more = Μάθετε περισσότερα
newtab-pocket-cta-button = Αποκτήστε το { -pocket-brand-name }
newtab-pocket-cta-text = Αποθηκεύστε τις ιστορίες που αγαπάτε στο { -pocket-brand-name } και τροφοδοτήστε το μυαλό σας με εκπληκτικά κείμενα.
newtab-pocket-pocket-firefox-family = Το { -pocket-brand-name } ανήκει στην οικογένεια του { -brand-product-name }
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = Αποθήκευση
newtab-pocket-saved = Αποθηκεύτηκε

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = Περισσότερα σαν κι αυτό
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = Όχι για μένα
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = Ευχαριστούμε. Τα σχόλιά σας θα μας βοηθήσουν να βελτιώσουμε τη ροή σας.
newtab-toast-dismiss-button =
    .title = Απόρριψη
    .aria-label = Απόρριψη

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Ανακαλύψτε τα καλύτερα του διαδικτύου
newtab-pocket-onboarding-cta = Το { -pocket-brand-name } εξερευνά μια μεγάλη γκάμα εκδόσεων για να μεταφέρει το πιο ενημερωτικό, εμπνευσμένο και αξιόπιστο περιεχόμενο στο πρόγραμμα περιήγησης { -brand-product-name } σας.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Ωχ, κάτι πήγε στραβά κατά τη φόρτωση του περιεχομένου.
newtab-error-fallback-refresh-link = Ανανεώστε τη σελίδα για να δοκιμάσετε ξανά.

## Customization Menu

newtab-custom-shortcuts-title = Συντομεύσεις
newtab-custom-shortcuts-subtitle = Ιστότοποι από σελιδοδείκτες ή ιστορικό
newtab-custom-shortcuts-toggle =
    .label = Συντομεύσεις
    .description = Ιστότοποι από σελιδοδείκτες ή ιστορικό
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } σειρά
       *[other] { $num } σειρές
    }
newtab-custom-sponsored-sites = Χορηγούμενες συντομεύσεις
newtab-custom-pocket-title = Προτείνεται από το { -pocket-brand-name }
newtab-custom-pocket-subtitle = Εξαιρετικό περιεχόμενο από το { -pocket-brand-name }, μέρος της οικογένειας του { -brand-product-name }
newtab-custom-stories-toggle =
    .label = Προτεινόμενα άρθρα
    .description = Εξαιρετικό περιεχόμενο από την οικογένεια του { -brand-product-name }
newtab-custom-pocket-sponsored = Χορηγούμενα άρθρα
newtab-custom-pocket-show-recent-saves = Εμφάνιση πρόσφατων αποθηκεύσεων
newtab-custom-recent-title = Πρόσφατη δραστηριότητα
newtab-custom-recent-subtitle = Συλλογή πρόσφατων ιστοτόπων και περιεχομένου
newtab-custom-recent-toggle =
    .label = Πρόσφατη δραστηριότητα
    .description = Συλλογή πρόσφατων ιστοτόπων και περιεχομένου
newtab-custom-weather-toggle =
    .label = Καιρός
    .description = Σημερινή πρόγνωση με μια ματιά
newtab-custom-close-button = Κλείσιμο
newtab-custom-settings = Διαχείριση περισσότερων ρυθμίσεων

## New Tab Wallpapers

newtab-wallpaper-title = Ταπετσαρίες
newtab-wallpaper-reset = Επαναφορά προεπιλογής
newtab-wallpaper-upload-image = Μεταφόρτωση εικόνας
newtab-wallpaper-custom-color = Επιλογή χρώματος
# Variables
#   $file_size (number) - The number of the maximum image file size (in MB) that may be uploaded
newtab-wallpaper-error-max-file-size = Η εικόνα υπερέβη το όριο μεγέθους των { $file_size }MB. Δοκιμάστε να μεταφορτώσετε ένα μικρότερο αρχείο.
newtab-wallpaper-error-file-type = Δεν ήταν δυνατή η μεταφόρτωση του αρχείου σας. Δοκιμάστε ξανά με έναν άλλο τύπο αρχείου.
newtab-wallpaper-light-red-panda = Κόκκινο πάντα
newtab-wallpaper-light-mountain = Λευκό βουνό
newtab-wallpaper-light-sky = Ουρανός με μωβ και ροζ σύννεφα
newtab-wallpaper-light-color = Μπλε, ροζ και κίτρινα σχήματα
newtab-wallpaper-light-landscape = Μπλε ορεινό τοπίο ομίχλης
newtab-wallpaper-light-beach = Παραλία με φοίνικα
newtab-wallpaper-dark-aurora = Βόρειο σέλας
newtab-wallpaper-dark-color = Κόκκινα και μπλε σχήματα
newtab-wallpaper-dark-panda = Κόκκινο πάντα στο δάσος
newtab-wallpaper-dark-sky = Αστικό τοπίο με νυχτερινό ουρανό
newtab-wallpaper-dark-mountain = Ορεινό τοπίο
newtab-wallpaper-dark-city = Μωβ αστικό τοπίο
newtab-wallpaper-dark-fox-anniversary = Μια αλεπού στο πεζοδρόμιο κοντά σε ένα δάσος
newtab-wallpaper-light-fox-anniversary = Μια αλεπού μέσα σε γρασίδι, με ένα ομιχλώδες ορεινό τοπίο

## Solid Colors

newtab-wallpaper-category-title-colors = Συμπαγή χρώματα
newtab-wallpaper-blue = Μπλε
newtab-wallpaper-light-blue = Ανοιχτό μπλε
newtab-wallpaper-light-purple = Ανοιχτό μωβ
newtab-wallpaper-light-green = Ανοιχτό πράσινο
newtab-wallpaper-green = Πράσινο
newtab-wallpaper-beige = Μπεζ
newtab-wallpaper-yellow = Κίτρινο
newtab-wallpaper-orange = Πορτοκαλί
newtab-wallpaper-pink = Ροζ
newtab-wallpaper-light-pink = Ανοιχτό ροζ
newtab-wallpaper-red = Κόκκινο
newtab-wallpaper-dark-blue = Σκούρο μπλε
newtab-wallpaper-dark-purple = Σκούρο μωβ
newtab-wallpaper-dark-green = Σκούρο πράσινο
newtab-wallpaper-brown = Καφέ

## Abstract

newtab-wallpaper-category-title-abstract = Αφηρημένο
newtab-wallpaper-abstract-green = Πράσινα σχήματα
newtab-wallpaper-abstract-blue = Μπλε σχήματα
newtab-wallpaper-abstract-purple = Μωβ σχήματα
newtab-wallpaper-abstract-orange = Πορτοκαλί σχήματα
newtab-wallpaper-gradient-orange = Διαβάθμιση πορτοκαλί και ροζ
newtab-wallpaper-abstract-blue-purple = Μπλε και μωβ σχήματα
newtab-wallpaper-abstract-white-curves = Λευκό με σκιασμένες καμπύλες
newtab-wallpaper-abstract-purple-green = Διαβάθμιση μωβ και πράσινου φωτός
newtab-wallpaper-abstract-blue-purple-waves = Μπλε και μωβ κυματιστές μορφές
newtab-wallpaper-abstract-black-waves = Μαύρες κυματιστές μορφές

## Celestial

newtab-wallpaper-category-title-photographs = Φωτογραφίες
newtab-wallpaper-beach-at-sunrise = Παραλία στην ανατολή του ήλιου
newtab-wallpaper-beach-at-sunset = Παραλία στη δύση του ήλιου
newtab-wallpaper-storm-sky = Ουρανός με καταιγίδα
newtab-wallpaper-sky-with-pink-clouds = Ουρανός με ροζ σύννεφα
newtab-wallpaper-red-panda-yawns-in-a-tree = Κόκκινο πάντα που χασμουριέται σε ένα δέντρο
newtab-wallpaper-white-mountains = Λευκά βουνά
newtab-wallpaper-hot-air-balloons = Αερόστατα διάφορων χρωμάτων στο φως της ημέρας
newtab-wallpaper-starry-canyon = Μπλε έναστρη νύχτα
newtab-wallpaper-suspension-bridge = Γκρι φωτογραφία με μια κρεμαστή γέφυρα κατά τη διάρκεια της ημέρας
newtab-wallpaper-sand-dunes = Λευκοί αμμόλοφοι
newtab-wallpaper-palm-trees = Φιγούρες κοκοφοινίκων κατά τη «χρυσή ώρα»
newtab-wallpaper-blue-flowers = Κοντινή φωτογραφία ανθισμένων λουλουδιών με μπλε πέταλα
newtab-wallpaper-forest-trees = Δέντρα που καλύπτονται από ομίχλη
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = Φωτογραφία από <a data-l10n-name="name-link">{ $author_string }</a> στο <a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-header = Δοκιμάστε μια πινελιά χρώματος
newtab-wallpaper-feature-highlight-content = Δώστε νέα εμφάνιση στη νέα σας καρτέλα με ταπετσαρίες.
newtab-wallpaper-feature-highlight-button = Το κατάλαβα
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = Απόρριψη
    .aria-label = Κλείσιμο αναδυόμενου παραθύρου
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial

# “Celestial” referring to astronomy; positioned in or relating to the sky,
# or outer space as observed in astronomy.
# Not to be confused with religious definition of the word.
newtab-wallpaper-category-title-celestial = Ουράνια
newtab-wallpaper-celestial-lunar-eclipse = Έκλειψη Σελήνης
newtab-wallpaper-celestial-earth-night = Νυχτερινή φωτογραφία από τη χαμηλή τροχιά της Γης
newtab-wallpaper-celestial-starry-sky = Έναστρος ουρανός
newtab-wallpaper-celestial-eclipse-time-lapse = Έκλειψη Σελήνης σε βαθμιαία παρέλευση χρόνου
newtab-wallpaper-celestial-black-hole = Εικονογράφηση γαλαξία με μια μαύρη τρύπα
newtab-wallpaper-celestial-river = Δορυφορική εικόνα ποταμού

## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = Προβολή πρόγνωσης στο { $provider }
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ Χορηγία
newtab-weather-menu-change-location = Αλλαγή τοποθεσίας
newtab-weather-change-location-search-input-placeholder =
    .placeholder = Αναζήτηση τοποθεσίας
    .aria-label = Αναζήτηση τοποθεσίας
newtab-weather-change-location-search-input = Αναζήτηση τοποθεσίας
newtab-weather-menu-weather-display = Προβολή καιρού
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = Απλή
newtab-weather-menu-change-weather-display-simple = Εναλλαγή σε απλή προβολή
newtab-weather-menu-weather-display-option-detailed = Λεπτομερής
newtab-weather-menu-change-weather-display-detailed = Εναλλαγή σε λεπτομερή προβολή
newtab-weather-menu-temperature-units = Μονάδες θερμοκρασίας
newtab-weather-menu-temperature-option-fahrenheit = Φαρενάιτ
newtab-weather-menu-temperature-option-celsius = Κελσίου
newtab-weather-menu-change-temperature-units-fahrenheit = Εναλλαγή σε Φαρενάιτ
newtab-weather-menu-change-temperature-units-celsius = Εναλλαγή σε Κελσίου
newtab-weather-menu-hide-weather = Απόκρυψη καιρού στη νέα καρτέλα
newtab-weather-menu-learn-more = Μάθετε περισσότερα
# This message is shown if user is working offline
newtab-weather-error-not-available = Τα δεδομένα καιρού δεν είναι διαθέσιμα αυτήν τη στιγμή.

## Topic Labels

newtab-topic-label-business = Επιχειρήσεις
newtab-topic-label-career = Καριέρα
newtab-topic-label-education = Εκπαίδευση
newtab-topic-label-arts = Ψυχαγωγία
newtab-topic-label-food = Φαγητό
newtab-topic-label-health = Υγεία
newtab-topic-label-hobbies = Παιχνίδια
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = Οικονομικά
newtab-topic-label-society-parenting = Ανατροφή παιδιών
newtab-topic-label-government = Πολιτική
newtab-topic-label-education-science = Επιστήμη
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = Αυτοβελτίωση
newtab-topic-label-sports = Αθλήματα
newtab-topic-label-tech = Τεχνολογία
newtab-topic-label-travel = Ταξίδια
newtab-topic-label-home = Σπίτι και κήπος

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = Επιλέξτε θέματα για να βελτιώσετε τη ροή σας
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = Επιλέξτε δύο ή περισσότερα θέματα. Οι ειδικοί επιμελητές μας δίνουν προτεραιότητα σε άρθρα που ταιριάζουν με τα ενδιαφέροντά σας. Κάντε ενημέρωση ανά πάσα στιγμή.
newtab-topic-selection-save-button = Αποθήκευση
newtab-topic-selection-cancel-button = Ακύρωση
newtab-topic-selection-button-maybe-later = Ίσως αργότερα
newtab-topic-selection-privacy-link = Μάθετε πώς προστατεύουμε και διαχειριζόμαστε τα δεδομένα
newtab-topic-selection-button-update-interests = Ενημερώστε τα ενδιαφέροντά σας
newtab-topic-selection-button-pick-interests = Επιλέξτε τα ενδιαφέροντά σας

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = Ακολουθήστε
newtab-section-following-button = Ακολουθείται
newtab-section-unfollow-button = Άρση παρακολούθησης

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = Φραγή
newtab-section-blocked-button = Φραγή
newtab-section-unblock-button = Άρση φραγής

## Confirmation modal for blocking a section

newtab-section-cancel-button = Όχι τώρα
newtab-section-confirm-block-topic-p1 = Θέλετε σίγουρα να αποκλείσετε αυτό το θέμα;
newtab-section-confirm-block-topic-p2 = Τα αποκλεισμένα θέματα δεν θα εμφανίζονται πλέον στη ροή σας.
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = Αποκλεισμός του «{ $topic }»

## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-mangage-topics-title = Θέματα
newtab-section-manage-topics-button-v2 =
    .label = Διαχείριση θεμάτων
newtab-section-mangage-topics-followed-topics = Ακολουθούνται
newtab-section-mangage-topics-followed-topics-empty-state = Δεν έχετε παρακολουθήσει κανένα θέμα ακόμα.
newtab-section-mangage-topics-blocked-topics = Αποκλεισμένα
newtab-section-mangage-topics-blocked-topics-empty-state = Δεν έχετε αποκλείσει κανένα θέμα ακόμα.
newtab-custom-wallpaper-title = Οι προσαρμοσμένες ταπετσαρίες έφτασαν
# 'Make firefox yours" means to customize or personalize
newtab-custom-wallpaper-subtitle = Μεταφορτώστε τη δική σας ταπετσαρία ή επιλέξτε ένα προσαρμοσμένο χρώμα για να κάνετε το { -brand-product-name } δικό σας.
newtab-custom-wallpaper-cta = Δοκιμή
