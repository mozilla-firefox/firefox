# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Yeni Sekme
newtab-settings-button =
    .title = Yeni Sekme sayfanızı özelleştirin
newtab-personalize-settings-icon-label =
    .title = Yeni sekmeyi kişiselleştir
    .aria-label = Ayarlar
newtab-settings-dialog-label =
    .aria-label = Ayarlar
newtab-personalize-icon-label =
    .title = Yeni sekmeyi kişiselleştir
    .aria-label = Yeni sekmeyi kişiselleştir
newtab-personalize-dialog-label =
    .aria-label = Kişiselleştir
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Ara
    .aria-label = Ara
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = { $engine } ile arama yapın veya adres yazın
newtab-search-box-handoff-text-no-engine = Arama yapın veya adres yazın
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = { $engine } ile arama yapın veya adres yazın
    .title = { $engine } ile arama yapın veya adres yazın
    .aria-label = { $engine } ile arama yapın veya adres yazın
newtab-search-box-handoff-input-no-engine =
    .placeholder = Arama yapın veya adres yazın
    .title = Arama yapın veya adres yazın
    .aria-label = Arama yapın veya adres yazın
newtab-search-box-text = Web’de ara
newtab-search-box-input =
    .placeholder = Web’de ara
    .aria-label = Web’de ara

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Arama motoru ekle
newtab-topsites-add-shortcut-header = Yeni kısayol
newtab-topsites-edit-topsites-header = Sık kullanılan siteyi düzenle
newtab-topsites-edit-shortcut-header = Kısayolu düzenle
newtab-topsites-add-shortcut-label = Kısayol ekle
newtab-topsites-title-label = Başlık
newtab-topsites-title-input =
    .placeholder = Başlık yazın
newtab-topsites-url-label = Adres
newtab-topsites-url-input =
    .placeholder = Adres yazın ve yapıştırın
newtab-topsites-url-validation = Geçerli bir adres gerekli
newtab-topsites-image-url-label = Özel resim adresi
newtab-topsites-use-image-link = Özel resim kullan…
newtab-topsites-image-validation = Resim yüklenemedi. Başka bir adres deneyin.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = İptal
newtab-topsites-delete-history-button = Geçmişten sil
newtab-topsites-save-button = Kaydet
newtab-topsites-preview-button = Ön izleme yap
newtab-topsites-add-button = Ekle

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Bu sayfanın tüm kayıtlarını geçmişinizden silmek istediğinizden emin misiniz?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Bu işlem geri alınamaz.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Sponsorlu

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Menüyü aç
    .aria-label = Menüyü aç
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Kaldır
    .aria-label = Kaldır
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Menüyü aç
    .aria-label = { $title } sağ tıklama menüsünü aç
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Bu siteyi düzenle
    .aria-label = Bu siteyi düzenle

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Düzenle
newtab-menu-open-new-window = Yeni pencerede aç
newtab-menu-open-new-private-window = Yeni gizli pencerede aç
newtab-menu-dismiss = Kapat
newtab-menu-pin = Sabitle
newtab-menu-unpin = Sabitleneni kaldır
newtab-menu-delete-history = Geçmişten sil
newtab-menu-save-to-pocket = { -pocket-brand-name }’a kaydet
newtab-menu-delete-pocket = { -pocket-brand-name }’tan sil
newtab-menu-archive-pocket = { -pocket-brand-name }’ta arşivle
newtab-menu-show-privacy-info = Sponsorlarımız ve gizliliğiniz
newtab-menu-about-fakespot = { -fakespot-brand-name } hakkında
newtab-menu-report-content = Bu içeriği rapor et
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = Engelle
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = Konuyu takip etmeyi bırak

## Context menu options for sponsored stories and new ad formats on New Tab.

newtab-menu-manage-sponsored-content = Sponsorlu içerikleri yönet
newtab-menu-our-sponsors-and-your-privacy = Sponsorlarımız ve gizliliğiniz
newtab-menu-report-this-ad = Bu reklamı rapor et

## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Tamam
newtab-privacy-modal-button-manage = Sponsorlu içerik ayarlarını yönet
newtab-privacy-modal-header = Gizliliğiniz bizim için önemli.
newtab-privacy-modal-paragraph-2 = İlginizi çekebilecek yazıların yanı sıra seçkin sponsorlarımızdan gelen bazı içerikleri de gösteriyoruz. Gezinti verileriniz <strong>asla bilgisayarınızdaki { -brand-product-name } kurulumunun dışına çıkmıyor</strong>: Hangi sitelere girdiğinizi ne biz görüyoruz ne de sponsorlarımız.
newtab-privacy-modal-link = Yeni sekmede gizliliğinizi nasıl koruduğumuzu öğrenin

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Yer imini sil
# Bookmark is a verb here.
newtab-menu-bookmark = Yer imlerine ekle

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = İndirme bağlantısını kopyala
newtab-menu-go-to-download-page = İndirme sayfasına git
newtab-menu-remove-download = Geçmişten kaldır

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Finder’da göster
       *[other] Bulunduğu klasörü aç
    }
newtab-menu-open-file = Dosyayı aç

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Ziyaret etmiştiniz
newtab-label-bookmarked = Yer imlerinizde
newtab-label-removed-bookmark = Yer imi silindi
newtab-label-recommended = Popüler
newtab-label-saved = { -pocket-brand-name }’a kaydedildi
newtab-label-download = İndirildi
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · Sponsorlu
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = { $sponsor } sponsorluğunda
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } dk

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Bölümü kaldır
newtab-section-menu-collapse-section = Bölümü daralt
newtab-section-menu-expand-section = Bölümü genişlet
newtab-section-menu-manage-section = Bölümü yönet
newtab-section-menu-manage-webext = Uzantıyı yönet
newtab-section-menu-add-topsite = Sık kullanılan site ekle
newtab-section-menu-add-search-engine = Arama motoru ekle
newtab-section-menu-move-up = Yukarı taşı
newtab-section-menu-move-down = Aşağı taşı
newtab-section-menu-privacy-notice = Gizlilik bildirimi

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Bölümü daralt
newtab-section-expand-section-label =
    .aria-label = Bölümü genişlet

## Section Headers.

newtab-section-header-topsites = Sık Kullanılan Siteler
newtab-section-header-recent-activity = Son Etkinlikler
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = { $provider } öneriyor
newtab-section-header-stories = Merak uyandıran makaleler
# "picks" refers to recommended articles
newtab-section-header-todays-picks = Bugün sizin için seçtiklerimiz

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Gezinmeye başlayın. Son zamanlarda baktığınız veya yer imlerinize eklediğiniz bazı güzel makaleleri, videoları ve diğer sayfaları burada göstereceğiz.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Hepsini bitirdiniz. Yeni { $provider } yazıları için yine gelin. Beklemek istemiyor musunuz? İlginç yazılara ulaşmak için popüler konulardan birini seçebilirsiniz.
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = Hepsini bitirdiniz. Yeni yazılar için daha sonra yine gelin. Beklemek istemiyor musunuz? İlginç yazılara ulaşmak için popüler konulardan birini seçebilirsiniz.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Hepsini bitirdiniz!
newtab-discovery-empty-section-topstories-content = Daha fazla yazı için daha sonra yine gelin.
newtab-discovery-empty-section-topstories-try-again-button = Tekrar dene
newtab-discovery-empty-section-topstories-loading = Yükleniyor…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Hata! Bu bölüm tam olarak yüklenemedi.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Popüler konular:
newtab-pocket-new-topics-title = Daha fazla içeriğe ne dersiniz? { -pocket-brand-name }’taki popüler konulara göz atın
newtab-pocket-more-recommendations = Daha fazla öneri
newtab-pocket-learn-more = Daha fazla bilgi al
newtab-pocket-cta-button = { -pocket-brand-name }’ı edinin
newtab-pocket-cta-text = Sevdiğiniz yazıları { -pocket-brand-name }’a kaydedin, aklınızı okumaya değer şeylerle doldurun.
newtab-pocket-pocket-firefox-family = { -pocket-brand-name }, { -brand-product-name } ailesinin bir parçasıdır
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = Kaydet
newtab-pocket-saved = Kaydedildi

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = Bunun gibi daha fazla
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = Bana göre değil
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = Teşekkürler. Geri bildiriminiz akışınızı geliştirmemize yardımcı olacak.
newtab-toast-dismiss-button =
    .title = Kapat
    .aria-label = Kapat

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Web’deki en iyi içerikleri keşfedin
newtab-pocket-onboarding-cta = { -pocket-brand-name }, çeşitli yayınları tarayarak en bilgilendirici, ilham verici ve güvenilir içerikleri doğrudan { -brand-product-name } tarayıcınıza getiriyor.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Bu içerik yüklenirken bir hata oluştu.
newtab-error-fallback-refresh-link = Yeniden denemek için sayfayı tazeleyin.

