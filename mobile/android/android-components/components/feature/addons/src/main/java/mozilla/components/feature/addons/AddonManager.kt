/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.addons

import android.graphics.Bitmap
import android.os.Handler
import android.os.HandlerThread
import androidx.annotation.VisibleForTesting
import java.util.Collections.newSetFromMap
import java.util.Locale
import java.util.concurrent.ConcurrentHashMap
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.TimeoutCancellationException
import kotlinx.coroutines.android.asCoroutineDispatcher
import kotlinx.coroutines.awaitAll
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.CancellableOperation
import mozilla.components.concept.engine.webextension.DisabledFlags
import mozilla.components.concept.engine.webextension.EnableSource
import mozilla.components.concept.engine.webextension.InstallationMethod
import mozilla.components.concept.engine.webextension.WebExtension
import mozilla.components.concept.engine.webextension.WebExtensionRuntime
import mozilla.components.concept.engine.webextension.isBlockListed
import mozilla.components.concept.engine.webextension.isDisabledIncompatible
import mozilla.components.concept.engine.webextension.isDisabledUnsigned
import mozilla.components.concept.engine.webextension.isSoftBlocked
import mozilla.components.concept.engine.webextension.isUnsupported
import mozilla.components.feature.addons.update.AddonUpdater
import mozilla.components.feature.addons.update.AddonUpdater.Status
import mozilla.components.support.webextensions.WebExtensionSupport
import mozilla.components.support.webextensions.WebExtensionSupport.installedExtensions

internal const val SHORT_READ_TIMEOUT_IN_SECONDS = 3L

/**
 * Provides access to installed and recommended [Addon]s and manages their states.
 *
 * @property store The application's [BrowserStore].
 * @property runtime The application's [WebExtensionRuntime] to install and configure extensions.
 * @property addonsProvider The [AddonsProvider] to query available [Addon]s.
 * @property addonUpdater The [AddonUpdater] instance to use when checking / triggering updates.
 * @param ioDispatcher Coroutine dispatcher for IO operations.
 */
