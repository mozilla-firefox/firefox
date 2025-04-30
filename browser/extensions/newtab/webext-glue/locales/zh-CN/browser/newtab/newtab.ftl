# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = 新标签页
newtab-settings-button =
    .title = 定制您的新标签页
newtab-personalize-settings-icon-label =
    .title = 个性化新标签页
    .aria-label = 设置
newtab-settings-dialog-label =
    .aria-label = 设置
newtab-personalize-icon-label =
    .title = 个性化标签页
    .aria-label = 个性化标签页
newtab-personalize-dialog-label =
    .aria-label = 个性化
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = 搜索
    .aria-label = 搜索
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = 使用 { $engine } 搜索，或者输入网址
newtab-search-box-handoff-text-no-engine = 搜索或输入网址
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = 使用 { $engine } 搜索，或者输入网址
    .title = 使用 { $engine } 搜索，或者输入网址
    .aria-label = 使用 { $engine } 搜索，或者输入网址
newtab-search-box-handoff-input-no-engine =
    .placeholder = 搜索或输入网址
    .title = 搜索或输入网址
    .aria-label = 搜索或输入网址
newtab-search-box-text = 网上搜索
newtab-search-box-input =
    .placeholder = 网上搜索
    .aria-label = 网上搜索

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = 添加搜索引擎
newtab-topsites-add-shortcut-header = 新建快捷方式
newtab-topsites-edit-topsites-header = 编辑常用网站
newtab-topsites-edit-shortcut-header = 编辑快捷方式
newtab-topsites-add-shortcut-label = 添加快捷方式
newtab-topsites-title-label = 标题
newtab-topsites-title-input =
    .placeholder = 输入标题
newtab-topsites-url-label = 网址
newtab-topsites-url-input =
    .placeholder = 输入或粘贴网址
newtab-topsites-url-validation = 需要有效的网址
newtab-topsites-image-url-label = 自定义图像网址
newtab-topsites-use-image-link = 使用自定义图像…
newtab-topsites-image-validation = 图像加载失败。请尝试其他网址。

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = 取消
newtab-topsites-delete-history-button = 从历史记录中删除
newtab-topsites-save-button = 保存
newtab-topsites-preview-button = 预览
newtab-topsites-add-button = 添加

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = 您确定要删除此页面在您的历史记录中的所有记录吗？
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = 此操作无法撤销。

## Top Sites - Sponsored label

newtab-topsite-sponsored = 赞助推广

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = 打开菜单
    .aria-label = 打开菜单
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = 移除
    .aria-label = 移除
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = 打开菜单
    .aria-label = 打开 { $title } 的快捷菜单
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = 编辑此网站
    .aria-label = 编辑此网站

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = 编辑
newtab-menu-open-new-window = 新建窗口打开
newtab-menu-open-new-private-window = 新建隐私浏览窗口打开
newtab-menu-dismiss = 隐藏
newtab-menu-pin = 固定
newtab-menu-unpin = 取消固定
newtab-menu-delete-history = 从历史记录中删除
newtab-menu-save-to-pocket = 保存到 { -pocket-brand-name }
newtab-menu-delete-pocket = 从 { -pocket-brand-name } 删除
newtab-menu-archive-pocket = 在 { -pocket-brand-name } 中存档
newtab-menu-show-privacy-info = 我们的赞助商＆您的隐私
newtab-menu-about-fakespot = 关于 { -fakespot-brand-name }
newtab-menu-report-content = 举报此内容
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = 屏蔽
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = 取消关注主题

## Context menu options for sponsored stories and new ad formats on New Tab.

newtab-menu-manage-sponsored-content = 管理赞助内容
newtab-menu-our-sponsors-and-your-privacy = 我们的赞助商与您的隐私
newtab-menu-report-this-ad = 举报此广告

## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = 完成
newtab-privacy-modal-button-manage = 管理赞助内容设置
newtab-privacy-modal-header = 隐私是公民的基本权利。
newtab-privacy-modal-paragraph-2 = 除了提供引人入胜的文章之外，我们还与赞助商合作展示有价值，且经甄选的内容。请放心，<strong>您的浏览数据永远只会留在本机 { -brand-product-name }</strong> 中 — 我们看不到，我们的赞助商亦然。
newtab-privacy-modal-link = 了解新标签页如何保障您的隐私

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = 删除书签
# Bookmark is a verb here.
newtab-menu-bookmark = 添加书签

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = 复制下载链接
newtab-menu-go-to-download-page = 前往下载页面
newtab-menu-remove-download = 从历史记录中移除

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] 在访达中显示
       *[other] 打开所在文件夹
    }
