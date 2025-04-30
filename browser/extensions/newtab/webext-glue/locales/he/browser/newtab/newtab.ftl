# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = לשונית חדשה
newtab-settings-button =
    .title = התאמה אישית של דף הלשונית החדשה שלך
newtab-personalize-settings-icon-label =
    .title = התאמה אישית של דף הלשונית החדשה
    .aria-label = הגדרות
newtab-settings-dialog-label =
    .aria-label = הגדרות
newtab-personalize-icon-label =
    .title = התאמה אישית של דף הלשונית החדשה
    .aria-label = התאמה אישית של דף הלשונית החדשה
newtab-personalize-dialog-label =
    .aria-label = התאמה אישית
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = חיפוש
    .aria-label = חיפוש
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = ‏ניתן לחפש עם { $engine } או להקליד כתובת
newtab-search-box-handoff-text-no-engine = חיפוש או הכנסת כתובת
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = ‏ניתן לחפש עם { $engine } או להקליד כתובת
    .title = ‏ניתן לחפש עם { $engine } או להקליד כתובת
    .aria-label = ‏ניתן לחפש עם { $engine } או להקליד כתובת
newtab-search-box-handoff-input-no-engine =
    .placeholder = חיפוש או הכנסת כתובת
    .title = חיפוש או הכנסת כתובת
    .aria-label = חיפוש או הכנסת כתובת
newtab-search-box-text = חיפוש ברשת
newtab-search-box-input =
    .placeholder = חיפוש ברשת
    .aria-label = חיפוש ברשת

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = הוספת מנוע חיפוש
newtab-topsites-add-shortcut-header = קיצור דרך חדש
newtab-topsites-edit-topsites-header = עריכת אתר מוביל
newtab-topsites-edit-shortcut-header = עריכת קיצור דרך
newtab-topsites-add-shortcut-label = הוספת קיצור דרך
newtab-topsites-title-label = כותרת
newtab-topsites-title-input =
    .placeholder = נא להזין כותרת
newtab-topsites-url-label = כתובת
newtab-topsites-url-input =
    .placeholder = נא להקליד או להזין כתובת
newtab-topsites-url-validation = נדרשת כתובת תקינה
newtab-topsites-image-url-label = כתובת תמונה מותאמת אישית
newtab-topsites-use-image-link = שימוש בתמונה מותאמת אישית…
newtab-topsites-image-validation = טעינת התמונה נכשלה. נא לנסות כתובת שונה.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = ביטול
newtab-topsites-delete-history-button = מחיקה מההיסטוריה
newtab-topsites-save-button = שמירה
newtab-topsites-preview-button = תצוגה מקדימה
newtab-topsites-add-button = הוספה

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = למחוק כל עותק של העמוד הזה מההיסטוריה שלך?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = לא ניתן לבטל פעולה זו.

## Top Sites - Sponsored label

newtab-topsite-sponsored = ממומן

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = פתיחת תפריט
    .aria-label = פתיחת תפריט
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = הסרה
    .aria-label = הסרה
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = פתיחת תפריט
    .aria-label = פתיחת תפריט ההקשר עבור { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = עריכת אתר זה
    .aria-label = עריכת אתר זה

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = עריכה
newtab-menu-open-new-window = פתיחה בחלון חדש
newtab-menu-open-new-private-window = פתיחה בחלון פרטי חדש
newtab-menu-dismiss = הסרה
newtab-menu-pin = נעיצה
newtab-menu-unpin = ביטול נעיצה
newtab-menu-delete-history = מחיקה מההיסטוריה
newtab-menu-save-to-pocket = שמירה אל { -pocket-brand-name }
newtab-menu-delete-pocket = מחיקה מ־{ -pocket-brand-name }
newtab-menu-archive-pocket = העברה לארכיון ב־{ -pocket-brand-name }
newtab-menu-about-fakespot = על אודות { -fakespot-brand-name }
newtab-menu-report-content = דיווח על תוכן זה
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = חסימה
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = ביטול המעקב אחרי הנושא

## Context menu options for sponsored stories and new ad formats on New Tab.

newtab-menu-manage-sponsored-content = ניהול תוכן ממומן
newtab-menu-our-sponsors-and-your-privacy = נותני החסות שלנו והפרטיות שלך
newtab-menu-report-this-ad = דיווח על פרסומת זו

## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = סיום
newtab-privacy-modal-button-manage = ניהול הגדרות תוכן ממומן
newtab-privacy-modal-header = הפרטיות שלך חשובה.
newtab-privacy-modal-link = הסבר על האופן בו עובדת הפרטיות שלך בלשונית החדשה

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = הסרת סימנייה
# Bookmark is a verb here.
newtab-menu-bookmark = הוספת סימנייה

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = העתקת קישור ההורדה
newtab-menu-go-to-download-page = מעבר לעמוד ההורדה
newtab-menu-remove-download = הסרה מההיסטורייה

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] הצגה ב־Finder
       *[other] פתיחת תיקייה מכילה
    }
