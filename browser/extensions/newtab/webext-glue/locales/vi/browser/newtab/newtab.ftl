# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Thẻ mới
newtab-settings-button =
    .title = Tùy biến trang thẻ mới
newtab-personalize-settings-icon-label =
    .title = Cá nhân hóa thẻ mới
    .aria-label = Cài đặt
newtab-settings-dialog-label =
    .aria-label = Cài đặt
newtab-personalize-icon-label =
    .title = Cá nhân hóa thẻ mới
    .aria-label = Cá nhân hóa thẻ mới
newtab-personalize-dialog-label =
    .aria-label = Cá nhân hóa
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Tìm kiếm
    .aria-label = Tìm kiếm
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Tìm kiếm với { $engine } hoặc nhập địa chỉ
newtab-search-box-handoff-text-no-engine = Tìm kiếm hoặc nhập địa chỉ
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Tìm kiếm với { $engine } hoặc nhập địa chỉ
    .title = Tìm kiếm với { $engine } hoặc nhập địa chỉ
    .aria-label = Tìm kiếm với { $engine } hoặc nhập địa chỉ
newtab-search-box-handoff-input-no-engine =
    .placeholder = Tìm kiếm hoặc nhập địa chỉ
    .title = Tìm kiếm hoặc nhập địa chỉ
    .aria-label = Tìm kiếm hoặc nhập địa chỉ
newtab-search-box-text = Tìm kiếm trên mạng
newtab-search-box-input =
    .placeholder = Tìm kiếm trên mạng
    .aria-label = Tìm kiếm trên mạng

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Thêm công cụ tìm kiếm
newtab-topsites-add-shortcut-header = Lối tắt mới
newtab-topsites-edit-topsites-header = Sửa trang web hàng đầu
newtab-topsites-edit-shortcut-header = Chỉnh sửa lối tắt
newtab-topsites-add-shortcut-label = Thêm lối tắt
newtab-topsites-title-label = Tiêu đề
newtab-topsites-title-input =
    .placeholder = Nhập tiêu đề
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Nhập hoặc dán URL
newtab-topsites-url-validation = Yêu cầu URL hợp lệ
newtab-topsites-image-url-label = Hình ảnh Tuỳ chỉnh URL
newtab-topsites-use-image-link = Sử dụng hình ảnh tùy chỉnh…
newtab-topsites-image-validation = Không tải được hình ảnh. Hãy thử một URL khác.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Hủy bỏ
newtab-topsites-delete-history-button = Xóa khỏi lịch sử
newtab-topsites-save-button = Lưu lại
newtab-topsites-preview-button = Xem trước
newtab-topsites-add-button = Thêm

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Bạn có chắc bạn muốn xóa bỏ mọi thứ của trang này từ lịch sử?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Thao tác này không thể hoàn tác được.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Được tài trợ

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Mở bảng chọn
    .aria-label = Mở bảng chọn
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Gỡ bỏ
    .aria-label = Gỡ bỏ
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Mở bảng chọn
    .aria-label = Mở bảng chọn ngữ cảnh cho { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Chỉnh sửa trang web này
    .aria-label = Chỉnh sửa trang web này

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Chỉnh sửa
newtab-menu-open-new-window = Mở trong cửa sổ mới
newtab-menu-open-new-private-window = Mở trong cửa sổ riêng tư mới
newtab-menu-dismiss = Bỏ qua
newtab-menu-pin = Ghim
newtab-menu-unpin = Bỏ ghim
newtab-menu-delete-history = Xóa khỏi lịch sử
newtab-menu-save-to-pocket = Lưu vào { -pocket-brand-name }
newtab-menu-delete-pocket = Xóa khỏi { -pocket-brand-name }
newtab-menu-archive-pocket = Lưu trữ trong { -pocket-brand-name }
newtab-menu-show-privacy-info = Nhà tài trợ của chúng tôi và sự riêng tư của bạn
newtab-menu-about-fakespot = Về { -fakespot-brand-name }
newtab-menu-report-content = Báo cáo nội dung này
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = Chặn
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = Bỏ theo dõi chủ đề

## Context menu options for sponsored stories and new ad formats on New Tab.

newtab-menu-manage-sponsored-content = Quản lý nội dung được tài trợ
newtab-menu-our-sponsors-and-your-privacy = Nhà tài trợ của chúng tôi và sự riêng tư của bạn
newtab-menu-report-this-ad = Báo cáo quảng cáo này

## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Xong
newtab-privacy-modal-button-manage = Quản lý cài đặt nội dung được tài trợ
newtab-privacy-modal-header = Vấn đề riêng tư của bạn.
newtab-privacy-modal-paragraph-2 =
    Ngoài việc tận hưởng những câu chuyện hấp dẫn, chúng tôi cũng cho bạn thấy có liên quan,
    nội dung được đánh giá cao từ các nhà tài trợ chọn lọc. Hãy yên tâm, <strong>dữ liệu duyệt của bạn
    không bao giờ để lại bản sao { -brand-product-name }</strong> của bạn — chúng tôi không thể nhìn thấy nó
    và các tài trợ của chúng tôi cũng vậy.
newtab-privacy-modal-link = Tìm hiểu cách hoạt động của quyền riêng tư trên thẻ mới

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Xóa dấu trang
# Bookmark is a verb here.
newtab-menu-bookmark = Dấu trang

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Sao chép địa chỉ tải xuống
newtab-menu-go-to-download-page = Đi đến trang web tải xuống
newtab-menu-remove-download = Xóa khỏi lịch sử

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Hiển thị trong Finder
       *[other] Mở thư mục chứa
    }
