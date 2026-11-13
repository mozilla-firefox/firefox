/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.nimbus

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.Fragment
import androidx.lifecycle.lifecycleScope
import androidx.navigation.fragment.findNavController
import androidx.navigation.fragment.navArgs
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import mozilla.components.lib.state.ext.consumeFrom
import mozilla.components.lib.state.helpers.StoreProvider.Companion.fragmentStore
import mozilla.components.service.nimbus.R as nimbusR
import mozilla.components.support.base.log.logger.Logger
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.core.Action
import org.mozilla.fenix.compose.snackbar.Snackbar
import org.mozilla.fenix.compose.snackbar.SnackbarState
import org.mozilla.fenix.e2e.SystemInsetsPaddedFragment
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.navigateWithBreadcrumb
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.ext.showToolbar
import org.mozilla.fenix.nimbus.controller.NimbusBranchesController
import org.mozilla.fenix.nimbus.view.NimbusBranchesView

/** A fragment to show the branches of a Nimbus experiment. */
class NimbusBranchesFragment : Fragment(), SystemInsetsPaddedFragment {

    private lateinit var nimbusBranchesStore: NimbusBranchesStore
    private lateinit var nimbusBranchesView: NimbusBranchesView
    private lateinit var controller: NimbusBranchesController

    private val args by navArgs<NimbusBranchesFragmentArgs>()

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View? {
        return inflater.inflate(nimbusR.layout.mozac_service_nimbus_experiment_details, container, false)
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        nimbusBranchesStore =
            fragmentStore(NimbusBranchesState(branches = emptyList())) {
                    NimbusBranchesStore(it)
                }
                .value

        controller =
            NimbusBranchesController(
                isTelemetryEnabled = { requireComponents.settings.isTelemetryEnabled },
                isExperimentationEnabled = { requireComponents.settings.isExperimentationEnabled },
                nimbusBranchesStore = nimbusBranchesStore,
                experiments = requireContext().components.nimbus.sdk,
                experimentId = args.experimentId,
                notifyUserToEnableExperiments = showSnackbarToEnableExperiments(view.rootView),
            )

        nimbusBranchesView =
            NimbusBranchesView(view.findViewById(nimbusR.id.nimbus_experiment_branches_list), controller)

        loadExperimentBranches()

        consumeFrom(nimbusBranchesStore) { state ->
            nimbusBranchesView.update(state)
        }
    }

    /**
     * Shows a snackbar to inform the user that experiments are disabled and provides an action to navigate to the data
     * choices fragment to enable them.
     *
     * @param rootView The [View] to embed the Snackbar in.
     * @return A lambda function that, when invoked, will display the snackbar.
     */
    private fun showSnackbarToEnableExperiments(rootView: View): () -> Unit = {
        val message = getString(R.string.experiments_snackbar)
        val actionLabel = getString(R.string.experiments_snackbar_button)

        Snackbar.make(
                snackBarParentView = rootView,
                snackbarState =
                    SnackbarState(
                        message = message,
                        duration = SnackbarState.Duration.Preset.Long,
                        action =
                            Action(
                                label = actionLabel,
                                onClick = {
                                    findNavController()
                                        .navigateWithBreadcrumb(
                                            directions =
                                                NimbusBranchesFragmentDirections
                                                    .actionNimbusBranchesFragmentToDataChoicesFragment(),
                                            navigateFrom = "NimbusBranchesController",
                                            navigateTo = "ActionNimbusBranchesFragmentToDataChoicesFragment",
                                            crashReporter = requireContext().components.analytics.crashReporter,
                                        )
                                },
                            ),
                    ),
            )
            .show()
    }

    override fun onResume() {
        super.onResume()
        showToolbar(args.experimentName)
    }

    @Suppress("TooGenericExceptionCaught")
    private fun loadExperimentBranches() {
        lifecycleScope.launch(Dispatchers.IO) {
            try {
                val experiments = requireContext().components.nimbus.sdk
                val branches = experiments.getExperimentBranches(args.experimentId) ?: emptyList()
                val selectedBranch = experiments.getExperimentBranch(args.experimentId) ?: ""

                nimbusBranchesStore.dispatch(
                    NimbusBranchesAction.UpdateBranches(
                        branches,
                        selectedBranch,
                    )
                )
            } catch (e: Throwable) {
                Logger.error("Failed to getActiveExperiments()", e)
            }
        }
    }
}
