/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.prompts

import android.app.Activity
import android.content.Intent
import android.net.Uri
import androidx.annotation.VisibleForTesting
import androidx.annotation.VisibleForTesting.Companion.PRIVATE
import androidx.fragment.app.DialogFragment
import androidx.fragment.app.Fragment
import androidx.fragment.app.FragmentManager
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.map
import mozilla.components.browser.state.action.ContentAction
import mozilla.components.browser.state.selector.findTabOrCustomTab
import mozilla.components.browser.state.selector.findTabOrCustomTabOrSelectedTab
import mozilla.components.browser.state.selector.selectedTab
import mozilla.components.browser.state.state.ContentState
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.prompt.Choice
import mozilla.components.concept.engine.prompt.PromptRequest
import mozilla.components.concept.engine.prompt.PromptRequest.Alert
import mozilla.components.concept.engine.prompt.PromptRequest.Authentication
import mozilla.components.concept.engine.prompt.PromptRequest.BeforeUnload
import mozilla.components.concept.engine.prompt.PromptRequest.CertificateRequest
import mozilla.components.concept.engine.prompt.PromptRequest.Color
import mozilla.components.concept.engine.prompt.PromptRequest.Confirm
import mozilla.components.concept.engine.prompt.PromptRequest.Dismissible
import mozilla.components.concept.engine.prompt.PromptRequest.File
import mozilla.components.concept.engine.prompt.PromptRequest.Folder
import mozilla.components.concept.engine.prompt.PromptRequest.FolderUploadPrompt
import mozilla.components.concept.engine.prompt.PromptRequest.MenuChoice
import mozilla.components.concept.engine.prompt.PromptRequest.MultipleChoice
import mozilla.components.concept.engine.prompt.PromptRequest.Popup
import mozilla.components.concept.engine.prompt.PromptRequest.Redirect
import mozilla.components.concept.engine.prompt.PromptRequest.Repost
import mozilla.components.concept.engine.prompt.PromptRequest.SaveCreditCard
import mozilla.components.concept.engine.prompt.PromptRequest.SaveLoginPrompt
import mozilla.components.concept.engine.prompt.PromptRequest.SelectAddress
import mozilla.components.concept.engine.prompt.PromptRequest.SelectCreditCard
import mozilla.components.concept.engine.prompt.PromptRequest.SelectLoginPrompt
import mozilla.components.concept.engine.prompt.PromptRequest.Share
import mozilla.components.concept.engine.prompt.PromptRequest.SingleChoice
import mozilla.components.concept.engine.prompt.PromptRequest.TextPrompt
import mozilla.components.concept.engine.prompt.PromptRequest.TimeSelection
import mozilla.components.concept.engine.prompt.PromptRequest.WebAuthnRelatedOriginPrompt
import mozilla.components.concept.identitycredential.Account
import mozilla.components.concept.identitycredential.Provider
import mozilla.components.concept.storage.CreditCardEntry
import mozilla.components.concept.storage.CreditCardValidationDelegate
import mozilla.components.concept.storage.Login
import mozilla.components.concept.storage.LoginEntry
import mozilla.components.concept.storage.LoginHint
import mozilla.components.concept.storage.LoginValidationDelegate
import mozilla.components.feature.prompts.address.AddressDelegate
import mozilla.components.feature.prompts.address.AddressPicker
import mozilla.components.feature.prompts.address.DefaultAddressDelegate
import mozilla.components.feature.prompts.certificate.CertificatePicker
import mozilla.components.feature.prompts.creditcard.CreditCardDelegate
import mozilla.components.feature.prompts.creditcard.CreditCardPicker
import mozilla.components.feature.prompts.creditcard.CreditCardSaveDialogFragment
import mozilla.components.feature.prompts.dialog.AlertDialogFragment
import mozilla.components.feature.prompts.dialog.AuthenticationDialogFragment
import mozilla.components.feature.prompts.dialog.ChoiceDialogFragment
import mozilla.components.feature.prompts.dialog.ChoiceDialogFragment.Companion.MENU_CHOICE_DIALOG_TYPE
import mozilla.components.feature.prompts.dialog.ChoiceDialogFragment.Companion.MULTIPLE_CHOICE_DIALOG_TYPE
import mozilla.components.feature.prompts.dialog.ChoiceDialogFragment.Companion.SINGLE_CHOICE_DIALOG_TYPE
import mozilla.components.feature.prompts.dialog.ColorPickerDialogFragment
import mozilla.components.feature.prompts.dialog.ConfirmDialogFragment
import mozilla.components.feature.prompts.dialog.MultiButtonDialogFragment
import mozilla.components.feature.prompts.dialog.PromptDialogFragment
import mozilla.components.feature.prompts.dialog.Prompter
import mozilla.components.feature.prompts.dialog.SaveLoginDialogFragment
import mozilla.components.feature.prompts.dialog.TextPromptDialogFragment
import mozilla.components.feature.prompts.dialog.TimePickerDialogFragment
import mozilla.components.feature.prompts.dialog.WebAuthnRelatedOriginDialogFragment
import mozilla.components.feature.prompts.dialog.emitGeneratedPasswordShownFact
import mozilla.components.feature.prompts.emailmask.EmailMaskDelegate
import mozilla.components.feature.prompts.emailmask.EmailMaskPromptViewListener
import mozilla.components.feature.prompts.ext.executeIfWindowedPrompt
import mozilla.components.feature.prompts.facts.emitCreditCardSaveShownFact
import mozilla.components.feature.prompts.facts.emitPromptConfirmedFact
import mozilla.components.feature.prompts.facts.emitPromptDismissedFact
import mozilla.components.feature.prompts.facts.emitPromptDisplayedFact
import mozilla.components.feature.prompts.facts.emitSuccessfulAddressAutofillFormDetectedFact
import mozilla.components.feature.prompts.facts.emitSuccessfulCreditCardAutofillFormDetectedFact
import mozilla.components.feature.prompts.file.AndroidPhotoPicker
import mozilla.components.feature.prompts.file.FilePicker
import mozilla.components.feature.prompts.file.FileUploadsDirCleaner
import mozilla.components.feature.prompts.identitycredential.DialogColors
import mozilla.components.feature.prompts.identitycredential.DialogColorsProvider
import mozilla.components.feature.prompts.identitycredential.PrivacyPolicyDialogFragment
import mozilla.components.feature.prompts.identitycredential.SelectAccountDialogFragment
import mozilla.components.feature.prompts.identitycredential.SelectProviderDialogFragment
import mozilla.components.feature.prompts.login.LoginDelegate
import mozilla.components.feature.prompts.login.LoginExceptions
import mozilla.components.feature.prompts.login.LoginPicker
import mozilla.components.feature.prompts.login.PasswordGeneratorDialogColors
import mozilla.components.feature.prompts.login.PasswordGeneratorDialogColorsProvider
import mozilla.components.feature.prompts.login.PasswordGeneratorDialogFragment
import mozilla.components.feature.prompts.login.StrongPasswordPromptViewListener
import mozilla.components.feature.prompts.login.SuggestStrongPasswordDelegate
import mozilla.components.feature.prompts.share.DefaultShareDelegate
import mozilla.components.feature.prompts.share.ShareDelegate
import mozilla.components.feature.session.SessionUseCases
import mozilla.components.feature.session.SessionUseCases.ExitFullScreenUseCase
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.lib.state.ext.flowScoped
import mozilla.components.support.base.feature.ActivityResultHandler
import mozilla.components.support.base.feature.LifecycleAwareFeature
import mozilla.components.support.base.feature.OnNeedToRequestPermissions
import mozilla.components.support.base.feature.PermissionsFeature
import mozilla.components.support.base.feature.UserInteractionHandler
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.ktx.kotlin.ifNullOrEmpty
import mozilla.components.support.ktx.kotlin.tryGetHostFromUrl
import mozilla.components.support.ktx.kotlinx.coroutines.flow.ifAnyChanged
import mozilla.components.support.ktx.util.PromptAbuserDetector
import java.lang.ref.WeakReference
import java.security.InvalidParameterException
import java.util.Collections
import java.util.Date
import java.util.WeakHashMap

