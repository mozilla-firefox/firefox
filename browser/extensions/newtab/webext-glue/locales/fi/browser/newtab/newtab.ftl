# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Uusi välilehti
newtab-settings-button =
    .title = Muokkaa Uusi välilehti -sivua
newtab-personalize-settings-icon-label =
    .title = Mukauta uutta välilehteä
    .aria-label = Asetukset
newtab-settings-dialog-label =
    .aria-label = Asetukset
newtab-personalize-icon-label =
    .title = Muokkaa uutta välilehteä
    .aria-label = Muokkaa uutta välilehteä
newtab-personalize-dialog-label =
    .aria-label = Muokkaa
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Haku
    .aria-label = Haku
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Hae hakukoneella { $engine } tai kirjoita osoite
newtab-search-box-handoff-text-no-engine = Kirjoita osoite tai hakusana
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Hae hakukoneella { $engine } tai kirjoita osoite
    .title = Hae hakukoneella { $engine } tai kirjoita osoite
    .aria-label = Hae hakukoneella { $engine } tai kirjoita osoite
newtab-search-box-handoff-input-no-engine =
    .placeholder = Kirjoita osoite tai hakusana
    .title = Kirjoita osoite tai hakusana
    .aria-label = Kirjoita osoite tai hakusana
newtab-search-box-text = Verkkohaku
newtab-search-box-input =
    .placeholder = Verkkohaku
    .aria-label = Verkkohaku

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Lisää hakukone
newtab-topsites-add-shortcut-header = Uusi oikotie
newtab-topsites-edit-topsites-header = Muokkaa ykkössivustoa
newtab-topsites-edit-shortcut-header = Muokkaa oikotietä
newtab-topsites-add-shortcut-label = Lisää pikavalinta
newtab-topsites-title-label = Otsikko
newtab-topsites-title-input =
    .placeholder = Kirjoita otsikko
newtab-topsites-url-label = Osoite
newtab-topsites-url-input =
    .placeholder = Kirjoita tai liitä osoite
newtab-topsites-url-validation = Kelvollinen osoite vaaditaan
newtab-topsites-image-url-label = Oman kuvan osoite
newtab-topsites-use-image-link = Käytä omaa kuvaa…
newtab-topsites-image-validation = Kuvan lataaminen epäonnistui. Kokeile toista osoitetta.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Peruuta
newtab-topsites-delete-history-button = Poista historiasta
newtab-topsites-save-button = Tallenna
newtab-topsites-preview-button = Esikatsele
newtab-topsites-add-button = Lisää

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Haluatko varmasti poistaa tämän sivun kaikkialta historiastasi?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Tämä toiminto on peruuttamaton.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Sponsoroitu

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Avaa valikko
    .aria-label = Avaa valikko
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Poista
    .aria-label = Poista
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Avaa valikko
    .aria-label = Avaa pikavalikko sivustolle { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Muokkaa tätä sivustoa
    .aria-label = Muokkaa tätä sivustoa

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Muokkaa
newtab-menu-open-new-window = Avaa uuteen ikkunaan
newtab-menu-open-new-private-window = Avaa uuteen yksityiseen ikkunaan
newtab-menu-dismiss = Hylkää
newtab-menu-pin = Kiinnitä
newtab-menu-unpin = Poista kiinnitys
newtab-menu-delete-history = Poista historiasta
newtab-menu-save-to-pocket = Tallenna { -pocket-brand-name }-palveluun
newtab-menu-delete-pocket = Poista { -pocket-brand-name }-palvelusta
newtab-menu-archive-pocket = Arkistoi { -pocket-brand-name }-palveluun
newtab-menu-show-privacy-info = Tukijamme ja yksityisyytesi
newtab-menu-about-fakespot = Tietoja { -fakespot-brand-name }ista
newtab-menu-report-content = Ilmoita tästä sisällöstä
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = Estä
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = Lopeta aiheen seuraaminen

## Context menu options for sponsored stories and new ad formats on New Tab.

newtab-menu-manage-sponsored-content = Hallinnoi sponsoroitua sisältöä
newtab-menu-our-sponsors-and-your-privacy = Tukijamme ja yksityisyytesi
newtab-menu-report-this-ad = Ilmoita tästä mainoksesta

## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Valmis
newtab-privacy-modal-button-manage = Hallitse sponsoroidun sisällön asetuksia
newtab-privacy-modal-header = Yksityisyydelläsi on merkitystä.
newtab-privacy-modal-paragraph-2 =
    Kiehtovien tarinoiden tarjoamisen lisäksi näytämme sinulle myös kiinnostavaa,
    tarkastettua sisältöä valituilta sponsoreilta. Voit olla varma, että <strong>selaustietosi
    pysyvät omassa { -brand-product-name }-kopiossasi</strong> – emme näe niitä eivätkä 
    myöskään sponsorimme.
newtab-privacy-modal-link = Opi miten yksityisyys on esillä uusi välilehti -sivulla

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Poista kirjanmerkki
# Bookmark is a verb here.
newtab-menu-bookmark = Lisää kirjanmerkki

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Kopioi latauslinkki
newtab-menu-go-to-download-page = Siirry lataussivulle
newtab-menu-remove-download = Poista historiasta

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Näytä Finderissa
       *[other] Avaa kohteen kansio
    }
