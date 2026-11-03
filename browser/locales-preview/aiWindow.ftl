# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

## Monitor Toolbar Button

# Toolbar button that opens the panel where the user creates a monitor
smartwindow-monitor-button =
    .label = Tasks
    .tooltiptext = Tasks

# Title of the panel opened by the toolbar button above
smartwindow-monitor-panel-title = Tasks

# Label above tasks that newly met their condition since the panel was last opened
smartwindow-monitor-panel-new-matches = New matches
# Label above the list of tasks the user has running
smartwindow-monitor-panel-watching = Recent
smartwindow-monitor-panel-empty-title = Nothing watched yet.
smartwindow-monitor-panel-empty-description = { -brand-short-name } can keep an eye on a page and tell you the moment it changes. It only checks while { -brand-short-name } is open. Create new task below to get started.
# Shown when the task's condition was met on its last check
smartwindow-monitor-panel-result-match = Match
smartwindow-monitor-panel-result-no-match = No match
# Shown when the most recent check failed, so there is no match to report.
smartwindow-monitor-panel-result-could-not-check = Couldn’t check
smartwindow-monitor-panel-create = Create new task
# Panel title while the user is filling in the create form
smartwindow-monitor-panel-create-title = Create new task
# Variables:
#   $used (number) - How many tasks the user has
#   $max (number) - The maximum number of tasks allowed
smartwindow-monitor-panel-count = { $used } of { $max }
smartwindow-monitor-panel-manage = Manage and view all tasks

## AI Tasks

# Desktop notification shown when a AI Tasks fires. A "monitor" is
# a user-created task in Smart Window that watches a web page and alerts the
# user when a condition they described is met.
# The user-facing name is not final.
ai-tasks-monitor-notification-title = Match found
# Variables:
#   $pageCount (Number) - How many pages the monitor watches
ai-tasks-monitor-notification-body =
    { $pageCount ->
        [one] { -smart-window-brand-name } found a match on a page you’re watching.
       *[other] { -smart-window-brand-name } found a match on pages you’re watching.
    }
ai-tasks-monitor-notification-snooze = Snooze
ai-tasks-monitor-notification-dismiss = Dismiss

# Desktop notification shown right after the user creates a monitor, so they
# know a match will be announced the same way. The notification title is the
# monitor's name.
# Variables:
#   $site (String) - Hostname of the first page the monitor watches, e.g. "example.com"
#   $extraCount (Number) - How many more pages the monitor watches besides $site
ai-tasks-monitor-created-notification-body =
    { $extraCount ->
        [0] Now watching { $site }. You’ll get a notification like this one when there’s a match.
        [one] Now watching { $site } and { $extraCount } other page. You’ll get a notification like this one when there’s a match.
       *[other] Now watching { $site } and { $extraCount } other pages. You’ll get a notification like this one when there’s a match.
    }

# Desktop notification shown when a monitor pauses itself, either because it
# went a long time without its condition being met or because it reached its
# maximum lifetime. The notification title is the monitor's name.
# Variables:
#   $days (Number) - Number of days the monitor ran without a match
ai-tasks-monitor-expired-notification-body-no-match = This task was automatically paused after { $days } days without a match. You can resume it anytime.
# Variables:
#   $days (Number) - Number of days the monitor has been running
ai-tasks-monitor-expired-notification-body-max-age = This task was automatically paused after { $days } days. You can resume it anytime.
ai-tasks-monitor-expired-notification-resume = Resume
# Desktop notification shown when a task's check could not run at all, for
# example because the page could not be loaded. The task's name is the
# notification title, so the body does not repeat it.
ai-tasks-monitor-error-notification-body = This task couldn’t run. Please check.

# Smart Window Alerts
# This file contains localized strings for the Smart Window alerts feature,
# which allows users to create alerts to monitor webpages for changes.

## Alert Creation Form - Strings used in the alert creation dialog and form fields

