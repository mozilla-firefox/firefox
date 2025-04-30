# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = 新規タブ
newtab-settings-button =
    .title = 新規タブページをカスタマイズ
newtab-personalize-settings-icon-label =
    .title = 新規タブをパーソナライズ
    .aria-label = 設定
newtab-settings-dialog-label =
    .aria-label = 設定
newtab-personalize-icon-label =
    .title = 新規タブをパーソナライズ
    .aria-label = 新規タブをパーソナライズ
newtab-personalize-dialog-label =
    .aria-label = パーソナライズ
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = 検索
    .aria-label = 検索
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = { $engine } で検索、または URL を入力します
newtab-search-box-handoff-text-no-engine = 検索語句、または URL を入力します
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = { $engine } で検索、または URL を入力します
    .title = { $engine } で検索、または URL を入力します
    .aria-label = { $engine } で検索、または URL を入力します
newtab-search-box-handoff-input-no-engine =
    .placeholder = 検索語句、または URL を入力します
    .title = 検索語句、または URL を入力します
    .aria-label = 検索語句、または URL を入力します
newtab-search-box-text = ウェブを検索
newtab-search-box-input =
    .placeholder = ウェブを検索
    .aria-label = ウェブを検索

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = 検索エンジンを追加
newtab-topsites-add-shortcut-header = 新規ショートカット
newtab-topsites-edit-topsites-header = トップサイトを編集
newtab-topsites-edit-shortcut-header = ショートカットを編集
newtab-topsites-add-shortcut-label = ショートカット追加
newtab-topsites-title-label = タイトル
newtab-topsites-title-input =
    .placeholder = タイトルを入力
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = URL を入力するかペースト
newtab-topsites-url-validation = 正しい URL を入力してください
newtab-topsites-image-url-label = カスタム画像 URL
newtab-topsites-use-image-link = カスタム画像を使用...
newtab-topsites-image-validation = 画像を読み込めませんでした。別の URL を試してください。

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = キャンセル
newtab-topsites-delete-history-button = 履歴から削除
newtab-topsites-save-button = 保存
newtab-topsites-preview-button = プレビュー
newtab-topsites-add-button = 追加

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = 本当にこのページに関して保存されているあらゆる情報を履歴から削除しますか？
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = この操作は取り消せません。

## Top Sites - Sponsored label

newtab-topsite-sponsored = 広告

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = メニューを開きます
    .aria-label = メニューを開きます
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = 削除
    .aria-label = 削除
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = メニューを開きます
    .aria-label = { $title } のコンテキストメニューを開く
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = このサイトを編集
    .aria-label = このサイトを編集

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = 編集
newtab-menu-open-new-window = 新規ウインドウで開く
newtab-menu-open-new-private-window = 新規プライベートウインドウで開く
newtab-menu-dismiss = 閉じる
newtab-menu-pin = ピン留め
newtab-menu-unpin = ピン留めを外す
newtab-menu-delete-history = 履歴から削除
newtab-menu-save-to-pocket = { -pocket-brand-name } に保存
newtab-menu-delete-pocket = { -pocket-brand-name } から削除
newtab-menu-archive-pocket = { -pocket-brand-name } にアーカイブ
newtab-menu-show-privacy-info = 私たちのスポンサーとあなたのプライバシー
newtab-menu-about-fakespot = { -fakespot-brand-name } について
newtab-menu-report-content = このコンテンツを報告
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = ブロック
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = トピックのフォローを解除

## Context menu options for sponsored stories and new ad formats on New Tab.

newtab-menu-manage-sponsored-content = 広告コンテンツを管理
newtab-menu-our-sponsors-and-your-privacy = 私たちのスポンサーとユーザーのプライバシー
newtab-menu-report-this-ad = この広告を報告

## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = 完了
newtab-privacy-modal-button-manage = 広告コンテンツの設定を管理
newtab-privacy-modal-header = プライバシーは重要です。
newtab-privacy-modal-paragraph-2 =
    盛り上がる魅力あるストーリーに加えて、選ばれたスポンサーからあなたの興味を引きそうな厳選コンテンツを提供します。
    <strong>閲覧データに { -brand-product-name } の個人情報のコピーが残ることはありません。</strong>私たちとスポンサーのどちらもその情報を見ることはありませんので、ご安心ください。
newtab-privacy-modal-link = 新規タブページでのプライバシーの仕組みついて

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = ブックマークを削除
# Bookmark is a verb here.
newtab-menu-bookmark = ブックマーク

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = ダウンロード元の URL をコピー
newtab-menu-go-to-download-page = ダウンロード元のページを開く
newtab-menu-remove-download = 履歴から削除

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Finder に表示
       *[other] 保存フォルダーを開く
    }
