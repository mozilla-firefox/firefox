# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = แท็บใหม่
newtab-settings-button =
    .title = ปรับแต่งหน้าแท็บใหม่ของคุณ
newtab-personalize-settings-icon-label =
    .title = ปรับแต่งแท็บใหม่
    .aria-label = การตั้งค่า
newtab-settings-dialog-label =
    .aria-label = การตั้งค่า
newtab-personalize-icon-label =
    .title = ปรับแท็บใหม่ให้เป็นส่วนตัว
    .aria-label = ปรับแท็บใหม่ให้เป็นส่วนตัว
newtab-personalize-dialog-label =
    .aria-label = ปรับให้เป็นแบบส่วนตัว
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = ค้นหา
    .aria-label = ค้นหา
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = ค้นหาด้วย { $engine } หรือป้อนที่อยู่
newtab-search-box-handoff-text-no-engine = ค้นหาหรือป้อนที่อยู่
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = ค้นหาด้วย { $engine } หรือป้อนที่อยู่
    .title = ค้นหาด้วย { $engine } หรือป้อนที่อยู่
    .aria-label = ค้นหาด้วย { $engine } หรือป้อนที่อยู่
newtab-search-box-handoff-input-no-engine =
    .placeholder = ค้นหาหรือป้อนที่อยู่
    .title = ค้นหาหรือป้อนที่อยู่
    .aria-label = ค้นหาหรือป้อนที่อยู่
newtab-search-box-text = ค้นหาเว็บ
newtab-search-box-input =
    .placeholder = ค้นหาเว็บ
    .aria-label = ค้นหาเว็บ

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = เพิ่มเครื่องมือค้นหา
newtab-topsites-add-shortcut-header = ทางลัดใหม่
newtab-topsites-edit-topsites-header = แก้ไขไซต์เด่น
newtab-topsites-edit-shortcut-header = แก้ไขทางลัด
newtab-topsites-add-shortcut-label = เพิ่มทางลัด
newtab-topsites-title-label = ชื่อเรื่อง
newtab-topsites-title-input =
    .placeholder = ป้อนชื่อเรื่อง
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = พิมพ์หรือวาง URL
newtab-topsites-url-validation = ต้องการ URL ที่ถูกต้อง
newtab-topsites-image-url-label = URL ภาพกำหนดเอง
newtab-topsites-use-image-link = ใช้ภาพกำหนดเอง…
newtab-topsites-image-validation = ไม่สามารถโหลดภาพ ลอง URL อื่น

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = ยกเลิก
newtab-topsites-delete-history-button = ลบออกจากประวัติ
newtab-topsites-save-button = บันทึก
newtab-topsites-preview-button = แสดงตัวอย่าง
newtab-topsites-add-button = เพิ่ม

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = คุณแน่ใจหรือไม่ว่าต้องการลบทุกอินสแตนซ์ของหน้านี้ออกจากประวัติของคุณ?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = การกระทำนี้ไม่สามารถเลิกทำได้

## Top Sites - Sponsored label