ai-tasks-alert-name =
  .label = Task name
ai-tasks-alert-alert =
  .label = Notify me when
  .placeholder = Examples: New JBL Tune 670NC headphones under $50 (not refurbished). Lasagna recipe appears on blog home.
  .description = Describe in detail what you want to watch for.
# Variables:
#   $maxPages (number) - The maximum number of webpages that can be monitored
ai-tasks-alert-pages =
  .label = Pages to watch ({ $maxPages } max)
  .placeholder = Type or paste a webpage address
ai-tasks-alert-check-label = How often to check
ai-tasks-alert-time-label = Time
ai-tasks-alert-day-label = Day

## Schedule Options - Strings for scheduling alert frequency

ai-tasks-alert-schedule-daily =
  .label = Daily
ai-tasks-alert-schedule-weekly =
  .label = Weekly

## Day labels for dropdown list for weekly scheduling

ai-tasks-alert-weekday-sunday =
  .label = Sunday
ai-tasks-alert-weekday-monday =
  .label = Monday
ai-tasks-alert-weekday-tuesday =
  .label = Tuesday
ai-tasks-alert-weekday-wednesday =
  .label = Wednesday
ai-tasks-alert-weekday-thursday =
  .label = Thursday
ai-tasks-alert-weekday-friday =
  .label = Friday
ai-tasks-alert-weekday-saturday =
  .label = Saturday

## Alert Actions - Button labels for alert management

ai-tasks-alert-create-button = Create task
ai-tasks-alert-cancel-button = Cancel
ai-tasks-alert-save-button = Save changes
ai-tasks-alert-delete-button =
  .aria-label = Delete alert
ai-tasks-alert-edit-button = Edit
ai-tasks-alert-pause-button = Pause
ai-tasks-alert-resume-button = Resume
ai-tasks-alert-check-now-button = Check now

## Dialog Headers - Titles for alert dialogs

ai-tasks-alert-modal-title = Create task

# Note shown at the bottom of the create form indicating required fields
# The asterisk (*) marks form fields that must be filled in.
ai-tasks-alert-required-note = * Required

## Page Content - Strings displayed on the alerts page

ai-tasks-page-title = Tasks
ai-tasks-add-alert-button = Create task
ai-task-page-description = { -smart-window-brand-name } can watch prices and content on pages you choose. You’ll get a desktop notification when it finds a match.
ai-tasks-no-monitors-title = { -smart-window-brand-name } tasks aren’t available in your region
ai-tasks-no-monitors-message = Learn more <a data-l10n-name="smart-window-link">about Smart Window</a>.
ai-tasks-blocked-title = { -smart-window-brand-name } tasks are blocked in AI controls
ai-task-page-no-alerts = Create a task to track prices or content on pages you choose. You’ll get notified when { -smart-window-brand-name } finds a match.
ai-task-page-no-alerts-title = Watch what matters
# Variables:
#   $count (number) - The number of tasks currently active
ai-tasks-alerts-count = { $count ->
    [one] { $count } task
   *[other] { $count } tasks
}
# Variables:
#   $count (number) - The number of webpages being watched
ai-tasks-alert-watching-pages = { $count ->
    [one] Watching { $count } webpage
   *[other] Watching { $count } webpages
}

## Error Messages - Validation and error messages for alert creation

# Shown under the task name field when it is left empty on submit
ai-tasks-alert-error-name-required = Enter a name for this task.
# Shown under the "Notify me when" field when it is left empty on submit
ai-tasks-alert-error-condition-required = Enter what you want to watch for.
# Shown under the page field for input that isn't a web address. A missing
# scheme is filled in with https automatically
ai-tasks-alert-error-invalid-url = Enter a valid URL.
ai-tasks-alert-error-duplicate-url = This URL has already been added.
# Shown under the page field when submitting with no pages added
ai-tasks-alert-error-no-pages = Add at least one page to watch.
# Variables:
#   $maxUrls (number) - Maximum number of pages allowed per task
ai-tasks-alert-error-max-urls = { $maxUrls ->
 *[other] You can watch up to { $maxUrls } pages. Delete one to add another.
}
# Shown when creating or resuming a task is refused because the limit of
# active tasks has been reached. Paused tasks don’t count toward the limit.
ai-tasks-alert-error-active-limit = You’ve reached the limit of active tasks. Pause or delete one to add or resume another.

