/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.experiments.prefhandling

import androidx.annotation.VisibleForTesting
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineName
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.MainScope
import kotlinx.coroutines.launch
import kotlinx.coroutines.plus
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.browser.engine.gecko.preferences.BrowserPrefObserverIntegration
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.engine.preferences.BrowserPrefType
import mozilla.components.concept.engine.preferences.BrowserPreference
import mozilla.components.concept.engine.preferences.SetBrowserPreference
import mozilla.components.service.nimbus.NimbusApi
import mozilla.components.support.base.log.logger.Logger
import org.mozilla.experiments.nimbus.internal.GeckoPrefHandler
import org.mozilla.experiments.nimbus.internal.GeckoPrefState
import org.mozilla.experiments.nimbus.internal.OriginalGeckoPref
import org.mozilla.experiments.nimbus.internal.PrefBranch
import org.mozilla.experiments.nimbus.internal.PrefUnenrollReason
import org.mozilla.fenix.nimbus.FxNimbus

private val logger = Logger("Nimbus/GeckoPrefHandler")

/**
 * The handler Nimbus uses for reading and writing Gecko preferences.
 *
 * @param engine The browser engine used to read and write preferences.
 * @param nimbusApi The Nimbus API that will orchestrate enrollment and unenrollment.
 * @param geckoScope The scope that will be used for completing Gecko tasks.
 */
