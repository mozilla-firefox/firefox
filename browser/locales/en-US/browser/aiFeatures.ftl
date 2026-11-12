# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

preferences-ai-controls-block-confirmation-smart-window = { -smart-window-brand-name }
smart-window-block-title = Block { -smart-window-brand-name }?
smart-window-block-description-both = This will delete your { -smart-window-brand-name } chats and memories.
smart-window-block-description-chats = This will delete your { -smart-window-brand-name } chats.
smart-window-block-description-memories = This will delete your { -smart-window-brand-name } memories.

ai-window-features-group =
    .label = { -smart-window-brand-name }
    .description = Ask questions, compare pages, and get personalized suggestions with a built-in assistant.

smart-window-select-label =
    .label = { -smart-window-brand-name }

ai-window-activate-link =
    .label = Get started

ai-window-personalize-button =
    .label = { -smart-window-brand-name } settings

ai-window-personalize-header =
    .heading = { -smart-window-brand-name }

ai-window-default-section =
    .label = Default settings
ai-window-is-default-window =
    .label = Use { -smart-window-brand-name } by default
    .description = Open { -smart-window-brand-name } when { -brand-short-name } starts, restarts, or opens links from other apps.
ai-window-open-sidebar =
    .label = Open assistant automatically
    .description = Show the assistant sidebar on each new tab. Close it anytime.
ai-window-smart-cursor-in-smart-window =
    .label = Show shortcuts when selecting text
    .description = Get quick access to summarize, explain, and more.

smart-window-model-section =
    .label = Assistant model
    .description = Choose a model based on what’s important to you.

smart-window-model-radio-group =
    .aria-label = Assistant model
smart-window-model-learn-link = Learn about models

## Full name indicates the full version name of the model currently listed along with its publisher
##   $shortName (string) - The display name of the model collection
##   $model (string) - The name of the AI model
##   $ownerName (String) - The name of owner of the AI model

smart-window-model-fast-v2 =
    .label = {$shortName}: Fast
    .description = Best for quick answers when speed matters. Full name: { $model } by { $ownerName }
smart-window-model-flexible-v2 =
    .label = {$shortName}: Flexible
    .description = Ready for a variety of tasks. Full name: { $model } by { $ownerName }
smart-window-model-personal-v2 =
    .label = {$shortName}: Personal
    .description = Built for personalized help across languages. Full name: { $model } by { $ownerName }

smart-window-model-custom =
    .label = Custom: Use your own LLM
smart-window-model-custom-name =
    .label = Model name
    .placeholder = Example: glm4
smart-window-model-custom-url =
    .label = Model endpoint
    .placeholder = Example: http://localhost:11434/v1
smart-window-model-custom-token =
    .label = API key or auth token, if required
smart-window-model-custom-info =
    .message = When you use a custom model, { -smart-window-brand-name } may not work as expected.
smart-window-model-custom-more-link = More about custom models
smart-window-model-custom-save =
    .label = Save
smart-window-model-custom-save-confirmation = Model details saved. Start a new chat to test.

ai-window-memories-section =
    .label = Memories
    .description = { -brand-short-name } can learn from your activity to create memories. They’re used to help personalize responses and are stored locally on this device.

ai-window-learn-from-chat-activity =
    .label = Learn from chats in { -smart-window-brand-name }

ai-window-learn-from-browsing-activity =
    .label = Learn from browsing in Classic and { -smart-window-brand-name(plural-form: "true") }

ai-window-manage-memories-button =
    .label = Manage memories

ai-window-manage-memories-header =
    .heading = Manage memories
    .description = Memories are stored locally on this device to help protect your privacy. They refresh a few times a day while you use { -smart-window-brand-name }, so recent activity may take time to be reflected.

ai-window-no-memories =
    .label = No memories yet
    .description = As { -smart-window-brand-name } learns from your activity, you’ll see memories here.

ai-window-no-memories-learning-off =
    .label = No memories to show
    .description = Learning from activity is off, so { -smart-window-brand-name } isn’t creating memories.

ai-window-delete-all-memories-button =
    .label = Delete all

ai-window-delete-all-memories-title = Delete all memories?
# “Learn from…” refers to two different options in settings that start with "Learn from"
# (ai-window-learn-from-chat-activity and ai-window-learn-from-browsing-activity)
ai-window-delete-all-memories-message = Existing memories will be deleted. If you don’t want any new memories created, uncheck the options to “Learn from…” in { -smart-window-brand-name } settings.
ai-window-delete-all-memories-confirm = Delete
ai-window-delete-all-memories-cancel = Cancel

# Variables:
#   $label (String) - The memory summary text that will be deleted
ai-window-memory-delete-button =
    .title = Delete memory
    .aria-label = Delete { $label }