@VisibleForTesting(otherwise = PRIVATE)
internal const val FRAGMENT_TAG = "mozac_feature_prompt_dialog"

/**
 * Feature for displaying native dialogs for html elements like: input type
 * date, file, time, color, option, menu, authentication, confirmation and alerts.
 *
 * There are some requests that are handled with intents instead of dialogs,
 * like file choosers and others. For this reason, you have to keep the feature
 * aware of the flow of requesting data from other apps, overriding
 * onActivityResult in your [Activity] or [Fragment] and forward its calls
 * to [onActivityResult].
 *
 * This feature will subscribe to the currently selected session and display
 * a suitable native dialog based on the [SessionState.content] state
 * [ContentState.promptRequests] events.
 * Once the dialog is closed or the user selects an item from the dialog
 * the related [PromptRequest] will be consumed.
 *
 * @property container The [Activity] or [Fragment] which hosts this feature.
 * @property store The [BrowserStore] this feature should subscribe to.
 * @property customTabId Optional id of a custom tab. Instead of showing context
 * menus for the currently selected tab this feature will show only context menus
 * for this custom tab if an id is provided.
 * @property fragmentManager The [FragmentManager] to be used when displaying
 * a dialog (fragment).
 * @property shareDelegate Delegate used to display share sheet.
 * @property exitFullscreenUsecase Usecase allowing to exit browser tabs' fullscreen mode.
 * @property isLoginAutofillEnabled A callback invoked before an autofill prompt is triggered. If false,
 * 'autofill login' prompts will not be shown.
 * @property isSaveLoginEnabled A callback invoked when a login prompt is triggered. If false,
 * 'save login' prompts will not be shown.
 * @property isCreditCardAutofillEnabled A callback invoked when credit card fields are detected in the webpage.
 * If this resolves to `true` a prompt allowing the user to select the credit card details to be autocompleted
 * will be shown.
 * @property isAddressAutofillEnabled A callback invoked when address fields are detected in the webpage.
 * If this resolves to `true` a prompt allowing the user to select the address details to be autocompleted
 * will be shown.
 * @property loginExceptionStorage An implementation of [LoginExceptions] that saves and checks origins
 * the user does not want to see a save login dialog for.
 * @property loginDelegate Delegate for login picker.
 * @property suggestStrongPasswordDelegate Delegate for strong password generator.
 * @property emailMaskDelegate Delegate for email mask.
 * @property isSuggestEmailMaskEnabled A callback invoked before an email mask prompt is triggered.
 * If false, 'Use email mask' prompt will not be shown.
 * @property isEmailMaskFeatureEnabled A callback invoked before an email mask prompt is triggered,
 * which is determined by Nimbus. If false, 'Use email mask' prompt will not be shown.
 * @property onSaveLoginWithStrongPassword A callback invoked to save a new login that uses the
 * generated strong password
 * @property shouldAutomaticallyShowSuggestedPassword A callback invoked to check whether the user
 * is engaging with signup for the first time.
 * @property onFirstTimeEngagedWithSignup A callback invoked when user is engaged with signup for
 * the first time.
 * @property creditCardDelegate Delegate for credit card picker.
 * @property addressDelegate Delegate for address picker.
 * @property fileUploadsDirCleaner a [FileUploadsDirCleaner] to clean up temporary file uploads.
 * @property onNeedToRequestPermissions A callback invoked when permissions
 * need to be requested before a prompt (e.g. a file picker) can be displayed.
 * Once the request is completed, [onPermissionsResult] needs to be invoked.
 */