newtab-topsite-sponsored = ได้รับการสนับสนุน

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = เปิดเมนู
    .aria-label = เปิดเมนู
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = เอาออก
    .aria-label = เอาออก
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = เปิดเมนู
    .aria-label = เปิดเมนูบริบทสำหรับ { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = แก้ไขไซต์นี้
    .aria-label = แก้ไขไซต์นี้

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = แก้ไข
newtab-menu-open-new-window = เปิดในหน้าต่างใหม่
newtab-menu-open-new-private-window = เปิดในหน้าต่างส่วนตัวใหม่
newtab-menu-dismiss = ยกเลิก
newtab-menu-pin = ปักหมุด
newtab-menu-unpin = ถอนหมุด
newtab-menu-delete-history = ลบออกจากประวัติ
newtab-menu-save-to-pocket = บันทึกไปยัง { -pocket-brand-name }
newtab-menu-delete-pocket = ลบจาก { -pocket-brand-name }
newtab-menu-archive-pocket = เก็บถาวรใน { -pocket-brand-name }
newtab-menu-show-privacy-info = สปอนเซอร์ของเราและความเป็นส่วนตัวของคุณ
newtab-menu-about-fakespot = เกี่ยวกับ { -fakespot-brand-name }
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = ปิดกั้น
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = เลิกติดตามหัวข้อ

## Context menu options for sponsored stories and new ad formats on New Tab.

newtab-menu-manage-sponsored-content = จัดการเนื้อหาที่ได้รับการสนับสนุน
newtab-menu-our-sponsors-and-your-privacy = ผู้สนับสนุนของเราและความเป็นส่วนตัวของคุณ

## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = เสร็จสิ้น
newtab-privacy-modal-button-manage = จัดการการตั้งค่าเนื้อหาที่ได้รับการสนับสนุน
newtab-privacy-modal-header = ความเป็นส่วนตัวของคุณสำคัญ
newtab-privacy-modal-paragraph-2 =
    นอกเหนือจากการนำเสนอเรื่องราวที่น่าสนใจ เรายังแสดงให้คุณเห็นเนื้อหาที่เกี่ยวข้อง
    ซึ่งได้รับการตรวจสอบอย่างละเอียดจากผู้สนับสนุนที่ได้รับการคัดเลือก ทำให้คุณมั่นใจ
    ได้ว่า<strong>ข้อมูลการเรียกดูของคุณจะไม่ทิ้งสำเนาส่วนตัวของ { -brand-product-name } ของคุณ</strong>ซึ่งเราและ
    สปอนเซอร์ของเราจะไม่เห็น
newtab-privacy-modal-link = เรียนรู้วิธีการปกป้องความเป็นส่วนตัวในแท็บใหม่

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = เอาที่คั่นหน้าออก
# Bookmark is a verb here.
newtab-menu-bookmark = เพิ่มที่คั่นหน้า

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = คัดลอกลิงก์ดาวน์โหลด
newtab-menu-go-to-download-page = ไปยังหน้าดาวน์โหลด
newtab-menu-remove-download = เอาออกจากประวัติ

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] แสดงใน Finder
       *[other] เปิดโฟลเดอร์ที่บรรจุ
    }
newtab-menu-open-file = เปิดไฟล์

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = เยี่ยมชมแล้ว
newtab-label-bookmarked = เพิ่มที่คั่นหน้าแล้ว
newtab-label-removed-bookmark = เอาที่คั่นหน้าออกแล้ว
newtab-label-recommended = กำลังนิยม
newtab-label-saved = บันทึกไปยัง { -pocket-brand-name } แล้ว
newtab-label-download = ดาวน์โหลดแล้ว
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · ผู้สนับสนุน
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = สนับสนุนโดย { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } นาที

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = เอาส่วนออก
newtab-section-menu-collapse-section = ยุบส่วน
newtab-section-menu-expand-section = ขยายส่วน
newtab-section-menu-manage-section = จัดการส่วน
newtab-section-menu-manage-webext = จัดการส่วนขยาย
newtab-section-menu-add-topsite = เพิ่มไซต์เด่น
newtab-section-menu-add-search-engine = เพิ่มเครื่องมือค้นหา
newtab-section-menu-move-up = ย้ายขึ้น
newtab-section-menu-move-down = ย้ายลง
newtab-section-menu-privacy-notice = ข้อกำหนดความเป็นส่วนตัว

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = ยุบส่วน
newtab-section-expand-section-label =
    .aria-label = ขยายส่วน

## Section Headers.

