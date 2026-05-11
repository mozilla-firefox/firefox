package org.mozilla.fenix.ui

import android.os.Build
import androidx.test.filters.SdkSuppress
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.AppAndSystemHelper.dismissSetAsDefaultBrowserOnboardingDialog
import org.mozilla.fenix.helpers.AppAndSystemHelper.runWithCondition
import org.mozilla.fenix.helpers.AppAndSystemHelper.runWithLauncherIntent
import org.mozilla.fenix.helpers.FenixTestRule
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.TestAssetHelper.getGenericAsset
import org.mozilla.fenix.helpers.TestHelper.closeApp
import org.mozilla.fenix.helpers.TestHelper.restartApp
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.homeScreen
import org.mozilla.fenix.ui.robots.navigationToolbar
import kotlin.test.Ignore
import androidx.compose.ui.test.junit4.v2.AndroidComposeTestRule as AndroidComposeTestRuleV2

class OnboardingTest {
    @get:Rule(order = 0)
    val fenixTestRule: FenixTestRule = FenixTestRule()

    private val mockWebServer get() = fenixTestRule.mockWebServer

    @get:Rule(order = 1)
    val composeTestRule =
        AndroidComposeTestRuleV2(
            HomeActivityIntentTestRule.withDefaultSettingsOverrides(launchActivity = false),
        ) { it.activity }

