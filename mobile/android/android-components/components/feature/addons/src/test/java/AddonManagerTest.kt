/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.addons

import android.graphics.Bitmap
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlin.test.assertIs
import kotlin.test.assertNotNull
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.TimeoutCancellationException
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.action.BrowserAction
import mozilla.components.browser.state.action.WebExtensionAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.WebExtensionState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.webextension.ActionHandler
import mozilla.components.concept.engine.webextension.DisabledFlags
import mozilla.components.concept.engine.webextension.DisabledFlags.Companion.APP_SUPPORT
import mozilla.components.concept.engine.webextension.DisabledFlags.Companion.APP_VERSION
import mozilla.components.concept.engine.webextension.DisabledFlags.Companion.BLOCKLIST
import mozilla.components.concept.engine.webextension.DisabledFlags.Companion.SIGNATURE
import mozilla.components.concept.engine.webextension.DisabledFlags.Companion.SOFT_BLOCKLIST
import mozilla.components.concept.engine.webextension.DisabledFlags.Companion.USER
import mozilla.components.concept.engine.webextension.EnableSource
import mozilla.components.concept.engine.webextension.InstallationMethod
import mozilla.components.concept.engine.webextension.Metadata
import mozilla.components.concept.engine.webextension.WebExtension
import mozilla.components.feature.addons.ui.translateName
import mozilla.components.feature.addons.update.AddonUpdater.Status
import mozilla.components.support.test.any
import mozilla.components.support.test.argumentCaptor
import mozilla.components.support.test.eq
import mozilla.components.support.test.middleware.CaptureActionsMiddleware
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.test.whenever
import mozilla.components.support.webextensions.WebExtensionSupport
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.ArgumentMatchers.anyBoolean
import org.mockito.ArgumentMatchers.anyString
import org.mockito.Mockito.doNothing
import org.mockito.Mockito.doThrow
import org.mockito.Mockito.never
import org.mockito.Mockito.spy
import org.mockito.Mockito.times
import org.mockito.Mockito.verify

@RunWith(AndroidJUnit4::class)
class AddonManagerTest {
    private val dispatcher = StandardTestDispatcher()

    @Before
    fun setup() {
        WebExtensionSupport.installedExtensions.clear()
    }

    @After
    fun after() {
        WebExtensionSupport.installedExtensions.clear()
    }

