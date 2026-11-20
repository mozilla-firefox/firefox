/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.perf

import android.app.Application
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import androidx.annotation.StringRes
import androidx.annotation.VisibleForTesting
import androidx.core.content.ContextCompat
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import java.io.IOException
import kotlin.time.Duration.Companion.milliseconds
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import mozilla.components.concept.base.profiler.Profiler
import org.json.JSONException
import org.mozilla.fenix.R
import org.mozilla.fenix.ext.components
import org.mozilla.gecko.GeckoJavaSampler

/** Represents the various states of the profiler UI. */
sealed class ProfilerUiState {
    /** The profiler is idle and ready to start */
    data object Idle : ProfilerUiState()

    /** The profiler is in the process of starting up */
    data object Starting : ProfilerUiState()

    /** The profiler is active and "profiling" */
    data object Running : ProfilerUiState()

    /** The profiler is gathering and processing collected data */
    data object Gathering : ProfilerUiState()

    /** The profiler is in the process of shutting down */
    data object Stopping : ProfilerUiState()

    /** A toast message should be displayed to the user. */
    data class ShowToast(
        @param:StringRes val messageResId: Int,
        val extra: String = "",
    ) : ProfilerUiState()

    /** The profiling session has finished. */
    data class Finished(val profileUrl: String?) : ProfilerUiState()

    /** An error occurred during profiling. */
    data class Error(
        @param:StringRes val messageResId: Int,
        val errorDetails: String = "",
    ) : ProfilerUiState()

    /** Determines whether the dialog should be dismissed. */
    fun shouldDismiss(): Boolean {
        return this is Running || this is Finished || this is Error
    }
}

/** Factory for creating ProfilerViewModel instances with injectable dependencies. */
class ProfilerViewModelFactory(
    private val application: Application,
    private val mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
) : ViewModelProvider.Factory {
    override fun <T : ViewModel> create(modelClass: Class<T>): T {
        if (modelClass.isAssignableFrom(ProfilerViewModel::class.java)) {
            @Suppress("UNCHECKED_CAST")
            return ProfilerViewModel(application, mainDispatcher, ioDispatcher) as T
        }
        throw IllegalArgumentException("Unknown ViewModel class")
    }
}

/**
 * ViewModel for managing profiler operations and UI state. Simplified to use ProfilerService with NotificationsDelegate
 * integration.
 */
