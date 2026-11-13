/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.address

import android.os.Bundle
import android.view.LayoutInflater
import android.view.ViewGroup
import androidx.fragment.compose.content
import androidx.lifecycle.lifecycleScope
import androidx.navigation.fragment.findNavController
import androidx.navigation.fragment.navArgs
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.engine.autofill.AddressStructure
import mozilla.components.lib.state.helpers.StoreProvider.Companion.fragmentStore
import org.mozilla.fenix.SecureFragment
import org.mozilla.fenix.e2e.SystemInsetsPaddedFragment
import org.mozilla.fenix.ext.hideToolbar
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.settings.address.store.AddressEnvironment
import org.mozilla.fenix.settings.address.store.AddressMiddleware
import org.mozilla.fenix.settings.address.store.AddressState
import org.mozilla.fenix.settings.address.store.AddressStore
import org.mozilla.fenix.settings.address.store.AddressStructureMiddleware
import org.mozilla.fenix.settings.address.ui.edit.EditAddressScreen
import org.mozilla.fenix.theme.FirefoxTheme

/** Displays an address editor for adding and editing an address. */
class AddressEditorFragment : SecureFragment(), SystemInsetsPaddedFragment {
    private val args by navArgs<AddressEditorFragmentArgs>()

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ) = content {
        val store =
            fragmentStore(
                AddressState.initial(
                    region = requireComponents.core.store.state.search.region,
                    address = args.address,
                )
            ) {
                val storage = requireComponents.core.autofillStorage
                val engine = requireComponents.core.engine
                val crashReporter = requireComponents.analytics.crashReporter
                val environment =
                    AddressEnvironment(
                        navigateBack = { findNavController().popBackStack() },
                        createAddress = { fields -> storage.addAddress(fields) },
                        updateAddress = { guid, fields -> storage.updateAddress(guid, fields) },
                        deleteAddress = { guid -> storage.deleteAddress(guid) },
                        getAddressStructure = engine::getAddressStructure,
                        submitCaughtException = crashReporter::submitCaughtException,
                    )

                AddressStore(
                    initialState = it,
                    middleware =
                        listOf(
                            AddressMiddleware(
                                environment = environment,
                                scope = viewLifecycleOwner.lifecycleScope,
                            ),
                            AddressStructureMiddleware(
                                environment = environment,
                                scope = viewLifecycleOwner.lifecycleScope,
                            ),
                        ),
                )
            }
        FirefoxTheme {
            EditAddressScreen(store.value)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        hideToolbar()
    }
}

private suspend fun Engine.getAddressStructure(countryCode: String): AddressStructure {
    return withContext(Dispatchers.Main) {
        suspendCancellableCoroutine { continuation ->
            val operation =
                getAddressStructure(
                    countryCode = countryCode,
                    onSuccess = { fields -> continuation.resume(fields) },
                    onError = { throwable -> continuation.resumeWithException(throwable) },
                )

            continuation.invokeOnCancellation {
                @Suppress("DeferredResultUnused") operation.cancel()
            }
        }
    }
}
