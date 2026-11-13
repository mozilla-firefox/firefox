/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.prompts.login

import mozilla.components.browser.state.action.ContentAction
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.prompt.PromptRequest
import mozilla.components.concept.storage.Login
import mozilla.components.feature.prompts.concept.AutocompletePrompt
import mozilla.components.feature.prompts.concept.SelectablePromptView
import mozilla.components.feature.prompts.consumePromptFrom
import mozilla.components.feature.prompts.facts.emitLoginAutofillDismissedFact
import mozilla.components.feature.prompts.facts.emitLoginAutofillPerformedFact
import mozilla.components.feature.prompts.facts.emitLoginAutofillShownFact
import mozilla.components.support.base.log.logger.Logger

/**
 * The [LoginPicker] displays a list of possible logins in a [SelectablePromptView] for a site after receiving a
 * [PromptRequest.SelectLoginPrompt] when a user clicks into a login field and we have matching logins. It allows the
 * user to select which one of these logins they would like to fill, or select an option to manage their logins.
 *
 * @property store The [BrowserStore] this feature should subscribe to.
 * @property loginSelectBar The [AutocompletePrompt] view into which the select login "prompt" will be inflated.
 * @property manageLoginsCallback A callback invoked when a user selects "manage logins" from the select login prompt.
 * @property sessionId This is the id of the session which requested the prompt.
 * @property selectLoginCallback A callback invoked when a user selects a login, before it is filled. See
 *   [LoginDelegate.onSelectLogin].
 */
internal class LoginPicker(
    private val store: BrowserStore,
    private val loginSelectBar: AutocompletePrompt<Login>,
    private val manageLoginsCallback: () -> Unit = {},
    private var sessionId: String? = null,
    private val selectLoginCallback: (onConfirm: () -> Unit, onDismiss: () -> Unit) -> Unit = { onConfirm, _ ->
        onConfirm()
    },
) : SelectablePromptView.Listener<Login> {

    init {
        loginSelectBar.selectablePromptListener = this
    }

    internal fun handleSelectLoginRequest(request: PromptRequest.SelectLoginPrompt) {
        emitLoginAutofillShownFact()
        loginSelectBar.showPrompt()
        loginSelectBar.populate(request.logins)
    }

    override fun onOptionSelect(option: Login) {
        loginSelectBar.hidePrompt()
        selectLoginCallback(
            {
                store.consumePromptFrom<PromptRequest.SelectLoginPrompt>(sessionId) {
                    it.onConfirm(option)
                }
                emitLoginAutofillPerformedFact()
            },
            { dismissCurrentLoginSelect() },
        )
    }

    override fun onManageOptions() {
        manageLoginsCallback.invoke()
        dismissCurrentLoginSelect()
    }

    @Suppress("TooGenericExceptionCaught")
    fun dismissCurrentLoginSelect(promptRequest: PromptRequest.SelectLoginPrompt? = null) {
        try {
            if (promptRequest != null) {
                promptRequest.onDismiss()
                sessionId?.let {
                    store.dispatch(ContentAction.ConsumePromptRequestAction(it, promptRequest))
                }
                loginSelectBar.hidePrompt()
                return
            }

            store.consumePromptFrom<PromptRequest.SelectLoginPrompt>(sessionId) {
                it.onDismiss()
            }
        } catch (e: RuntimeException) {
            Logger.error("Can't dismiss this login select prompt", e)
        }
        emitLoginAutofillDismissedFact()
        loginSelectBar.hidePrompt()
    }
}
