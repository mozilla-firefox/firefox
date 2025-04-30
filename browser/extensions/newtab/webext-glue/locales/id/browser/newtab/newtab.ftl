# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Tab Baru
newtab-settings-button =
    .title = Ubahsuai laman Tab Baru Anda
newtab-personalize-settings-icon-label =
    .title = Personalisasikan Tab Baru
    .aria-label = Pengaturan
newtab-settings-dialog-label =
    .aria-label = Pengaturan
newtab-personalize-icon-label =
    .title = Personalisasikan tab baru
    .aria-label = Personalisasikan tab baru
newtab-personalize-dialog-label =
    .aria-label = Personalisasikan
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Cari
    .aria-label = Cari
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Cari lewat { $engine } atau masukkan alamat
newtab-search-box-handoff-text-no-engine = Cari atau masukkan alamat
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Cari lewat { $engine } atau masukkan alamat
    .title = Cari lewat { $engine } atau masukkan alamat
    .aria-label = Cari lewat { $engine } atau masukkan alamat
newtab-search-box-handoff-input-no-engine =
    .placeholder = Cari atau masukkan alamat
    .title = Cari atau masukkan alamat
    .aria-label = Cari atau masukkan alamat
newtab-search-box-text = Cari di Web
newtab-search-box-input =
    .placeholder = Cari di web
    .aria-label = Cari di web

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Tambahkan Mesin Pencari
newtab-topsites-add-shortcut-header = Pintasan Baru
newtab-topsites-edit-topsites-header = Ubah Situs Pilihan
newtab-topsites-edit-shortcut-header = Edit Pintasan
newtab-topsites-add-shortcut-label = Tambahkan Pintasan
newtab-topsites-title-label = Judul
newtab-topsites-title-input =
    .placeholder = Masukkan judul
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Ketik atau tempel URL
newtab-topsites-url-validation = URL valid diperlukan
newtab-topsites-image-url-label = URL Gambar Khusus
newtab-topsites-use-image-link = Gunakan gambar khusus…
newtab-topsites-image-validation = Gambar gagal dimuat. Coba URL lain.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Batalkan
newtab-topsites-delete-history-button = Hapus dari Riwayat
newtab-topsites-save-button = Simpan
newtab-topsites-preview-button = Pratinjau
newtab-topsites-add-button = Tambah

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Yakin ingin menghapus setiap bagian dari laman ini dari riwayat Anda?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Tindakan ini tidak bisa diurungkan.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Bersponsor

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Buka menu
    .aria-label = Buka menu
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Hapus
    .aria-label = Hapus
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Buka menu
    .aria-label = Buka menu konteks untuk { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Edit situs ini
    .aria-label = Edit situs ini

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Edit
newtab-menu-open-new-window = Buka di Jendela Baru
newtab-menu-open-new-private-window = Buka di Jendela Penjelajahan Pribadi Baru
newtab-menu-dismiss = Tutup
newtab-menu-pin = Semat
newtab-menu-unpin = Lepas
newtab-menu-delete-history = Hapus dari Riwayat
newtab-menu-save-to-pocket = Simpan ke { -pocket-brand-name }
newtab-menu-delete-pocket = Hapus dari { -pocket-brand-name }
newtab-menu-archive-pocket = Arsip di { -pocket-brand-name }
newtab-menu-show-privacy-info = Sponsor kami & privasi Anda
newtab-menu-about-fakespot = Tentang { -fakespot-brand-name }
newtab-menu-report-content = Laporkan konten ini
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = Blokir
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = Berhenti Mengikuti Topik

## Context menu options for sponsored stories and new ad formats on New Tab.

newtab-menu-manage-sponsored-content = Kelola konten bersponsor
newtab-menu-our-sponsors-and-your-privacy = Sponsor kami dan privasi Anda
newtab-menu-report-this-ad = Laporkan iklan ini

## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Selesai
newtab-privacy-modal-button-manage = Kelola pengaturan konten sponsor
newtab-privacy-modal-header = Privasi Anda penting.
newtab-privacy-modal-paragraph-2 = Selain menampilkan berbagai kisah menawan, kami juga menampilkan konten yang relevan, yang telah diperiksa dari sponsor tertentu, untuk Anda. Yakinlah, <strong>data penjelajahan Anda tidak pernah meninggalkan { -brand-product-name } Anda</strong> — kami dan sponsor kami tidak melihatnya.
newtab-privacy-modal-link = Pelajari cara privasi bekerja di tab baru

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Hapus Markah
# Bookmark is a verb here.
newtab-menu-bookmark = Markah

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Salin Tautan Unduhan
newtab-menu-go-to-download-page = Buka Laman Unduhan
newtab-menu-remove-download = Hapus dari Riwayat

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Tampilkan di Finder
       *[other] Buka Foldernya
    }
