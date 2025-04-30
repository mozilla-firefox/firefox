/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import org.junit.Ignore
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.helpers.AppAndSystemHelper.runWithCondition
import org.mozilla.fenix.helpers.DataGenerationHelper.getSponsoredFxSuggestPlaceHolder
import org.mozilla.fenix.helpers.HomeActivityTestRule
import org.mozilla.fenix.helpers.TestHelper
import org.mozilla.fenix.helpers.TestSetup
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.navigationToolbar

/**
 *  Tests for verifying the Firefox suggest search fragment
 *
 */

class FirefoxSuggestTest : TestSetup() {

    @get:Rule
    val activityTestRule = AndroidComposeTestRule(
        HomeActivityTestRule(
            skipOnboarding = true,
            isPocketEnabled = false,
            isRecentTabsFeatureEnabled = false,
            isWallpaperOnboardingEnabled = false,
        ),
    ) { it.activity }

    @get:Rule
    val memoryLeaksRule = DetectMemoryLeaksRule()

    private val sponsoredKeyWords: Map<String, List<String>> =
        mapOf(
            "Amazon" to
                listOf(
                    "Amazon.com - Official Site",
                    "amazon.com/?tag=admarketus-20&ref=pd_sl_924ab4435c5a5c23aa2804307ee0669ab36f88caee841ce51d1f2ecb&mfadid=adm",
                ),
            "Nike" to
                listOf(
                    "Nike.com - Official Site",
                    "nike.com/?cp=16423867261_search_318370984us128${getSponsoredFxSuggestPlaceHolder()}&mfadid=adm",
                ),
            "Houzz" to listOf(
                "Houzz.com - Official Site",
                "houzz.com/products?m_refid=us-dsp-mpl-admp-219577_15416306_kwd-353208810&adcid=319104989us1287${getSponsoredFxSuggestPlaceHolder()}&utm_source=admarketplace&utm_medium=cpc&utm_campaign=Privacy&utm_term=houzz&utm_content=319104989&mfadid=adm",
            ),
            "Spanx" to listOf(
                "SPANX® -  Official Site",
                "spanx.com/?utm_source=admarketplace&utm_medium=cpc&utm_campaign=privacy&utm_content=319093361us1202${getSponsoredFxSuggestPlaceHolder()}&mfadid=adm",
            ),
            "Bloom" to listOf(
                "Bloomingdales.com - Official Site",
                "bloomingdales.com/?cm_mmc=Admarketplace-_-Privacy-_-Privacy-_-privacy%20instant%20suggest-_-319093353us1228${getSponsoredFxSuggestPlaceHolder()}-_-kclickid__kenshoo_clickid_&mfadid=adm",
            ),
            "Groupon" to listOf(
                "groupon.com - Discover & Save!",
                "groupon.com/?utm_source=google&utm_medium=cpc&utm_campaign=us_dt_sea_ggl_txt_smp_sr_cbp_ch1_nbr_k*groupon_m*broad_d*ADMRKT_319093357us1279${getSponsoredFxSuggestPlaceHolder()}&mfadid=adm",
            ),
        )

    private val sponsoredKeyWord = sponsoredKeyWords.keys.random()

    private val nonSponsoredKeyWords: Map<String, List<String>> =
        mapOf(
            "Marvel" to
                listOf(
                    "Wikipedia - Marvel Cinematic Universe",
                    "wikipedia.org/wiki/Marvel_Cinematic_Universe",
                ),
            "Apple" to
                listOf(
                    "Wikipedia - Apple Inc.",
                    "wikipedia.org/wiki/Apple_Inc",
                ),
            "Africa" to listOf(
                "Wikipedia - African Union",
                "wikipedia.org/wiki/African_Union",
            ),
            "Ultimate" to listOf(
                "Wikipedia - Ultimate Fighting Championship",
                "wikipedia.org/wiki/Ultimate_Fighting_Championship",
            ),
            "Youtube" to listOf(
                "Wikipedia - YouTube",
                "wikipedia.org/wiki/YouTube",
            ),
            "Fifa" to listOf(
                "Wikipedia - FIFA World Cup",
                "en.m.wikipedia.org/wiki/FIFA_World_Cup",
            ),
        )

