/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.topsites.ui

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Card
import androidx.compose.material3.CardColors
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CardElevation
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.unit.dp

private val homepageCardRadius = 16.dp
private val homepageCardImageRadius = 12.dp
private val cardElevation = 0.dp

private val homepageCardShape = RoundedCornerShape(homepageCardRadius)
val homepageCardImageShape = RoundedCornerShape(homepageCardImageRadius)

/**
 * Card for use on the homepage, with the default style including a border and rounded corner shape.
 *
 * @param backgroundColor [Color] to be applied to the background of this card.
 * @param modifier [Modifier] to be applied to this card.
 * @param shape [Shape] to be applied to this card.
 * @param elevation [CardElevation] to be applied to this card.
 * @param border [BorderStroke] to be applied to this card.
 * @param content The composable content to display on the card.
 */
@Composable
internal fun HomepageCard(
    backgroundColor: Color,
    modifier: Modifier = Modifier,
    shape: Shape = homepageCardShape,
    elevation: CardElevation = CardDefaults.cardElevation(defaultElevation = cardElevation),
    border: BorderStroke? = null,
    content: @Composable ColumnScope.() -> Unit,
) {
    HomepageCard(
        modifier = modifier,
        shape = shape,
        colors = CardDefaults.cardColors(
            containerColor = backgroundColor,
            contentColor = MaterialTheme.colorScheme.onSurface,
        ),
        elevation = elevation,
        border = null,
        content = content,
    )
}

/**
 * Card for use on the homepage, with the default style including a border and rounded corner shape.
 *
 * @param modifier [Modifier] to be applied to this card.
 * @param shape [Shape] to be applied to this card.
 * @param colors [CardColors] to be applied to this card.
 * @param elevation [CardElevation] to be applied to this card.
 * @param border [BorderStroke] to be applied to this card.
 * @param content The content displayed on the card.
 */
@Composable
internal fun HomepageCard(
    modifier: Modifier = Modifier,
    shape: Shape = homepageCardShape,
    colors: CardColors = CardDefaults.cardColors(),
    elevation: CardElevation = CardDefaults.cardElevation(defaultElevation = cardElevation),
    border: BorderStroke? = BorderStroke(0.dp, MaterialTheme.colorScheme.outlineVariant),
    content: @Composable ColumnScope.() -> Unit,
) {
    Card(
        modifier = modifier,
        shape = shape,
        colors = colors,
        elevation = elevation,
        border = null,
        content = content,
    )
}
