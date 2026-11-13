/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.support.webextensions

import androidx.annotation.VisibleForTesting
import java.util.concurrent.ConcurrentHashMap
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.webextension.MessageHandler
import mozilla.components.concept.engine.webextension.WebExtension
import mozilla.components.concept.engine.webextension.WebExtensionRuntime
import mozilla.components.support.base.log.logger.Logger
import org.json.JSONObject

/**
 * Provides functionality to feature modules that need to interact with a web extension that these modules provide.
 *
 * @property extensionId the unique ID of the web extension e.g. mozacReaderview.
 * @property extensionUrl the url pointing to a resources path for locating the extension within the APK file e.g.
 *   resource://android/assets/extensions/my_web_ext.
 * @property defaultPort the name of the default port used to exchange messages between extension scripts and the
 *   application. Extensions can open multiple ports. so [sendContentMessage] and [sendBackgroundMessage] allow
 *   specifying an alternative port, if needed.
 */
class BuiltInWebExtensionController(
    private val extensionId: String,
    private val extensionUrl: String,
    private val defaultPort: String,
) {
    private val logger = Logger("mozac-webextensions")
    private var registerContentMessageHandler: (WebExtension) -> Unit = {}
    private var registerBackgroundMessageHandler: (WebExtension) -> Unit = {}

    /**
     * Makes sure the web extension is installed in the provided runtime. If a content message handler was registered
     * (see [registerContentMessageHandler]) before install completed, registration will happen upon successful
     * installation.
     *
     * @param runtime the [WebExtensionRuntime] the web extension should be installed in.
     * @param onSuccess (optional) callback invoked if the extension was installed successfully or is already installed.
     * @param onError (optional) callback invoked if there was an error installing the extension.
     */
    fun install(
        runtime: WebExtensionRuntime,
        onSuccess: ((WebExtension) -> Unit) = {},
        onError: ((Throwable) -> Unit) = { _ -> },
    ) {
        val installedExtension = installedBuiltInExtensions[extensionId]
        if (installedExtension == null) {
            runtime.installBuiltInWebExtension(
                extensionId,
                extensionUrl,
                onSuccess = {
                    logger.debug("Installed extension: ${it.id}")
                    synchronized(this@BuiltInWebExtensionController) {
                        registerContentMessageHandler(it)
                        registerBackgroundMessageHandler(it)
                        installedBuiltInExtensions[extensionId] = it
                        onSuccess(it)
                    }
                },
                onError = { throwable ->
                    logger.error("Failed to install extension: $extensionId", throwable)
                    onError(throwable)
                },
            )
        } else {
            onSuccess(installedExtension)
        }
    }

    /**
     * Uninstalls the web extension, unless the extension wasn't installed.
     *
     * @param runtime the [WebExtensionRuntime] needed to uninstall the extension.
     * @param onSuccess (optional) callback invoked when the extension was uninstalled successfully.
     * @param onError (optional) callback invoked when there was an error uninstalling the extension.
     */
    fun uninstall(
        runtime: WebExtensionRuntime,
        onSuccess: (() -> Unit) = {},
        onError: ((Throwable) -> Unit) = { _ -> },
    ) {
        // We need to retrieve the list of installed extensions from `WebExtensionSupport` because
        // `WebExtensionController.installedBuiltInExtensions` is only used for extensions installed
        // via the `install()` method above.
        WebExtensionSupport.installedExtensions[extensionId]?.let { extension ->
            runtime.uninstallWebExtension(
                extension,
                onSuccess = {
                    logger.debug("Successfully uninstalled extension: ${extension.id}")
                    onSuccess()
                },
                onError = { _, throwable ->
                    logger.error("Failed to uninstall extension: ${extension.id}", throwable)
                    onError(throwable)
                },
            )
        }
    }

    /**
     * Registers a content message handler for the provided session. Currently only one handler can be registered per
     * session. An existing handler will be replaced and there is no need to unregister.
     *
     * @param engineSession the session the content message handler should be registered with.
     * @param messageHandler the message handler to register.
     * @param name (optional) name of the port, if not specified [defaultPort] will be used.
     */
    fun registerContentMessageHandler(
        engineSession: EngineSession,
        messageHandler: MessageHandler,
        name: String = defaultPort,
    ) {
        synchronized(this) {
            registerContentMessageHandler = {
                it.registerContentMessageHandler(engineSession, name, messageHandler)
            }

            installedBuiltInExtensions[extensionId]?.let { registerContentMessageHandler(it) }
        }
    }

    /**
     * Registers a background message handler for this extension. An existing handler will be replaced and there is no
     * need to unregister.
     *
     * @param messageHandler the message handler to register.
     * @param name (optional) name of the port, if not specified [defaultPort] will be used.
     */
    fun registerBackgroundMessageHandler(
        messageHandler: MessageHandler,
        name: String = defaultPort,
    ) {
        synchronized(this) {
            registerBackgroundMessageHandler = {
                it.registerBackgroundMessageHandler(name, messageHandler)
            }

            installedBuiltInExtensions[extensionId]?.let { registerBackgroundMessageHandler(it) }
        }
    }

    /**
     * Sends a content message to the provided session.
     *
     * @param msg the message to send
     * @param engineSession the session to send the content message to.
     * @param name (optional) name of the port, if not specified [defaultPort] will be used.
     */
    fun sendContentMessage(msg: JSONObject, engineSession: EngineSession?, name: String = defaultPort) {
        engineSession?.let { session ->
            installedBuiltInExtensions[extensionId]?.let { ext ->
                val port = ext.getConnectedPort(name, session)
                port?.postMessage(msg)
                    ?: logger.error("No port with name $name connected for provided session. Message $msg not sent.")
            }
        }
    }

    /**
     * Sends a background message to the provided extension.
     *
     * @param msg the message to send
     * @param name (optional) name of the port, if not specified [defaultPort] will be used.
     */
    fun sendBackgroundMessage(
        msg: JSONObject,
        name: String = defaultPort,
    ) {
        installedBuiltInExtensions[extensionId]?.let { ext ->
            val port = ext.getConnectedPort(name)
            port?.postMessage(msg) ?: logger.error("No port connected for provided extension. Message $msg not sent.")
        }
    }

    /**
     * Checks whether or not a port is connected for the provided session.
     *
     * @param engineSession the session the port should be connected to or null for a port to a background script.
     * @param name (optional) name of the port, if not specified [defaultPort] will be used.
     */
    fun portConnected(engineSession: EngineSession?, name: String = defaultPort): Boolean {
        return installedBuiltInExtensions[extensionId]?.let { ext ->
            ext.getConnectedPort(name, engineSession) != null
        } ?: false
    }

    /**
     * Disconnects the port of the provided session.
     *
     * @param engineSession the session the port is connected to or null for a port to a background script.
     * @param name (optional) name of the port, if not specified [defaultPort] will be used.
     */
    fun disconnectPort(engineSession: EngineSession?, name: String = defaultPort) {
        installedBuiltInExtensions[extensionId]?.disconnectPort(name, engineSession)
    }

    /** Companion object for [BuiltInWebExtensionController]. */
    companion object {
        @VisibleForTesting val installedBuiltInExtensions = ConcurrentHashMap<String, WebExtension>()
    }
}
