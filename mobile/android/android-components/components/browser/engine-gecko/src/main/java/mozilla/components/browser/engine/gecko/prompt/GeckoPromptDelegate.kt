/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.engine.gecko.prompt

import android.content.Context
import android.net.Uri
import androidx.annotation.VisibleForTesting
import mozilla.components.browser.engine.gecko.GeckoEngineSession
import mozilla.components.browser.engine.gecko.ext.convertToChoices
import mozilla.components.browser.engine.gecko.ext.toAddress
import mozilla.components.browser.engine.gecko.ext.toAutocompleteAddress
import mozilla.components.browser.engine.gecko.ext.toAutocompleteCreditCard
import mozilla.components.browser.engine.gecko.ext.toCreditCardEntry
import mozilla.components.browser.engine.gecko.ext.toLoginEntry
import mozilla.components.browser.engine.gecko.prompt.ext.toLoginHint
import mozilla.components.browser.engine.gecko.prompt.ext.toSelectOption
import mozilla.components.concept.engine.prompt.Choice
import mozilla.components.concept.engine.prompt.PromptRequest
import mozilla.components.concept.engine.prompt.PromptRequest.File.Companion.DEFAULT_UPLOADS_DIR_NAME
import mozilla.components.concept.engine.prompt.PromptRequest.MenuChoice
import mozilla.components.concept.engine.prompt.PromptRequest.MultipleChoice
import mozilla.components.concept.engine.prompt.PromptRequest.SingleChoice
import mozilla.components.concept.engine.prompt.ShareData
import mozilla.components.concept.identitycredential.Account
import mozilla.components.concept.identitycredential.Provider
import mozilla.components.concept.storage.Address
import mozilla.components.concept.storage.CreditCardEntry
import mozilla.components.concept.storage.Login
import mozilla.components.concept.storage.LoginEntry
import mozilla.components.support.ktx.android.net.toFileUri
import mozilla.components.support.ktx.kotlin.ifNullOrEmpty
import mozilla.components.support.ktx.kotlin.toDate
import mozilla.components.support.utils.TimePicker.shouldShowMillisecondsPicker
import mozilla.components.support.utils.TimePicker.shouldShowSecondsPicker
import org.mozilla.geckoview.AllowOrDeny
import org.mozilla.geckoview.Autocomplete
import org.mozilla.geckoview.Autocomplete.SelectOption.Hint
import org.mozilla.geckoview.GeckoResult
import org.mozilla.geckoview.GeckoSession
import org.mozilla.geckoview.GeckoSession.PromptDelegate
import org.mozilla.geckoview.GeckoSession.PromptDelegate.AutocompleteRequest
import org.mozilla.geckoview.GeckoSession.PromptDelegate.BeforeUnloadPrompt
import org.mozilla.geckoview.GeckoSession.PromptDelegate.CertificateRequest
import org.mozilla.geckoview.GeckoSession.PromptDelegate.DateTimePrompt.Type.DATE
import org.mozilla.geckoview.GeckoSession.PromptDelegate.DateTimePrompt.Type.DATETIME_LOCAL
import org.mozilla.geckoview.GeckoSession.PromptDelegate.DateTimePrompt.Type.MONTH
import org.mozilla.geckoview.GeckoSession.PromptDelegate.DateTimePrompt.Type.TIME
import org.mozilla.geckoview.GeckoSession.PromptDelegate.DateTimePrompt.Type.WEEK
import org.mozilla.geckoview.GeckoSession.PromptDelegate.IdentityCredential.AccountSelectorPrompt
import org.mozilla.geckoview.GeckoSession.PromptDelegate.IdentityCredential.PrivacyPolicyPrompt
import org.mozilla.geckoview.GeckoSession.PromptDelegate.IdentityCredential.ProviderSelectorPrompt
import org.mozilla.geckoview.GeckoSession.PromptDelegate.PromptResponse
import org.mozilla.geckoview.GeckoSession.PromptDelegate.WebAuthnRelatedOriginPrompt
import java.security.InvalidParameterException
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