newtab-menu-open-file = Buka Berkas

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Dikunjungi
newtab-label-bookmarked = Dimarkahi
newtab-label-removed-bookmark = Markah dihapus
newtab-label-recommended = Trending
newtab-label-saved = Disimpan di { -pocket-brand-name }
newtab-label-download = Terunduh
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · Pesan Sponsor
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Disponsori oleh { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } ・ { $timeToRead } mnt

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Hapus Bagian
newtab-section-menu-collapse-section = Ciutkan Bagian
newtab-section-menu-expand-section = Bentangkan Bagian
newtab-section-menu-manage-section = Kelola Bagian
newtab-section-menu-manage-webext = Kelola Ekstensi
newtab-section-menu-add-topsite = Tambah Situs Pilihan
newtab-section-menu-add-search-engine = Tambahkan Mesin Pencari
newtab-section-menu-move-up = Naikkan
newtab-section-menu-move-down = Turunkan
newtab-section-menu-privacy-notice = Kebijakan Privasi

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Ciutkan Bagian
newtab-section-expand-section-label =
    .aria-label = Bentangkan Bagian

## Section Headers.

newtab-section-header-topsites = Situs Teratas
newtab-section-header-recent-activity = Aktivitas terbaru
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Disarankan oleh { $provider }
newtab-section-header-stories = Cerita yang menggugah pikiran
# "picks" refers to recommended articles
newtab-section-header-todays-picks = Pilihan hari ini untuk Anda

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Mulai menjelajah, dan kami akan menampilkan beberapa artikel bagus, video, dan halaman lain yang baru saja Anda kunjungi atau termarkah di sini.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Maaf Anda tercegat. Periksa lagi nanti untuk lebih banyak cerita terbaik dari { $provider }. Tidak mau menunggu? Pilih topik populer untuk menemukan lebih banyak cerita hebat dari seluruh web.
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = Semuanya sudah dibuka. Silakan kembali nanti untuk cerita lainnya. Tidak sabar? Pilih topik populer untuk menemukan lebih banyak cerita hebat dari seluruh web.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Semua sudah selesai terbaca!
newtab-discovery-empty-section-topstories-content = Periksa kembali nanti untuk lebih banyak kisah.
newtab-discovery-empty-section-topstories-try-again-button = Coba Lagi
newtab-discovery-empty-section-topstories-loading = Memuat…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Ups! Kami belum selesai memuat bagian ini, tetapi ternyata belum.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Topik Populer:
newtab-pocket-new-topics-title = Ingin lebih banyak cerita? Lihat topik populer ini dari { -pocket-brand-name }
newtab-pocket-more-recommendations = Rekomendasi Lainnya
newtab-pocket-learn-more = Pelajari lebih lanjut
newtab-pocket-cta-button = Dapatkan { -pocket-brand-name }
newtab-pocket-cta-text = Simpan cerita yang anda sukai di { -pocket-brand-name }, dan dapatkan bacaan menarik untuk Anda.
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } adalah bagian dari keluarga { -brand-product-name }
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = Simpan
newtab-pocket-saved = Disimpan

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = Lainnya seperti ini
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = Bukan untuk saya
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = Terima kasih. Umpan balik Anda akan membantu kami meningkatkan umpan Anda.
newtab-toast-dismiss-button =
    .title = Tutup
    .aria-label = Tutup

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Temukan yang terbaik dari web
newtab-pocket-onboarding-cta = { -pocket-brand-name } mengeksplorasi beragam publikasi untuk menghadirkan konten yang paling informatif, inspiratif, dan dapat dipercaya langsung ke peramban { -brand-product-name } Anda.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Ups, ada masalah saat memuat konten ini.
newtab-error-fallback-refresh-link = Segarkan laman untuk mencoba lagi.