## Customization Menu

newtab-custom-shortcuts-title = Kısayollar
newtab-custom-shortcuts-subtitle = Kaydettiğiniz veya ziyaret ettiğiniz siteler
newtab-custom-shortcuts-toggle =
    .label = Kısayollar
    .description = Kaydettiğiniz veya ziyaret ettiğiniz siteler
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } satır
       *[other] { $num } satır
    }
newtab-custom-sponsored-sites = Sponsorlu kısayollar
newtab-custom-pocket-title = { -pocket-brand-name } önerileri
newtab-custom-pocket-subtitle = { -brand-product-name } ailesinin bir parçası olan { -pocket-brand-name }’ın seçtiği harika içerikler
newtab-custom-stories-toggle =
    .label = Önerilen makaleler
    .description = { -brand-product-name } ailesinin seçtiği harika içerikler
newtab-custom-pocket-sponsored = Sponsorlu haberler
newtab-custom-pocket-show-recent-saves = Son kaydedilenleri göster
newtab-custom-recent-title = Son etkinlikler
newtab-custom-recent-subtitle = Son kullanılan siteler ve içeriklerden bir seçki
newtab-custom-recent-toggle =
    .label = Son etkinlikler
    .description = Son kullanılan siteler ve içeriklerden bir seçki
newtab-custom-weather-toggle =
    .label = Hava durumu
    .description = Bugünkü hava durumu tahmini
newtab-custom-close-button = Kapat
newtab-custom-settings = Diğer ayarları yönet

## New Tab Wallpapers

newtab-wallpaper-title = Duvar kâğıtları
newtab-wallpaper-reset = Varsayılana sıfırla
newtab-wallpaper-upload-image = Resim yükle
newtab-wallpaper-custom-color = Renk seç
# Variables
#   $file_size (number) - The number of the maximum image file size (in MB) that may be uploaded
newtab-wallpaper-error-max-file-size = Bu resim, izin verilen { $file_size } MB boyut sınırını aşıyor. Lütfen daha küçük bir dosya yüklemeyi deneyin.
newtab-wallpaper-error-file-type = Dosyanızı yükleyemedik. Lütfen farklı bir dosya türüyle tekrar deneyin.
newtab-wallpaper-light-red-panda = Kızıl panda
newtab-wallpaper-light-mountain = Beyaz dağ
newtab-wallpaper-light-sky = Mor ve pembe bulutlu gökyüzü
newtab-wallpaper-light-color = Mavi, pembe ve sarı şekiller
newtab-wallpaper-light-landscape = Mavi sisli dağ manzarası
newtab-wallpaper-light-beach = Palmiye ağaçlı sahil
newtab-wallpaper-dark-aurora = Aurora Borealis
newtab-wallpaper-dark-color = Kırmızı ve mavi şekiller
newtab-wallpaper-dark-panda = Ormanda saklanan kızıl panda
newtab-wallpaper-dark-sky = Gece gökyüzüyle şehir manzarası
newtab-wallpaper-dark-mountain = Manzaralı dağ
newtab-wallpaper-dark-city = Mor şehir manzarası
newtab-wallpaper-dark-fox-anniversary = Bir ormanın yakınında kaldırımda bir tilki
newtab-wallpaper-light-fox-anniversary = Sisli bir dağ manzarasıyla çimenli bir alanda bir tilki

## Solid Colors