newtab-menu-open-file = Avaa tiedosto

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Vierailtu
newtab-label-bookmarked = Kirjanmerkki
newtab-label-removed-bookmark = Kirjanmerkki poistettu
newtab-label-recommended = Pinnalla
newtab-label-saved = Tallennettu { -pocket-brand-name }-palveluun
newtab-label-download = Ladatut
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · Sponsoroitu
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Sponsorina { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } min

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Poista osio
newtab-section-menu-collapse-section = Pienennä osio
newtab-section-menu-expand-section = Laajenna osio
newtab-section-menu-manage-section = Muokkaa osiota
newtab-section-menu-manage-webext = Hallitse laajennusta
newtab-section-menu-add-topsite = Lisää ykkössivusto
newtab-section-menu-add-search-engine = Lisää hakukone
newtab-section-menu-move-up = Siirrä ylös
newtab-section-menu-move-down = Siirrä alas
newtab-section-menu-privacy-notice = Tietosuojakäytäntö

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Pienennä osio
newtab-section-expand-section-label =
    .aria-label = Laajenna osio

## Section Headers.

newtab-section-header-topsites = Ykkössivustot
newtab-section-header-recent-activity = Viimeisin toiminta
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Suositukset lähteestä { $provider }
newtab-section-header-stories = Ajatuksia herättäviä tarinoita
# "picks" refers to recommended articles
newtab-section-header-todays-picks = Tämän päivän valinnat sinulle

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Ala selata, niin tässä alkaa näkyä hyviä juttuja, videoita ja muita sivuja, joilla olet käynyt hiljattain tai jotka olet lisännyt kirjanmerkkeihin.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Ei enempää suosituksia juuri nyt. Katso myöhemmin uudestaan lisää ykkösjuttuja lähteestä { $provider }. Etkö malta odottaa? Valitse suosittu aihe ja löydä lisää hyviä juttuja ympäri verkkoa.
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = Ei enempää suosituksia juuri nyt. Katso myöhemmin uudestaan lisää juttuja. Etkö malta odottaa? Valitse suosittu aihe ja löydä lisää hyviä juttuja ympäri verkkoa.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Olet ajan tasalla!
newtab-discovery-empty-section-topstories-content = Katso myöhemmin lisää juttuja.
newtab-discovery-empty-section-topstories-try-again-button = Yritä uudelleen
newtab-discovery-empty-section-topstories-loading = Ladataan…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Hups! Tämä osio ladattiin melkein, mutta ei ihan.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Suositut aiheet:
newtab-pocket-new-topics-title = Haluatko lisää tarinoita? Katso nämä suositut aiheet { -pocket-brand-name }ista
newtab-pocket-more-recommendations = Lisää suosituksia
newtab-pocket-learn-more = Lue lisää
newtab-pocket-cta-button = Hanki { -pocket-brand-name }
newtab-pocket-cta-text = Tallenna tykkäämäsi tekstit { -pocket-brand-name }iin ja ravitse mieltäsi kiinnostavilla teksteillä.
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } on osa { -brand-product-name }-perhettä
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = Tallenna
newtab-pocket-saved = Tallennettu

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = Lisää tällaista
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = Ei minulle
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = Kiitos. Palautteesi auttaa meitä parantamaan syötettäsi.
newtab-toast-dismiss-button =
    .title = Hylkää
    .aria-label = Hylkää

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Löydä verkon parhaat puolet
newtab-pocket-onboarding-cta = { -pocket-brand-name } tutkii monenlaisia julkaisuja tarjotakseen informatiivisimman, inspiroivimman ja luotettavimman sisällön suoraan { -brand-product-name }-selaimellesi.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Hups, jotain meni vikaan tätä sisältöä ladattaessa.
newtab-error-fallback-refresh-link = Yritä uudestaan päivittämällä sivu.

