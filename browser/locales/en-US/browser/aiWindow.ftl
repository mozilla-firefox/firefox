# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

## Chrome

main-context-menu-open-link-new-smart-window =
    .label = Open Link in New { -smart-window-brand-name }
    .accesskey = S

appmenuitem-new-ai-window =
    .label = New { -smart-window-brand-name }
    .value = New { -smart-window-brand-name }

appmenuitem-new-classic-window =
    .label = New Classic Window

menu-file-new-ai-window =
    .label = New { -smart-window-brand-name }

menu-file-new-classic-window =
    .label = New Classic Window

menu-history-chats =
    .label = Chats

menu-history-chats-recent =
    .label = Recent Chats

smartwindow-fullpage-heading = { -smart-window-brand-name }

smartwindow-document-title = New Tab

## Smart Window Toggle Button

toolbar-switcher-customizable-label =
    .label = { -smart-window-brand-name } switcher
    .tooltiptext = Switch between Smart and Classic windows.

ai-window-toggleview-switch-classic =
    .label = Classic Window
    .value = Classic Window

ai-window-toggleview-switch-classic-description =
    .label = Standard browsing
    .value = Standard browsing

ai-window-toggleview-switch-ai =
    .label = { -smart-window-brand-name }
    .value = { -smart-window-brand-name }

ai-window-toggleview-switch-ai-description =
    .label = Ask Funxplorer as you browse
    .value = Ask Funxplorer as you browse

ai-window-toggleview-switch-private =
    .label = Private Window

ai-window-toggleview-open-private =
    .label = Open New Private Window

ai-window-toggleview-status-label-active = { -smart-window-brand-name }

ai-window-toggleview-status-label-inactive = Classic Window

## Input CTA

aiwindow-input-cta-submit-label-chat = Ask
aiwindow-input-cta-submit-label-navigate = Go
aiwindow-input-cta-submit-label-search = Search
aiwindow-input-cta-submit-label-stop = Stop

# Text announced to screen readers when response generation starts.
aiwindow-generation-started-announcement = Response generation started

aiwindow-input-cta-menu-label-chat = Ask
aiwindow-input-cta-menu-label-navigate = Go to site
# $searchEngineName (string) - The name of the default search engine
aiwindow-input-cta-menu-label-search = Search with { $searchEngineName }
aiwindow-input-cta-menu-label-search-with = Search with…

aiwindow-input-cta-search-submenu-header = Search

aiwindow-input-cta-stop-button =
    .aria-label = Stop response generation
    .title = Stop response

## Smartbar

smartbar-placeholder =
    .placeholder = Ask, search, or type a URL
smartbar-placeholder-hint-1 = Use @ to mention recent tabs…
smartbar-placeholder-hint-2 = Ask anything…
smartbar-placeholder-hint-3 = Enter a web address…
smartbar-placeholder-hint-4 = Search the web…

## Mentions

smartbar-mention-typing-placeholder = Tag a tab or site
smartbar-mentions-list-no-results-label = No results found
smartbar-mentions-list-recent-tabs-label = Recent tabs

## Context mentions menu toggle button

smartbar-context-menu-button =
    .aria-label = Add a tab or site
    .tooltiptext = Add a tab or site

## Website Chip

aiwindow-website-chip-placeholder = Tag a tab or site
aiwindow-website-chip-history-deleted = History deleted
aiwindow-website-chip-remove-button =
    .aria-label = Remove

## Firstrun onboarding

aiwindow-firstrun-title = Welcome to { -smart-window-brand-name }
aiwindow-firstrun-model-title = What’s important to you?
aiwindow-firstrun-model-title-v2 = Pick a model to start
aiwindow-firstrun-model-subtitle = Pick a model to power { -smart-window-brand-name }. Switch anytime.
aiwindow-firstrun-model-subtitle-v2 = Each model can help you to summarize, compare, and explore across your tabs. Switch anytime.

# $model (string) - The name of the AI model
# $ownerName (string) - The name of the model owner/provider
# $shortName (string) - The short name of the model version
aiwindow-firstrun-model-chip-subtitle = Model { $model } by { $ownerName }
aiwindow-firstrun-model-personal-label = Personal
aiwindow-firstrun-model-personal-body = Most tailored answers
# $shortName (string) - The short name of the model version
aiwindow-firstrun-model-personal-label-v2 = Personal: { $shortName }
aiwindow-firstrun-model-personal-body-v2 = Built for personalized help across languages
aiwindow-firstrun-model-fast-label = Fast
aiwindow-firstrun-model-fast-body = Answers quickly
# $shortName (string) - The short name of the model version
aiwindow-firstrun-model-fast-label-v2 = Fast: { $shortName }
aiwindow-firstrun-model-fast-body-v2 = Best for quick answers when you’re moving fast
aiwindow-firstrun-model-allpurpose-label = Flexible
aiwindow-firstrun-model-allpurpose-body = Solid fit for most needs
# $shortName (string) - The short name of the model version
aiwindow-firstrun-model-flexible-label = Flexible: { $shortName }
aiwindow-firstrun-model-flexible-body = Ready for a variety of tasks

