# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = 新分頁
newtab-settings-button =
    .title = 自訂您的新分頁頁面
newtab-personalize-settings-icon-label =
    .title = 個人化新分頁
    .aria-label = 設定
newtab-settings-dialog-label =
    .aria-label = 設定
newtab-personalize-icon-label =
    .title = 個人化新分頁
    .aria-label = 個人化新分頁
newtab-personalize-dialog-label =
    .aria-label = 個人化
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = 搜尋
    .aria-label = 搜尋
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = 使用 { $engine } 搜尋或輸入網址
newtab-search-box-handoff-text-no-engine = 搜尋或輸入網址
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = 使用 { $engine } 搜尋或輸入網址
    .title = 使用 { $engine } 搜尋或輸入網址
    .aria-label = 使用 { $engine } 搜尋或輸入網址
newtab-search-box-handoff-input-no-engine =
    .placeholder = 搜尋或輸入網址
    .title = 搜尋或輸入網址
    .aria-label = 搜尋或輸入網址
newtab-search-box-text = 搜尋 Web
newtab-search-box-input =
    .placeholder = 搜尋 Web
    .aria-label = 搜尋 Web

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = 新增搜尋引擎
newtab-topsites-add-shortcut-header = 新增捷徑
newtab-topsites-edit-topsites-header = 編輯熱門網站
newtab-topsites-edit-shortcut-header = 編輯捷徑
newtab-topsites-add-shortcut-label = 新增捷徑
newtab-topsites-title-label = 標題
newtab-topsites-title-input =
    .placeholder = 輸入標題
newtab-topsites-url-label = 網址
newtab-topsites-url-input =
    .placeholder = 輸入或貼上網址
newtab-topsites-url-validation = 請輸入有效的網址
newtab-topsites-image-url-label = 自訂圖片網址
newtab-topsites-use-image-link = 使用自訂圖片…
newtab-topsites-image-validation = 圖片載入失敗，請改用不同網址。

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = 取消
newtab-topsites-delete-history-button = 從瀏覽紀錄刪除
newtab-topsites-save-button = 儲存
newtab-topsites-preview-button = 預覽
newtab-topsites-add-button = 新增

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = 您確定要刪除此頁面的所有瀏覽紀錄？
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = 此動作無法復原。

## Top Sites - Sponsored label

newtab-topsite-sponsored = 贊助項目

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = 開啟選單
    .aria-label = 開啟選單
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = 移除
    .aria-label = 移除
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = 開啟選單
    .aria-label = 開啟 { $title } 的右鍵選單
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = 編輯此網站
    .aria-label = 編輯此網站

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = 編輯
newtab-menu-open-new-window = 用新視窗開啟
newtab-menu-open-new-private-window = 用新隱私視窗開啟
newtab-menu-dismiss = 隱藏
newtab-menu-pin = 釘選
newtab-menu-unpin = 取消釘選
newtab-menu-delete-history = 從瀏覽紀錄刪除
newtab-menu-save-to-pocket = 儲存至 { -pocket-brand-name }
newtab-menu-delete-pocket = 從 { -pocket-brand-name } 刪除
newtab-menu-archive-pocket = 在 { -pocket-brand-name } 裡封存
newtab-menu-show-privacy-info = 我們的贊助商與您的隱私權
newtab-menu-about-fakespot = 關於 { -fakespot-brand-name }
newtab-menu-report-content = 回報此內容
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = 封鎖
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = 取消追蹤主題

## Context menu options for sponsored stories and new ad formats on New Tab.

newtab-menu-manage-sponsored-content = 管理贊助內容
newtab-menu-our-sponsors-and-your-privacy = 我們的贊助商與您的隱私權
newtab-menu-report-this-ad = 回報此廣告

## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = 完成
newtab-privacy-modal-button-manage = 管理贊助內容設定
newtab-privacy-modal-header = 您的隱私相當重要。
newtab-privacy-modal-paragraph-2 = 除了提供吸引人的文章之外，我們還與贊助商合作提供與您相關，且經精挑細選的內容。請放心，<strong>您的上網資料絕對不會流出於您電腦上的 { -brand-product-name } 之外</strong>— 我們跟我們的贊助商都不會看到。
newtab-privacy-modal-link = 了解我們如何在提供新分頁內容的同時確保您的隱私

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = 移除書籤
# Bookmark is a verb here.
newtab-menu-bookmark = 書籤

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = 複製下載鏈結
newtab-menu-go-to-download-page = 前往下載頁面
newtab-menu-remove-download = 自下載記錄移除

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] 顯示於 Finder
       *[other] 開啟所在資料夾
    }