    @Test
    fun `getAddons - queries addons from provider and updates installation state`() =
        runTest(dispatcher) {
            // Prepare addons provider
            // addon1 (ext1) is a featured extension that is already installed.
            // addon2 (ext2) is a featured extension that is not installed.
            // addon3 (ext3) is a featured extension that is marked as disabled.
            // addon4 (ext4) and addon5 (ext5) are not featured extensions but they are installed.
            val addonsProvider: AddonsProvider = mock()

            whenever(addonsProvider.getFeaturedAddons(anyBoolean(), eq(3L), language = anyString()))
                .thenReturn(listOf(Addon(id = "ext1"), Addon(id = "ext2"), Addon(id = "ext3")))

            // Prepare engine
            val engine: Engine = mock()
            val callbackCaptor = argumentCaptor<((List<WebExtension>) -> Unit)>()
            whenever(engine.listInstalledWebExtensions(callbackCaptor.capture(), any())).thenAnswer {
                callbackCaptor.value.invoke(emptyList())
            }
            val store =
                BrowserStore(
                    BrowserState(
                        extensions =
                            mapOf(
                                "ext1" to WebExtensionState("ext1", "url"),
                                "ext4" to WebExtensionState("ext4", "url"),
                                "ext5" to WebExtensionState("ext5", "url"),
                                // ext6 is a temporarily loaded extension.
                                "ext6" to WebExtensionState("ext6", "url"),
                                // ext7 is a built-in extension.
                                "ext7" to WebExtensionState("ext7", "url"),
                            )
                    )
                )

            WebExtensionSupport.initialize(engine, store)
            val ext1: WebExtension = mock()
            whenever(ext1.id).thenReturn("ext1")
            whenever(ext1.isEnabled()).thenReturn(true)
            WebExtensionSupport.installedExtensions["ext1"] = ext1

            // Make `ext3` an extension that is disabled because it wasn't supported.
            val unsupportedExtension: WebExtension = mock()
            val metadata: Metadata = mock()
            whenever(unsupportedExtension.isEnabled()).thenReturn(false)
            whenever(metadata.disabledFlags).thenReturn(DisabledFlags.select(APP_SUPPORT))
            whenever(metadata.optionsPageUrl).thenReturn("http://options-page.moz")
            whenever(metadata.openOptionsPageInTab).thenReturn(true)
            whenever(unsupportedExtension.id).thenReturn("ext3")
            whenever(unsupportedExtension.url).thenReturn("site_url")
            whenever(unsupportedExtension.getMetadata()).thenReturn(metadata)
            WebExtensionSupport.installedExtensions["ext3"] = unsupportedExtension

            val ext4: WebExtension = mock()
            whenever(ext4.id).thenReturn("ext4")
            whenever(ext4.isEnabled()).thenReturn(true)
            val ext4Metadata: Metadata = mock()
            whenever(ext4Metadata.temporary).thenReturn(false)
            whenever(ext4.getMetadata()).thenReturn(ext4Metadata)
            WebExtensionSupport.installedExtensions["ext4"] = ext4

            val ext5: WebExtension = mock()
            whenever(ext5.id).thenReturn("ext5")
            whenever(ext5.isEnabled()).thenReturn(true)
            val ext5Metadata: Metadata = mock()
            whenever(ext5Metadata.temporary).thenReturn(false)
            whenever(ext5.getMetadata()).thenReturn(ext5Metadata)
            WebExtensionSupport.installedExtensions["ext5"] = ext5

            val ext6: WebExtension = mock()
            whenever(ext6.id).thenReturn("ext6")
            whenever(ext6.url).thenReturn("some url")
            whenever(ext6.isEnabled()).thenReturn(true)
            val ext6Metadata: Metadata = mock()
            whenever(ext6Metadata.name).thenReturn("temporarily loaded extension - ext6")
            whenever(ext6Metadata.temporary).thenReturn(true)
            whenever(ext6.getMetadata()).thenReturn(ext6Metadata)
            WebExtensionSupport.installedExtensions["ext6"] = ext6

            val ext7: WebExtension = mock()
            whenever(ext7.id).thenReturn("ext7")
            whenever(ext7.isEnabled()).thenReturn(true)
            whenever(ext7.isBuiltIn()).thenReturn(true)
            WebExtensionSupport.installedExtensions["ext7"] = ext7

            // Verify add-ons were updated with state provided by the engine/store.
            val addons = AddonManager(store, mock(), addonsProvider, mock()).getAddons()
            assertEquals(6, addons.size)

            // ext1 should be installed.
            val addon1 = addons.find { it.id == "ext1" }!!

            assertEquals("ext1", addon1.id)
            assertNotNull(addon1.installedState)
            assertEquals("ext1", addon1.installedState.id)
            assertTrue(addon1.isEnabled())
            assertFalse(addon1.isDisabledAsUnsupported())
            assertNull(addon1.installedState.optionsPageUrl)
            assertFalse(addon1.installedState.openOptionsPageInTab)

            // ext2 should not be installed.
            val addon2 = addons.find { it.id == "ext2" }!!
            assertEquals("ext2", addon2.id)
            assertNull(addon2.installedState)

            // ext3 should still be disabled as unsupported.
            val addon3 = addons.find { it.id == "ext3" }!!
            assertEquals("ext3", addon3.id)
            assertNotNull(addon3.installedState)
            assertEquals("ext3", addon3.installedState.id)
            assertFalse(addon3.isEnabled())
            assertTrue(addon3.isDisabledAsUnsupported())
            assertEquals("http://options-page.moz", addon3.installedState.optionsPageUrl)
            assertTrue(addon3.installedState.openOptionsPageInTab)

            // ext4 should be installed.
            val addon4 = addons.find { it.id == "ext4" }!!
            assertEquals("ext4", addon4.id)
            assertNotNull(addon4.installedState)
            assertEquals("ext4", addon4.installedState.id)
            assertTrue(addon4.isEnabled())
            assertFalse(addon4.isDisabledAsUnsupported())
            assertNull(addon4.installedState.optionsPageUrl)
            assertFalse(addon4.installedState.openOptionsPageInTab)

            // ext5 should be installed.
            val addon5 = addons.find { it.id == "ext5" }!!
            assertEquals("ext5", addon5.id)
            assertNotNull(addon5.installedState)
            assertEquals("ext5", addon5.installedState.id)
            assertTrue(addon5.isEnabled())
            assertFalse(addon5.isDisabledAsUnsupported())
            assertNull(addon5.installedState.optionsPageUrl)
            assertFalse(addon5.installedState.openOptionsPageInTab)

            // ext6 should be installed.
            val addon6 = addons.find { it.id == "ext6" }!!
            assertEquals("ext6", addon6.id)
            assertNotNull(addon6.installedState)
            assertEquals("ext6", addon6.installedState.id)
            assertTrue(addon6.isEnabled())
            assertFalse(addon6.isDisabledAsUnsupported())
            assertNull(addon6.installedState.optionsPageUrl)
            assertFalse(addon6.installedState.openOptionsPageInTab)
        }

    @Test
    fun `getAddons - returns temporary add-ons`() =
        runTest(dispatcher) {
            val addonsProvider: AddonsProvider = mock()
            whenever(addonsProvider.getFeaturedAddons(anyBoolean(), eq(null), language = anyString()))
                .thenReturn(listOf())

            // Prepare engine
            val engine: Engine = mock()
            val callbackCaptor = argumentCaptor<((List<WebExtension>) -> Unit)>()
            whenever(engine.listInstalledWebExtensions(callbackCaptor.capture(), any())).thenAnswer {
                callbackCaptor.value.invoke(emptyList())
            }

            val store = BrowserStore()
            WebExtensionSupport.initialize(engine, store)

            // Add temporary extension
            val temporaryExtension: WebExtension = mock()
            val temporaryExtensionIcon: Bitmap = mock()
            val temporaryExtensionMetadata: Metadata = mock()
            whenever(temporaryExtensionMetadata.temporary).thenReturn(true)
            whenever(temporaryExtensionMetadata.name).thenReturn("name")
            whenever(temporaryExtension.id).thenReturn("temp_ext")
            whenever(temporaryExtension.url).thenReturn("site_url")
            whenever(temporaryExtension.getMetadata()).thenReturn(temporaryExtensionMetadata)
            WebExtensionSupport.installedExtensions["temp_ext"] = temporaryExtension

            val addonManager = spy(AddonManager(store, mock(), addonsProvider, mock()))

            whenever(addonManager.loadIcon(temporaryExtension)).thenReturn(temporaryExtensionIcon)

            val addons = addonManager.getAddons()
            assertEquals(1, addons.size)

            // Temporary extension should be returned
            assertEquals("temp_ext", addons[0].id)
            assertEquals(1, addons[0].translatableName.size)
            assertNotNull(addons[0].translatableName[addons[0].defaultLocale])
            assertTrue(addons[0].translatableName.containsValue("name"))
            assertNotNull(addons[0].installedState)
            assertEquals(temporaryExtensionIcon, addons[0].installedState!!.icon)
        }