newtab-section-header-topsites = ไซต์เด่น
newtab-section-header-recent-activity = กิจกรรมล่าสุด
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = แนะนำโดย { $provider }
newtab-section-header-stories = เรื่องราวที่จุดประกายความคิด
# "picks" refers to recommended articles
newtab-section-header-todays-picks = บทความคัดสรรสำหรับคุณประจำวันนี้

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = เริ่มเรียกดูและเราจะแสดงบทความ วิดีโอ และหน้าอื่น ๆ บางส่วนที่ยอดเยี่ยมที่คุณได้เยี่ยมชมหรือเพิ่มที่คั่นหน้าไว้ล่าสุดที่นี่
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = คุณได้อ่านเรื่องราวครบทั้งหมดแล้ว คุณสามารถกลับมาตรวจดูเรื่องราวเด่นจาก { $provider } ได้ภายหลัง อดใจรอไม่ได้งั้นหรือ? เลือกหัวข้อยอดนิยมเพื่อค้นหาเรื่องราวที่ยอดเยี่ยมจากเว็บต่าง ๆ
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = คุณได้อ่านเรื่องราวครบทั้งหมดแล้ว คุณสามารถกลับมาตรวจดูเรื่องราวเพิ่มเติมได้ภายหลัง อดใจรอไม่ได้งั้นหรือ? เลือกหัวข้อยอดนิยมเพื่อค้นหาเรื่องราวที่ยอดเยี่ยมจากเว็บต่างๆ

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = คุณได้อ่านเรื่องราวครบทั้งหมดแล้ว!
newtab-discovery-empty-section-topstories-content = คุณสามารถกลับมาตรวจดูเรื่องราวเพิ่มเติมได้ภายหลัง
newtab-discovery-empty-section-topstories-try-again-button = ลองอีกครั้ง
newtab-discovery-empty-section-topstories-loading = กำลังโหลด…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = อุ๊ปส์! เราโหลดส่วนนี้เกือบเสร็จแล้ว แต่ยังไม่เสร็จดี

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = หัวข้อยอดนิยม:
newtab-pocket-new-topics-title = ต้องการเรื่องราวเพิ่มเติมหรือไม่ ดูหัวข้อยอดนิยมเหล่านี้จาก { -pocket-brand-name }
newtab-pocket-more-recommendations = คำแนะนำเพิ่มเติม
newtab-pocket-learn-more = เรียนรู้เพิ่มเติม
newtab-pocket-cta-button = รับ { -pocket-brand-name }
newtab-pocket-cta-text = บันทึกเรื่องราวที่คุณรักลงใน { -pocket-brand-name } และเติมเต็มสมองของคุณด้วยบทความที่น่าหลงใหล
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } เป็นส่วนหนึ่งของตระกูล { -brand-product-name }
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = บันทึก
newtab-pocket-saved = บันทึกแล้ว

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = เรื่องราวที่คล้ายกัน
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = ฉันไม่สนใจ
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = ขอบคุณ ความคิดเห็นของคุณจะช่วยเราปรับปรุงฟีดของคุณให้ดีขึ้น
newtab-toast-dismiss-button =
    .title = ปิด
    .aria-label = ปิด

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = ค้นพบสุดยอดเว็บ
newtab-pocket-onboarding-cta = { -pocket-brand-name } สำรวจสิ่งพิมพ์ที่หลากหลายเพื่อนำเนื้อหาที่ให้ข้อมูล สร้างแรงบันดาลใจ และน่าเชื่อถือที่สุดมาสู่เบราว์เซอร์ { -brand-product-name } ของคุณ

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = อุปส์ มีบางอย่างผิดพลาดในการโหลดเนื้อหานี้
newtab-error-fallback-refresh-link = เรียกหน้าใหม่เพื่อลองอีกครั้ง

## Customization Menu

newtab-custom-shortcuts-title = ทางลัด
newtab-custom-shortcuts-subtitle = ไซต์ที่คุณบันทึกหรือเยี่ยมชม
newtab-custom-shortcuts-toggle =
    .label = ทางลัด
    .description = ไซต์ที่คุณบันทึกหรือเยี่ยมชม
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
       *[other] { $num } แถว
    }
newtab-custom-sponsored-sites = ทางลัดที่ได้รับการสนับสนุน
newtab-custom-pocket-title = แนะนำโดย { -pocket-brand-name }
newtab-custom-pocket-subtitle = เนื้อหาคัดสรรพิเศษโดย { -pocket-brand-name } ซึ่งเป็นส่วนหนึ่งของตระกูล { -brand-product-name }
newtab-custom-stories-toggle =
    .label = เรื่องราวแนะนำ
    .description = เนื้อหาคัดสรรพิเศษโดยผลิตภัณฑ์ตระกูล { -brand-product-name }