newtab-wallpaper-category-title-colors = Düz renkler
newtab-wallpaper-blue = Mavi
newtab-wallpaper-light-blue = Açık mavi
newtab-wallpaper-light-purple = Açık mor
newtab-wallpaper-light-green = Açık yeşil
newtab-wallpaper-green = Yeşil
newtab-wallpaper-beige = Bej
newtab-wallpaper-yellow = Sarı
newtab-wallpaper-orange = Turuncu
newtab-wallpaper-pink = Pembe
newtab-wallpaper-light-pink = Açık pembe
newtab-wallpaper-red = Kırmızı
newtab-wallpaper-dark-blue = Koyu mavi
newtab-wallpaper-dark-purple = Koyu mor
newtab-wallpaper-dark-green = Koyu yeşil
newtab-wallpaper-brown = Kahverengi

## Abstract

newtab-wallpaper-category-title-abstract = Soyut
newtab-wallpaper-abstract-green = Yeşil şekiller
newtab-wallpaper-abstract-blue = Mavi şekiller
newtab-wallpaper-abstract-purple = Mor şekiller
newtab-wallpaper-abstract-orange = Turuncu şekiller
newtab-wallpaper-gradient-orange = Turuncu ve pembe renk geçişi
newtab-wallpaper-abstract-blue-purple = Mavi ve mor şekiller
newtab-wallpaper-abstract-white-curves = Gölgeli kıvrımlı beyaz
newtab-wallpaper-abstract-purple-green = Mor ve yeşil ışık geçişi
newtab-wallpaper-abstract-blue-purple-waves = Mavi ve mor dalgalı şekiller
newtab-wallpaper-abstract-black-waves = Siyah dalgalı şekiller

## Celestial

newtab-wallpaper-category-title-photographs = Fotoğraflar
newtab-wallpaper-beach-at-sunrise = Gün doğumunda sahil
newtab-wallpaper-beach-at-sunset = Gün batımında sahil
newtab-wallpaper-storm-sky = Fırtınalı gökyüzü
newtab-wallpaper-sky-with-pink-clouds = Pembe bulutlarla kaplı gökyüzü
newtab-wallpaper-red-panda-yawns-in-a-tree = Ağaçta esneyen kızıl panda
newtab-wallpaper-white-mountains = Beyaz dağlar
newtab-wallpaper-hot-air-balloons = Gündüz vakti çeşitli renklerde sıcak hava balonları
newtab-wallpaper-starry-canyon = Mavi yıldızlı gece
newtab-wallpaper-suspension-bridge = Gündüz vakti gri asma köprü fotoğrafı
newtab-wallpaper-sand-dunes = Beyaz kumullar
newtab-wallpaper-palm-trees = Tan vaktinde hindistancevizi ağaçlarının silueti
newtab-wallpaper-blue-flowers = Açmış mavi renkli çiçeklerin yakın plan fotoğrafı
newtab-wallpaper-forest-trees = Sisle kaplı ağaçlar
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = Fotoğraf: <a data-l10n-name="name-link">{ $author_string }</a> / <a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-header = Farklı renkleri deneyin
newtab-wallpaper-feature-highlight-content = Duvar kâğıtlarıyla yeni sekme sayfanıza yeni bir görünüm kazandırın.
newtab-wallpaper-feature-highlight-button = Anladım
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = Kapat
    .aria-label = Açılır pencereyi kapat
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial

# “Celestial” referring to astronomy; positioned in or relating to the sky,
# or outer space as observed in astronomy.
# Not to be confused with religious definition of the word.
newtab-wallpaper-category-title-celestial = Göksel
newtab-wallpaper-celestial-lunar-eclipse = Ay tutulması
newtab-wallpaper-celestial-earth-night = Alçak Dünya yörüngesinden gece fotoğrafı
newtab-wallpaper-celestial-starry-sky = Yıldızlı gökyüzü
newtab-wallpaper-celestial-eclipse-time-lapse = Zaman atlamalı ay tutulması
newtab-wallpaper-celestial-black-hole = Karadelik galaksisi illüstrasyonu
newtab-wallpaper-celestial-river = Nehrin uydu görüntüsü

## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = { $provider } tahminlerine bak
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ Sponsorlu
newtab-weather-menu-change-location = Konumu değiştir
newtab-weather-change-location-search-input-placeholder =
    .placeholder = Konum ara
    .aria-label = Konum ara
newtab-weather-change-location-search-input = Konum ara
newtab-weather-menu-weather-display = Hava durumu göstergesi
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = Basit
newtab-weather-menu-change-weather-display-simple = Basit görünüme geç
newtab-weather-menu-weather-display-option-detailed = Ayrıntılı
newtab-weather-menu-change-weather-display-detailed = Ayrıntılı görünüme geç
newtab-weather-menu-temperature-units = Sıcaklık birimi
newtab-weather-menu-temperature-option-fahrenheit = Fahrenhayt
newtab-weather-menu-temperature-option-celsius = Celcius
newtab-weather-menu-change-temperature-units-fahrenheit = Fahrenhayta geç
newtab-weather-menu-change-temperature-units-celsius = Celsius’a geç
newtab-weather-menu-hide-weather = Yeni sekmede hava durumunu gizle
newtab-weather-menu-learn-more = Daha fazla bilgi al
# This message is shown if user is working offline
newtab-weather-error-not-available = Hava durumu verileri şu anda mevcut değil.

## Topic Labels

newtab-topic-label-business = İş
newtab-topic-label-career = Kariyer
newtab-topic-label-education = Eğitim
newtab-topic-label-arts = Eğlence
newtab-topic-label-food = Yemek
newtab-topic-label-health = Sağlık
newtab-topic-label-hobbies = Oyun
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = Para
newtab-topic-label-society-parenting = Ebeveynlik
newtab-topic-label-government = Politika
newtab-topic-label-education-science = Bilim
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = Kişisel Gelişim
newtab-topic-label-sports = Spor
newtab-topic-label-tech = Teknoloji
newtab-topic-label-travel = Seyahat
newtab-topic-label-home = Ev ve bahçe

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = Akışınızı iyileştirmek için konuları seçin
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = En az iki konu seçin. Küratörlerimiz ilgi alanlarınıza uyan makalelere öncelik verir. Seçtiğiniz konuları istediğiniz zaman güncelleyebilirsiniz.
newtab-topic-selection-save-button = Kaydet
newtab-topic-selection-cancel-button = Vazgeç
newtab-topic-selection-button-maybe-later = Daha sonra
newtab-topic-selection-privacy-link = Verileri nasıl koruduğumuzu ve yönettiğimizi öğrenin
newtab-topic-selection-button-update-interests = İlgi alanlarınızı güncelleyin
newtab-topic-selection-button-pick-interests = İlgi alanlarınızı seçin

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = Takip et
newtab-section-following-button = Takip ediliyor
newtab-section-unfollow-button = Takibi bırak

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = Engelle
newtab-section-blocked-button = Engellendi
newtab-section-unblock-button = Engeli kaldırın

## Confirmation modal for blocking a section

newtab-section-cancel-button = Şimdi değil
newtab-section-confirm-block-topic-p1 = Bu konuyu engellemek istediğinizden emin misiniz?
newtab-section-confirm-block-topic-p2 = Engellenen konular artık akışınızda görünmeyecektir.
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = { $topic } konusunu engelle

## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-mangage-topics-title = Konular
newtab-section-manage-topics-button-v2 =
    .label = Konuları yönet
newtab-section-mangage-topics-followed-topics = Takip ediliyor
newtab-section-mangage-topics-followed-topics-empty-state = Henüz hiçbir konuyu takip etmiyorsunuz.
newtab-section-mangage-topics-blocked-topics = Engellendi
newtab-section-mangage-topics-blocked-topics-empty-state = Henüz hiçbir konuyu engellemediniz.
newtab-custom-wallpaper-title = Artık kendi duvar kâğıtlarınızı kullanabilirsiniz
# 'Make firefox yours" means to customize or personalize
newtab-custom-wallpaper-subtitle = { -brand-product-name } tarayıcınızı kişiselleştirmek için kendi duvar kâğıdınızı yükleyin veya istediğiniz rengi seçin.
newtab-custom-wallpaper-cta = Deneyin