newtab-menu-open-file = 打开文件

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = 曾经访问
newtab-label-bookmarked = 已加书签
newtab-label-removed-bookmark = 书签已移除
newtab-label-recommended = 趋势
newtab-label-saved = 已保存到 { -pocket-brand-name }
newtab-label-download = 已下载
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · 赞助
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = 由 { $sponsor } 赞助
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } 分钟

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = 移除版块
newtab-section-menu-collapse-section = 折叠版块
newtab-section-menu-expand-section = 展开版块
newtab-section-menu-manage-section = 管理版块
newtab-section-menu-manage-webext = 管理扩展
newtab-section-menu-add-topsite = 添加常用网站
newtab-section-menu-add-search-engine = 添加搜索引擎
newtab-section-menu-move-up = 上移
newtab-section-menu-move-down = 下移
newtab-section-menu-privacy-notice = 隐私声明

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = 折叠版块
newtab-section-expand-section-label =
    .aria-label = 展开版块

## Section Headers.

newtab-section-header-topsites = 常用网站
newtab-section-header-recent-activity = 近期动态
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = { $provider } 推荐
newtab-section-header-stories = 精选文章
# "picks" refers to recommended articles
newtab-section-header-todays-picks = 今日专属荐读

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = 开始网上冲浪之旅吧，之后这里会显示您最近看过或加了书签的精彩文章、视频与其他页面。
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = 所有文章都读完啦！晚点再来，{ $provider } 将推荐更多精彩文章。等不及了？选择热门主题，找到更多网上的好文章。
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = 所有文章都读完了。待会再来看是否有新文章。等不及？那么请选择热门主题，从网上找到更多好文章。

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = 都读完了！
newtab-discovery-empty-section-topstories-content = 待会再来看是否有新文章。
newtab-discovery-empty-section-topstories-try-again-button = 重试
newtab-discovery-empty-section-topstories-loading = 正在加载…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = 哎呀！无法完全加载此版块。

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = 热门主题：
newtab-pocket-new-topics-title = 想刷到更多文章？看看这些 { -pocket-brand-name } 上的热门主题
newtab-pocket-more-recommendations = 更多推荐
newtab-pocket-learn-more = 详细了解
newtab-pocket-cta-button = 获取 { -pocket-brand-name }
newtab-pocket-cta-text = 将您喜爱的故事保存到 { -pocket-brand-name }，用精彩的读物为思想注入活力。
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } 是 { -brand-product-name } 系列产品的一部分
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = 保存
newtab-pocket-saved = 已保存

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = 再多来点
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = 不感兴趣
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = 谢谢，您的反馈有助于我们改进为您提供的推送。
newtab-toast-dismiss-button =
    .title = 知道了
    .aria-label = 知道了

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = 发现最好的网络
newtab-pocket-onboarding-cta = { -pocket-brand-name } 探索各种各样的出版物，为您的 { -brand-product-name } 浏览器带来最翔实、最鼓舞人心和最值得信赖的内容。

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = 哎呀，加载内容时发生错误。
newtab-error-fallback-refresh-link = 刷新页面以重试。

## Customization Menu

newtab-custom-shortcuts-title = 快捷方式
newtab-custom-shortcuts-subtitle = 您保存或访问过的网站
newtab-custom-shortcuts-toggle =
    .label = 快捷方式
    .description = 您保存或访问过的网站
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
       *[other] { $num } 行
    }
newtab-custom-sponsored-sites = 赞助商网站
newtab-custom-pocket-title = 由 { -pocket-brand-name } 推荐
newtab-custom-pocket-subtitle = 由 { -brand-product-name } 旗下 { -pocket-brand-name } 策划的特别内容
newtab-custom-stories-toggle =
    .label = 推荐文章
    .description = 由 { -brand-product-name } 推荐的精选内容
newtab-custom-pocket-sponsored = 赞助内容
newtab-custom-pocket-show-recent-saves = 显示近期保存内容
newtab-custom-recent-title = 近期动态
newtab-custom-recent-subtitle = 近期访问的网站与内容精选
newtab-custom-recent-toggle =
    .label = 近期动态
    .description = 近期访问的网站与内容精选
newtab-custom-weather-toggle =
    .label = 天气
    .description = 速览今日天气预报
