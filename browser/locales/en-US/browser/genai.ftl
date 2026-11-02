# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

## Generative AI (GenAI) Settings section

genai-settings-chat-chatgpt-links = By choosing ChatGPT, you agree to the OpenAI <a data-l10n-name="link1">Terms of Use</a> and <a data-l10n-name="link2">Privacy Policy</a>.
genai-settings-chat-claude-links = By choosing Anthropic Claude, you agree to the Anthropic <a data-l10n-name="link1">Consumer Terms of Service</a>, <a data-l10n-name="link2">Usage Policy</a>, and <a data-l10n-name="link3">Privacy Policy</a>.
genai-settings-chat-copilot-links = By choosing Copilot, you agree to the <a data-l10n-name="link1">Copilot AI Experiences Terms</a> and <a data-l10n-name="link2">Microsoft Privacy Statement</a>.
genai-settings-chat-gemini-links = By choosing Google Gemini, you agree to the <a data-l10n-name="link1">Google Terms of Service</a>, <a data-l10n-name="link2">Generative AI Prohibited Use Policy</a>, and <a data-l10n-name="link3">Gemini Apps Privacy Notice</a>.
genai-settings-chat-huggingchat-links = By choosing HuggingChat, you agree to the <a data-l10n-name="link1">HuggingChat Privacy Notice</a> and <a data-l10n-name="link2">Hugging Face Privacy Policy</a>.
genai-settings-chat-lechat-links-2 = By choosing Mistral Vibe, you agree to the Mistral AI <a data-l10n-name="link1">Terms of Service</a> and <a data-l10n-name="link2">Privacy Policy</a>.
genai-settings-chat-localhost-links = Bring your own private local chatbot such as <a data-l10n-name="link1">llamafile</a> from { -vendor-short-name }’s Innovation group.
genai-settings-chat-scout-links = By choosing Yahoo Scout, you agree to the Yahoo <a data-l10n-name="link1">Terms of Service</a> and <a data-l10n-name="link2">Privacy Policy</a>.

## Chatbot prompts
## Prompts are plain language ‘instructions’ sent to a chatbot.
## These prompts have been made concise and direct in English because some chatbot providers
## have character restrictions and being direct reduces the chance for misinterpretation.
## When localizing, please be concise and direct, but not at the expense of losing meaning.

# This prompt is added to the beginning of selection prompts sent to a chatbot.
# $tabTitle (string) - title of the webpage
# $selection (string) - selected text
genai-prompt-prefix-selection = I’m on page “{ $tabTitle }” with “{ $selection }” selected.

# Prompt purpose: help users understand what a selection covers at a glance
genai-prompts-summarize =
    .label = Summarize
    .value = Please summarize the selection using precise and concise language. Use headers and bulleted lists in the summary, to make it scannable. Maintain the meaning and factual accuracy.
# Prompt purpose: help users understand what a webpage covers at a glance
# $url (string) - address of the webpage to summarize
genai-prompts-summarize-page = Please summarize the webpage at { $url } using precise and concise language. Use headers and bulleted lists in the summary, to make it scannable. Maintain the meaning and factual accuracy.
# Prompt purpose: make a selection easier to read
genai-prompts-simplify =
    .label = Simplify language
    .value = Please rewrite the selection using short sentences and simple words. Maintain the meaning and factual accuracy.
# Prompt purpose: test understanding of selection in an interactive way
genai-prompts-quiz =
    .label = Quiz me
    .value = Please quiz me on this selection. Ask me a variety of types of questions, for example multiple choice, true or false, and short answer. Wait for my response before moving on to the next question.
# Prompt purpose: helps users understand words, phrases, concepts
genai-prompts-explain =
    .label = Explain this
    .value = Please explain the key concepts in this selection, using simple words. Also, use examples.
# Prompt purpose: writing tool that helps users with spelling and grammar mistakes and produce a response that identifies errors and rewrites the inputted text correctly
genai-prompts-proofread =
    .label = Proofread
    .value = Please proofread the selection for spelling and grammar errors. Identify any mistakes and provide a corrected version of the text. Maintain the meaning and factual accuracy and output the list of proposed corrections first, followed by the final, corrected version of the text.

## Chatbot menu shortcuts

genai-menu-ask-smart-window =
    .label = Ask…
    .accesskey = z
genai-menu-no-provider-2 =
    .label = Ask an AI Chatbot
    .accesskey = z
genai-menu-choose-chatbot =
    .label = Choose an AI Chatbot
genai-menu-ask-generic-2 =
    .label = Ask AI Chatbot
    .accesskey = z
# $provider (string) - name of the provider
genai-menu-ask-provider-2 =
    .label = Ask { $provider }
    .accesskey = z
genai-menu-open-generic =
    .label = Open AI Chatbot
# $provider (string) - name of the provider
genai-menu-open-provider =
    .label = Open { $provider }