@Suppress("LargeClass", "LongParameterList")
class PromptFeature private constructor(
    private val container: PromptContainer,
    private val store: BrowserStore,
    private var customTabId: String?,
    private val fragmentManager: FragmentManager,
    private val mainDispatcher: CoroutineDispatcher,
    private val identityCredentialColorsProvider: DialogColorsProvider = DialogColorsProvider {
        DialogColors.default()
    },
    private val tabsUseCases: TabsUseCases,
    private val shareDelegate: ShareDelegate,
    private val exitFullscreenUsecase: ExitFullScreenUseCase = SessionUseCases(store).exitFullscreen,
    override val creditCardValidationDelegate: CreditCardValidationDelegate? = null,
    override val loginValidationDelegate: LoginValidationDelegate? = null,
    private val isLoginAutofillEnabled: () -> Boolean = { false },
    private val isSaveLoginEnabled: () -> Boolean = { false },
    private val isCreditCardAutofillEnabled: () -> Boolean = { false },
    private val isAddressAutofillEnabled: () -> Boolean = { false },
    override val loginExceptionStorage: LoginExceptions? = null,
    private val loginDelegate: LoginDelegate = object : LoginDelegate {},
    private val suggestStrongPasswordDelegate: SuggestStrongPasswordDelegate = object :
        SuggestStrongPasswordDelegate {},
    private val emailMaskDelegate: EmailMaskDelegate? = null,
    private val isSuggestEmailMaskEnabled: () -> Boolean,
    private val isEmailMaskFeatureEnabled: () -> Boolean,
    private var shouldAutomaticallyShowSuggestedPassword: () -> Boolean = { false },
    private val onFirstTimeEngagedWithSignup: () -> Unit = {},
    private val onSaveLoginWithStrongPassword: (String, String) -> Unit = { _, _ -> },
    private val onSaveLogin: (Boolean) -> Unit = { _ -> },
    private val passwordGeneratorColorsProvider: PasswordGeneratorDialogColorsProvider =
        PasswordGeneratorDialogColorsProvider { PasswordGeneratorDialogColors.default() },
    private val hideUpdateFragmentAfterSavingGeneratedPassword: (String, String) -> Boolean = { _, _ -> true },
    private val removeLastSavedGeneratedPassword: () -> Unit = {},
    private val creditCardDelegate: CreditCardDelegate = object : CreditCardDelegate {},
    private val addressDelegate: AddressDelegate = DefaultAddressDelegate(),
    private val fileUploadsDirCleaner: FileUploadsDirCleaner,
    onNeedToRequestPermissions: OnNeedToRequestPermissions,
    androidPhotoPicker: AndroidPhotoPicker?,
) : LifecycleAwareFeature,
    PermissionsFeature,
    Prompter,
    ActivityResultHandler,
    UserInteractionHandler {
    // These three scopes have identical lifetimes. We do not yet have a way of combining scopes
    private var handlePromptScope: CoroutineScope? = null
    private var dismissPromptScope: CoroutineScope? = null

    @VisibleForTesting
    var activePromptRequest: PromptRequest? = null

    internal val promptAbuserDetector = PromptAbuserDetector()
    private val logger = Logger("PromptFeature")

    @VisibleForTesting(otherwise = PRIVATE)
    internal var activePrompt: WeakReference<PromptDialogFragment>? = null

    // This set of weak references of fragments is only used for dismissing all prompts on navigation.
    // For all other code only `activePrompt` is tracked for now.
    @VisibleForTesting(otherwise = PRIVATE)
    internal val activePromptsToDismiss =
        Collections.newSetFromMap(WeakHashMap<PromptDialogFragment, Boolean>())

    @VisibleForTesting(otherwise = PRIVATE)
    internal var previousPromptRequest: PromptRequest? = null
    private var lastPromptRequest: PromptRequest? = null

    // boolean that becomes true when the user chooses not to use the strong generated password
    private var dontUseStrongSuggestedPassword: Boolean = false

    constructor(
        activity: Activity,
        store: BrowserStore,
        customTabId: String? = null,
        fragmentManager: FragmentManager,
        mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
        tabsUseCases: TabsUseCases,
        identityCredentialColorsProvider: DialogColorsProvider = DialogColorsProvider { DialogColors.default() },
        shareDelegate: ShareDelegate = DefaultShareDelegate(),
        exitFullscreenUsecase: ExitFullScreenUseCase = SessionUseCases(store).exitFullscreen,
        creditCardValidationDelegate: CreditCardValidationDelegate? = null,
        loginValidationDelegate: LoginValidationDelegate? = null,
        isLoginAutofillEnabled: () -> Boolean = { false },
        isSaveLoginEnabled: () -> Boolean = { false },
        isCreditCardAutofillEnabled: () -> Boolean = { false },
        isAddressAutofillEnabled: () -> Boolean = { false },
        loginExceptionStorage: LoginExceptions? = null,
        loginDelegate: LoginDelegate = object : LoginDelegate {},
        suggestStrongPasswordDelegate: SuggestStrongPasswordDelegate = object :
            SuggestStrongPasswordDelegate {},
        emailMaskDelegate: EmailMaskDelegate? = null,
        isSuggestEmailMaskEnabled: () -> Boolean,
        isEmailMaskFeatureEnabled: () -> Boolean,
        shouldAutomaticallyShowSuggestedPassword: () -> Boolean = { false },
        onFirstTimeEngagedWithSignup: () -> Unit = {},
        onSaveLoginWithStrongPassword: (String, String) -> Unit = { _, _ -> },
        onSaveLogin: (Boolean) -> Unit = { _ -> },
        passwordGeneratorColorsProvider: PasswordGeneratorDialogColorsProvider =
            PasswordGeneratorDialogColorsProvider { PasswordGeneratorDialogColors.default() },
        hideUpdateFragmentAfterSavingGeneratedPassword: (String, String) -> Boolean = { _, _ -> true },
        removeLastSavedGeneratedPassword: () -> Unit = {},
        creditCardDelegate: CreditCardDelegate = object : CreditCardDelegate {},
        addressDelegate: AddressDelegate = DefaultAddressDelegate(),
        fileUploadsDirCleaner: FileUploadsDirCleaner,
        onNeedToRequestPermissions: OnNeedToRequestPermissions,
        androidPhotoPicker: AndroidPhotoPicker? = null,
    ) : this(
        container = PromptContainer.Activity(activity),
        store = store,
        customTabId = customTabId,
        fragmentManager = fragmentManager,
        identityCredentialColorsProvider = identityCredentialColorsProvider,
        tabsUseCases = tabsUseCases,
        shareDelegate = shareDelegate,
        exitFullscreenUsecase = exitFullscreenUsecase,
        creditCardValidationDelegate = creditCardValidationDelegate,
        loginValidationDelegate = loginValidationDelegate,
        isLoginAutofillEnabled = isLoginAutofillEnabled,
        isSaveLoginEnabled = isSaveLoginEnabled,
        isCreditCardAutofillEnabled = isCreditCardAutofillEnabled,
        isAddressAutofillEnabled = isAddressAutofillEnabled,
        loginExceptionStorage = loginExceptionStorage,
        loginDelegate = loginDelegate,
        suggestStrongPasswordDelegate = suggestStrongPasswordDelegate,
        emailMaskDelegate = emailMaskDelegate,
        isSuggestEmailMaskEnabled = isSuggestEmailMaskEnabled,
        isEmailMaskFeatureEnabled = isEmailMaskFeatureEnabled,
        shouldAutomaticallyShowSuggestedPassword = shouldAutomaticallyShowSuggestedPassword,
        onFirstTimeEngagedWithSignup = onFirstTimeEngagedWithSignup,
        onSaveLoginWithStrongPassword = onSaveLoginWithStrongPassword,
        onSaveLogin = onSaveLogin,
        passwordGeneratorColorsProvider = passwordGeneratorColorsProvider,
        hideUpdateFragmentAfterSavingGeneratedPassword = hideUpdateFragmentAfterSavingGeneratedPassword,
        removeLastSavedGeneratedPassword = removeLastSavedGeneratedPassword,
        creditCardDelegate = creditCardDelegate,
        addressDelegate = addressDelegate,
        fileUploadsDirCleaner = fileUploadsDirCleaner,
        onNeedToRequestPermissions = onNeedToRequestPermissions,
        androidPhotoPicker = androidPhotoPicker,
        mainDispatcher = mainDispatcher,
    )

    constructor(
        fragment: Fragment,
        store: BrowserStore,
        customTabId: String? = null,
        fragmentManager: FragmentManager,
        mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
        tabsUseCases: TabsUseCases,
        shareDelegate: ShareDelegate = DefaultShareDelegate(),
        exitFullscreenUsecase: ExitFullScreenUseCase = SessionUseCases(store).exitFullscreen,
        creditCardValidationDelegate: CreditCardValidationDelegate? = null,
        loginValidationDelegate: LoginValidationDelegate? = null,
        isLoginAutofillEnabled: () -> Boolean = { false },
        isSaveLoginEnabled: () -> Boolean = { false },
        isCreditCardAutofillEnabled: () -> Boolean = { false },
        isAddressAutofillEnabled: () -> Boolean = { false },
        loginExceptionStorage: LoginExceptions? = null,
        loginDelegate: LoginDelegate = object : LoginDelegate {},
        suggestStrongPasswordDelegate: SuggestStrongPasswordDelegate = object :
            SuggestStrongPasswordDelegate {},
        emailMaskDelegate: EmailMaskDelegate? = null,
        isSuggestEmailMaskEnabled: () -> Boolean = { false },
        isEmailMaskFeatureEnabled: () -> Boolean = { false },
        shouldAutomaticallyShowSuggestedPassword: () -> Boolean = { false },
        onFirstTimeEngagedWithSignup: () -> Unit = {},
        onSaveLoginWithStrongPassword: (String, String) -> Unit = { _, _ -> },
        onSaveLogin: (Boolean) -> Unit = { _ -> },
        hideUpdateFragmentAfterSavingGeneratedPassword: (String, String) -> Boolean = { _, _ -> true },
        removeLastSavedGeneratedPassword: () -> Unit = {},
        creditCardDelegate: CreditCardDelegate = object : CreditCardDelegate {},
        addressDelegate: AddressDelegate = DefaultAddressDelegate(),
        fileUploadsDirCleaner: FileUploadsDirCleaner,
        androidPhotoPicker: AndroidPhotoPicker? = null,
        onNeedToRequestPermissions: OnNeedToRequestPermissions,
    ) : this(
        container = PromptContainer.Fragment(fragment),
        store = store,
        customTabId = customTabId,
        fragmentManager = fragmentManager,
        tabsUseCases = tabsUseCases,
        shareDelegate = shareDelegate,
        exitFullscreenUsecase = exitFullscreenUsecase,
        creditCardValidationDelegate = creditCardValidationDelegate,
        loginValidationDelegate = loginValidationDelegate,
        isLoginAutofillEnabled = isLoginAutofillEnabled,
        isSaveLoginEnabled = isSaveLoginEnabled,
        isCreditCardAutofillEnabled = isCreditCardAutofillEnabled,
        isAddressAutofillEnabled = isAddressAutofillEnabled,
        loginExceptionStorage = loginExceptionStorage,
        loginDelegate = loginDelegate,
        suggestStrongPasswordDelegate = suggestStrongPasswordDelegate,
        emailMaskDelegate = emailMaskDelegate,
        isSuggestEmailMaskEnabled = isSuggestEmailMaskEnabled,
        isEmailMaskFeatureEnabled = isEmailMaskFeatureEnabled,
        shouldAutomaticallyShowSuggestedPassword = shouldAutomaticallyShowSuggestedPassword,
        onFirstTimeEngagedWithSignup = onFirstTimeEngagedWithSignup,
        onSaveLoginWithStrongPassword = onSaveLoginWithStrongPassword,
        onSaveLogin = onSaveLogin,
        hideUpdateFragmentAfterSavingGeneratedPassword = hideUpdateFragmentAfterSavingGeneratedPassword,
        removeLastSavedGeneratedPassword = removeLastSavedGeneratedPassword,
        creditCardDelegate = creditCardDelegate,
        addressDelegate = addressDelegate,
        fileUploadsDirCleaner = fileUploadsDirCleaner,
        onNeedToRequestPermissions = onNeedToRequestPermissions,
        androidPhotoPicker = androidPhotoPicker,
        mainDispatcher = mainDispatcher,
    )

    @VisibleForTesting
    // var for testing purposes
    internal var filePicker = FilePicker(
        container,
        store,
        customTabId,
        fileUploadsDirCleaner,
        androidPhotoPicker,
        onNeedToRequestPermissions,
    )

    @VisibleForTesting(otherwise = PRIVATE)
    internal var certificatePicker = CertificatePicker(container)

    @VisibleForTesting(otherwise = PRIVATE)
    internal var loginPicker =
        with(loginDelegate) {
            loginPickerView?.let {
                LoginPicker(store, it, onManageLogins, customTabId)
            }
        }

    @VisibleForTesting(otherwise = PRIVATE)
    internal var strongPasswordPromptViewListener =
        with(suggestStrongPasswordDelegate) {
            strongPasswordPromptViewListenerView?.let {
                StrongPasswordPromptViewListener(store, it, customTabId)
            }
        }

    private var emailMaskPromptViewListener = emailMaskDelegate?.let { delegate ->
        delegate.emailMaskPromptViewListenerView?.let { view ->
            EmailMaskPromptViewListener(
                browserStore = store,
                emailMaskBar = view,
                emailMaskDelegate = delegate,
                sessionId = customTabId,
            )
        }
    }

    @VisibleForTesting(otherwise = PRIVATE)
    internal var creditCardPicker =
        with(creditCardDelegate) {
            creditCardPickerView?.let {
                CreditCardPicker(
                    store = store,
                    creditCardSelectBar = it,
                    manageCreditCardsCallback = onManageCreditCards,
                    selectCreditCardCallback = onSelectCreditCard,
                    sessionId = customTabId,
                )
            }
        }

    @VisibleForTesting(otherwise = PRIVATE)
    internal var addressPicker =
        with(addressDelegate) {
            addressPickerView?.let {
                AddressPicker(
                    store = store,
                    addressSelectBar = it,
                    onManageAddresses = onManageAddresses,
                    sessionId = customTabId,
                )
            }
        }

    override val onNeedToRequestPermissions
        get() = filePicker.onNeedToRequestPermissions

    override fun onOpenLink(url: String) {
        tabsUseCases.addTab(
            url = url,
        )
    }

    /**
     * Starts observing the selected session to listen for prompt requests
     * and displays a dialog when needed.
     */
    @Suppress("CognitiveComplexMethod", "LongMethod", "CyclomaticComplexMethod")
    override fun start() {
        promptAbuserDetector.resetJSAlertAbuseState()

        handlePromptScope = store.flowScoped(dispatcher = mainDispatcher) { flow ->
            flow.map { state -> state.findTabOrCustomTabOrSelectedTab(customTabId) }
                .ifAnyChanged {
                    arrayOf(it?.content?.promptRequests, it?.content?.loading)
                }
                .collect { state ->
                    state?.content?.let { content ->
                        if (content.promptRequests.lastOrNull() != activePromptRequest) {
                            // Dismiss any active select login or credit card prompt if it does
                            // not match the current prompt request for the session.
                            when (activePromptRequest) {
                                is SelectLoginPrompt -> {
                                    loginPicker?.dismissCurrentLoginSelect(activePromptRequest as SelectLoginPrompt)
                                    strongPasswordPromptViewListener?.dismissCurrentSuggestStrongPassword(
                                        activePromptRequest as SelectLoginPrompt,
                                    )
                                    emailMaskPromptViewListener?.dismissCurrentEmailMaskPrompt(
                                        activePromptRequest as SelectLoginPrompt,
                                    )
                                }

                                is SaveLoginPrompt -> {
                                    (activePrompt?.get() as? SaveLoginDialogFragment)?.dismissAllowingStateLoss()
                                }

                                is SaveCreditCard -> {
                                    (activePrompt?.get() as? CreditCardSaveDialogFragment)?.dismissAllowingStateLoss()
                                }

                                is SelectCreditCard -> {
                                    creditCardPicker?.dismissSelectCreditCardRequest(
                                        activePromptRequest as SelectCreditCard,
                                    )
                                }

                                is SelectAddress -> {
                                    addressPicker?.dismissSelectAddressRequest(
                                        activePromptRequest as SelectAddress,
                                    )
                                }

                                is SingleChoice,
                                is MultipleChoice,
                                is MenuChoice,
                                -> {
                                    (activePrompt?.get() as? ChoiceDialogFragment)?.let { dialog ->
                                        if (dialog.isStateSaved) {
                                            dialog.dismissAllowingStateLoss()
                                        } else {
                                            activePromptsToDismiss.remove(dialog)
                                            activePrompt?.clear()
                                        }
                                    }
                                }

                                else -> {
                                    // no-op
                                }
                            }

                            onPromptRequested(state)
                        } else if (!content.loading) {
                            promptAbuserDetector.resetJSAlertAbuseState()
                        } else {
                            dismissSelectPrompts()
                        }

                        activePromptRequest = content.promptRequests.lastOrNull()
                    }
                }
        }

        // Dismiss all prompts when page host or session id changes. See Fenix#5326
        dismissPromptScope = store.flowScoped(dispatcher = mainDispatcher) { flow ->
            flow.ifAnyChanged { state ->
                arrayOf(
                    state.selectedTabId,
                    state.findTabOrCustomTabOrSelectedTab(customTabId)?.content?.url?.tryGetHostFromUrl(),
                )
            }.collect {
                dismissSelectPrompts()

                val prompt = activePrompt?.get()
                // When showing folder upload confirm prompt, this is next of folder chooser prompt immediately.
                store.consumeAllSessionPrompts(
                    sessionId = prompt?.sessionId,
                    activePrompt = activePrompt,
                    predicate = {
                        it.shouldDismissOnLoad && it !is File &&
                            (it !is FolderUploadPrompt || previousPromptRequest !is Folder)
                    },
                    consume = {
                        if (prompt?.isStateSaved == true) {
                            prompt.dismiss()
                        }
                    },
                )

                // Let's make sure we do not leave anything behind.
                activePromptsToDismiss.forEach { fragment ->
                    if (fragment.isStateSaved) {
                        fragment.dismiss()
                    }
                }
            }
        }

        fragmentManager.findFragmentByTag(FRAGMENT_TAG)?.let { fragment ->
            // There's still a [PromptDialogFragment] visible from the last time. Re-attach this feature so that the
            // fragment can invoke the callback on this feature once the user makes a selection. This can happen when
            // the app was in the background and on resume the activity and fragments get recreated.
            reattachFragment(fragment as PromptDialogFragment)
        }
    }

    override fun stop() {
        // Stops observing the selected session for incoming prompt requests.
        handlePromptScope?.cancel()
        dismissPromptScope?.cancel()

        // Dismisses the logins prompt so that it can appear on another tab
        dismissSelectPrompts()
    }

    override fun onBackPressed(): Boolean {
        return dismissSelectPrompts()
    }

    /**
     * Notifies the feature of intent results for prompt requests handled by
     * other apps like credit card and file chooser requests.
     *
     * @param requestCode The code of the app that requested the intent.
     * @param data The result of the request.
     * @param resultCode The code of the result.
     */
    override fun onActivityResult(requestCode: Int, data: Intent?, resultCode: Int): Boolean {
        if (requestCode == PIN_REQUEST) {
            if (resultCode == Activity.RESULT_OK) {
                creditCardPicker?.onAuthSuccess()
            } else {
                creditCardPicker?.onAuthFailure()
            }

            return true
        }

        return filePicker.onActivityResult(requestCode, resultCode, data)
    }

    /**
     * Notifies the feature that the biometric authentication was completed. It will then
     * either process or dismiss the prompt request.
     *
     * @param isAuthenticated True if the user is authenticated successfully from the biometric
     * authentication prompt or false otherwise.
     */
    fun onBiometricResult(isAuthenticated: Boolean) {
        if (isAuthenticated) {
            creditCardPicker?.onAuthSuccess()
        } else {
            creditCardPicker?.onAuthFailure()
        }
    }

    /**
     * Notifies the feature that the permissions request was completed. It will then
     * either process or dismiss the prompt request.
     *
     * @param permissions List of permission requested.
     * @param grantResults The grant results for the corresponding permissions
     * @see [onNeedToRequestPermissions].
     */
    override fun onPermissionsResult(permissions: Array<String>, grantResults: IntArray) {
        filePicker.onPermissionsResult(permissions, grantResults)
    }

    /**
     * Invoked when a native dialog needs to be shown.
     *
     * @param session The session which requested the dialog.
     */
    @VisibleForTesting(otherwise = PRIVATE)
    internal fun onPromptRequested(session: SessionState) {
        // Some requests are handle with intents
        session.content.promptRequests.lastOrNull()?.let { promptRequest ->
            if (session.content.permissionRequestsList.isNotEmpty()) {
                val value: Any? = if (promptRequest is Popup || promptRequest is Redirect) false else null
                onCancel(session.id, promptRequest.uid, value)
            } else {
                processPromptRequest(promptRequest, session)
            }
        }
    }

    @Suppress("NestedBlockDepth", "CyclomaticComplexMethod", "CognitiveComplexMethod")
    @VisibleForTesting(otherwise = PRIVATE)
    internal fun processPromptRequest(
        promptRequest: PromptRequest,
        session: SessionState,
    ) {
        store.state.findTabOrCustomTabOrSelectedTab(customTabId)?.let {
            promptRequest.executeIfWindowedPrompt { exitFullscreenUsecase(it.id) }
        }

        previousPromptRequest = lastPromptRequest
        lastPromptRequest = promptRequest

        when (promptRequest) {
            is CertificateRequest -> {
                certificatePicker.handleCertificateRequest(promptRequest)
            }

            is File -> {
                emitPromptDisplayedFact(promptName = "FilePrompt")
                filePicker.handleFileRequest(promptRequest)
            }

            is Folder -> {
                emitPromptDisplayedFact(promptName = "FolderPrompt")
                filePicker.handleFolderRequest()
            }

            is Share -> handleShareRequest(promptRequest, session)
            is SelectCreditCard -> {
                emitSuccessfulCreditCardAutofillFormDetectedFact()
                if (isCreditCardAutofillEnabled() && promptRequest.creditCards.isNotEmpty()) {
                    creditCardPicker?.handleSelectCreditCardRequest(promptRequest)
                }
            }

            is SelectLoginPrompt -> {
                if (!isLoginAutofillEnabled() || dontUseStrongSuggestedPassword) {
                    return
                }

                if (previousPromptRequest is SaveLoginPrompt) {
                    return
                }

                val loginsByHint = promptRequest.logins.groupBy { it.hint }
                when {
                    LoginHint.GENERATED in loginsByHint -> {
                        if (shouldAutomaticallyShowSuggestedPassword.invoke()) {
                            onFirstTimeEngagedWithSignup.invoke()
                            handleDialogsRequest(
                                promptRequest,
                                session,
                            )
                        } else {
                            strongPasswordPromptViewListener?.onGeneratedPasswordPromptClick = {
                                handleDialogsRequest(
                                    promptRequest,
                                    session,
                                )
                            }
                            strongPasswordPromptViewListener?.handleSuggestStrongPasswordRequest()
                        }
                    }
                    LoginHint.EMAIL_MASK in loginsByHint -> handleEmailMaskOrLoginPrompt(
                        promptRequest,
                        session,
                        loginsByHint,
                    )
                    else -> {
                        loginPicker?.handleSelectLoginRequest(promptRequest)
                    }
                }

                emitPromptDisplayedFact(promptName = "SelectLoginPrompt")
            }

            is SelectAddress -> {
                emitSuccessfulAddressAutofillFormDetectedFact()
                if (isAddressAutofillEnabled() && promptRequest.addresses.isNotEmpty()) {
                    addressPicker?.handleSelectAddressRequest(promptRequest)
                }
            }

            else -> handleDialogsRequest(promptRequest, session)
        }
    }

    /**
     * Invoked when a dialog is dismissed. This consumes the [PromptFeature]
     * value from the session indicated by [sessionId].
     *
     * @param sessionId this is the id of the session which requested the prompt.
     * @param promptRequestUID identifier of the [PromptRequest] for which this dialog was shown.
     * @param value an optional value provided by the dialog as a result of canceling the action.
     */
    override fun onCancel(sessionId: String, promptRequestUID: String, value: Any?) {
        store.consumePromptFrom(sessionId, promptRequestUID, activePrompt) {
            emitPromptDismissedFact(promptName = it::class.simpleName.ifNullOrEmpty { "" })
            when (it) {
                is BeforeUnload -> it.onStay()
                is Popup -> {
                    val shouldNotShowMoreDialogs = value as Boolean
                    promptAbuserDetector.userWantsMoreDialogs(!shouldNotShowMoreDialogs)
                    it.onDeny()
                }
                is Redirect -> {
                    val shouldNotShowMoreDialogs = value as Boolean
                    promptAbuserDetector.userWantsMoreDialogs(!shouldNotShowMoreDialogs)
                    it.onDeny()
                }

                is Dismissible -> {
                    if (it is SelectLoginPrompt) {
                        dontUseStrongSuggestedPassword = true
                    }
                    it.onDismiss()
                }
                else -> {
                    // no-op
                }
            }
        }
    }

    /**
     * Invoked when the user confirms the action on the dialog. This consumes
     * the [PromptFeature] value from the [SessionState] indicated by [sessionId].
     *
     * @param sessionId that requested to show the dialog.
     * @param promptRequestUID identifier of the [PromptRequest] for which this dialog was shown.
     * @param value an optional value provided by the dialog as a result of confirming the action.
     */
    @Suppress("UNCHECKED_CAST", "CyclomaticComplexMethod")
    override fun onConfirm(sessionId: String, promptRequestUID: String, value: Any?) {
        store.consumePromptFrom(sessionId, promptRequestUID, activePrompt) {
            when (it) {
                is TimeSelection -> it.onConfirm(value as Date)
                is Color -> it.onConfirm(value as String)
                is Alert -> {
                    val shouldNotShowMoreDialogs = value as Boolean
                    promptAbuserDetector.userWantsMoreDialogs(!shouldNotShowMoreDialogs)
                    it.onConfirm(!shouldNotShowMoreDialogs)
                }

                is SingleChoice -> it.onConfirm(value as Choice)
                is MenuChoice -> it.onConfirm(value as Choice)
                is BeforeUnload -> it.onLeave()
                is Popup -> {
                    val shouldNotShowMoreDialogs = value as Boolean
                    promptAbuserDetector.userWantsMoreDialogs(!shouldNotShowMoreDialogs)
                    it.onAllow()
                }
                is Redirect -> {
                    val shouldNotShowMoreDialogs = value as Boolean
                    promptAbuserDetector.userWantsMoreDialogs(!shouldNotShowMoreDialogs)
                    it.onAllow()
                }

                is MultipleChoice -> it.onConfirm(value as Array<Choice>)

                is Authentication -> {
                    val (user, password) = value as Pair<String, String>
                    it.onConfirm(user, password)
                }

                is TextPrompt -> {
                    val (shouldNotShowMoreDialogs, text) = value as Pair<Boolean, String>

                    promptAbuserDetector.userWantsMoreDialogs(!shouldNotShowMoreDialogs)
                    it.onConfirm(!shouldNotShowMoreDialogs, text)
                }

                is Share -> it.onSuccess()

                is SaveCreditCard -> it.onConfirm(value as CreditCardEntry)
                is SaveLoginPrompt -> it.onConfirm(value as LoginEntry)

                is Confirm -> {
                    val (isCheckBoxChecked, buttonType) =
                        value as Pair<Boolean, MultiButtonDialogFragment.ButtonType>
                    promptAbuserDetector.userWantsMoreDialogs(!isCheckBoxChecked)
                    when (buttonType) {
                        MultiButtonDialogFragment.ButtonType.POSITIVE ->
                            it.onConfirmPositiveButton(!isCheckBoxChecked)

                        MultiButtonDialogFragment.ButtonType.NEGATIVE ->
                            it.onConfirmNegativeButton(!isCheckBoxChecked)

                        MultiButtonDialogFragment.ButtonType.NEUTRAL ->
                            it.onConfirmNeutralButton(!isCheckBoxChecked)
                    }
                }

                is Repost -> it.onConfirm()
                is PromptRequest.IdentityCredential.SelectProvider -> it.onConfirm(value as Provider)
                is PromptRequest.IdentityCredential.SelectAccount -> it.onConfirm(value as Account)
                is PromptRequest.IdentityCredential.PrivacyPolicy -> it.onConfirm(value as Boolean)
                is SelectLoginPrompt -> it.onConfirm(value as Login)
                is FolderUploadPrompt -> it.onConfirm()
                is WebAuthnRelatedOriginPrompt -> it.onConfirm()
                else -> {
                    // no-op
                }
            }
            emitPromptConfirmedFact(it::class.simpleName.ifNullOrEmpty { "" })
        }
    }

    /**
     * Invoked when the user is requesting to clear the selected value from the dialog.
     * This consumes the [PromptFeature] value from the [SessionState] indicated by [sessionId].
     *
     * @param sessionId that requested to show the dialog.
     * @param promptRequestUID identifier of the [PromptRequest] for which this dialog was shown.
     */
    override fun onClear(sessionId: String, promptRequestUID: String) {
        store.consumePromptFrom(sessionId, promptRequestUID, activePrompt) {
            when (it) {
                is TimeSelection -> it.onClear()
                else -> {
                    // no-op
                }
            }
        }
    }

    /**
     * Re-attaches a fragment that is still visible but not linked to this feature anymore.
     */
    private fun reattachFragment(fragment: PromptDialogFragment) {
        val session = store.state.findTabOrCustomTab(fragment.sessionId)
        if (session == null || session.content.promptRequests.isEmpty()) {
            fragmentManager.beginTransaction()
                .remove(fragment)
                .commitAllowingStateLoss()
            return
        }
        // Re-assign the feature instance so that the fragment can invoke us once the user makes a selection or cancels
        // the dialog.
        fragment.feature = this
    }

    private fun handleShareRequest(promptRequest: Share, session: SessionState) {
        emitPromptDisplayedFact(promptName = "ShareSheet")
        shareDelegate.showShareSheet(
            context = container.context,
            shareData = promptRequest.data,
            onDismiss = {
                emitPromptDismissedFact(promptName = "ShareSheet")
                onCancel(session.id, promptRequest.uid)
            },
            onSuccess = { onConfirm(session.id, promptRequest.uid, null) },
        )
    }

    /**
     * Called from on [onPromptRequested] to handle requests for showing native dialogs.
     */
    @Suppress("CognitiveComplexMethod", "LongMethod", "ReturnCount", "CyclomaticComplexMethod")
    @VisibleForTesting(otherwise = PRIVATE)
    internal fun handleDialogsRequest(
        promptRequest: PromptRequest,
        session: SessionState,
    ) {
        // Requests that are handled with dialogs
        val dialog = when (promptRequest) {
            is SelectLoginPrompt -> {
                val currentUrl =
                    store.state.findTabOrCustomTabOrSelectedTab(customTabId)?.content?.url
                val generatedPassword = promptRequest.generatedPassword

                if (generatedPassword == null || currentUrl == null) {
                    logger.debug(
                        "Ignoring received SelectLogin.onGeneratedPasswordPromptClick" +
                            " when either the generated password or the currentUrl is null.",
                    )
                    dismissDialogRequest(promptRequest, session)
                    return
                }

                emitGeneratedPasswordShownFact()

                PasswordGeneratorDialogFragment.newInstance(
                    sessionId = session.id,
                    promptRequestUID = promptRequest.uid,
                    generatedPassword = generatedPassword,
                    currentUrl = currentUrl,
                    onSavedGeneratedPassword = onSaveLogin,
                    colorsProvider = passwordGeneratorColorsProvider,
                )
            }

            is SaveCreditCard -> {
                if (!isCreditCardAutofillEnabled.invoke() || creditCardValidationDelegate == null ||
                    !promptRequest.creditCard.isValid
                ) {
                    dismissDialogRequest(promptRequest, session)

                    if (creditCardValidationDelegate == null) {
                        logger.debug(
                            "Ignoring received SaveCreditCard because PromptFeature." +
                                "creditCardValidationDelegate is null. If you are trying to autofill " +
                                "credit cards, try attaching a CreditCardValidationDelegate to PromptFeature",
                        )
                    }

                    return
                }

                emitCreditCardSaveShownFact()

                CreditCardSaveDialogFragment.newInstance(
                    sessionId = session.id,
                    promptRequestUID = promptRequest.uid,
                    shouldDismissOnLoad = false,
                    creditCard = promptRequest.creditCard,
                )
            }

            is SaveLoginPrompt -> {
                if (!isSaveLoginEnabled.invoke() || loginValidationDelegate == null) {
                    dismissDialogRequest(promptRequest, session)

                    if (loginValidationDelegate == null) {
                        logger.debug(
                            "Ignoring received SaveLoginPrompt because PromptFeature." +
                                "loginValidationDelegate is null. If you are trying to autofill logins, " +
                                "try attaching a LoginValidationDelegate to PromptFeature",
                        )
                    }

                    return
                } else if (hideUpdateFragmentAfterSavingGeneratedPassword(
                        promptRequest.logins[0].username,
                        promptRequest.logins[0].password,
                    )
                ) {
                    removeLastSavedGeneratedPassword()
                    dismissDialogRequest(promptRequest, session)

                    return
                }
                SaveLoginDialogFragment.newInstance(
                    sessionId = session.id,
                    promptRequestUID = promptRequest.uid,
                    shouldDismissOnLoad = false,
                    hint = promptRequest.hint,
                    // For v1, we only handle a single login and drop all others on the floor
                    entry = promptRequest.logins[0],
                    icon = session.content.icon,
                )
            }

            is SingleChoice -> ChoiceDialogFragment.newInstance(
                promptRequest.choices,
                session.id,
                promptRequest.uid,
                true,
                SINGLE_CHOICE_DIALOG_TYPE,
            )

            is MultipleChoice -> ChoiceDialogFragment.newInstance(
                promptRequest.choices,
                session.id,
                promptRequest.uid,
                true,
                MULTIPLE_CHOICE_DIALOG_TYPE,
            )

            is MenuChoice -> ChoiceDialogFragment.newInstance(
                promptRequest.choices,
                session.id,
                promptRequest.uid,
                true,
                MENU_CHOICE_DIALOG_TYPE,
            )

            is Alert -> {
                with(promptRequest) {
                    AlertDialogFragment.newInstance(
                        session.id,
                        promptRequest.uid,
                        true,
                        title,
                        message,
                        promptAbuserDetector.areDialogsBeingAbused(),
                    )
                }
            }

            is TimeSelection -> {
                val selectionType = when (promptRequest.type) {
                    TimeSelection.Type.DATE -> TimePickerDialogFragment.SELECTION_TYPE_DATE
                    TimeSelection.Type.DATE_AND_TIME -> TimePickerDialogFragment.SELECTION_TYPE_DATE_AND_TIME
                    TimeSelection.Type.TIME -> TimePickerDialogFragment.SELECTION_TYPE_TIME
                    TimeSelection.Type.MONTH -> TimePickerDialogFragment.SELECTION_TYPE_MONTH
                }

                with(promptRequest) {
                    TimePickerDialogFragment.newInstance(
                        session.id,
                        promptRequest.uid,
                        true,
                        initialDate,
                        minimumDate,
                        maximumDate,
                        selectionType,
                        stepValue,
                    )
                }
            }

            is TextPrompt -> {
                with(promptRequest) {
                    TextPromptDialogFragment.newInstance(
                        session.id,
                        promptRequest.uid,
                        true,
                        title,
                        inputLabel,
                        inputValue,
                        promptAbuserDetector.areDialogsBeingAbused(),
                        store.state.selectedTab?.content?.private == true,
                    )
                }
            }

            is Authentication -> {
                with(promptRequest) {
                    AuthenticationDialogFragment.newInstance(
                        session.id,
                        promptRequest.uid,
                        true,
                        title,
                        message,
                        userName,
                        password,
                        onlyShowPassword,
                        uri,
                    )
                }
            }

            is Color -> ColorPickerDialogFragment.newInstance(
                session.id,
                promptRequest.uid,
                true,
                promptRequest.defaultColor,
            )

            is Popup -> {
                val title = container.getString(R.string.mozac_feature_prompts_popup_dialog_title)
                val positiveLabel = container.getString(R.string.mozac_feature_prompts_allow)
                val negativeLabel = container.getString(R.string.mozac_feature_prompts_deny)

                ConfirmDialogFragment.newInstance(
                    sessionId = session.id,
                    promptRequest.uid,
                    title = title,
                    message = promptRequest.targetUri,
                    positiveButtonText = positiveLabel,
                    negativeButtonText = negativeLabel,
                    hasShownManyDialogs = promptAbuserDetector.areDialogsBeingAbused(),
                    shouldDismissOnLoad = true,
                )
            }

            is Redirect -> {
                val title = container.getString(R.string.mozac_feature_prompts_redirect_dialog_title)
                val positiveLabel = container.getString(R.string.mozac_feature_prompts_allow)
                val negativeLabel = container.getString(R.string.mozac_feature_prompts_deny)

                ConfirmDialogFragment.newInstance(
                    sessionId = session.id,
                    promptRequest.uid,
                    title = title,
                    message = promptRequest.targetUri,
                    positiveButtonText = positiveLabel,
                    negativeButtonText = negativeLabel,
                    hasShownManyDialogs = promptAbuserDetector.areDialogsBeingAbused(),
                    shouldDismissOnLoad = true,
                )
            }

            is BeforeUnload -> {
                val title =
                    container.getString(R.string.mozac_feature_prompt_before_unload_dialog_title)
                val body =
                    container.getString(R.string.mozac_feature_prompt_before_unload_dialog_body)
                val leaveLabel =
                    container.getString(R.string.mozac_feature_prompts_before_unload_leave)
                val stayLabel =
                    container.getString(R.string.mozac_feature_prompts_before_unload_stay)

                ConfirmDialogFragment.newInstance(
                    sessionId = session.id,
                    promptRequest.uid,
                    title = title,
                    message = body,
                    positiveButtonText = leaveLabel,
                    negativeButtonText = stayLabel,
                    shouldDismissOnLoad = true,
                )
            }

            is Confirm -> {
                with(promptRequest) {
                    val positiveButton = positiveButtonTitle.ifEmpty {
                        container.getString(R.string.mozac_feature_prompts_ok)
                    }
                    val negativeButton = negativeButtonTitle.ifEmpty {
                        container.getString(R.string.mozac_feature_prompts_cancel)
                    }

                    MultiButtonDialogFragment.newInstance(
                        session.id,
                        promptRequest.uid,
                        title,
                        message,
                        promptAbuserDetector.areDialogsBeingAbused(),
                        false,
                        positiveButton,
                        negativeButton,
                        neutralButtonTitle,
                    )
                }
            }

            is Repost -> {
                val title = container.context.getString(R.string.mozac_feature_prompt_repost_title)
                val message =
                    container.context.getString(R.string.mozac_feature_prompt_repost_message)
                val positiveAction =
                    container.context.getString(R.string.mozac_feature_prompt_repost_positive_button_text)
                val negativeAction =
                    container.context.getString(R.string.mozac_feature_prompt_repost_negative_button_text)

                ConfirmDialogFragment.newInstance(
                    sessionId = session.id,
                    promptRequestUID = promptRequest.uid,
                    shouldDismissOnLoad = true,
                    title = title,
                    message = message,
                    positiveButtonText = positiveAction,
                    negativeButtonText = negativeAction,
                )
            }

            is FolderUploadPrompt -> {
                val title = container.context.getString(R.string.mozac_feature_prompt_folder_upload_confirm_title)
                val message =
                    container.getString(
                        R.string.mozac_feature_prompt_folder_upload_confirm_message,
                        promptRequest.folderName,
                    )
                val positiveAction =
                    container.getString(R.string.mozac_feature_prompt_folder_upload_confirm_positive_button_text)
                val negativeAction =
                    container.getString(R.string.mozac_feature_prompt_folder_upload_confirm_negative_button_text)

                ConfirmDialogFragment.newInstance(
                    sessionId = session.id,
                    promptRequestUID = promptRequest.uid,
                    shouldDismissOnLoad = true,
                    title = title,
                    message = message,
                    positiveButtonText = positiveAction,
                    negativeButtonText = negativeAction,
                )
            }

            is WebAuthnRelatedOriginPrompt -> {
                val messageResId = if (promptRequest.isCreate) {
                    R.string.webauthn_related_origin_create_message
                } else {
                    R.string.webauthn_related_origin_use_message
                }
                WebAuthnRelatedOriginDialogFragment.newInstance(
                    sessionId = session.id,
                    promptRequestUID = promptRequest.uid,
                    message = container.getString(messageResId, promptRequest.origin, promptRequest.rpId),
                )
            }

            is PromptRequest.IdentityCredential.SelectProvider -> {
                SelectProviderDialogFragment.newInstance(
                    sessionId = session.id,
                    promptRequestUID = promptRequest.uid,
                    shouldDismissOnLoad = true,
                    providers = promptRequest.providers,
                    colorsProvider = identityCredentialColorsProvider,
                )
            }

            is PromptRequest.IdentityCredential.SelectAccount -> {
                SelectAccountDialogFragment.newInstance(
                    sessionId = session.id,
                    promptRequestUID = promptRequest.uid,
                    shouldDismissOnLoad = true,
                    accounts = promptRequest.accounts,
                    provider = promptRequest.provider,
                    colorsProvider = identityCredentialColorsProvider,
                )
            }

            is PromptRequest.IdentityCredential.PrivacyPolicy -> {
                val title =
                    container.getString(
                        R.string.mozac_feature_prompts_identity_credentials_privacy_policy_title,
                        promptRequest.providerDomain,
                    )
                val message =
                    container.getString(
                        R.string.mozac_feature_prompts_identity_credentials_privacy_policy_description,
                        promptRequest.host,
                        promptRequest.providerDomain,
                        promptRequest.privacyPolicyUrl,
                        promptRequest.termsOfServiceUrl,
                    )
                PrivacyPolicyDialogFragment.newInstance(
                    sessionId = session.id,
                    promptRequestUID = promptRequest.uid,
                    shouldDismissOnLoad = true,
                    title = title,
                    message = message,
                    icon = promptRequest.icon,
                )
            }

            else -> throw InvalidParameterException("Not valid prompt request type $promptRequest")
        }

        dialog.feature = this

        if (canShowThisPrompt(promptRequest)) {
            maybeDismissPreviousDialog(session)

            emitPromptDisplayedFact(promptName = dialog::class.simpleName.ifNullOrEmpty { "" })
            dialog.show(fragmentManager, FRAGMENT_TAG)
            activePrompt = WeakReference(dialog)

            if (promptRequest.shouldDismissOnLoad) {
                activePromptsToDismiss.add(dialog)
            }
        } else {
            dismissDialogRequest(promptRequest, session)
        }
        promptAbuserDetector.updateJSDialogAbusedState()
    }

    /**
     * If the ChoiceDialogFragment's choices data were updated, we need to dismiss the previous
     * dialog.
     */
    private fun maybeDismissPreviousDialog(session: SessionState) {
        activePrompt?.get()?.let { promptDialog ->
            // ChoiceDialogFragment could update their choices data,
            // and we need to dismiss the previous UI dialog,
            // without consuming the engine callbacks, and allow to create a new dialog with the
            // updated data.
            if (promptDialog is ChoiceDialogFragment &&
                !session.content.promptRequests.any { it.uid == promptDialog.promptRequestUID }
            ) {
                // We want to avoid consuming the engine callbacks and allow a new dialog
                // to be created with the updated data.
                promptDialog.feature = null
                if (promptDialog.isStateSaved) {
                    promptDialog.dismiss()
                }
            }
        }
    }

    /**
     * Helper function to handle showing an email mask prompt if the conditions are met
     * or to fallback to handling the dialog request.
     */
    @VisibleForTesting
    internal fun handleEmailMaskOrLoginPrompt(
        promptRequest: SelectLoginPrompt,
        session: SessionState,
        loginsByHint: Map<LoginHint, List<Login>>,
    ) {
        val shouldShowEmailMaskSuggestion =
            isEmailMaskFeatureEnabled() && isSuggestEmailMaskEnabled()
        if (!shouldShowEmailMaskSuggestion) {
            return
        } else {
            emailMaskPromptViewListener?.apply {
                onEmailMaskPromptClick = { handleDialogsRequest(promptRequest, session) }
                handleEmailMaskRequest(loginsByHint[LoginHint.EMAIL_MASK])
            } ?: handleDialogsRequest(promptRequest, session)
        }
    }

    /**
     * Dismiss and consume the given prompt request for the session.
     */
    @VisibleForTesting
    internal fun dismissDialogRequest(promptRequest: PromptRequest, session: SessionState) {
        (promptRequest as Dismissible).onDismiss()
        store.dispatch(ContentAction.ConsumePromptRequestAction(session.id, promptRequest))
        emitPromptDismissedFact(promptName = promptRequest::class.simpleName.ifNullOrEmpty { "" })
    }

    @VisibleForTesting
    internal fun redirectDialogFragmentIsActive() =
        (fragmentManager.findFragmentByTag("SHOULD_OPEN_APP_LINK_PROMPT_DIALOG") as? DialogFragment) != null

    private fun canShowThisPrompt(promptRequest: PromptRequest): Boolean {
        if (redirectDialogFragmentIsActive()) {
            return false
        }

        return when (promptRequest) {
            is SingleChoice,
            is MultipleChoice,
            is MenuChoice,
            is TimeSelection,
            is File,
            is Folder,
            is Color,
            is Authentication,
            is BeforeUnload,
            is CertificateRequest,
            is SaveLoginPrompt,
            is SelectLoginPrompt,
            is SelectCreditCard,
            is SaveCreditCard,
            is SelectAddress,
            is Share,
            is PromptRequest.IdentityCredential.SelectProvider,
            is PromptRequest.IdentityCredential.SelectAccount,
            is PromptRequest.IdentityCredential.PrivacyPolicy,
            -> true

            is Alert, is TextPrompt, is Confirm, is Repost, is Popup, is FolderUploadPrompt, is Redirect,
            -> promptAbuserDetector.shouldShowMoreDialogs

            is WebAuthnRelatedOriginPrompt,
            -> true
        }
    }

    /**
     * Dismisses the select prompts if they are active and visible.
     *
     * @returns true if a select prompt was dismissed, otherwise false.
     */
    @VisibleForTesting
    internal fun dismissSelectPrompts(): Boolean {
        var dismissed = false

        (activePromptRequest as? SelectLoginPrompt)?.let { selectLoginPrompt ->
            loginPicker?.let { loginPicker ->
                if (loginDelegate.loginPickerView?.isPromptDisplayed == true) {
                    loginPicker.dismissCurrentLoginSelect(selectLoginPrompt)
                    dismissed = true
                }
            }

            strongPasswordPromptViewListener?.let { strongPasswordPromptViewListener ->
                if (suggestStrongPasswordDelegate.strongPasswordPromptViewListenerView?.isPromptDisplayed == true) {
                    strongPasswordPromptViewListener.dismissCurrentSuggestStrongPassword(
                        selectLoginPrompt,
                    )
                    dismissed = true
                }
            }
        }

        (activePromptRequest as? SelectCreditCard)?.let { selectCreditCardPrompt ->
            creditCardPicker?.let { creditCardPicker ->
                if (creditCardDelegate.creditCardPickerView?.isPromptDisplayed == true) {
                    creditCardPicker.dismissSelectCreditCardRequest(selectCreditCardPrompt)
                    dismissed = true
                }
            }
        }

        (activePromptRequest as? SelectAddress)?.let { selectAddressPrompt ->
            addressPicker?.let { addressPicker ->
                if (addressDelegate.addressPickerView?.isPromptDisplayed == true) {
                    addressPicker.dismissSelectAddressRequest(selectAddressPrompt)
                    dismissed = true
                }
            }
        }

        if (dismissed) {
            activePromptRequest = null
        }

        return dismissed
    }

    /**
     * Handles the result received from the Android photo picker.
     *
     * @param uriList An array of [Uri] objects representing the selected photos.
     */

    fun onAndroidPhotoPickerResult(uriList: Array<Uri>) {
        filePicker.onAndroidPhotoPickerResult(uriList)
    }

    companion object {
        // The PIN request code
        const val PIN_REQUEST = 303
    }
}