# Recommended represents the chat brand and model we recommend for users. Only affects European users.
aiwindow-firstrun-model-recommended = Recommended

aiwindow-firstrun-button = Let’s go
aiwindow-firstrun-back-button = Back
aiwindow-firstrun-next-button = Next

## These are labels describing model types in the smartbar model select.

aiwindow-input-model-select-button-label-fast = Fast
aiwindow-input-model-select-button-label-allpurpose = Flexible
aiwindow-input-model-select-button-label-personal = Personal
aiwindow-input-model-select-button-label-custom = Custom
aiwindow-input-model-select-button-description-custom = Use your own LLM

# Variables:
# $ownerName (string) - The name of the model owner/provider
# $model (string) - The model name
aiwindow-input-model-select-menu-item-description = { $ownerName } { $model }
aiwindow-input-model-select-menu-item-description-custom = Use your own LLM
aiwindow-input-model-select-default-badge =
    .label = Default
    .title = The selected default model
aiwindow-input-model-select-settings-link = Model settings

## Firstrun memories onboarding

aiwindow-firstrun-memories-title = More helpful answers, on your terms
aiwindow-firstrun-memories-subtitle = { -smart-window-brand-name } can learn from your chats, browsing, or both to create memories. They make answers more helpful over time.

aiwindow-firstrun-memories-conversation-title = Keep the conversation going
aiwindow-firstrun-memories-conversation-body = Learning from chats means you’ll have to repeat yourself less.

aiwindow-firstrun-memories-relevance-title = More relevant answers
aiwindow-firstrun-memories-relevance-body = Learning from browsing gives { -smart-window-brand-name } the bigger picture.

aiwindow-firstrun-memories-privacy-title = Private by design
aiwindow-firstrun-memories-privacy-body = Memories are stored on this device. Delete or turn off anytime.

aiwindow-firstrun-memories-choose-label = Choose what { -smart-window-brand-name } learns from
aiwindow-firstrun-memories-checkbox-chats = Chats in { -smart-window-brand-name }
aiwindow-firstrun-memories-checkbox-browsing = Browsing across { -brand-product-name }
aiwindow-firstrun-memories-update-settings = Update in settings anytime.
aiwindow-firstrun-memories-no-create = Got it. { -smart-window-brand-name } won’t create memories. Update in settings anytime.

## Firstrun set as default onboarding

aiwindow-firstrun-default-title = Make { -smart-window-brand-name } your go-to
aiwindow-firstrun-default-subtitle = Browse, search, and ask in one place. You can still open Private and Classic Windows when you want.
aiwindow-firstrun-default-checkbox-label = Always open { -brand-product-name } in { -smart-window-brand-name }
aiwindow-firstrun-default-checkbox-description = Switch in settings anytime

## Ask Toolbar Button

smartwindow-ask-button =
    .label = Ask

## Memories toggle button

aiwindow-memories-on =
    .tooltiptext = Use memories in responses when helpful
    .aria-label = Memories on
aiwindow-memories-off =
    .tooltiptext = Don’t use memories in responses
    .aria-label = Memories off

## New Chat Button

aiwindow-new-chat =
    .tooltiptext = New chat
    .aria-label = New chat

## Close Sidebar Button

aiwindow-close-sidebar =
    .tooltiptext = Close
    .aria-label = Close

## Chat History Menu
## The menu opened from the "..." button next to the new chat button. Its main
## view links to the chat history subview and Smart Window settings; the chat
## history subview lists recent chats and a shortcut to all chats.

aiwindow-history-menu =
    .tooltiptext = More options
    .aria-label = More options

aiwindow-history-menu-chat-history = Chat history

aiwindow-history-menu-back =
    .tooltiptext = Back
    .aria-label = Back

aiwindow-history-menu-view-all-chats = View all chats

aiwindow-history-menu-settings = { -smart-window-brand-name } settings

## Fullpage top actions
## Labeled buttons shown at the top of the fullpage Smart Window.

aiwindow-fullpage-new-chat =
    .label = New chat
aiwindow-fullpage-chat-history =
    .label = Chat history
# "More" is the label for a button that opens a menu of additional Smart Window
# options (currently Smart Window settings) — i.e. "more actions/options", not
# more content or more chat history.
aiwindow-fullpage-more =
    .label = More
    .title = More

## Sign out dialog

fxa-signout-dialog-body-aiwindow = Synced data will remain in your account. Open { -smart-window-brand-name(plural-form: "true") } will switch to Classic Windows.

## Smart Window Toggle Button (in-page)

smartwindow-switch-to-smart-window = Switch to { -smart-window-brand-name }

## Fullpage Footer Actions

smartwindow-footer-chats =
    .tooltiptext = Chats
    .aria-label = Chats
    .label = Chats

smartwindow-footer-history =
    .tooltiptext = History
    .aria-label = History
    .label = History

## Disclaimer
## Text displayed to user to warn user about potential mistakes.

smartwindow-disclaimer = AI can make mistakes.

