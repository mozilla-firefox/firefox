# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

smartwindow-messages-document-title = { -smart-window-brand-name } chat messages

## Context chips in the chat content

# Variables:
#   $tags (Number) - Number of context chips added in a chat bubble (only when we have 3 or more chips)
smart-window-context-chips-tag-count = { $tags ->
    [one] { $tags } Tag
   *[other] { $tags } Tags
}

## Error messages in the chat content

smartwindow-assistant-error-generic-header = Something went wrong. Please try again.
smartwindow-assistant-error-budget-header = You’ve reached today’s chat limit.
smartwindow-assistant-error-account-header = To use { -smart-window-brand-name }, you’ll need to sign in to FunComputer.
smartwindow-assistant-error-capacity-header = { -smart-window-brand-name } is at capacity right now. Please try again later.

smartwindow-assistant-error-budget-body = You can still browse in this window. Chat will be available again once your daily limit resets.
smartwindow-assistant-error-many-requests-header = Please wait a moment and try again. Too many messages were sent in a short time.
smartwindow-assistant-error-max-length-header = It’s time to start a new chat. This one’s reached its length limit.
smartwindow-assistant-error-request-blocked-header = { -smart-window-brand-name } couldn’t reach the server. Try a different network, or disable your VPN.
# Variables:
#   $status (Number) - HTTP status code returned by the inference back-end
smartwindow-assistant-error-http-header = Server error (HTTP { $status }). Please try again.
smartwindow-retry-btn = Try Again
smartwindow-clear-btn = New chat
smartwindow-signin-btn = Sign in to FunComputer

## Assistant Message footer

aiwindow-memories-used = Memories used
aiwindow-memories-callout-description = Memories helped personalize this response.
aiwindow-memories-learn-more = Learn more
aiwindow-manage-memories =
    .label = Memory settings
aiwindow-retry-without-memories =
    .label = Retry without memories
aiwindow-retry =
  .tooltiptext = Retry
  .aria-label = Retry
aiwindow-copy-message =
    .tooltiptext = Copy
    .aria-label = Copy message
aiwindow-copy-table =
    .tooltiptext = Copy table
    .aria-label = Copy table
aiwindow-table-scroll-indicator = Scroll to see more
aiwindow-thumbs-up =
    .tooltiptext = Share positive feedback
    .aria-label = Share positive feedback
aiwindow-thumbs-down =
    .tooltiptext = Share negative feedback
    .aria-label = Share negative feedback
aiwindow-applied-memories-popover =
    .aria-label = Memories panel
aiwindow-applied-memories-list =
    .aria-label = Memories
# Variables:
#   $summary (String) - The memory text that will be deleted
aiwindow-delete-memory-button =
    .aria-label = Delete { $summary }

## Jump to Bottom Button

aiwindow-jump-to-bottom =
    .tooltiptext = Jump to bottom
    .aria-label = Jump to bottom of chat

## Natural Language Action

smartwindow-nl-retry-tool-button =
    .label = Retry

smartwindow-nl-retry-message = If you still want to close tabs, choose <strong>Retry</strong> and make your selection in the card that opens.
smartwindow-nl-retry-group-tabs-message = If you still want to group tabs, choose <strong>Retry</strong> and select which ones in the card that opens.

smartwindow-nl-thinking = Looking for matching tabs…
smartwindow-loading-assistant-response =
    .aria-label = Loading assistant response
smartwindow-nl-undo-button =
    .label = Undo

## Variables
##   $count (number) - Number of tabs closed/restored

smart-window-closed-tabs-label =
    { $count ->
        [one] Closed { $count } tab
       *[other] Closed { $count } tabs
    }
smart-window-closed-tabs-summary =
    { $count ->
        [one] Done! Tab closed.
       *[other] Done! Tabs closed.
    }
smart-window-closed-tabs-row-label = Closed tabs
smart-window-closed-and-restored-label = Closed and restored tabs
smart-window-restored-row-label =
    { $count ->
        [one] Restored { $count } tab
       *[other] Restored { $count } tabs
    }
smart-window-restore-success-summary =
    { $count ->
        [one] Tab closed, then restored.
       *[other] Tabs closed, then restored.
    }
smart-window-cancelled-label = Request canceled.
# Button label - "Group" is a verb (action to group tabs)
smart-window-confirm-group-tab = Group
# Button label - "Group" is a verb (action to group tabs)
smart-window-confirm-group-tabs =
    { $count ->
        [one] Group { $count } tab
       *[other] Group { $count } tabs
    }
