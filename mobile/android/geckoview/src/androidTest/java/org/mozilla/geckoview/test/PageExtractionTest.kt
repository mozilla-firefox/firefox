/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.geckoview.test

import androidx.test.ext.junit.rules.ActivityScenarioRule
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.rules.RuleChain
import org.junit.runner.RunWith
import org.mozilla.geckoview.Autofill
import org.mozilla.geckoview.PageExtractionController
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule

/**
 * Tests the GeckoView integration with the PageExtractor actor in toolkit.
 */
@RunWith(AndroidJUnit4::class)
class PageExtractionTest : BaseSessionTest() {

    /**
     * The page extractor actor considers the rect sizes of elements to determine if they're visible
     * or not, so a headless GeckoSession test does not suffice.
     *
     * We need to and bind our session to a view that will be shown via [GeckoViewTestActivity],
     */
    private val activityRule = ActivityScenarioRule(GeckoViewTestActivity::class.java)

    @get:Rule
    override val rules: RuleChain = RuleChain.outerRule(activityRule).around(sessionRule)

    @Before
    fun setup() {
        activityRule.scenario.onActivity { activity ->
            activity.view.setSession(mainSession)
        }
    }

    @After
    fun cleanup() {
        try {
            activityRule.scenario.onActivity { activity ->
                activity.view.releaseSession()
            }
        } catch (_: Exception) {
        }
    }

    @GeckoSessionTestRule.NullDelegate(Autofill.Delegate::class)
    @Test
    fun returnsPageTextContent() {
        mainSession.loadTestPath(PAGE_EXTRACTION_HTML_PATH)
        mainSession.waitForPageStop()

        val geckoResult = mainSession.sessionPageExtractor.pageContent
        val pageContent = sessionRule.waitForResult(geckoResult)
        mainSession.waitForRoundTrip()

        assertNotNull("Expected page content result to be non-null", pageContent)
        assertEquals(
            "Expected page content result to contain the text as represented in the HTML page",
            """
                 This is an example of a block with inline elements.
                 The computed style is respected for extraction.
            """.trimIndent(),
            pageContent,
        )
    }

    @GeckoSessionTestRule.NullDelegate(Autofill.Delegate::class)
    @Test
    fun removeBoilerplateStripsNonArticleContent() {
        mainSession.loadTestPath(PAGE_EXTRACTION_READER_MODE_HTML_PATH)
        mainSession.waitForPageStop()

        val options = PageExtractionController.ContentParams(true)
        val pageContent = sessionRule.waitForResult(
            mainSession.sessionPageExtractor.getPageContent(options),
        )
        mainSession.waitForRoundTrip()

        assertNotNull("Expected page content result to be non-null", pageContent)
        assertTrue(
            "Expected article body text to be present after boilerplate removal",
            pageContent!!.contains("Lorem ipsum"),
        )
        assertTrue(
            "Expected site header to be stripped by boilerplate removal",
            !pageContent.contains("Site header"),
        )
    }

    @GeckoSessionTestRule.NullDelegate(Autofill.Delegate::class)
    @Test
    fun returnsPageMetadata() {
        mainSession.loadTestPath(PAGE_EXTRACTION_HTML_PATH)
        mainSession.waitForPageStop()

        val metadata = sessionRule.waitForResult(mainSession.sessionPageExtractor.pageMetadata)
        mainSession.waitForRoundTrip()

        assertNotNull("Expected page metadata result to be non-null", metadata)
        assertTrue("Expected word count to be greater than 0", metadata.wordCount > 0)
        assertEquals("Expected language to be 'en'", "en", metadata.language)
        assertTrue(
            "Expected structuredDataTypes to contain 'Article'",
            metadata.structuredDataTypes.contains("Article"),
        )
    }
}
