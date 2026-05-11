/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.merino.manifest

import android.content.res.AssetManager
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.support.test.any
import mozilla.components.support.test.mock
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.doReturn

private val TEST_JSON = MerinoManifestProviderTest::class.java.classLoader!!
    .getResourceAsStream(ASSET_FILE_PATH)!!
    .bufferedReader()
    .readText()

@RunWith(AndroidJUnit4::class)
class MerinoManifestProviderTest {

    private fun providerWith(json: String): MerinoManifestProvider {
        val assetManager: AssetManager = mock()
        doReturn(json.toByteArray().inputStream()).`when`(assetManager).open(any())
        return MerinoManifestProvider(assetManager)
    }

    @Test
    fun `GIVEN host has a non-blank icon WHEN the icon URL is fetched THEN return the icon URL`() {
        val provider = providerWith(TEST_JSON)
        assertNotNull(provider.getIconUrl("google.com"))
    }

    @Test
    fun `GIVEN host is not available in the manifest WHEN the icon URL is fetched THEN return null`() {
        val provider = providerWith(TEST_JSON)
        assertNull(provider.getIconUrl("unknown.com"))
    }

    @Test
    fun `GIVEN host has a blank icon WHEN the icon URL is fetched THEN return null`() {
        val provider = providerWith(
            """{"domains":[{"rank":1,"domain":"example","categories":[],"serp_categories":[],"url":"https://example.com/","title":"Example","icon":""}]}""",
        )
        assertNull(provider.getIconUrl("example.com"))
    }

    @Test
    fun `GIVEN host is known WHEN fetching the manifest entry is called THEN return the full manifest entry`() {
        val provider = providerWith(TEST_JSON)
        val entry = provider.getManifestEntry("facebook.com")

        assertNotNull(entry)
        assertEquals("facebook", entry!!.domain)
        assertEquals("Facebook", entry.title)
        assertTrue(entry.rank > 0)
        assertTrue("expected at least one category", entry.categories.isNotEmpty())
    }

    @Test
    fun `GIVEN host is not available in the manifest WHEN the manifest entry is fetched THEN return null`() {
        val provider = providerWith(TEST_JSON)
        assertNull(provider.getManifestEntry("unknown.com"))
    }

    @Test
    fun `WHEN the top domains are fetched THEN entries are sorted by rank ascending`() {
        val provider = providerWith(TEST_JSON)
        val domains = provider.getTopDomains()

        assertTrue("expected at least one top domain", domains.isNotEmpty())
        domains.zipWithNext().forEach { (a, b) ->
            assertTrue("ranks should be non-decreasing", a.rank <= b.rank)
        }
    }

    @Test
    fun `GIVEN a limit is provided WHEN the top domains are fetched THEN only that many entries are returned`() {
        val provider = providerWith(TEST_JSON)
        val domains = provider.getTopDomains(limit = 2)
        assertEquals(2, domains.size)
        assertTrue("ranks should be non-decreasing", domains[0].rank <= domains[1].rank)
    }

    @Test
    fun `GIVEN the manifest cannot be parsed WHEN any lookup is performed THEN return empty results`() {
        val provider = providerWith("{invalid}")
        assertNull(provider.getIconUrl("google.com"))
        assertNull(provider.getManifestEntry("google.com"))
        assertEquals(0, provider.getTopDomains().size)
    }
}
