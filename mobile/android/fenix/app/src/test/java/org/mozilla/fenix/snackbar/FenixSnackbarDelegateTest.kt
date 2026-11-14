/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.snackbar

import android.view.View
import androidx.compose.material3.SnackbarHostState
import com.google.android.material.snackbar.Snackbar.LENGTH_INDEFINITE
import com.google.android.material.snackbar.Snackbar.LENGTH_LONG
import com.google.android.material.snackbar.Snackbar.LENGTH_SHORT
import io.mockk.MockKAnnotations
import io.mockk.every
import io.mockk.impl.annotations.MockK
import io.mockk.impl.annotations.RelaxedMockK
import io.mockk.mockk
import io.mockk.verify
import kotlin.coroutines.EmptyCoroutineContext
import kotlinx.coroutines.CoroutineScope
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.snackbar.Snackbar
import org.mozilla.fenix.compose.snackbar.SnackbarState
import org.mozilla.fenix.helpers.MockkRetryTestRule

private const val APP_NAME = "Firefox"
private const val EDIT_PASSWORD = "Edit password"

class FenixSnackbarDelegateTest {

    @MockK private lateinit var view: View

    @RelaxedMockK private lateinit var snackbar: Snackbar
    private lateinit var delegate: FenixSnackbarDelegate

    @get:Rule val mockkRule = MockkRetryTestRule()

    @Before
    fun setup() {
        MockKAnnotations.init(this)

        delegate = FenixSnackbarDelegate(view) { parent, state -> snackbar }

        every { view.context.getString(R.string.app_name) } returns APP_NAME
        every { view.context.getString(R.string.edit_2) } returns EDIT_PASSWORD
    }

    @Test
    fun `GIVEN an action listener is not provided WHEN the snackbar is made THEN the snackbar's action listener is null`() {
        val snackbarState =
            delegate.makeSnackbarState(
                snackBarParentView = view,
                text = APP_NAME,
                duration = LENGTH_LONG,
                isError = false,
                actionText = EDIT_PASSWORD,
                withDismissAction = false,
                listener = null,
            )

        assertNull(snackbarState.action)
    }

    @Test
    fun `GIVEN an action string is not provided WHEN the snackbar is made THEN the snackbar's action listener is null`() {
        val snackbarState =
            delegate.makeSnackbarState(
                snackBarParentView = view,
                text = APP_NAME,
                duration = LENGTH_LONG,
                isError = false,
                actionText = null,
                withDismissAction = false,
                listener = {},
            )

        assertNull(snackbarState.action)
    }

    @Test
    fun `GIVEN an action string and an action listener are not provided WHEN the snackbar state is made THEN the snackbar state's listener is null`() {
        val snackbarState =
            delegate.makeSnackbarState(
                snackBarParentView = view,
                text = APP_NAME,
                duration = LENGTH_LONG,
                isError = false,
                actionText = null,
                withDismissAction = false,
                listener = null,
            )

        assertNull(snackbarState.action)
    }

    @Test
    fun `GIVEN the snackbar is an error WHEN the snackbar state is made THEN the snackbar should be the warning type`() {
        val snackbarState =
            delegate.makeSnackbarState(
                snackBarParentView = view,
                text = APP_NAME,
                duration = LENGTH_LONG,
                isError = true,
                actionText = null,
                withDismissAction = false,
                listener = null,
            )

        assertTrue(snackbarState.type == SnackbarState.Type.Warning)
    }

    @Test
    fun `GIVEN the snackbar has a subText WHEN the snackbar state is made THEN the snackbar should be with a subMessage`() {
        val subText = "subText"
        val snackbarState =
            delegate.makeSnackbarState(
                snackBarParentView = view,
                text = APP_NAME,
                subText = subText,
                duration = LENGTH_LONG,
                isError = true,
                actionText = null,
                withDismissAction = false,
                listener = null,
            )

        assertTrue(snackbarState.subMessage?.text == subText)
    }

    @Test
    fun `GIVEN the snackbar is not an error WHEN the snackbar state is made THEN the snackbar should be the default type`() {
        val snackbarState =
            delegate.makeSnackbarState(
                snackBarParentView = view,
                text = APP_NAME,
                duration = LENGTH_LONG,
                isError = false,
                actionText = null,
                withDismissAction = false,
                listener = null,
            )

        assertTrue(snackbarState.type == SnackbarState.Type.Default)
    }

    @Test
    fun `WHEN the snackbar is requested THEN show snackbar is shown`() {
        delegate.show(
            text = APP_NAME,
            duration = LENGTH_LONG,
            action = null,
            listener = null,
        )

        verify { snackbar.show() }
    }

    @Test
    fun `GIVEN a snackbar is shown for indefinite duration WHEN dismiss is called THEN dismiss the snackbar`() {
        delegate.show(
            text = R.string.app_name,
            duration = LENGTH_INDEFINITE,
            action = R.string.edit_2,
            listener = null,
        )

        delegate.dismiss()

        verify { snackbar.dismiss() }
    }

    @Test
    fun `GIVEN a snackbar is shown with a short duration WHEN dismiss is called THEN dismiss the snackbar`() {
        delegate.show(
            text = R.string.app_name,
            duration = LENGTH_SHORT,
            action = R.string.edit_2,
            listener = null,
        )

        delegate.dismiss()

        verify(exactly = 1) { snackbar.dismiss() }
    }

    @Test
    fun `GIVEN an indefinite snackbar is already displayed WHEN a new indefinite snackbar is requested THEN dismiss the original snackbar`() {
        delegate.show(
            text = R.string.app_name,
            duration = LENGTH_INDEFINITE,
            action = R.string.edit_2,
            listener = null,
        )

        delegate.show(
            text = R.string.app_name,
            duration = LENGTH_INDEFINITE,
            action = R.string.edit_2,
            listener = null,
        )

        verify { snackbar.dismiss() }
    }

    @Test
    fun `GIVEN a clickable snackbarState WHEN clicked THEN do not invoke a view is not attached`() {
        var invoked = false
        val snackbarState =
            delegate.makeSnackbarState(
                snackBarParentView = view,
                text = APP_NAME,
                duration = LENGTH_LONG,
                isError = false,
                actionText = "Click me!",
                withDismissAction = false,
                listener = { invoked = true },
            )
        every { view.isAttachedToWindow } returns false

        snackbarState.action?.onClick()
        assertFalse(invoked)

        every { view.isAttachedToWindow } returns true

        snackbarState.action?.onClick()
        assertTrue(invoked)
    }

    @Test
    fun `GIVEN a clickable snackbarState with a snackbarHostState WHEN clicked THEN invoke even if a view is not attached`() {
        var invoked = false
        val composeDelegate =
            FenixSnackbarDelegate(
                snackbarHostState = SnackbarHostState(),
                scope = CoroutineScope(EmptyCoroutineContext),
                context = mockk(relaxed = true),
            )
        val composeSnackbarState =
            composeDelegate.makeSnackbarState(
                snackBarParentView = view,
                text = APP_NAME,
                duration = LENGTH_LONG,
                isError = false,
                actionText = "Click me!",
                withDismissAction = false,
                listener = { invoked = true },
            )
        every { view.isAttachedToWindow } returns false

        composeSnackbarState.action?.onClick()
        assertTrue(invoked)
    }
}