newtab-menu-open-file = 開啟檔案

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = 造訪過的網站
newtab-label-bookmarked = 已加入書籤
newtab-label-removed-bookmark = 已移除書籤
newtab-label-recommended = 熱門
newtab-label-saved = 已儲存至 { -pocket-brand-name }
newtab-label-download = 已下載
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · 贊助
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = 由 { $sponsor } 贊助
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } 分鐘

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = 移除段落
newtab-section-menu-collapse-section = 摺疊段落
newtab-section-menu-expand-section = 展開段落
newtab-section-menu-manage-section = 管理段落
newtab-section-menu-manage-webext = 管理擴充套件
newtab-section-menu-add-topsite = 新增熱門網站
newtab-section-menu-add-search-engine = 新增搜尋引擎
newtab-section-menu-move-up = 上移
newtab-section-menu-move-down = 下移
newtab-section-menu-privacy-notice = 隱私權公告

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = 摺疊段落
newtab-section-expand-section-label =
    .aria-label = 展開段落

## Section Headers.

newtab-section-header-topsites = 熱門網站
newtab-section-header-recent-activity = 近期動態
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = { $provider } 推薦
newtab-section-header-stories = 發人深省的文章
# "picks" refers to recommended articles
newtab-section-header-todays-picks = 本日精選文章

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = 開始上網，我們就會把您在網路上發現的好文章、影片、剛加入書籤的頁面顯示於此。
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = 所有文章都讀完啦！晚點再來，{ $provider } 將提供更多推薦故事。等不及了？選擇熱門主題，看看 Web 上各式精采資訊。
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = 所有文章都讀完啦！晚點再來看看更多推薦故事。等不及了？選擇熱門主題，看看 Web 上各式精采資訊。

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = 都讀完了！
newtab-discovery-empty-section-topstories-content = 晚點再回來看看有沒有新鮮事。
newtab-discovery-empty-section-topstories-try-again-button = 重試
newtab-discovery-empty-section-topstories-loading = 載入中…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = 唉呀，暫時無法載入此區塊。

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = 熱門主題:
newtab-pocket-new-topics-title = 想要更多文章嗎？看這些來自 { -pocket-brand-name } 的熱門主題
newtab-pocket-more-recommendations = 更多推薦項目
newtab-pocket-learn-more = 了解更多
newtab-pocket-cta-button = 取得 { -pocket-brand-name }
newtab-pocket-cta-text = 將您喜愛的故事儲存到 { -pocket-brand-name }，閱讀一篇篇好文章。
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } 是 { -brand-product-name } 產品家族的一部份
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = 儲存
newtab-pocket-saved = 已儲存

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = 更多這樣的內容
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = 我沒興趣
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = 感謝您。您的意見可幫助我們改善顯示的內容。
newtab-toast-dismiss-button =
    .title = 知道了！
    .aria-label = 知道了！

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = 探索網路精華內容
newtab-pocket-onboarding-cta = { -pocket-brand-name } 為您探索不同的線上內容，將最豐富、最有啟發性、最可靠的內容帶來您的 { -brand-product-name } 瀏覽器。

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = 唉唷，載入內容時發生錯誤。
newtab-error-fallback-refresh-link = 請重新整理頁面再試一次。

## Customization Menu

newtab-custom-shortcuts-title = 捷徑
newtab-custom-shortcuts-subtitle = 您儲存或造訪過的網站
newtab-custom-shortcuts-toggle =
    .label = 捷徑
    .description = 您儲存或造訪過的網站
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } 行
       *[other] { $num } 行
    }
newtab-custom-sponsored-sites = 贊助捷徑
newtab-custom-pocket-title = 由 { -pocket-brand-name } 推薦
newtab-custom-pocket-subtitle = 由 { -brand-product-name } 的姊妹作 { -pocket-brand-name } 精心策展的內容
newtab-custom-stories-toggle =
    .label = 推薦的文章
    .description = 由 { -brand-product-name } 產品家族精選的內容文章
newtab-custom-pocket-sponsored = 贊助內容
newtab-custom-pocket-show-recent-saves = 顯示近期儲存項目
newtab-custom-recent-title = 近期動態
newtab-custom-recent-subtitle = 近期造訪過的網站與內容精選
newtab-custom-recent-toggle =
    .label = 近期動態
    .description = 近期造訪過的網站與內容精選
newtab-custom-weather-toggle =
    .label = 天氣
    .description = 快速了解本日天氣
newtab-custom-close-button = 關閉
newtab-custom-settings = 管理更多設定

## New Tab Wallpapers

