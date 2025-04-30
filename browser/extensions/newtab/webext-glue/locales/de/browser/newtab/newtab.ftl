# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Neuer Tab
newtab-settings-button =
    .title = Einstellungen für neue Tabs anpassen
newtab-personalize-settings-icon-label =
    .title = Neuen Tab anpassen
    .aria-label = Einstellungen
newtab-settings-dialog-label =
    .aria-label = Einstellungen
newtab-personalize-icon-label =
    .title = Neuen Tab anpassen
    .aria-label = Neuen Tab anpassen
newtab-personalize-dialog-label =
    .aria-label = Anpassen
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Suchen
    .aria-label = Suchen
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Mit { $engine } suchen oder Adresse eingeben
newtab-search-box-handoff-text-no-engine = Suche oder Adresse eingeben
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Mit { $engine } suchen oder Adresse eingeben
    .title = Mit { $engine } suchen oder Adresse eingeben
    .aria-label = Mit { $engine } suchen oder Adresse eingeben
newtab-search-box-handoff-input-no-engine =
    .placeholder = Suche oder Adresse eingeben
    .title = Suche oder Adresse eingeben
    .aria-label = Suche oder Adresse eingeben
newtab-search-box-text = Das Web durchsuchen
newtab-search-box-input =
    .placeholder = Das Web durchsuchen
    .aria-label = Das Web durchsuchen

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Suchmaschine hinzufügen
newtab-topsites-add-shortcut-header = Neue Verknüpfung
newtab-topsites-edit-topsites-header = Wichtige Seite bearbeiten
newtab-topsites-edit-shortcut-header = Verknüpfung bearbeiten
newtab-topsites-add-shortcut-label = Verknüpfung hinzufügen
newtab-topsites-title-label = Titel
newtab-topsites-title-input =
    .placeholder = Name eingeben
newtab-topsites-url-label = Adresse
newtab-topsites-url-input =
    .placeholder = Eine Adresse eingeben oder einfügen
newtab-topsites-url-validation = Gültige Adresse erforderlich
newtab-topsites-image-url-label = Adresse von benutzerdefinierter Grafik
newtab-topsites-use-image-link = Eine benutzerdefinierte Grafik verwenden…
newtab-topsites-image-validation = Grafik konnte nicht geladen werden. Verwenden Sie eine andere Adresse.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Abbrechen
newtab-topsites-delete-history-button = Aus Chronik löschen
newtab-topsites-save-button = Speichern
newtab-topsites-preview-button = Vorschau
newtab-topsites-add-button = Hinzufügen

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Soll wirklich jede Instanz dieser Seite aus Ihrer Chronik gelöscht werden?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Diese Aktion kann nicht rückgängig gemacht werden.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Gesponsert

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Menü öffnen
    .aria-label = Menü öffnen
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Entfernen
    .aria-label = Entfernen
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Menü öffnen
    .aria-label = Kontextmenü für { $title } öffnen
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Diese Website bearbeiten
    .aria-label = Diese Website bearbeiten

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Bearbeiten
newtab-menu-open-new-window = In neuem Fenster öffnen
newtab-menu-open-new-private-window = In neuem privaten Fenster öffnen
newtab-menu-dismiss = Entfernen
newtab-menu-pin = Anheften
newtab-menu-unpin = Ablösen
newtab-menu-delete-history = Aus Chronik löschen
newtab-menu-save-to-pocket = Bei { -pocket-brand-name } speichern
newtab-menu-delete-pocket = Aus { -pocket-brand-name } löschen
newtab-menu-archive-pocket = In { -pocket-brand-name } archivieren
newtab-menu-show-privacy-info = Unsere Sponsoren & Ihre Privatsphäre
newtab-menu-about-fakespot = Über { -fakespot-brand-name }
newtab-menu-report-content = Diesen Inhalt melden
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = Blockieren
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = Thema nicht mehr folgen

## Context menu options for sponsored stories and new ad formats on New Tab.

newtab-menu-manage-sponsored-content = Gesponserte Inhalte verwalten
newtab-menu-our-sponsors-and-your-privacy = Unsere Sponsoren und Ihre Privatsphäre
newtab-menu-report-this-ad = Diese Anzeige melden

## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Fertig
newtab-privacy-modal-button-manage = Einstellungen für gesponserte Inhalte
newtab-privacy-modal-header = Ihre Privatsphäre ist wichtig.
newtab-privacy-modal-paragraph-2 =
    Neben spannenden Geschichten zeigen wir Ihnen auch relevante,
    geprüfte Inhalte von ausgewählten Sponsoren. <strong>Ihre 
    Surf-Daten verlassen niemals Ihre { -brand-product-name }-Installation</strong> — wir sehen sie nicht und unsere
    Sponsoren auch nicht.
newtab-privacy-modal-link = Wie Datenschutz für die Tab-Startseite funktioniert

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Lesezeichen entfernen
# Bookmark is a verb here.
newtab-menu-bookmark = Lesezeichen

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Download-Link kopieren
newtab-menu-go-to-download-page = Zur Download-Seite gehen
newtab-menu-remove-download = Aus Chronik entfernen

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Im Finder anzeigen
       *[other] Beinhaltenden Ordner öffnen
    }
newtab-menu-open-file = Datei öffnen

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Besucht
newtab-label-bookmarked = Lesezeichen
newtab-label-removed-bookmark = Lesezeichen entfernt
newtab-label-recommended = Beliebt
newtab-label-saved = Bei { -pocket-brand-name } gespeichert
newtab-label-download = Heruntergeladen
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · Gesponsert
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Werbung von { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } min

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Abschnitt entfernen
newtab-section-menu-collapse-section = Abschnitt einklappen
newtab-section-menu-expand-section = Abschnitt ausklappen
newtab-section-menu-manage-section = Abschnitt verwalten
newtab-section-menu-manage-webext = Erweiterung verwalten
newtab-section-menu-add-topsite = Wichtige Seite hinzufügen
newtab-section-menu-add-search-engine = Suchmaschine hinzufügen
newtab-section-menu-move-up = Nach oben schieben
newtab-section-menu-move-down = Nach unten schieben
newtab-section-menu-privacy-notice = Datenschutzhinweis

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Abschnitt einklappen
newtab-section-expand-section-label =
    .aria-label = Abschnitt ausklappen

## Section Headers.

newtab-section-header-topsites = Wichtige Seiten
newtab-section-header-recent-activity = Neueste Aktivität
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Empfohlen von { $provider }
newtab-section-header-stories = Geschichten, die zum Nachdenken anregen
# "picks" refers to recommended articles
newtab-section-header-todays-picks = Unsere heutigen Tipps für Sie

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Surfen Sie los und wir zeigen Ihnen hier einige der interessanten Artikel, Videos und anderen Seiten, die Sie kürzlich besucht oder als Lesezeichen gespeichert haben.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Jetzt kennen Sie die Neuigkeiten. Schauen Sie später wieder vorbei, um neue Informationen von { $provider } zu erhalten. Können Sie nicht warten? Wählen Sie ein beliebtes Thema und lesen Sie weitere interessante Geschichten aus dem Internet.
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = Jetzt kennen Sie die Neuigkeiten. Schauen Sie später wieder vorbei, um neue Artikel zu erhalten. Können Sie nicht warten? Wählen Sie ein beliebtes Thema und lesen Sie weitere interessante Geschichten aus dem Internet.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Alle Artikel gelesen
newtab-discovery-empty-section-topstories-content = Öffnen Sie diese Seite später ein weiteres Mal, um neue Artikel angezeigt zu bekommen.
newtab-discovery-empty-section-topstories-try-again-button = Erneut versuchen
newtab-discovery-empty-section-topstories-loading = Wird geladen…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Leider ist ein Fehler beim Laden des Abschnitts aufgetreten.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Beliebte Themen:
newtab-pocket-new-topics-title = Sie wollen noch mehr Artikel? Sehen Sie sich diese beliebten Themen von { -pocket-brand-name } an
newtab-pocket-more-recommendations = Mehr Empfehlungen
newtab-pocket-learn-more = Weitere Informationen
newtab-pocket-cta-button = { -pocket-brand-name } holen
newtab-pocket-cta-text = Speichern Sie Ihre Lieblingstexte in { -pocket-brand-name } und gewinnen Sie gedankenreiche Einblicke durch faszinierende Texte.
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } ist Teil der { -brand-product-name }-Familie
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = Speichern
newtab-pocket-saved = Gespeichert

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = Mehr solcher Artikel
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = Nichts für mich
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = Vielen Dank. Ihr Feedback hilft uns, Ihren Feed zu verbessern.
newtab-toast-dismiss-button =
    .title = Schließen
    .aria-label = Schließen

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Entdecken Sie das Beste des Internets
newtab-pocket-onboarding-cta = { -pocket-brand-name } durchsucht eine Vielzahl von Veröffentlichungen, um die informativsten, inspirierendsten und vertrauenswürdigsten Inhalte direkt in Ihren { -brand-product-name }-Browser zu bringen.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Beim Laden dieses Inhalts ist ein Fehler aufgetreten.
newtab-error-fallback-refresh-link = Aktualisieren Sie die Seite, um es erneut zu versuchen.