# Button label - "Open" is a verb (action to open, then group, tabs)
smart-window-confirm-open-tab = Open
# Button label - "Open" is a verb (action to open, then group, tabs)
smart-window-confirm-open-tabs =
    { $count ->
        [one] Open { $count } tab
       *[other] Open { $count } tabs
    }

# Action result labels for grouped tabs
# Variables
#   $count (number) - Number of tabs grouped
smart-window-grouped-tabs-label =
    { $count ->
        [one] Grouped { $count } tab
       *[other] Grouped { $count } tabs
    }

# Fallback name used in place of $label above when the model didn't provide
# one for the tab group.
smart-window-default-tab-group-label = Tab Group

# Variables
#   $count (number) - Number of tabs grouped
#   $label (string) - The label of the tab group
smart-window-grouped-tabs-summary =
    { $count ->
        [one] Created the group “{ $label }” and added { $count } tab.
        *[other] Created the group “{ $label }” and added { $count } tabs.
    }

smart-window-grouped-tabs-row-label = Grouped tabs

# Action result labels for opened (and grouped) tabs
# Variables
#   $count (number) - Number of tabs opened
smart-window-opened-tabs-label =
    { $count ->
        [one] Opened { $count } tab
       *[other] Opened { $count } tabs
    }

# Variables
#   $count (number) - Number of tabs opened
smart-window-opened-tabs-summary-single =
    { $count ->
        [one] Opened { $count } tab.
       *[other] Opened { $count } tabs.
    }

# Variables
#   $count (number) - Number of tabs opened
#   $label (string) - The label of the tab group
smart-window-opened-tabs-summary-group =
    { $count ->
       *[other] Created the group “{ $label }” and opened { $count } tabs.
    }

smart-window-opened-tabs-row-label = Opened tabs

# Action result labels for switching to a single already-open tab
smart-window-switched-tab-label = Switched tabs
# Variables
#   $title (String) - Title of the tab switched to
smart-window-switched-tab-summary = Switched to “{ $title }”.

# Action result labels for ungrouped tabs
smart-window-grouped-and-ungrouped-label = Tabs ungrouped

# Variables
#   $count (number) - Number of tabs ungrouped
smart-window-ungroup-success-summary =
    { $count ->
        [one] { $count } tab grouped, then ungrouped.
        *[other] { $count } tabs grouped, then ungrouped.
    }


smart-window-ungrouped-row-label =
    { $count ->
        [one] Ungrouped { $count } tab
        *[other] Ungrouped { $count } tabs
    }

## Action log

action-log-searching-tabs = Searching tabs
action-log-searched-open-tabs = Searched open tabs
action-log-searching-history = Searching history
action-log-searched-history = Searched history
action-log-reading-page = Reading page
# Read is past tense, to indicate that the action has been completed.
action-log-read-page = Read page content
action-log-searching-web = Searching the web
action-log-searched-web = Searched the web
# Exa is the name of a third-party web search API and a brand name that
# should not be translated. The <a> wraps "Exa" as a link to a support
# article explaining the third-party web search integration.
action-log-searching-web-with-exa = Searching the web with <a data-l10n-name="exa-link">Exa</a>
# Exa is the name of a third-party web search API and a brand name that
# should not be translated. The <a> wraps "Exa" as a link to a support
# article explaining the third-party web search integration.
action-log-searched-web-with-exa = Searched the web with <a data-l10n-name="exa-link">Exa</a>
action-log-checking-memories = Checking memories
action-log-checked-memories = Checked memories
action-log-searching-settings = Searching settings
action-log-searched-settings = Searched settings
action-log-searching-world-cup-matches = Searching World Cup matches
action-log-searched-world-cup-matches = Searched World Cup matches
action-log-checking-world-cup-live = Checking live World Cup matches
action-log-checked-world-cup-live = Checked live World Cup matches

# Variables
#   $count (Number) - how many tool steps completed in the turn
action-log-completed-steps =
    { $count ->
        [one] Completed 1 step
       *[other] Completed { $count } steps
    }

## Assistant Loader

# Shown while the assistant analyzes search results that it loaded into the
# current tab on the user’s behalf. Communicates both that the tab’s content
# changed and that the assistant is reviewing the results before responding.
smartwindow-search-loader-text = Loaded search results in this tab. Analyzing…

## Citations

# Label for the button that reveals overflowing citation sources.
# Variables:
#   $count (Number) - Number of the additional hidden sources
smartwindow-assistant-citations-more-label = +{ $count } more
