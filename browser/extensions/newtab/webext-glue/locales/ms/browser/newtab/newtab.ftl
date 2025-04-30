# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Tab Baru
newtab-settings-button =
    .title = Sesuaikan halaman Tab Baru anda

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Cari
    .aria-label = Cari

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Tambah Enjin Carian
newtab-topsites-edit-topsites-header = Edit Laman Teratas
newtab-topsites-title-label = Tajuk
newtab-topsites-title-input =
    .placeholder = Masukkan tajuk
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Taip atau tampal URL
newtab-topsites-url-validation = Perlukan URL yang sah
newtab-topsites-image-url-label = URL Imej Penyesuaian
newtab-topsites-use-image-link = Guna imej penyesuaian…
newtab-topsites-image-validation = Imej gagal dimuatkan. Cuba URL lain.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Batal
newtab-topsites-delete-history-button = Buang daripada Sejarah
newtab-topsites-save-button = Simpan
newtab-topsites-preview-button = Previu
newtab-topsites-add-button = Tambah

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Adakah anda pasti mahu membuang setiap contoh halaman ini daripada sejarah anda?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Tindakan ini tidak boleh dibatalkan.

## Top Sites - Sponsored label


## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Buka menu
    .aria-label = Buka menu
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Buka menu
    .aria-label = Buka menu konteks untuk { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Edit laman ini
    .aria-label = Edit laman ini

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Edit
newtab-menu-open-new-window = Buka dalam Tetingkap Baru
newtab-menu-open-new-private-window = Buka dalam Tetingkap Peribadi Baru
newtab-menu-dismiss = Abai
newtab-menu-pin = Pin
newtab-menu-unpin = Nyahpin
newtab-menu-delete-history = Buang daripada Sejarah
newtab-menu-save-to-pocket = Simpan ke { -pocket-brand-name }
newtab-menu-delete-pocket = Buang dari { -pocket-brand-name }
newtab-menu-archive-pocket = Arkib dalam { -pocket-brand-name }

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.


##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Buang Tandabuku
# Bookmark is a verb here.
newtab-menu-bookmark = Tandabuku

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Salin Pautan Muat Turun
newtab-menu-go-to-download-page = Pergi ke Halaman Muat Turun
newtab-menu-remove-download = Buang daripada Sejarah

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Papar dalam Finder
       *[other] Buka Kandungan Folder
    }
newtab-menu-open-file = Buka Fail

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Dilawati
newtab-label-bookmarked = Ditandabuku
newtab-label-recommended = Sohor kini
newtab-label-saved = Disimpan ke { -pocket-brand-name }
newtab-label-download = Telah dimuat turun

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Buang Seksyen
newtab-section-menu-collapse-section = Runtuhkan Seksyen
newtab-section-menu-expand-section = Kembangkan Seksyen
newtab-section-menu-manage-section = Urus Seksyen
newtab-section-menu-manage-webext = Urus Ekstensi
newtab-section-menu-add-topsite = Tambah Laman Teratas
newtab-section-menu-add-search-engine = Tambah Enjin Carian
newtab-section-menu-move-up = Pindah Atas
newtab-section-menu-move-down = Pindah Bawah
newtab-section-menu-privacy-notice = Notis Privasi

## Section aria-labels


## Section Headers.

newtab-section-header-topsites = Laman Teratas
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Disyorkan oleh { $provider }

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Mulakan melayar dan kami akan paparkan beberapa artikel, video dan halaman menarik lain yang sudah anda layari dan tandabuku di sini.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Anda sudah di sini. Tapi sila datang lagi untuk mendapatkan lebih banyak berita hangat daripada { $provider }. Tidak boleh tunggu? Pilih topik untuk mendapatkannya dari serata dunia.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.


## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Topik Popular:
newtab-pocket-more-recommendations = Saranan Lain
newtab-pocket-cta-button = Dapatkan { -pocket-brand-name }
newtab-pocket-cta-text = Simpan cerita yang anda suka dalam { -pocket-brand-name } dan jana minda dengan bahan bacaan yang menarik.

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.


## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.


## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Ooops, ada kesilapan memuatkan kandungan ini.
newtab-error-fallback-refresh-link = Muat semula halaman untuk cuba lagi.

## Customization Menu


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