## Customization Menu

newtab-custom-shortcuts-title = Oikotiet
newtab-custom-shortcuts-subtitle = Tallentamasi tai vierailemasi sivustot
newtab-custom-shortcuts-toggle =
    .label = Oikotiet
    .description = Tallentamasi tai vierailemasi sivustot
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } rivi
       *[other] { $num } riviä
    }
newtab-custom-sponsored-sites = Sponsoroidut oikotiet
newtab-custom-pocket-title = { -pocket-brand-name } suosittelee
newtab-custom-pocket-subtitle = Poikkeuksellista, valikoitua sisältöä { -pocket-brand-name }-palvelulta, osana { -brand-product-name }-perhettä
newtab-custom-stories-toggle =
    .label = Suositellut tarinat
    .description = Poikkeuksellista { -brand-product-name }-perheen kuratoimaa sisältöä
newtab-custom-pocket-sponsored = Sponsoroidut tarinat
newtab-custom-pocket-show-recent-saves = Näytä viimeisimmät tallennukset
newtab-custom-recent-title = Viimeisin toiminta
newtab-custom-recent-subtitle = Valikoima viimeisimpiä sivustoja ja sisältöä
newtab-custom-recent-toggle =
    .label = Viimeisin toiminta
    .description = Valikoima viimeisimpiä sivustoja ja sisältöä
newtab-custom-weather-toggle =
    .label = Sää
    .description = Päivän sääennuste yhdellä vilkaisulla
newtab-custom-close-button = Sulje
newtab-custom-settings = Muokkaa lisää asetuksia

## New Tab Wallpapers

newtab-wallpaper-title = Taustakuvat
newtab-wallpaper-reset = Palauta oletusarvo
newtab-wallpaper-upload-image = Lähetä kuva
newtab-wallpaper-custom-color = Valitse väri
# Variables
#   $file_size (number) - The number of the maximum image file size (in MB) that may be uploaded
newtab-wallpaper-error-max-file-size = Kuvan koko ylitti tiedostokokorajan { $file_size } Mt. Yritä ladata pienempi tiedosto.
newtab-wallpaper-error-file-type = Tiedostoa ei voitu lähettää. Yritä uudelleen toisella tiedostotyypillä.
newtab-wallpaper-light-red-panda = Kultapanda
newtab-wallpaper-light-mountain = Valkoinen vuori
newtab-wallpaper-light-sky = Taivas violettien ja vaaleanpunaisten pilvien kera
newtab-wallpaper-light-color = Siniset, vaaleanpunaiset ja keltaiset muodot
newtab-wallpaper-light-landscape = Sinertävän usvan vuorimaisema
newtab-wallpaper-light-beach = Ranta ja palmupuu
newtab-wallpaper-dark-aurora = Revontulet
newtab-wallpaper-dark-color = Punaiset ja siniset muodot
newtab-wallpaper-dark-panda = Kultapanda metsän piilossa
newtab-wallpaper-dark-sky = Kaupunkimaisema ja yötaivas
newtab-wallpaper-dark-mountain = Vuorimaisema
newtab-wallpaper-dark-city = Purppura kaupunkimaisema
newtab-wallpaper-dark-fox-anniversary = Kettu jalkakäytävällä lähellä metsää
newtab-wallpaper-light-fox-anniversary = Kettu ruohopellolla ja sumuinen vuoristomaisema

## Solid Colors