newtab-custom-pocket-sponsored = เรื่องราวที่ได้รับการสนับสนุน
newtab-custom-pocket-show-recent-saves = แสดงบันทึกล่าสุด
newtab-custom-recent-title = กิจกรรมล่าสุด
newtab-custom-recent-subtitle = ไซต์และเนื้อหาล่าสุดที่คัดสรรมา
newtab-custom-recent-toggle =
    .label = กิจกรรมล่าสุด
    .description = ไซต์และเนื้อหาล่าสุดที่คัดสรรมา
newtab-custom-weather-toggle =
    .label = พยากรณ์อากาศ
    .description = ดูพยากรณ์อากาศประจำวันนี้ได้อย่างรวดเร็ว
newtab-custom-close-button = ปิด
newtab-custom-settings = จัดการการตั้งค่าเพิ่มเติม

## New Tab Wallpapers

newtab-wallpaper-title = รูปพื้นหลัง
newtab-wallpaper-reset = กลับเป็นค่าเริ่มต้น
newtab-wallpaper-upload-image = อัปโหลดภาพ
newtab-wallpaper-custom-color = เลือกสี
# Variables
#   $file_size (number) - The number of the maximum image file size (in MB) that may be uploaded
newtab-wallpaper-error-max-file-size = ภาพมีขนาดเกินขีดจำกัดขนาดไฟล์ที่ { $file_size } MB โปรดลองอัปโหลดไฟล์ที่มีขนาดเล็กกว่านี้
newtab-wallpaper-error-file-type = เราไม่สามารถอัปโหลดไฟล์ของคุณได้ โปรดลองอีกครั้งด้วยชนิดไฟล์ที่ต่างจากนี้
newtab-wallpaper-light-red-panda = แพนด้าแดง
newtab-wallpaper-light-mountain = ภูเขาสีขาว
newtab-wallpaper-light-sky = ท้องฟ้าที่มีเมฆสีม่วงและสีชมพู
newtab-wallpaper-light-color = รูปทรงที่มีสีฟ้า สีชมพู และสีเหลือง
newtab-wallpaper-light-landscape = วิวภูเขาที่มีหมอกสีฟ้า
newtab-wallpaper-light-beach = ชายหาดที่มีต้นปาล์ม
newtab-wallpaper-dark-aurora = แสงเหนือ
newtab-wallpaper-dark-color = รูปทรงที่มีสีแดงและสีน้ำเงิน
newtab-wallpaper-dark-panda = แพนด้าแดงที่ซ่อนตัวอยู่ในป่า
newtab-wallpaper-dark-sky = วิวเมืองใต้ท้องฟ้ายามค่ำคืน
newtab-wallpaper-dark-mountain = วิวภูเขา
newtab-wallpaper-dark-city = วิวเมืองสีม่วง
newtab-wallpaper-dark-fox-anniversary = สุนัขจิ้งจอกอยู่บนทางเท้าใกล้ป่าแห่งหนึ่ง
newtab-wallpaper-light-fox-anniversary = สุนัขจิ้งจอกอยู่ในทุ่งหญ้าที่มีทิวทัศน์ภูเขาซึ่งเต็มไปด้วยหมอก

## Solid Colors

newtab-wallpaper-category-title-colors = สีทึบ
newtab-wallpaper-blue = น้ำเงิน
newtab-wallpaper-light-blue = น้ำเงินอ่อน
newtab-wallpaper-light-purple = ม่วงอ่อน
newtab-wallpaper-light-green = เขียวอ่อน
newtab-wallpaper-green = เขียว
newtab-wallpaper-beige = เบจ
newtab-wallpaper-yellow = เหลือง
newtab-wallpaper-orange = ส้ม
newtab-wallpaper-pink = ชมพู
newtab-wallpaper-light-pink = ชมพูอ่อน
newtab-wallpaper-red = แดง
newtab-wallpaper-dark-blue = น้ำเงินเข้ม
newtab-wallpaper-dark-purple = ม่วงเข้ม
newtab-wallpaper-dark-green = เขียวเข้ม
newtab-wallpaper-brown = น้ำตาล

## Abstract