    private val nonSponsoredKeyWord = nonSponsoredKeyWords.keys.random()

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2348361
    // Known bug that might affect this UI test: https://bugzilla.mozilla.org/show_bug.cgi?id=1813587
    @Ignore("Failing, see: https://bugzilla.mozilla.org/show_bug.cgi?id=1898435")
    @SmokeTest
    @Test
    fun verifyFirefoxSuggestSponsoredSearchResultsTest() {
        runWithCondition(TestHelper.appContext.settings().enableFxSuggest) {
            navigationToolbar {
            }.clickUrlbar {
                typeSearch(searchTerm = sponsoredKeyWord)
                verifySponsoredSuggestionsResults(
                    rule = activityTestRule,
                    searchSuggestions = arrayOf(
                        "Firefox Suggest",
                        sponsoredKeyWords.getValue(sponsoredKeyWord)[0],
                        "Sponsored",
                    ),
                    searchTerm = sponsoredKeyWord,
                )
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2348362
    // Known bug that might affect this UI test: https://bugzilla.mozilla.org/show_bug.cgi?id=1813587
    @Ignore("Failing, see: https://bugzilla.mozilla.org/show_bug.cgi?id=1898435")
    @Test
    fun verifyFirefoxSuggestSponsoredSearchResultsWithPartialKeywordTest() {
        runWithCondition(TestHelper.appContext.settings().enableFxSuggest) {
            navigationToolbar {
            }.clickUrlbar {
                typeSearch(searchTerm = sponsoredKeyWord.dropLast(1))
                verifySponsoredSuggestionsResults(
                    rule = activityTestRule,
                    searchSuggestions = arrayOf(
                        "Firefox Suggest",
                        sponsoredKeyWords.getValue(sponsoredKeyWord)[0],
                        "Sponsored",
                    ),
                    searchTerm = sponsoredKeyWord.dropLast(1),
                )
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2348363
    // Known bug that might affect this UI test: https://bugzilla.mozilla.org/show_bug.cgi?id=1813587
    @Ignore("Failing, see: https://bugzilla.mozilla.org/show_bug.cgi?id=1898435")
    @Test
    fun openFirefoxSuggestSponsoredSearchResultsTest() {
        runWithCondition(TestHelper.appContext.settings().enableFxSuggest) {
            navigationToolbar {
            }.clickUrlbar {
                typeSearch(searchTerm = sponsoredKeyWord)
                verifySponsoredSuggestionsResults(
                    rule = activityTestRule,
                    searchSuggestions = arrayOf(
                        "Firefox Suggest",
                        sponsoredKeyWords.getValue(sponsoredKeyWord)[0],
                        "Sponsored",
                    ),
                    searchTerm = sponsoredKeyWord,
                )
            }.clickSearchSuggestion(sponsoredKeyWords.getValue(sponsoredKeyWord)[0]) {
                verifyUrl(sponsoredKeyWords.getValue(sponsoredKeyWord)[1])
                verifyTabCounter("1")
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2348369
    // Known bug that might affect this UI test: https://bugzilla.mozilla.org/show_bug.cgi?id=1813587
    @Ignore("Failing, see: https://bugzilla.mozilla.org/show_bug.cgi?id=1898435")
    @Test
    fun verifyFirefoxSuggestSponsoredSearchResultsWithEditedKeywordTest() {
        runWithCondition(TestHelper.appContext.settings().enableFxSuggest) {
            navigationToolbar {
            }.clickUrlbar {
                typeSearch(searchTerm = sponsoredKeyWord)
                deleteSearchKeywordCharacters(numberOfDeletionSteps = 1)
                verifySponsoredSuggestionsResults(
                    rule = activityTestRule,
                    searchSuggestions = arrayOf(
                        "Firefox Suggest",
                        sponsoredKeyWords.getValue(sponsoredKeyWord)[0],
                        "Sponsored",
                    ),
                    searchTerm = sponsoredKeyWord,
                    shouldEditKeyword = true,
                    numberOfDeletionSteps = 1,
                )
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2348374
    // Known bug that might affect this UI test: https://bugzilla.mozilla.org/show_bug.cgi?id=1813587
    @Ignore("Failing, see: https://bugzilla.mozilla.org/show_bug.cgi?id=1882035")
    @SmokeTest
    @Test
    fun verifyFirefoxSuggestNonSponsoredSearchResultsTest() {
        runWithCondition(TestHelper.appContext.settings().enableFxSuggest) {
            navigationToolbar {
            }.clickUrlbar {
                typeSearch(searchTerm = nonSponsoredKeyWord)
                verifySponsoredSuggestionsResults(
                    rule = activityTestRule,
                    searchSuggestions = arrayOf(
                        "Firefox Suggest",
                        nonSponsoredKeyWords.getValue(nonSponsoredKeyWord)[0],
                    ),
                    searchTerm = nonSponsoredKeyWord,
                )
                verifySuggestionsAreNotDisplayed(
                    rule = activityTestRule,
                    searchSuggestions = arrayOf(
                        "Sponsored",
                    ),
                )
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2348375
    // Known bug that might affect this UI test: https://bugzilla.mozilla.org/show_bug.cgi?id=1813587
    @Ignore("Failing, see: https://bugzilla.mozilla.org/show_bug.cgi?id=1882035")
    @Test
    fun verifyFirefoxSuggestNonSponsoredSearchResultsWithPartialKeywordTest() {
        runWithCondition(TestHelper.appContext.settings().enableFxSuggest) {
            navigationToolbar {
            }.clickUrlbar {
                typeSearch(searchTerm = nonSponsoredKeyWord.dropLast(1))
                verifySponsoredSuggestionsResults(
                    rule = activityTestRule,
                    searchSuggestions = arrayOf(
                        "Firefox Suggest",
                        nonSponsoredKeyWords.getValue(nonSponsoredKeyWord)[0],
                    ),
                    searchTerm = nonSponsoredKeyWord.dropLast(1),
                )
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2348376
    // Known bug that might affect this UI test: https://bugzilla.mozilla.org/show_bug.cgi?id=1813587
    @Ignore("Failing, see: https://bugzilla.mozilla.org/show_bug.cgi?id=1882035")
    @Test
    fun openFirefoxSuggestNonSponsoredSearchResultsTest() {
        runWithCondition(TestHelper.appContext.settings().enableFxSuggest) {
            navigationToolbar {
            }.clickUrlbar {
                typeSearch(searchTerm = nonSponsoredKeyWord)
                verifySponsoredSuggestionsResults(
                    rule = activityTestRule,
                    searchSuggestions = arrayOf(
                        "Firefox Suggest",
                        nonSponsoredKeyWords.getValue(nonSponsoredKeyWord)[0],
                    ),
                    searchTerm = nonSponsoredKeyWord,
                )
            }.clickSearchSuggestion(nonSponsoredKeyWords.getValue(nonSponsoredKeyWord)[0]) {
                waitForPageToLoad()
                verifyUrl(nonSponsoredKeyWords.getValue(nonSponsoredKeyWord)[1])
            }
        }
    }
}
