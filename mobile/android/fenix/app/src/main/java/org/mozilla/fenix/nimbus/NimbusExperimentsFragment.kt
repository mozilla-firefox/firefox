/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.nimbus

import android.os.Bundle
import android.view.LayoutInflater
import android.view.ViewGroup
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import androidx.fragment.app.Fragment
import androidx.fragment.compose.content
import androidx.navigation.fragment.findNavController
import org.mozilla.fenix.R
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.showToolbar
import org.mozilla.fenix.nimbus.ext.fetchPartitionedExperimentListsAsync
import org.mozilla.fenix.nimbus.view.NimbusExperimentItem
import org.mozilla.fenix.nimbus.view.NimbusExperiments
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * Fragment use for managing Nimbus experiments.
 */
class NimbusExperimentsFragment : Fragment() {

    override fun onResume() {
        super.onResume()
        showToolbar(getString(R.string.preferences_nimbus_experiments))
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ) = content {
        FirefoxTheme {
            var experiments by remember { mutableStateOf(emptyList<NimbusExperimentItem>()) }
            val nimbusSdk = LocalContext.current.components.nimbus.sdk

            LaunchedEffect(Unit) {
                experiments = nimbusSdk.fetchPartitionedExperimentListsAsync()
            }

            NimbusExperiments(
                experiments = experiments,
                onExperimentClick = { experiment ->
                    val directions =
                        NimbusExperimentsFragmentDirections.actionNimbusExperimentsFragmentToNimbusBranchesFragment(
                            experimentId = experiment.slug,
                            experimentName = experiment.userFacingName,
                        )

                    findNavController().navigate(directions)
                },
            )
        }
    }
}