typealias GeckoAuthOptions = PromptDelegate.AuthPrompt.AuthOptions
typealias GeckoChoice = PromptDelegate.ChoicePrompt.Choice
typealias GECKO_AUTH_FLAGS = PromptDelegate.AuthPrompt.AuthOptions.Flags
typealias GECKO_AUTH_LEVEL = PromptDelegate.AuthPrompt.AuthOptions.Level
typealias GECKO_PROMPT_FILE_TYPE = PromptDelegate.FilePrompt.Type
typealias GECKO_PROMPT_PROVIDER_SELECTOR = ProviderSelectorPrompt.Provider
typealias GECKO_PROMPT_ACCOUNT_SELECTOR = AccountSelectorPrompt.Account
typealias GECKO_PROMPT_ACCOUNT_SELECTOR_PROVIDER = AccountSelectorPrompt.Provider
typealias GECKO_PROMPT_CHOICE_TYPE = PromptDelegate.ChoicePrompt.Type
typealias GECKO_PROMPT_FILE_CAPTURE = PromptDelegate.FilePrompt.Capture
typealias GECKO_PROMPT_SHARE_RESULT = PromptDelegate.SharePrompt.Result
typealias AC_AUTH_LEVEL = PromptRequest.Authentication.Level
typealias AC_AUTH_METHOD = PromptRequest.Authentication.Method
typealias AC_FILE_FACING_MODE = PromptRequest.File.FacingMode

/**
 * Gecko-based PromptDelegate implementation.
 */