    @Test
    fun `getAddons - filters unneeded locales on featured add-ons`() =
        runTest(dispatcher) {
            val addon =
                Addon(
                    id = "addon1",
                    translatableName =
                        mapOf(Addon.DEFAULT_LOCALE to "name", "invalid1" to "Name", "invalid2" to "nombre"),
                    translatableDescription =
                        mapOf(
                            Addon.DEFAULT_LOCALE to "description",
                            "invalid1" to "Beschreibung",
                            "invalid2" to "descripción",
                        ),
                    translatableSummary =
                        mapOf(Addon.DEFAULT_LOCALE to "summary", "invalid1" to "Kurzfassung", "invalid2" to "resumen"),
                )

            val store = BrowserStore()

            val engine: Engine = mock()
            val callbackCaptor = argumentCaptor<((List<WebExtension>) -> Unit)>()
            whenever(engine.listInstalledWebExtensions(callbackCaptor.capture(), any())).thenAnswer {
                callbackCaptor.value.invoke(emptyList())
            }

            val addonsProvider: AddonsProvider = mock()
            whenever(addonsProvider.getFeaturedAddons(anyBoolean(), eq(null), language = anyString()))
                .thenReturn(listOf(addon))
            WebExtensionSupport.initialize(engine, store)

            val addons = AddonManager(store, mock(), addonsProvider, mock()).getAddons()
            assertEquals(1, addons[0].translatableName.size)
            assertTrue(addons[0].translatableName.contains(addons[0].defaultLocale))
            assertEquals(1, addons[0].translatableDescription.size)
            assertTrue(addons[0].translatableDescription.contains(addons[0].defaultLocale))
            assertEquals(1, addons[0].translatableSummary.size)
            assertTrue(addons[0].translatableSummary.contains(addons[0].defaultLocale))
        }

    @Test
    fun `getAddons - filters unneeded locales on non-featured installed add-ons`() =
        runTest(dispatcher) {
            val addon =
                Addon(
                    id = "addon1",
                    translatableName =
                        mapOf(Addon.DEFAULT_LOCALE to "name", "invalid1" to "Name", "invalid2" to "nombre"),
                    translatableDescription =
                        mapOf(
                            Addon.DEFAULT_LOCALE to "description",
                            "invalid1" to "Beschreibung",
                            "invalid2" to "descripción",
                        ),
                    translatableSummary =
                        mapOf(Addon.DEFAULT_LOCALE to "summary", "invalid1" to "Kurzfassung", "invalid2" to "resumen"),
                )

            val store = BrowserStore()

            val engine: Engine = mock()
            val callbackCaptor = argumentCaptor<((List<WebExtension>) -> Unit)>()
            whenever(engine.listInstalledWebExtensions(callbackCaptor.capture(), any())).thenAnswer {
                callbackCaptor.value.invoke(emptyList())
            }

            val addonsProvider: AddonsProvider = mock()
            whenever(addonsProvider.getFeaturedAddons(anyBoolean(), eq(null), language = anyString()))
                .thenReturn(emptyList())
            WebExtensionSupport.initialize(engine, store)
            val extension: WebExtension = mock()
            whenever(extension.id).thenReturn(addon.id)
            whenever(extension.isEnabled()).thenReturn(true)
            whenever(extension.getMetadata()).thenReturn(mock())
            WebExtensionSupport.installedExtensions[addon.id] = extension

            val addons = AddonManager(store, mock(), addonsProvider, mock()).getAddons()
            assertEquals(1, addons[0].translatableName.size)
            assertTrue(addons[0].translatableName.contains(addons[0].defaultLocale))
            assertEquals(1, addons[0].translatableDescription.size)
            assertTrue(addons[0].translatableDescription.contains(addons[0].defaultLocale))
            assertEquals(1, addons[0].translatableSummary.size)
            assertTrue(addons[0].translatableSummary.contains(addons[0].defaultLocale))
        }

    @Test
    fun `getAddons - suspends until pending actions are completed`() =
        runTest(dispatcher) {
            val addon =
                Addon(
                    id = "ext1",
                    installedState = Addon.InstalledState("ext1", "1.0", "", true),
                )

            val extension: WebExtension = mock()
            whenever(extension.id).thenReturn("ext1")

            val store = BrowserStore()

            val engine: Engine = mock()
            val callbackCaptor = argumentCaptor<((List<WebExtension>) -> Unit)>()
            whenever(engine.listInstalledWebExtensions(callbackCaptor.capture(), any())).thenAnswer {
                callbackCaptor.value.invoke(emptyList())
            }
            val addonsProvider: AddonsProvider = mock()

            whenever(addonsProvider.getFeaturedAddons(anyBoolean(), eq(null), language = anyString()))
                .thenReturn(listOf(addon))
            WebExtensionSupport.initialize(engine, store)
            WebExtensionSupport.installedExtensions[addon.id] = extension

            val addonManager = AddonManager(store, mock(), addonsProvider, mock(), dispatcher)
            addonManager.installAddon(url = addon.downloadUrl)
            addonManager.enableAddon(addon)
            addonManager.disableAddon(addon)
            addonManager.uninstallAddon(addon)
            assertEquals(4, addonManager.pendingAddonActions.size)

            var getAddonsResult: List<Addon>? = null
            val nonSuspendingJob =
                CoroutineScope(Dispatchers.IO).launch {
                    getAddonsResult = addonManager.getAddons(waitForPendingActions = false)
                }

            nonSuspendingJob.join()
            assertNotNull(getAddonsResult)

            getAddonsResult = null
            val suspendingJob =
                CoroutineScope(Dispatchers.IO).launch {
                    getAddonsResult = addonManager.getAddons(waitForPendingActions = true)
                }

            addonManager.pendingAddonActions.forEach { it.complete(Unit) }

            suspendingJob.join()
            assertNotNull(getAddonsResult)
        }