newtab-wallpaper-category-title-abstract = นามธรรม
newtab-wallpaper-abstract-green = รูปร่างสีเขียว
newtab-wallpaper-abstract-blue = รูปร่างสีน้ำเงิน
newtab-wallpaper-abstract-purple = รูปร่างสีม่วง
newtab-wallpaper-abstract-orange = รูปร่างสีส้ม
newtab-wallpaper-gradient-orange = ไล่ระดับสีส้มและชมพู
newtab-wallpaper-abstract-blue-purple = รูปร่างสีน้ำเงินและสีม่วง
newtab-wallpaper-abstract-white-curves = สีขาวพร้อมเส้นโค้งแรเงา
newtab-wallpaper-abstract-purple-green = เกรเดียนท์สีอ่อนสีม่วงและสีเขียว
newtab-wallpaper-abstract-blue-purple-waves = รูปทรงคลื่นสีน้ำเงินและสีม่วง
newtab-wallpaper-abstract-black-waves = รูปทรงคลื่นสีดำ

## Celestial

newtab-wallpaper-category-title-photographs = ภาพถ่าย
newtab-wallpaper-beach-at-sunrise = ชายหาดตอนพระอาทิตย์ขึ้น
newtab-wallpaper-beach-at-sunset = ชายหาดตอนพระอาทิตย์ตก
newtab-wallpaper-storm-sky = ท้องฟ้ามีพายุ
newtab-wallpaper-sky-with-pink-clouds = ท้องฟ้ามีเมฆสีชมพู
newtab-wallpaper-red-panda-yawns-in-a-tree = แพนด้าแดงหาวอยู่บนต้นไม้
newtab-wallpaper-white-mountains = ภูเขาสีขาว
newtab-wallpaper-hot-air-balloons = บอลลูนลมร้อนหลากสีสันในช่วงกลางวัน
newtab-wallpaper-starry-canyon = คืนที่เต็มไปด้วยดวงดาวสีฟ้า
newtab-wallpaper-suspension-bridge = ภาพถ่ายสะพานแขวนสีเทาในช่วงกลางวัน
newtab-wallpaper-sand-dunes = เนินทรายสีขาว
newtab-wallpaper-palm-trees = ภาพเงาของต้นมะพร้าวในช่วงชั่วโมงทองคำ
newtab-wallpaper-forest-trees = ต้นไม้ที่ถูกปกคลุมด้วยหมอก
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = ภาพถ่ายโดย <a data-l10n-name="name-link">{ $author_string }</a> จาก <a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-header = ลองเลือกสีสันที่คุณชอบ
newtab-wallpaper-feature-highlight-content = เปลี่ยนโฉมแท็บใหม่ของคุณด้วยภาพพื้นหลัง
newtab-wallpaper-feature-highlight-button = เข้าใจแล้ว
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = ปิด
    .aria-label = ปิดป็อปอัป
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial

# “Celestial” referring to astronomy; positioned in or relating to the sky,
# or outer space as observed in astronomy.
# Not to be confused with religious definition of the word.
newtab-wallpaper-category-title-celestial = ท้องฟ้า
newtab-wallpaper-celestial-lunar-eclipse = จันทรุปราคา
newtab-wallpaper-celestial-earth-night = ภาพถ่ายตอนกลางคืนจากวงโคจรต่ำของโลก
newtab-wallpaper-celestial-starry-sky = ท้องฟ้าที่เต็มไปด้วยดวงดาว
newtab-wallpaper-celestial-eclipse-time-lapse = ภาพไทม์แลปส์ปรากฏการณ์จันทรุปราคา
newtab-wallpaper-celestial-black-hole = ภาพประกอบกาแล็กซี่หลุมดำ
newtab-wallpaper-celestial-river = ภาพถ่ายดาวเทียมของแม่น้ำ

## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = ดูพยากรณ์ใน { $provider }
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = สนับสนุนโดย { $provider }
newtab-weather-menu-change-location = เปลี่ยนตำแหน่งที่ตั้ง
newtab-weather-change-location-search-input-placeholder =
    .placeholder = ค้นหาตำแหน่งที่ตั้ง
    .aria-label = ค้นหาตำแหน่งที่ตั้ง
