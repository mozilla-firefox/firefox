/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.base.textfield

import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.ui.icons.R

@Composable
internal fun TextFieldLabel(text: String) {
    Text(
        text = text,
        style = AcornTheme.typography.caption,
    )
}

@Composable
internal fun TextFieldPlaceholder(text: String) {
    Text(
        text = text,
        style = AcornTheme.typography.body1,
    )
}

@Composable
internal fun TextFieldSupportingText(
    isError: Boolean,
    errorText: String,
    supportingText: String? = null,
) {
    (if (isError) errorText else supportingText)?.let { Text(text = it, style = AcornTheme.typography.caption) }
}

@Composable
internal fun TextFieldTrailingIcon(
    isError: Boolean,
    trailingIcon: @Composable (TrailingIconScope.() -> Unit)?,
) {
    if (!isError && trailingIcon == null) return

    Spacer(modifier = Modifier.width(12.dp))

    if (isError) {
        Icon(
            painter = painterResource(R.drawable.mozac_ic_warning_fill_24),
            contentDescription = null,
        )
    } else {
        Row {
            with(TrailingIconScope(this)) {
                trailingIcon?.invoke(this)
            }
        }
    }
}