newtab-wallpaper-category-title-colors = Yhtenäiset värit
newtab-wallpaper-blue = Sininen
newtab-wallpaper-light-blue = Vaaleansininen
newtab-wallpaper-light-purple = Vaaleanvioletti
newtab-wallpaper-light-green = Vaaleanvihreä
newtab-wallpaper-green = Vihreä
newtab-wallpaper-beige = Beige
newtab-wallpaper-yellow = Keltainen
newtab-wallpaper-orange = Oranssi
newtab-wallpaper-pink = Pinkki
newtab-wallpaper-light-pink = Vaaleanpinkki
newtab-wallpaper-red = Punainen
newtab-wallpaper-dark-blue = Tummansininen
newtab-wallpaper-dark-purple = Tummanvioletti
newtab-wallpaper-dark-green = Tummanvihreä
newtab-wallpaper-brown = Ruskea

## Abstract

newtab-wallpaper-category-title-abstract = Abstrakti
newtab-wallpaper-abstract-green = Vihreät muodot
newtab-wallpaper-abstract-blue = Siniset muodot
newtab-wallpaper-abstract-purple = Violetit muodot
newtab-wallpaper-abstract-orange = Oranssit muodot
newtab-wallpaper-gradient-orange = Oranssi ja pinkki liukuväreissä
newtab-wallpaper-abstract-blue-purple = Sinisiä ja violetteja muotoja
newtab-wallpaper-abstract-white-curves = Valkoista ja varjostettuja kaaria
newtab-wallpaper-abstract-purple-green = Violetin ja vihreän valon liukuväriä
newtab-wallpaper-abstract-blue-purple-waves = Sinisiä ja violetteja aaltoilevia muotoja
newtab-wallpaper-abstract-black-waves = Mustia aaltoilevia muotoja

## Celestial

newtab-wallpaper-category-title-photographs = Valokuvat
newtab-wallpaper-beach-at-sunrise = Ranta auringonnousun aikaan
newtab-wallpaper-beach-at-sunset = Ranta auringonlaskun aikaan
newtab-wallpaper-storm-sky = Myrskyinen taivas
newtab-wallpaper-sky-with-pink-clouds = Taivas ja vaaleanpunaiset pilvet
newtab-wallpaper-red-panda-yawns-in-a-tree = Kultapanda haukottelee puussa
newtab-wallpaper-white-mountains = Valkoiset vuoret
newtab-wallpaper-hot-air-balloons = Värikkäitä kuumailmapalloja päiväsaikaan
newtab-wallpaper-starry-canyon = Sininen tähtiyö
newtab-wallpaper-suspension-bridge = Harmaa riippusilta päiväsaikaan
newtab-wallpaper-sand-dunes = Valkoiset hiekkadyynit
newtab-wallpaper-palm-trees = Kookospalmujen siluetti auringonnousun aikana
newtab-wallpaper-blue-flowers = Lähikuva siniterälehtisistä kukista
newtab-wallpaper-forest-trees = Puut sumun peitossa
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = Kuva: <a data-l10n-name="name-link">{ $author_string }</a> sivustolla <a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-header = Lisää ripaus väriä
newtab-wallpaper-feature-highlight-content = Anna uudelle välilehdelle uusi ilme taustakuvien avulla.
newtab-wallpaper-feature-highlight-button = Selvä
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = Hylkää
    .aria-label = Sulje ilmoitus
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial

# “Celestial” referring to astronomy; positioned in or relating to the sky,
# or outer space as observed in astronomy.
# Not to be confused with religious definition of the word.
newtab-wallpaper-category-title-celestial = Taivaallinen
newtab-wallpaper-celestial-lunar-eclipse = Kuunpimennys
newtab-wallpaper-celestial-earth-night = Yökuva matalalta Maan kiertoradalta
newtab-wallpaper-celestial-starry-sky = Tähtitaivas
newtab-wallpaper-celestial-eclipse-time-lapse = Kuunpimennyksen kuvasarja
newtab-wallpaper-celestial-black-hole = Mustan aukon galaksikuvitus
newtab-wallpaper-celestial-river = Satelliittikuva joesta

## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = Katso ennuste palvelussa { $provider }
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ Sponsoroitu
newtab-weather-menu-change-location = Vaihda sijaintia
newtab-weather-change-location-search-input-placeholder =
    .placeholder = Hae sijaintia
    .aria-label = Hae sijaintia
newtab-weather-change-location-search-input = Hae sijaintia
newtab-weather-menu-weather-display = Sään näkymä
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = Yksinkertainen
newtab-weather-menu-change-weather-display-simple = Vaihda yksinkertaiseen näkymään
newtab-weather-menu-weather-display-option-detailed = Yksityiskohtainen
newtab-weather-menu-change-weather-display-detailed = Vaihda yksityiskohtaiseen näkymään
newtab-weather-menu-temperature-units = Lämpötilayksiköt
newtab-weather-menu-temperature-option-fahrenheit = Fahrenheit
newtab-weather-menu-temperature-option-celsius = Celsius
newtab-weather-menu-change-temperature-units-fahrenheit = Vaihda Fahrenheitiin
newtab-weather-menu-change-temperature-units-celsius = Vaihda Celsiukseen
newtab-weather-menu-hide-weather = Piilota sää uudessa välilehdessä
newtab-weather-menu-learn-more = Lue lisää
# This message is shown if user is working offline
newtab-weather-error-not-available = Säätiedot eivät ole tällä hetkellä saatavilla.

## Topic Labels

newtab-topic-label-business = Liiketoiminta
newtab-topic-label-career = Ura
newtab-topic-label-education = Koulutus
newtab-topic-label-arts = Viihde
newtab-topic-label-food = Ruoka
newtab-topic-label-health = Terveys
newtab-topic-label-hobbies = Pelaaminen
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = Raha-asiat
newtab-topic-label-society-parenting = Vanhemmuus
newtab-topic-label-government = Politiikka
newtab-topic-label-education-science = Tiede
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = Niksit
newtab-topic-label-sports = Urheilu
newtab-topic-label-tech = Tekniikka
newtab-topic-label-travel = Matkailu
newtab-topic-label-home = Koti ja puutarha

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = Hienosäädä syötettä valitsemalla aiheita
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = Valitse vähintään kaksi aihetta. Asiantuntevat kuraattorimme priorisoivat kiinnostuksen kohteidesi mukaan räätälöityjä tarinoita. Päivitä milloin tahansa.
newtab-topic-selection-save-button = Tallenna
newtab-topic-selection-cancel-button = Peruuta
newtab-topic-selection-button-maybe-later = Ehkä myöhemmin
newtab-topic-selection-privacy-link = Lue lisää, kuinka suojaamme ja hallitsemme tietoja
newtab-topic-selection-button-update-interests = Päivitä kiinnostuksen kohteesi
newtab-topic-selection-button-pick-interests = Valitse kiinnostuksen kohteesi

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = Seuraa
newtab-section-following-button = Seurataan
newtab-section-unfollow-button = Lopeta seuraaminen

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = Estä
newtab-section-blocked-button = Estetty
newtab-section-unblock-button = Poista esto

## Confirmation modal for blocking a section

newtab-section-cancel-button = Ei nyt
newtab-section-confirm-block-topic-p1 = Haluatko varmasti estää tämän aiheen?
newtab-section-confirm-block-topic-p2 = Estetyt aiheet eivät enää näy syötteessäsi.
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = Estä { $topic }

## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-mangage-topics-title = Aiheet
newtab-section-manage-topics-button-v2 =
    .label = Hallinnoi aiheita
newtab-section-mangage-topics-followed-topics = Seurattu
newtab-section-mangage-topics-followed-topics-empty-state = Et ole vielä seurannut yhtäkään aihetta.
newtab-section-mangage-topics-blocked-topics = Estetty
newtab-section-mangage-topics-blocked-topics-empty-state = Et ole vielä estänyt yhtäkään aihetta.
newtab-custom-wallpaper-title = Mukautetut taustakuvat ovat täällä
# 'Make firefox yours" means to customize or personalize
newtab-custom-wallpaper-subtitle = Valitse oma taustakuvasi tai mukautettu väri ja tee { -brand-product-name }ista mieluisesi.
newtab-custom-wallpaper-cta = Kokeile