newtab-weather-change-location-search-input = ค้นหาตำแหน่งที่ตั้ง
newtab-weather-menu-weather-display = การแสดงผลพยากรณ์อากาศ
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = ธรรมดา
newtab-weather-menu-change-weather-display-simple = สลับเป็นมุมมองธรรมดา
newtab-weather-menu-weather-display-option-detailed = ละเอียด
newtab-weather-menu-change-weather-display-detailed = สลับเป็นมุมมองละเอียด
newtab-weather-menu-temperature-units = หน่วยอุณหภูมิ
newtab-weather-menu-temperature-option-fahrenheit = ฟาเรนไฮต์
newtab-weather-menu-temperature-option-celsius = เซลเซียส
newtab-weather-menu-change-temperature-units-fahrenheit = เปลี่ยนเป็นฟาเรนไฮต์
newtab-weather-menu-change-temperature-units-celsius = เปลี่ยนเป็นเซลเซียส
newtab-weather-menu-hide-weather = ซ่อนพยากรณ์อากาศในแท็บใหม่
newtab-weather-menu-learn-more = เรียนรู้เพิ่มเติม
# This message is shown if user is working offline
newtab-weather-error-not-available = ไม่มีข้อมูลพยากรณ์อากาศในขณะนี้

## Topic Labels

newtab-topic-label-business = ธุรกิจ
newtab-topic-label-career = อาชีพ
newtab-topic-label-education = การศึกษา
newtab-topic-label-arts = ความบันเทิง
newtab-topic-label-food = อาหาร
newtab-topic-label-health = สุขภาพ
newtab-topic-label-hobbies = เกมมิ่ง
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = เงิน
newtab-topic-label-society-parenting = การเลี้ยงลูก
newtab-topic-label-government = การเมือง
newtab-topic-label-education-science = วิทยาศาสตร์
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = เคล็ดลับการใช้ชีวิต
newtab-topic-label-sports = กีฬา
newtab-topic-label-tech = เทคโนโลยี
newtab-topic-label-travel = การท่องเที่ยว
newtab-topic-label-home = บ้านและสวน

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = เลือกหัวข้อเพื่อปรับแต่งฟีดของคุณ
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = เลือกหัวข้อสองหัวข้อขึ้นไป ผู้เชี่ยวชาญของเราจะจัดลำดับความสำคัญของเรื่องราวที่ปรับให้เหมาะกับความสนใจของคุณ ซึ่งสามารถอัปเดตได้ตลอดเวลา
newtab-topic-selection-save-button = บันทึก
newtab-topic-selection-cancel-button = ยกเลิก
newtab-topic-selection-button-maybe-later = ไว้ภายหลัง
newtab-topic-selection-privacy-link = เรียนรู้ว่าเราปกป้องและจัดการข้อมูลอย่างไร
newtab-topic-selection-button-update-interests = ปรับเปลี่ยนความสนใจของคุณ
newtab-topic-selection-button-pick-interests = เลือกความสนใจของคุณ

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = ติดตาม
newtab-section-following-button = ติดตามอยู่
newtab-section-unfollow-button = เลิกติดตาม

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = ปิดกั้น
newtab-section-blocked-button = ปิดกั้นแล้ว
newtab-section-unblock-button = เลิกปิดกั้น

## Confirmation modal for blocking a section

newtab-section-cancel-button = ยังไม่ทำตอนนี้
newtab-section-confirm-block-topic-p1 = คุณแน่ใจหรือไม่ว่าต้องการปิดกั้นหัวข้อนี้?
newtab-section-confirm-block-topic-p2 = หัวข้อที่ถูกปิดกั้นจะไม่ปรากฏในฟีดของคุณอีกต่อไป
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = ปิดกั้น { $topic }

## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-mangage-topics-title = หัวข้อ
newtab-section-manage-topics-button-v2 =
    .label = จัดการหัวข้อ
newtab-section-mangage-topics-followed-topics = ติดตามอยู่
newtab-section-mangage-topics-followed-topics-empty-state = คุณยังไม่ได้ติดตามหัวข้อใดๆ
newtab-section-mangage-topics-blocked-topics = ปิดกั้นอยู่
newtab-section-mangage-topics-blocked-topics-empty-state = คุณยังไม่ได้ปิดกั้นหัวข้อใดๆ