newtab-menu-open-file = פתיחת הקובץ

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = ביקורים קודמים
newtab-label-bookmarked = שמור כסימנייה
newtab-label-removed-bookmark = הסימנייה הוסרה
newtab-label-recommended = פופולרי
newtab-label-saved = נשמר ל־{ -pocket-brand-name }
newtab-label-download = התקבל
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · ממומן
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = בחסות { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time =
    { $timeToRead ->
        [1] ‏{ $source }  · דקה אחת
       *[other] ‏{ $source } · { $timeToRead } דקות
    }

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = הסרת מדור
newtab-section-menu-collapse-section = צמצום מדור
newtab-section-menu-expand-section = הרחבת מדור
newtab-section-menu-manage-section = ניהול מדור
newtab-section-menu-manage-webext = ניהול הרחבה
newtab-section-menu-add-topsite = הוספת אתר מוביל
newtab-section-menu-add-search-engine = הוספת מנוע חיפוש
newtab-section-menu-move-up = העברה למעלה
newtab-section-menu-move-down = העברה למטה
newtab-section-menu-privacy-notice = הצהרת פרטיות

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = צמצום מדור
newtab-section-expand-section-label =
    .aria-label = הרחבת מדור

## Section Headers.

newtab-section-header-topsites = אתרים מובילים
newtab-section-header-recent-activity = פעילות אחרונה
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = מומלץ על־ידי { $provider }
newtab-section-header-stories = סיפורים מעוררי מחשבה
# "picks" refers to recommended articles
newtab-section-header-todays-picks = המאמרים של היום בשבילך

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = ניתן להתחיל בגלישה ואנו נציג בפניך מספר כתבות, סרטונים ועמודים שונים מעולים בהם ביקרת לאחרונה או שהוספת לסימניות.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = התעדכנת בכל הסיפורים. כדאי לנסות שוב מאוחר יותר כדי לקבל עוד סיפורים מובילים מאת { $provider }. לא רוצה לחכות? ניתן לבחור נושא נפוץ כדי למצוא עוד סיפורים נפלאים מרחבי הרשת.
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = התעדכנת בכל הסיפורים. כדאי לנסות שוב מאוחר יותר כדי לקבל עוד סיפורים. לא רוצה לחכות? ניתן לבחור נושא נפוץ כדי למצוא עוד סיפורים נפלאים מרחבי הרשת.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-try-again-button = ניסיון חוזר
newtab-discovery-empty-section-topstories-loading = בטעינה…

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = נושאים פופולריים:
newtab-pocket-new-topics-title = רוצה אפילו עוד סיפורים? ניתן לעיין בנושאים הנפוצים האלו מ־{ -pocket-brand-name }
newtab-pocket-more-recommendations = המלצות נוספות
newtab-pocket-learn-more = מידע נוסף
newtab-pocket-cta-button = קבלת { -pocket-brand-name }
newtab-pocket-cta-text = שמירת הסיפורים שאהבת ב־{ -pocket-brand-name } על מנת למלא את מחשבתך בקריאה מרתקת.
newtab-pocket-pocket-firefox-family = ‏{ -pocket-brand-name } הוא חלק ממשפחת { -brand-product-name }
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = שמירה
newtab-pocket-saved = נשמר

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = ארצה עוד כאלה
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = לא בשבילי
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = תודה. המשוב שלך יעזור לנו לשפר את הפיד שלך.
newtab-toast-dismiss-button =
    .title = סגירה
    .aria-label = סגירה

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = לגלות את המיטב של האינטרנט
newtab-pocket-onboarding-cta = ‏{ -pocket-brand-name } חוקר מגוון רחב של פרסומים כדי להביא את התוכן האינפורמטיבי, מעורר ההשראה והאמין ביותר ישירות לדפדפן ה־{ -brand-product-name } שלך.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = אופס, משהו השתבש בעת טעינת התוכן הזה.
newtab-error-fallback-refresh-link = נא לרענן את הדף כדי לנסות שוב.

## Customization Menu

newtab-custom-shortcuts-title = קיצורי דרך
newtab-custom-shortcuts-subtitle = אתרים ששמרת או ביקרת בהם
newtab-custom-shortcuts-toggle =
    .label = קיצורי דרך
    .description = אתרים ששמרת או ביקרת בהם
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] שורה אחת
       *[other] { $num } שורות
    }