## Customization Menu

newtab-custom-shortcuts-title = Verknüpfungen
newtab-custom-shortcuts-subtitle = Websites, die Sie speichern oder besuchen
newtab-custom-shortcuts-toggle =
    .label = Verknüpfungen
    .description = Websites, die Sie speichern oder besuchen
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } Zeile
       *[other] { $num } Zeilen
    }
newtab-custom-sponsored-sites = Gesponserte Verknüpfungen
newtab-custom-pocket-title = Empfohlen von { -pocket-brand-name }
newtab-custom-pocket-subtitle = Besondere Inhalte ausgewählt von { -pocket-brand-name }, Teil der { -brand-product-name }-Familie
newtab-custom-stories-toggle =
    .label = Empfohlene Geschichten
    .description = Besondere Inhalte ausgewählt von der { -brand-product-name }-Familie
newtab-custom-pocket-sponsored = Gesponserte Inhalte
newtab-custom-pocket-show-recent-saves = Zuletzt hinzugefügte Einträge anzeigen
newtab-custom-recent-title = Neueste Aktivität
newtab-custom-recent-subtitle = Eine Auswahl kürzlich besuchter Websites und Inhalte
newtab-custom-recent-toggle =
    .label = Neueste Aktivität
    .description = Eine Auswahl kürzlich besuchter Websites und Inhalte
newtab-custom-weather-toggle =
    .label = Wetter
    .description = Heutige Vorhersage auf einen Blick
newtab-custom-close-button = Schließen
newtab-custom-settings = Weitere Einstellungen verwalten

## New Tab Wallpapers

newtab-wallpaper-title = Hintergrundbilder
newtab-wallpaper-reset = Standard wiederherstellen
newtab-wallpaper-upload-image = Grafik hochladen
newtab-wallpaper-custom-color = Farbe auswählen
# Variables
#   $file_size (number) - The number of the maximum image file size (in MB) that may be uploaded
newtab-wallpaper-error-max-file-size = Die Grafik hat die Größenbegrenzung von { $file_size } MB überschritten. Bitte versuchen Sie, eine kleinere Datei hochzuladen.
newtab-wallpaper-error-file-type = Wir konnten Ihre Datei nicht hochladen. Bitte versuchen Sie es erneut mit einem anderen Dateityp.
newtab-wallpaper-light-red-panda = Roter Panda
newtab-wallpaper-light-mountain = Weißer Berg
newtab-wallpaper-light-sky = Himmel mit violetten und rosafarbenen Wolken
newtab-wallpaper-light-color = Blaue, rosa und gelbe Formen
newtab-wallpaper-light-landscape = Berglandschaft mit blauem Nebel
newtab-wallpaper-light-beach = Strand mit Palme
newtab-wallpaper-dark-aurora = Aurora Borealis
newtab-wallpaper-dark-color = Rote und blaue Formen
newtab-wallpaper-dark-panda = Roter Panda im Wald versteckt
newtab-wallpaper-dark-sky = Stadtlandschaft mit Nachthimmel
newtab-wallpaper-dark-mountain = Berg in der Landschaft
newtab-wallpaper-dark-city = Violette Stadtlandschaft
newtab-wallpaper-dark-fox-anniversary = Ein Fuchs auf einer Straße in der Nähe eines Waldes
newtab-wallpaper-light-fox-anniversary = Ein Fuchs auf einer Weide vor einer nebligen Berglandschaft