newtab-menu-open-file = ファイルを開く

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = 訪問済み
newtab-label-bookmarked = ブックマーク済み
newtab-label-removed-bookmark = 削除済みブックマーク
newtab-label-recommended = 話題の記事
newtab-label-saved = { -pocket-brand-name } に保存しました
newtab-label-download = ダウンロード済み
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = 提供: { $sponsorOrSource }
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = 提供: { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } 分

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = セクションを削除
newtab-section-menu-collapse-section = セクションを折りたたむ
newtab-section-menu-expand-section = セクションを広げる
newtab-section-menu-manage-section = セクションを管理
newtab-section-menu-manage-webext = 拡張機能を管理
newtab-section-menu-add-topsite = トップサイトを追加
newtab-section-menu-add-search-engine = 検索エンジンを追加
newtab-section-menu-move-up = 上へ移動
newtab-section-menu-move-down = 下へ移動
newtab-section-menu-privacy-notice = プライバシー通知

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = セクションを折りたたむ
newtab-section-expand-section-label =
    .aria-label = セクションを広げる

## Section Headers.

newtab-section-header-topsites = トップサイト
newtab-section-header-recent-activity = 最近のアクティビティ
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = { $provider } のおすすめ
newtab-section-header-stories = 示唆に富むストーリー
# "picks" refers to recommended articles
newtab-section-header-todays-picks = 本日のおすすめ

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = ブラウジング中にあなたが最近訪れたりブックマークしたりした、優れた記事、動画、その他ページの一部をここに表示します。
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = すべて既読です。また後で { $provider } からのおすすめ記事をチェックしてください。待ちきれない場合は、人気のトピックを選択してウェブ上の他の優れた記事を見つけてください。
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = すべて既読です。また後でおすすめ記事をチェックしてください。待ちきれない場合は、人気のトピックを選択してウェブ上の他の優れた記事を見つけてください。

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = すべて既読です！
newtab-discovery-empty-section-topstories-content = また後で戻っておすすめ記事をチェックしてください。
newtab-discovery-empty-section-topstories-try-again-button = 再試行
newtab-discovery-empty-section-topstories-loading = 読み込み中...
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = おおっと、このセクションはほぼ読み込みましたが、完全ではありません。

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = 人気のトピック:
newtab-pocket-new-topics-title = 他の記事も読みたいですか？ { -pocket-brand-name } からの人気記事も見てみましょう
newtab-pocket-more-recommendations = 他のおすすめ
newtab-pocket-learn-more = 詳細
newtab-pocket-cta-button = { -pocket-brand-name } を入手
newtab-pocket-cta-text = お気に入りに記事を { -pocket-brand-name } に保存して、魅力的な読み物を思う存分楽しみましょう。
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } は { -brand-product-name } ファミリーの一員です
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = 保存
newtab-pocket-saved = 保存しました

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = お気に入り
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = 興味なし
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = ありがとうございます。あなたのフィードバックがフィードを改善する助けになります。
newtab-toast-dismiss-button =
    .title = 閉じる
    .aria-label = 閉じる

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = ウェブのベストコンテンツを見つけましょう
newtab-pocket-onboarding-cta = { -pocket-brand-name } は、さまざまな出版物の中から最も有益で、感動的な、信頼できるコンテンツをあなたの { -brand-product-name } ブラウザーにもたらします。

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = このコンテンツの読み込み中に何か問題が発生しました。
newtab-error-fallback-refresh-link = ページを再読み込みしてもう一度試してください。

## Customization Menu

newtab-custom-shortcuts-title = ショートカット
newtab-custom-shortcuts-subtitle = 保存または訪問したサイト
newtab-custom-shortcuts-toggle =
    .label = ショートカット
    .description = 保存または訪問したサイト
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector = { $num } 行
newtab-custom-sponsored-sites = 広告ショートカット
newtab-custom-pocket-title = { -pocket-brand-name } のおすすめ
newtab-custom-pocket-subtitle = { -brand-product-name } ファミリーを構成する { -pocket-brand-name } が厳選した注目のコンテンツ
newtab-custom-stories-toggle =
    .label = おすすめのストーリー
    .description = { -brand-product-name } ファミリーに選ばれた優良コンテンツです
newtab-custom-pocket-sponsored = 広告記事
newtab-custom-pocket-show-recent-saves = 最近保存したものを表示
newtab-custom-recent-title = 最近のアクティビティ
newtab-custom-recent-subtitle = 最近のサイトとコンテンツの抜粋
newtab-custom-recent-toggle =
    .label = 最近のアクティビティ
    .description = 最近のサイトとコンテンツの抜粋
newtab-custom-weather-toggle =
    .label = 天気予報
    .description = 一目でわかる今日の天気