    @get:Rule(order = 2)
    val memoryLeaksRule = DetectMemoryLeaksRule(composeTestRule = { composeTestRule })

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3349493
    @SmokeTest
    @Test
    fun verifyTheTermsOfUseOnboardingCardTest() {
        runWithLauncherIntent(composeTestRule.activityRule) {
            homeScreen(composeTestRule) {
                verifyTheTermsOfUseOnboardingCard()
                clickTheOnboardingCardContinueButton()
                // Check if the device is running on Android version lower than 10
                if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) {
                    // If true, the "Set as default browser" onboarding card is displayed
                    verifyTheSetAsDefaultBrowserOnboardingCard()
                } else {
                    // If the device is running on Android version higher or equal to 10 the "Set as default browser" system dialog is displayed
                    verifyTheSetAsDefaultBrowserSystemDialog()
                }
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3814795
    @SdkSuppress(minSdkVersion = 29)
    @SmokeTest
    @Test
    fun verifyTheSetAsDefaultBrowserOnboardingCardFunctionalityTest() {
        runWithLauncherIntent(composeTestRule.activityRule) {
            homeScreen(composeTestRule) {
                verifyTheTermsOfUseOnboardingCard()
                clickTheOnboardingCardContinueButton()
                verifyTheSetAsDefaultBrowserSystemDialog()
                clickTheSetAsDefaultBrowserDialogCancelButton()
                verifyTheSetAsDefaultBrowserOnboardingCard()
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3349495
    @SdkSuppress(minSdkVersion = 29)
    @SmokeTest
    @Test
    fun verifyTheFirefoxSearchWidgetOnboardingCardTest() {
        runWithLauncherIntent(composeTestRule.activityRule) {
            homeScreen(composeTestRule) {
                clickTheOnboardingCardContinueButton()
                clickTheSetAsDefaultBrowserDialogCancelButton()
                clickNotNowOnboardingCardButton()
                verifyTheFirefoxSearchWidgetOnboardingCard()
                clickNotNowOnboardingCardButton()
                verifyTheStartSyncingOnboardingCard()
                swipeRightTheStartSyncingOnboardingCard()
                verifyTheFirefoxSearchWidgetOnboardingCard()
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3349496
    @Ignore("Disabled, see: https://bugzilla.mozilla.org/show_bug.cgi?id=2037831")
    @SdkSuppress(minSdkVersion = 29)
    @SmokeTest
    @Test
    fun verifyTheStartSyncingOnboardingCardTest() {
        runWithLauncherIntent(composeTestRule.activityRule) {
            homeScreen(composeTestRule) {
                clickTheOnboardingCardContinueButton()
                clickTheSetAsDefaultBrowserDialogCancelButton()
                clickNotNowOnboardingCardButton()
                clickNotNowOnboardingCardButton()
                verifyTheStartSyncingOnboardingCard()
            }.clickTheStartSyncingOnboardingCardButton {
                verifyTurnOnSyncMenu()
            }.goBackToHomeScreen {
                verifyTheStartSyncingOnboardingCard()
                clickNotNowOnboardingCardButton()
                // Check if the device is running on Android version lower than 13
                if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) {
                    // If true, the "Choose address bar" onboarding card is displayed
                    verifyTheChooseYourAddressBarOnboardingCard()
                } else {
                    // If the device is running on Android version higher or equal to 13 the "Turn on notifications" onboarding card is displayed
                    verifyTheTurnOnNotificationsOnboardingCard()
                }
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3349498
    // If the device is running on Android version higher or equal to 13 the "Turn on notifications" onboarding card is displayed
    @Ignore("Disabled, see: https://bugzilla.mozilla.org/show_bug.cgi?id=2037831")
    @SdkSuppress(minSdkVersion = 33)
    @SmokeTest
    @Test
    fun verifyTheNotificationsOnboardingCardTest() {
        runWithLauncherIntent(composeTestRule.activityRule) {
            homeScreen(composeTestRule) {
                clickTheOnboardingCardContinueButton()
                clickTheSetAsDefaultBrowserDialogCancelButton()
                clickNotNowOnboardingCardButton()
                clickNotNowOnboardingCardButton()
                clickNotNowOnboardingCardButton()
                verifyTheTurnOnNotificationsOnboardingCard()
                clickNotNowOnboardingCardButton()
                verifyTheChooseYourAddressBarOnboardingCard()
                swipeRightTheChooseYourAddressBarOnboardingCard()
                verifyTheTurnOnNotificationsOnboardingCard()
                clickTheTurnOnNotificationsOnboardingCardButton()
                verifyTheChooseYourAddressBarOnboardingCard()
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3349499
    @Ignore("Disabled, see: https://bugzilla.mozilla.org/show_bug.cgi?id=2037831")
    @SdkSuppress(minSdkVersion = 29)
    @SmokeTest
    @Test
    fun verifyTheChooseYourAddressBarOnboardingCardTest() {
        val genericPage = mockWebServer.getGenericAsset(1)

        runWithLauncherIntent(composeTestRule.activityRule) {
            homeScreen(composeTestRule) {
                clickTheOnboardingCardContinueButton()
                clickTheSetAsDefaultBrowserDialogCancelButton()
                clickNotNowOnboardingCardButton()
                clickNotNowOnboardingCardButton()
                clickNotNowOnboardingCardButton()
                // Check if the device is running on Android version lower than 13
                if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) {
                    // If true, the "Choose address bar" onboarding card is displayed
                    verifyTheChooseYourAddressBarOnboardingCard()
                } else {
                    // If the device is running on Android version higher or equal to 13 the "Turn on notifications" onboarding card is displayed
                    verifyTheTurnOnNotificationsOnboardingCard()
                    clickNotNowOnboardingCardButton()
                    verifyTheChooseYourAddressBarOnboardingCard()
                }
                clickTheAddressBarOnboardingCardBottomOption()
                clickTheOnboardingCardContinueButton()
                verifyToolbarPosition(true)
            }
            navigationToolbar(composeTestRule) {
            }.enterURLAndEnterToBrowser(genericPage.url) {
                verifyPageContent(genericPage.content)
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3349492
    @Ignore("Disabled, see: https://bugzilla.mozilla.org/show_bug.cgi?id=2037831")
    @SdkSuppress(minSdkVersion = 29)
    @Test
    fun verifyTheOnboardingCardOrderTest() {
        runWithLauncherIntent(composeTestRule.activityRule) {
            homeScreen(composeTestRule) {
                verifyTheTermsOfUseOnboardingCard()
                clickTheOnboardingCardContinueButton()

                clickTheSetAsDefaultBrowserDialogCancelButton()
                verifyTheSetAsDefaultBrowserOnboardingCard()
                clickNotNowOnboardingCardButton()

                verifyTheFirefoxSearchWidgetOnboardingCard()
                clickNotNowOnboardingCardButton()

                verifyTheStartSyncingOnboardingCard()
                clickNotNowOnboardingCardButton()

                // Check if the device is running on Android version lower than 13
                if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) {
                    // If true, the "Choose address bar" onboarding card is displayed
                    verifyTheChooseYourAddressBarOnboardingCard()
                } else {
                    // If the device is running on Android version higher or equal to 13 the "Turn on notifications" onboarding card is displayed
                    verifyTheTurnOnNotificationsOnboardingCard()
                    clickNotNowOnboardingCardButton()
                    verifyTheChooseYourAddressBarOnboardingCard()
                }
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3814666
    @SdkSuppress(minSdkVersion = 29)
    @Test
    fun verifyTheTermsOfUseOnboardingCardCannotBeDismissedWithoutAcceptingTest() {
        runWithLauncherIntent(composeTestRule.activityRule) {
            homeScreen(composeTestRule) {
                verifyTheTermsOfUseOnboardingCard()
                swipeRightTheTermsOfUseOnboardingCard()
                verifyTheTermsOfUseOnboardingCard()
                restartApp(composeTestRule.activityRule)
                verifyTheTermsOfUseOnboardingCard()
                closeApp(composeTestRule.activityRule)
                restartApp(composeTestRule.activityRule)
                verifyTheTermsOfUseOnboardingCard()
                clickTheOnboardingCardContinueButton()
                verifyTheSetAsDefaultBrowserSystemDialog()
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3349494
    @SdkSuppress(minSdkVersion = 29)
    @Test
    fun verifyTheSetAsDefaultBrowserOnboardingCardTest() {
        runWithLauncherIntent(composeTestRule.activityRule) {
            homeScreen(composeTestRule) {
                verifyTheTermsOfUseOnboardingCard()
                clickTheOnboardingCardContinueButton()
                verifyTheSetAsDefaultBrowserSystemDialog()
                clickTheSetAsDefaultBrowserDialogCancelButton()
                verifyTheSetAsDefaultBrowserOnboardingCard()
                clickNotNowOnboardingCardButton()
                verifyTheFirefoxSearchWidgetOnboardingCard()
                swipeRightTheFirefoxSearchWidgetOnboardingCard()
                verifyTheSetAsDefaultBrowserOnboardingCard()
                closeApp(composeTestRule.activityRule)
                restartApp(composeTestRule.activityRule)
                verifyTheTermsOfUseOnboardingCard()
                clickTheOnboardingCardContinueButton()
                verifyTheSetAsDefaultBrowserSystemDialog()
                clickTheSetAsDefaultBrowserDialogCancelButton()
                verifyTheSetAsDefaultBrowserOnboardingCard()
            }
        }
    }
}
