package org.mozilla.fenix.ui

import android.os.Build
import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import androidx.test.filters.SdkSuppress
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.AppAndSystemHelper.dismissSetAsDefaultBrowserOnboardingDialog
import org.mozilla.fenix.helpers.AppAndSystemHelper.runWithCondition
import org.mozilla.fenix.helpers.AppAndSystemHelper.runWithLauncherIntent
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.TestSetup
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.homeScreen

class OnboardingTest : TestSetup() {

    @get:Rule
    val activityTestRule =
        AndroidComposeTestRule(
            HomeActivityIntentTestRule.withDefaultSettingsOverrides(launchActivity = false),
        ) { it.activity }

    @get:Rule
    val memoryLeaksRule = DetectMemoryLeaksRule()

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2122321
    @Test
    fun verifyFirstOnboardingCardItemsTest() {
        // Run UI test only on devices with Android version lower than 10
        // because on Android 10 and above, the default browser dialog is shown and the first onboarding card is skipped
        runWithCondition(Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) {
            runWithLauncherIntent(activityTestRule) {
                homeScreen {
                    verifyFirstOnboardingCard(activityTestRule)
                }
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2122334
    @Test
    fun verifyFirstOnboardingCardItemsFunctionalityTest() {
        // Run UI test only on devices with Android version lower than 10
        // because on Android 10 and above, the default browser dialog is shown and the first onboarding card is skipped
        runWithCondition(Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) {
            runWithLauncherIntent(activityTestRule) {
                homeScreen {
                    clickDefaultCardNotNowOnboardingButton(activityTestRule)
                    verifySecondOnboardingCard(activityTestRule)
                    swipeSecondOnboardingCardToRight()
                }.clickSetAsDefaultBrowserOnboardingButton(activityTestRule) {
                    verifyAndroidDefaultAppsMenuAppears()
                }.goBackToOnboardingScreen {
                    verifySecondOnboardingCard(activityTestRule)
                }
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2122343
    @Test
    fun verifySecondOnboardingCardItemsTest() {
        runWithLauncherIntent(activityTestRule) {
            homeScreen {
                // Check if the device is running on Android version lower than 10
                if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) {
                    // If true, click the "Not Now" button from the first onboarding card
                    clickDefaultCardNotNowOnboardingButton(activityTestRule)
                }
                dismissSetAsDefaultBrowserOnboardingDialog()
                verifySecondOnboardingCard(activityTestRule)
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2122344
    @SmokeTest
    @Test
    fun verifyThirdOnboardingCardSignInFunctionalityTest() {
        runWithLauncherIntent(activityTestRule) {
            homeScreen {
                // Check if the device is running on Android version lower than 10
                if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) {
                    // If true, click the "Not Now" button from the first onboarding card
                    clickDefaultCardNotNowOnboardingButton(activityTestRule)
                }
                dismissSetAsDefaultBrowserOnboardingDialog()
                verifySecondOnboardingCard(activityTestRule)
                clickAddSearchWidgetNotNowOnboardingButton(activityTestRule)
                verifyThirdOnboardingCard(activityTestRule)
            }.clickSignInOnboardingButton(activityTestRule) {
                verifyTurnOnSyncMenu()
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2609732
    @SdkSuppress(minSdkVersion = 29)
    @SmokeTest
    @Test
    fun verifySetAsDefaultBrowserDialogWhileFirefoxIsNotSetAsDefaultBrowserTest() {
        runWithLauncherIntent(activityTestRule) {
            homeScreen {
                verifySetAsDefaultBrowserDialogWhileFirefoxIsNotSetAsDefaultBrowser()
            }
        }
    }
}