class NimbusGeckoPrefHandler(
    private val engine: Lazy<Engine>,
    private val nimbusApi: Lazy<NimbusApi>,
    private val geckoScope: CoroutineScope = MainScope() + CoroutineName("NimbusGeckoPrefHandler"),
) : GeckoPrefHandler, BrowserPrefObserverIntegration.Observer {

    val browserPrefObserverIntegration by lazy { BrowserPrefObserverIntegration(engine.value) }

    // Used for handling errors when we fail to set during enrollment
    val enrollmentErrors = mutableListOf<Pair<GeckoPrefState, Throwable?>>()

    // Used for ensuring we have the correct validated preference type
    val preferenceTypes = mutableMapOf<String, BrowserPrefType>()

    // A map of experiment feature ids that maps to a map of variable ids and their respective GeckoPrefStates.
    val nimbusGeckoPreferences: Map<String, Map<String, GeckoPrefState>> =
        FxNimbus.geckoPrefsMap().mapValues { featureEntry ->
            featureEntry.value.mapValues { variableEntry ->
                GeckoPrefState(
                    geckoPref = variableEntry.value,
                    geckoValue = null,
                    enrollmentValue = null,
                    isUserSet = false,
                )
            }
        }

    // The list of preferences provided by Nimbus, that we are interested in for
    // experiment purposes.
    @VisibleForTesting
    internal var preferenceList = nimbusGeckoPreferences.flatMap { featureEntry ->
        featureEntry.value.map { variablesEntry ->
            variablesEntry.value.prefString()
        }
    }

    // Used to help prevent double queries for the same information.
    // List<String> - The list of preferences being fetched.
    // Deferred<Boolean> - The deferred query state.
    @VisibleForTesting internal var fetchingGeckoPrefState: Pair<List<String>, Deferred<Boolean>>? = null

    /** Called when ready to begin observation. */
    fun start() {
        browserPrefObserverIntegration.start()
        browserPrefObserverIntegration.register(this)
    }

    /**
     * Obtains the preference state for a specific preference string
     *
     * @param pref: The string name of the preference for which to obtain the value
     * @return The GeckoPrefState instance for the requested preference, if it exists
     */
    fun getPreferenceState(pref: String): GeckoPrefState? =
        nimbusGeckoPreferences.values.flatMap { it.values }.firstOrNull { it.prefString() == pref }

    /**
     * Retrieves initial values of the specified preferences for Nimbus. This is part of the Nimbus Gecko pref
     * enrollment flow.
     *
     * @return The state of the Gecko preferences for which Nimbus could set values
     */
    @OptIn(ExperimentalAndroidComponentsApi::class)
    fun getPreferenceStateFromGecko(): Deferred<Boolean> {
        val completable = CompletableDeferred<Boolean>()
        geckoScope.launch {
            // Check for an existing fetch to optimize rapidly querying for the same information
            val existingFetch = fetchingGeckoPrefState
            if (existingFetch != null && existingFetch.first == preferenceList) {
                completable.complete(existingFetch.second.await())
                return@launch
            }
            fetchingGeckoPrefState = Pair(preferenceList, completable)

            try {
                engine.value.getBrowserPrefs(
                    prefs = preferenceList,
                    onSuccess = { preferences ->
                        for (preference in preferences) {
                            preferenceTypes[preference.pref] = preference.prefType
                            val state = getPreferenceState(preference.pref) ?: continue
                            state.geckoValue =
                                if (state.branch() == PrefBranch.DEFAULT) {
                                        preference.defaultValue
                                    } else {
                                        preference.userValue
                                    }
                                    .toString()
                            state.isUserSet = preference.hasUserChangedValue
                        }
                        fetchingGeckoPrefState = null
                        completable.complete(true)
                    },
                    onError = { throwable ->
                        logger.error("Error getting preference state from Gecko: ", throwable)
                        fetchingGeckoPrefState = null
                        completable.complete(false)
                    },
                )
            } catch (e: IllegalThreadStateException) {
                logger.error("Error getting preference state from Gecko: ", e)
                fetchingGeckoPrefState = null
                completable.complete(false)
            }
        }
        return completable
    }

    /**
     * Fetches specified preferences and adds them to the [preferenceTypes] variable for reference.
     *
     * @param prefs The preferences that we need to know type information for.
     */
    @OptIn(ExperimentalAndroidComponentsApi::class)
    private fun fetchPrefTypeInfo(prefs: List<String>): Deferred<Boolean> {
        val completable = CompletableDeferred<Boolean>()
        geckoScope.launch {
            try {
                engine.value.getBrowserPrefs(
                    prefs = prefs,
                    onSuccess = { prefInfo ->
                        prefInfo.associateTo(preferenceTypes) { it.pref to it.prefType }
                        completable.complete(true)
                    },
                    onError = {
                        logger.error("Error getting preference type info from Gecko")
                        completable.complete(false)
                    },
                )
            } catch (e: IllegalThreadStateException) {
                logger.error("Error getting preference state from Gecko", e)
                completable.complete(false)
            }
        }
        return completable
    }

    /**
     * Handles the errors stored in [enrollmentErrors], and unenrolls from Nimbus experiments for the preferences that
     * failed to set.
     *
     * This is part of the Nimbus Gecko pref enrollment flow.
     */
    fun handleErrors() {
        val processedExperiments = mutableSetOf<Set<String>>()

        for ((prefState, _) in enrollmentErrors) {
            val experimentPrefs = allExperimentPrefs(prefState).toSet()

            // We only need to unenroll a given experiment once, it doesn't matter which one is the triggering event.
            // For example, in a multi-pref experiment, if both failed to set, we only need to report one of them.
            if (!processedExperiments.contains(experimentPrefs)) {
                processedExperiments.add(experimentPrefs)
                unenrollFromPrefExperiment(prefState, PrefUnenrollReason.FAILED_TO_SET)
            }
        }
        enrollmentErrors.clear()
    }

    /**
     * Get the Nimbus Gecko preferences state.
     *
     * @return The map of GeckoPrefState instances
     */
    override fun getPrefsWithState(): Map<String, Map<String, GeckoPrefState>> {
        return nimbusGeckoPreferences
    }

    /**
     * Sets Gecko preferences to their original values when experiment unenrollment occurs.
     *
     * This is part of the Nimbus Gecko pref unenrollment flow. The goal is to revert the pref to a known value before
     * the experiment occurred.
     *
     * @param originalGeckoPrefs: The list of original Gecko preference values
     */
    @OptIn(ExperimentalAndroidComponentsApi::class)
    override fun setGeckoPrefsOriginalValues(originalGeckoPrefs: List<OriginalGeckoPref>) {
        geckoScope.launch {
            // Clear the geckoValue in the shared records.
            originalGeckoPrefs.forEach { pref ->
                getPreferenceState(pref.pref)?.let { it.geckoValue = null }
            }

            // We need type information to correctly revert
            if (preferenceTypes.isEmpty() || originalGeckoPrefs.any { it.pref !in preferenceTypes }) {
                fetchPrefTypeInfo(originalGeckoPrefs.map { it.pref }).await()
            }

            val (prefsWithValues, prefsToReset) = originalGeckoPrefs.partition { it.value != null }

            // Stop observing the preference we are about to restore.
            browserPrefObserverIntegration.unregisterPrefsForObservation(
                prefs = originalGeckoPrefs.map { it.pref },
                onSuccess = {},
                onError = { logger.error("Error unregistering preferences from observation", it) },
            )

            // Set elements that we have values we can restore back to
            val setters = createSettersFromOriginalGeckoPrefs(prefsWithValues, preferenceTypes)
            engine.value.setBrowserPrefs(
                prefs = setters,
                onSuccess = { resultMap ->
                    logRestoreSuccess(resultMap)
                },
                onError = { logger.error("Error setting Gecko preferences to their original values", it) },
            )

            // Clear elements that we have no values we can restore back to
            prefsToReset.forEach { (prefString, _) ->
                engine.value.clearBrowserUserPref(
                    pref = prefString,
                    onSuccess = { logger.info("Unset preference $prefString") },
                    onError = { logger.warn("Error unsetting Gecko preference $prefString") },
                )
            }
        }
    }

    /**
     * Convenience method for logging whether preferences restored or not to their original value.
     *
     * @param resultMap A map of a pref name and whether it set or not.
     */
    private fun logRestoreSuccess(resultMap: Map<String, Boolean>) {
        resultMap.forEach { (prefString, wasSet) ->
            if (wasSet) {
                logger.info("Set preference $prefString to its original value")
            } else {
                logger.warn("Unable to set $prefString to its original value")
            }
        }
    }

    /**
     * Sets the Gecko preference state when new state should be applied during the Enrollment flow.
     *
     * This is part of the Nimbus Gecko pref enrollment flow.
     *
     * @param newPrefsState: The list of new Gecko preference states.
     */
    override fun setGeckoPrefsState(newPrefsState: List<GeckoPrefState>) {
        if (newPrefsState.isEmpty()) {
            return
        }

        geckoScope.launch {
            // All preference values from Nimbus arrive as strings.
            // Type information from Gecko is needed to know how to
            // parse and value information for storing the original preference value.
            if (
                preferenceTypes.isEmpty() ||
                    newPrefsState.any { it.prefString() !in preferenceTypes } ||
                    newPrefsState.any { getPreferenceState(it.prefString())?.geckoValue == null }
            ) {
                getPreferenceStateFromGecko().await()
            }

            val setters: List<SetBrowserPreference<*>> =
                createSettersFromGeckoPrefStates(newPrefsState, preferenceTypes)

            // Report when we fail to make setters
            val setterNames = setters.map { it.pref }.toSet()
            newPrefsState.forEach { prefState ->
                if (prefState.prefString() !in setterNames) {
                    enrollmentErrors.add(Pair(prefState, IllegalStateException("Failed to make a setter!")))
                }
            }

            applyEnrollmentPrefs(setters, newPrefsState)
        }
    }

    /**
     * This function sets the browser prefs, registers errors, registers observation, and does the Nimbus callback of
     * `registerPreviousGeckoPrefStates` to set the original values on the database.
     *
     * @param setters The preferences that should be set on the browser.
     * @param newPrefsState: The list of new Gecko preference states.
     */
    @OptIn(ExperimentalAndroidComponentsApi::class)
    private fun applyEnrollmentPrefs(
        setters: List<SetBrowserPreference<*>>,
        newPrefsState: List<GeckoPrefState>,
    ) {
        engine.value.setBrowserPrefs(
            prefs = setters,
            onSuccess = { resultMap ->

                // Determine new Nimbus enrollments from re-evaluations.
                val newEnrollment =
                    newPrefsState
                        .filter {
                            getPreferenceState(it.prefString())?.enrollmentValue?.experimentSlug !=
                                it.enrollmentValue?.experimentSlug
                        }
                        .map { it.prefString() }
                        .toSet()

                // Ensures state and processes errors
                val succeededPrefs = processSetResults(resultMap, newPrefsState)

                if (succeededPrefs.isNotEmpty()) {
                    // Observe the newly set prefs for changes
                    browserPrefObserverIntegration.registerPrefsForObservation(
                        prefs = succeededPrefs,
                        onSuccess = {
                            logger.info("Successfully registered prefs for observation")
                        },
                        onError = { throwable ->
                            logger.error("Failed to register prefs for observation: ", throwable)
                        },
                    )

                    // Exclude re-evaluations — their originals are already stored in Nimbus.
                    // This prevents errantly writing prior experiment/rollout values
                    // that modified the preference in-between.
                    val statesToRegister =
                        succeededPrefs.filter { it in newEnrollment }.mapNotNull { getPreferenceState(it) }

                    // Reports back the value for Nimbus to store
                    if (statesToRegister.isNotEmpty()) {
                        nimbusApi.value.registerPreviousGeckoPrefStates(geckoPrefStates = statesToRegister)
                    }
                }

                handleErrors()
            },
            onError = {
                logger.error(
                    "Unknown error while awaiting setting Gecko preferences",
                    it,
                )
            },
        )
    }

    /**
     * Method checks the Nimbus state of the known prefs, adds errors, and creates a list of successful sets.
     *
     * @param preferencesSettingSuccess A map of prefs, by pref identifier as key and if it set as expected by value.
     * @param newPrefsState The list of new Gecko preference states.
     * @return A validated list of what [preferencesSettingSuccess] set.
     */
    private fun processSetResults(
        preferencesSettingSuccess: Map<String, Boolean>,
        newPrefsState: List<GeckoPrefState>,
    ): List<String> {
        val succeeded = mutableListOf<String>()
        preferencesSettingSuccess.forEach { (prefString, wasSet) ->
            if (wasSet) {
                val state = getPreferenceState(prefString) ?: return@forEach
                state.enrollmentValue = newPrefsState.findByPrefString(prefString)?.enrollmentValue
                succeeded.add(prefString)
            } else {
                val state = getPreferenceState(prefString) ?: return@forEach
                val throwable = Throwable("Preference $prefString value was " + "not set")
                logger.error("Error while setting preference value", throwable)
                enrollmentErrors.add(Pair(state, throwable))
            }
        }
        return succeeded
    }

    /**
     * Handles when registered (active experiment) preferences are changed.
     *
     * @param observedPreference: The preference that was changed
     */
    override fun onPreferenceChange(observedPreference: BrowserPreference<*>) {
        if (preferenceList.contains(observedPreference.pref)) {
            val geckoPrefState =
                getPreferenceState(observedPreference.pref)
                    ?: run {
                        logger.warn(
                            "Preference ${observedPreference.pref} does not have a " + "GeckoPrefState instance"
                        )
                        return
                    }
            unenrollFromPrefExperiment(geckoPrefState, PrefUnenrollReason.CHANGED)
        } else {
            logger.info("Preference ${observedPreference.pref} was changed, but is not " + "in Nimbus' preference list")
        }
    }

    /**
     * Unenrolls the Gecko pref experiment from Nimbus and cleans up state.
     *
     * State that needs to be cleaned up includes removing the preference from observation.
     *
     * @param geckoPrefState The Gecko pref to unenroll.
     * @param reason The reason unenrollment was triggered.
     */
    @VisibleForTesting
    internal fun unenrollFromPrefExperiment(geckoPrefState: GeckoPrefState, reason: PrefUnenrollReason) {
        logger.info("Unenrollment was set for ${geckoPrefState.prefString()} due to $reason")

        // For multi-pref experiments, unregister all prefs in the same feature to prevent
        // double observation triggers when Nimbus restores the other pref to its original value.
        // If an item has already been unregistered, then it'll be a no-op.
        val associatedPrefs = allExperimentPrefs(geckoPrefState)

        browserPrefObserverIntegration.unregisterPrefsForObservation(
            prefs = associatedPrefs,
            onSuccess = { logger.info("Unregistered $associatedPrefs from observation.") },
            onError = { logger.warn("Could not unregister $associatedPrefs from observation.") },
        )

        // Nimbus will handle the case of unregistering the full feature experiment.
        nimbusApi.value.unenrollForGeckoPref(geckoPrefState, reason)
    }

    /**
     * Convenience method for getting associated experiment prefs a given [GeckoPrefState] is associated with.
     *
     * @param geckoPrefState The pref to use to identify other associated prefs with.
     * @return A list of Gecko prefs that are associated to the same experiment.
     */
    @VisibleForTesting
    internal fun allExperimentPrefs(geckoPrefState: GeckoPrefState): List<String> {
        val targetPref = geckoPrefState.prefString()
        val matchingFeature =
            nimbusGeckoPreferences.values.find { feature ->
                feature.values.any { state -> state.prefString() == targetPref }
            }
        val experimentPrefs = matchingFeature?.values?.map { state -> state.prefString() }
        return experimentPrefs ?: listOf(targetPref)
    }
}