newtab-menu-open-file = Mở tập tin

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Đã truy cập
newtab-label-bookmarked = Đã được đánh dấu
newtab-label-removed-bookmark = Đã xóa dấu trang
newtab-label-recommended = Xu hướng
newtab-label-saved = Đã lưu vào { -pocket-brand-name }
newtab-label-download = Đã tải xuống
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · Được tài trợ
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Được tài trợ bởi { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } phút

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Xoá mục
newtab-section-menu-collapse-section = Thu gọn mục
newtab-section-menu-expand-section = Mở rộng mục
newtab-section-menu-manage-section = Quản lý mục
newtab-section-menu-manage-webext = Quản lí tiện ích
newtab-section-menu-add-topsite = Thêm trang web hàng đầu
newtab-section-menu-add-search-engine = Thêm công cụ tìm kiếm
newtab-section-menu-move-up = Di chuyển lên
newtab-section-menu-move-down = Di chuyển xuống
newtab-section-menu-privacy-notice = Thông báo bảo mật

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Thu gọn mục
newtab-section-expand-section-label =
    .aria-label = Mở rộng mục

## Section Headers.

newtab-section-header-topsites = Trang web hàng đầu
newtab-section-header-recent-activity = Hoạt động gần đây
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Được đề xuất bởi { $provider }
newtab-section-header-stories = Những câu chuyện kích động tư tưởng
# "picks" refers to recommended articles
newtab-section-header-todays-picks = Lựa chọn hôm nay dành cho bạn

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Bắt đầu duyệt web và chúng tôi sẽ hiển thị một số bài báo, video, và các trang khác mà bạn đã xem hoặc đã đánh dấu tại đây.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Bạn đã bắt kịp. Kiểm tra lại sau để biết thêm các câu chuyện hàng đầu từ { $provider }. Không muốn đợi? Chọn một chủ đề phổ biến để tìm thêm những câu chuyện tuyệt vời từ khắp nơi trên web.
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = Bạn đã bắt kịp. Kiểm tra lại sau để biết thêm các câu chuyện. Không muốn đợi? Chọn một chủ đề phổ biến để tìm thêm những câu chuyện tuyệt vời từ khắp nơi trên web.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Bạn đã bắt kịp!
newtab-discovery-empty-section-topstories-content = Kiểm tra lại sau để biết thêm câu chuyện.
newtab-discovery-empty-section-topstories-try-again-button = Thử lại
newtab-discovery-empty-section-topstories-loading = Đang tải…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Rất tiếc! Chúng tôi gần như tải phần này, nhưng không hoàn toàn.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Các chủ đề phổ biến:
newtab-pocket-new-topics-title = Muốn nhiều câu chuyện hơn nữa? Xem các chủ đề phổ biến này từ { -pocket-brand-name }
newtab-pocket-more-recommendations = Nhiều khuyến nghị hơn
newtab-pocket-learn-more = Tìm hiểu thêm
newtab-pocket-cta-button = Sử dụng { -pocket-brand-name }
newtab-pocket-cta-text = Lưu những câu chuyện bạn yêu thích trong { -pocket-brand-name } và vui vẻ khi đọc chúng.
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } là một phần của gia đình { -brand-product-name }
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = Lưu
newtab-pocket-saved = Đã lưu

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = Thêm những nội dung giống thế này
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = Không hợp với tôi
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = Cảm ơn. Những phản hồi của bạn sẽ giúp chúng tôi cải thiện bản tin của bạn.
newtab-toast-dismiss-button =
    .title = Bỏ qua
    .aria-label = Bỏ qua

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Khám phá những điều tốt nhất của web
newtab-pocket-onboarding-cta = { -pocket-brand-name } khám phá nhiều loại ấn phẩm khác nhau để mang nội dung giàu thông tin, truyền cảm hứng và đáng tin cậy nhất đến ngay trình duyệt { -brand-product-name } của bạn.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Rất tiếc, đã xảy ra lỗi khi tải nội dung này.
newtab-error-fallback-refresh-link = Thử làm mới lại trang.