## Customization Menu

newtab-custom-shortcuts-title = Pintasan
newtab-custom-shortcuts-subtitle = Situs yang Anda simpan atau kunjungi
newtab-custom-shortcuts-toggle =
    .label = Pintasan
    .description = Situs yang Anda simpan atau kunjungi
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
       *[other] { $num } baris
    }
newtab-custom-sponsored-sites = Pintasan bersponsor
newtab-custom-pocket-title = Disarankan oleh { -pocket-brand-name }
newtab-custom-pocket-subtitle = Konten luar biasa yang dikelola oleh { -pocket-brand-name }, bagian dari keluarga { -brand-product-name }
newtab-custom-stories-toggle =
    .label = Cerita yang direkomendasikan
    .description = Konten luar biasa yang dikurasi oleh keluarga { -brand-product-name }
newtab-custom-pocket-sponsored = Konten bersponsor
newtab-custom-pocket-show-recent-saves = Tampilkan penyimpanan terbaru
newtab-custom-recent-title = Aktivitas terbaru
newtab-custom-recent-subtitle = Pilihan situs dan konten terbaru
newtab-custom-recent-toggle =
    .label = Aktivitas terbaru
    .description = Pilihan situs dan konten terbaru
newtab-custom-weather-toggle =
    .label = Cuaca
    .description = Sekilas prakiraan cuaca hari ini
newtab-custom-close-button = Tutup
newtab-custom-settings = Kelola pengaturan lainnya

## New Tab Wallpapers

newtab-wallpaper-title = Gambar latar
newtab-wallpaper-reset = Setel ulang ke bawaan
newtab-wallpaper-upload-image = Unggah gambar
newtab-wallpaper-custom-color = Pilih warna
# Variables
#   $file_size (number) - The number of the maximum image file size (in MB) that may be uploaded
newtab-wallpaper-error-max-file-size = Gambar melebihi batas ukuran berkas sebesar { $file_size }MB. Coba unggah berkas yang lebih kecil.
newtab-wallpaper-error-file-type = Kami tidak dapat mengunggah berkas Anda. Silakan coba lagi dengan jenis berkas yang berbeda.
newtab-wallpaper-light-red-panda = Panda merah
newtab-wallpaper-light-mountain = Pegunungan putih
newtab-wallpaper-light-sky = Langit dengan awan ungu dan merah muda
newtab-wallpaper-light-color = Bentuk biru, merah muda, dan kuning
newtab-wallpaper-light-landscape = Lanskap pegunungan kabut biru
newtab-wallpaper-light-beach = Pantai dengan pohon palem
newtab-wallpaper-dark-aurora = Aurora Borealis
newtab-wallpaper-dark-color = Bentuk merah dan biru
newtab-wallpaper-dark-panda = Panda merah tersembunyi di hutan
newtab-wallpaper-dark-sky = Lanskap kota dengan langit malam
newtab-wallpaper-dark-mountain = Lanskap pegunungan
newtab-wallpaper-dark-city = Lanskap kota ungu
newtab-wallpaper-dark-fox-anniversary = Seekor rubah di trotoar dekat hutan
newtab-wallpaper-light-fox-anniversary = Seekor rubah di padang berumput dengan lanskap pegunungan yang berkabut

## Solid Colors