newtab-custom-sponsored-sites = קיצורי דרך ממומנים
newtab-custom-pocket-title = מומלץ על־ידי { -pocket-brand-name }
newtab-custom-pocket-subtitle = תוכן יוצא דופן שנבחר בקפידה על־ידי { -pocket-brand-name }, חלק ממשפחת { -brand-product-name }
newtab-custom-stories-toggle =
    .label = סיפורים מומלצים
    .description = תוכן יוצא דופן שנבחר בקפידה על־ידי משפחת { -brand-product-name }
newtab-custom-pocket-sponsored = סיפורים ממומנים
newtab-custom-pocket-show-recent-saves = הצגת שמירות אחרונות
newtab-custom-recent-title = פעילות אחרונה
newtab-custom-recent-subtitle = מבחר של אתרים ותכנים אחרונים
newtab-custom-recent-toggle =
    .label = פעילות אחרונה
    .description = מבחר של אתרים ותכנים אחרונים
newtab-custom-weather-toggle =
    .label = מזג אוויר
    .description = התחזית של היום
newtab-custom-close-button = סגירה
newtab-custom-settings = ניהול הגדרות נוספות

## New Tab Wallpapers

newtab-wallpaper-title = תמונות רקע
newtab-wallpaper-reset = איפוס לברירת מחדל
newtab-wallpaper-upload-image = העלאת תמונה
newtab-wallpaper-custom-color = בחירת צבע
# Variables
#   $file_size (number) - The number of the maximum image file size (in MB) that may be uploaded
newtab-wallpaper-error-max-file-size = התמונה חרגה ממגבלת גודל הקובץ של { $file_size } מ״ב. נא לנסות להעלות קובץ קטן יותר.
newtab-wallpaper-error-file-type = לא הצלחנו להעלות את הקובץ שלך. נא לנסות שוב עם סוג קובץ אחר.
newtab-wallpaper-light-red-panda = פנדה אדומה
newtab-wallpaper-light-mountain = הר לבן
newtab-wallpaper-light-sky = שמיים עם עננים סגולים וורודים
newtab-wallpaper-light-color = צורות כחולות, ורודות וצהובות
newtab-wallpaper-light-landscape = נוף הררי עם ערפל כחול
newtab-wallpaper-light-beach = חוף עם עץ דקל
newtab-wallpaper-dark-aurora = זוהר צפוני
newtab-wallpaper-dark-color = צורות אדומות וכחולות
newtab-wallpaper-dark-panda = פנדה אדומה חבויה ביער
newtab-wallpaper-dark-sky = נוף עיר עם שמי לילה
newtab-wallpaper-dark-mountain = נוף הררי
newtab-wallpaper-dark-city = נוף עירוני סגול
newtab-wallpaper-dark-fox-anniversary = שועל על המדרכה ליד יער
newtab-wallpaper-light-fox-anniversary = שועל בשדה עשב עם נוף הררי ערפילי

## Solid Colors