newtab-custom-close-button = 关闭
newtab-custom-settings = 管理更多设置

## New Tab Wallpapers

newtab-wallpaper-title = 壁纸
newtab-wallpaper-reset = 重置为默认设置
newtab-wallpaper-upload-image = 上传图像
newtab-wallpaper-custom-color = 选择颜色
# Variables
#   $file_size (number) - The number of the maximum image file size (in MB) that may be uploaded
newtab-wallpaper-error-max-file-size = 图像超出文件大小上限（{ $file_size }MB），请尝试上传较小的文件。
newtab-wallpaper-error-file-type = 无法上传文件，请尝试使用其他文件类型。
newtab-wallpaper-light-red-panda = 小熊猫
newtab-wallpaper-light-mountain = 白山山脉
newtab-wallpaper-light-sky = 漂浮着粉紫色云的天空
newtab-wallpaper-light-color = 蓝色、粉色和黄色的形状
newtab-wallpaper-light-landscape = 淡蓝薄雾笼罩下的山地景观
newtab-wallpaper-light-beach = 生长着棕榈树的海滩
newtab-wallpaper-dark-aurora = 极光
newtab-wallpaper-dark-color = 红色和蓝色的形状
newtab-wallpaper-dark-panda = 躲在森林里的小熊猫
newtab-wallpaper-dark-sky = 夜空下的城市景观
newtab-wallpaper-dark-mountain = 山地景观
newtab-wallpaper-dark-city = 紫色城市景观
newtab-wallpaper-dark-fox-anniversary = 树林旁边人行道上的狐狸
newtab-wallpaper-light-fox-anniversary = 迷蒙山景中草地上的狐狸

## Solid Colors

newtab-wallpaper-category-title-colors = 纯色
newtab-wallpaper-blue = 蓝色
newtab-wallpaper-light-blue = 淡蓝色
newtab-wallpaper-light-purple = 淡紫色
newtab-wallpaper-light-green = 淡绿色
newtab-wallpaper-green = 绿色
newtab-wallpaper-beige = 米色
newtab-wallpaper-yellow = 黄色
newtab-wallpaper-orange = 橙色
newtab-wallpaper-pink = 粉色
newtab-wallpaper-light-pink = 淡粉色
newtab-wallpaper-red = 红色
newtab-wallpaper-dark-blue = 深蓝色
newtab-wallpaper-dark-purple = 深紫色
newtab-wallpaper-dark-green = 深绿色
newtab-wallpaper-brown = 棕色

## Abstract

newtab-wallpaper-category-title-abstract = 抽象
newtab-wallpaper-abstract-green = 绿色形状
newtab-wallpaper-abstract-blue = 蓝色形状
newtab-wallpaper-abstract-purple = 紫色形状
newtab-wallpaper-abstract-orange = 橙色形状
newtab-wallpaper-gradient-orange = 橙粉渐变
newtab-wallpaper-abstract-blue-purple = 蓝紫渐变
newtab-wallpaper-abstract-white-curves = 白色带阴影曲线
newtab-wallpaper-abstract-purple-green = 紫绿光渐变
newtab-wallpaper-abstract-blue-purple-waves = 蓝色和紫色的波浪形状
newtab-wallpaper-abstract-black-waves = 黑色波浪形状

## Celestial

newtab-wallpaper-category-title-photographs = 摄影
newtab-wallpaper-beach-at-sunrise = 海滩日出
newtab-wallpaper-beach-at-sunset = 海滩日落
newtab-wallpaper-storm-sky = 电闪雷鸣
newtab-wallpaper-sky-with-pink-clouds = 飘着粉色云朵的天空
newtab-wallpaper-red-panda-yawns-in-a-tree = 在树上打哈欠的小熊猫
newtab-wallpaper-white-mountains = 皑白山脉
newtab-wallpaper-hot-air-balloons = 白天各种颜色的热气球
newtab-wallpaper-starry-canyon = 蓝色星空
newtab-wallpaper-suspension-bridge = 白天时的灰色全悬索桥照片
newtab-wallpaper-sand-dunes = 白色沙丘
newtab-wallpaper-palm-trees = 魔术光下的椰子树侧影
newtab-wallpaper-blue-flowers = 蓝瓣花绽放的近景照片
newtab-wallpaper-forest-trees = 浓雾掩映的树木
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = 照片由 <a data-l10n-name="name-link">{ $author_string }</a> 发布于 <a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-header = 试用新色彩
newtab-wallpaper-feature-highlight-content = 选张壁纸，给新标签页加点新鲜感。
newtab-wallpaper-feature-highlight-button = 知道了
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = 知道了
    .aria-label = 关闭弹窗
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial

# “Celestial” referring to astronomy; positioned in or relating to the sky,
# or outer space as observed in astronomy.
# Not to be confused with religious definition of the word.
newtab-wallpaper-category-title-celestial = 天体
newtab-wallpaper-celestial-lunar-eclipse = 月食
newtab-wallpaper-celestial-earth-night = 从近地轨道拍摄的夜晚照片
newtab-wallpaper-celestial-starry-sky = 星空
newtab-wallpaper-celestial-eclipse-time-lapse = 月食延时照片
newtab-wallpaper-celestial-black-hole = 黑洞星空图
newtab-wallpaper-celestial-river = 河流卫星图

## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = 在“{ $provider }”上查看天气预报
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ 赞助
newtab-weather-menu-change-location = 更改位置
newtab-weather-change-location-search-input-placeholder =
    .placeholder = 搜索位置
    .aria-label = 搜索位置
newtab-weather-change-location-search-input = 搜索位置
newtab-weather-menu-weather-display = 天气信息显示方式
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = 简明
newtab-weather-menu-change-weather-display-simple = 切换到简明视图
newtab-weather-menu-weather-display-option-detailed = 详细
newtab-weather-menu-change-weather-display-detailed = 切换到详细视图
newtab-weather-menu-temperature-units = 温度单位
newtab-weather-menu-temperature-option-fahrenheit = 华氏度
newtab-weather-menu-temperature-option-celsius = 摄氏度
newtab-weather-menu-change-temperature-units-fahrenheit = 切换为华氏度
newtab-weather-menu-change-temperature-units-celsius = 切换为摄氏度
newtab-weather-menu-hide-weather = 隐藏新标签页上的天气信息
newtab-weather-menu-learn-more = 详细了解
# This message is shown if user is working offline
newtab-weather-error-not-available = 目前无法获取天气数据。

## Topic Labels

newtab-topic-label-business = 商业
newtab-topic-label-career = 职场
newtab-topic-label-education = 教育
newtab-topic-label-arts = 娱乐
newtab-topic-label-food = 饮食
newtab-topic-label-health = 健康
newtab-topic-label-hobbies = 游戏
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = 理财
newtab-topic-label-society-parenting = 育儿
newtab-topic-label-government = 政治
newtab-topic-label-education-science = 科学
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = 自我提升
newtab-topic-label-sports = 体育
newtab-topic-label-tech = 科技
newtab-topic-label-travel = 旅行
newtab-topic-label-home = 家庭与园艺

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = 选择主题，让推送内容更合您胃口
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = 请选择两个或更多主题。我们的专业采编团队会按照您的喜好，优先呈上专属推荐，您还可以随时刷新。
newtab-topic-selection-save-button = 保存
newtab-topic-selection-cancel-button = 取消
newtab-topic-selection-button-maybe-later = 以后再说
newtab-topic-selection-privacy-link = 了解我们保护和管理数据的方式
newtab-topic-selection-button-update-interests = 更新您感兴趣的主题
newtab-topic-selection-button-pick-interests = 选择您感兴趣的主题

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = 关注
newtab-section-following-button = 正在关注
newtab-section-unfollow-button = 取消关注

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = 屏蔽
newtab-section-blocked-button = 已屏蔽
newtab-section-unblock-button = 取消屏蔽

## Confirmation modal for blocking a section

newtab-section-cancel-button = 暂时不要
newtab-section-confirm-block-topic-p1 = 确定要屏蔽此主题吗？
newtab-section-confirm-block-topic-p2 = 将不再向您推送被屏蔽的主题。
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = 屏蔽“{ $topic }”

## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-mangage-topics-title = 主题
newtab-section-manage-topics-button-v2 =
    .label = 管理主题
newtab-section-mangage-topics-followed-topics = 已关注
newtab-section-mangage-topics-followed-topics-empty-state = 没有已关注的主题。
newtab-section-mangage-topics-blocked-topics = 已屏蔽
newtab-section-mangage-topics-blocked-topics-empty-state = 没有已屏蔽的主题
newtab-custom-wallpaper-title = 在此处自定义壁纸
newtab-custom-wallpaper-cta = 试试看