/**
 * Removes the [PromptRequest] indicated by [promptRequestUID] from the current Session if it it exists
 * and offers a [consume] callback for other optional side effects.
 *
 * @param sessionId Session id of the tab or custom tab in which to try consuming [PromptRequest]s.
 * If the id is not provided or a tab with that id is not found the method will act on the current tab.
 * @param promptRequestUID Id of the [PromptRequest] to be consumed.
 * @param activePrompt The current active Prompt if known. If provided it will always be cleared,
 * irrespective of if [PromptRequest] indicated by [promptRequestUID] is found and removed or not.
 * @param consume callback with the [PromptRequest] if found, before being removed from the Session.
 */
internal fun BrowserStore.consumePromptFrom(
    sessionId: String?,
    promptRequestUID: String,
    activePrompt: WeakReference<PromptDialogFragment>? = null,
    consume: (PromptRequest) -> Unit,
) {
    state.findTabOrCustomTabOrSelectedTab(sessionId)?.let { tab ->
        activePrompt?.clear()
        tab.content.promptRequests.firstOrNull { it.uid == promptRequestUID }?.let {
            consume(it)
            dispatch(ContentAction.ConsumePromptRequestAction(tab.id, it))
        }
    }
}

/**
 * Removes the most recent [PromptRequest] of type [P] from the current Session if it it exists
 * and offers a [consume] callback for other optional side effects.
 *
 * @param sessionId Session id of the tab or custom tab in which to try consuming [PromptRequest]s.
 * If the id is not provided or a tab with that id is not found the method will act on the current tab.
 * @param activePrompt The current active Prompt if known. If provided it will always be cleared,
 * irrespective of if [PromptRequest] indicated by [PromptRequest.uid] is found and removed or not.
 * @param consume callback with the [PromptRequest] if found, before being removed from the Session.
 */