## Accessibility - ARIA labels and accessibility text

ai-tasks-alert-show-details =
  .aria-label = Show task details
ai-tasks-alert-add-url = Add page

## Alert Display - Strings used when displaying alert details

ai-tasks-alert-change-history = Result
ai-tasks-alert-change-history-description = Results show what { -smart-window-brand-name } found at the time of each check. Open the page to confirm.
ai-tasks-alert-on-this-page = Pages to watch
# When viewing a monitor's details the user will see this string as a label for the box containing the prompt the user entered on monitor creation
ai-tasks-alert-the-alert = Notify me when
# Variables:
#   $time (DateTime) - The time to be formatted based on locale
ai-tasks-alert-schedule-daily-at = Check daily at { DATETIME($time, timeStyle: "short") }
# Variables:
#   $time (DateTime) - The time to be formatted based on locale
ai-tasks-alert-schedule-weekly-sunday = Check weekly on Sunday at { DATETIME($time, timeStyle: "short") }
# Variables:
#   $time (DateTime) - The time to be formatted based on locale
ai-tasks-alert-schedule-weekly-monday = Check weekly on Monday at { DATETIME($time, timeStyle: "short") }
# Variables:
#   $time (DateTime) - The time to be formatted based on locale
ai-tasks-alert-schedule-weekly-tuesday = Check weekly on Tuesday at { DATETIME($time, timeStyle: "short") }
# Variables:
#   $time (DateTime) - The time to be formatted based on locale
ai-tasks-alert-schedule-weekly-wednesday = Check weekly on Wednesday at { DATETIME($time, timeStyle: "short") }
# Variables:
#   $time (DateTime) - The time to be formatted based on locale
ai-tasks-alert-schedule-weekly-thursday = Check weekly on Thursday at { DATETIME($time, timeStyle: "short") }
# Variables:
#   $time (DateTime) - The time to be formatted based on locale
ai-tasks-alert-schedule-weekly-friday = Check weekly on Friday at { DATETIME($time, timeStyle: "short") }
# Variables:
#   $time (DateTime) - The time to be formatted based on locale
ai-tasks-alert-schedule-weekly-saturday = Check weekly on Saturday at { DATETIME($time, timeStyle: "short") }
ai-tasks-alert-status-watching = Active
ai-tasks-alert-status-paused = Paused

## AI Window Agent chat messages
## Shown in the chat conversation while the Agent sets up or manages a monitor.

# The <a data-l10n-name="tasks"> element links to the tasks page. Its text is
# the page address; keep it and the element (with its name) unchanged.
# Variables:
#   $count (number) - The maximum number of watch tasks allowed
smartwindow-agent-monitor-limit-reached =
    { $count ->
        [one] You’ve got { $count } active watch task, which is the current limit. To create a new task, go to <a data-l10n-name="tasks">about:smartwindowtasks</a> and delete one you no longer need.
       *[other] You’ve got { $count } active watch tasks, which is the current limit. To create a new task, go to <a data-l10n-name="tasks">about:smartwindowtasks</a> and delete one you no longer need.
    }

smartwindow-agent-monitor-setup = Let’s create a task to watch this page. The more detail you include about what you’re looking for, the better.

# Shown when the user runs the watch command from a page that has no address
# to watch (for example an internal page or a blank tab).
smartwindow-agent-monitor-page-not-watchable =
    I can’t watch this page — watching works on regular web pages, not { -brand-product-name }’s own screens.<br/>
    Open the page you want checked and type /watch there.