## Customization Menu

newtab-custom-shortcuts-title = Lối tắt
newtab-custom-shortcuts-subtitle = Các trang web bạn lưu hoặc truy cập
newtab-custom-shortcuts-toggle =
    .label = Lối tắt
    .description = Các trang web bạn lưu hoặc truy cập
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
       *[other] { $num } hàng
    }
newtab-custom-sponsored-sites = Các lối tắt được tài trợ
newtab-custom-pocket-title = Được đề xuất bởi { -pocket-brand-name }
newtab-custom-pocket-subtitle = Nội dung đặc biệt do { -pocket-brand-name }, một phần của { -brand-product-name }, quản lý
newtab-custom-stories-toggle =
    .label = Câu chuyện được đề xuất
    .description = Nội dung đặc biệt được quản lý bởi gia đình { -brand-product-name }
newtab-custom-pocket-sponsored = Câu chuyện được tài trợ
newtab-custom-pocket-show-recent-saves = Hiển thị các lần lưu gần đây
newtab-custom-recent-title = Hoạt động gần đây
newtab-custom-recent-subtitle = Tuyển chọn các trang và nội dung gần đây
newtab-custom-recent-toggle =
    .label = Hoạt động gần đây
    .description = Tuyển chọn các trang và nội dung gần đây
newtab-custom-weather-toggle =
    .label = Thời tiết
    .description = Sơ lược về dự báo hôm nay
newtab-custom-close-button = Đóng
newtab-custom-settings = Quản lý các cài đặt khác

## New Tab Wallpapers

newtab-wallpaper-title = Hình nền
newtab-wallpaper-reset = Đặt lại về mặc định
newtab-wallpaper-upload-image = Tải lên một ảnh
newtab-wallpaper-custom-color = Chọn màu
# Variables
#   $file_size (number) - The number of the maximum image file size (in MB) that may be uploaded
newtab-wallpaper-error-max-file-size = Hình ảnh vượt quá giới hạn kích thước tập tin { $file_size }MB. Vui lòng thử tải lên một tập tin nhỏ hơn.
newtab-wallpaper-error-file-type = Chúng tôi không thể tải lên tập tin của bạn. Vui lòng thử lại với loại tập tin khác.
newtab-wallpaper-light-red-panda = Gấu trúc đỏ
newtab-wallpaper-light-mountain = Núi trắng
newtab-wallpaper-light-sky = Bầu trời với những đám mây màu tím và hồng
newtab-wallpaper-light-color = Hình dạng màu xanh, hồng và vàng
newtab-wallpaper-light-landscape = Phong cảnh núi sương mù xanh
newtab-wallpaper-light-beach = Bãi biển có cây cọ
newtab-wallpaper-dark-aurora = Cực quang
newtab-wallpaper-dark-color = Hình dạng màu đỏ và màu xanh
newtab-wallpaper-dark-panda = Gấu trúc đỏ ẩn trong rừng
newtab-wallpaper-dark-sky = Cảnh quan thành phố với bầu trời đêm
newtab-wallpaper-dark-mountain = Phong cảnh núi
newtab-wallpaper-dark-city = Phong cảnh thành phố màu tím
newtab-wallpaper-dark-fox-anniversary = Một chú cáo đứng trên vỉa hè gần khu rừng
newtab-wallpaper-light-fox-anniversary = Một chú cáo trong cánh đồng xanh cỏ với phong cảnh núi non mờ sương

## Solid Colors