@Suppress("LargeClass", "TooManyFunctions")
internal class GeckoPromptDelegate(private val geckoEngineSession: GeckoEngineSession) :
    PromptDelegate {
    override fun onSelectIdentityCredentialProvider(
        session: GeckoSession,
        prompt: ProviderSelectorPrompt,
    ): GeckoResult<PromptResponse> {
        val geckoResult = GeckoResult<PromptResponse>()

        val onConfirm: (Provider) -> Unit = { provider ->
            if (!prompt.isComplete) {
                geckoResult.complete(
                    prompt.confirm(
                        provider.id,
                    ),
                )
            }
        }

        val onDismiss: () -> Unit = {
            prompt.dismissSafely(geckoResult)
        }

        geckoEngineSession.notifyObservers {
            onPromptRequest(
                PromptRequest.IdentityCredential.SelectProvider(
                    providers = prompt.providers.map { it.toProvider() },
                    onConfirm = onConfirm,
                    onDismiss = onDismiss,
                ),
            )
        }
        return geckoResult
    }

    override fun onSelectIdentityCredentialAccount(
        session: GeckoSession,
        prompt: AccountSelectorPrompt,
    ): GeckoResult<PromptResponse> {
        val geckoResult = GeckoResult<PromptResponse>()

        val onConfirm: (Account) -> Unit = { account ->
            if (!prompt.isComplete) {
                geckoResult.complete(
                    prompt.confirm(
                        account.id,
                    ),
                )
            }
        }

        val onDismiss: () -> Unit = {
            prompt.dismissSafely(geckoResult)
        }

        geckoEngineSession.notifyObservers {
            onPromptRequest(
                PromptRequest.IdentityCredential.SelectAccount(
                    accounts = prompt.accounts.map { it.toAccount() },
                    provider = prompt.provider.let { it.toProvider() },
                    onConfirm = onConfirm,
                    onDismiss = onDismiss,
                ),
            )
        }
        return geckoResult
    }

    override fun onShowPrivacyPolicyIdentityCredential(
        session: GeckoSession,
        prompt: PrivacyPolicyPrompt,
    ): GeckoResult<PromptResponse> {
        val geckoResult = GeckoResult<PromptResponse>()

        val onConfirm: (Boolean) -> Unit = { confirmed ->
            if (!prompt.isComplete) {
                geckoResult.complete(
                    prompt.confirm(confirmed),
                )
            }
        }

        val onDismiss: () -> Unit = {
            prompt.dismissSafely(geckoResult)
        }

        geckoEngineSession.notifyObservers {
            onPromptRequest(
                PromptRequest.IdentityCredential.PrivacyPolicy(
                    privacyPolicyUrl = prompt.privacyPolicyUrl,
                    termsOfServiceUrl = prompt.termsOfServiceUrl,
                    providerDomain = prompt.providerDomain,
                    host = prompt.host,
                    icon = prompt.icon,
                    onConfirm = onConfirm,
                    onDismiss = onDismiss,
                ),
            )
        }
        return geckoResult
    }

    override fun onRequestCertificate(
        session: GeckoSession,
        request: CertificateRequest,
    ): GeckoResult<PromptResponse> {
        val geckoResult = GeckoResult<PromptResponse>()

        val onComplete: (String?) -> Unit = {
            geckoResult.complete(request.confirm(it))
        }

        geckoEngineSession.notifyObservers {
            onPromptRequest(
                PromptRequest.CertificateRequest(request.host, request.issuers, onComplete),
            )
        }

        return geckoResult
    }

    override fun onCreditCardSave(
        session: GeckoSession,
        request: AutocompleteRequest<Autocomplete.CreditCardSaveOption>,
    ): GeckoResult<PromptResponse> {
        val geckoResult = GeckoResult<PromptResponse>()

        val onConfirm: (CreditCardEntry) -> Unit = { creditCard ->
            if (!request.isComplete) {
                geckoResult.complete(
                    request.confirm(
                        Autocomplete.CreditCardSelectOption(creditCard.toAutocompleteCreditCard()),
                    ),
                )
            }
        }

        val onDismiss: () -> Unit = {
            request.dismissSafely(geckoResult)
        }

        geckoEngineSession.notifyObservers {
            onPromptRequest(
                PromptRequest.SaveCreditCard(
                    creditCard = request.options[0].value.toCreditCardEntry(),
                    onConfirm = onConfirm,
                    onDismiss = onDismiss,
                ).also {
                    request.delegate = PromptInstanceDismissDelegate(
                        geckoEngineSession,
                        it,
                    )
                },
            )
        }

        return geckoResult
    }

    /**
     * Handle a credit card selection prompt request. This is triggered by the user
     * focusing on a credit card input field.
     *
     * @param session The [GeckoSession] that triggered the request.
     * @param request The [AutocompleteRequest] containing the credit card selection request.
     */
    override fun onCreditCardSelect(
        session: GeckoSession,
        request: AutocompleteRequest<Autocomplete.CreditCardSelectOption>,
    ): GeckoResult<PromptResponse>? {
        val geckoResult = GeckoResult<PromptResponse>()

        val onConfirm: (CreditCardEntry) -> Unit = { creditCard ->
            if (!request.isComplete) {
                geckoResult.complete(
                    request.confirm(
                        Autocomplete.CreditCardSelectOption(creditCard.toAutocompleteCreditCard()),
                    ),
                )
            }
        }

        val onDismiss: () -> Unit = {
            request.dismissSafely(geckoResult)
        }

        geckoEngineSession.notifyObservers {
            onPromptRequest(
                PromptRequest.SelectCreditCard(
                    creditCards = request.options.map { it.value.toCreditCardEntry() },
                    onDismiss = onDismiss,
                    onConfirm = onConfirm,
                ),
            )
        }

        return geckoResult
    }

    override fun onLoginSave(
        session: GeckoSession,
        prompt: AutocompleteRequest<Autocomplete.LoginSaveOption>,
    ): GeckoResult<PromptResponse>? {
        val geckoResult = GeckoResult<PromptResponse>()
        val onConfirmSave: (LoginEntry) -> Unit = { entry ->
            if (!prompt.isComplete) {
                geckoResult.complete(prompt.confirm(Autocomplete.LoginSelectOption(entry.toLoginEntry())))
            }
        }
        val onDismiss: () -> Unit = {
            prompt.dismissSafely(geckoResult)
        }

        geckoEngineSession.notifyObservers {
            onPromptRequest(
                PromptRequest.SaveLoginPrompt(
                    hint = prompt.options[0].hint,
                    logins = prompt.options.map { it.value.toLoginEntry() },
                    onConfirm = onConfirmSave,
                    onDismiss = onDismiss,
                ).also {
                    prompt.delegate = PromptInstanceDismissDelegate(
                        geckoEngineSession,
                        it,
                    )
                },
            )
        }
        return geckoResult
    }

    override fun onLoginSelect(
        session: GeckoSession,
        prompt: AutocompleteRequest<Autocomplete.LoginSelectOption>,
    ): GeckoResult<PromptResponse>? {
        val promptOptions = prompt.options

        val generatedPassword = promptOptions
            .firstOrNull { option -> option.hint == Hint.GENERATED }?.value?.password

        val geckoResult = GeckoResult<PromptResponse>()
        val onConfirmSelect: (Login) -> Unit = { login ->
            if (!prompt.isComplete) {
                geckoResult.complete(
                    prompt.confirm(
                        Autocomplete.LoginSelectOption(
                            login.toLoginEntry(),
                            login.hint.toSelectOption(),
                        ),
                    ),
                )
            }
        }
        val onDismiss: () -> Unit = {
            prompt.dismissSafely(geckoResult)
        }

        val loginList = promptOptions.map { option ->
            /**
             * N.B: Even though the data structure says we need to always have a guid, the
             * LoginStorage implementation doesn't seem to care that we don't have a valid one
             * because it's generated eventually within the `crate::login::LoginDb::add`
             * in application-services.
             *
             * @see [appservices](https://searchfox.org/mozilla-mobile/rev/fc0c36b437d53e65a6d85362c9c981feaf074385/application-services/components/logins/src/db.rs#569)
             */
            val guid = option.value.guid.ifNullOrEmpty { "" }
            // `guid` plus exactly one of `httpRealm` and `formSubmitURL` must be present to be a valid login entry.
            val hasHttpRealm = !option.value.httpRealm.isNullOrBlank()
            val hasFormActionOrigin = !option.value.formActionOrigin.isNullOrBlank()
            val formActionOrigin = if (!hasHttpRealm && !hasFormActionOrigin) {
                geckoEngineSession.currentUrl
            } else {
                option.value.formActionOrigin
            }

            Login(
                guid = guid,
                origin = option.value.origin,
                formActionOrigin = formActionOrigin,
                httpRealm = option.value.httpRealm,
                username = option.value.username,
                password = option.value.password,
                hint = option.hint.toLoginHint(),
            )
        }

        geckoEngineSession.notifyObservers {
            onPromptRequest(
                PromptRequest.SelectLoginPrompt(
                    logins = loginList,
                    generatedPassword = generatedPassword,
                    onConfirm = onConfirmSelect,
                    onDismiss = onDismiss,
                ),
            )
        }
        return geckoResult
    }

    override fun onChoicePrompt(
        session: GeckoSession,
        geckoPrompt: PromptDelegate.ChoicePrompt,
    ): GeckoResult<PromptResponse>? {
        val geckoResult = GeckoResult<PromptResponse>()
        val choices = convertToChoices(geckoPrompt.choices)

        val onDismiss: () -> Unit = {
            geckoPrompt.dismissSafely(geckoResult)
        }

        val onConfirmSingleChoice: (Choice) -> Unit = { selectedChoice ->
            if (!geckoPrompt.isComplete) {
                geckoResult.complete(geckoPrompt.confirm(selectedChoice.id))
            }
        }
        val onConfirmMultipleSelection: (Array<Choice>) -> Unit = { selectedChoices ->
            if (!geckoPrompt.isComplete) {
                val ids = selectedChoices.toIdsArray()
                geckoResult.complete(geckoPrompt.confirm(ids))
            }
        }

        val promptRequest = when (geckoPrompt.type) {
            GECKO_PROMPT_CHOICE_TYPE.SINGLE -> SingleChoice(
                choices,
                onConfirmSingleChoice,
                onDismiss,
            )
            GECKO_PROMPT_CHOICE_TYPE.MENU -> MenuChoice(
                choices,
                onConfirmSingleChoice,
                onDismiss,
            )
            GECKO_PROMPT_CHOICE_TYPE.MULTIPLE -> MultipleChoice(
                choices,
                onConfirmMultipleSelection,
                onDismiss,
            )
            else -> throw InvalidParameterException("${geckoPrompt.type} is not a valid Gecko @Choice.ChoiceType")
        }

        geckoPrompt.delegate = ChoicePromptDelegate(
            geckoEngineSession,
            promptRequest,
        )

        geckoEngineSession.notifyObservers {
            onPromptRequest(promptRequest)
        }

        return geckoResult
    }

    override fun onAddressSelect(
        session: GeckoSession,
        request: AutocompleteRequest<Autocomplete.AddressSelectOption>,
    ): GeckoResult<PromptResponse> {
        val geckoResult = GeckoResult<PromptResponse>()

        val onConfirm: (Address) -> Unit = { address ->
            if (!request.isComplete) {
                geckoResult.complete(
                    request.confirm(
                        Autocomplete.AddressSelectOption(address.toAutocompleteAddress()),
                    ),
                )
            }
        }

        val onDismiss: () -> Unit = {
            request.dismissSafely(geckoResult)
        }

        geckoEngineSession.notifyObservers {
            onPromptRequest(
                PromptRequest.SelectAddress(
                    addresses = request.options.map { it.value.toAddress() },
                    onConfirm = onConfirm,
                    onDismiss = onDismiss,
                ),
            )
        }

        return geckoResult
    }

    override fun onAlertPrompt(
        session: GeckoSession,
        prompt: PromptDelegate.AlertPrompt,
    ): GeckoResult<PromptResponse> {
        val geckoResult = GeckoResult<PromptResponse>()
        val onDismiss: () -> Unit = { prompt.dismissSafely(geckoResult) }
        val onConfirm: (Boolean) -> Unit = { _ -> onDismiss() }
        val title = prompt.title ?: ""
        val message = prompt.message ?: ""

        geckoEngineSession.notifyObservers {
            onPromptRequest(
                PromptRequest.Alert(
                    title,
                    message,
                    false,
                    onConfirm,
                    onDismiss,
                ),
            )
        }
        return geckoResult
    }

    override fun onFilePrompt(
        session: GeckoSession,
        prompt: PromptDelegate.FilePrompt,
    ): GeckoResult<PromptResponse>? {
        val geckoResult = GeckoResult<PromptResponse>()

        if (prompt.type == GECKO_PROMPT_FILE_TYPE.FOLDER) {
            val onSelect: (Context, Uri) -> Unit = { context, uri ->
                if (!prompt.isComplete) {
                    geckoResult.complete(prompt.confirm(context, uri))
                }
            }
            val onDismiss: () -> Unit = {
                prompt.dismissSafely(geckoResult)
            }
            geckoEngineSession.notifyObservers {
                onPromptRequest(
                    PromptRequest.Folder(
                        onSelect,
                        onDismiss,
                    ),
                )
            }
            return geckoResult
        }

        val isMultipleFilesSelection = prompt.type == GECKO_PROMPT_FILE_TYPE.MULTIPLE

        val captureMode = when (prompt.capture) {
            GECKO_PROMPT_FILE_CAPTURE.ANY -> AC_FILE_FACING_MODE.ANY
            GECKO_PROMPT_FILE_CAPTURE.USER -> AC_FILE_FACING_MODE.FRONT_CAMERA
            GECKO_PROMPT_FILE_CAPTURE.ENVIRONMENT -> AC_FILE_FACING_MODE.BACK_CAMERA
            else -> AC_FILE_FACING_MODE.NONE
        }

        val onSelectMultiple: (Context, Array<Uri>) -> Unit = { context, uris ->
            val filesUris = uris.map {
                toFileUri(uri = it, context)
            }.toTypedArray()
            if (!prompt.isComplete) {
                geckoResult.complete(prompt.confirm(context, filesUris))
            }
        }

        val onSelectSingle: (Context, Uri) -> Unit = { context, uri ->
            if (!prompt.isComplete) {
                geckoResult.complete(prompt.confirm(context, toFileUri(uri, context)))
            }
        }

        val onDismiss: () -> Unit = {
            prompt.dismissSafely(geckoResult)
        }

        geckoEngineSession.notifyObservers {
            onPromptRequest(
                PromptRequest.File(
                    prompt.mimeTypes ?: emptyArray(),
                    isMultipleFilesSelection,
                    captureMode,
                    onSelectSingle,
                    onSelectMultiple,
                    onDismiss,
                ),
            )
        }
        return geckoResult
    }

    override fun onDateTimePrompt(
        session: GeckoSession,
        prompt: PromptDelegate.DateTimePrompt,
    ): GeckoResult<PromptResponse>? {
        val geckoResult = GeckoResult<PromptResponse>()
        val onConfirm: (String) -> Unit = {
            if (!prompt.isComplete) {
                geckoResult.complete(prompt.confirm(it))
            }
        }

        val onDismiss: () -> Unit = {
            prompt.dismissSafely(geckoResult)
        }

        val onClear: () -> Unit = {
            onConfirm("")
        }
        val initialDateString = prompt.defaultValue ?: ""
        val stepValue = with(prompt.stepValue) {
            if (this?.toDoubleOrNull() == null) {
                null
            } else {
                this
            }
        }

        val format = when (prompt.type) {
            DATE -> "yyyy-MM-dd"
            MONTH -> "yyyy-MM"
            WEEK -> "yyyy-'W'ww"
            TIME -> {
                if (shouldShowMillisecondsPicker(stepValue?.toFloat())) {
                    "HH:mm:ss.SSS"
                } else if (shouldShowSecondsPicker(stepValue?.toFloat())) {
                    "HH:mm:ss"
                } else {
                    "HH:mm"
                }
            }
            DATETIME_LOCAL -> "yyyy-MM-dd'T'HH:mm"
            else -> {
                throw InvalidParameterException("${prompt.type} is not a valid DatetimeType")
            }
        }

        notifyDatePromptRequest(
            prompt.title ?: "",
            initialDateString,
            prompt.minValue,
            prompt.maxValue,
            stepValue,
            onClear,
            format,
            onConfirm,
            onDismiss,
        )

        return geckoResult
    }

    override fun onAuthPrompt(
        session: GeckoSession,
        geckoPrompt: PromptDelegate.AuthPrompt,
    ): GeckoResult<PromptResponse>? {
        val geckoResult = GeckoResult<PromptResponse>()
        val title = geckoPrompt.title ?: ""
        val message = geckoPrompt.message ?: ""
        val uri = geckoPrompt.authOptions.uri
        val flags = geckoPrompt.authOptions.flags
        val userName = geckoPrompt.authOptions.username ?: ""
        val password = geckoPrompt.authOptions.password ?: ""
        val method =
            if (flags in GECKO_AUTH_FLAGS.HOST) AC_AUTH_METHOD.HOST else AC_AUTH_METHOD.PROXY
        val level = geckoPrompt.authOptions.toACLevel()
        val onlyShowPassword = flags in GECKO_AUTH_FLAGS.ONLY_PASSWORD
        val previousFailed = flags in GECKO_AUTH_FLAGS.PREVIOUS_FAILED
        val isCrossOrigin = flags in GECKO_AUTH_FLAGS.CROSS_ORIGIN_SUB_RESOURCE

        val onConfirm: (String, String) -> Unit =
            { user, pass ->
                if (!geckoPrompt.isComplete) {
                    if (onlyShowPassword) {
                        geckoResult.complete(geckoPrompt.confirm(pass))
                    } else {
                        geckoResult.complete(geckoPrompt.confirm(user, pass))
                    }
                }
            }

        val onDismiss: () -> Unit = { geckoPrompt.dismissSafely(geckoResult) }

        geckoEngineSession.notifyObservers {
            onPromptRequest(
                PromptRequest.Authentication(
                    uri,
                    title,
                    message,
                    userName,
                    password,
                    method,
                    level,
                    onlyShowPassword,
                    previousFailed,
                    isCrossOrigin,
                    onConfirm,
                    onDismiss,
                ),
            )
        }
        return geckoResult
    }

    override fun onTextPrompt(
        session: GeckoSession,
        prompt: PromptDelegate.TextPrompt,
    ): GeckoResult<PromptResponse>? {
        val geckoResult = GeckoResult<PromptResponse>()
        val title = prompt.title ?: ""
        val inputLabel = prompt.message ?: ""
        val inputValue = prompt.defaultValue ?: ""
        val onDismiss: () -> Unit = { prompt.dismissSafely(geckoResult) }
        val onConfirm: (Boolean, String) -> Unit = { _, valueInput ->
            if (!prompt.isComplete) {
                geckoResult.complete(prompt.confirm(valueInput))
            }
        }

        geckoEngineSession.notifyObservers {
            onPromptRequest(
                PromptRequest.TextPrompt(
                    title,
                    inputLabel,
                    inputValue,
                    false,
                    onConfirm,
                    onDismiss,
                ),
            )
        }

        return geckoResult
    }

    override fun onColorPrompt(
        session: GeckoSession,
        prompt: PromptDelegate.ColorPrompt,
    ): GeckoResult<PromptResponse>? {
        val geckoResult = GeckoResult<PromptResponse>()
        val onConfirm: (String) -> Unit = {
            if (!prompt.isComplete) {
                geckoResult.complete(prompt.confirm(it))
            }
        }
        val onDismiss: () -> Unit = { prompt.dismissSafely(geckoResult) }

        val defaultColor = prompt.defaultValue ?: ""

        geckoEngineSession.notifyObservers {
            onPromptRequest(
                PromptRequest.Color(defaultColor, onConfirm, onDismiss),
            )
        }
        return geckoResult
    }

    override fun onPopupPrompt(
        session: GeckoSession,
        prompt: PromptDelegate.PopupPrompt,
    ): GeckoResult<PromptResponse> {
        val geckoResult = GeckoResult<PromptResponse>()
        val onAllow: () -> Unit = {
            if (!prompt.isComplete) {
                geckoResult.complete(prompt.confirm(AllowOrDeny.ALLOW))
            }
        }
        val onDeny: () -> Unit = {
            if (!prompt.isComplete) {
                geckoResult.complete(prompt.confirm(AllowOrDeny.DENY))
            }
        }

        geckoEngineSession.notifyObservers {
            onPromptRequest(
                PromptRequest.Popup(prompt.targetUri ?: "", onAllow, onDeny),
            )
        }
        return geckoResult
    }

    override fun onRedirectPrompt(
        session: GeckoSession,
        prompt: PromptDelegate.RedirectPrompt,
    ): GeckoResult<PromptResponse> {
        val geckoResult = GeckoResult<PromptResponse>()
        val onAllow: () -> Unit = {
            if (!prompt.isComplete) {
                geckoResult.complete(prompt.confirm(AllowOrDeny.ALLOW))
            }
        }
        val onDeny: () -> Unit = {
            if (!prompt.isComplete) {
                geckoResult.complete(prompt.confirm(AllowOrDeny.DENY))
            }
        }

        geckoEngineSession.notifyObservers {
            onPromptRequest(
                PromptRequest.Redirect(prompt.targetUri ?: "", onAllow, onDeny),
            )
        }
        return geckoResult
    }

    override fun onBeforeUnloadPrompt(
        session: GeckoSession,
        geckoPrompt: BeforeUnloadPrompt,
    ): GeckoResult<PromptResponse>? {
        val geckoResult = GeckoResult<PromptResponse>()
        val title = geckoPrompt.title ?: ""
        val onAllow: () -> Unit = {
            if (!geckoPrompt.isComplete) {
                geckoResult.complete(geckoPrompt.confirm(AllowOrDeny.ALLOW))
            }
        }
        val onDeny: () -> Unit = {
            if (!geckoPrompt.isComplete) {
                geckoResult.complete(geckoPrompt.confirm(AllowOrDeny.DENY))
                geckoEngineSession.notifyObservers { onBeforeUnloadPromptDenied() }
            }
        }

        val onDismiss: () -> Unit = {
            geckoPrompt.dismissSafely(geckoResult)
        }

        geckoEngineSession.notifyObservers {
            onPromptRequest(PromptRequest.BeforeUnload(title, onAllow, onDeny, onDismiss))
        }

        return geckoResult
    }

    override fun onWebAuthnRelatedOriginPrompt(
        session: GeckoSession,
        geckoPrompt: WebAuthnRelatedOriginPrompt,
    ): GeckoResult<PromptResponse>? {
        val geckoResult = GeckoResult<PromptResponse>()
        val onConfirm: () -> Unit = {
            if (!geckoPrompt.isComplete) {
                geckoResult.complete(geckoPrompt.confirm(AllowOrDeny.ALLOW))
            }
        }
        val onDismiss: () -> Unit = {
            geckoPrompt.dismissSafely(geckoResult)
        }
        geckoEngineSession.notifyObservers {
            onPromptRequest(
                PromptRequest.WebAuthnRelatedOriginPrompt(
                    geckoPrompt.origin ?: "",
                    geckoPrompt.rpId ?: "",
                    geckoPrompt.isCreate,
                    onConfirm,
                    onDismiss,
                ),
            )
        }
        return geckoResult
    }

    override fun onSharePrompt(
        session: GeckoSession,
        prompt: PromptDelegate.SharePrompt,
    ): GeckoResult<PromptResponse> {
        val geckoResult = GeckoResult<PromptResponse>()
        val onSuccess = {
            if (!prompt.isComplete) {
                geckoResult.complete(prompt.confirm(GECKO_PROMPT_SHARE_RESULT.SUCCESS))
            }
        }
        val onFailure = {
            if (!prompt.isComplete) {
                geckoResult.complete(prompt.confirm(GECKO_PROMPT_SHARE_RESULT.FAILURE))
            }
        }
        val onDismiss = { prompt.dismissSafely(geckoResult) }

        geckoEngineSession.notifyObservers {
            onPromptRequest(
                PromptRequest.Share(
                    ShareData(
                        title = prompt.title,
                        text = prompt.text,
                        url = prompt.uri,
                    ),
                    onSuccess,
                    onFailure,
                    onDismiss,
                ),
            )
        }
        return geckoResult
    }

    override fun onButtonPrompt(
        session: GeckoSession,
        prompt: PromptDelegate.ButtonPrompt,
    ): GeckoResult<PromptResponse>? {
        val geckoResult = GeckoResult<PromptResponse>()
        val title = prompt.title ?: ""
        val message = prompt.message ?: ""

        val onConfirmPositiveButton: (Boolean) -> Unit = {
            if (!prompt.isComplete) {
                geckoResult.complete(prompt.confirm(PromptDelegate.ButtonPrompt.Type.POSITIVE))
            }
        }
        val onConfirmNegativeButton: (Boolean) -> Unit = {
            if (!prompt.isComplete) {
                geckoResult.complete(prompt.confirm(PromptDelegate.ButtonPrompt.Type.NEGATIVE))
            }
        }

        val onDismiss: (Boolean) -> Unit = { prompt.dismissSafely(geckoResult) }

        geckoEngineSession.notifyObservers {
            onPromptRequest(
                PromptRequest.Confirm(
                    title,
                    message,
                    false,
                    "",
                    "",
                    "",
                    onConfirmPositiveButton,
                    onConfirmNegativeButton,
                    onDismiss,
                ) {
                    onDismiss(false)
                },
            )
        }
        return geckoResult
    }

    override fun onRepostConfirmPrompt(
        session: GeckoSession,
        prompt: PromptDelegate.RepostConfirmPrompt,
    ): GeckoResult<PromptResponse>? {
        val geckoResult = GeckoResult<PromptResponse>()

        val onConfirm: () -> Unit = {
            if (!prompt.isComplete) {
                geckoResult.complete(prompt.confirm(AllowOrDeny.ALLOW))
            }
        }
        val onCancel: () -> Unit = {
            if (!prompt.isComplete) {
                geckoResult.complete(prompt.confirm(AllowOrDeny.DENY))
                geckoEngineSession.notifyObservers { onRepostPromptCancelled() }
            }
        }

        geckoEngineSession.notifyObservers {
            onPromptRequest(
                PromptRequest.Repost(
                    onConfirm,
                    onCancel,
                ),
            )
        }
        return geckoResult
    }

    override fun onFolderUploadPrompt(
        session: GeckoSession,
        prompt: PromptDelegate.FolderUploadPrompt,
    ): GeckoResult<PromptResponse>? {
        val geckoResult = GeckoResult<PromptResponse>()
        val directoryName = prompt.directoryName ?: ""

        val onConfirm: () -> Unit = {
            if (!prompt.isComplete) {
                geckoResult.complete(prompt.confirm(AllowOrDeny.ALLOW))
            }
        }
        val onCancel: () -> Unit = {
            if (!prompt.isComplete) {
                geckoResult.complete(prompt.confirm(AllowOrDeny.DENY))
            }
        }

        geckoEngineSession.notifyObservers {
            onPromptRequest(PromptRequest.FolderUploadPrompt(directoryName, onConfirm, onCancel))
        }
        return geckoResult
    }

    @Suppress("LongParameterList")
    private fun notifyDatePromptRequest(
        title: String,
        initialDateString: String,
        minDateString: String?,
        maxDateString: String?,
        stepValue: String?,
        onClear: () -> Unit,
        format: String,
        onConfirm: (String) -> Unit,
        onDismiss: () -> Unit,
    ) {
        val initialDate = initialDateString.toDate(format)
        val minDate = if (minDateString.isNullOrEmpty()) null else minDateString.toDate()
        val maxDate = if (maxDateString.isNullOrEmpty()) null else maxDateString.toDate()
        val onSelect: (Date) -> Unit = {
            val stringDate = it.toString(format)
            onConfirm(stringDate)
        }

        val selectionType = when (format) {
            "HH:mm", "HH:mm:ss", "HH:mm:ss.SSS" -> PromptRequest.TimeSelection.Type.TIME
            "yyyy-MM" -> PromptRequest.TimeSelection.Type.MONTH
            "yyyy-MM-dd'T'HH:mm" -> PromptRequest.TimeSelection.Type.DATE_AND_TIME
            else -> PromptRequest.TimeSelection.Type.DATE
        }

        geckoEngineSession.notifyObservers {
            onPromptRequest(
                PromptRequest.TimeSelection(
                    title,
                    initialDate,
                    minDate,
                    maxDate,
                    stepValue,
                    selectionType,
                    onSelect,
                    onClear,
                    onDismiss,
                ),
            )
        }
    }

    private fun GeckoAuthOptions.toACLevel(): AC_AUTH_LEVEL {
        return when (level) {
            GECKO_AUTH_LEVEL.NONE -> AC_AUTH_LEVEL.NONE
            GECKO_AUTH_LEVEL.PW_ENCRYPTED -> AC_AUTH_LEVEL.PASSWORD_ENCRYPTED
            GECKO_AUTH_LEVEL.SECURE -> AC_AUTH_LEVEL.SECURED
            else -> {
                AC_AUTH_LEVEL.NONE
            }
        }
    }

    private operator fun Int.contains(mask: Int): Boolean {
        return (this and mask) != 0
    }

    @VisibleForTesting
    internal fun toFileUri(uri: Uri, context: Context): Uri {
        return uri.toFileUri(context, dirToCopy = DEFAULT_UPLOADS_DIR_NAME)
    }
}

@VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
internal fun Array<Choice>.toIdsArray(): Array<String> {
    return this.map { it.id }.toTypedArray()
}

@VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
internal fun Date.toString(format: String): String {
    val formatter = SimpleDateFormat(format, Locale.ROOT)
    return formatter.format(this) ?: ""
}

/**
 * Only dismiss if the prompt is not already dismissed.
 */
@VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
internal fun PromptDelegate.BasePrompt.dismissSafely(geckoResult: GeckoResult<PromptResponse>) {
    if (!this.isComplete) {
        geckoResult.complete(dismiss())
    }
}

@VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
internal fun GECKO_PROMPT_PROVIDER_SELECTOR.toProvider(): Provider {
    return Provider(id, icon, name, domain)
}

@VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
internal fun GECKO_PROMPT_ACCOUNT_SELECTOR.toAccount(): Account {
    return Account(id, email, name, icon)
}

@VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
internal fun GECKO_PROMPT_ACCOUNT_SELECTOR_PROVIDER.toProvider(): Provider {
    return Provider(0, icon, name, domain)
}