# Fallback name used for $monitorName when the watched page has no title.
smartwindow-agent-monitor-default-name = Page Watch

# The <a data-l10n-name="tasks"> element links to the tasks page. Its text is
# the page address; keep it and the element (with its name) unchanged.
# Variables:
#   $monitorName (string) - The name of the page or target being watched
#   $schedule (string) - Readable check cadence, e.g. "daily at 9:00 AM"
smartwindow-agent-monitor-watching = I’ll check { $monitorName } { $schedule }. I’ll notify you when I find content matching your task. View or edit this task anytime at <a data-l10n-name="tasks">about:smartwindowtasks</a>.

# Shown in place of the card when the user deletes a task from the chat.
# Variables:
#   $monitorName (string) - The name of the page or target that was being watched
smartwindow-agent-monitor-deleted = I’ve stopped watching { $monitorName } and removed this task.

# Shown in place of the card when the user cancels creating a task from the chat.
smartwindow-agent-monitor-canceled = Canceled. Is there anything else I can assist you with?

# Check watch schedule, added { $schedule } in the chat message.
# Variables:
#   $time (date) - The scheduled check time
smartwindow-agent-monitor-schedule-daily = daily at { DATETIME($time, hour: "numeric", minute: "2-digit") }

# Variables:
#   $time (date) - The scheduled check day and time
smartwindow-agent-monitor-schedule-weekly = weekly on { DATETIME($time, weekday: "long") } at { DATETIME($time, hour: "numeric", minute: "2-digit") }

# Status chip and change-history rows shown on a monitor card in chat.
smartwindow-agent-monitor-status-watching = Watching
smartwindow-agent-monitor-status-paused = Paused
smartwindow-agent-monitor-history-no-match = Checked, didn’t meet your task. Check again later.

## Alert deletion confirmation

ai-tasks-alert-delete-confirmation-title = Delete this task?

ai-tasks-alert-delete-confirmation-message = { -smart-window-brand-name } won’t watch these pages anymore. To stop watching temporarily, pause instead.

ai-tasks-alert-delete-confirm-button = Delete

## Used in the header to show the last check result

ai-tasks-alert-last-result-met = Match
ai-tasks-alert-last-result-not-met = No match

# Shown when the most recent check failed, so there is no match to report.
ai-tasks-alert-last-result-could-not-check = Couldn’t check

## Used in the history table as a simple status badge

ai-tasks-alert-condition-met = Match
ai-tasks-alert-condition-not-met = No match
# Shown in place of Match / No match when the check itself failed to run.
ai-tasks-alert-condition-could-not-check = Couldn’t check

## Notes shown beside a Couldn’t check badge, explaining why a check failed.
## One per failure the run can produce.

ai-tasks-alert-history-error-network = { -smart-window-brand-name } couldn’t connect to this page, so nothing was compared. This task will try again at its next scheduled time.

ai-tasks-alert-history-error-timeout = The page took too long to respond. This task will try again at its next scheduled time.

ai-tasks-alert-history-error-budget = { -smart-window-brand-name } has reached its daily check limit. This task will try again at its next scheduled time.

ai-tasks-alert-history-error-rate-limit = { -smart-window-brand-name } has reached its request limit for now. This task will try again at its next scheduled time.

ai-tasks-alert-history-error-too-large = There was too much page content to compare in one check. This task will try again at its next scheduled time.

ai-tasks-alert-history-error-capacity = { -smart-window-brand-name } is at capacity. This task will try again at its next scheduled time.

ai-tasks-alert-history-error-upstream-rate-limit = { -smart-window-brand-name } is temporarily handling too many requests. This task will try again at its next scheduled time.

ai-tasks-alert-history-error-connection-blocked = { -smart-window-brand-name } temporarily blocked requests from your connection. This task will try again at its next scheduled time.

