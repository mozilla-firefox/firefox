/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.experiments.nimbus.internal

import android.content.SharedPreferences
import org.json.JSONObject
import org.mozilla.experiments.nimbus.FeaturesInterface
import org.mozilla.experiments.nimbus.HardcodedNimbusFeatures
import org.mozilla.experiments.nimbus.NullVariables
import org.mozilla.experiments.nimbus.Variables
import java.util.concurrent.locks.ReentrantLock
import kotlin.concurrent.withLock

/**
 * `FeatureHolder` is a class that unpacks a JSON object from the Nimbus SDK and transforms it into a useful
 * type safe object, generated from a feature manifest (a `.fml.yaml` file).
 *
 * The two routinely useful methods are the `value()` and `recordExposure()` events.
 *
 * There are methods useful for testing, and more advanced uses: these all start with `with`.
 */
class FeatureHolder<T : FMLFeatureInterface>(
    private var getSdk: () -> FeaturesInterface?,
    private val featureId: String,
    private var create: (Variables, SharedPreferences?) -> T,
) {
    private val lock = ReentrantLock()

    private var cachedValue: T? = null

    /**
     * Get the JSON configuration from the Nimbus SDK and transform it into a configuration object as specified
     * in the feature manifest. This is done each call of the method, so the method should be called once, and the
     * result used for the configuration of the feature.
     *
     * The unused parameter `Context` was relevant for `Text` and `Image` typed properties, but is no longer necessary.
     * It will be removed in later releases.
     *
     * @returns T
     * @throws NimbusFeatureException thrown before the Nimbus object has been constructed or `FxNimbus.initialize`
     * has not been set.
     * This can be resolved by setting `FxNimbus.initialize`, and after that by passing in a `Context` object.
     */
    fun value(): T =
        lock.withLock {
            if (cachedValue != null) {
                cachedValue!!
            } else {
                val variables = getSdk()?.getVariables(featureId, false) ?: run {
                    NullVariables.instance
                }
                val prefs = getSdk()?.prefs
                create(variables, prefs).also { value ->
                    cachedValue = value
                }
            }
        }

    /**
     * Send an exposure event for this feature. This should be done when the user is shown the feature, and may change
     * their behavior because of it.
     */
    fun recordExposure() {
        if (!value().isModified()) {
            getSdk()?.recordExposureEvent(featureId)
        }
    }

    /**
     * Send an exposure event for this feature, in the given experiment.
     *
     * If the experiment does not exist, or the client is not enrolled in that experiment, then no exposure event
     * is recorded.
     *
     * If you are not sure of the experiment slug, then this is _not_ the API you need: you should use
     * [recordExposure] instead.
     */
    fun recordExperimentExposure(slug: String) {
        if (!value().isModified()) {
            getSdk()?.recordExposureEvent(featureId, slug)
        }
    }

    /**
     * A convenience method for calling [value()].[toJSONObject()].
     *
     * This is likely only useful for integrations that are going to serialize the configuration.
     * Regular app developers should use the type safety provided by [value()].
     */
    fun toJSONObject() = value().toJSONObject()

    /**
     * Send a malformed feature event for this feature.
     *
     * @param partId an optional detail or part identifier to be attached to the event.
     */
    fun recordMalformedConfiguration(partId: String = "") {
        getSdk()?.recordMalformedConfiguration(featureId, partId)
    }

    /**
     * This overwrites the cached value with the passed one.
     *
     * This is most likely useful during testing only.
     */
    fun withCachedValue(value: T?) {
        lock.withLock {
            cachedValue = value
        }
    }

    /**
     * This changes the mapping between a `Variables` and the feature configuration object.
     *
     * This is most likely useful during testing and other generated code.
     */
    fun withInitializer(create: (Variables, SharedPreferences?) -> T) {
        lock.withLock {
            this.create = create
            this.cachedValue = null
        }
    }

    /**
     * This resets the SDK and clears the cached value.
     *
     * This is especially useful at start up and for imported features.
     */
    fun withSdk(getSdk: () -> FeaturesInterface?) {
        lock.withLock {
            this.getSdk = getSdk
            this.cachedValue = null
        }
    }

    /**
     * Add a callback to be called when the feature updates.
     *
     * This **must** be called after Nimbus is initialized, otherwise the
     * callback will not be registered.
     */
    fun addUpdateCallback(callback: () -> Unit) {
        getSdk()?.getFeatureUpdateDispatcher()?.addCallback(featureId, callback)
    }

    /**
     * Remove a feature update callback.
     */
    fun removeUpdateCallback(callback: () -> Unit) {
        getSdk()?.getFeatureUpdateDispatcher()?.removeCallback(featureId, callback)
    }

    /**
     * Is this feature the focus of an automated test.
     *
     * A utility flag to be used in conjunction with {HardcodedNimbusFeatures}.
     *
     * It is intended for use for app-code to detect when the app is under test, and
     * take steps to make itself easier to test.
     *
     * These cases should be rare, and developers should look for other ways to test
     * code without relying on this facility.
     *
     * For example, a background worker might be scheduled to run every 24 hours, but
     * under test it would be desirable to run immediately, and only once.
     */
    fun isUnderTest(): Boolean = lock.withLock {
        val sdk = getSdk() as? HardcodedNimbusFeatures ?: return@withLock false
        sdk.hasFeature(featureId)
    }
}

class NimbusFeatureException(message: String) : Exception(message)

/**
 * A bare-bones interface for the FML generated objects.
 */
interface FMLObjectInterface {
    fun toJSONObject(): JSONObject
}

/**
 * A bare-bones interface for the FML generated features.
 *
 * App developers should use the generated concrete classes, which
 * implement this interface.
 */
interface FMLFeatureInterface : FMLObjectInterface {
    /**
     * A test if the feature configuration has been modified somehow, invalidating any experiment
     * that uses it.
     *
     * This may be `true` if a `pref-key` has been set in the feature manifest and the user has
     * set that preference.
     */
    fun isModified(): Boolean = false
}