newtab-wallpaper-title = 背景圖
newtab-wallpaper-reset = 還原為預設值
newtab-wallpaper-upload-image = 上傳圖片
newtab-wallpaper-custom-color = 選擇一種色彩
# Variables
#   $file_size (number) - The number of the maximum image file size (in MB) that may be uploaded
newtab-wallpaper-error-max-file-size = 圖片超過 { $file_size }MB 的檔案大小限制。請嘗試上傳小一點的檔案。
newtab-wallpaper-error-file-type = 無法上傳您的檔案，請稍後再以不同格式檔案上傳。
newtab-wallpaper-light-red-panda = 小貓熊
newtab-wallpaper-light-mountain = 白色山脈
newtab-wallpaper-light-sky = 紫色與粉紅色的天空
newtab-wallpaper-light-color = 藍色、粉紅與黃色圖型
newtab-wallpaper-light-landscape = 藍霧山景
newtab-wallpaper-light-beach = 棕櫚樹海灘
newtab-wallpaper-dark-aurora = 極光
newtab-wallpaper-dark-color = 紅色與藍色圖型
newtab-wallpaper-dark-panda = 隱藏在森林中的小貓熊
newtab-wallpaper-dark-sky = 城市的夜空景觀
newtab-wallpaper-dark-mountain = 山景
newtab-wallpaper-dark-city = 紫色城市風景
newtab-wallpaper-dark-fox-anniversary = 一隻在森林附近人行道上的狐狸
newtab-wallpaper-light-fox-anniversary = 一隻在迷霧山景中的草原上的狐狸

## Solid Colors

newtab-wallpaper-category-title-colors = 純色
newtab-wallpaper-blue = 藍色
newtab-wallpaper-light-blue = 淺藍色
newtab-wallpaper-light-purple = 淺紫色
newtab-wallpaper-light-green = 淺綠色
newtab-wallpaper-green = 綠色
newtab-wallpaper-beige = 米色
newtab-wallpaper-yellow = 黃色
newtab-wallpaper-orange = 橘色
newtab-wallpaper-pink = 粉紅色
newtab-wallpaper-light-pink = 淺粉紅色
newtab-wallpaper-red = 紅色
newtab-wallpaper-dark-blue = 深藍色
newtab-wallpaper-dark-purple = 深紫色
newtab-wallpaper-dark-green = 深綠色
newtab-wallpaper-brown = 棕色

## Abstract

newtab-wallpaper-category-title-abstract = 抽象派
newtab-wallpaper-abstract-green = 綠色造型
newtab-wallpaper-abstract-blue = 藍色造型
newtab-wallpaper-abstract-purple = 紫色造型
newtab-wallpaper-abstract-orange = 橘色造型
newtab-wallpaper-gradient-orange = 橘色粉紅色漸層
newtab-wallpaper-abstract-blue-purple = 藍色紫色造型
newtab-wallpaper-abstract-white-curves = 白色的曲線圖案
newtab-wallpaper-abstract-purple-green = 紫色與綠色漸層
newtab-wallpaper-abstract-blue-purple-waves = 藍色與紫色波浪圖
newtab-wallpaper-abstract-black-waves = 黑色波浪圖

## Celestial

newtab-wallpaper-category-title-photographs = 相片
newtab-wallpaper-beach-at-sunrise = 海邊日出
newtab-wallpaper-beach-at-sunset = 海邊日落
newtab-wallpaper-storm-sky = 暴風雨的天空
newtab-wallpaper-sky-with-pink-clouds = 有粉紅色雲朵的天空
newtab-wallpaper-red-panda-yawns-in-a-tree = 在樹上打呵欠的小貓熊
newtab-wallpaper-white-mountains = 白色山脈
newtab-wallpaper-hot-air-balloons = 於白天有各種色彩的熱氣球
newtab-wallpaper-starry-canyon = 藍色星空
newtab-wallpaper-suspension-bridge = 白天的灰色吊橋照片
newtab-wallpaper-sand-dunes = 白色沙丘
newtab-wallpaper-palm-trees = 在魔術光下的椰子樹剪影
newtab-wallpaper-blue-flowers = 盛開的藍色花朵特寫
newtab-wallpaper-forest-trees = 被濃霧遮蓋住的樹
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = 相片由 <a data-l10n-name="name-link">{ $author_string }</a> 於 <a data-l10n-name="webpage-link">{ $webpage_string }</a> 提供
newtab-wallpaper-feature-highlight-header = 試用新色彩
newtab-wallpaper-feature-highlight-content = 讓您的「新分頁」耳目一新！
newtab-wallpaper-feature-highlight-button = 知道了！
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = 隱藏
    .aria-label = 關閉彈出視窗
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial

# “Celestial” referring to astronomy; positioned in or relating to the sky,
# or outer space as observed in astronomy.
# Not to be confused with religious definition of the word.
newtab-wallpaper-category-title-celestial = 天空
newtab-wallpaper-celestial-lunar-eclipse = 月食
newtab-wallpaper-celestial-earth-night = 從低地球軌道拍攝的夜晚照片
newtab-wallpaper-celestial-starry-sky = 星空
newtab-wallpaper-celestial-eclipse-time-lapse = 月食縮時攝影
newtab-wallpaper-celestial-black-hole = 黑洞銀河插圖
newtab-wallpaper-celestial-river = 河流的衛星照片

## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = 到 { $provider } 檢視天氣預報
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ 贊助資訊
newtab-weather-menu-change-location = 更改位置
newtab-weather-change-location-search-input-placeholder =
    .placeholder = 搜尋位置
    .aria-label = 搜尋位置
newtab-weather-change-location-search-input = 搜尋位置
newtab-weather-menu-weather-display = 顯示天氣
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = 簡潔
newtab-weather-menu-change-weather-display-simple = 切換為簡潔畫面
newtab-weather-menu-weather-display-option-detailed = 詳細
newtab-weather-menu-change-weather-display-detailed = 切換為詳細畫面
newtab-weather-menu-temperature-units = 溫度單位
newtab-weather-menu-temperature-option-fahrenheit = 華氏
newtab-weather-menu-temperature-option-celsius = 攝氏
newtab-weather-menu-change-temperature-units-fahrenheit = 切換為華氏溫度
newtab-weather-menu-change-temperature-units-celsius = 切換為攝氏溫度
newtab-weather-menu-hide-weather = 隱藏新分頁的天氣資訊
newtab-weather-menu-learn-more = 更多資訊
# This message is shown if user is working offline
newtab-weather-error-not-available = 目前暫時無法提供天氣資訊。

## Topic Labels

newtab-topic-label-business = 商業
newtab-topic-label-career = 職涯
newtab-topic-label-education = 教育
newtab-topic-label-arts = 娛樂
newtab-topic-label-food = 美食
newtab-topic-label-health = 健康
newtab-topic-label-hobbies = 遊戲
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = 個人財務
newtab-topic-label-society-parenting = 育兒
newtab-topic-label-government = 政治
newtab-topic-label-education-science = 科學
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = 自我成長
newtab-topic-label-sports = 體育
newtab-topic-label-tech = 科技
newtab-topic-label-travel = 旅遊
newtab-topic-label-home = 家庭與園藝

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = 請選擇主題來調整您的資訊來源
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = 選擇兩組以上的主題，我們的策展專家會依照您的興趣優先顯示。內容隨時更新。
newtab-topic-selection-save-button = 儲存
newtab-topic-selection-cancel-button = 取消
newtab-topic-selection-button-maybe-later = 之後再說
newtab-topic-selection-privacy-link = 了解我們如何保護與管理資料
newtab-topic-selection-button-update-interests = 更新您有興趣的項目
newtab-topic-selection-button-pick-interests = 挑選您有興趣的項目

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = 追蹤
newtab-section-following-button = 追蹤中
newtab-section-unfollow-button = 取消追蹤

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = 封鎖
newtab-section-blocked-button = 已封鎖
newtab-section-unblock-button = 解除封鎖

## Confirmation modal for blocking a section

newtab-section-cancel-button = 現在不要
newtab-section-confirm-block-topic-p1 = 您確定要封鎖這個主題的內容嗎？
newtab-section-confirm-block-topic-p2 = 將主題封鎖後就不會再顯示於資訊來源中。
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = 封鎖 { $topic }

## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-mangage-topics-title = 主題
newtab-section-manage-topics-button-v2 =
    .label = 管理主題
newtab-section-mangage-topics-followed-topics = 已追蹤
newtab-section-mangage-topics-followed-topics-empty-state = 您並未關注任何主題。
newtab-section-mangage-topics-blocked-topics = 已封鎖
newtab-section-mangage-topics-blocked-topics-empty-state = 您並未封鎖任何主題。
newtab-custom-wallpaper-title = 可以在這裡自訂背景圖片
# 'Make firefox yours" means to customize or personalize
newtab-custom-wallpaper-subtitle = 上傳您自己的背景圖，或挑選一組色彩，讓 { -brand-product-name } 有您的風格。
newtab-custom-wallpaper-cta = 試試看