newtab-wallpaper-category-title-colors = צבעים אחידים
newtab-wallpaper-blue = כחול
newtab-wallpaper-light-blue = כחול בהיר
newtab-wallpaper-light-purple = סגול בהיר
newtab-wallpaper-light-green = ירוק בהיר
newtab-wallpaper-green = ירוק
newtab-wallpaper-beige = בז’
newtab-wallpaper-yellow = צהוב
newtab-wallpaper-orange = כתום
newtab-wallpaper-pink = ורוד
newtab-wallpaper-light-pink = ורוד בהיר
newtab-wallpaper-red = אדום
newtab-wallpaper-dark-blue = כחול כהה
newtab-wallpaper-dark-purple = סגול כהה
newtab-wallpaper-dark-green = ירוק כהה
newtab-wallpaper-brown = חום

## Abstract

newtab-wallpaper-category-title-abstract = מופשט
newtab-wallpaper-abstract-green = צורות ירוקות
newtab-wallpaper-abstract-blue = צורות כחולות
newtab-wallpaper-abstract-purple = צורות סגולות
newtab-wallpaper-abstract-orange = צורות כתומות
newtab-wallpaper-gradient-orange = מעברי צבע כתום וורוד
newtab-wallpaper-abstract-blue-purple = צורות כחולות וסגולות
newtab-wallpaper-abstract-white-curves = לבן עם קימורים מוצללים
newtab-wallpaper-abstract-purple-green = מעברי צבע סגול וירוק
newtab-wallpaper-abstract-blue-purple-waves = צורות גליות בצבע כחול וסגול
newtab-wallpaper-abstract-black-waves = צורות גליות בצבע שחור

## Celestial

newtab-wallpaper-category-title-photographs = תצלומים
newtab-wallpaper-beach-at-sunrise = זריחה בחוף הים
newtab-wallpaper-beach-at-sunset = שקיעה בחוף הים
newtab-wallpaper-storm-sky = שמיים סוערים
newtab-wallpaper-sky-with-pink-clouds = שמיים עם עננים ורודים
newtab-wallpaper-red-panda-yawns-in-a-tree = פנדה אדומה מפהקת בעץ
newtab-wallpaper-white-mountains = הרים לבנים
newtab-wallpaper-hot-air-balloons = מגוון צבעים של בלוני אוויר חם במהלך שעות היום
newtab-wallpaper-starry-canyon = ליל כוכבים כחול
newtab-wallpaper-suspension-bridge = תצלום של גשר תלוי אפור במהלך שעות היום
newtab-wallpaper-sand-dunes = דיונות חול לבן
newtab-wallpaper-palm-trees = צללית של עצי דקל קוקוס במהלך שעת הזהב
newtab-wallpaper-blue-flowers = צילום תקריב של פרחים כחולי כותרת בפריחה
newtab-wallpaper-forest-trees = עצים מכוסים בערפל
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = תמונה מאת <a data-l10n-name="name-link">{ $author_string }</a> ב־<a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-header = אולי איזה מגע של צבע
newtab-wallpaper-feature-highlight-content = תנו ללשונית החדשה שלכם מראה רענן עם תמונות רקע.
newtab-wallpaper-feature-highlight-button = הבנתי
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = סגירה
    .aria-label = סגירת ההודעה הקופצת
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial

# “Celestial” referring to astronomy; positioned in or relating to the sky,
# or outer space as observed in astronomy.
# Not to be confused with religious definition of the word.
newtab-wallpaper-category-title-celestial = שמימי
newtab-wallpaper-celestial-lunar-eclipse = ליקוי ירח
newtab-wallpaper-celestial-earth-night = צילום לילה ממסלול לווייני נמוך של כדור הארץ
newtab-wallpaper-celestial-starry-sky = שמי כוכבים
newtab-wallpaper-celestial-eclipse-time-lapse = ליקוי ירח בהילוך מהיר
newtab-wallpaper-celestial-black-hole = איור של גלקסיית חור שחור
newtab-wallpaper-celestial-river = תמונת לוויין של נהר

## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = הצגת התחזית ב־{ $provider }
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = ‏{ $provider } ∙ ממומן
newtab-weather-menu-change-location = שינוי מקום
newtab-weather-change-location-search-input-placeholder =
    .placeholder = חיפוש מקום
    .aria-label = חיפוש מקום
newtab-weather-change-location-search-input = חיפוש מקום
newtab-weather-menu-weather-display = תצוגת מזג אוויר
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = פשוטה
newtab-weather-menu-change-weather-display-simple = מעבר לתצוגה פשוטה
newtab-weather-menu-weather-display-option-detailed = מפורטת
newtab-weather-menu-change-weather-display-detailed = מעבר לתצוגה מפורטת
newtab-weather-menu-temperature-units = יחידות טמפרטורה
newtab-weather-menu-temperature-option-fahrenheit = פרנהייט
newtab-weather-menu-temperature-option-celsius = צלזיוס
newtab-weather-menu-change-temperature-units-fahrenheit = מעבר לפרנהייט
newtab-weather-menu-change-temperature-units-celsius = מעבר לצלזיוס
newtab-weather-menu-hide-weather = הסתרת מזג האוויר בלשונית החדשה
newtab-weather-menu-learn-more = מידע נוסף
# This message is shown if user is working offline
newtab-weather-error-not-available = נתוני מזג האוויר אינם זמינים כעת.

## Topic Labels

newtab-topic-label-business = עסקים
newtab-topic-label-career = קריירה
newtab-topic-label-education = חינוך
newtab-topic-label-arts = בידור
newtab-topic-label-food = אוכל
newtab-topic-label-health = בְּרִיאוּת
newtab-topic-label-hobbies = משחקים
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = כסף
newtab-topic-label-society-parenting = הורות
newtab-topic-label-government = פוליטיקה
newtab-topic-label-education-science = מדע
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = טיפים לחיים
newtab-topic-label-sports = ספורט
newtab-topic-label-tech = טכנולוגיה
newtab-topic-label-travel = טיולים
newtab-topic-label-home = בית וגן

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = נא לבחור בנושאים כדי לכוונן את הפיד שלך
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = נא לבחור בשני נושאים או יותר. המומחים שלנו נותנים עדיפות לסיפורים המותאמים לתחומי העניין שלך. ניתן לעדכן אותם בכל עת.
newtab-topic-selection-save-button = שמירה
newtab-topic-selection-cancel-button = ביטול
newtab-topic-selection-button-maybe-later = אולי אחר כך
newtab-topic-selection-privacy-link = כיצד אנו מגנים על נתונים ומנהלים אותם
newtab-topic-selection-button-update-interests = עדכון תחומי העניין שלך
newtab-topic-selection-button-pick-interests = בחירת תחומי העניין שלך

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = לעקוב
newtab-section-following-button = במעקב
newtab-section-unfollow-button = ביטול המעקב

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = חסימה
newtab-section-blocked-button = חסום
newtab-section-unblock-button = הסרת חסימה

## Confirmation modal for blocking a section

newtab-section-cancel-button = לא כעת
newtab-section-confirm-block-topic-p1 = האם ברצונך לחסום נושא זה?
newtab-section-confirm-block-topic-p2 = נושאים חסומים לא יופיעו יותר בפיד שלך.
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = חסימת { $topic }

## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-mangage-topics-title = נושאים
newtab-section-manage-topics-button-v2 =
    .label = ניהול נושאים
newtab-section-mangage-topics-followed-topics = במעקב
newtab-section-mangage-topics-followed-topics-empty-state = עדיין לא עקבת אחר אף נושא.
newtab-section-mangage-topics-blocked-topics = חסום
newtab-section-mangage-topics-blocked-topics-empty-state = עדיין לא חסמת אף נושא.
newtab-custom-wallpaper-title = טפטים מותאמים אישית נמצאים כאן
# 'Make firefox yours" means to customize or personalize
newtab-custom-wallpaper-subtitle = ניתן להעלות טפט משלך או לבחור בצבע מותאם אישית כדי להפוך את { -brand-product-name } לשלך.
newtab-custom-wallpaper-cta = בואו ננסה