class ProfilerViewModel(
    private val application: Application,
    private val mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
    private val profilerUtils: ProfilerUtils = ProfilerUtils,
) : AndroidViewModel(application) {

    @VisibleForTesting internal val delayToUpdateStatus = 50.milliseconds
    private val profiler: Profiler? = application.components.core.engine.profiler
    private val _isActive = MutableStateFlow(profiler?.isProfilerActive() ?: false)
    val isProfilerActive: StateFlow<Boolean> = _isActive.asStateFlow()

    private val _uiState = MutableStateFlow<ProfilerUiState>(ProfilerUiState.Idle)
    val uiState: StateFlow<ProfilerUiState> = _uiState.asStateFlow()

    private var stateReceiver: BroadcastReceiver? = null

    init {
        stateReceiver =
            object : BroadcastReceiver() {
                override fun onReceive(context: Context?, intent: Intent?) {
                    if (intent?.action == GeckoJavaSampler.INTENT_PROFILER_STATE_CHANGED) {
                        val active = intent.getBooleanExtra(ProfilerService.IS_PROFILER_ACTIVE, false)
                        _isActive.value = active
                        if (active && _uiState.value is ProfilerUiState.Starting) {
                            _uiState.value = ProfilerUiState.ShowToast(R.string.profiler_start_dialog_started)
                            _uiState.value = ProfilerUiState.Running
                        } else if (!active) {
                            val currentState = _uiState.value
                            if (currentState is ProfilerUiState.Running || currentState is ProfilerUiState.Stopping) {
                                _uiState.value = ProfilerUiState.Finished(null)
                            }
                        }
                    }
                }
            }
        val filter = IntentFilter(GeckoJavaSampler.INTENT_PROFILER_STATE_CHANGED)
        val permission = "${application.packageName}.permission.PROFILER_INTERNAL"
        ContextCompat.registerReceiver(
            application,
            stateReceiver!!,
            filter,
            permission,
            null,
            ContextCompat.RECEIVER_NOT_EXPORTED,
        )
    }

    /**
     * Initiates the profiler start process. The polling is done in order to make sure the profiler is started when the
     * notification from the [ProfilerService] is shown
     */
    fun initiateProfilerStartProcess(settings: ProfilerSettings) {
        if (profiler == null) {
            _uiState.value = ProfilerUiState.Error(R.string.profiler_error, "Profiler not available")
            return
        }
        if (isProfilerActive.value) {
            _uiState.value = ProfilerUiState.Running
            return
        }

        _uiState.value = ProfilerUiState.Starting
        profiler.startProfiler(settings.threads, settings.features)
    }

    /**
     * Stops the profiler and saves the collected profile data. This is for UI-initiated stops, so it should NOT create
     * files via ProfilerService.
     */
    fun stopProfilerAndSave() {
        if (profiler == null || !isProfilerActive.value) {
            _uiState.value = ProfilerUiState.Finished(null)
            return
        }

        _uiState.value = ProfilerUiState.Gathering

        profiler.stopProfiler(
            onSuccess = { profileData ->
                viewModelScope.launch(mainDispatcher) {
                    if (profileData != null) {
                        handleProfileSaveInternal(profileData)
                    } else {
                        _uiState.value = ProfilerUiState.Error(R.string.profiler_no_info)
                    }
                }
            },
            onError = { error ->
                viewModelScope.launch(mainDispatcher) {
                    val errorMessage = error.message ?: "Unknown stop error"
                    _uiState.value = ProfilerUiState.Error(R.string.profiler_error, errorMessage)
                }
            },
        )
    }

    /**
     * Stops the profiler without saving the collected data. This is for UI-initiated stops, so it should NOT create
     * files via ProfilerService.
     */
    fun stopProfilerWithoutSaving() {
        if (profiler == null || !isProfilerActive.value) {
            _uiState.value = ProfilerUiState.Finished(null)
            return
        }

        _uiState.value = ProfilerUiState.Stopping

        profiler.stopProfiler(
            onSuccess = {
                viewModelScope.launch(mainDispatcher) {
                    _uiState.value = ProfilerUiState.Finished(null)
                }
            },
            onError = { error ->
                viewModelScope.launch(mainDispatcher) {
                    val errorMessage = error.message ?: "Unknown stop error"
                    _uiState.value = ProfilerUiState.Error(R.string.profiler_error, errorMessage)
                }
            },
        )
    }

    private suspend fun handleProfileSaveInternal(profileData: ByteArray) {
        try {
            val url =
                withContext(ioDispatcher) {
                    profilerUtils.saveProfileUrlToClipboard(profileData, application)
                }

            profilerUtils.finishProfileSave(application, url)
            _uiState.value = ProfilerUiState.ShowToast(R.string.profiler_uploaded_url_to_clipboard)

            delay(delayToUpdateStatus)

            _uiState.value = ProfilerUiState.Finished(url)
        } catch (e: IOException) {
            handleViewModelError(e, R.string.profiler_io_error, "Upload failed")
        } catch (e: SecurityException) {
            handleViewModelError(e, R.string.profiler_io_error, "Permission denied")
        } catch (e: JSONException) {
            handleViewModelError(e, R.string.profiler_io_error, "Server response invalid")
        } catch (e: IllegalArgumentException) {
            handleViewModelError(e, R.string.profiler_io_error, "Invalid profile data")
        }
    }

    /** Resets the UI state to idle if it isn't already. */
    fun resetUiState() {
        if (_uiState.value != ProfilerUiState.Idle) {
            _uiState.value = ProfilerUiState.Idle
        }
    }

    override fun onCleared() {
        stateReceiver?.let { application.unregisterReceiver(it) }
    }

    private fun handleViewModelError(
        exception: Exception,
        @StringRes errorMessageRes: Int,
        fallbackMessage: String = "Operation failed",
    ) {
        _uiState.value =
            ProfilerUiState.Error(
                errorMessageRes,
                exception.message ?: fallbackMessage,
            )
    }
}
