/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home

import android.content.Context
import android.view.View
import io.mockk.every
import io.mockk.mockk
import io.mockk.spyk
import io.mockk.verify
import kotlin.test.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Before
import org.junit.Test
import org.mozilla.fenix.FenixApplication
import org.mozilla.fenix.components.Core
import org.mozilla.fenix.ext.application
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.utils.Settings

class HomeFragmentTest {

    private lateinit var settings: Settings
    private lateinit var context: Context
    private lateinit var core: Core
    private lateinit var homeFragment: HomeFragment
    private lateinit var view: View

    @Before
    fun setup() {
        settings = mockk(relaxed = true)
        context = mockk(relaxed = true)
        core = mockk(relaxed = true)
        view = mockk(relaxed = true)

        val fenixApplication: FenixApplication = mockk(relaxed = true)

        homeFragment = spyk(HomeFragment())

        every { context.application } returns fenixApplication
        every { homeFragment.context } answers { context }
        every { context.components.settings } answers { settings }
        every { context.components.core } answers { core }
        every { homeFragment.viewLifecycleOwner } returns mockk(relaxed = true)
    }

    @Test
    fun `WHEN isMicrosurveyEnabled is true GIVEN a call to initializeMicrosurveyFeature THEN messagingFeature is initialized and observer is added`() {
        val lifecycle = homeFragment.viewLifecycleOwner.lifecycle

        assertNull(homeFragment.messagingFeatureMicrosurvey.get())

        homeFragment.initializeMicrosurveyFeature(isMicrosurveyEnabled = true, view = view)

        val feature = homeFragment.messagingFeatureMicrosurvey.get()
        assertNotNull(feature)
        verify { lifecycle.addObserver(feature) }
    }

    @Test
    fun `WHEN isMicrosurveyEnabled is false GIVEN a call to initializeMicrosurveyFeature THEN messagingFeature is not initialized and observer is not added`() {
        val lifecycle = homeFragment.viewLifecycleOwner.lifecycle

        assertNull(homeFragment.messagingFeatureMicrosurvey.get())

        homeFragment.initializeMicrosurveyFeature(isMicrosurveyEnabled = false, view = view)

        assertNull(homeFragment.messagingFeatureMicrosurvey.get())
        verify(exactly = 0) { lifecycle.addObserver(any()) }
    }
}