newtab-wallpaper-category-title-colors = Warna-warni rata
newtab-wallpaper-blue = Biru
newtab-wallpaper-light-blue = Biru muda
newtab-wallpaper-light-purple = Ungu muda
newtab-wallpaper-light-green = Hijau muda
newtab-wallpaper-green = Hijau
newtab-wallpaper-beige = Beige
newtab-wallpaper-yellow = Kuning
newtab-wallpaper-orange = Jingga
newtab-wallpaper-pink = Merah Muda
newtab-wallpaper-light-pink = Merah muda terang
newtab-wallpaper-red = Merah
newtab-wallpaper-dark-blue = Biru tua
newtab-wallpaper-dark-purple = Ungu tua
newtab-wallpaper-dark-green = Hijau tua
newtab-wallpaper-brown = Coklat

## Abstract

newtab-wallpaper-category-title-abstract = Abstrak
newtab-wallpaper-abstract-green = Bentuk hijau
newtab-wallpaper-abstract-blue = Bentuk biru
newtab-wallpaper-abstract-purple = Bentuk ungu
newtab-wallpaper-abstract-orange = Bentuk jingga
newtab-wallpaper-gradient-orange = Gradien jingga dan merah muda
newtab-wallpaper-abstract-blue-purple = Bentuk biru dan ungu
newtab-wallpaper-abstract-white-curves = Putih dengan kurva berbayang
newtab-wallpaper-abstract-purple-green = Gradien ungu dan hijau terang
newtab-wallpaper-abstract-blue-purple-waves = Bentuk bergelombang biru dan ungu
newtab-wallpaper-abstract-black-waves = Bentuk hitam bergelombang

## Celestial

newtab-wallpaper-category-title-photographs = Foto
newtab-wallpaper-beach-at-sunrise = Pantai saat matahari terbit
newtab-wallpaper-beach-at-sunset = Pantai saat matahari terbenam
newtab-wallpaper-storm-sky = Langit badai
newtab-wallpaper-sky-with-pink-clouds = Langit dengan awan merah muda
newtab-wallpaper-red-panda-yawns-in-a-tree = Panda merah menguap di pohon
newtab-wallpaper-white-mountains = Pegunungan putih
newtab-wallpaper-hot-air-balloons = Aneka warna balon udara panas di siang hari
newtab-wallpaper-starry-canyon = Malam biru berbintang
newtab-wallpaper-suspension-bridge = Fotografi jembatan full-suspension abu-abu di siang hari
newtab-wallpaper-sand-dunes = Bukit pasir putih
newtab-wallpaper-palm-trees = Siluet pohon kelapa saat golden hour
newtab-wallpaper-blue-flowers = Foto jarak dekat bunga berkelopak biru yang sedang mekar
newtab-wallpaper-forest-trees = Pepohonan tertutup kabut
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = Foto oleh <a data-l10n-name="name-link">{ $author_string }</a> di <a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-header = Coba percikan warna
newtab-wallpaper-feature-highlight-content = Berikan Tab Baru Anda tampilan segar dengan gambar latar.
newtab-wallpaper-feature-highlight-button = Paham
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = Tutup
    .aria-label = Tutup popup
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial

# “Celestial” referring to astronomy; positioned in or relating to the sky,
# or outer space as observed in astronomy.
# Not to be confused with religious definition of the word.
newtab-wallpaper-category-title-celestial = Celestial
newtab-wallpaper-celestial-lunar-eclipse = Gerhana bulan
newtab-wallpaper-celestial-earth-night = Foto malam dari orbit rendah Bumi
newtab-wallpaper-celestial-starry-sky = Langit berbintang
newtab-wallpaper-celestial-eclipse-time-lapse = Selang waktu gerhana bulan
newtab-wallpaper-celestial-black-hole = Ilustrasi galaksi lubang hitam
newtab-wallpaper-celestial-river = Citra satelit sungai

## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = Lihat prakiraan di { $provider }
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ Bersponsor
newtab-weather-menu-change-location = Ubah lokasi
newtab-weather-change-location-search-input-placeholder =
    .placeholder = Cari lokasi
    .aria-label = Cari lokasi
newtab-weather-change-location-search-input = Cari lokasi
newtab-weather-menu-weather-display = Tampilan cuaca
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = Sederhana
newtab-weather-menu-change-weather-display-simple = Beralih ke tampilan sederhana
newtab-weather-menu-weather-display-option-detailed = Detail
newtab-weather-menu-change-weather-display-detailed = Beralih ke tampilan detail
newtab-weather-menu-temperature-units = Satuan suhu
newtab-weather-menu-temperature-option-fahrenheit = Fahrenheit
newtab-weather-menu-temperature-option-celsius = Celcius
newtab-weather-menu-change-temperature-units-fahrenheit = Beralih ke Fahrenheit
newtab-weather-menu-change-temperature-units-celsius = Beralih ke Celcius
newtab-weather-menu-hide-weather = Sembunyikan cuaca di Tab Baru
newtab-weather-menu-learn-more = Pelajari lebih lanjut
# This message is shown if user is working offline
newtab-weather-error-not-available = Data cuaca tidak tersedia saat ini.

## Topic Labels

newtab-topic-label-business = Bisnis
newtab-topic-label-career = Karir
newtab-topic-label-education = Pendidikan
newtab-topic-label-arts = Hiburan
newtab-topic-label-food = Makanan
newtab-topic-label-health = Kesehatan
newtab-topic-label-hobbies = Permainan
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = Keuangan
newtab-topic-label-society-parenting = Pengasuhan
newtab-topic-label-government = Politik
newtab-topic-label-education-science = Sains
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = Peningkatan Diri
newtab-topic-label-sports = Olahraga
newtab-topic-label-tech = Teknologi
newtab-topic-label-travel = Perjalanan
newtab-topic-label-home = Rumah & Taman

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = Pilih topik untuk menyempurnakan asupan Anda
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = Pilih dua atau lebih topik. Kurator ahli kami memprioritaskan cerita yang disesuaikan dengan minat Anda. Perbarui kapan saja.
newtab-topic-selection-save-button = Simpan
newtab-topic-selection-cancel-button = Batal
newtab-topic-selection-button-maybe-later = Mungkin nanti
newtab-topic-selection-privacy-link = Pelajari bagaimana kami melindungi dan mengelola data
newtab-topic-selection-button-update-interests = Perbarui minat Anda
newtab-topic-selection-button-pick-interests = Pilih minat Anda

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = Ikuti
newtab-section-following-button = Mengikuti
newtab-section-unfollow-button = Berhenti mengikuti

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = Blokir
newtab-section-blocked-button = Diblokir
newtab-section-unblock-button = Buka blokir

## Confirmation modal for blocking a section

newtab-section-cancel-button = Jangan sekarang
newtab-section-confirm-block-topic-p1 = Yakin ingin memblokir topik ini?
newtab-section-confirm-block-topic-p2 = Topik yang diblokir tidak akan muncul lagi di asupan Anda.
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = Blokir { $topic }

## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-mangage-topics-title = Topik
newtab-section-manage-topics-button-v2 =
    .label = Kelola topik
newtab-section-mangage-topics-followed-topics = Diikuti
newtab-section-mangage-topics-followed-topics-empty-state = Anda belum mengikuti topik apa pun.
newtab-section-mangage-topics-blocked-topics = Diblokir
newtab-section-mangage-topics-blocked-topics-empty-state = Anda belum memblokir topik apa pun.
newtab-custom-wallpaper-title = Wallpaper kustom ada di sini
# 'Make firefox yours" means to customize or personalize
newtab-custom-wallpaper-subtitle = Unggah wallpaper sendiri atau pilih warna kustom untuk menjadikan { -brand-product-name } lebih personal.
newtab-custom-wallpaper-cta = Coba sekarang