ai-tasks-alert-history-error-request-blocked = { -smart-window-brand-name } blocked this request for security reasons. This task will try again at its next scheduled time.

ai-tasks-alert-history-error-request = { -smart-window-brand-name } couldn’t process this request. This task will try again at its next scheduled time.

ai-tasks-alert-history-error-service-budget = { -smart-window-brand-name } has reached its usage limit. This task will try again at its next scheduled time.

ai-tasks-alert-history-error-auth = This page asked us to sign in, so there was nothing to compare. Open the page, sign in, and the next check should work.

ai-tasks-alert-history-error-content-extraction = This page couldn’t be read — it may have moved or been deleted. Check the address saved on this task.

ai-tasks-alert-history-error-canceled = You stopped this check before it finished, so nothing was compared.

ai-tasks-alert-history-error-interrupted = { -brand-short-name } closed while this check was running, so nothing was compared. This task will try again at its next scheduled time.

ai-tasks-alert-history-error-model = { -smart-window-brand-name } couldn’t reach the service that reads pages. Nothing was compared. This task will try again at its next scheduled time.

ai-tasks-alert-history-error-prompt-load = { -smart-window-brand-name } couldn’t load the instructions for this check, so nothing was compared. This task will try again at its next scheduled time.

ai-tasks-alert-history-error-unknown = Something went wrong on our side and this check didn’t run. Nothing was compared.

## Smart Page - A page generated from the content of the user's tabs

# The feature name. It stays here rather than in brandings.ftl until the Smart
# Page strings are exposed to localization.
-smart-page-brand-name =
    { $plural-form ->
        [true] Smart Pages
       *[false] Smart Page
    }

# Title given to a generated page when the model returns no title of its own and
# the user did not say what the page should focus on.
ai-tab-default-page-title = Generated page

# Shown in place of a generated page when it can't be displayed, for
# example because the user deleted it, the address is wrong, or it
# failed to load.
aitab-page-error-heading = This { -smart-page-brand-name } isn’t available
# Introduces the list of possible reasons the page can’t be displayed.
aitab-page-error-intro = Here’s what may have happened:
aitab-page-error-reason-deleted = <strong>The page was deleted.</strong> To create a new one, go to another tab in { -smart-window-brand-name } and ask in the sidebar.
aitab-page-error-reason-incorrect-link = <strong>The link is incorrect.</strong> Search for the page by name in the address bar.
aitab-page-error-reason-shared-link = <strong>The link was shared with you.</strong> { -smart-page-brand-name(plural-form: "true") } can’t be shared by link. Ask the sender for a PDF instead.

# Page context menu entry that builds a generated page from the current page.
main-context-menu-create-aitab =
    .label = Create { -smart-page-brand-name }
    .accesskey = C

# Tab context menu entry that builds a generated page from the tabs the menu
# was opened on.
tab-context-create-aitab =
    .label = Create { -smart-page-brand-name }

# Tab group menu entry that builds a generated page from the group's tabs.
tab-group-editor-action-create-aitab =
    .label = Create { -smart-page-brand-name }

# Chat message that starts the conversation created by a "Create Smart Page" menu
# entry, written in the user's voice. The URLs of the chosen tabs are appended
# on the lines below it.
# Variables:
#   $tabCount (Number) - How many tabs the page is built from.
ai-tab-create-page-prompt =
    { $tabCount ->
        [one] Create a { -smart-page-brand-name } from this tab:
       *[other] Create a { -smart-page-brand-name } from these tabs:
    }

# A rating cell in a generated table, for example "4.5 / 5". Both numbers
# arrive already formatted for the locale.
# Variables:
#   $value (String) - The item's rating.
#   $max (String) - The highest possible rating.
ai-tab-table-rating = { $value } / { $max }

## Smartbar command palette
## Slash commands shown in the smartbar when the user types "/".

