/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.prompts

import android.app.Activity
import android.app.Activity.RESULT_CANCELED
import android.app.Activity.RESULT_OK
import android.content.ClipData
import android.content.ContentResolver
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.net.Uri
import androidx.core.net.toUri
import androidx.fragment.app.Fragment
import androidx.fragment.app.FragmentManager
import androidx.fragment.app.FragmentTransaction
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.action.BrowserAction
import mozilla.components.browser.state.action.ContentAction
import mozilla.components.browser.state.action.TabListAction
import mozilla.components.browser.state.engine.EngineMiddleware
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.ContentState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.state.createCustomTab
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.prompt.Choice
import mozilla.components.concept.engine.prompt.PromptRequest
import mozilla.components.concept.engine.prompt.PromptRequest.Alert
import mozilla.components.concept.engine.prompt.PromptRequest.Authentication
import mozilla.components.concept.engine.prompt.PromptRequest.Authentication.Level.NONE
import mozilla.components.concept.engine.prompt.PromptRequest.Authentication.Method.HOST
import mozilla.components.concept.engine.prompt.PromptRequest.Color
import mozilla.components.concept.engine.prompt.PromptRequest.MenuChoice
import mozilla.components.concept.engine.prompt.PromptRequest.MultipleChoice
import mozilla.components.concept.engine.prompt.PromptRequest.SingleChoice
import mozilla.components.concept.engine.prompt.PromptRequest.TextPrompt
import mozilla.components.concept.engine.prompt.ShareData
import mozilla.components.concept.storage.Address
import mozilla.components.concept.storage.CreditCardEntry
import mozilla.components.concept.storage.Login
import mozilla.components.concept.storage.LoginEntry
import mozilla.components.concept.storage.LoginHint
import mozilla.components.feature.prompts.address.AddressDelegate
import mozilla.components.feature.prompts.address.AddressPicker
import mozilla.components.feature.prompts.certificate.CertificatePicker
import mozilla.components.feature.prompts.concept.AutocompletePrompt
import mozilla.components.feature.prompts.concept.EmailMaskPromptView
import mozilla.components.feature.prompts.concept.PasswordPromptView
import mozilla.components.feature.prompts.creditcard.CreditCardDelegate
import mozilla.components.feature.prompts.creditcard.CreditCardPicker
import mozilla.components.feature.prompts.creditcard.CreditCardSaveDialogFragment
import mozilla.components.feature.prompts.dialog.ChoiceDialogFragment
import mozilla.components.feature.prompts.dialog.ConfirmDialogFragment
import mozilla.components.feature.prompts.dialog.MultiButtonDialogFragment
import mozilla.components.feature.prompts.dialog.PromptDialogFragment
import mozilla.components.feature.prompts.dialog.SaveLoginDialogFragment
import mozilla.components.feature.prompts.dialog.WebAuthnRelatedOriginDialogFragment
import mozilla.components.feature.prompts.emailmask.EmailMaskDelegate
import mozilla.components.feature.prompts.facts.CreditCardAutofillDialogFacts
import mozilla.components.feature.prompts.file.FilePicker.Companion.FILE_PICKER_ACTIVITY_REQUEST_CODE
import mozilla.components.feature.prompts.login.LoginDelegate
import mozilla.components.feature.prompts.login.LoginPicker
import mozilla.components.feature.prompts.login.StrongPasswordPromptViewListener
import mozilla.components.feature.prompts.login.SuggestStrongPasswordDelegate
import mozilla.components.feature.prompts.share.ShareDelegate
import mozilla.components.feature.session.SessionUseCases
import mozilla.components.support.base.Component
import mozilla.components.support.base.facts.Action
import mozilla.components.support.base.facts.processor.CollectionProcessor
import mozilla.components.support.test.any
import mozilla.components.support.test.eq
import mozilla.components.support.test.middleware.CaptureActionsMiddleware
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.test.whenever
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.doReturn
import org.mockito.Mockito.never
import org.mockito.Mockito.spy
import org.mockito.Mockito.times
import org.mockito.Mockito.verify
import org.mockito.Mockito.`when`
import org.robolectric.Robolectric
import java.lang.ref.WeakReference
import java.security.InvalidParameterException
import java.util.Date
import kotlin.test.assertIs

@RunWith(AndroidJUnit4::class)
class PromptFeatureTest {

    private val testDispatcher = StandardTestDispatcher()

    private lateinit var store: BrowserStore
    private lateinit var fragmentManager: FragmentManager
    private lateinit var loginPicker: LoginPicker
    private lateinit var creditCardPicker: CreditCardPicker
    private lateinit var addressPicker: AddressPicker
    private lateinit var certificatePicker: CertificatePicker

    private val tabId = "test-tab"
    private fun tab(): TabSessionState? {
        return store.state.tabs.find { it.id == tabId }
    }

    @Before
    fun setUp() {
        store = BrowserStore(
            BrowserState(
                tabs = listOf(
                    createTab("https://www.mozilla.org", id = tabId),
                ),
                customTabs = listOf(
                    createCustomTab("https://www.mozilla.org", id = "custom-tab"),
                ),
                selectedTabId = tabId,
            ),
            middleware = EngineMiddleware.create(mock()),
        )
        loginPicker = mock()
        creditCardPicker = mock()
        addressPicker = mock()
        certificatePicker = mock()
        fragmentManager = mockFragmentManager()
    }