## FirefoxView Chats
## Chats in this context refers to chats saved from the Smart Window Assistant

firefoxview-chats-nav = Chats
  .title = Chats
firefoxview-chats-header = Chats

firefoxview-chat-context-delete = Delete from Chats
    .accesskey = D

# Placeholder for the input field to search in open tabs ("search" is a verb).
firefoxview-search-text-box-chats =
  .placeholder = Search chats

## Variables:
##   $date (string) - Date to be formatted based on locale

firefoxview-chat-date-today = Today - { DATETIME($date, dateStyle: "full") }
firefoxview-chat-date-yesterday = Yesterday - { DATETIME($date, dateStyle: "full") }
firefoxview-chat-date-this-month = { DATETIME($date, dateStyle: "full") }
firefoxview-chat-date-prev-month = { DATETIME($date, month: "long", year: "numeric") }

## Message displayed in Firefox View when the user has no chat data

firefoxview-chats-empty-header = Get back to your chats
firefoxview-chats-empty-description = As you use { -smart-window-brand-name }, your chats will be saved here.

## Count displayed in fxview chat search results

# Variables:
#   $count (Number) - The number of chats matching the search query.

firefoxview-search-chat-results-count = { $count ->
  [one] { $count } chat
 *[other] { $count } chats
}

## Clear browsing data dialog

item-history-downloads-and-chat =
    .label = Browsing, download, and chat history
    .accesskey = B

item-history-downloads-and-chat-description = Clears site, download, and chat history

## Natural Language Interactions

smart-window-confirm-select-all =
    .aria-label = Select all
    .label = Select all

smart-window-confirm-deselect-all =
    .aria-label = Deselect all
    .label = Deselect all

smart-window-close-confirm =
    .tooltiptext = Cancel request and close
    .aria-label = Cancel request and close

smart-window-confirm-close-tab = Close

# Variables
#   $count (number) - Number of tabs to close
smart-window-confirm-close-tabs =
    { $count ->
        [one] Close { $count } tab
       *[other] Close { $count } tabs
    }

## Natural Language action callouts

# Shown after the Smart Window closes the user’s current tab in response to a
# natural language prompt action, anchored to the toolbar menu button.
smartwindow-close-tab-callout-title = Tab closed
smartwindow-close-tab-callout-subtitle = Reopen tabs from History anytime.

## Smart Window new tab promo

smart-window-default-promo-heading = Make { -smart-window-brand-name } your default?
smart-window-default-promo-message = { -brand-short-name } will open in { -smart-window-brand-name } every time.
smart-window-default-promo-primary-button = Set as default
smart-window-default-promo-additional-button = Not now

## Feedback modal

aiwindow-feedback-modal-title = Share feedback
aiwindow-feedback-what-worked-well = What worked well? No personal info, please.
aiwindow-feedback-choose-any = Choose any that apply
aiwindow-feedback-add-details = Add details if you’d like. No personal info, please.
aiwindow-feedback-disclaimer = Submitting shares your feedback and this chat to help { -brand-shorter-name } improve { -smart-window-brand-name }. Your other chats stay private. <a data-l10n-name="learn-more">Learn more</a>
aiwindow-feedback-submit = Submit
aiwindow-feedback-cancel = Cancel
aiwindow-feedback-reason-incorrect-or-misleading = Incorrect or misleading
aiwindow-feedback-reason-doesnt-address-my-request = Doesn’t address my request
aiwindow-feedback-reason-lacks-personalization = Lacks personalization or context
aiwindow-feedback-reason-performance-or-usability = Performance or usability issue
aiwindow-feedback-reason-harmful-or-offensive = Harmful or offensive
aiwindow-feedback-reason-other = Other
aiwindow-feedback-preview-report = View chat details
aiwindow-feedback-preview-report-with-page = View chat and page details
aiwindow-feedback-include-page-content = Share the pages referenced in this chat

## Smart Window ai-chat-grid

aiwindow-ai-chat-grid-view-controls =
    .aria-label = Switch view layout
aiwindow-ai-chat-grid-list-view =
    .aria-label = Switch mode: List View
    .tooltiptext = List View
aiwindow-ai-chat-grid-grid-view =
    .aria-label = Switch mode: Grid View
    .tooltiptext = Grid View

## Smart Window new-tab conversation starters
## These are short suggested user queries used to prompt the AI assistant when clicked on.
## They then become the first message in the conversation.
## When localizing, please write them as short instructions a user would give to an assistant.
## They should also be concise and direct, but not at the expense of losing meaning.

aiwindow-starter-writing-first-draft = Write a first draft
aiwindow-starter-writing-improve = Improve writing
aiwindow-starter-writing-proofread = Proofread a message

aiwindow-starter-planning-simplify = Simplify a topic
aiwindow-starter-planning-brainstorm = Brainstorm ideas
aiwindow-starter-planning-plan = Help make a plan

aiwindow-starter-browsing-history = Find tabs in history
aiwindow-starter-browsing-summarize = Summarize tabs
aiwindow-starter-browsing-compare = Compare tabs