# Group header for the agent task commands (for example, /watch).
smartbar-command-tasks-header = Tasks
# The /watch command creates a task that watches a page for changes.
smartbar-command-watch-label = /watch
smartbar-command-watch-description = Get notified when pages change
# Footer note shown at the bottom of the command palette, hinting that more
# task command types will be added later.
smartbar-command-coming-soon = More types of tasks are coming soon

## Smart Form Fill

ai-smart-form-fill-autocomplete-label = Smart Form Fill
ai-smart-form-fill-autocomplete-loading = Loading
ai-smart-form-fill-autocomplete-sources-label = Fill from sources
ai-smart-form-fill-autocomplete-choose-tabs = Open or choose pages related to this form to get autofill suggestions
ai-smart-form-fill-autocomplete-open-tabs = Open pages related to this form to get autofill suggestions
ai-smart-form-fill-close-review =
    .label = Close

# Variables:
#   $tabs (number) - The number of tabs used as a source for autofill
ai-smart-form-fill-autocomplete-tabs-count =
    { $tabs ->
        [one] { $tabs } tab
       *[other] { $tabs } tabs
    }

# Sources are what tabs the Smart Form Fill should use to generate field values
ai-smart-form-fill-edit-sources = Edit sources
ai-smart-form-fill-suggested-tabs = Suggested tabs
ai-smart-form-fill-other-tabs = Other tabs

# Variables:
#   $tabTitle (string) - Title of the tab controlled by the toggle
ai-smart-form-fill-tab-select-toggle =
    .aria-label = Toggle tab selection for “{ $tabTitle }”

ai-smart-form-fill-cancel-tab-select =
    .label = Cancel

ai-smart-form-fill-accept-tab-select =
    .label = Done

ai-smart-form-fill-fill-form =
    .label = Fill form

ai-smart-form-fill-review-heading = Review suggestions

# Variables:
#   $count (number) - The number of generated form field suggestions
ai-smart-form-fill-review-description =
    { $count ->
        [one] We found suggestions for { $count } field. Edit or delete anything that’s not correct.
       *[other] We found suggestions for { $count } fields. Edit or delete anything that’s not correct.
    }

ai-smart-form-fill-field =
    .label = Field

ai-smart-form-fill-jump-to-bottom =
    .tooltiptext = Jump to bottom
    .aria-label = Jump to bottom of suggestions

ai-smart-form-fill-cancel-review =
    .label = Cancel

ai-smart-form-fill-finding-suggestions = Finding suggestions

ai-smart-form-fill-stop-finding-suggestions =
    .aria-label = Stop finding suggestions

ai-smart-form-fill-success-heading = Suggestions added to form
ai-smart-form-fill-success-description = Review the form and fill any remaining fields before you submit.

ai-smart-form-fill-no-changes-heading = No changes applied
ai-smart-form-fill-no-changes-description = Smart Form Fill didn’t fill any fields. Check the form and try again.

ai-smart-form-fill-no-suggestions-heading = No suggestions found
ai-smart-form-fill-no-suggestions-description = Smart Form Fill wasn’t able to generate any suggestions for this form.

ai-smart-form-fill-error-try-again-heading = Suggestions weren’t added to the form
ai-smart-form-fill-error-try-again-description = Something went wrong. Try adding suggestions again.

ai-smart-form-fill-error-heading = Suggestions weren’t added
ai-smart-form-fill-error-description = Something went wrong. To try again, select a form field and choose Fill from sources.

ai-smart-form-fill-try-again =
    .label = Try again

## Smart Page generated pages

# Shown above the title of a generated page that was created today.
aitab-created-today = Created today

# Shown above the title of a generated page created on an earlier date.
# Variables:
#   $date (number) - Timestamp of when the page was generated.
aitab-created-on = Created { DATETIME($date, month: "short", day: "numeric") }

# Button that re-fetches the sources a generated page was built from.
aitab-page-refresh-sources =
    .label = Refresh sources