newtab-wallpaper-category-title-colors = Màu
newtab-wallpaper-blue = Xanh dương
newtab-wallpaper-light-blue = Xanh dương nhạt
newtab-wallpaper-light-purple = Tím nhạt
newtab-wallpaper-light-green = Xanh lục nhạt
newtab-wallpaper-green = Xanh lục
newtab-wallpaper-beige = Be
newtab-wallpaper-yellow = Vàng
newtab-wallpaper-orange = Da cam
newtab-wallpaper-pink = Hồng
newtab-wallpaper-light-pink = Hồng nhạt
newtab-wallpaper-red = Đỏ
newtab-wallpaper-dark-blue = Xanh dương đậm
newtab-wallpaper-dark-purple = Tím đậm
newtab-wallpaper-dark-green = Xanh lục đậm
newtab-wallpaper-brown = Nâu

## Abstract

newtab-wallpaper-category-title-abstract = Trừu tượng
newtab-wallpaper-abstract-green = Hình dạng màu xanh lục
newtab-wallpaper-abstract-blue = Hình dạng màu xanh dương
newtab-wallpaper-abstract-purple = Hình dạng màu tím
newtab-wallpaper-abstract-orange = Hình dạng màu cam
newtab-wallpaper-gradient-orange = Chuyển sắc màu cam và màu hồng
newtab-wallpaper-abstract-blue-purple = Hình dạng màu xanh dương và màu tím
newtab-wallpaper-abstract-white-curves = Màu trắng với các đường cong bóng mờ
newtab-wallpaper-abstract-purple-green = Chuyển sắc ánh sáng tím và xanh lá cây
newtab-wallpaper-abstract-blue-purple-waves = Hình dạng gợn sóng màu xanh dương và tím
newtab-wallpaper-abstract-black-waves = Hình dạng gợn sóng màu đen

## Celestial

newtab-wallpaper-category-title-photographs = Hình ảnh
newtab-wallpaper-beach-at-sunrise = Bãi biển lúc bình minh
newtab-wallpaper-beach-at-sunset = Bãi biển lúc hoàng hôn
newtab-wallpaper-storm-sky = Trời giông bão
newtab-wallpaper-sky-with-pink-clouds = Bầu trời với đám mây màu hồng
newtab-wallpaper-red-panda-yawns-in-a-tree = Gấu trúc đỏ ngáp trên cây
newtab-wallpaper-white-mountains = Núi trắng
newtab-wallpaper-hot-air-balloons = Các loại màu của bóng bay không khí nóng vào ban ngày
newtab-wallpaper-starry-canyon = Đêm sao màu xanh
newtab-wallpaper-suspension-bridge = Ảnh cầu treo màu xám chụp vào ban ngày
newtab-wallpaper-sand-dunes = Đồi cát trắng
newtab-wallpaper-palm-trees = Hình bóng của cây cọ dừa trong giờ vàng
newtab-wallpaper-blue-flowers = Ảnh chụp cận cảnh những bông hoa cánh xanh đang nở
newtab-wallpaper-forest-trees = Cây phủ đầy sương mù
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = Hình ảnh bởi <a data-l10n-name="name-link">{ $author_string }</a> trên <a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-header = Thử một chút màu sắc
newtab-wallpaper-feature-highlight-content = Mang lại diện mạo mới cho thẻ mới của bạn bằng hình nền.
newtab-wallpaper-feature-highlight-button = Đã hiểu
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = Bỏ qua
    .aria-label = Đóng cửa sổ bật lên
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial

# “Celestial” referring to astronomy; positioned in or relating to the sky,
# or outer space as observed in astronomy.
# Not to be confused with religious definition of the word.
newtab-wallpaper-category-title-celestial = Thiên thể
newtab-wallpaper-celestial-lunar-eclipse = Nguyệt thực
newtab-wallpaper-celestial-earth-night = Ảnh ban đêm từ quỹ đạo thấp của Trái Đất
newtab-wallpaper-celestial-starry-sky = Bầu trời đầy sao
newtab-wallpaper-celestial-eclipse-time-lapse = Thời gian trôi nhanh của nguyệt thực
newtab-wallpaper-celestial-black-hole = Minh họa lỗ đen trong thiên hà
newtab-wallpaper-celestial-river = Hình ảnh vệ tinh của sông

## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = Xem dự báo với { $provider }
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ Được tài trợ
newtab-weather-menu-change-location = Thay đổi khu vực
newtab-weather-change-location-search-input-placeholder =
    .placeholder = Tìm kiếm khu vực
    .aria-label = Tìm kiếm khu vực
newtab-weather-change-location-search-input = Tìm kiếm khu vực
newtab-weather-menu-weather-display = Cách hiển thị thời tiết
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = Đơn giản
newtab-weather-menu-change-weather-display-simple = Chuyển sang xem đơn giản
newtab-weather-menu-weather-display-option-detailed = Chi tiết
newtab-weather-menu-change-weather-display-detailed = Chuyển sang xem chi tiết
newtab-weather-menu-temperature-units = Đơn vị nhiệt độ
newtab-weather-menu-temperature-option-fahrenheit = Độ F
newtab-weather-menu-temperature-option-celsius = Độ C
newtab-weather-menu-change-temperature-units-fahrenheit = Chuyển sang độ F
newtab-weather-menu-change-temperature-units-celsius = Chuyển sang độ C
newtab-weather-menu-hide-weather = Ẩn thời tiết trên thẻ mới
newtab-weather-menu-learn-more = Tìm hiểu thêm
# This message is shown if user is working offline
newtab-weather-error-not-available = Dữ liệu thời tiết hiện không có sẵn.

## Topic Labels

newtab-topic-label-business = Kinh doanh
newtab-topic-label-career = Cơ hội nghề nghiệp
newtab-topic-label-education = Giáo dục
newtab-topic-label-arts = Giải trí
newtab-topic-label-food = Thực phẩm
newtab-topic-label-health = Sức khỏe
newtab-topic-label-hobbies = Trò chơi
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = Tài chính cá nhân
newtab-topic-label-society-parenting = Nuôi dạy con cái
newtab-topic-label-government = Chính trị
newtab-topic-label-education-science = Khoa học
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = Mẹo vặt cuộc sống
newtab-topic-label-sports = Thể thao
newtab-topic-label-tech = Công nghệ
newtab-topic-label-travel = Du lịch
newtab-topic-label-home = Nhà & vườn

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = Chọn chủ đề để tinh chỉnh nguồn cấp dữ liệu của bạn
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = Chọn hai hoặc nhiều chủ đề. Các người chuyên gia giám tuyển của chúng tôi sẽ ưu tiên những câu chuyện phù hợp với sở thích của bạn. Cập nhật bất cứ lúc nào.
newtab-topic-selection-save-button = Lưu
newtab-topic-selection-cancel-button = Hủy bỏ
newtab-topic-selection-button-maybe-later = Có lẽ để sau
newtab-topic-selection-privacy-link = Tìm hiểu cách chúng tôi bảo vệ và quản lý dữ liệu
newtab-topic-selection-button-update-interests = Cập nhật sở thích của bạn
newtab-topic-selection-button-pick-interests = Chọn sở thích của bạn

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = Theo dõi
newtab-section-following-button = Đang theo dõi
newtab-section-unfollow-button = Huỷ theo dõi

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = Chặn
newtab-section-blocked-button = Đã chặn
newtab-section-unblock-button = Bỏ chặn

## Confirmation modal for blocking a section

newtab-section-cancel-button = Không phải bây giờ
newtab-section-confirm-block-topic-p1 = Bạn có chắc là bạn muốn chặn chủ đề này?
newtab-section-confirm-block-topic-p2 = Chủ đề bị chặn sẽ không còn xuất hiện trong nguồn cấp dữ liệu của bạn.
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = Chặn { $topic }

## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-mangage-topics-title = Các chủ đề
newtab-section-manage-topics-button-v2 =
    .label = Quản lý chủ đề
newtab-section-mangage-topics-followed-topics = Đã theo dõi
newtab-section-mangage-topics-followed-topics-empty-state = Bạn chưa theo dõi bất kỳ chủ đề nào.
newtab-section-mangage-topics-blocked-topics = Đã chặn
newtab-section-mangage-topics-blocked-topics-empty-state = Bạn chưa chặn bất kỳ chủ đề nào.
newtab-custom-wallpaper-title = Hình nền tùy chỉnh ở đây
# 'Make firefox yours" means to customize or personalize
newtab-custom-wallpaper-subtitle = Tải lên hình nền của bạn hoặc chọn một màu tùy chỉnh để biến { -brand-product-name } thành của riêng bạn.
newtab-custom-wallpaper-cta = Thử ngay
