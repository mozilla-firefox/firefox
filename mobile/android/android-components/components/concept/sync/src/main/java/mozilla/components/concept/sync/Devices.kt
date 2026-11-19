/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.sync

import android.content.Context
import androidx.annotation.MainThread
import androidx.lifecycle.LifecycleOwner
import mozilla.components.support.base.observer.Observable

/** Represents a result of interacting with a backend service which may return an authentication error. */
sealed class ServiceResult {
    /** All good. */
    object Ok : ServiceResult()

    /** Auth error. */
    object AuthError : ServiceResult()

    /** Error that isn't auth. */
    object OtherError : ServiceResult()
}

/** Describes available interactions with the current device and other devices associated with an [OAuthAccount]. */
interface DeviceConstellation : Observable<AccountEventsObserver> {
    /**
     * Ensures the [capabilities] advertised for the device & account are up-to-date.
     *
     * It is expected to call this after authentication when the account's scopes or keys may have changed.
     *
     * @param capabilities List of capabilities to update. The caller is responsible that the list here are correct
     *   based on the state of the account. When the account does not have a sync scope/key, then this list should be
     *   empty.
     * @return [ServiceResult] that tells if the operation was successful.
     */
    suspend fun ensureCapabilities(capabilities: Set<DeviceCapability>): ServiceResult

    /**
     * Current state of the constellation. May be missing if state was never queried.
     *
     * @return [ConstellationState] describes current and other known devices in the constellation.
     */
    fun state(): ConstellationState?

    /**
     * Allows monitoring state of the device constellation via [DeviceConstellationObserver]. Use this to be notified of
     * changes to the current device or other devices.
     */
    @MainThread
    fun registerDeviceObserver(observer: DeviceConstellationObserver, owner: LifecycleOwner, autoPause: Boolean)

    /**
     * Set name of the current device.
     *
     * @param name New device name.
     * @param context An application context, used for updating internal caches.
     * @return A boolean success flag.
     */
    suspend fun setDeviceName(name: String, context: Context): Boolean

    /**
     * Set a [DevicePushSubscription] for the current device.
     *
     * @param subscription A new [DevicePushSubscription].
     * @return A boolean success flag.
     */
    suspend fun setDevicePushSubscription(subscription: DevicePushSubscription): Boolean

    /**
     * Send a command to a specified device.
     *
     * @param targetDeviceId A device ID of the recipient.
     * @param outgoingCommand An event to send.
     * @return A boolean success flag.
     */
    suspend fun sendCommandToDevice(targetDeviceId: String, outgoingCommand: DeviceCommandOutgoing): Boolean

    /**
     * Process a raw event, obtained via a push message or some other out-of-band mechanism.
     *
     * @param payload A raw, plaintext payload to be processed.
     * @return A boolean success flag.
     */
    suspend fun processRawEvent(payload: String): Boolean

    /**
     * Refreshes [ConstellationState]. Registered [DeviceConstellationObserver] observers will be notified.
     *
     * @param [ignoreCache] Whether to ignore the cache and refresh from the server.
     * @return A boolean success flag.
     */
    suspend fun refreshDevices(ignoreCache: Boolean = false): Boolean

    /**
     * Polls for any pending [DeviceCommandIncoming] commands. In case of new commands, registered
     * [AccountEventsObserver] observers will be notified.
     *
     * @return A boolean success flag.
     */
    suspend fun pollForCommands(): Boolean
}

/** Describes current device and other devices in the constellation. */
// N.B.: currentDevice should not be nullable.
// See https://github.com/mozilla-mobile/android-components/issues/8768
data class ConstellationState(val currentDevice: Device?, val otherDevices: List<Device>)

/** Allows monitoring constellation state. */
interface DeviceConstellationObserver {
    fun onDevicesUpdate(constellation: ConstellationState)
}

/** Describes a type of the physical device in the constellation. */
enum class DeviceType {
    DESKTOP,
    MOBILE,
    TABLET,
    TV,
    VR,
    UNKNOWN,
}

/** Describes an Autopush-compatible push channel subscription. */
data class DevicePushSubscription(
    val endpoint: String,
    val publicKey: String,
    val authKey: String,
)

/**
 * Configuration for the current device.
 *
 * @property name An initial name to use for the device record which will be created during authentication. This can be
 *   changed later via [DeviceConstellation.setDeviceName].
 * @property type Type of a device - mobile, desktop - used for displaying identifying icons on other devices. This
 *   cannot be changed once device record is created.
 * @property capabilities The configured device capabilities, exposed through [getCapabilities].
 * @property secureStateAtRest A flag indicating whether or not to use encrypted storage for the persisted account
 *   state.
 */
data class DeviceConfig(
    val name: String,
    val type: DeviceType,
    private val capabilities: Set<DeviceCapability>,
    val secureStateAtRest: Boolean = false,
) {
    /**
     * Returns the configured capabilities when [hasSyncScope] is true, or an empty set otherwise.
     *
     * All current device capabilities require the Sync scope.
     */
    fun getCapabilities(hasSyncScope: Boolean): Set<DeviceCapability> = if (hasSyncScope) capabilities else emptySet()
}

/** Capabilities that a [Device] may have. */
enum class DeviceCapability {
    SEND_TAB,
    CLOSE_TABS,
}

/** Describes a device in the [DeviceConstellation]. */
data class Device(
    val id: String,
    val displayName: String,
    val deviceType: DeviceType,
    val isCurrentDevice: Boolean,
    val lastAccessTime: Long?,
    val capabilities: List<DeviceCapability>,
    val subscriptionExpired: Boolean,
    val subscription: DevicePushSubscription?,
)