# Replaces the refresh label while the sources are being re-fetched.
aitab-page-refreshing-sources =
    .label = Refreshing sources

# Icon-only button that deletes the generated page.
aitab-page-delete =
    .aria-label = Delete page
    .title = Delete page

aitab-page-delete-dialog-title = Delete this { -smart-page-brand-name }?
aitab-page-delete-dialog-message = This page will be permanently deleted. The sources used to create it won’t be affected.

aitab-page-delete-dialog-cancel =
    .label = Cancel

aitab-page-delete-dialog-confirm =
    .label = Delete

# Credit at the bottom of a generated page.
aitab-page-footer-brand = { -smart-window-brand-name }

smartwindow-aitab-creating = Creating your { -smart-page-brand-name }
smartwindow-aitab-updating = Updating your { -smart-page-brand-name }
smartwindow-aitab-creating-an-aitab-failed = Tried to create { -smart-page-brand-name }, but couldn’t
smartwindow-aitab-updating-an-aitab-failed =  Tried to update { -smart-page-brand-name }, but couldn’t

# "Open here" opens aitab url in the current tab
smartwindow-aitab-open-current =
    .label = Open here
# "Open in new tab" opens a new browser tab with the AITab url in it
smartwindow-aitab-open-new =
    .label = Open in new tab

smartwindow-aitab-created-an-aitab = Created { -smart-page-brand-name }
smartwindow-aitab-updated-an-aitab = Updated { -smart-page-brand-name }
smartwindow-aitab-empty-chat-message = Want to make changes to your { -smart-page-brand-name }? Ask in the chat to add or remove content.

# Variables:
#   $tags (Number) - Number of sources used to create the AITab
smart-window-aitab-sources-count = { $tags ->
    [one] { $tags } source
   *[other] { $tags } sources
}

## "Pick up where you left off" cards for resuming browsing or chat journeys.
## A "journey" is a past browsing or chat session the user was in the middle
## of - for example, a set of tabs open toward some task, or an ongoing
## conversation - that the user can pick back up from where they left off.

# Variables:
#   $count (Number) - Number of tabs in the journey
aiwindow-resume-card-tab-count =
    { $count ->
        [one] { $count } tab
       *[other] { $count } tabs
    }
# Variables:
#   $text (String) - The journey title
aiwindow-resume-card-more = More
    .aria-label = More options for { $text }
aiwindow-resume-card-open-tabs = Open tabs
aiwindow-resume-card-snooze = Snooze for now
# Variables:
#   $text (String) - The journey title
aiwindow-resume-card-resume = Resume
    .aria-label = Resume { $text }
# Variables:
#   $text (String) - The journey title being dismissed
aiwindow-resume-card-dismiss =
    .title = Dismiss { $text }
    .aria-label = Dismiss { $text }

## Resume section
## Toggles between showing a couple of "Pick up where you left off" resume
## cards and showing all of them.

aiwindow-resume-section-heading = Jump back in
# Variables:
#   $count (Number) - Total number of resume cards in the section
aiwindow-resume-section-show-more = Show more ({ $count })
# Variables:
#   $count (Number) - Total number of resume cards in the section
aiwindow-resume-section-show-less = Show less ({ $count })

# Shown in place of the cards when there are no journeys to suggest yet. A
# "journey" is a past browsing or chat session the user can pick back up
# from where they left off.
aiwindow-resume-section-empty-no-suggestions-heading = Nothing to suggest right now
aiwindow-resume-section-empty-no-suggestions-description = When there’s something to pick back up, you’ll see it here.
# Shown in place of the cards when the user has dismissed every suggestion.
aiwindow-resume-section-empty-all-dismissed-heading = All caught up
aiwindow-resume-section-empty-all-dismissed-description = That’s all the suggestions for now. Check back later for more.
aiwindow-resume-section-hide = Hide for now