@Suppress("LargeClass")
class AddonManager(
    private val store: BrowserStore,
    private val runtime: WebExtensionRuntime,
    private val addonsProvider: AddonsProvider,
    private val addonUpdater: AddonUpdater,
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
) {

    @VisibleForTesting
    internal val pendingAddonActions = newSetFromMap(ConcurrentHashMap<CompletableDeferred<Unit>, Boolean>())

    @VisibleForTesting internal val iconLoadingScope = CoroutineScope(Dispatchers.IO)

    // Acts as an in-memory cache for the fetched addon's icons.
    @VisibleForTesting internal val iconsCache = ConcurrentHashMap<String, Bitmap>()

    /**
     * Returns the list of all installed and featured add-ons.
     *
     * @param waitForPendingActions whether or not to wait (suspend, but not block) until all pending add-on actions
     *   (install/uninstall/enable/disable) are completed in either success or failure.
     * @param allowCache whether or not the result may be provided from a previously cached response, defaults to true.
     * @return list of all [Addon]s with up-to-date [Addon.installedState].
     * @throws AddonManagerException in case of a problem reading from the [addonsProvider] or querying web extension
     *   state from the engine / store.
     */
    @Throws(AddonManagerException::class)
    @Suppress("TooGenericExceptionCaught")
    suspend fun getAddons(
        waitForPendingActions: Boolean = true,
        allowCache: Boolean = true,
    ): List<Addon> =
        withContext(ioDispatcher) {
            try {
                // Make sure extension support is initialized, i.e. the state of all installed extensions is known.
                WebExtensionSupport.awaitInitialization()

                // Make sure all pending actions are completed.
                if (waitForPendingActions) {
                    pendingAddonActions.awaitAll()
                }

                val readTimeoutInSeconds =
                    if (installedExtensions.any { !it.value.isBuiltIn() }) {
                        // When the user has already installed any extension, they most likely want to
                        // manage an existing extension. To avoid excessive delays when the network is
                        // slow, choose a very conservative deadline.
                        SHORT_READ_TIMEOUT_IN_SECONDS
                    } else {
                        // When the set of installed extensions is empty, there is no criticial information
                        // that needs to be displayed sooner. Thus we can patiently wait until the featured
                        // extensions have been fetched (until the default DEFAULT_READ_TIMEOUT_IN_SECONDS
                        // timeout is reached).
                        null
                    }

                // Get all the featured add-ons not installed from provider.
                // NB: We're keeping translations only for the default locale.
                var featuredAddons = emptyList<Addon>()
                try {
                    val userLanguage = Locale.getDefault().language
                    val locales = listOf(userLanguage)
                    featuredAddons =
                        addonsProvider
                            .getFeaturedAddons(allowCache, readTimeoutInSeconds, language = userLanguage)
                            .filter { addon -> !installedExtensions.containsKey(addon.id) }
                            .map { addon -> addon.filterTranslations(locales) }
                } catch (throwable: Throwable) {
                    // Do not throw when we fail to fetch the featured add-ons since there can be installed add-ons.
                    logger.warn("Failed to get the featured add-ons", throwable)
                }

                // Build a list of installed extensions that are not built-in extensions.
                val installedAddons =
                    installedExtensions
                        .filterValues { !it.isBuiltIn() }
                        .map {
                            val extension = it.value
                            val installedState = toInstalledState(extension)
                            Addon.newFromWebExtension(extension, installedState)
                        }

                return@withContext featuredAddons + installedAddons
            } catch (throwable: Throwable) {
                throw AddonManagerException(throwable)
            }
        }

    /**
     * Returns the [Addon] for an installed extension with the given ID.
     *
     * @param addonId The id of the installed add-on.
     * @throws AddonManagerException in case of a problem reading from the [addonsProvider] or querying web extension
     *   state from the engine / store.
     */
    @Throws(AddonManagerException::class)
    @Suppress("TooGenericExceptionCaught")
    suspend fun getAddonByID(addonId: String): Addon? =
        withContext(ioDispatcher) {
            try {
                WebExtensionSupport.awaitInitialization()

                // If there was a way to wait for a specific add-on, we would, but for now just await
                // all pending add-on actions.
                pendingAddonActions.awaitAll()

                val addon =
                    installedExtensions[addonId]?.let { extension ->
                        val installedState = toInstalledState(extension)
                        Addon.newFromWebExtension(extension, installedState)
                    }
                return@withContext addon
            } catch (throwable: Throwable) {
                throw AddonManagerException(throwable)
            }
        }

    /**
     * Installs an [Addon] from the provided [url].
     *
     * @param url the url pointing to either a resources path for locating the extension within the APK file (e.g.
     *   resource://android/assets/extensions/my_web_ext) or to a local (e.g.
     *   resource://android/assets/extensions/my_web_ext.xpi) or remote (e.g.
     *   https://addons.mozilla.org/firefox/downloads/file/123/some_web_ext.xpi) XPI file.
     * @param installationMethod (optional) the method used to install a [Addon].
     * @param onSuccess (optional) callback invoked if the addon was installed successfully, providing access to the
     *   [Addon] object.
     * @param onError (optional) callback invoked if there was an error installing the addon.
     */
    fun installAddon(
        url: String,
        installationMethod: InstallationMethod? = null,
        onSuccess: ((Addon) -> Unit) = {},
        onError: ((Throwable) -> Unit) = { _ -> },
    ): CancellableOperation {
        val pendingAction = addPendingAddonAction()
        return runtime.installWebExtension(
            url = url,
            installationMethod = installationMethod,
            onSuccess = { ext ->
                onAddonInstalled(ext, pendingAction, onSuccess)
            },
            onError = { throwable ->
                completePendingAddonAction(pendingAction)
                onError(throwable)
            },
        )
    }

    /**
     * Uninstalls the provided [Addon].
     *
     * @param addon the addon to uninstall.
     * @param onSuccess (optional) callback invoked if the addon was uninstalled successfully.
     * @param onError (optional) callback invoked if there was an error uninstalling the addon.
     */
    fun uninstallAddon(
        addon: Addon,
        onSuccess: (() -> Unit) = {},
        onError: ((String, Throwable) -> Unit) = { _, _ -> },
    ) {
        val extension = addon.installedState?.let { installedExtensions[it.id] }
        if (extension == null) {
            onError(addon.id, IllegalStateException("Addon is not installed"))
            return
        }

        val pendingAction = addPendingAddonAction()
        runtime.uninstallWebExtension(
            extension,
            onSuccess = {
                addonUpdater.unregisterForFutureUpdates(addon.id)
                completePendingAddonAction(pendingAction)
                onSuccess()
            },
            onError = { id, throwable ->
                completePendingAddonAction(pendingAction)
                onError(id, throwable)
            },
        )
    }

    /**
     * Enables the provided [Addon].
     *
     * @param addon the [Addon] to enable.
     * @param source [EnableSource] to indicate why the extension is enabled, default to EnableSource.USER.
     * @param onSuccess (optional) callback invoked with the enabled [Addon].
     * @param onError (optional) callback invoked if there was an error enabling
     */
    fun enableAddon(
        addon: Addon,
        source: EnableSource = EnableSource.USER,
        onSuccess: ((Addon) -> Unit) = {},
        onError: ((Throwable) -> Unit) = {},
    ) {
        val extension = addon.installedState?.let { installedExtensions[it.id] }
        if (extension == null) {
            onError(IllegalStateException("Addon is not installed"))
            return
        }

        val pendingAction = addPendingAddonAction()
        runtime.enableWebExtension(
            extension,
            source = source,
            onSuccess = { ext ->
                val enabledAddon = addon.copy(installedState = toInstalledState(ext))
                completePendingAddonAction(pendingAction)
                onSuccess(enabledAddon)
            },
            onError = {
                completePendingAddonAction(pendingAction)
                onError(it)
            },
        )
    }

    /**
     * Add the provided [permissions], [origins] and/or [dataCollectionPermissions] to [Addon].
     *
     * @param addon the [Addon] to add the provided [permissions] and [origins].
     * @param permissions the permissions to add, pass an empty array for opt-out.
     * @param origins the origins to add, pass an empty array for opt-out.
     * @param dataCollectionPermissions the data collection permissions to add, pass an empty array for opt-out.
     * @param onSuccess (optional) callback invoked with the added permissions and origins.
     * @param onError (optional) callback invoked if there was an error adding.
     */
    fun addOptionalPermission(
        addon: Addon,
        permissions: List<String> = emptyList(),
        origins: List<String> = emptyList(),
        dataCollectionPermissions: List<String> = emptyList(),
        onSuccess: ((Addon) -> Unit) = {},
        onError: ((Throwable) -> Unit) = {},
    ) {
        if (permissions.isEmpty() && origins.isEmpty() && dataCollectionPermissions.isEmpty()) {
            onError(
                IllegalStateException(
                    "At least one of permissions, origins or dataCollectionPermissions must not be empty"
                )
            )
            return
        }
        val extension = addon.installedState?.let { installedExtensions[it.id] }
        if (extension == null) {
            onError(IllegalStateException("Addon is not installed"))
            return
        }

        val pendingAction = addPendingAddonAction()
        runtime.addOptionalPermissions(
            extension.id,
            permissions = permissions,
            origins = origins,
            dataCollectionPermissions = dataCollectionPermissions,
            onSuccess = { ext ->
                val updatedAddon = Addon.newFromWebExtension(ext, toInstalledState(ext))
                completePendingAddonAction(pendingAction)
                onSuccess(updatedAddon)
            },
            onError = {
                completePendingAddonAction(pendingAction)
                onError(it)
            },
        )
    }

    /**
     * Remove the provided [permissions], [origins] and/or [dataCollectionPermissions] from [Addon].
     *
     * @param addon the [Addon] to remove the provided [permissions] and [origins].
     * @param permissions the permissions to remove, pass an empty array for opt-out.
     * @param origins the origins to remove, pass an empty array for opt-out.
     * @param dataCollectionPermissions the data collection permissions to remove, pass an empty array for opt-out.
     * @param onSuccess (optional) callback invoked with the removed permissions and origins.
     * @param onError (optional) callback invoked if there was an error removed.
     */
    fun removeOptionalPermission(
        addon: Addon,
        permissions: List<String> = emptyList(),
        origins: List<String> = emptyList(),
        dataCollectionPermissions: List<String> = emptyList(),
        onSuccess: ((Addon) -> Unit) = {},
        onError: ((Throwable) -> Unit) = {},
    ) {
        if (permissions.isEmpty() && origins.isEmpty() && dataCollectionPermissions.isEmpty()) {
            onError(
                IllegalStateException(
                    "At least one of permissions, origins or dataCollectionPermissions must not be empty"
                )
            )
            return
        }

        val extension = addon.installedState?.let { installedExtensions[it.id] }
        if (extension == null) {
            onError(IllegalStateException("Addon is not installed"))
            return
        }

        val pendingAction = addPendingAddonAction()
        runtime.removeOptionalPermissions(
            extension.id,
            permissions = permissions,
            origins = origins,
            dataCollectionPermissions = dataCollectionPermissions,
            onSuccess = { ext ->
                val updatedAddon = Addon.newFromWebExtension(ext, toInstalledState(ext))
                completePendingAddonAction(pendingAction)
                onSuccess(updatedAddon)
            },
            onError = {
                completePendingAddonAction(pendingAction)
                onError(it)
            },
        )
    }

    /**
     * Disables the provided [Addon].
     *
     * @param addon the [Addon] to disable.
     * @param source [EnableSource] to indicate why the addon is disabled, default to EnableSource.USER.
     * @param onSuccess (optional) callback invoked with the enabled [Addon].
     * @param onError (optional) callback invoked if there was an error enabling
     */
    fun disableAddon(
        addon: Addon,
        source: EnableSource = EnableSource.USER,
        onSuccess: ((Addon) -> Unit) = {},
        onError: ((Throwable) -> Unit) = {},
    ) {
        val extension = addon.installedState?.let { installedExtensions[it.id] }
        if (extension == null) {
            onError(IllegalStateException("Addon is not installed"))
            return
        }

        val pendingAction = addPendingAddonAction()
        runtime.disableWebExtension(
            extension,
            source,
            onSuccess = { ext ->
                val disabledAddon = addon.copy(installedState = toInstalledState(ext))
                completePendingAddonAction(pendingAction)
                onSuccess(disabledAddon)
            },
            onError = {
                completePendingAddonAction(pendingAction)
                onError(it)
            },
        )
    }

    /**
     * Sets whether to allow/disallow the provided [Addon] in private browsing mode.
     *
     * @param addon the [Addon] to allow/disallow.
     * @param allowed true if allow, false otherwise.
     * @param onSuccess (optional) callback invoked with the enabled [Addon].
     * @param onError (optional) callback invoked if there was an error enabling
     */
    fun setAddonAllowedInPrivateBrowsing(
        addon: Addon,
        allowed: Boolean,
        onSuccess: ((Addon) -> Unit) = {},
        onError: ((Throwable) -> Unit) = {},
    ) {
        val extension = addon.installedState?.let { installedExtensions[it.id] }
        if (extension == null) {
            onError(IllegalStateException("Addon is not installed"))
            return
        }

        val pendingAction = addPendingAddonAction()
        runtime.setAllowedInPrivateBrowsing(
            extension,
            allowed,
            onSuccess = { ext ->
                val modifiedAddon = addon.copy(installedState = toInstalledState(ext))
                completePendingAddonAction(pendingAction)
                onSuccess(modifiedAddon)
            },
            onError = {
                completePendingAddonAction(pendingAction)
                onError(it)
            },
        )
    }

    /**
     * Updates the addon with the provided [id] if an update is available.
     *
     * @param id the ID of the addon
     * @param onFinish callback invoked with the [Status] of the update once complete. An update that requires new
     *   permissions only completes once the user decides (see [AddonUpdater.onUpdatePermissionRequest]), if ever.
     */
    fun updateAddon(id: String, onFinish: ((Status) -> Unit)) {
        val extension = installedExtensions[id]

        if (extension == null || extension.isUnsupported()) {
            onFinish(Status.NotInstalled)
            return
        }

        val onSuccess: ((WebExtension?) -> Unit) = { updatedExtension ->
            val status =
                if (updatedExtension == null) {
                    Status.NoUpdateAvailable
                } else {
                    WebExtensionSupport.markExtensionAsUpdated(store, updatedExtension)
                    Status.SuccessfullyUpdated
                }
            onFinish(status)
        }

        val onError: ((String, Throwable) -> Unit) = { message, exception ->
            onFinish(Status.Error(message, exception))
        }

        runtime.updateWebExtension(extension, onSuccess, onError)
    }

    /**
     * Returns once the extension has an update waiting for the user to grant new permissions, which may never happen.
     *
     * @see AddonUpdater.onUpdatePermissionRequest
     */
    internal suspend fun awaitPendingUpdateForExtension(extensionId: String) {
        store.stateFlow.first { extensionId in it.pendingExtensionUpdates }
    }

    private fun addPendingAddonAction() =
        CompletableDeferred<Unit>().also {
            pendingAddonActions.add(it)
        }

    private fun completePendingAddonAction(action: CompletableDeferred<Unit>) {
        action.complete(Unit)
        pendingAddonActions.remove(action)
    }

    /** Converts a [WebExtension] to [Addon.InstalledState]. */
    fun toInstalledState(extension: WebExtension): Addon.InstalledState {
        val metadata = extension.getMetadata()
        val cachedIcon = iconsCache[extension.id]
        return Addon.InstalledState(
            id = extension.id,
            version = metadata?.version ?: "",
            optionsPageUrl = metadata?.optionsPageUrl,
            openOptionsPageInTab = metadata?.openOptionsPageInTab ?: false,
            enabled = extension.isEnabled(),
            disabledReason = extension.getDisabledReason(),
            allowedInPrivateBrowsing = extension.isAllowedInPrivateBrowsing(),
            icon =
                cachedIcon
                    ?: loadIcon(extension)?.also {
                        iconsCache[extension.id] = it
                    },
        )
    }

    @VisibleForTesting
    @Suppress("TooGenericExceptionCaught")
    internal fun loadIcon(extension: WebExtension): Bitmap? {
        // As we are loading the icon from the xpi file this operation should be quick.
        // If the operation takes a long time,  we proceed to return early,
        // and load the icon in background.
        return runBlocking(getIconDispatcher()) {
            try {
                val icon =
                    withTimeoutOrNull(ADDON_ICON_RETRIEVE_TIMEOUT) {
                        extension.loadIcon(ADDON_ICON_SIZE)
                    }
                logger.info("Icon for extension ${extension.id} loaded successfully")
                icon
            } catch (e: TimeoutCancellationException) {
                // If the icon is too big, we delegate the task to be done in background.
                // Eventually, we load the icon and keep it in cache for sequential loads.
                tryLoadIconInBackground(extension)
                logger.error("Failed load icon for extension ${extension.id}", e)
                null
            } catch (e: Exception) {
                null
            }
        }
    }

    @VisibleForTesting
    internal fun tryLoadIconInBackground(extension: WebExtension) {
        logger.info("Trying to load icon for extension ${extension.id} in background")
        iconLoadingScope.launch {
            withContext(getIconDispatcher()) {
                    extension.loadIcon(ADDON_ICON_SIZE)
                }
                ?.also {
                    logger.info("Icon for extension ${extension.id} loaded in background successfully")
                    iconsCache[extension.id] = it
                }
        }
    }

    @VisibleForTesting
    internal fun getIconDispatcher(): CoroutineDispatcher {
        val iconThread =
            HandlerThread("IconThread").also {
                it.start()
            }
        return Handler(iconThread.looper).asCoroutineDispatcher("WebExtensionIconDispatcher")
    }

    private fun onAddonInstalled(
        ext: WebExtension,
        pendingAction: CompletableDeferred<Unit>,
        onSuccess: ((Addon) -> Unit),
    ) {
        val installedState = toInstalledState(ext)
        val installedAddon = Addon.newFromWebExtension(ext, installedState)

        addonUpdater.registerForFutureUpdates(installedAddon.id)
        completePendingAddonAction(pendingAction)
        onSuccess(installedAddon)
    }

    companion object {
        // Size of the icon to load for extensions
        const val ADDON_ICON_SIZE = 48

        private const val ADDON_ICON_RETRIEVE_TIMEOUT = 1000L
    }
}