genai-menu-remove-generic =
    .label = Remove AI Chatbot
# $provider (string) - name of the provider
genai-menu-remove-provider =
    .label = Remove { $provider }
genai-menu-remove-sidebar =
    .label = Remove from Sidebar
genai-shortcut-button-3 =
    .tooltiptext = Ask about this text
    .aria-label = Ask about this text
# $engine (string) - name of the search engine
# $selection (string) - the selected text, truncated
genai-shortcut-search-button =
    .tooltiptext = Search { $engine } for “{ $selection }”
    .aria-label = Search { $engine } for “{ $selection }”
genai-shortcut-copy-button =
    .tooltiptext = Copy selected text
    .aria-label = Copy selected text
# Shown briefly after clicking the Copy action in the text selection menu,
# confirming the selection was copied to the clipboard.
genai-shortcut-copy-confirmation = Copied
genai-shortcut-more-actions-button =
    .tooltiptext = More options
    .aria-label = More options
genai-more-actions-panel =
    .aria-label = Choose which actions to show
# $provider (string) - name of the chatbot provider
genai-more-actions-ai-provider =
    .label = Ask { $provider }
genai-more-actions-ai-generic =
    .label = Ask AI
genai-more-actions-search =
    .label = Search
genai-more-actions-copy =
    .label = Copy
# The span is a right-pointing chevron that separates the steps of the path to
# the setting, in place of a ">" character.
genai-more-actions-all-off-notice = You won’t see this menu when you select text. Visit Settings <span data-l10n-name="chevron"></span> Tabs and browsing to show it again.
genai-more-actions-manage-settings =
    .label = Manage settings
genai-selection-callout-title = Highlight text to choose an action
# "Search" and "Copy" are verbs.
genai-selection-callout-text = Search or copy when you select text. Choose which actions appear with the <span data-l10n-name="more-actions">•••</span> menu.

genai-menu-new-badge = New
genai-menu-summarize-page = Summarize Page
genai-menu-summarize-with-ai = Summarize with AI

genai-input-ask-smart-window =
    .placeholder = Ask…
genai-input-ask-generic =
    .placeholder = Ask AI chatbot…
# $provider (string) - name of the provider
genai-input-ask-provider =
    .placeholder = Ask { $provider }…

# $selectionLength (number) - selected text length
# $maxLength (number) - max length of what can be selected
genai-shortcuts-selected-warning-generic =
    .heading = AI chatbot won’t get your full selection
    .message = { $selectionLength ->
        *[other] You’ve selected about { $selectionLength } characters. The number of characters we can send to the AI chatbot is about { $maxLength }.
    }
# $provider (string) - name of the provider
# $selectionLength (number) - selected text length
# $maxLength (number) - max length of what can be selected
genai-shortcuts-selected-warning =
    .heading = { $provider } won’t get your full selection
    .message = { $selectionLength ->
        *[other] You’ve selected about { $selectionLength } characters. The number of characters we can send to { $provider } is about { $maxLength }.
    }
genai-shortcuts-hide =
    .label = Hide chatbot shortcut

## Chatbot header

genai-chatbot-title = AI chatbot
genai-header-provider-menu =
    .title = Choose a chatbot
genai-header-settings-button =
    .title = AI Chat settings
genai-header-close-button =
    .title = Close

genai-provider-view-details =
    .label = View chatbot details
genai-options-reload-generic =
    .label = Reload AI chatbot
# $provider (string) - name of the provider
genai-options-reload-provider =
    .label = Reload { $provider }
genai-options-show-shortcut =
    .label = Show shortcut when selecting text
genai-options-hide-shortcut =
    .label = Hide shortcut when selecting text
genai-options-about-chatbot =
    .label = About AI chatbots in { -brand-short-name }

## Chatbot footer

genai-page-button-summarize = Summarize page

## Chatbot onboarding

genai-chatbot-summarize-title = New! Summarize pages in one click
genai-chatbot-summarize-button = Summarize page

# “Summarize Page” should be consistent with the translation for the string genai-menu-summarize-page
genai-chatbot-summarize-sidebar-provider-subtitle = Right-click on your AI chatbot in the sidebar and choose “Summarize Page”.
# “Summarize Page” should be consistent with the translation for the string genai-menu-summarize-page
genai-chatbot-summarize-sidebar-generic-subtitle = Right-click the sparkles button in the sidebar and choose “Summarize Page”. The first time, you’ll also choose an AI chatbot.

# “Summarize page” should be consistent with the translation for the string genai-page-button-summarize
genai-chatbot-summarize-footer-provider-subtitle = Open your AI chatbot in the sidebar and choose “Summarize page” at the bottom.
genai-chatbot-summarize-footer-generic-subtitle = Add an AI chatbot to the { -brand-short-name } sidebar to quickly summarize pages.