internal inline fun <reified P : PromptRequest> BrowserStore.consumePromptFrom(
    sessionId: String?,
    activePrompt: WeakReference<PromptDialogFragment>? = null,
    consume: (P) -> Unit,
) {
    state.findTabOrCustomTabOrSelectedTab(sessionId)?.let { tab ->
        activePrompt?.clear()
        tab.content.promptRequests.lastOrNull { it is P }?.let {
            consume(it as P)
            dispatch(ContentAction.ConsumePromptRequestAction(tab.id, it))
        }
    }
}

/**
 * Filters and removes all [PromptRequest]s from the current Session if it it exists
 * and offers a [consume] callback for other optional side effects on each filtered [PromptRequest].
 *
 * @param sessionId Session id of the tab or custom tab in which to try consuming [PromptRequest]s.
 * If the id is not provided or a tab with that id is not found the method will act on the current tab.
 * @param activePrompt The current active Prompt if known. If provided it will always be cleared,
 * irrespective of if [PromptRequest] indicated by [PromptRequest.uid] is found and removed or not.
 * @param predicate function allowing matching only specific [PromptRequest]s from all contained in the Session.
 * @param consume callback with the [PromptRequest] if found, before being removed from the Session.
 */
internal fun BrowserStore.consumeAllSessionPrompts(
    sessionId: String?,
    activePrompt: WeakReference<PromptDialogFragment>? = null,
    predicate: (PromptRequest) -> Boolean,
    consume: (PromptRequest) -> Unit = { },
) {
    state.findTabOrCustomTabOrSelectedTab(sessionId)?.let { tab ->
        activePrompt?.clear()
        tab.content.promptRequests
            .filter { predicate(it) }
            .forEach {
                consume(it)
                dispatch(ContentAction.ConsumePromptRequestAction(tab.id, it))
            }
    }
}