    @Test
    fun `getAddons - passes on allowCache parameter`() =
        runTest(dispatcher) {
            val store = BrowserStore()

            val engine: Engine = mock()
            val callbackCaptor = argumentCaptor<((List<WebExtension>) -> Unit)>()
            whenever(engine.listInstalledWebExtensions(callbackCaptor.capture(), any())).thenAnswer {
                callbackCaptor.value.invoke(emptyList())
            }
            WebExtensionSupport.initialize(engine, store)

            val addonsProvider: AddonsProvider = mock()
            whenever(addonsProvider.getFeaturedAddons(anyBoolean(), eq(null), language = anyString()))
                .thenReturn(emptyList())
            val addonsManager = AddonManager(store, mock(), addonsProvider, mock(), dispatcher)

            addonsManager.getAddons()
            verify(addonsProvider).getFeaturedAddons(eq(true), eq(null), language = anyString())

            addonsManager.getAddons(allowCache = false)
            verify(addonsProvider).getFeaturedAddons(eq(false), eq(null), language = anyString())
        }

    @Test
    fun `getAddons - passes readTimeoutInSeconds parameter dependent on installed extensions`() =
        runTest(dispatcher) {
            val store = BrowserStore()

            val engine: Engine = mock()
            val callbackCaptor = argumentCaptor<((List<WebExtension>) -> Unit)>()
            whenever(engine.listInstalledWebExtensions(callbackCaptor.capture(), any())).thenAnswer {
                callbackCaptor.value.invoke(emptyList())
            }
            WebExtensionSupport.initialize(engine, store)

            // Built-in extensions don't count as an installed extension, as they are not displayed to the user.
            val ext1: WebExtension = mock()
            whenever(ext1.id).thenReturn("ext1")
            whenever(ext1.isEnabled()).thenReturn(true)
            whenever(ext1.isBuiltIn()).thenReturn(true)
            WebExtensionSupport.installedExtensions["ext1"] = ext1

            val addonsProvider: AddonsProvider = mock()
            whenever(
                    addonsProvider.getFeaturedAddons(anyBoolean(), readTimeoutInSeconds = any(), language = anyString())
                )
                .thenReturn(emptyList())
            val addonsManager = AddonManager(store, mock(), addonsProvider, mock(), dispatcher)

            addonsManager.getAddons()
            verify(addonsProvider).getFeaturedAddons(eq(true), readTimeoutInSeconds = eq(null), language = anyString())
            // ^ readTimeoutInSeconds is null, so the default is used.

            // This counts as an installed extension
            val ext2: WebExtension = mock()
            whenever(ext2.id).thenReturn("ext2")
            whenever(ext2.isEnabled()).thenReturn(true)
            whenever(ext2.isBuiltIn()).thenReturn(false)
            WebExtensionSupport.installedExtensions["ext2"] = ext2

            addonsManager.getAddons()
            verify(addonsProvider).getFeaturedAddons(eq(true), readTimeoutInSeconds = eq(3L), language = anyString())
            // ^ readTimeoutInSeconds is now minimal to ensure quick loading (bug 1949963).
        }