/** Wraps exceptions thrown by either the initialization process or an [AddonsProvider]. */
class AddonManagerException(throwable: Throwable) : Exception(throwable)

/**
 * This method returns a single [Addon.DisabledReason] from a [WebExtension] instance, which can have more that 1
 * disabled flag. This is fine as long as we use this method in UI and we don't need to know that an [Addon] is disabled
 * for multiple reasons.
 *
 * A concrete example is when an [Addon] is soft-blocked and the user has enabled/disabled this [Addon] manually (which
 * is possible for soft-blocked add-ons). When that happens, the [Addon] is soft-blocked per the `BlocklistState` as
 * well as user-disabled. This method would only return `Addon.DisabledReason.SOFT_BLOCKED` in this case. That's fine
 * for now because we want to always show the error message to the users, but it might not always be desirable.
 */
internal fun WebExtension.getDisabledReason(): Addon.DisabledReason? {
    return if (isBlockListed()) {
        Addon.DisabledReason.BLOCKLISTED
    } else if (isSoftBlocked()) {
        Addon.DisabledReason.SOFT_BLOCKED
    } else if (isDisabledUnsigned()) {
        Addon.DisabledReason.NOT_CORRECTLY_SIGNED
    } else if (isDisabledIncompatible()) {
        Addon.DisabledReason.INCOMPATIBLE
    } else if (isUnsupported()) {
        Addon.DisabledReason.UNSUPPORTED
    } else if (getMetadata()?.disabledFlags?.contains(DisabledFlags.USER) == true) {
        Addon.DisabledReason.USER_REQUESTED
    } else {
        null
    }
}
