/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.fragment.onboarding

import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material.Button
import androidx.compose.material.ButtonDefaults
import androidx.compose.material.Text
import androidx.compose.material.minimumInteractiveComponentSize
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.colorResource
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.onClick
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.LinkAnnotation
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.TextLinkStyles
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.withLink
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import mozilla.components.support.utils.ext.isLandscape
import mozilla.components.ui.colors.PhotonColors
import org.mozilla.focus.R
import org.mozilla.focus.ui.theme.FocusTheme
import org.mozilla.focus.ui.theme.focusTypography
import org.mozilla.focus.ui.theme.gradientBackground

private const val TOP_SPACER_WEIGHT = 1f
private const val MIDDLE_SPACER_WEIGHT = 0.7f

@Composable
@Preview
private fun OnBoardingFirstScreenComposePreview() {
    FocusTheme {
        OnBoardingFirstScreenCompose({}, {}, {})
    }
}

/**
 * Displays the first onBoarding screen.
 *
 * @param termsOfServiceOnClick called when the user clicks on the terms of service link.
 * @param privacyNoticeOnClick called when the user clicks on the privacy notice link.
 * @param buttonOnClick called when the user clicks on the 'continue' button.
 */
@Composable
fun OnBoardingFirstScreenCompose(
    termsOfServiceOnClick: () -> Unit,
    privacyNoticeOnClick: () -> Unit,
    buttonOnClick: () -> Unit,
) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .gradientBackground(),
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Spacer(Modifier.weight(TOP_SPACER_WEIGHT))

        Image(
            painter = painterResource(R.drawable.onboarding_logo),
            contentDescription = LocalContext.current.getString(R.string.app_name),
            modifier = Modifier
                .size(150.dp, 150.dp)
                .then(
                    if (LocalContext.current.isLandscape()) {
                        Modifier.weight(1f, false)
                    } else {
                        Modifier
                    },
                ),
        )

        TitleContent()

        Spacer(Modifier.weight(MIDDLE_SPACER_WEIGHT))

        LinkText(
            text = stringResource(
                R.string.onboarding_first_screen_terms_of_use_text,
                stringResource(R.string.onboarding_first_screen_terms_of_use_link),
            ),
            linkText = stringResource(R.string.onboarding_first_screen_terms_of_use_link),
            onClick = termsOfServiceOnClick,
        )

        LinkText(
            text = stringResource(
                R.string.onboarding_first_screen_privacy_notice_text,
                stringResource(R.string.onboarding_first_screen_privacy_notice_link),
            ),
            linkText = stringResource(R.string.onboarding_first_screen_privacy_notice_link),
            onClick = privacyNoticeOnClick,
        )

        ComponentGoToOnBoardingSecondScreen { buttonOnClick() }
    }
}

@Composable
private fun TitleContent() {
    Text(
        text = stringResource(
            R.string.onboarding_first_screen_title,
            stringResource(R.string.app_name),
        ),
        modifier = Modifier.padding(top = 32.dp, start = 16.dp, end = 16.dp),
        textAlign = TextAlign.Center,
        style = focusTypography.onboardingTitle,
    )

    Text(
        text = stringResource(
            R.string.onboarding_first_screen_subtitle,
        ),
        modifier = Modifier.padding(top = 16.dp, start = 16.dp, end = 16.dp),
        textAlign = TextAlign.Center,
        style = focusTypography.onboardingSubtitle,
    )
}

@Composable
private fun LinkText(text: String, linkText: String, onClick: () -> Unit) {
    val textWithClickableLink = buildAnnotatedString {
        val textWithoutLink =
            text.replace(oldValue = linkText, newValue = "", ignoreCase = true)
        append(textWithoutLink)
        val link =
            LinkAnnotation.Url(
                "",
                TextLinkStyles(SpanStyle(color = colorResource(R.color.preference_learn_more_link))),
            ) { onClick() }
        withLink(link) { append(linkText) }
    }

    val linkAvailableText = stringResource(id = R.string.a11y_link_available)

    Text(
        text = textWithClickableLink,
        modifier = Modifier
            .clearAndSetSemantics {
                onClick {
                    onClick()

                    return@onClick true
                }

                contentDescription = "$textWithClickableLink $linkAvailableText"
            }
            .padding(horizontal = 16.dp)
            .minimumInteractiveComponentSize(),
        textAlign = TextAlign.Center,
        style = focusTypography.onboardingDescription,
    )
}

@Composable
private fun ComponentGoToOnBoardingSecondScreen(goToOnBoardingSecondScreen: () -> Unit) {
    Button(
        onClick = goToOnBoardingSecondScreen,
        modifier = Modifier
            .padding(top = 16.dp, start = 16.dp, end = 16.dp, bottom = 74.dp)
            .fillMaxWidth(),
        colors = ButtonDefaults.textButtonColors(
            backgroundColor = colorResource(R.color.onboardingButtonOneColor),
        ),
    ) {
        Text(
            text = AnnotatedString(
                LocalContext.current.resources.getString(
                    R.string.onboarding_first_screen_button_agree_and_continue,
                ),
            ),
            color = PhotonColors.White,
        )
    }
}