    @Test
    fun `PromptFeature acts on the selected session by default`() = runTest(testDispatcher) {
        val feature = spy(
            PromptFeature(
                fragment = mock(),
                fileUploadsDirCleaner = mock(),
                store = store,
                tabsUseCases = mock(),
                fragmentManager = fragmentManager,
                mainDispatcher = testDispatcher,
            ) { },
        )
        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        val promptRequest = SingleChoice(arrayOf(), {}, {})
        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, promptRequest))
        testDispatcher.scheduler.advanceUntilIdle()

        verify(feature).onPromptRequested(store.state.tabs.first())
    }

    @Test
    fun `PromptFeature acts on a given custom tab session`() = runTest(testDispatcher) {
        val feature = spy(
            PromptFeature(
                fragment = mock(),
                store = store,
                tabsUseCases = mock(),
                fileUploadsDirCleaner = mock(),
                customTabId = "custom-tab",
                fragmentManager = fragmentManager,
                mainDispatcher = testDispatcher,
            ) { },
        )
        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        val promptRequest = SingleChoice(arrayOf(), {}, {})
        store.dispatch(ContentAction.UpdatePromptRequestAction("custom-tab", promptRequest))
        testDispatcher.scheduler.advanceUntilIdle()

        verify(feature).onPromptRequested(store.state.customTabs.first())
    }

    @Test
    fun `PromptFeatures must cancel promptRequest when there is an active permission`() = runTest(testDispatcher) {
        val feature = spy(
            PromptFeature(
                fragment = mock(),
                fileUploadsDirCleaner = mock(),
                store = store,
                tabsUseCases = mock(),
                fragmentManager = fragmentManager,
                mainDispatcher = testDispatcher,
            ) { },
        )

        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        val promptRequest = SingleChoice(arrayOf(), {}, {})
        store.dispatch(ContentAction.UpdatePermissionsRequest(tabId, mock()))
        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, promptRequest))
        testDispatcher.scheduler.advanceUntilIdle()

        verify(feature).onCancel(tabId, promptRequest.uid)
    }

    @Test
    fun `PromptFeature acts on the selected session if there is no custom tab ID`() = runTest(testDispatcher) {
        val feature = spy(
            PromptFeature(
                fragment = mock(),
                store = store,
                tabsUseCases = mock(),
                customTabId = tabId,
                mainDispatcher = testDispatcher,
                fileUploadsDirCleaner = mock(),
                fragmentManager = fragmentManager,
            ) { },
        )
        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        val promptRequest = SingleChoice(arrayOf(), {}, {})
        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, promptRequest))
        testDispatcher.scheduler.advanceUntilIdle()

        verify(feature).onPromptRequested(store.state.tabs.first())
    }

    @Test
    fun `New promptRequests for selected session will cause fragment transaction`() = runTest(testDispatcher) {
        val feature =
            PromptFeature(
                fragment = mock(),
                store = store,
                tabsUseCases = mock(),
                fileUploadsDirCleaner = mock(),
                fragmentManager = fragmentManager,
                mainDispatcher = testDispatcher,
            ) { }
        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        val singleChoiceRequest = SingleChoice(arrayOf(), {}, {})
        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, singleChoiceRequest))
        testDispatcher.scheduler.advanceUntilIdle()

        verify(fragmentManager).beginTransaction()
    }

    @Test
    fun `New promptRequests for selected session will not cause fragment transaction if feature is stopped`() = runTest(testDispatcher) {
        val feature =
            PromptFeature(
                fragment = mock(),
                tabsUseCases = mock(),
                store = store,
                fileUploadsDirCleaner = mock(),
                fragmentManager = fragmentManager,
            ) { }
        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()
        feature.stop()

        val singleChoiceRequest = SingleChoice(arrayOf(), {}, {})
        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, singleChoiceRequest))
        verify(fragmentManager, never()).beginTransaction()
    }

    @Test
    fun `Feature will re-attach to already existing fragment`() = runTest(testDispatcher) {
        val fragment: ChoiceDialogFragment = mock()
        doReturn(tabId).`when`(fragment).sessionId
        doReturn(fragment).`when`(fragmentManager).findFragmentByTag(FRAGMENT_TAG)

        val singleChoiceRequest = mock<SingleChoice>()
        whenever(singleChoiceRequest.shouldDismissOnLoad).thenReturn(false)

        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, singleChoiceRequest))

        val feature = PromptFeature(
            activity = mock(),
            store = store,
            fragmentManager = fragmentManager,
            tabsUseCases = mock(),
            fileUploadsDirCleaner = mock(),
            isEmailMaskFeatureEnabled = { false },
            isSuggestEmailMaskEnabled = { false },
            onNeedToRequestPermissions = { },
        )
        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()
        verify(fragment).feature = feature
    }

    @Test
    fun `Existing fragment will be removed if session has no prompt request`() = runTest(testDispatcher) {
        val fragment: ChoiceDialogFragment = mock()
        doReturn(tabId).`when`(fragment).sessionId
        doReturn(fragment).`when`(fragmentManager).findFragmentByTag(FRAGMENT_TAG)

        val transaction: FragmentTransaction = mock()
        doReturn(transaction).`when`(fragmentManager).beginTransaction()
        doReturn(transaction).`when`(transaction).remove(any())

        val feature = PromptFeature(
            activity = mock(),
            store = store,
            fragmentManager = fragmentManager,
            tabsUseCases = mock(),
            fileUploadsDirCleaner = mock(),
            isEmailMaskFeatureEnabled = { false },
            isSuggestEmailMaskEnabled = { false },
            onNeedToRequestPermissions = { },
        )
        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        verify(fragment, never()).feature = feature
        verify(fragmentManager).beginTransaction()
        verify(transaction).remove(fragment)
    }

    @Test
    fun `Existing fragment will be removed if session does not exist anymore`() = runTest(testDispatcher) {
        val fragment: ChoiceDialogFragment = mock()
        doReturn("invalid-tab").`when`(fragment).sessionId
        doReturn(fragment).`when`(fragmentManager).findFragmentByTag(FRAGMENT_TAG)

        val singleChoiceRequest = SingleChoice(arrayOf(), {}, {})
        store.dispatch(ContentAction.UpdatePromptRequestAction("invalid-tab", singleChoiceRequest))

        val transaction: FragmentTransaction = mock()
        doReturn(transaction).`when`(fragmentManager).beginTransaction()
        doReturn(transaction).`when`(transaction).remove(any())

        val feature = PromptFeature(
            activity = mock(),
            store = store,
            fragmentManager = fragmentManager,
            tabsUseCases = mock(),
            fileUploadsDirCleaner = mock(),
            isEmailMaskFeatureEnabled = { false },
            isSuggestEmailMaskEnabled = { false },
            onNeedToRequestPermissions = { },
        )
        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        verify(fragment, never()).feature = feature
        verify(fragmentManager).beginTransaction()
        verify(transaction).remove(fragment)
    }

    @Test
    fun `Calling onStop will attempt to dismiss the select prompts`() = runTest(testDispatcher) {
        val feature = spy(
            PromptFeature(
                mock<Activity>(),
                store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = { },
            ),
        )

        feature.stop()

        verify(feature).dismissSelectPrompts()
    }

    @Test
    fun `GIVEN loginPickerView is visible WHEN dismissSelectPrompts THEN dismissCurrentLoginSelect called and true returned`() = runTest(testDispatcher) {
        // given
        val loginPickerView: AutocompletePrompt<Login> = mock()
        val feature = spy(
            PromptFeature(
                mock<Activity>(),
                store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                loginDelegate = object : LoginDelegate {
                    override val loginPickerView = loginPickerView
                    override val onManageLogins = {}
                },
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = { },
            ),
        )
        val selectLoginPrompt = mock<PromptRequest.SelectLoginPrompt>()
        whenever(loginPickerView.isPromptDisplayed).thenReturn(true)
        feature.loginPicker = loginPicker
        feature.activePromptRequest = selectLoginPrompt

        // when
        val result = feature.dismissSelectPrompts()

        // then
        verify(feature.loginPicker!!).dismissCurrentLoginSelect(selectLoginPrompt)
        assertEquals(true, result)
        assertEquals(null, feature.activePromptRequest)
    }

    @Test
    fun `GIVEN strongPasswordPromptView is visible WHEN dismissSelectPrompts THEN dismissCurrentSuggestStrongPassword called and true returned`() = runTest(testDispatcher) {
        // given
        val strongPasswordPromptViewListener: StrongPasswordPromptViewListener = mock()
        val strongPasswordPromptViewListenerView: PasswordPromptView = mock()
        val feature = spy(
            PromptFeature(
                mock<Activity>(),
                store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                suggestStrongPasswordDelegate = object : SuggestStrongPasswordDelegate {
                    override val strongPasswordPromptViewListenerView =
                        strongPasswordPromptViewListenerView
                },
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = { },
            ),
        )
        val selectLoginPrompt = mock<PromptRequest.SelectLoginPrompt>()
        whenever(strongPasswordPromptViewListenerView.isPromptDisplayed).thenReturn(true)
        feature.strongPasswordPromptViewListener = strongPasswordPromptViewListener
        feature.activePromptRequest = selectLoginPrompt

        // when
        val result = feature.dismissSelectPrompts()

        // then
        verify(feature.strongPasswordPromptViewListener!!).dismissCurrentSuggestStrongPassword(
            selectLoginPrompt,
        )
        assertEquals(true, result)
        assertEquals(null, feature.activePromptRequest)
    }

    @Test
    fun `GIVEN saveLoginPrompt is visible WHEN prompt is removed from state THEN dismiss saveLoginPrompt`() = runTest(testDispatcher) {
        // given
        val loginUsername = "username"
        val loginPassword = "password"
        val entry: LoginEntry = mock()
        `when`(entry.username).thenReturn(loginUsername)
        `when`(entry.password).thenReturn(loginPassword)
        val promptRequest = PromptRequest.SaveLoginPrompt(2, listOf(entry), { }, { })
        val saveLoginPrompt: SaveLoginDialogFragment = mock()

        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, promptRequest))

        val feature = spy(
            PromptFeature(
                mock<Activity>(),
                store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                exitFullscreenUsecase = mock(),
                loginValidationDelegate = mock(),
                isSaveLoginEnabled = { true },
                hideUpdateFragmentAfterSavingGeneratedPassword = { _, _ -> false },
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                mainDispatcher = testDispatcher,
                onNeedToRequestPermissions = { },
            ),
        )

        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()
        feature.activePrompt = WeakReference(saveLoginPrompt)
        feature.activePromptRequest = promptRequest

        // when
        store.dispatch(ContentAction.ConsumePromptRequestAction(tabId, promptRequest))
        testDispatcher.scheduler.advanceUntilIdle()

        // then
        verify(saveLoginPrompt).dismissAllowingStateLoss()
    }

    @Test
    fun `GIVEN isSaveLoginEnabled is false WHEN saveLoginPrompt request is handled THEN dismiss saveLoginPrompt`() = runTest(testDispatcher) {
        val promptRequest = spy(
            PromptRequest.SaveLoginPrompt(
                hint = 2,
                logins = emptyList(),
                onConfirm = {},
                onDismiss = {},
            ),
        )
        val feature = spy(
            PromptFeature(
                activity = mock(),
                store = store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                isSaveLoginEnabled = { false },
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = {},
            ),
        )
        val session = tab()!!

        feature.handleDialogsRequest(promptRequest, session)

        verify(feature).dismissDialogRequest(promptRequest, session)
    }

    @Test
    fun `GIVEN hideUpdateFragmentAfterSavingGeneratedPassword is true WHEN saveLoginPrompt request is handled THEN dismiss saveLoginPrompt`() = runTest(testDispatcher) {
        val loginUsername = "username"
        val loginPassword = "password"
        val entry: LoginEntry = mock()
        `when`(entry.username).thenReturn(loginUsername)
        `when`(entry.password).thenReturn(loginPassword)
        val promptRequest = PromptRequest.SaveLoginPrompt(2, listOf(entry), { }, { })

        var onRemoveLastSavedPasswordCalled = false
        val feature = spy(
            PromptFeature(
                activity = mock(),
                store = store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                loginValidationDelegate = mock(),
                isSaveLoginEnabled = { true },
                hideUpdateFragmentAfterSavingGeneratedPassword = { _, _ -> true },
                removeLastSavedGeneratedPassword = { onRemoveLastSavedPasswordCalled = true },
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = {},
            ),
        )
        val session = tab()!!

        feature.handleDialogsRequest(promptRequest, session)

        assertTrue(onRemoveLastSavedPasswordCalled)
        verify(feature).dismissDialogRequest(promptRequest, session)
    }

    @Test
    fun `GIVEN loginValidationDelegate is null WHEN saveLoginPrompt request is handled THEN dismiss saveLoginPrompt`() = runTest(testDispatcher) {
        val promptRequest = spy(
            PromptRequest.SaveLoginPrompt(
                hint = 2,
                logins = emptyList(),
                onConfirm = {},
                onDismiss = {},
            ),
        )
        val feature = spy(
            PromptFeature(
                activity = mock(),
                store = store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                isSaveLoginEnabled = { true },
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = {},
            ),
        )
        val session = tab()!!

        feature.handleDialogsRequest(promptRequest, session)

        verify(feature).dismissDialogRequest(promptRequest, session)
    }

    @Test
    fun `WHEN dismissDialogRequest is called THEN dismiss and consume the prompt request`() = runTest(testDispatcher) {
        val tab = createTab("https://www.mozilla.org", id = tabId)
        val captureActionsMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()

        val store = BrowserStore(
            BrowserState(
                tabs = listOf(tab),
                customTabs = listOf(
                    createCustomTab("https://www.mozilla.org", id = "custom-tab"),
                ),
                selectedTabId = tabId,
            ),
            middleware = listOf(captureActionsMiddleware) + EngineMiddleware.create(mock()),
        )

        val feature = PromptFeature(
            activity = mock(),
            store = store,
            fragmentManager = fragmentManager,
            tabsUseCases = mock(),
            fileUploadsDirCleaner = mock(),
            isEmailMaskFeatureEnabled = { false },
            isSuggestEmailMaskEnabled = { false },
            onNeedToRequestPermissions = {},
        )

        var onDismissWasCalled = false
        val promptRequest = PromptRequest.SaveLoginPrompt(
            hint = 2,
            logins = emptyList(),
            onConfirm = {},
            onDismiss = { onDismissWasCalled = true },
        )

        feature.dismissDialogRequest(promptRequest, tab)

        captureActionsMiddleware.assertFirstAction(ContentAction.ConsumePromptRequestAction::class) { action ->
            assertEquals(tab.id, action.sessionId)
            assertEquals(promptRequest, action.promptRequest)
        }

        assertTrue(onDismissWasCalled)
    }

    @Test
    fun `GIVEN loginPickerView is not visible WHEN dismissSelectPrompts THEN dismissCurrentLoginSelect called and false returned`() = runTest(testDispatcher) {
        // given
        val loginPickerView: AutocompletePrompt<Login> = mock()
        val feature = spy(
            PromptFeature(
                mock<Activity>(),
                store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                loginDelegate = object : LoginDelegate {
                    override val loginPickerView = loginPickerView
                    override val onManageLogins = {}
                },
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = { },
            ),
        )
        val selectLoginPrompt = mock<PromptRequest.SelectLoginPrompt>()
        whenever(loginPickerView.isPromptDisplayed).thenReturn(false)
        feature.loginPicker = loginPicker
        feature.activePromptRequest = selectLoginPrompt

        // when
        val result = feature.dismissSelectPrompts()

        // then
        assertEquals(false, result)
        assertNotNull(feature.activePromptRequest)
    }

    @Test
    fun `GIVEN PromptFeature WHEN onBackPressed THEN dismissSelectPrompts is called`() = runTest(testDispatcher) {
        // given
        val loginPickerView: AutocompletePrompt<Login> = mock()
        val feature = spy(
            PromptFeature(
                mock<Activity>(),
                store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                loginDelegate = object : LoginDelegate {
                    override val loginPickerView = loginPickerView
                    override val onManageLogins = {}
                },
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = { },
            ),
        )
        val selectLoginPrompt = mock<PromptRequest.SelectLoginPrompt>()
        whenever(loginPickerView.isPromptDisplayed).thenReturn(true)
        feature.loginPicker = loginPicker
        feature.activePromptRequest = selectLoginPrompt

        // when
        val result = feature.onBackPressed()

        // then
        verify(feature).dismissSelectPrompts()
        assertEquals(true, result)
    }

    @Test
    fun `Calling dismissSelectPrompts should dismiss the login picker if the login prompt is active`() = runTest(testDispatcher) {
        val loginPickerView: AutocompletePrompt<Login> = mock()
        val feature = spy(
            PromptFeature(
                mock<Activity>(),
                store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                loginDelegate = object : LoginDelegate {
                    override val loginPickerView = loginPickerView
                    override val onManageLogins = {}
                },
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = { },
            ),
        )
        val selectLoginPrompt = mock<PromptRequest.SelectLoginPrompt>()
        whenever(loginPickerView.isPromptDisplayed).thenReturn(true)

        feature.loginPicker = loginPicker
        feature.activePromptRequest = mock<SingleChoice>()
        feature.dismissSelectPrompts()
        verify(feature.loginPicker!!, never()).dismissCurrentLoginSelect(any())

        feature.loginPicker = loginPicker
        feature.activePromptRequest = selectLoginPrompt
        feature.dismissSelectPrompts()
        verify(feature.loginPicker!!).dismissCurrentLoginSelect(selectLoginPrompt)
    }

    @Test
    fun `GIVEN creditCardPickerView is visible WHEN dismissSelectPrompts is called THEN dismissSelectCreditCardRequest returns true`() = runTest(testDispatcher) {
        val creditCardPickerView: AutocompletePrompt<CreditCardEntry> = mock()
        val feature = spy(
            PromptFeature(
                mock<Activity>(),
                store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                creditCardDelegate = object : CreditCardDelegate {
                    override val creditCardPickerView = creditCardPickerView
                    override val onSelectCreditCard = {}
                    override val onManageCreditCards = {}
                },
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = { },
            ),
        )
        val selectCreditCardRequest = mock<PromptRequest.SelectCreditCard>()
        feature.creditCardPicker = creditCardPicker
        feature.activePromptRequest = selectCreditCardRequest

        whenever(creditCardPickerView.isPromptDisplayed).thenReturn(true)

        val result = feature.dismissSelectPrompts()

        verify(feature.creditCardPicker!!).dismissSelectCreditCardRequest(selectCreditCardRequest)
        assertEquals(true, result)
        assertEquals(null, feature.activePromptRequest)
    }

    @Test
    fun `GIVEN creditCardPickerView is not visible WHEN dismissSelectPrompts is called THEN dismissSelectPrompt returns false`() = runTest(testDispatcher) {
        val creditCardPickerView: AutocompletePrompt<CreditCardEntry> = mock()
        val feature = spy(
            PromptFeature(
                mock<Activity>(),
                store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                creditCardDelegate = object : CreditCardDelegate {
                    override val creditCardPickerView = creditCardPickerView
                    override val onSelectCreditCard = {}
                    override val onManageCreditCards = {}
                },
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = { },
            ),
        )
        val selectCreditCardRequest = mock<PromptRequest.SelectCreditCard>()
        feature.creditCardPicker = creditCardPicker
        feature.activePromptRequest = selectCreditCardRequest

        whenever(creditCardPickerView.isPromptDisplayed).thenReturn(false)

        val result = feature.dismissSelectPrompts()

        assertEquals(false, result)
        assertNotNull(feature.activePromptRequest)
    }

    @Test
    fun `GIVEN an active select credit card request WHEN onBackPressed is called THEN dismissSelectPrompts is called`() = runTest(testDispatcher) {
        val creditCardPickerView: AutocompletePrompt<CreditCardEntry> = mock()
        val feature = spy(
            PromptFeature(
                mock<Activity>(),
                store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                creditCardDelegate = object : CreditCardDelegate {
                    override val creditCardPickerView = creditCardPickerView
                    override val onSelectCreditCard = {}
                    override val onManageCreditCards = {}
                },
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = { },
            ),
        )
        val selectCreditCardRequest = mock<PromptRequest.SelectCreditCard>()
        feature.creditCardPicker = creditCardPicker
        feature.activePromptRequest = selectCreditCardRequest

        whenever(creditCardPickerView.isPromptDisplayed).thenReturn(true)

        val result = feature.onBackPressed()

        verify(feature).dismissSelectPrompts()
        assertEquals(true, result)
        assertEquals(null, feature.activePromptRequest)
    }

    @Test
    fun `WHEN dismissSelectPrompts is called THEN the active credit card picker should be dismissed`() = runTest(testDispatcher) {
        val creditCardPickerView: AutocompletePrompt<CreditCardEntry> = mock()
        val feature = spy(
            PromptFeature(
                mock<Activity>(),
                store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                creditCardDelegate = object : CreditCardDelegate {
                    override val creditCardPickerView = creditCardPickerView
                    override val onSelectCreditCard = {}
                    override val onManageCreditCards = {}
                },
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = { },
            ),
        )
        feature.creditCardPicker = creditCardPicker
        feature.activePromptRequest = mock<SingleChoice>()

        whenever(creditCardPickerView.isPromptDisplayed).thenReturn(true)

        feature.dismissSelectPrompts()
        verify(feature.creditCardPicker!!, never()).dismissSelectCreditCardRequest(any())

        val selectCreditCardRequest = mock<PromptRequest.SelectCreditCard>()
        feature.activePromptRequest = selectCreditCardRequest

        feature.dismissSelectPrompts()

        verify(feature.creditCardPicker!!).dismissSelectCreditCardRequest(selectCreditCardRequest)
    }

    @Test
    fun `WHEN dismissSelectPrompts is called THEN the active addressPicker dismiss should be called`() = runTest(testDispatcher) {
        val addressPickerView: AutocompletePrompt<Address> = mock()
        val addressDelegate: AddressDelegate = mock()
        val feature = spy(
            PromptFeature(
                mock<Activity>(),
                store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                addressDelegate = addressDelegate,
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = { },
            ),
        )
        feature.addressPicker = addressPicker
        feature.activePromptRequest = mock<SingleChoice>()

        whenever(addressDelegate.addressPickerView).thenReturn(addressPickerView)
        whenever(addressPickerView.isPromptDisplayed).thenReturn(true)

        feature.dismissSelectPrompts()
        verify(feature.addressPicker!!, never()).dismissSelectAddressRequest(any())

        val selectAddressPromptRequest = mock<PromptRequest.SelectAddress>()
        feature.activePromptRequest = selectAddressPromptRequest

        feature.dismissSelectPrompts()

        verify(feature.addressPicker!!).dismissSelectAddressRequest(selectAddressPromptRequest)

        assertTrue(tab()!!.content.promptRequests.isEmpty())
    }

    @Test
    fun `GIVEN addressPickerView is not visible WHEN dismissSelectPrompts is called THEN dismissSelectPrompts returns false`() = runTest(testDispatcher) {
        val addressPickerView: AutocompletePrompt<Address> = mock()
        val addressDelegate: AddressDelegate = mock()
        val feature = spy(
            PromptFeature(
                mock<Activity>(),
                store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                addressDelegate = addressDelegate,
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = { },
            ),
        )
        val selectAddressRequest = mock<PromptRequest.SelectAddress>()
        feature.addressPicker = addressPicker
        feature.activePromptRequest = selectAddressRequest

        whenever(addressDelegate.addressPickerView).thenReturn(addressPickerView)
        whenever(addressPickerView.isPromptDisplayed).thenReturn(false)

        val result = feature.dismissSelectPrompts()

        assertEquals(false, result)
        assertNotNull(feature.activePromptRequest)
    }

    @Test
    fun `Calling onCancel will consume promptRequest`() = runTest(testDispatcher) {
        val feature =
            PromptFeature(
                activity = mock(),
                store = store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = { },
            )

        val singleChoiceRequest = SingleChoice(arrayOf(), {}, {})
        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, singleChoiceRequest))

        assertEquals(1, tab()!!.content.promptRequests.size)
        assertEquals(singleChoiceRequest, tab()!!.content.promptRequests[0])
        feature.onCancel(tabId, singleChoiceRequest.uid)

        assertTrue(tab()?.content?.promptRequests?.isEmpty() ?: false)
    }

    @Test
    fun `Selecting an item in a single choice dialog will consume promptRequest`() = runTest(testDispatcher) {
        val feature =
            PromptFeature(
                activity = mock(),
                store = store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                exitFullscreenUsecase = mock(),
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = { },
            )
        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        val singleChoiceRequest = SingleChoice(arrayOf(), {}, {})
        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, singleChoiceRequest))

        assertEquals(1, tab()!!.content.promptRequests.size)
        assertEquals(singleChoiceRequest, tab()!!.content.promptRequests[0])
        feature.onConfirm(tabId, singleChoiceRequest.uid, mock<Choice>())

        assertTrue(tab()!!.content.promptRequests.isEmpty())
    }

    @Test
    fun `Selecting an item in a menu choice dialog will consume promptRequest`() = runTest(testDispatcher) {
        val feature =
            PromptFeature(
                activity = mock(),
                store = store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                exitFullscreenUsecase = mock(),
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = { },
            )
        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        val menuChoiceRequest = MenuChoice(arrayOf(), {}, {})
        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, menuChoiceRequest))

        assertEquals(1, tab()!!.content.promptRequests.size)
        assertEquals(menuChoiceRequest, tab()!!.content.promptRequests[0])
        feature.onConfirm(tabId, menuChoiceRequest.uid, mock<Choice>())

        assertTrue(tab()!!.content.promptRequests.isEmpty())
    }

    @Test
    fun `Selecting items on multiple choice dialog will consume promptRequest`() = runTest(testDispatcher) {
        val feature =
            PromptFeature(
                activity = mock(),
                store = store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                exitFullscreenUsecase = mock(),
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = { },
            )
        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        val multipleChoiceRequest = MultipleChoice(arrayOf(), {}, {})
        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, multipleChoiceRequest))

        assertEquals(1, tab()!!.content.promptRequests.size)
        assertEquals(multipleChoiceRequest, tab()!!.content.promptRequests[0])
        feature.onConfirm(tabId, multipleChoiceRequest.uid, arrayOf<Choice>())

        assertTrue(tab()!!.content.promptRequests.isEmpty())
    }

    @Test
    fun `onNoMoreDialogsChecked will consume promptRequest`() = runTest(testDispatcher) {
        val feature =
            PromptFeature(
                activity = mock(),
                store = store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                exitFullscreenUsecase = mock(),
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = { },
            )

        var onShowNoMoreAlertsWasCalled = false
        var onDismissWasCalled = false

        val promptRequest = Alert(
            "title",
            "message",
            false,
            { onShowNoMoreAlertsWasCalled = true },
            { onDismissWasCalled = true },
        )

        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, promptRequest))
        feature.onConfirm(tabId, promptRequest.uid, false)
        assertTrue(tab()!!.content.promptRequests.isEmpty())
        assertTrue(onShowNoMoreAlertsWasCalled)

        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, promptRequest))
        feature.onCancel(tabId, promptRequest.uid)
        assertTrue(onDismissWasCalled)
    }

    @Test
    fun `Calling onCancel with an alert request will consume promptRequest and call onDismiss`() = runTest(testDispatcher) {
        val feature =
            PromptFeature(
                activity = mock(),
                store = store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                exitFullscreenUsecase = mock(),
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = { },
            )
        var onDismissWasCalled = false
        val promptRequest = Alert("title", "message", false, {}, { onDismissWasCalled = true })

        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, promptRequest))
        feature.onCancel(tabId, promptRequest.uid)
        assertTrue(onDismissWasCalled)
        assertTrue(tab()!!.content.promptRequests.isEmpty())
    }

    @Test
    fun `onConfirmTextPrompt will consume promptRequest`() = runTest(testDispatcher) {
        val feature =
            PromptFeature(
                activity = mock(),
                store = store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                exitFullscreenUsecase = mock(),
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = { },
            )
        var onConfirmWasCalled = false
        var onDismissWasCalled = false

        val promptRequest = TextPrompt(
            "title",
            "message",
            "input",
            false,
            { _, _ -> onConfirmWasCalled = true },
            { onDismissWasCalled = true },
        )

        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, promptRequest))
        feature.onConfirm(tabId, promptRequest.uid, false to "")
        assertTrue(tab()!!.content.promptRequests.isEmpty())
        assertTrue(onConfirmWasCalled)

        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, promptRequest))
        feature.onCancel(tabId, promptRequest.uid)
        assertTrue(tab()!!.content.promptRequests.isEmpty())
        assertTrue(onDismissWasCalled)
    }

    @Test
    fun `Calling onCancel with an TextPrompt request will consume promptRequest and call onDismiss`() = runTest(testDispatcher) {
        val feature =
            PromptFeature(
                activity = mock(),
                store = store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                exitFullscreenUsecase = mock(),
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = { },
            )
        var onDismissWasCalled = false

        val promptRequest = TextPrompt(
            "title",
            "message",
            "value",
            false,
            { _, _ -> },
            { onDismissWasCalled = true },
        )

        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, promptRequest))

        feature.onCancel(tabId, promptRequest.uid)
        assertTrue(tab()!!.content.promptRequests.isEmpty())
        assertTrue(onDismissWasCalled)
    }

    @Test
    fun `selecting a time will consume promptRequest`() = runTest(testDispatcher) {
        val timeSelectionTypes = listOf(
            PromptRequest.TimeSelection.Type.DATE,
            PromptRequest.TimeSelection.Type.DATE_AND_TIME,
            PromptRequest.TimeSelection.Type.TIME,
            PromptRequest.TimeSelection.Type.MONTH,
        )

        timeSelectionTypes.forEach { type ->
            val feature = PromptFeature(
                activity = mock(),
                store = store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                exitFullscreenUsecase = mock(),
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = { },
            )
            var onClearWasCalled = false
            var selectedDate: Date? = null
            val promptRequest = PromptRequest.TimeSelection(
                "title",
                Date(0),
                null,
                null,
                null,
                type,
                { date -> selectedDate = date },
                { onClearWasCalled = true },
                { },
            )

            feature.start()
        testDispatcher.scheduler.advanceUntilIdle()
            store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, promptRequest))

            val now = Date()
            feature.onConfirm(tabId, promptRequest.uid, now)
            assertTrue(tab()!!.content.promptRequests.isEmpty())

            assertEquals(now, selectedDate)
            store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, promptRequest))

            feature.onClear(tabId, promptRequest.uid)
            assertTrue(onClearWasCalled)
            feature.stop()
        }
    }

    @Test(expected = InvalidParameterException::class)
    fun `calling handleDialogsRequest with invalid type will throw an exception`() = runTest(testDispatcher) {
        val feature =
            PromptFeature(
                activity = mock(),
                store = store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = { },
            )
        feature.handleDialogsRequest(mock<PromptRequest.File>(), mock())
    }

    @Test
    fun `onActivityResult with RESULT_OK and isMultipleFilesSelection false will consume PromptRequest`() = runTest(testDispatcher) {
        var onSingleFileSelectionWasCalled = false

        val onSingleFileSelection: (Context, Uri) -> Unit = { _, _ ->
            onSingleFileSelectionWasCalled = true
        }

        val filePickerRequest =
            PromptRequest.File(emptyArray(), false, onSingleFileSelection, { _, _ -> }) { }
        val activity = mock<Activity>()
        doReturn(mock<ContentResolver>()).`when`(activity).contentResolver

        val feature =
            PromptFeature(
                activity = activity,
                store = store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = { },
            )
        val intent = Intent()

        intent.data = mock()
        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, filePickerRequest))

        feature.onActivityResult(FILE_PICKER_ACTIVITY_REQUEST_CODE, intent, RESULT_OK)
        assertTrue(onSingleFileSelectionWasCalled)
        assertTrue(tab()!!.content.promptRequests.isEmpty())
    }

    @Test
    fun `onActivityResult with RESULT_OK and isMultipleFilesSelection true will consume PromptRequest of the actual session`() = runTest(testDispatcher) {
        var onMultipleFileSelectionWasCalled = false

        val onMultipleFileSelection: (Context, Array<Uri>) -> Unit = { _, _ ->
            onMultipleFileSelectionWasCalled = true
        }

        val filePickerRequest =
            PromptRequest.File(emptyArray(), true, { _, _ -> }, onMultipleFileSelection) {}
        val activity = mock<Activity>()
        doReturn(mock<ContentResolver>()).`when`(activity).contentResolver

        val feature =
            PromptFeature(
                activity = activity,
                store = store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = { },
            )
        val intent = Intent()

        intent.clipData = mock()
        val item = mock<ClipData.Item>()

        doReturn(mock<Uri>()).`when`(item).uri

        intent.clipData?.apply {
            doReturn(1).`when`(this).itemCount
            doReturn(item).`when`(this).getItemAt(0)
        }

        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, filePickerRequest))

        feature.onActivityResult(FILE_PICKER_ACTIVITY_REQUEST_CODE, intent, RESULT_OK)
        assertTrue(onMultipleFileSelectionWasCalled)
        assertTrue(tab()!!.content.promptRequests.isEmpty())
    }

    @Test
    fun `onActivityResult with RESULT_CANCELED will consume PromptRequest call onDismiss`() = runTest(testDispatcher) {
        var onDismissWasCalled = false

        val filePickerRequest =
            PromptRequest.File(emptyArray(), true, { _, _ -> }, { _, _ -> }) {
                onDismissWasCalled = true
            }

        val feature =
            PromptFeature(
                activity = mock(),
                store = store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = { },
            )
        val intent = Intent()

        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, filePickerRequest))

        feature.onActivityResult(FILE_PICKER_ACTIVITY_REQUEST_CODE, intent, RESULT_CANCELED)
        assertTrue(onDismissWasCalled)
        assertTrue(tab()!!.content.promptRequests.isEmpty())
    }

    @Test
    fun `WHEN onActivityResult is called with PIN_REQUEST and RESULT_OK THEN onAuthSuccess) is called`() = runTest(testDispatcher) {
        val creditCardPickerView: AutocompletePrompt<CreditCardEntry> = mock()
        val feature =
            PromptFeature(
                activity = mock(),
                store = store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                isCreditCardAutofillEnabled = { true },
                creditCardDelegate = object : CreditCardDelegate {
                    override val creditCardPickerView = creditCardPickerView
                    override val onSelectCreditCard = {}
                    override val onManageCreditCards = {}
                },
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = { },
            )
        feature.creditCardPicker = creditCardPicker
        val intent = Intent()

        feature.onActivityResult(PromptFeature.PIN_REQUEST, intent, RESULT_OK)

        verify(creditCardPicker).onAuthSuccess()
    }

    @Test
    fun `WHEN onActivityResult is called with PIN_REQUEST and RESULT_CANCELED THEN onAuthFailure is called`() = runTest(testDispatcher) {
        val creditCardPickerView: AutocompletePrompt<CreditCardEntry> = mock()
        val feature =
            PromptFeature(
                activity = mock(),
                store = store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                isCreditCardAutofillEnabled = { true },
                creditCardDelegate = object : CreditCardDelegate {
                    override val creditCardPickerView = creditCardPickerView
                    override val onSelectCreditCard = {}
                    override val onManageCreditCards = {}
                },
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = { },
            )
        feature.creditCardPicker = creditCardPicker
        val intent = Intent()

        feature.onActivityResult(PromptFeature.PIN_REQUEST, intent, RESULT_CANCELED)

        verify(creditCardPicker).onAuthFailure()
    }

    @Test
    fun `GIVEN user successfully authenticates by biometric prompt WHEN onBiometricResult is called THEN onAuthSuccess is called`() = runTest(testDispatcher) {
        val creditCardPickerView: AutocompletePrompt<CreditCardEntry> = mock()
        val feature =
            PromptFeature(
                activity = mock(),
                store = store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                isCreditCardAutofillEnabled = { true },
                creditCardDelegate = object : CreditCardDelegate {
                    override val creditCardPickerView = creditCardPickerView
                    override val onSelectCreditCard = {}
                    override val onManageCreditCards = {}
                },
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = { },
            )
        feature.creditCardPicker = creditCardPicker

        feature.onBiometricResult(isAuthenticated = true)

        verify(creditCardPicker).onAuthSuccess()
    }

    @Test
    fun `GIVEN user fails to authenticate by biometric prompt WHEN onBiometricResult is called THEN onAuthFailure) is called`() = runTest(testDispatcher) {
        val creditCardPickerView: AutocompletePrompt<CreditCardEntry> = mock()
        val feature =
            PromptFeature(
                activity = mock(),
                store = store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                isCreditCardAutofillEnabled = { true },
                creditCardDelegate = object : CreditCardDelegate {
                    override val creditCardPickerView = creditCardPickerView
                    override val onSelectCreditCard = {}
                    override val onManageCreditCards = {}
                },
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = { },
            )
        feature.creditCardPicker = creditCardPicker

        feature.onBiometricResult(isAuthenticated = false)

        verify(creditCardPicker).onAuthFailure()
    }

    @Test
    fun `Selecting a login confirms the request`() = runTest(testDispatcher) {
        var onDismissWasCalled = false
        var confirmedLogin: Login? = null

        val login =
            Login(guid = "A", origin = "https://www.mozilla.org", username = "username", password = "password")
        val login2 =
            Login(guid = "B", origin = "https://www.mozilla.org", username = "username2", password = "password")

        val loginPickerRequest = PromptRequest.SelectLoginPrompt(
            logins = listOf(login, login2),
            generatedPassword = null,
            onConfirm = { confirmedLogin = it },
            onDismiss = { onDismissWasCalled = true },
        )

        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, loginPickerRequest))

        loginPickerRequest.onConfirm(login)

        assertEquals(confirmedLogin, login)

        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, loginPickerRequest))
        loginPickerRequest.onDismiss()

        assertTrue(onDismissWasCalled)
    }

    @Test
    fun `WHEN a credit card is selected THEN confirm the prompt request with the selected credit card`() = runTest(testDispatcher) {
        val creditCard = CreditCardEntry(
            guid = "id",
            name = "Banana Apple",
            number = "4111111111111110",
            expiryMonth = "5",
            expiryYear = "2030",
            cardType = "amex",
        )
        var onDismissCalled = false
        var onConfirmCalled = false
        var confirmedCreditCard: CreditCardEntry? = null

        val selectCreditCardRequest = PromptRequest.SelectCreditCard(
            creditCards = listOf(creditCard),
            onDismiss = {
                onDismissCalled = true
            },
            onConfirm = {
                confirmedCreditCard = it
                onConfirmCalled = true
            },
        )

        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, selectCreditCardRequest))

        selectCreditCardRequest.onConfirm(creditCard)

        assertEquals(creditCard, confirmedCreditCard)
        assertTrue(onConfirmCalled)

        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, selectCreditCardRequest))
        selectCreditCardRequest.onDismiss()

        assertTrue(onDismissCalled)
    }

    @Test
    fun `Calling onConfirmAuthentication will consume promptRequest`() = runTest(testDispatcher) {
        val feature =
            PromptFeature(
                activity = mock(),
                store = store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                exitFullscreenUsecase = mock(),
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = { },
            )

        var onConfirmWasCalled = false
        var onDismissWasCalled = false

        val promptRequest = Authentication(
            uri = "https://www.mozilla.org",
            title = "title",
            message = "message",
            userName = "username",
            password = "password",
            method = HOST,
            level = NONE,
            onlyShowPassword = false,
            previousFailed = false,
            isCrossOrigin = false,
            onConfirm = { _, _ -> onConfirmWasCalled = true },
            onDismiss = { onDismissWasCalled = true },
        )

        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, promptRequest))

        feature.onConfirm(tabId, promptRequest.uid, "" to "")
        assertTrue(tab()!!.content.promptRequests.isEmpty())
        assertTrue(onConfirmWasCalled)

        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, promptRequest))

        feature.onCancel(tabId, promptRequest.uid)
        assertTrue(onDismissWasCalled)
    }

    @Test
    fun `Calling onConfirm on a BeforeUnload request will consume promptRequest`() = runTest(testDispatcher) {
        val fragment: Fragment = mock()
        whenever(fragment.getString(R.string.mozac_feature_prompt_before_unload_dialog_title)).thenReturn(
            "",
        )
        whenever(fragment.getString(R.string.mozac_feature_prompt_before_unload_dialog_body)).thenReturn(
            "",
        )
        whenever(fragment.getString(R.string.mozac_feature_prompts_before_unload_stay)).thenReturn("")
        whenever(fragment.getString(R.string.mozac_feature_prompts_before_unload_leave)).thenReturn(
            "",
        )

        val feature =
            PromptFeature(
                fragment = fragment,
                store = store,
                fileUploadsDirCleaner = mock(),
                tabsUseCases = mock(),
                fragmentManager = fragmentManager,
                exitFullscreenUsecase = mock(),
            ) { }

        var onLeaveWasCalled = false

        val promptRequest = PromptRequest.BeforeUnload(
            title = "title",
            onLeave = { onLeaveWasCalled = true },
            onStay = { },
            onDismiss = { },
        )

        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, promptRequest))

        feature.onConfirm(tabId, promptRequest.uid, "" to "")
        assertTrue(tab()!!.content.promptRequests.isEmpty())
        assertTrue(onLeaveWasCalled)
    }

    @Test
    fun `Calling onCancel on a authentication request will consume promptRequest and call onDismiss`() = runTest(testDispatcher) {
        val feature =
            PromptFeature(
                activity = mock(),
                store = store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                exitFullscreenUsecase = mock(),
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = { },
            )
        var onDismissWasCalled = false

        val promptRequest = Authentication(
            uri = "https://www.mozilla.org",
            title = "title",
            message = "message",
            userName = "username",
            password = "password",
            method = HOST,
            level = NONE,
            onlyShowPassword = false,
            previousFailed = false,
            isCrossOrigin = false,
            onConfirm = { _, _ -> },
            onDismiss = { onDismissWasCalled = true },
        )

        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, promptRequest))

        feature.onCancel(tabId, promptRequest.uid)
        assertTrue(tab()!!.content.promptRequests.isEmpty())
        assertTrue(onDismissWasCalled)
    }

    @Test
    fun `Calling onConfirm on a color request will consume promptRequest`() = runTest(testDispatcher) {
        val feature =
            PromptFeature(
                activity = mock(),
                store = store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                exitFullscreenUsecase = mock(),
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = { },
            )

        var onConfirmWasCalled = false
        var onDismissWasCalled = false

        val promptRequest = Color(
            "#e66465",
            {
                onConfirmWasCalled = true
            },
        ) {
            onDismissWasCalled = true
        }

        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, promptRequest))

        feature.onConfirm(tabId, promptRequest.uid, "#f6b73c")
        assertTrue(tab()!!.content.promptRequests.isEmpty())
        assertTrue(onConfirmWasCalled)

        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, promptRequest))

        feature.onCancel(tabId, promptRequest.uid)
        assertTrue(tab()!!.content.promptRequests.isEmpty())
        assertTrue(onDismissWasCalled)
    }

    @Test
    fun `Calling onConfirm on a popup request will consume promptRequest`() = runTest(testDispatcher) {
        val fragment: Fragment = mock()
        whenever(fragment.getString(R.string.mozac_feature_prompts_popup_dialog_title)).thenReturn("")
        whenever(fragment.getString(R.string.mozac_feature_prompts_allow)).thenReturn("")
        whenever(fragment.getString(R.string.mozac_feature_prompts_deny)).thenReturn("")

        val feature =
            PromptFeature(
                fragment = fragment,
                store = store,
                tabsUseCases = mock(),
                fileUploadsDirCleaner = mock(),
                fragmentManager = fragmentManager,
                exitFullscreenUsecase = mock(),
            ) { }
        var onConfirmWasCalled = false

        val promptRequest = PromptRequest.Popup(
            "http://www.popuptest.com/",
            { onConfirmWasCalled = true },
            { },
        ) {}

        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, promptRequest))

        feature.onConfirm(tabId, promptRequest.uid, true)
        assertTrue(tab()!!.content.promptRequests.isEmpty())
        assertTrue(onConfirmWasCalled)
    }

    @Test
    fun `Calling onCancel on a popup request will consume promptRequest`() = runTest(testDispatcher) {
        val fragment: Fragment = mock()
        whenever(fragment.getString(R.string.mozac_feature_prompts_popup_dialog_title)).thenReturn("")
        whenever(fragment.getString(R.string.mozac_feature_prompts_allow)).thenReturn("")
        whenever(fragment.getString(R.string.mozac_feature_prompts_deny)).thenReturn("")

        val feature =
            PromptFeature(
                fragment = fragment,
                fileUploadsDirCleaner = mock(),
                store = store,
                tabsUseCases = mock(),
                fragmentManager = fragmentManager,
                exitFullscreenUsecase = mock(),
            ) { }
        var onCancelWasCalled = false

        val promptRequest = PromptRequest.Popup(
            "http://www.popuptest.com/",
            onAllow = { },
            onDeny = {
                onCancelWasCalled = true
            },
        )

        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, promptRequest))

        feature.onCancel(tabId, promptRequest.uid, true)
        assertTrue(tab()!!.content.promptRequests.isEmpty())
        assertTrue(onCancelWasCalled)
    }

    @Test
    fun `Calling onCancel on a BeforeUnload request will consume promptRequest`() = runTest(testDispatcher) {
        val fragment: Fragment = mock()
        whenever(fragment.getString(R.string.mozac_feature_prompt_before_unload_dialog_title)).thenReturn(
            "",
        )
        whenever(fragment.getString(R.string.mozac_feature_prompt_before_unload_dialog_body)).thenReturn(
            "",
        )
        whenever(fragment.getString(R.string.mozac_feature_prompts_before_unload_stay)).thenReturn("")
        whenever(fragment.getString(R.string.mozac_feature_prompts_before_unload_leave)).thenReturn(
            "",
        )

        val feature =
            PromptFeature(
                fragment = fragment,
                fileUploadsDirCleaner = mock(),
                store = store,
                tabsUseCases = mock(),
                fragmentManager = fragmentManager,
                exitFullscreenUsecase = mock(),
            ) { }
        var onCancelWasCalled = false

        val promptRequest = PromptRequest.BeforeUnload(
            title = "http://www.test.com/",
            onLeave = { },
            onStay = { onCancelWasCalled = true },
            onDismiss = { },
        )

        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, promptRequest))

        feature.onCancel(tabId, promptRequest.uid)
        assertTrue(tab()!!.content.promptRequests.isEmpty())
        assertTrue(onCancelWasCalled)
    }

    @Test
    fun `Calling onConfirm on a confirm request will consume promptRequest`() = runTest(testDispatcher) {
        val feature =
            PromptFeature(
                activity = mock(),
                store = store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                exitFullscreenUsecase = mock(),
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = { },
            )
        var onPositiveButtonWasCalled = false
        var onNegativeButtonWasCalled = false
        var onNeutralButtonWasCalled = false

        val onConfirmPositiveButton: (Boolean) -> Unit = {
            onPositiveButtonWasCalled = true
        }

        val onConfirmNegativeButton: (Boolean) -> Unit = {
            onNegativeButtonWasCalled = true
        }

        val onConfirmNeutralButton: (Boolean) -> Unit = {
            onNeutralButtonWasCalled = true
        }

        val promptRequest = PromptRequest.Confirm(
            "title",
            "message",
            false,
            "positive",
            "negative",
            "neutral",
            onConfirmPositiveButton,
            onConfirmNegativeButton,
            onConfirmNeutralButton,
        ) {}

        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, promptRequest))
        feature.onConfirm(tabId, promptRequest.uid, true to MultiButtonDialogFragment.ButtonType.POSITIVE)
        assertTrue(tab()!!.content.promptRequests.isEmpty())
        assertTrue(onPositiveButtonWasCalled)

        feature.promptAbuserDetector.resetJSAlertAbuseState()
        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, promptRequest))
        feature.onConfirm(tabId, promptRequest.uid, true to MultiButtonDialogFragment.ButtonType.NEGATIVE)
        assertTrue(tab()!!.content.promptRequests.isEmpty())
        assertTrue(onNegativeButtonWasCalled)

        feature.promptAbuserDetector.resetJSAlertAbuseState()
        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, promptRequest))
        feature.onConfirm(tabId, promptRequest.uid, true to MultiButtonDialogFragment.ButtonType.NEUTRAL)
        assertTrue(tab()!!.content.promptRequests.isEmpty())
        assertTrue(onNeutralButtonWasCalled)
    }

    @Test
    fun `Calling onCancel on a confirm request will consume promptRequest`() = runTest(testDispatcher) {
        val feature =
            PromptFeature(
                activity = mock(),
                store = store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                exitFullscreenUsecase = mock(),
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = { },
            )
        var onCancelWasCalled = false

        val onConfirm: (Boolean) -> Unit = { }

        val onDismiss: () -> Unit = {
            onCancelWasCalled = true
        }

        val promptRequest = PromptRequest.Confirm(
            "title",
            "message",
            false,
            "positive",
            "negative",
            "neutral",
            onConfirm,
            onConfirm,
            onConfirm,
            onDismiss,
        )

        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, promptRequest))

        feature.onCancel(tabId, promptRequest.uid)
        assertTrue(tab()!!.content.promptRequests.isEmpty())
        assertTrue(onCancelWasCalled)
    }

    @Test
    fun `When dialogs are being abused prompts are not allowed`() = runTest(testDispatcher) {
        val feature =
            PromptFeature(
                activity = mock(),
                store = store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                exitFullscreenUsecase = mock(),
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                mainDispatcher = testDispatcher,
                onNeedToRequestPermissions = { },
            )
        var onDismissWasCalled: Boolean
        val onDismiss = { onDismissWasCalled = true }
        val alertRequest = Alert("", "", false, {}, onDismiss)
        val textRequest = TextPrompt("", "", "", false, { _, _ -> }, onDismiss)
        val confirmRequest =
            PromptRequest.Confirm("", "", false, "+", "-", "", {}, {}, {}, onDismiss)

        val promptRequests = arrayOf<PromptRequest>(alertRequest, textRequest, confirmRequest)

        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        feature.promptAbuserDetector.userWantsMoreDialogs(false)
        testDispatcher.scheduler.advanceUntilIdle()

        promptRequests.forEach { request ->
            onDismissWasCalled = false
            store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, request))
            testDispatcher.scheduler.advanceUntilIdle()

            verify(fragmentManager, never()).beginTransaction()
            assertTrue(onDismissWasCalled)
        }
    }

    @Test
    fun `When dialogs are being abused but the page is refreshed prompts are allowed`() = runTest(testDispatcher) {
        val feature =
            PromptFeature(
                activity = mock(),
                store = store,
                fragmentManager = fragmentManager,
                mainDispatcher = testDispatcher,
                tabsUseCases = mock(),
                exitFullscreenUsecase = mock(),
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = { },
            )
        var onDismissWasCalled = false
        val onDismiss = { onDismissWasCalled = true }
        val alertRequest = Alert("", "", false, {}, onDismiss)

        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()
        feature.promptAbuserDetector.userWantsMoreDialogs(false)

        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, alertRequest))
        testDispatcher.scheduler.advanceUntilIdle()

        verify(fragmentManager, never()).beginTransaction()
        assertTrue(onDismissWasCalled)

        // Simulate reloading page
        store.dispatch(ContentAction.UpdateLoadingStateAction(tabId, true))
        testDispatcher.scheduler.advanceUntilIdle()

        store.dispatch(ContentAction.UpdateLoadingStateAction(tabId, false))
        testDispatcher.scheduler.advanceUntilIdle()

        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, alertRequest))
        testDispatcher.scheduler.advanceUntilIdle()

        assertTrue(feature.promptAbuserDetector.shouldShowMoreDialogs)
        verify(fragmentManager).beginTransaction()
    }

    @Test
    fun `User can stop further popups from being displayed on the current page`() = runTest(testDispatcher) {
        val feature = PromptFeature(
            activity = Robolectric.buildActivity(Activity::class.java).setup().get(),
            store = store,
            fragmentManager = fragmentManager,
            tabsUseCases = mock(),
            exitFullscreenUsecase = mock(),
            fileUploadsDirCleaner = mock(),
            isEmailMaskFeatureEnabled = { false },
            isSuggestEmailMaskEnabled = { false },
            mainDispatcher = testDispatcher,
            onNeedToRequestPermissions = { },
        )

        var onDenyCalled = false
        val onDeny = { onDenyCalled = true }
        val popupPrompt = PromptRequest.Popup("https://firefox.com", onAllow = { }, onDeny = onDeny)

        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()
        assertTrue(feature.promptAbuserDetector.shouldShowMoreDialogs)

        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, popupPrompt))
        testDispatcher.scheduler.advanceUntilIdle()

        verify(fragmentManager, times(1)).beginTransaction()
        feature.onCancel(tabId, popupPrompt.uid, true)
        assertFalse(feature.promptAbuserDetector.shouldShowMoreDialogs)
        assertTrue(onDenyCalled)

        onDenyCalled = false
        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, popupPrompt))
        testDispatcher.scheduler.advanceUntilIdle()

        verify(fragmentManager, times(1)).beginTransaction()
        assertFalse(feature.promptAbuserDetector.shouldShowMoreDialogs)
        assertTrue(onDenyCalled)
    }

    @Test
    fun `WHEN login autofill is enabled THEN the select login prompt is shown`() = runTest(testDispatcher) {
        val loginPickerView: AutocompletePrompt<Login> = mock()

        val login =
            Login(guid = "A", origin = "origin", username = "user123", password = "password123")

        val feature =
            PromptFeature(
                activity = mock<Activity>(),
                store = store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                exitFullscreenUsecase = mock(),
                isLoginAutofillEnabled = { true },
                loginDelegate = object : LoginDelegate {
                    override val loginPickerView = loginPickerView
                    override val onManageLogins = {}
                },
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                mainDispatcher = testDispatcher,
                onNeedToRequestPermissions = { },
            )
        feature.loginPicker = loginPicker
        val onLoginDismiss: () -> Unit = {}
        val onLoginConfirm: (Login) -> Unit = {}

        val selectLoginRequest =
            PromptRequest.SelectLoginPrompt(listOf(login), null, onLoginConfirm, onLoginDismiss)

        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, selectLoginRequest))
        testDispatcher.scheduler.advanceUntilIdle()

        verify(loginPicker).handleSelectLoginRequest(selectLoginRequest)
    }

    @Test
    fun `WHEN login autofill is disabled THEN the select login prompt is not shown`() = runTest(testDispatcher) {
        val loginPickerView: AutocompletePrompt<Login> = mock()

        val login =
            Login(guid = "A", origin = "origin", username = "user123", password = "password123")

        val feature =
            PromptFeature(
                activity = mock<Activity>(),
                store = store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                exitFullscreenUsecase = mock(),
                loginDelegate = object : LoginDelegate {
                    override val loginPickerView = loginPickerView
                    override val onManageLogins = {}
                },
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = { },
            )
        feature.loginPicker = loginPicker
        val onLoginDismiss: () -> Unit = {}
        val onLoginConfirm: (Login) -> Unit = {}

        val selectLoginRequest =
            PromptRequest.SelectLoginPrompt(listOf(login), null, onLoginConfirm, onLoginDismiss)

        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()
        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, selectLoginRequest))
        verify(loginPicker, never()).handleSelectLoginRequest(selectLoginRequest)
    }

    @Test
    fun `WHEN login prompt is received AND previous prompt is save login THEN the select login prompt is not shown`() = runTest(testDispatcher) {
        val loginPickerView: AutocompletePrompt<Login> = mock()

        val login =
            Login(guid = "A", origin = "origin", username = "user123", password = "password123")

        val feature =
            PromptFeature(
                activity = mock<Activity>(),
                store = store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                exitFullscreenUsecase = mock(),
                loginDelegate = object : LoginDelegate {
                    override val loginPickerView = loginPickerView
                    override val onManageLogins = {}
                },
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = { },
            )
        feature.loginPicker = loginPicker
        val onLoginDismiss: () -> Unit = {}
        val onLoginConfirm: (Login) -> Unit = {}
        val onLoginEntryConfirm: (LoginEntry) -> Unit = {}

        val selectLoginRequest =
            PromptRequest.SelectLoginPrompt(listOf(login), null, onLoginConfirm, onLoginDismiss)

        feature.previousPromptRequest = PromptRequest.SaveLoginPrompt(
            0,
            listOf(login.toEntry()),
            onLoginEntryConfirm,
            onLoginDismiss,
        )

        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()
        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, selectLoginRequest))
        verify(loginPicker, never()).handleSelectLoginRequest(selectLoginRequest)
    }

    @Test
    fun `When page is refreshed login dialog is dismissed`() = runTest(testDispatcher) {
        val loginPickerView: AutocompletePrompt<Login> = mock()
        val feature =
            PromptFeature(
                activity = mock(),
                store = store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                exitFullscreenUsecase = mock(),
                isLoginAutofillEnabled = { true },
                loginDelegate = object : LoginDelegate {
                    override val loginPickerView = loginPickerView
                    override val onManageLogins = {}
                },
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                mainDispatcher = testDispatcher,
                onNeedToRequestPermissions = { },
            )
        feature.loginPicker = loginPicker
        val onLoginDismiss: () -> Unit = {}
        val onLoginConfirm: (Login) -> Unit = {}

        val login = Login(guid = "A", origin = "origin", username = "username", password = "password")
        val selectLoginRequest =
            PromptRequest.SelectLoginPrompt(listOf(login), null, onLoginConfirm, onLoginDismiss)

        whenever(loginPickerView.isPromptDisplayed).thenReturn(true)

        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()
        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, selectLoginRequest))
        testDispatcher.scheduler.advanceUntilIdle()

        verify(loginPicker).handleSelectLoginRequest(selectLoginRequest)

        // Simulate reloading page
        store.dispatch(ContentAction.UpdateLoadingStateAction(tabId, true))
        testDispatcher.scheduler.advanceUntilIdle()

        verify(loginPicker).dismissCurrentLoginSelect(selectLoginRequest)
    }

    @Test
    fun `WHEN page is refreshed THEN credit card prompt is dismissed`() = runTest(testDispatcher) {
        val creditCardPickerView: AutocompletePrompt<CreditCardEntry> = mock()
        val feature =
            PromptFeature(
                activity = mock(),
                store = store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                exitFullscreenUsecase = mock(),
                isCreditCardAutofillEnabled = { true },
                creditCardDelegate = object : CreditCardDelegate {
                    override val creditCardPickerView = creditCardPickerView
                    override val onSelectCreditCard = {}
                    override val onManageCreditCards = {}
                },
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                mainDispatcher = testDispatcher,
                onNeedToRequestPermissions = { },
            )
        feature.creditCardPicker = creditCardPicker
        val onDismiss: () -> Unit = {}
        val onConfirm: (CreditCardEntry) -> Unit = {}
        val creditCard = CreditCardEntry(
            guid = "1",
            name = "Banana Apple",
            number = "4111111111111110",
            expiryMonth = "5",
            expiryYear = "2030",
            cardType = "",
        )
        val selectCreditCardRequest =
            PromptRequest.SelectCreditCard(listOf(creditCard), onConfirm, onDismiss)

        whenever(creditCardPickerView.isPromptDisplayed).thenReturn(true)

        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()
        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, selectCreditCardRequest))
        testDispatcher.scheduler.advanceUntilIdle()

        verify(creditCardPicker).handleSelectCreditCardRequest(selectCreditCardRequest)

        // Simulate reloading page
        store.dispatch(ContentAction.UpdateLoadingStateAction(tabId, true))
        testDispatcher.scheduler.advanceUntilIdle()

        verify(creditCardPicker).dismissSelectCreditCardRequest(selectCreditCardRequest)
    }

    @Test
    fun `Share prompt calls ShareDelegate`() = runTest(testDispatcher) {
        val delegate: ShareDelegate = mock()
        val activity: Activity = mock()
        val feature = spy(
            PromptFeature(
                activity,
                store,
                customTabId = "custom-tab",
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                shareDelegate = delegate,
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                mainDispatcher = testDispatcher,
                onNeedToRequestPermissions = { },
            ),
        )
        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        val promptRequest = PromptRequest.Share(ShareData("Title", "Text", null), {}, {}, {})
        store.dispatch(ContentAction.UpdatePromptRequestAction("custom-tab", promptRequest))
        testDispatcher.scheduler.advanceUntilIdle()

        verify(feature).onPromptRequested(store.state.customTabs.first())
        verify(delegate).showShareSheet(
            eq(activity),
            eq(promptRequest.data),
            onDismiss = any(),
            onSuccess = any(),
        )
    }

    @Test
    fun `GIVEN credit card autofill enabled and cards available WHEN getting a SelectCreditCard request THEN that request is handled`() = runTest(testDispatcher) {
        val feature = spy(
            PromptFeature(
                mock<Activity>(),
                store,
                customTabId = "custom-tab",
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                isCreditCardAutofillEnabled = { true },
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                mainDispatcher = testDispatcher,
                onNeedToRequestPermissions = { },
            ),
        )
        feature.creditCardPicker = creditCardPicker
        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()
        val selectCreditCardRequest = PromptRequest.SelectCreditCard(listOf(mock()), {}, {})

        store.dispatch(ContentAction.UpdatePromptRequestAction("custom-tab", selectCreditCardRequest))
        testDispatcher.scheduler.advanceUntilIdle()

        verify(feature).onPromptRequested(store.state.customTabs.first())
        verify(creditCardPicker).handleSelectCreditCardRequest(selectCreditCardRequest)
    }

    @Test
    fun `GIVEN credit card autofill enabled but no cards available WHEN getting a SelectCreditCard request THEN that request is not acted upon`() = runTest(testDispatcher) {
        val feature = spy(
            PromptFeature(
                mock<Activity>(),
                store,
                customTabId = "custom-tab",
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                isCreditCardAutofillEnabled = { true },
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                mainDispatcher = testDispatcher,
                onNeedToRequestPermissions = { },
            ),
        )
        feature.creditCardPicker = creditCardPicker
        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()
        val selectCreditCardRequest = PromptRequest.SelectCreditCard(emptyList(), {}, {})

        store.dispatch(ContentAction.UpdatePromptRequestAction("custom-tab", selectCreditCardRequest))
        testDispatcher.scheduler.advanceUntilIdle()

        verify(feature).onPromptRequested(store.state.customTabs.first())
        verify(creditCardPicker, never()).handleSelectCreditCardRequest(selectCreditCardRequest)
    }

    @Test
    fun `GIVEN credit card autofill disabled and cards available WHEN getting a SelectCreditCard request THEN that request is handled`() = runTest(testDispatcher) {
        val feature = spy(
            PromptFeature(
                mock<Activity>(),
                store,
                customTabId = "custom-tab",
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                isCreditCardAutofillEnabled = { false },
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                mainDispatcher = testDispatcher,
                onNeedToRequestPermissions = { },
            ),
        )
        feature.creditCardPicker = creditCardPicker
        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()
        val selectCreditCardRequest = PromptRequest.SelectCreditCard(listOf(mock()), {}, {})

        store.dispatch(ContentAction.UpdatePromptRequestAction("custom-tab", selectCreditCardRequest))
        testDispatcher.scheduler.advanceUntilIdle()

        verify(feature).onPromptRequested(store.state.customTabs.first())
        verify(creditCardPicker, never()).handleSelectCreditCardRequest(selectCreditCardRequest)
    }

    @Test
    fun `GIVEN a custom tab WHEN a new prompt is requested THEN exit fullscreen`() = runTest(testDispatcher) {
        val exitFullScreenUseCase: SessionUseCases.ExitFullScreenUseCase = mock()
        val feature = PromptFeature(
            fragment = mock(),
            store = store,
            fileUploadsDirCleaner = mock(),
            tabsUseCases = mock(),
            customTabId = "custom-tab",
            fragmentManager = fragmentManager,
            mainDispatcher = testDispatcher,
            exitFullscreenUsecase = exitFullScreenUseCase,
        ) { }

        val promptRequest = Alert(
            "title",
            "message",
            false,
            { },
            { },
        )

        feature.start()

        store.dispatch(ContentAction.UpdatePromptRequestAction("custom-tab", promptRequest))
        testDispatcher.scheduler.advanceUntilIdle()

        verify(exitFullScreenUseCase).invoke("custom-tab")
    }

    @Test
    fun `GIVEN a normal tab WHEN a new prompt is requested THEN exit fullscreen`() = runTest(testDispatcher) {
        val exitFullScreenUseCase: SessionUseCases.ExitFullScreenUseCase = mock()
        val feature = PromptFeature(
            fragment = mock(),
            fileUploadsDirCleaner = mock(),
            store = store,
            mainDispatcher = testDispatcher,
            tabsUseCases = mock(),
            fragmentManager = fragmentManager,
            exitFullscreenUsecase = exitFullScreenUseCase,
        ) { }

        val promptRequest = Alert(
            "title",
            "message",
            false,
            { },
            { },
        )

        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, promptRequest))
        testDispatcher.scheduler.advanceUntilIdle()

        verify(exitFullScreenUseCase).invoke(tabId)
    }

    @Test
    fun `GIVEN a private tab WHEN a new prompt is requested THEN exit fullscreen`() = runTest(testDispatcher) {
        val privateTabId = "private-tab"
        val exitFullScreenUseCase: SessionUseCases.ExitFullScreenUseCase = mock()
        store = BrowserStore(
            initialState = store.state.copy(
                tabs = store.state.tabs + TabSessionState(
                    id = privateTabId,
                    content = ContentState(url = "", private = true),
                ),
                selectedTabId = privateTabId,
            ),
        )
        val feature = PromptFeature(
            fragment = mock(),
            store = store,
            tabsUseCases = mock(),
            fileUploadsDirCleaner = mock(),
            fragmentManager = fragmentManager,
            exitFullscreenUsecase = exitFullScreenUseCase,
            mainDispatcher = testDispatcher,
        ) { }

        val promptRequest = Alert(
            "title",
            "message",
            false,
            { },
            { },
        )

        feature.start()

        store.dispatch(ContentAction.UpdatePromptRequestAction(privateTabId, promptRequest))
        testDispatcher.scheduler.advanceUntilIdle()

        verify(exitFullScreenUseCase).invoke(privateTabId)
    }

    @Test
    fun `GIVEN isCreditCardAutofillEnabled is false WHEN SaveCreditCard request is handled THEN dismiss SaveCreditCard`() = runTest(testDispatcher) {
        val creditCardEntry = CreditCardEntry(
            guid = "1",
            name = "Banana Apple",
            number = "4111111111111110",
            expiryMonth = "5",
            expiryYear = "2030",
            cardType = "",
        )
        val promptRequest = spy(
            PromptRequest.SaveCreditCard(
                creditCard = creditCardEntry,
                onConfirm = {},
                onDismiss = {},
            ),
        )
        val feature = spy(
            PromptFeature(
                activity = mock(),
                store = store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                creditCardValidationDelegate = mock(),
                isCreditCardAutofillEnabled = { false },
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = {},
            ),
        )
        val session = tab()!!

        feature.handleDialogsRequest(promptRequest, session)

        verify(feature).dismissDialogRequest(promptRequest, session)
    }

    @Test
    fun `GIVEN creditCardValidationDelegate is null WHEN SaveCreditCard request is handled THEN dismiss SaveCreditCard`() = runTest(testDispatcher) {
        val creditCardEntry = CreditCardEntry(
            guid = "1",
            name = "Banana Apple",
            number = "4111111111111110",
            expiryMonth = "5",
            expiryYear = "2030",
            cardType = "",
        )
        val promptRequest = spy(
            PromptRequest.SaveCreditCard(
                creditCard = creditCardEntry,
                onConfirm = {},
                onDismiss = {},
            ),
        )
        val feature = spy(
            PromptFeature(
                activity = mock(),
                store = store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                creditCardValidationDelegate = null,
                isCreditCardAutofillEnabled = { true },
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = {},
            ),
        )
        val session = tab()!!

        feature.handleDialogsRequest(promptRequest, session)

        verify(feature).dismissDialogRequest(promptRequest, session)
    }

    @Test
    fun `GIVEN prompt request credit card is invalid WHEN SaveCreditCard request is handled THEN dismiss SaveCreditCard`() = runTest(testDispatcher) {
        val invalidMonth = ""
        val invalidYear = ""
        val creditCardEntry = CreditCardEntry(
            guid = "1",
            name = "Banana Apple",
            number = "4111111111111110",
            expiryMonth = invalidMonth,
            expiryYear = invalidYear,
            cardType = "",
        )
        val promptRequest = spy(
            PromptRequest.SaveCreditCard(
                creditCard = creditCardEntry,
                onConfirm = {},
                onDismiss = {},
            ),
        )
        val feature = spy(
            PromptFeature(
                activity = mock(),
                store = store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                creditCardValidationDelegate = mock(),
                isCreditCardAutofillEnabled = { true },
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = {},
            ),
        )
        val session = tab()!!

        feature.handleDialogsRequest(promptRequest, session)

        verify(feature).dismissDialogRequest(promptRequest, session)
    }

    @Test
    fun `Selecting an item in a share dialog will consume promptRequest`() = runTest(testDispatcher) {
        val delegate: ShareDelegate = mock()
        val feature = PromptFeature(
            activity = mock(),
            store = store,
            fragmentManager = fragmentManager,
            tabsUseCases = mock(),
            shareDelegate = delegate,
            exitFullscreenUsecase = mock(),
            fileUploadsDirCleaner = mock(),
            isEmailMaskFeatureEnabled = { false },
            isSuggestEmailMaskEnabled = { false },
            onNeedToRequestPermissions = { },
        )
        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        var onSuccessCalled = false

        val shareRequest = PromptRequest.Share(
            ShareData("Title", "Text", null),
            onSuccess = { onSuccessCalled = true },
            onFailure = {},
            onDismiss = {},
        )
        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, shareRequest))

        assertEquals(1, tab()!!.content.promptRequests.size)
        assertEquals(shareRequest, tab()!!.content.promptRequests[0])
        feature.onConfirm(tabId, shareRequest.uid, null)

        assertTrue(tab()!!.content.promptRequests.isEmpty())
        assertTrue(onSuccessCalled)
    }

    @Test
    fun `Dismissing a share dialog will consume promptRequest`() = runTest(testDispatcher) {
        val delegate: ShareDelegate = mock()
        val feature = PromptFeature(
            activity = mock(),
            store = store,
            fragmentManager = fragmentManager,
            tabsUseCases = mock(),
            shareDelegate = delegate,
            exitFullscreenUsecase = mock(),
            fileUploadsDirCleaner = mock(),
            isEmailMaskFeatureEnabled = { false },
            isSuggestEmailMaskEnabled = { false },
            onNeedToRequestPermissions = { },
        )
        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        var onDismissCalled = false

        val shareRequest = PromptRequest.Share(
            ShareData("Title", "Text", null),
            onSuccess = {},
            onFailure = {},
            onDismiss = { onDismissCalled = true },
        )
        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, shareRequest))

        assertEquals(1, tab()!!.content.promptRequests.size)
        assertEquals(shareRequest, tab()!!.content.promptRequests[0])
        feature.onCancel(tabId, shareRequest.uid)

        assertTrue(tab()!!.content.promptRequests.isEmpty())
        assertTrue(onDismissCalled)
    }

    @Test
    fun `dialog will be dismissed if tab ID changes`() = runTest(testDispatcher) {
        val feature = spy(
            PromptFeature(
                activity = mock(),
                store = store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                shareDelegate = mock(),
                exitFullscreenUsecase = mock(),
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                mainDispatcher = testDispatcher,
                onNeedToRequestPermissions = { },
            ),
        )
        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        val shareRequest = PromptRequest.Share(
            ShareData("Title", "Text", null),
            onSuccess = {},
            onFailure = {},
            onDismiss = {},
        )
        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, shareRequest))
        testDispatcher.scheduler.advanceUntilIdle()

        val fragment = mock<PromptDialogFragment>()
        whenever(fragment.shouldDismissOnLoad).thenReturn(true)
        whenever(fragment.sessionId).thenReturn(tabId)
        whenever(fragment.isStateSaved).thenReturn(true)
        feature.activePrompt = WeakReference(fragment)

        val secondTabId = "second-test-tab"
        store.dispatch(
            TabListAction.AddTabAction(
                TabSessionState(
                    id = secondTabId,
                    content = ContentState(url = "mozilla.org"),
                ),
                select = true,
            ),
        )
        testDispatcher.scheduler.advanceUntilIdle()

        verify(fragment, times(1)).dismiss()
    }

    @Test
    fun `dialog will be dismissed if tab changes`() = runTest(testDispatcher) {
        val feature = spy(
            PromptFeature(
                activity = mock(),
                store = store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                shareDelegate = mock(),
                exitFullscreenUsecase = mock(),
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                mainDispatcher = testDispatcher,
                onNeedToRequestPermissions = { },
            ),
        )
        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        val shareRequest = PromptRequest.Share(
            ShareData("Title", "Text", null),
            onSuccess = {},
            onFailure = {},
            onDismiss = {},
        )
        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, shareRequest))
        testDispatcher.scheduler.advanceUntilIdle()

        val fragment = mock<PromptDialogFragment>()
        whenever(fragment.shouldDismissOnLoad).thenReturn(true)
        whenever(fragment.sessionId).thenReturn(tabId)
        whenever(fragment.isStateSaved).thenReturn(true)
        feature.activePrompt = WeakReference(fragment)

        val newTabId = "test-tab-2"

        store.dispatch(TabListAction.SelectTabAction(newTabId))
        testDispatcher.scheduler.advanceUntilIdle()

        verify(fragment, times(1)).dismiss()
    }

    @Test
    fun `dialog will be dismissed if tab URL changes`() = runTest(testDispatcher) {
        val feature = spy(
            PromptFeature(
                activity = mock(),
                store = store,
                fragmentManager = fragmentManager,
                mainDispatcher = testDispatcher,
                tabsUseCases = mock(),
                shareDelegate = mock(),
                exitFullscreenUsecase = mock(),
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = { },
            ),
        )
        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        val shareRequest = PromptRequest.Share(
            ShareData("Title", "Text", null),
            onSuccess = {},
            onFailure = {},
            onDismiss = {},
        )
        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, shareRequest))
        testDispatcher.scheduler.advanceUntilIdle()

        val fragment = mock<PromptDialogFragment>()
        whenever(fragment.shouldDismissOnLoad).thenReturn(true)
        whenever(fragment.isStateSaved).thenReturn(true)
        feature.activePrompt = WeakReference(fragment)

        store.dispatch(ContentAction.UpdateUrlAction(tabId, "mozilla.org"))
        testDispatcher.scheduler.advanceUntilIdle()

        verify(fragment, times(1)).dismiss()
    }

    @Test
    fun `dialog will not be dismissed if it is not attached to a fragment`() = runTest(testDispatcher) {
        val feature = spy(
            PromptFeature(
                activity = mock(),
                store = store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                shareDelegate = mock(),
                exitFullscreenUsecase = mock(),
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = {},
            ),
        )
        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        val shareRequest = PromptRequest.Share(
            ShareData("Title", "Text", null),
            onSuccess = {},
            onFailure = {},
            onDismiss = {},
        )
        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, shareRequest))

        val fragment = mock<PromptDialogFragment>()
        whenever(fragment.shouldDismissOnLoad).thenReturn(true)
        whenever(fragment.isStateSaved).thenReturn(false)
        feature.activePrompt = WeakReference(fragment)

        store.dispatch(ContentAction.UpdateUrlAction(tabId, "mozilla.org"))
        verify(fragment, never()).dismiss()
    }

    @Test
    fun `GIVEN saveLoginDialog is visible WHEN host doesn't change THEN keep saveLoginDialog visible`() = runTest(testDispatcher) {
        val newUrlSameDomain = "https://www.mozilla.org/en-GB/firefox/browsers/mobile/android/"
        val promptRequest = PromptRequest.Share(
            data = mock(),
            onSuccess = {},
            onFailure = {},
            onDismiss = {},
        )
        val saveLoginPrompt: SaveLoginDialogFragment = mock()

        val feature = spy(
            PromptFeature(
                mock<Activity>(),
                store = store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                exitFullscreenUsecase = mock(),
                loginValidationDelegate = mock(),
                isSaveLoginEnabled = { true },
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = { },
            ),
        )
        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, promptRequest))
        feature.activePrompt = WeakReference(saveLoginPrompt)

        store.dispatch(ContentAction.UpdateUrlAction(tabId, newUrlSameDomain))
        verify(saveLoginPrompt, never()).dismiss()
    }

    @Test
    fun `GIVEN saveLoginDialog is visible WHEN host changes THEN dismiss saveLoginDialog`() = runTest(testDispatcher) {
        val newUrlDifferentDomain = "https://www.android.com/new-features-on-android/"
        val promptRequest = PromptRequest.Share(
            data = mock(),
            onSuccess = {},
            onFailure = {},
            onDismiss = {},
        )
        val saveLoginPrompt: SaveLoginDialogFragment = mock()
        whenever(saveLoginPrompt.isStateSaved).thenReturn(true)

        val feature = PromptFeature(
            activity = mock(),
            store = store,
            fragmentManager = fragmentManager,
            mainDispatcher = testDispatcher,
            tabsUseCases = mock(),
            shareDelegate = mock(),
            exitFullscreenUsecase = mock(),
            isSaveLoginEnabled = { true },
            fileUploadsDirCleaner = mock(),
            isEmailMaskFeatureEnabled = { false },
            isSuggestEmailMaskEnabled = { false },
            onNeedToRequestPermissions = { },
        )

        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, promptRequest))
        testDispatcher.scheduler.advanceUntilIdle()

        feature.activePrompt = WeakReference(saveLoginPrompt)

        store.dispatch(ContentAction.UpdateUrlAction(tabId, newUrlDifferentDomain))
        testDispatcher.scheduler.advanceUntilIdle()

        verify(saveLoginPrompt, times(1)).dismiss()
    }

    @Test
    fun `prompt will always start the save login dialog with an icon`() = runTest(testDispatcher) {
        val feature = PromptFeature(
            activity = mock(),
            store = store,
            fragmentManager = fragmentManager,
            tabsUseCases = mock(),
            shareDelegate = mock(),
            loginValidationDelegate = mock(),
            isSaveLoginEnabled = { true },
            hideUpdateFragmentAfterSavingGeneratedPassword = { _, _ -> false },
            fileUploadsDirCleaner = mock(),
            isEmailMaskFeatureEnabled = { false },
            isSuggestEmailMaskEnabled = { false },
            onNeedToRequestPermissions = { },
        )
        val loginUsername = "username"
        val loginPassword = "password"
        val entry: LoginEntry = mock()
        `when`(entry.username).thenReturn(loginUsername)
        `when`(entry.password).thenReturn(loginPassword)
        val loginsPrompt = PromptRequest.SaveLoginPrompt(2, listOf(entry), { }, { })
        val websiteIcon: Bitmap = mock()
        val contentState: ContentState = mock()
        val session: TabSessionState = mock()
        val sessionId = "sessionId"
        `when`(contentState.icon).thenReturn(websiteIcon)
        `when`(session.content).thenReturn(contentState)
        `when`(session.id).thenReturn(sessionId)

        feature.handleDialogsRequest(
            loginsPrompt,
            session,
        )

        // Only interested in the icon, but it doesn't hurt to be sure we show a properly configured dialog.
        val dialogFragment = feature.activePrompt!!.get()
        assertIs<SaveLoginDialogFragment>(dialogFragment)
        assertEquals(loginUsername, dialogFragment.username)
        assertEquals(loginPassword, dialogFragment.password)
        assertEquals(websiteIcon, dialogFragment.icon)
        assertEquals(sessionId, dialogFragment.sessionId)
    }

    @Test
    fun `save login dialog will not be dismissed on page load`() = runTest(testDispatcher) {
        val feature = spy(
            PromptFeature(
                activity = mock(),
                store = store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                shareDelegate = mock(),
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = { },
            ),
        )
        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        val shareRequest = PromptRequest.Share(
            ShareData("Title", "Text", null),
            onSuccess = {},
            onFailure = {},
            onDismiss = {},
        )
        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, shareRequest))

        val fragment = spy(
            SaveLoginDialogFragment.newInstance(
                sessionId = tabId,
                promptRequestUID = shareRequest.uid,
                shouldDismissOnLoad = false,
                hint = 0,
                entry = LoginEntry(
                    origin = "https://www.mozilla.org",
                    username = "username",
                    password = "password",
                ),
            ),
        )
        feature.activePrompt = WeakReference(fragment)

        store.dispatch(ContentAction.UpdateProgressAction(tabId, 0))
        store.dispatch(ContentAction.UpdateProgressAction(tabId, 10))
        store.dispatch(ContentAction.UpdateProgressAction(tabId, 100))

        verify(fragment, times(0)).dismiss()
    }

    @Test
    fun `confirm dialogs will not be automatically dismissed`() = runTest(testDispatcher) {
        val feature = spy(
            PromptFeature(
                activity = mock(),
                store = store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                shareDelegate = mock(),
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                mainDispatcher = testDispatcher,
                onNeedToRequestPermissions = { },
            ),
        )
        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        val promptRequest = PromptRequest.Confirm(
            "title",
            "message",
            false,
            "positive",
            "negative",
            "neutral",
            { },
            { },
            { },
            { },
        )
        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, promptRequest))
        testDispatcher.scheduler.advanceUntilIdle()

        val prompt = feature.activePrompt?.get()
        assertNotNull(prompt)
        assertFalse(prompt!!.shouldDismissOnLoad)
    }

    @Test
    fun `A Repost PromptRequest prompt will be shown as a ConfirmDialogFragment`() = runTest(testDispatcher) {
        val feature = PromptFeature(
            // Proper activity here to allow for the feature to properly execute "container.context.getString"
            activity = Robolectric.buildActivity(Activity::class.java).setup().get(),
            store = store,
            fragmentManager = fragmentManager,
            tabsUseCases = mock(),
            shareDelegate = mock(),
            loginValidationDelegate = mock(),
            isSaveLoginEnabled = { true },
            fileUploadsDirCleaner = mock(),
            isEmailMaskFeatureEnabled = { false },
            isSuggestEmailMaskEnabled = { false },
            onNeedToRequestPermissions = { },
        )
        val repostPromptRequest: PromptRequest.Repost = mock()
        doReturn("uid").`when`(repostPromptRequest).uid

        feature.handleDialogsRequest(repostPromptRequest, mock())

        val dialog: ConfirmDialogFragment = feature.activePrompt!!.get() as ConfirmDialogFragment
        assertEquals(testContext.getString(R.string.mozac_feature_prompt_repost_title), dialog.title)
        assertEquals(testContext.getString(R.string.mozac_feature_prompt_repost_message), dialog.message)
        assertEquals(
            testContext.getString(R.string.mozac_feature_prompt_repost_positive_button_text),
            dialog.positiveButtonText,
        )
        assertEquals(
            testContext.getString(R.string.mozac_feature_prompt_repost_negative_button_text),
            dialog.negativeButtonText,
        )
    }

    @Test
    fun `Positive button on a Repost dialog will call onAccept and consume the dialog`() = runTest(testDispatcher) {
        val feature = PromptFeature(
            activity = Robolectric.buildActivity(Activity::class.java).setup().get(),
            store = store,
            fragmentManager = fragmentManager,
            tabsUseCases = mock(),
            exitFullscreenUsecase = mock(),
            fileUploadsDirCleaner = mock(),
            isEmailMaskFeatureEnabled = { false },
            isSuggestEmailMaskEnabled = { false },
            onNeedToRequestPermissions = { },
        )
        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        var acceptCalled = false
        val repostRequest = PromptRequest.Repost(
            { acceptCalled = true },
            { },
        )
        store
            .dispatch(ContentAction.UpdatePromptRequestAction(tabId, repostRequest))

        assertEquals(1, tab()!!.content.promptRequests.size)
        assertEquals(repostRequest, tab()!!.content.promptRequests[0])
        feature.onConfirm(tabId, repostRequest.uid, null)

        assertTrue(acceptCalled)
        assertTrue(tab()!!.content.promptRequests.isEmpty())
    }

    @Test
    fun `Negative button on a Repost dialog will call onDismiss and consume the dialog`() = runTest(testDispatcher) {
        val feature = PromptFeature(
            activity = Robolectric.buildActivity(Activity::class.java).setup().get(),
            store = store,
            fragmentManager = fragmentManager,
            tabsUseCases = mock(),
            exitFullscreenUsecase = mock(),
            fileUploadsDirCleaner = mock(),
            isEmailMaskFeatureEnabled = { false },
            isSuggestEmailMaskEnabled = { false },
            onNeedToRequestPermissions = { },
        )
        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        var dismissCalled = false
        val repostRequest = PromptRequest.Repost(
            { },
            { dismissCalled = true },
        )
        store
            .dispatch(ContentAction.UpdatePromptRequestAction(tabId, repostRequest))

        assertEquals(1, tab()!!.content.promptRequests.size)
        assertEquals(repostRequest, tab()!!.content.promptRequests[0])
        feature.onCancel(tabId, repostRequest.uid)

        assertTrue(dismissCalled)
        assertTrue(tab()!!.content.promptRequests.isEmpty())
    }

    @Test
    fun `WHEN onConfirm is called on a SaveCreditCard dialog THEN a confirm request will consume the dialog`() = runTest(testDispatcher) {
        val feature = PromptFeature(
            activity = mock(),
            store = store,
            fragmentManager = fragmentManager,
            tabsUseCases = mock(),
            exitFullscreenUsecase = mock(),
            creditCardValidationDelegate = mock(),
            isCreditCardAutofillEnabled = { true },
            fileUploadsDirCleaner = mock(),
            isEmailMaskFeatureEnabled = { false },
            isSuggestEmailMaskEnabled = { false },
            onNeedToRequestPermissions = { },
        )
        val creditCardEntry = CreditCardEntry(
            guid = "1",
            name = "Banana Apple",
            number = "4111111111111110",
            expiryMonth = "5",
            expiryYear = "2030",
            cardType = "",
        )

        val request = PromptRequest.SaveCreditCard(
            creditCard = creditCardEntry,
            onConfirm = {},
            onDismiss = {},
        )

        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, request))

        assertEquals(1, tab()!!.content.promptRequests.size)

        feature.onConfirm(
            sessionId = tabId,
            promptRequestUID = request.uid,
            value = creditCardEntry,
        )

        assertTrue(tab()!!.content.promptRequests.isEmpty())
    }

    @Test
    fun `WHEN a credit card is confirmed to save THEN confirm the prompt request with the selected credit card`() = runTest(testDispatcher) {
        val creditCardEntry = CreditCardEntry(
            guid = "1",
            name = "Banana Apple",
            number = "4111111111111110",
            expiryMonth = "5",
            expiryYear = "2030",
            cardType = "",
        )
        var onDismissCalled = false
        var onConfirmCalled = false
        var confirmedCreditCard: CreditCardEntry? = null

        val request = PromptRequest.SaveCreditCard(
            creditCard = creditCardEntry,
            onConfirm = {
                confirmedCreditCard = it
                onConfirmCalled = true
            },
            onDismiss = {
                onDismissCalled = true
            },
        )

        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, request))

        request.onConfirm(creditCardEntry)

        assertEquals(creditCardEntry, confirmedCreditCard)
        assertTrue(onConfirmCalled)

        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, request))

        request.onDismiss()

        assertTrue(onDismissCalled)
    }

    @Test
    fun `WHEN the save credit card dialog fragment is created THEN the credit card entry is passed into the instance`() = runTest(testDispatcher) {
        val feature = PromptFeature(
            activity = mock(),
            store = store,
            fragmentManager = fragmentManager,
            tabsUseCases = mock(),
            creditCardValidationDelegate = mock(),
            isCreditCardAutofillEnabled = { true },
            fileUploadsDirCleaner = mock(),
            isEmailMaskFeatureEnabled = { false },
            isSuggestEmailMaskEnabled = { false },
            onNeedToRequestPermissions = { },
        )
        val creditCardEntry = CreditCardEntry(
            guid = "1",
            name = "Banana Apple",
            number = "4111111111111110",
            expiryMonth = "5",
            expiryYear = "2030",
            cardType = "",
        )
        val request = PromptRequest.SaveCreditCard(
            creditCard = creditCardEntry,
            onConfirm = {},
            onDismiss = {},
        )

        val contentState: ContentState = mock()
        val session: TabSessionState = mock()
        val sessionId = "sessionId"

        `when`(session.content).thenReturn(contentState)
        `when`(session.id).thenReturn(sessionId)

        feature.handleDialogsRequest(
            promptRequest = request,
            session = session,
        )

        val dialogFragment = feature.activePrompt!!.get()
        assertIs<CreditCardSaveDialogFragment>(dialogFragment)

        assertEquals(sessionId, dialogFragment.sessionId)
        assertEquals(creditCardEntry, dialogFragment.creditCard)
    }

    @Test
    fun `GIVEN SaveCreditCard prompt is shown WHEN prompt is removed from state THEN dismiss SaveCreditCard prompt`() = runTest(testDispatcher) {
        val creditCardEntry = CreditCardEntry(
            guid = "1",
            name = "Banana Apple",
            number = "4111111111111110",
            expiryMonth = "5",
            expiryYear = "2030",
            cardType = "",
        )
        val promptRequest = PromptRequest.SaveCreditCard(
            creditCard = creditCardEntry,
            onConfirm = {},
            onDismiss = {},
        )
        val dialogFragment: CreditCardSaveDialogFragment = mock()

        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, promptRequest))
        testDispatcher.scheduler.advanceUntilIdle()

        val feature = PromptFeature(
            activity = mock(),
            store = store,
            fragmentManager = fragmentManager,
            mainDispatcher = testDispatcher,
            tabsUseCases = mock(),
            exitFullscreenUsecase = mock(),
            creditCardValidationDelegate = mock(),
            isCreditCardAutofillEnabled = { true },
            fileUploadsDirCleaner = mock(),
            isEmailMaskFeatureEnabled = { false },
            isSuggestEmailMaskEnabled = { false },
            onNeedToRequestPermissions = { },
        )

        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()
        feature.activePrompt = WeakReference(dialogFragment)
        feature.activePromptRequest = promptRequest

        store.dispatch(ContentAction.ConsumePromptRequestAction(tabId, promptRequest))
        testDispatcher.scheduler.advanceUntilIdle()

        verify(dialogFragment).dismissAllowingStateLoss()
    }

    @Test
    fun `WHEN SaveCreditCard is handled THEN the credit card save prompt shown fact is emitted`() = runTest(testDispatcher) {
        val feature = PromptFeature(
            activity = mock(),
            store = store,
            fragmentManager = fragmentManager,
            tabsUseCases = mock(),
            creditCardValidationDelegate = mock(),
            isCreditCardAutofillEnabled = { true },
            fileUploadsDirCleaner = mock(),
            isEmailMaskFeatureEnabled = { false },
            isSuggestEmailMaskEnabled = { false },
            onNeedToRequestPermissions = { },
        )
        val creditCardEntry = CreditCardEntry(
            guid = "1",
            name = "CC",
            number = "4111111111111110",
            expiryMonth = "5",
            expiryYear = "2030",
            cardType = "",
        )
        val request = PromptRequest.SaveCreditCard(
            creditCard = creditCardEntry,
            onConfirm = {},
            onDismiss = {},
        )
        val session: TabSessionState = mock()
        val sessionId = "sessionId"
        `when`(session.id).thenReturn(sessionId)

        CollectionProcessor.withFactCollection { facts ->
            feature.handleDialogsRequest(
                promptRequest = request,
                session = session,
            )

            val fact = facts.find { it.item == CreditCardAutofillDialogFacts.Items.AUTOFILL_CREDIT_CARD_SAVE_PROMPT_SHOWN }!!
            assertEquals(Component.FEATURE_PROMPTS, fact.component)
            assertEquals(Action.DISPLAY, fact.action)
            assertEquals(
                CreditCardAutofillDialogFacts.Items.AUTOFILL_CREDIT_CARD_SAVE_PROMPT_SHOWN,
                fact.item,
            )
        }
    }

    @Test
    fun `WHEN choice promptRequest is dismissed by the engine THEN the active prompt will be cleared`() = runTest(testDispatcher) {
        val feature = spy(
            PromptFeature(
                activity = mock(),
                store = store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                shareDelegate = mock(),
                exitFullscreenUsecase = mock(),
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = { },
            ),
        )
        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        val singleChoicePrompt = SingleChoice(
            choices = arrayOf(),
            onConfirm = {},
            onDismiss = {},
        )
        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, singleChoicePrompt))
        val fragment = mock<ChoiceDialogFragment>()
        whenever(fragment.isStateSaved).thenReturn(false)

        store.dispatch(ContentAction.ConsumePromptRequestAction(tabId, singleChoicePrompt))
        assertEquals(null, feature.activePrompt?.get())
        assertTrue(feature.activePromptsToDismiss.isEmpty())
    }

    @Test
    fun `WHEN promptRequest is updated THEN the replaced active prompt will be dismissed`() = runTest(testDispatcher) {
        val feature = spy(
            PromptFeature(
                activity = mock(),
                store = store,
                fragmentManager = fragmentManager,
                mainDispatcher = testDispatcher,
                tabsUseCases = mock(),
                shareDelegate = mock(),
                exitFullscreenUsecase = mock(),
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = { },
            ),
        )
        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        val previousPrompt = SingleChoice(
            choices = arrayOf(),
            onConfirm = {},
            onDismiss = {},
        )
        val updatedPrompt = SingleChoice(
            choices = arrayOf(),
            onConfirm = {},
            onDismiss = {},
        )
        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, previousPrompt))
        testDispatcher.scheduler.advanceUntilIdle()

        val fragment = mock<ChoiceDialogFragment>()
        whenever(fragment.shouldDismissOnLoad).thenReturn(true)
        whenever(fragment.isStateSaved).thenReturn(true)
        feature.activePrompt = WeakReference(fragment)

        store.dispatch(ContentAction.ReplacePromptRequestAction(tabId, previousPrompt.uid, updatedPrompt))
        testDispatcher.scheduler.advanceUntilIdle()

        verify(fragment).dismiss()
    }

    @Test
    fun `WHEN prompt request is a file THEN the active prompt should not be dismissed`() = runTest(testDispatcher) {
        var onDismissWasCalled = false

        val filePickerRequest =
            PromptRequest.File(emptyArray(), true, { _, _ -> }, { _, _ -> }) {
                onDismissWasCalled = true
            }

        val feature = PromptFeature(
            activity = mock(),
            store = store,
            fileUploadsDirCleaner = mock(),
            isEmailMaskFeatureEnabled = { false },
            isSuggestEmailMaskEnabled = { false },
            tabsUseCases = mock(),
            fragmentManager = fragmentManager,
            onNeedToRequestPermissions = { },
        )

        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, filePickerRequest))

        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        assertFalse(onDismissWasCalled)
        assertTrue(tab()!!.content.promptRequests.isNotEmpty())
    }

    @Test
    fun `WHEN onAndroidPhotoPickerResult is called THEN it calls filePicker with correct argument`() = runTest(testDispatcher) {
        val feature = spy(
            PromptFeature(
                activity = mock(),
                store = store,
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                shareDelegate = mock(),
                exitFullscreenUsecase = mock(),
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                onNeedToRequestPermissions = { },
            ),
        )

        feature.filePicker = mock()

        val uris = arrayOf("content://path/to/file1".toUri(), "content://path/to/file2".toUri())

        feature.onAndroidPhotoPickerResult(uris)

        verify(feature.filePicker).onAndroidPhotoPickerResult(uris)
    }

    @Test
    fun `GIVEN the redirect dialog is active WHEN handleDialogsRequest is called THEN dismiss the prompt request`() = runTest(testDispatcher) {
        val feature = spy(
            PromptFeature(
                activity = mock(),
                store = store,
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                tabsUseCases = mock(),
                fragmentManager = fragmentManager,
                onNeedToRequestPermissions = {},
            ),
        )
        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()
        feature.activePrompt = WeakReference(mock())
        `when`(feature.redirectDialogFragmentIsActive()).thenReturn(true)

        val promptRequest = spy(
            SingleChoice(
                choices = arrayOf(mock()),
                onConfirm = {},
                onDismiss = {},
            ),
        )
        val session = tab()!!
        feature.handleDialogsRequest(promptRequest, session)

        verify(feature).dismissDialogRequest(promptRequest, session)
    }

    @Test
    fun `GIVEN a CertificateRequest prompt THEN handleCertificateRequest(promptRequest) is called`() = runTest(testDispatcher) {
        val feature = spy(
            PromptFeature(
                mock<Activity>(),
                store,
                customTabId = "custom-tab",
                fragmentManager = fragmentManager,
                tabsUseCases = mock(),
                isCreditCardAutofillEnabled = { true },
                fileUploadsDirCleaner = mock(),
                isEmailMaskFeatureEnabled = { false },
                isSuggestEmailMaskEnabled = { false },
                mainDispatcher = testDispatcher,
                onNeedToRequestPermissions = { },
            ),
        )
        feature.certificatePicker = certificatePicker
        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        val certificateRequest = PromptRequest.CertificateRequest("exmaple.com", null, { })

        store.dispatch(ContentAction.UpdatePromptRequestAction("custom-tab", certificateRequest))
        testDispatcher.scheduler.advanceUntilIdle()

        verify(feature).onPromptRequested(store.state.customTabs.first())
        verify(certificatePicker).handleCertificateRequest(certificateRequest)
    }

    @Test
    fun `A FolderUploadPrompt PromptRequest prompt will be shown as a ConfirmDialogFragment`() = runTest(testDispatcher) {
        val feature = PromptFeature(
            // Proper activity here to allow for the feature to properly execute "container.context.getString"
            activity = Robolectric.buildActivity(Activity::class.java).setup().get(),
            store = store,
            fragmentManager = fragmentManager,
            tabsUseCases = mock(),
            shareDelegate = mock(),
            loginValidationDelegate = mock(),
            isSaveLoginEnabled = { true },
            fileUploadsDirCleaner = mock(),
            isEmailMaskFeatureEnabled = { false },
            isSuggestEmailMaskEnabled = { false },
            onNeedToRequestPermissions = { },
        )
        val promptRequest: PromptRequest.FolderUploadPrompt = mock()
        doReturn("uid").`when`(promptRequest).uid
        doReturn("folder").`when`(promptRequest).folderName

        feature.handleDialogsRequest(promptRequest, mock())

        val dialog: ConfirmDialogFragment = feature.activePrompt!!.get() as ConfirmDialogFragment
        assertEquals(testContext.getString(R.string.mozac_feature_prompt_folder_upload_confirm_title), dialog.title)
        assertEquals(
            testContext.getString(R.string.mozac_feature_prompt_folder_upload_confirm_message, "folder"),
            dialog.message,
        )
        assertEquals(
            testContext.getString(R.string.mozac_feature_prompt_folder_upload_confirm_positive_button_text),
            dialog.positiveButtonText,
        )
        assertEquals(
            testContext.getString(R.string.mozac_feature_prompt_folder_upload_confirm_negative_button_text),
            dialog.negativeButtonText,
        )
    }

    @Test
    fun `A WebAuthnRelatedOriginPrompt for create will be shown as a WebAuthnRelatedOriginDialogFragment`() = runTest(testDispatcher) {
        val feature = PromptFeature(
            activity = Robolectric.buildActivity(Activity::class.java).setup().get(),
            store = store,
            fragmentManager = fragmentManager,
            tabsUseCases = mock(),
            fileUploadsDirCleaner = mock(),
            isEmailMaskFeatureEnabled = { false },
            isSuggestEmailMaskEnabled = { false },
            onNeedToRequestPermissions = { },
        )
        val promptRequest = PromptRequest.WebAuthnRelatedOriginPrompt(
            origin = "example.com",
            rpId = "rp.example.com",
            isCreate = true,
            onConfirm = { },
            onDismiss = { },
        )

        feature.handleDialogsRequest(promptRequest, mock())

        val dialog = feature.activePrompt!!.get() as WebAuthnRelatedOriginDialogFragment
        assertEquals(
            testContext.getString(R.string.webauthn_related_origin_create_message, "example.com", "rp.example.com"),
            dialog.message,
        )
    }

    @Test
    fun `A WebAuthnRelatedOriginPrompt for use will be shown as a WebAuthnRelatedOriginDialogFragment`() = runTest(testDispatcher) {
        val feature = PromptFeature(
            activity = Robolectric.buildActivity(Activity::class.java).setup().get(),
            store = store,
            fragmentManager = fragmentManager,
            tabsUseCases = mock(),
            fileUploadsDirCleaner = mock(),
            isEmailMaskFeatureEnabled = { false },
            isSuggestEmailMaskEnabled = { false },
            onNeedToRequestPermissions = { },
        )
        val promptRequest = PromptRequest.WebAuthnRelatedOriginPrompt(
            origin = "example.com",
            rpId = "rp.example.com",
            isCreate = false,
            onConfirm = { },
            onDismiss = { },
        )

        feature.handleDialogsRequest(promptRequest, mock())

        val dialog = feature.activePrompt!!.get() as WebAuthnRelatedOriginDialogFragment
        assertEquals(
            testContext.getString(R.string.webauthn_related_origin_use_message, "example.com", "rp.example.com"),
            dialog.message,
        )
    }

    @Test
    fun `Calling onConfirm on a WebAuthnRelatedOriginPrompt request will consume promptRequest`() = runTest(testDispatcher) {
        val feature = PromptFeature(
            activity = mock(),
            store = store,
            fragmentManager = fragmentManager,
            tabsUseCases = mock(),
            fileUploadsDirCleaner = mock(),
            isEmailMaskFeatureEnabled = { false },
            isSuggestEmailMaskEnabled = { false },
            onNeedToRequestPermissions = { },
        )
        var onConfirmWasCalled = false
        val promptRequest = PromptRequest.WebAuthnRelatedOriginPrompt(
            origin = "example.com",
            rpId = "rp.example.com",
            isCreate = true,
            onConfirm = { onConfirmWasCalled = true },
            onDismiss = { },
        )

        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, promptRequest))

        feature.onConfirm(tabId, promptRequest.uid, false)
        assertTrue(tab()!!.content.promptRequests.isEmpty())
        assertTrue(onConfirmWasCalled)
    }

    @Test
    fun `Calling onCancel on a WebAuthnRelatedOriginPrompt request will consume promptRequest`() = runTest(testDispatcher) {
        val feature = PromptFeature(
            activity = mock(),
            store = store,
            fragmentManager = fragmentManager,
            tabsUseCases = mock(),
            fileUploadsDirCleaner = mock(),
            isEmailMaskFeatureEnabled = { false },
            isSuggestEmailMaskEnabled = { false },
            onNeedToRequestPermissions = { },
        )
        var onDismissWasCalled = false
        val promptRequest = PromptRequest.WebAuthnRelatedOriginPrompt(
            origin = "example.com",
            rpId = "rp.example.com",
            isCreate = true,
            onConfirm = { },
            onDismiss = { onDismissWasCalled = true },
        )

        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, promptRequest))

        feature.onCancel(tabId, promptRequest.uid)
        assertTrue(tab()!!.content.promptRequests.isEmpty())
        assertTrue(onDismissWasCalled)
    }

    @Test
    fun `GIVEN isEmailMaskFeatureEnabled is false WHEN handleEmailMaskOrLoginPrompt is called THEN no prompt is shown`() {
        val emailMaskView: EmailMaskPromptView = mock()
        val feature = spy(
            createPromptFeature(
                emailMaskDelegate = mockEmailMaskDelegate(emailMaskView),
                isEmailMaskFeatureEnabled = false,
            ),
        )

        val emailMaskLogin = mockEmailMaskLogin()
        val selectLoginRequest = createSelectLoginPrompt(emailMaskLogin)

        feature.handleEmailMaskOrLoginPrompt(
            selectLoginRequest,
            tab()!!,
            mapOf(LoginHint.EMAIL_MASK to listOf(emailMaskLogin)),
        )

        verify(emailMaskView, never()).showPrompt()
        verify(feature, never()).handleDialogsRequest(any(), any())
    }

    @Test
    fun `GIVEN isSuggestEmailMaskEnabled is false WHEN handleEmailMaskOrLoginPrompt is called THEN no prompt is shown`() {
        val emailMaskView: EmailMaskPromptView = mock()
        val feature = spy(
            createPromptFeature(
                emailMaskDelegate = mockEmailMaskDelegate(emailMaskView),
                isSuggestEmailMaskEnabled = false,
            ),
        )

        val emailMaskLogin = mockEmailMaskLogin()
        val selectLoginRequest = createSelectLoginPrompt(emailMaskLogin)

        feature.handleEmailMaskOrLoginPrompt(
            selectLoginRequest,
            tab()!!,
            mapOf(LoginHint.EMAIL_MASK to listOf(emailMaskLogin)),
        )

        verify(emailMaskView, never()).showPrompt()
        verify(feature, never()).handleDialogsRequest(any(), any())
    }

    @Test
    fun `GIVEN both email mask flags are true AND no emailMaskDelegate WHEN handleEmailMaskOrLoginPrompt is called THEN handleDialogsRequest is called`() {
        val feature = spy(createPromptFeature())

        val emailMaskLogin = mockEmailMaskLogin()
        val selectLoginRequest = createSelectLoginPrompt(emailMaskLogin)

        feature.handleEmailMaskOrLoginPrompt(
            selectLoginRequest,
            tab()!!,
            mapOf(LoginHint.EMAIL_MASK to listOf(emailMaskLogin)),
        )

        verify(feature).handleDialogsRequest(eq(selectLoginRequest), any())
    }

    @Test
    fun `GIVEN both email mask flags are true AND emailMaskDelegate is set WHEN handleEmailMaskOrLoginPrompt is called THEN email mask prompt is shown`() {
        val emailMaskView: EmailMaskPromptView = mock()
        val feature = createPromptFeature(emailMaskDelegate = mockEmailMaskDelegate(emailMaskView))

        val emailMaskLogin = mockEmailMaskLogin()
        val selectLoginRequest = spy(createSelectLoginPrompt(emailMaskLogin))

        feature.handleEmailMaskOrLoginPrompt(
            selectLoginRequest,
            tab()!!,
            mapOf(LoginHint.EMAIL_MASK to listOf(emailMaskLogin)),
        )

        verify(emailMaskView).showPrompt()
    }

    private fun createPromptFeature(
        emailMaskDelegate: EmailMaskDelegate? = null,
        isEmailMaskFeatureEnabled: Boolean = true,
        isSuggestEmailMaskEnabled: Boolean = true,
    ) = PromptFeature(
        activity = mock(),
        store = store,
        fragmentManager = fragmentManager,
        tabsUseCases = mock(),
        fileUploadsDirCleaner = mock(),
        emailMaskDelegate = emailMaskDelegate,
        isEmailMaskFeatureEnabled = { isEmailMaskFeatureEnabled },
        isSuggestEmailMaskEnabled = { isSuggestEmailMaskEnabled },
        onNeedToRequestPermissions = { },
    )

    private fun mockEmailMaskDelegate(emailMaskView: EmailMaskPromptView) =
        object : EmailMaskDelegate {
            override val emailMaskPromptViewListenerView = emailMaskView
            override fun shouldShowEmailMaskCfr() = false
            override fun onEmailMaskCfrDismissed() = Unit
            override suspend fun onEmailMaskClick(generatedFor: String) = null
        }

    private fun mockEmailMaskLogin() = Login(
        guid = "A",
        origin = "https://www.mozilla.org",
        username = "user@example.com",
        password = "password",
        hint = LoginHint.EMAIL_MASK,
    )

    private fun createSelectLoginPrompt(emailMaskLogin: Login): PromptRequest.SelectLoginPrompt =
        PromptRequest.SelectLoginPrompt(
            logins = listOf(emailMaskLogin),
            generatedPassword = null,
            onConfirm = {},
            onDismiss = {},
        )

    private fun mockFragmentManager(): FragmentManager {
        val fragmentManager: FragmentManager = mock()
        val transaction: FragmentTransaction = mock()
        doReturn(transaction).`when`(fragmentManager).beginTransaction()
        doReturn(transaction).`when`(transaction).remove(any())
        return fragmentManager
    }
}