    @Test
    fun `updateAddon - when a extension is updated successfully`() {
        val engine: Engine = mock()
        val engineSession: EngineSession = mock()
        val captureActionsMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val store =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs =
                            listOf(
                                createTab(
                                    id = "1",
                                    url = "https://www.mozilla.org",
                                    engineSession = engineSession,
                                )
                            ),
                        extensions = mapOf("extensionId" to mock()),
                    ),
                middleware = listOf(captureActionsMiddleware),
            )
        val onSuccessCaptor = argumentCaptor<((WebExtension?) -> Unit)>()
        var updateStatus: Status? = null
        val manager = AddonManager(store, engine, mock(), mock(), mock())

        val updatedExt: WebExtension = mock()
        whenever(updatedExt.id).thenReturn("extensionId")
        whenever(updatedExt.url).thenReturn("url")
        whenever(updatedExt.supportActions).thenReturn(true)

        WebExtensionSupport.installedExtensions["extensionId"] = mock()

        val oldExt = WebExtensionSupport.installedExtensions["extensionId"]

        manager.updateAddon("extensionId") { status ->
            updateStatus = status
        }

        val actionHandlerCaptor = argumentCaptor<ActionHandler>()

        // Verifying we returned the right status
        verify(engine).updateWebExtension(any(), onSuccessCaptor.capture(), any())
        onSuccessCaptor.value.invoke(updatedExt)
        assertEquals(Status.SuccessfullyUpdated, updateStatus)

        // Verifying we updated the extension in WebExtensionSupport
        assertNotEquals(oldExt, WebExtensionSupport.installedExtensions["extensionId"])
        assertEquals(updatedExt, WebExtensionSupport.installedExtensions["extensionId"])

        // Verifying we updated the extension in the store
        captureActionsMiddleware.assertFirstAction(WebExtensionAction.UpdateWebExtensionAction::class) { action ->
            assertEquals(
                WebExtensionState(
                    updatedExt.id,
                    updatedExt.url,
                    updatedExt.getMetadata()?.name,
                    updatedExt.isEnabled(),
                ),
                action.updatedExtension,
            )
        }

        // Verify that we registered an action handler for all existing sessions on the extension
        verify(updatedExt).registerActionHandler(eq(engineSession), actionHandlerCaptor.capture())
        actionHandlerCaptor.value.onBrowserAction(updatedExt, engineSession, mock())
    }

    @Test
    fun `updateAddon - when extension is not installed`() {
        var updateStatus: Status? = null

        val manager = AddonManager(BrowserStore(), mock(), mock(), mock(), mock())

        manager.updateAddon("extensionId") { status ->
            updateStatus = status
        }

        assertEquals(Status.NotInstalled, updateStatus)
    }

    @Test
    fun `updateAddon - when extension is not supported`() {
        var updateStatus: Status? = null

        val extension: WebExtension = mock()
        whenever(extension.id).thenReturn("unsupportedExt")

        val metadata: Metadata = mock()
        whenever(metadata.disabledFlags).thenReturn(DisabledFlags.select(APP_SUPPORT))
        whenever(extension.getMetadata()).thenReturn(metadata)

        WebExtensionSupport.installedExtensions["extensionId"] = extension

        val manager = AddonManager(BrowserStore(), mock(), mock(), mock(), mock())
        manager.updateAddon("extensionId") { status ->
            updateStatus = status
        }

        assertEquals(Status.NotInstalled, updateStatus)
    }

    @Test
    fun `updateAddon - when an error happens while updating`() {
        val engine: Engine = mock()
        val onErrorCaptor = argumentCaptor<((String, Throwable) -> Unit)>()
        var updateStatus: Status? = null
        val manager = AddonManager(BrowserStore(), engine, mock(), mock(), mock())

        WebExtensionSupport.installedExtensions["extensionId"] = mock()

        manager.updateAddon("extensionId") { status ->
            updateStatus = status
        }

        // Verifying we returned the right status
        verify(engine).updateWebExtension(any(), any(), onErrorCaptor.capture())
        onErrorCaptor.value.invoke("message", Exception())
        assertIs<Status.Error>(updateStatus)
    }

    @Test
    fun `updateAddon - when there is not new updates for the extension`() {
        val engine: Engine = mock()
        val onSuccessCaptor = argumentCaptor<((WebExtension?) -> Unit)>()
        var updateStatus: Status? = null
        val manager = AddonManager(BrowserStore(), engine, mock(), mock(), mock())

        WebExtensionSupport.installedExtensions["extensionId"] = mock()
        manager.updateAddon("extensionId") { status ->
            updateStatus = status
        }

        verify(engine).updateWebExtension(any(), onSuccessCaptor.capture(), any())
        onSuccessCaptor.value.invoke(null)
        assertEquals(Status.NoUpdateAvailable, updateStatus)
    }

    @Test
    fun `installAddon successfully`() {
        val addon = Addon(id = "ext1")
        val engine: Engine = mock()
        val onSuccessCaptor = argumentCaptor<((WebExtension) -> Unit)>()

        var installedAddon: Addon? = null
        val manager = AddonManager(BrowserStore(), engine, mock(), mock(), mock())
        manager.installAddon(
            url = addon.downloadUrl,
            installationMethod = InstallationMethod.MANAGER,
            onSuccess = {
                installedAddon = it
            },
        )

        verify(engine)
            .installWebExtension(
                any(),
                eq(InstallationMethod.MANAGER),
                onSuccessCaptor.capture(),
                any(),
            )

        val metadata: Metadata = mock()
        val extension: WebExtension = mock()
        whenever(metadata.name).thenReturn("nameFromMetadata")
        whenever(extension.id).thenReturn("ext1")
        whenever(extension.getMetadata()).thenReturn(metadata)
        onSuccessCaptor.value.invoke(extension)
        assertNotNull(installedAddon)
        assertEquals(addon.id, installedAddon.id)
        assertEquals("nameFromMetadata", installedAddon.translateName(testContext))
        assertTrue(manager.pendingAddonActions.isEmpty())
    }

    @Test
    fun `installAddon failure`() {
        val addon = Addon(id = "ext1")
        val engine: Engine = mock()
        val onErrorCaptor = argumentCaptor<((Throwable) -> Unit)>()

        var throwable: Throwable? = null
        val manager = AddonManager(BrowserStore(), engine, mock(), mock(), mock())
        manager.installAddon(
            url = addon.downloadUrl,
            installationMethod = InstallationMethod.FROM_FILE,
            onError = { caught ->
                throwable = caught
            },
        )

        verify(engine)
            .installWebExtension(
                url = any(),
                installationMethod = eq(InstallationMethod.FROM_FILE),
                onSuccess = any(),
                onError = onErrorCaptor.capture(),
            )

        onErrorCaptor.value.invoke(IllegalStateException("test"))
        assertNotNull(throwable)
        assertTrue(manager.pendingAddonActions.isEmpty())
    }

    @Test
    fun `uninstallAddon successfully`() {
        val installedAddon =
            Addon(
                id = "ext1",
                installedState = Addon.InstalledState("ext1", "1.0", "", true),
            )

        val extension: WebExtension = mock()
        whenever(extension.id).thenReturn("ext1")
        WebExtensionSupport.installedExtensions[installedAddon.id] = extension

        val engine: Engine = mock()
        val onSuccessCaptor = argumentCaptor<(() -> Unit)>()

        var successCallbackInvoked = false
        val manager = AddonManager(BrowserStore(), engine, mock(), mock(), mock())
        manager.uninstallAddon(
            installedAddon,
            onSuccess = {
                successCallbackInvoked = true
            },
        )
        verify(engine).uninstallWebExtension(eq(extension), onSuccessCaptor.capture(), any())

        onSuccessCaptor.value.invoke()
        assertTrue(successCallbackInvoked)
        assertTrue(manager.pendingAddonActions.isEmpty())
    }

    @Test
    fun `uninstallAddon failure cases`() {
        val addon = Addon(id = "ext1")
        val engine: Engine = mock()
        val onErrorCaptor = argumentCaptor<((String, Throwable) -> Unit)>()
        var throwable: Throwable? = null
        var msg: String? = null
        val errorCallback = { errorMsg: String, caught: Throwable ->
            throwable = caught
            msg = errorMsg
        }
        val manager = AddonManager(BrowserStore(), engine, mock(), mock(), mock())

        // Extension is not installed so we're invoking the error callback and never the engine
        manager.uninstallAddon(addon, onError = errorCallback)
        verify(engine, never()).uninstallWebExtension(any(), any(), onErrorCaptor.capture())
        assertNotNull(throwable)
        assertEquals("Addon is not installed", throwable.localizedMessage)

        // Install extension and try again
        val extension: WebExtension = mock()
        whenever(extension.id).thenReturn("ext1")
        WebExtensionSupport.installedExtensions[addon.id] = extension
        manager.uninstallAddon(addon, onError = errorCallback)
        verify(engine, never()).uninstallWebExtension(any(), any(), onErrorCaptor.capture())

        // Make sure engine error is forwarded to caller
        val installedAddon = addon.copy(installedState = Addon.InstalledState(addon.id, "1.0", "", true))
        manager.uninstallAddon(installedAddon, onError = errorCallback)
        verify(engine).uninstallWebExtension(eq(extension), any(), onErrorCaptor.capture())
        onErrorCaptor.value.invoke(addon.id, IllegalStateException("test"))
        assertNotNull(throwable)
        assertEquals("test", throwable.localizedMessage)
        assertEquals(msg, addon.id)
        assertTrue(manager.pendingAddonActions.isEmpty())
    }

    @Test
    fun `add optional permissions successfully`() {
        val permission = listOf("permission1")
        val origin = listOf("origin")
        val addon =
            Addon(
                id = "ext1",
                installedState = Addon.InstalledState("ext1", "1.0", "", true),
            )

        val extension: WebExtension = mock()
        whenever(extension.id).thenReturn("ext1")
        WebExtensionSupport.installedExtensions[addon.id] = extension
        val metadata: Metadata = mock()
        whenever(extension.getMetadata()).thenReturn(metadata)
        whenever(metadata.optionalPermissions).thenReturn(permission)
        whenever(metadata.grantedOptionalPermissions).thenReturn(permission)
        whenever(metadata.optionalOrigins).thenReturn(origin)
        whenever(metadata.grantedOptionalOrigins).thenReturn(origin)
        val engine: Engine = mock()
        val onSuccessCaptor = argumentCaptor<((WebExtension) -> Unit)>()

        var updateAddon: Addon? = null
        val manager = AddonManager(BrowserStore(), engine, mock(), mock(), mock())
        manager.addOptionalPermission(
            addon,
            permission,
            origin,
            onSuccess = {
                updateAddon = it
            },
        )

        verify(engine).addOptionalPermissions(eq(extension.id), any(), any(), any(), onSuccessCaptor.capture(), any())
        onSuccessCaptor.value.invoke(extension)
        assertNotNull(updateAddon)
        assertEquals(addon.id, updateAddon.id)
        assertEquals("permission1", updateAddon.optionalPermissions.first().name)
        assertEquals(true, updateAddon.optionalPermissions.first().granted)
        assertEquals("origin", updateAddon.optionalOrigins.first().name)
        assertEquals(true, updateAddon.optionalOrigins.first().granted)
        assertTrue(manager.pendingAddonActions.isEmpty())
    }

    @Test
    fun `add optional with empty permissions and origins`() {
        var onErrorWasExecuted = false
        val manager = AddonManager(BrowserStore(), mock(), mock(), mock(), mock())

        manager.addOptionalPermission(
            mock(),
            emptyList(),
            emptyList(),
            onError = {
                onErrorWasExecuted = true
            },
        )

        assertTrue(onErrorWasExecuted)
    }

    @Test
    fun `remove optional permissions successfully`() {
        val permission = listOf("permission1")
        val origins = listOf("origin")
        val addon =
            Addon(
                id = "ext1",
                installedState = Addon.InstalledState("ext1", "1.0", "", true),
            )

        val extension: WebExtension = mock()
        whenever(extension.id).thenReturn("ext1")
        WebExtensionSupport.installedExtensions[addon.id] = extension

        val engine: Engine = mock()
        val onSuccessCaptor = argumentCaptor<((WebExtension) -> Unit)>()

        var updateAddon: Addon? = null
        val manager = AddonManager(BrowserStore(), engine, mock(), mock(), mock())
        manager.removeOptionalPermission(
            addon,
            permission,
            origins,
            onSuccess = {
                updateAddon = it
            },
        )

        verify(engine)
            .removeOptionalPermissions(eq(extension.id), any(), any(), any(), onSuccessCaptor.capture(), any())
        onSuccessCaptor.value.invoke(extension)
        assertNotNull(updateAddon)
        assertEquals(addon.id, updateAddon.id)
        assertTrue(manager.pendingAddonActions.isEmpty())
    }

    @Test
    fun `remove optional with empty permissions and origins`() {
        var onErrorWasExecuted = false
        val manager = AddonManager(BrowserStore(), mock(), mock(), mock(), mock())

        manager.removeOptionalPermission(
            mock(),
            emptyList(),
            emptyList(),
            onError = {
                onErrorWasExecuted = true
            },
        )

        assertTrue(onErrorWasExecuted)
    }

    @Test
    fun `enableAddon successfully`() {
        val addon =
            Addon(
                id = "ext1",
                installedState = Addon.InstalledState("ext1", "1.0", "", true),
            )

        val extension: WebExtension = mock()
        whenever(extension.id).thenReturn("ext1")
        WebExtensionSupport.installedExtensions[addon.id] = extension

        val engine: Engine = mock()
        val onSuccessCaptor = argumentCaptor<((WebExtension) -> Unit)>()

        var enabledAddon: Addon? = null
        val manager = AddonManager(BrowserStore(), engine, mock(), mock(), mock())
        manager.enableAddon(
            addon,
            onSuccess = {
                enabledAddon = it
            },
        )

        verify(engine).enableWebExtension(eq(extension), any(), onSuccessCaptor.capture(), any())
        onSuccessCaptor.value.invoke(extension)
        assertNotNull(enabledAddon)
        assertEquals(addon.id, enabledAddon.id)
        assertTrue(manager.pendingAddonActions.isEmpty())
    }

    @Test
    fun `enableAddon failure cases`() {
        val addon = Addon(id = "ext1")
        val engine: Engine = mock()
        val onErrorCaptor = argumentCaptor<((Throwable) -> Unit)>()
        var throwable: Throwable? = null
        val errorCallback = { caught: Throwable ->
            throwable = caught
        }
        val manager = AddonManager(BrowserStore(), engine, mock(), mock(), mock())

        // Extension is not installed so we're invoking the error callback and never the engine
        manager.enableAddon(addon, onError = errorCallback)
        verify(engine, never()).enableWebExtension(any(), any(), any(), onErrorCaptor.capture())
        assertNotNull(throwable)
        assertEquals("Addon is not installed", throwable.localizedMessage)

        // Install extension and try again
        val extension: WebExtension = mock()
        whenever(extension.id).thenReturn("ext1")
        WebExtensionSupport.installedExtensions[addon.id] = extension
        manager.enableAddon(addon, onError = errorCallback)
        verify(engine, never()).enableWebExtension(any(), any(), any(), onErrorCaptor.capture())

        // Make sure engine error is forwarded to caller
        val installedAddon = addon.copy(installedState = Addon.InstalledState(addon.id, "1.0", "", true))
        manager.enableAddon(installedAddon, source = EnableSource.APP_SUPPORT, onError = errorCallback)
        verify(engine).enableWebExtension(eq(extension), eq(EnableSource.APP_SUPPORT), any(), onErrorCaptor.capture())
        onErrorCaptor.value.invoke(IllegalStateException("test"))
        assertNotNull(throwable)
        assertEquals("test", throwable.localizedMessage)
        assertTrue(manager.pendingAddonActions.isEmpty())
    }

    @Test
    fun `disableAddon successfully`() {
        val addon =
            Addon(
                id = "ext1",
                installedState = Addon.InstalledState("ext1", "1.0", "", true),
            )

        val extension: WebExtension = mock()
        whenever(extension.id).thenReturn("ext1")
        WebExtensionSupport.installedExtensions[addon.id] = extension

        val engine: Engine = mock()
        val onSuccessCaptor = argumentCaptor<((WebExtension) -> Unit)>()

        var disabledAddon: Addon? = null
        val manager = AddonManager(BrowserStore(), engine, mock(), mock(), mock())
        manager.disableAddon(
            addon,
            source = EnableSource.APP_SUPPORT,
            onSuccess = {
                disabledAddon = it
            },
        )

        verify(engine)
            .disableWebExtension(eq(extension), eq(EnableSource.APP_SUPPORT), onSuccessCaptor.capture(), any())
        onSuccessCaptor.value.invoke(extension)
        assertNotNull(disabledAddon)
        assertEquals(addon.id, disabledAddon.id)
        assertTrue(manager.pendingAddonActions.isEmpty())
    }

    @Test
    fun `disableAddon failure cases`() {
        val addon = Addon(id = "ext1")
        val engine: Engine = mock()
        val onErrorCaptor = argumentCaptor<((Throwable) -> Unit)>()
        var throwable: Throwable? = null
        val errorCallback = { caught: Throwable ->
            throwable = caught
        }
        val manager = AddonManager(BrowserStore(), engine, mock(), mock(), mock())

        // Extension is not installed so we're invoking the error callback and never the engine
        manager.disableAddon(addon, onError = errorCallback)
        verify(engine, never()).disableWebExtension(any(), any(), any(), onErrorCaptor.capture())
        assertNotNull(throwable)
        assertEquals("Addon is not installed", throwable.localizedMessage)

        // Install extension and try again
        val extension: WebExtension = mock()
        whenever(extension.id).thenReturn("ext1")
        WebExtensionSupport.installedExtensions[addon.id] = extension
        manager.disableAddon(addon, onError = errorCallback)
        verify(engine, never()).disableWebExtension(any(), any(), any(), onErrorCaptor.capture())

        // Make sure engine error is forwarded to caller
        val installedAddon = addon.copy(installedState = Addon.InstalledState(addon.id, "1.0", "", true))
        manager.disableAddon(installedAddon, onError = errorCallback)
        verify(engine).disableWebExtension(eq(extension), any(), any(), onErrorCaptor.capture())
        onErrorCaptor.value.invoke(IllegalStateException("test"))
        assertNotNull(throwable)
        assertEquals("test", throwable.localizedMessage)
        assertTrue(manager.pendingAddonActions.isEmpty())
    }

    @Test
    fun `toInstalledState read from icon cache`() {
        val extension: WebExtension = mock()
        val metadata: Metadata = mock()

        val manager = spy(AddonManager(BrowserStore(), mock(), mock(), mock(), mock()))

        manager.iconsCache["ext1"] = mock()
        whenever(extension.id).thenReturn("ext1")
        whenever(extension.getMetadata()).thenReturn(metadata)
        whenever(extension.isEnabled()).thenReturn(true)
        whenever(extension.getDisabledReason()).thenReturn(null)
        whenever(extension.isAllowedInPrivateBrowsing()).thenReturn(true)
        whenever(metadata.version).thenReturn("version")
        whenever(metadata.optionsPageUrl).thenReturn("optionsPageUrl")
        whenever(metadata.openOptionsPageInTab).thenReturn(true)

        val installedExtension = manager.toInstalledState(extension)

        assertEquals(manager.iconsCache["ext1"], installedExtension.icon)
        assertEquals("version", installedExtension.version)
        assertEquals("optionsPageUrl", installedExtension.optionsPageUrl)
        assertNull(installedExtension.disabledReason)
        assertTrue(installedExtension.openOptionsPageInTab)
        assertTrue(installedExtension.enabled)
        assertTrue(installedExtension.allowedInPrivateBrowsing)

        verify(manager, times(0)).loadIcon(eq(extension))
    }

    @Test
    fun `toInstalledState load icon when cache is not available`() {
        val extension: WebExtension = mock()
        val metadata: Metadata = mock()

        val manager = spy(AddonManager(BrowserStore(), mock(), mock(), mock(), mock()))

        whenever(extension.id).thenReturn("ext1")
        whenever(extension.getMetadata()).thenReturn(metadata)
        whenever(extension.isEnabled()).thenReturn(true)
        whenever(extension.getDisabledReason()).thenReturn(null)
        whenever(extension.isAllowedInPrivateBrowsing()).thenReturn(true)
        whenever(metadata.version).thenReturn("version")
        whenever(metadata.optionsPageUrl).thenReturn("optionsPageUrl")
        whenever(metadata.openOptionsPageInTab).thenReturn(true)

        val installedExtension = manager.toInstalledState(extension)

        assertEquals(manager.iconsCache["ext1"], installedExtension.icon)
        assertEquals("version", installedExtension.version)
        assertEquals("optionsPageUrl", installedExtension.optionsPageUrl)
        assertNull(installedExtension.disabledReason)
        assertTrue(installedExtension.openOptionsPageInTab)
        assertTrue(installedExtension.enabled)
        assertTrue(installedExtension.allowedInPrivateBrowsing)

        verify(manager).loadIcon(extension)
    }

    @Test
    fun `loadIcon try to load the icon from extension`() =
        runTest(dispatcher) {
            val extension: WebExtension = mock()

            val manager = spy(AddonManager(BrowserStore(), mock(), mock(), mock(), mock()))

            whenever(extension.loadIcon(AddonManager.ADDON_ICON_SIZE)).thenReturn(mock())

            val icon = manager.loadIcon(extension)

            assertNotNull(icon)
        }

    @Test
    fun `loadIcon calls tryLoadIconInBackground when TimeoutCancellationException`() = runTest {
        val extension: WebExtension = mock()

        val manager = spy(AddonManager(BrowserStore(), mock(), mock(), mock(), mock()))
        doNothing().`when`(manager).tryLoadIconInBackground(extension)

        doThrow(mock<TimeoutCancellationException>()).`when`(extension).loadIcon(AddonManager.ADDON_ICON_SIZE)

        val icon = manager.loadIcon(extension)

        assertNull(icon)
        verify(manager).loadIcon(extension)
    }

    @Test
    fun `getDisabledReason cases`() {
        val extension: WebExtension = mock()
        val metadata: Metadata = mock()
        whenever(extension.getMetadata()).thenReturn(metadata)

        whenever(metadata.disabledFlags).thenReturn(DisabledFlags.select(BLOCKLIST))
        assertEquals(Addon.DisabledReason.BLOCKLISTED, extension.getDisabledReason())

        whenever(metadata.disabledFlags).thenReturn(DisabledFlags.select(APP_SUPPORT))
        assertEquals(Addon.DisabledReason.UNSUPPORTED, extension.getDisabledReason())

        whenever(metadata.disabledFlags).thenReturn(DisabledFlags.select(USER))
        assertEquals(Addon.DisabledReason.USER_REQUESTED, extension.getDisabledReason())

        whenever(metadata.disabledFlags).thenReturn(DisabledFlags.select(SIGNATURE))
        assertEquals(Addon.DisabledReason.NOT_CORRECTLY_SIGNED, extension.getDisabledReason())

        whenever(metadata.disabledFlags).thenReturn(DisabledFlags.select(APP_VERSION))
        assertEquals(Addon.DisabledReason.INCOMPATIBLE, extension.getDisabledReason())

        whenever(metadata.disabledFlags).thenReturn(DisabledFlags.select(SOFT_BLOCKLIST))
        assertEquals(Addon.DisabledReason.SOFT_BLOCKED, extension.getDisabledReason())

        whenever(metadata.disabledFlags).thenReturn(DisabledFlags.select(0))
        assertNull(extension.getDisabledReason())
    }
}