newtab-custom-close-button = 閉じる
newtab-custom-settings = 他の設定を管理

## New Tab Wallpapers

newtab-wallpaper-title = 壁紙
newtab-wallpaper-reset = デフォルトにリセット
newtab-wallpaper-upload-image = 画像をアップロード
newtab-wallpaper-custom-color = カラーを選択
# Variables
#   $file_size (number) - The number of the maximum image file size (in MB) that may be uploaded
newtab-wallpaper-error-max-file-size = 画像がファイルサイズの上限を超えています。{ $file_size } MB より小さなファイルをアップロードしてください。
newtab-wallpaper-error-file-type = ファイルをアップロードできませんでした。別のファイル形式で再度試してください。
newtab-wallpaper-light-red-panda = レッサーパンダ
newtab-wallpaper-light-mountain = 白い雪山
newtab-wallpaper-light-sky = 紫色の雲と空
newtab-wallpaper-light-color = 黄色、ピンク色、青色の模様
newtab-wallpaper-light-landscape = 空色の雲海と山の景色
newtab-wallpaper-light-beach = ヤシの木のある砂浜
newtab-wallpaper-dark-aurora = 北極のオーロラ
newtab-wallpaper-dark-color = 赤色と青色の模様
newtab-wallpaper-dark-panda = 森に隠れるレッサーパンダ
newtab-wallpaper-dark-sky = 夜空と街の景色
newtab-wallpaper-dark-mountain = 山の景色
newtab-wallpaper-dark-city = 紫色の街の景色
newtab-wallpaper-dark-fox-anniversary = 森林の道路に座るキツネ
newtab-wallpaper-light-fox-anniversary = 霧がかかった山を背景に草原にたたずむキツネ

## Solid Colors

newtab-wallpaper-category-title-colors = 無地
newtab-wallpaper-blue = 空色
newtab-wallpaper-light-blue = 白藍色
newtab-wallpaper-light-purple = 紅藤
newtab-wallpaper-light-green = 白緑
newtab-wallpaper-green = 若緑
newtab-wallpaper-beige = 肌色
newtab-wallpaper-yellow = 女郎花
newtab-wallpaper-orange = 柑子色
newtab-wallpaper-pink = 牡丹色
newtab-wallpaper-light-pink = 桜色
newtab-wallpaper-red = 茜色
newtab-wallpaper-dark-blue = 紺色
newtab-wallpaper-dark-purple = 小紫
newtab-wallpaper-dark-green = 深緑
newtab-wallpaper-brown = 栗色

## Abstract

newtab-wallpaper-category-title-abstract = 抽象的
newtab-wallpaper-abstract-green = 緑色の形状
newtab-wallpaper-abstract-blue = 青色の形状
newtab-wallpaper-abstract-purple = 紫色の形状
newtab-wallpaper-abstract-orange = オレンジ色の形状
newtab-wallpaper-gradient-orange = オレンジとピンクのグラデーション
newtab-wallpaper-abstract-blue-purple = 青色と紫色の形状
newtab-wallpaper-abstract-white-curves = 影のついた白色の曲線
newtab-wallpaper-abstract-purple-green = 紫色と緑色の明るいグラデーション
newtab-wallpaper-abstract-blue-purple-waves = 青色と紫色の波形の形状
newtab-wallpaper-abstract-black-waves = 黒色の波形の形状

## Photographs

newtab-wallpaper-category-title-photographs = 写真
newtab-wallpaper-beach-at-sunrise = 早朝の砂浜
newtab-wallpaper-beach-at-sunset = 夕暮れの砂浜
newtab-wallpaper-storm-sky = 嵐の空
newtab-wallpaper-sky-with-pink-clouds = ピンク色に染まる雲
newtab-wallpaper-red-panda-yawns-in-a-tree = あくびをするレッサーパンダ
newtab-wallpaper-white-mountains = 白い雪山
newtab-wallpaper-hot-air-balloons = 昼空に浮かぶさまざまな色の熱気球
newtab-wallpaper-starry-canyon = 青い星夜
newtab-wallpaper-suspension-bridge = 昼の灰色の吊橋
newtab-wallpaper-sand-dunes = 白砂の砂丘
newtab-wallpaper-palm-trees = 朝焼けに照らされたココヤシの木々のシルエット
newtab-wallpaper-blue-flowers = 咲き誇る青い花のクローズアップ写真
newtab-wallpaper-forest-trees = 霧に覆われた木々
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = 写真提供: <a data-l10n-name="name-link">{ $author_string }</a> (<a data-l10n-name="webpage-link">{ $webpage_string }</a>)
newtab-wallpaper-feature-highlight-header = カラフルな壁紙を試しましょう
newtab-wallpaper-feature-highlight-content = 壁紙で新規タブをカラフルに彩りましょう。
newtab-wallpaper-feature-highlight-button = 了解
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = 閉じる
    .aria-label = ポップアップを閉じます
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial

# “Celestial” referring to astronomy; positioned in or relating to the sky,
# or outer space as observed in astronomy.
# Not to be confused with religious definition of the word.
newtab-wallpaper-category-title-celestial = 宇宙
newtab-wallpaper-celestial-lunar-eclipse = 月食
newtab-wallpaper-celestial-earth-night = 地球低軌道からの夜景
newtab-wallpaper-celestial-starry-sky = 星空
newtab-wallpaper-celestial-eclipse-time-lapse = 月食のタイムラプス
newtab-wallpaper-celestial-black-hole = ブラックホール銀河のイラスト
newtab-wallpaper-celestial-river = 河川の衛星画像

## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = { $provider } による天気予報を表示します
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = 提供: { $provider }
newtab-weather-menu-change-location = 予報地点を変更
newtab-weather-change-location-search-input-placeholder =
    .placeholder = 場所を検索
    .aria-label = 場所を検索
newtab-weather-change-location-search-input = 場所を検索
newtab-weather-menu-weather-display = 天気表示
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = シンプル
newtab-weather-menu-change-weather-display-simple = シンプル表示に切り替えます
newtab-weather-menu-weather-display-option-detailed = 詳細
newtab-weather-menu-change-weather-display-detailed = 詳細表示に切り替えます
newtab-weather-menu-temperature-units = 温度の単位
newtab-weather-menu-temperature-option-fahrenheit = 華氏 (℉)
newtab-weather-menu-temperature-option-celsius = 摂氏 (℃)
newtab-weather-menu-change-temperature-units-fahrenheit = ファーレンハイト度に切り替えます
newtab-weather-menu-change-temperature-units-celsius = セルシウス度に切り替えます
newtab-weather-menu-hide-weather = 新規タブの天気表示を隠す
newtab-weather-menu-learn-more = 詳細情報
# This message is shown if user is working offline
newtab-weather-error-not-available = 現在、天気データが利用できません。

## Topic Labels

newtab-topic-label-business = 仕事
newtab-topic-label-career = 経歴
newtab-topic-label-education = 教育
newtab-topic-label-arts = 娯楽
newtab-topic-label-food = 食品
newtab-topic-label-health = 健康
newtab-topic-label-hobbies = ゲーム
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = 金融
newtab-topic-label-society-parenting = 育児
newtab-topic-label-government = 政治
newtab-topic-label-education-science = 科学
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = ライフハック
newtab-topic-label-sports = スポーツ
newtab-topic-label-tech = 技術
newtab-topic-label-travel = 旅行
newtab-topic-label-home = 家庭

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = あなたのフィードに最適なトピックを選択
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = 複数のトピックを選んでください。専門のキュレーターがあなたの関心事に合わせてストーリーに優先順位を付けます。いつでも更新できます。
newtab-topic-selection-save-button = 保存
newtab-topic-selection-cancel-button = キャンセル
newtab-topic-selection-button-maybe-later = 後で選ぶ
newtab-topic-selection-privacy-link = ユーザーデータの保護と管理について
newtab-topic-selection-button-update-interests = 関心事を更新
newtab-topic-selection-button-pick-interests = 関心事を選ぶ

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.


## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = フォローする
newtab-section-following-button = フォロー中
newtab-section-unfollow-button = フォロー解除

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = ブロックする
newtab-section-blocked-button = ブロック中
newtab-section-unblock-button = ブロック解除

## Confirmation modal for blocking a section

newtab-section-cancel-button = 後で
newtab-section-confirm-block-topic-p1 = 本当にこのトピックをブロックしますか？
newtab-section-confirm-block-topic-p2 = ブロックしたトピックはフィードに表示されません。
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = { $topic } をブロック

## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-mangage-topics-title = トピック
newtab-section-manage-topics-button-v2 =
    .label = トピックを管理
newtab-section-mangage-topics-followed-topics = フォロー中
newtab-section-mangage-topics-followed-topics-empty-state = フォローしているトピックはありません。
newtab-section-mangage-topics-blocked-topics = ブロック中
newtab-section-mangage-topics-blocked-topics-empty-state = ブロックしているトピックはありません。
newtab-custom-wallpaper-title = カスタム壁紙が利用できます
# 'Make firefox yours" means to customize or personalize
newtab-custom-wallpaper-subtitle = 壁紙をアップロードするかお好みのカラーを選んで、あなただけの { -brand-product-name } にカスタマイズしましょう。
newtab-custom-wallpaper-cta = 壁紙を試す