genai-chatbot-contextual-title = Use an AI chatbot without switching tabs
genai-chatbot-contextual-subtitle = Chat and browse side-by-side when you add an AI chatbot in the { -brand-short-name } sidebar.
genai-chatbot-contextual-button = Choose a chatbot

genai-onboarding-choose-header = Choose an AI chatbot to use in the { -brand-short-name } sidebar
# "Switch anytime" refers to allowing the user to switch to a different chatbot.
genai-onboarding-choose-description = Switch anytime. For help choosing, <a data-l10n-name="learn-more">learn more about each chatbot</a>.
genai-onboarding-primary = Continue
genai-onboarding-secondary = Close
genai-onboarding-claude-tooltip =
    .title = Anthropic Claude
genai-onboarding-chatgpt-tooltip =
    .title = ChatGPT
genai-onboarding-copilot-tooltip =
    .title = Copilot
genai-onboarding-gemini-tooltip =
    .title = Google Gemini
genai-onboarding-huggingchat-tooltip =
    .title = HuggingChat
genai-onboarding-lechat-tooltip-2 =
    .title = Mistral Vibe
genai-onboarding-scout-tooltip =
    .title = Yahoo Scout

## Model Optin Component

genai-model-optin-continue =
  .label = Continue

genai-model-optin-optout =
  .label = Cancel

genai-model-optin-cancel =
  .label = Cancel

## Link previews

# ‘min’ is short for “minute”
# ‘mins’ is short for “minutes”
# An estimate for how long it takes to read an article,
# expressed as a range covering both slow and fast readers.
# Variables:
#   $rangePlural (String): The plural category of the range, using the same set as for numbers.
#   $range (String): The range of minutes as a localised string. Examples: "3-7", "~1".
link-preview-reading-time =
    { $rangePlural ->
        [one] { $range } min reading time
       *[other] { $range } mins reading time
    }

# Error message displayed when a link preview cannot be generated
link-preview-error-message-v2 = { -brand-short-name } can’t preview this link

# Text for the link to visit the original URL when in error state
link-preview-visit-link = Visit link

# Error message when key points generation (summary highlights or main ideas of page content) fails for a page
link-preview-generation-error-missing-data-v2 = { -brand-short-name } can’t generate key points for this webpage.

# Error message when something went wrong during key point generation
link-preview-generation-error-unexpected = Something went wrong.

# Text for the retry link when generation fails
link-preview-generation-retry = Try again

# Button that opens the Link Preview settings
link-preview-settings-button =
    .title = Link Preview Settings

link-preview-settings-enable =
    .label = Enable link previews
    .description = See the page title, description, and more when you use the shortcut or right-click on a link.
link-preview-settings-key-points =
    .label = Allow AI to read the beginning of the page and generate key points
link-preview-settings-long-press =
    .label = Shortcut: Click and hold the link for 1 second (long press)

highlight-to-search-settings-enable =
    .label = Show actions menu when selecting text
    .description = Get quick access to helpful actions when you select content.
# $provider (string) - name of the AI chatbot provider
highlight-to-search-settings-ask-provider =
    .label = Ask { $provider }
highlight-to-search-settings-ask-generic =
    .label = Ask AI
# Checkbox label for one of the actions in the menu that appears when selecting
# text on a page. "Search" is a verb: the action searches the selected text
# with the default search engine.
highlight-to-search-settings-search =
    .label = Search
# Checkbox label for one of the actions in the menu that appears when selecting
# text on a page. "Copy" is a verb: the action copies the selected text to the
# clipboard.
highlight-to-search-settings-copy =
    .label = Copy

# Title that appears when user is shown the opt-in flow for link previews
link-preview-optin-title = See more with AI?

# Message that appears when user is shown the opt-in flow for link previews
link-preview-optin-message = { -brand-short-name } uses AI to read the beginning of the page and generate a few key points. To prioritize your privacy, this happens on your device.

# Onboarding card title for long press
link-preview-onboarding-title-long-press = New: Click and hold any link for a preview

# Onboarding card description for long press
link-preview-onboarding-description-long-press = See a short description, reading time, and more to decide if the link is worth opening. Also available on right-click.

# Header for the key points section
link-preview-key-points-header = Key points

# Disclaimer for AI-generated key points
link-preview-key-points-disclaimer = Key points are AI-generated and may have mistakes.

# Progress message for the first-time setup
# $progress (number) - The percentage value 1-100 indicating the progress of the setup.
link-preview-setup = First-time setup • <strong>{ $progress }%</strong>

# Message indicating faster performance after initial setup
link-preview-setup-faster-next-time = You’ll see key points more quickly next time.

# Onboarding card See a preview button
link-preview-onboarding-button = See a preview

# Onboarding card Close button
link-preview-onboarding-close = Close

# Title for the first-time setup modal
link-preview-first-time-setup-title = First-time setup

# Message for the first-time setup modal
link-preview-first-time-setup-message = This may take a moment. You’ll see key points more quickly next time.
