/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.settings

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.systemBarsPadding
import androidx.compose.material3.Icon
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.colorResource
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.core.content.ContextCompat
import androidx.fragment.app.Fragment
import androidx.fragment.compose.content
import mozilla.components.compose.base.button.IconButton
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.focus.R
import org.mozilla.focus.ext.hideToolbar
import org.mozilla.focus.ui.theme.FocusTheme
import org.mozilla.focus.ui.theme.focusColors

/**
 * Fragment acting as a wrapper over a [Composable] which will be shown below a [TopAppBar].
 *
 * Useful for Fragments shown in otherwise fullscreen Activities such that they would be shown beneath the status bar
 * not below it.
 *
 * Classes extending this are expected to provide the [Composable] content and toolbar title.
 */
abstract class BaseComposeFragment : Fragment() {
    /** Screen title shown in toolbar. */
    open val titleRes: Int? = null

    open val titleText: String? = null

    open val backgroundColorResource: Int = R.color.settings_background

    /** Callback for the up navigation button shown in toolbar. */
    open fun onNavigateUp(): () -> Unit = {
        activity?.onBackPressedDispatcher?.onBackPressed()
    }

    /** content of the screen in compose. That will be shown below Toolbar */
    @Composable abstract fun Content()

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View {
        val title = getTitle()
        return content {
            FocusTheme {
                Scaffold(
                    modifier = Modifier.systemBarsPadding(),
                    topBar = {
                        FocusTopAppBar(
                            title = title,
                            modifier = Modifier,
                            onNavigateUpClick = onNavigateUp(),
                        )
                    },
                ) { paddingValues ->
                    Column(
                        modifier =
                            Modifier.background(colorResource(id = backgroundColorResource)).padding(paddingValues)
                    ) {
                        this@BaseComposeFragment.Content()
                    }
                }
            }
        }
            .apply {
                isTransitionGroup = true
                setBackgroundColor(ContextCompat.getColor(requireContext(), backgroundColorResource))
            }
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        hideToolbar()
    }

    private fun getTitle(): String {
        var title = ""
        titleRes?.let { title = getString(it) }
        titleText?.let { title = it }
        return title
    }
}

@Composable
private fun FocusTopAppBar(
    title: String,
    modifier: Modifier = Modifier,
    onNavigateUpClick: () -> Unit,
) {
    TopAppBar(
        title = {
            Text(text = title)
        },
        modifier = modifier,
        navigationIcon = {
            IconButton(
                onClick = onNavigateUpClick,
                contentDescription = stringResource(R.string.go_back),
            ) {
                Icon(
                    painter = painterResource(id = iconsR.drawable.mozac_ic_back_24),
                    contentDescription = null,
                )
            }
        },
        colors =
            TopAppBarDefaults.topAppBarColors(
                containerColor = colorResource(R.color.settings_background),
                scrolledContainerColor = colorResource(R.color.settings_background),
                navigationIconContentColor = focusColors.toolbarColor,
                titleContentColor = focusColors.toolbarColor,
            ),
    )
}