## Solid Colors

newtab-wallpaper-category-title-colors = Einfarbig
newtab-wallpaper-blue = Blau
newtab-wallpaper-light-blue = Hellblau
newtab-wallpaper-light-purple = Helllila
newtab-wallpaper-light-green = Hellgrün
newtab-wallpaper-green = Grün
newtab-wallpaper-beige = Beige
newtab-wallpaper-yellow = Gelb
newtab-wallpaper-orange = Orange
newtab-wallpaper-pink = Rosa
newtab-wallpaper-light-pink = Hellrosa
newtab-wallpaper-red = Rot
newtab-wallpaper-dark-blue = Dunkelblau
newtab-wallpaper-dark-purple = Dunkellila
newtab-wallpaper-dark-green = Dunkelgrün
newtab-wallpaper-brown = Braun

## Abstract

newtab-wallpaper-category-title-abstract = Abstrakt
newtab-wallpaper-abstract-green = Grüne Formen
newtab-wallpaper-abstract-blue = Blaue Formen
newtab-wallpaper-abstract-purple = Lila Formen
newtab-wallpaper-abstract-orange = Orangefarbene Formen
newtab-wallpaper-gradient-orange = Farbverlauf orange und rosa
newtab-wallpaper-abstract-blue-purple = Blaue und lila Formen
newtab-wallpaper-abstract-white-curves = Weiß mit schattierten Rundungen
newtab-wallpaper-abstract-purple-green = Lilafarbener und grüner Lichtverlauf
newtab-wallpaper-abstract-blue-purple-waves = Blaue und lila gewellte Formen
newtab-wallpaper-abstract-black-waves = Schwarze gewellte Formen

## Celestial

newtab-wallpaper-category-title-photographs = Fotos
newtab-wallpaper-beach-at-sunrise = Strand bei Sonnenaufgang
newtab-wallpaper-beach-at-sunset = Strand bei Sonnenuntergang
newtab-wallpaper-storm-sky = Gewitterhimmel
newtab-wallpaper-sky-with-pink-clouds = Himmel mit rosafarbenen Wolken
newtab-wallpaper-red-panda-yawns-in-a-tree = Roter Panda gähnt auf einem Baum
newtab-wallpaper-white-mountains = Weiße Berge
newtab-wallpaper-hot-air-balloons = Heißluftballons in verschiedenen Farben bei Tag
newtab-wallpaper-starry-canyon = Blaue sternenklare Nacht
newtab-wallpaper-suspension-bridge = Graue Fotografie einer Hängebrücke bei Tag
newtab-wallpaper-sand-dunes = Weiße Sanddünen
newtab-wallpaper-palm-trees = Silhouette von Kokospalmen zur Goldenen Stunde
newtab-wallpaper-blue-flowers = Detailaufnahmen von blühenden blauen Blumen
newtab-wallpaper-forest-trees = Bäume im Nebel
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = Foto von <a data-l10n-name="name-link">{ $author_string }</a> auf <a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-header = Wie wäre es mit einem Farbtupfer?
newtab-wallpaper-feature-highlight-content = Geben Sie Ihrem neuen Tab einen frischen Anstrich mit Hintergrundbildern.
newtab-wallpaper-feature-highlight-button = Verstanden
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = Verwerfen
    .aria-label = Pop-up schließen
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial

# “Celestial” referring to astronomy; positioned in or relating to the sky,
# or outer space as observed in astronomy.
# Not to be confused with religious definition of the word.
newtab-wallpaper-category-title-celestial = Astronomie
newtab-wallpaper-celestial-lunar-eclipse = Mondfinsternis
newtab-wallpaper-celestial-earth-night = Nachtfoto aus der niedrigen Erdumlaufbahn
newtab-wallpaper-celestial-starry-sky = Sternenhimmel
newtab-wallpaper-celestial-eclipse-time-lapse = Zeitraffer zur Mondfinsternis
newtab-wallpaper-celestial-black-hole = Illustration von Galaxie mit schwarzem Loch
newtab-wallpaper-celestial-river = Satellitenbild eines Flusses

## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = Vorhersage in { $provider } ansehen
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ Gesponsert
newtab-weather-menu-change-location = Standort ändern
newtab-weather-change-location-search-input-placeholder =
    .placeholder = Standort suchen
    .aria-label = Standort suchen
newtab-weather-change-location-search-input = Standort suchen
newtab-weather-menu-weather-display = Wetteranzeige
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = Einfach
newtab-weather-menu-change-weather-display-simple = Zur einfachen Ansicht wechseln
newtab-weather-menu-weather-display-option-detailed = Ausführlich
newtab-weather-menu-change-weather-display-detailed = Zur ausführlichen Ansicht wechseln
newtab-weather-menu-temperature-units = Temperatureinheiten
newtab-weather-menu-temperature-option-fahrenheit = Fahrenheit
newtab-weather-menu-temperature-option-celsius = Celsius
newtab-weather-menu-change-temperature-units-fahrenheit = Zu Fahrenheit wechseln
newtab-weather-menu-change-temperature-units-celsius = Zu Celsius wechseln
newtab-weather-menu-hide-weather = Wetter bei neuem Tab ausblenden
newtab-weather-menu-learn-more = Weitere Informationen
# This message is shown if user is working offline
newtab-weather-error-not-available = Wetterdaten sind derzeit nicht verfügbar.

## Topic Labels

newtab-topic-label-business = Wirtschaft
newtab-topic-label-career = Karriere
newtab-topic-label-education = Bildung
newtab-topic-label-arts = Unterhaltung
newtab-topic-label-food = Essen
newtab-topic-label-health = Gesundheit
newtab-topic-label-hobbies = Gaming
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = Finanzen
newtab-topic-label-society-parenting = Erziehung
newtab-topic-label-government = Politik
newtab-topic-label-education-science = Wissenschaft
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = Life-Hacks
newtab-topic-label-sports = Sport
newtab-topic-label-tech = Technik
newtab-topic-label-travel = Reisen
newtab-topic-label-home = Haus und Garten

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = Wählen Sie Themen aus, um Ihren Feed zu optimieren
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = Wählen Sie zwei oder mehr Themen aus. Unsere erfahrenen Kuratoren priorisieren Geschichten, die auf Ihre Interessen zugeschnitten sind. Passen Sie die Themen jederzeit an.
newtab-topic-selection-save-button = Speichern
newtab-topic-selection-cancel-button = Abbrechen
newtab-topic-selection-button-maybe-later = Vielleicht später
newtab-topic-selection-privacy-link = Erfahren Sie, wie wir Daten schützen und verwalten
newtab-topic-selection-button-update-interests = Aktualisieren Sie Ihre Interessen
newtab-topic-selection-button-pick-interests = Wählen Sie Ihre Interessen aus

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = Folgen
newtab-section-following-button = Folgen
newtab-section-unfollow-button = Nicht mehr folgen

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = Blockieren
newtab-section-blocked-button = Blockiert
newtab-section-unblock-button = Nicht mehr blockieren

## Confirmation modal for blocking a section

newtab-section-cancel-button = Nicht jetzt
newtab-section-confirm-block-topic-p1 = Soll dieses Thema wirklich blockiert werden?
newtab-section-confirm-block-topic-p2 = Blockierte Themen erscheinen nicht mehr in Ihrem Feed.
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = { $topic } blockieren

## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-mangage-topics-title = Themen
newtab-section-manage-topics-button-v2 =
    .label = Themen verwalten
newtab-section-mangage-topics-followed-topics = Gefolgt
newtab-section-mangage-topics-followed-topics-empty-state = Sie folgen noch keinen Themen.
newtab-section-mangage-topics-blocked-topics = Blockiert
newtab-section-mangage-topics-blocked-topics-empty-state = Sie haben noch keine Themen blockiert.
newtab-custom-wallpaper-title = Hier gibt es benutzerdefinierte Hintergrundbilder
# 'Make firefox yours" means to customize or personalize
newtab-custom-wallpaper-subtitle = Laden Sie Ihr eigenes Hintergrundbild hoch oder wählen Sie eine benutzerdefinierte Farbe, um { -brand-product-name } für Sie anzupassen.
newtab-custom-wallpaper-cta = Ausprobieren
