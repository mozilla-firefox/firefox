/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.topsites

import io.mockk.mockk
import mozilla.components.browser.state.search.RegionState
import mozilla.components.lib.crash.CrashReporter
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.R
import org.mozilla.fenix.home.topsites.utils.fetchDefaultTopSites
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class DefaultShortcutsTest {

    private val crashReporter: CrashReporter = mockk(relaxed = true)

    @Test
    fun `GIVEN the default region has no default top sites of its own WHEN the default top sites are fetched THEN Wikipedia is returned`() {
        assertEquals(
            listOf("Wikipedia" to "https://www.wikipedia.org/"),
            defaultTopSites(region = RegionState.Default.current),
        )
        assertEquals(listOf("Wikipedia" to "https://www.wikipedia.org/"), defaultTopSites(region = "CA"))
    }

    @Test
    fun `GIVEN the region is JP WHEN the default top sites are fetched THEN the JP default top sites are returned`() {
        assertEquals(
            listOf(
                "NHKニュース" to "https://www3.nhk.or.jp/news",
                "気象庁" to "https://www.jma.go.jp/",
                "ジョルダン" to "https://www.jorudan.co.jp/",
                "Jリーグ" to "https://www.jleague.jp/",
                "クラシル" to "https://www.kurashiru.com/",
            ),
            defaultTopSites("JP"),
        )
    }

    @Test
    fun `GIVEN the region is DE WHEN the default top sites are fetched THEN the DE default top sites are returned`() {
        assertEquals(
            listOf(
                "Tagesschau" to "https://www.tagesschau.de/",
                "DWD" to "https://www.dwd.de/",
                "Deutsche Bahn" to "https://www.bahn.de/",
                "Kicker" to "https://www.kicker.de/",
                "Chefkoch" to "https://www.chefkoch.de/",
            ),
            defaultTopSites("DE"),
        )
    }

    @Test
    fun `GIVEN the region is US WHEN the default top sites are fetched THEN the US default top sites are returned`() {
        assertEquals(
            listOf(
                "AP News" to "https://apnews.com/",
                "National Weather Service" to "https://www.weather.gov/",
                "NFL" to "https://www.nfl.com/",
                "AllRecipes" to "https://www.allrecipes.com/",
            ),
            defaultTopSites("US"),
        )
    }

    @Test
    fun `GIVEN the region is IN WHEN the default top sites are fetched THEN the IN default top sites are returned`() {
        assertEquals(
            listOf(
                "PTI" to "https://www.ptinews.com/",
                "IMD" to "https://mausam.imd.gov.in/index_en.php",
                "IRCTC" to "https://www.irctc.co.in/",
                "Cricbuzz" to "https://www.cricbuzz.com/",
                "Veg Recipes of India" to "https://www.vegrecipesofindia.com/",
            ),
            defaultTopSites("IN"),
        )
    }

    @Test
    fun `GIVEN the region is FR WHEN the default top sites are fetched THEN the FR default top sites are returned`() {
        assertEquals(
            listOf(
                "France Info" to "https://www.francetvinfo.fr/",
                "Météo-France" to "https://meteofrance.com/",
                "SNCF Connect" to "https://www.sncf-connect.com/",
                "L'Équipe" to "https://www.lequipe.fr/",
                "750g" to "https://www.750g.com/",
            ),
            defaultTopSites("FR"),
        )
    }

    @Test
    fun `GIVEN the region is BR WHEN the default top sites are fetched THEN the BR default top sites are returned`() {
        assertEquals(
            listOf(
                "Agência Brasil" to "https://agenciabrasil.ebc.com.br/",
                "INMET" to "https://portal.inmet.gov.br/",
                "TudoGostoso" to "https://www.tudogostoso.com.br/",
            ),
            defaultTopSites("BR"),
        )
    }

    @Test
    fun `GIVEN the region is GB WHEN the default top sites are fetched THEN the GB default top sites are returned`() {
        assertEquals(
            listOf(
                "BBC News" to "https://www.bbc.co.uk/news",
                "Met Office" to "https://www.metoffice.gov.uk/",
                "National Rail Enquiries" to "https://www.nationalrail.co.uk/",
                "BBC Sport" to "https://www.bbc.co.uk/sport",
                "Delicious" to "https://www.deliciousmagazine.co.uk/",
            ),
            defaultTopSites("GB"),
        )
    }

    @Test
    fun `GIVEN the region is PL WHEN the default top sites are fetched THEN the PL default top sites are returned`() {
        assertEquals(
            listOf(
                "PAP" to "https://www.pap.pl/",
                "IMGW" to "https://www.imgw.pl/",
                "Rozkład PKP" to "https://rozklad-pkp.pl/",
                "Przegląd Sportowy" to "https://przegladsportowy.onet.pl/",
                "Ania Gotuje" to "https://aniagotuje.pl/",
            ),
            defaultTopSites("PL"),
        )
    }

    @Test
    fun `GIVEN the region is IT WHEN the default top sites are fetched THEN the IT default top sites are returned`() {
        assertEquals(
            listOf(
                "RAI News" to "https://www.rainews.it/",
                "Meteo Aeronautica Militare" to "https://www.meteoam.it/",
                "Trenitalia" to "https://www.trenitalia.com/",
                "Gazzetta dello Sport" to "https://www.gazzetta.it/",
                "GialloZafferano" to "https://www.giallozafferano.it/",
            ),
            defaultTopSites("IT"),
        )
    }

    @Test
    fun `GIVEN the region is ES WHEN the default top sites are fetched THEN the ES default top sites are returned`() {
        assertEquals(
            listOf(
                "EFE" to "https://www.efe.com/",
                "AEMET" to "https://www.aemet.es/",
                "RENFE" to "https://www.renfe.com/",
                "Marca" to "https://www.marca.com/",
                "RecetasGratis" to "https://www.recetasgratis.net/",
            ),
            defaultTopSites("ES"),
        )
    }

    @Test
    fun `GIVEN the regional default shortcuts experiment is disabled WHEN the default top sites are fetched THEN Wikipedia is returned`() {
        listOf(RegionState.Default.current, "CA", "DE", "US", "IN", "FR", "BR", "GB", "PL", "IT", "ES").forEach { region
            ->
            assertEquals(
                listOf("Wikipedia" to "https://www.wikipedia.org/"),
                defaultTopSites(region, experiments = emptySet()),
            )
        }
    }

    @Test
    fun `GIVEN the regional default shortcuts experiment is disabled and the region is JP WHEN the default top sites are fetched THEN no default top sites are returned`() {
        assertEquals(emptyList<Pair<String, String>>(), defaultTopSites("JP", experiments = emptySet()))
    }

    private fun defaultTopSites(
        region: String,
        experiments: Set<String> = setOf(FenixDefaultTopSitesProvider.REGIONAL_DEFAULT_SHORTCUTS_EXPERIMENT),
    ): List<Pair<String, String>> =
        fetchDefaultTopSites(testContext.resources, R.raw.default_shortcuts, crashReporter, region, experiments).map {
            it.displayTitle() to it.url
        }
}
