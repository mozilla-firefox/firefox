/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:Suppress("MagicNumber")

package mozilla.components.compose.base.theme

import androidx.compose.material3.ColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.ReadOnlyComposable
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.graphics.Color
import mozilla.components.ui.colors.CustomColorsDark
import mozilla.components.ui.colors.CustomColorsLight
import mozilla.components.ui.colors.PhotonColors

/**
 * A custom Color Palette for Mozilla Firefox for Android (Fenix).
 */
@Suppress("LongParameterList")
@Stable
class AcornColors(
    layer2: Color,
    layer3: Color,
    layerAccent: Color,
    layerAccentNonOpaque: Color,
    layerGradientStart: Color,
    layerGradientEnd: Color,
    layerWarning: Color,
    layerCritical: Color,
    layerInformation: Color,
    actionWarning: Color,
    actionCritical: Color,
    actionInformation: Color,
    formDefault: Color,
    textOnColorPrimary: Color,
    iconPrimaryInactive: Color,
    iconActive: Color,
    iconOnColor: Color,
    iconOnColorDisabled: Color,
    iconActionPrimary: Color,
    borderAccent: Color,
    ripple: Color,
    tabActive: Color,
    tabInactive: Color,
    information: Color,
    surfaceDimVariant: Color,
    success: Color,
    iconNormal: Color,
    iconInverse: Color,
    iconSecondary: Color,
    textNormal: Color,
    textSecondary: Color,
    textInverse: Color,
    controlsInputFocusFill: Color,
    controlsButtonSecondaryNormal: Color,
    controlsButtonPrimaryNormal: Color,
    backgroundPlate: Color
) {
    // Layers

    // Card background, Menu background, Dialog, Banner
    var layer2 by mutableStateOf(layer2)
        private set

    // Search
    var layer3 by mutableStateOf(layer3)
        private set

    // App Bar Top (edit), Text Cursor, Selected Tab Check
    var layerAccent by mutableStateOf(layerAccent)
        private set

    // Selected tab
    var layerAccentNonOpaque by mutableStateOf(layerAccentNonOpaque)
        private set

    // Tooltip
    var layerGradientStart by mutableStateOf(layerGradientStart)
        private set

    // Tooltip
    var layerGradientEnd by mutableStateOf(layerGradientEnd)
        private set

    // Warning background
    var layerWarning by mutableStateOf(layerWarning)
        private set

    // Error Background
    var layerCritical by mutableStateOf(layerCritical)
        private set

    // Info background
    var layerInformation by mutableStateOf(layerInformation)
        private set

    // Actions

    // Warning button
    var actionWarning by mutableStateOf(actionWarning)
        private set

    // Error button
    var actionCritical by mutableStateOf(actionCritical)
        private set

    // Info button
    var actionInformation by mutableStateOf(actionInformation)
        private set

    // Checkbox default, Radio button default
    var formDefault by mutableStateOf(formDefault)
        private set

    // Text

    // Text Inverted/On Color
    var textOnColorPrimary by mutableStateOf(textOnColorPrimary)
        private set

    // Icon

    // Inactive tab
    var iconPrimaryInactive by mutableStateOf(iconPrimaryInactive)
        private set

    // Active tab
    var iconActive by mutableStateOf(iconActive)
        private set

    // Icon inverted (on color)
    var iconOnColor by mutableStateOf(iconOnColor)
        private set

    // Disabled icon inverted (on color)
    var iconOnColorDisabled by mutableStateOf(iconOnColorDisabled)
        private set

    // Action primary icon
    var iconActionPrimary by mutableStateOf(iconActionPrimary)
        private set

    // Border

    // Active tab (Nav), Selected tab, Active form
    var borderAccent by mutableStateOf(borderAccent)
        private set

    var ripple by mutableStateOf(ripple)
        private set

    // Tab Active
    var tabActive by mutableStateOf(tabActive)
        private set

    // Tab Inactive
    var tabInactive by mutableStateOf(tabInactive)
        private set

    /*
     * M3 color scheme extensions that do not have a mapped value from Acorn
     */

    /**
     * Attention-grabbing color against surface for fills, icons, and text,
     * indicating neutral information.
     */
    internal var information by mutableStateOf(information)
        private set

    /**
     * Surface Dim Variant
     *
     * Slightly dimmer surface color in light theme.
     */
    var surfaceDimVariant by mutableStateOf(surfaceDimVariant)
        private set

    /**
     * Attention-grabbing color against surface for fills, icons, and text,
     * indicating successful information
     */
    internal var success by mutableStateOf(success)
        private set

    // Normal icon
    var iconNormal by mutableStateOf(iconNormal)
        private set

    // Inverse icon (на темном фоне)
    var iconInverse by mutableStateOf(iconInverse)
        private set

    // Secondary icon
    var iconSecondary by mutableStateOf(iconSecondary)
        private set

    // Normal text
    var textNormal by mutableStateOf(textNormal)
        private set

    // Secondary text
    var textSecondary by mutableStateOf(textSecondary)
        private set

    // Inverse text (на темном фоне)
    var textInverse by mutableStateOf(textInverse)
        private set

    var controlsButtonPrimaryNormal by mutableStateOf(controlsButtonPrimaryNormal)
        private set

    var controlsInputFocusFill by mutableStateOf(controlsInputFocusFill)
        private set

    var controlsButtonSecondaryNormal by mutableStateOf(controlsButtonSecondaryNormal)
        private set

    var backgroundPlate by mutableStateOf(backgroundPlate)
        private set

    /**
     * Updates the existing colors with the provided [AcornColors].
     */
    @Suppress("LongMethod")
    fun update(other: AcornColors) {
        layer2 = other.layer2
        layer3 = other.layer3
        layerAccent = other.layerAccent
        layerAccentNonOpaque = other.layerAccentNonOpaque
        layerGradientStart = other.layerGradientStart
        layerGradientEnd = other.layerGradientEnd
        layerWarning = other.layerWarning
        layerCritical = other.layerCritical
        layerInformation = other.layerInformation
        actionWarning = other.actionWarning
        actionCritical = other.actionCritical
        actionInformation = other.actionInformation
        formDefault = other.formDefault
        textOnColorPrimary = other.textOnColorPrimary
        iconPrimaryInactive = other.iconPrimaryInactive
        iconActive = other.iconActive
        iconOnColor = other.iconOnColor
        iconOnColorDisabled = other.iconOnColorDisabled
        iconActionPrimary = other.iconActionPrimary
        borderAccent = other.borderAccent
        ripple = other.ripple
        tabActive = other.tabActive
        tabInactive = other.tabInactive
        information = other.information
        surfaceDimVariant = other.surfaceDimVariant
        success = other.success
        iconNormal = other.iconNormal
        iconInverse = other.iconInverse
        iconSecondary = other.iconSecondary
        textNormal = other.textNormal
        textSecondary = other.textSecondary
        textInverse = other.textInverse
        controlsInputFocusFill = other.controlsInputFocusFill
        controlsButtonSecondaryNormal = other.controlsButtonSecondaryNormal
        controlsButtonPrimaryNormal = other.controlsButtonPrimaryNormal
        backgroundPlate = other.backgroundPlate
    }

    /**
     * Return a copy of this [AcornColors] and optionally overriding any of the provided values.
     */
    @Suppress("LongMethod")
    fun copy(
        layer2: Color = this.layer2,
        layer3: Color = this.layer3,
        layerAccent: Color = this.layerAccent,
        layerAccentNonOpaque: Color = this.layerAccentNonOpaque,
        layerGradientStart: Color = this.layerGradientStart,
        layerGradientEnd: Color = this.layerGradientEnd,
        layerWarning: Color = this.layerWarning,
        layerCritical: Color = this.layerCritical,
        layerInformation: Color = this.layerInformation,
        actionWarning: Color = this.actionWarning,
        actionCritical: Color = this.actionCritical,
        actionInformation: Color = this.actionInformation,
        formDefault: Color = this.formDefault,
        textOnColorPrimary: Color = this.textOnColorPrimary,
        iconPrimaryInactive: Color = this.iconPrimaryInactive,
        iconActive: Color = this.iconActive,
        iconOnColor: Color = this.iconOnColor,
        iconOnColorDisabled: Color = this.iconOnColorDisabled,
        iconActionPrimary: Color = this.iconActionPrimary,
        borderAccent: Color = this.borderAccent,
        ripple: Color = this.ripple,
        tabActive: Color = this.tabActive,
        tabInactive: Color = this.tabInactive,
        information: Color = this.information,
        surfaceDimVariant: Color = this.surfaceDimVariant,
        success: Color = this.success,
        iconNormal: Color = this.iconNormal,
        iconInverse: Color = this.iconInverse,
        iconSecondary: Color = this.iconSecondary,
        textNormal: Color = this.textNormal,
        textSecondary: Color = this.textSecondary,
        textInverse: Color = this.textInverse,
        controlsInputFocusFill: Color = this.controlsInputFocusFill,
        controlsButtonSecondaryNormal: Color = this.controlsButtonSecondaryNormal,
        controlsButtonPrimaryNormal: Color = this.controlsButtonPrimaryNormal,
        backgroundPlate: Color = this.backgroundPlate
    ): AcornColors = AcornColors(
        layer2 = layer2,
        layer3 = layer3,
        layerAccent = layerAccent,
        layerAccentNonOpaque = layerAccentNonOpaque,
        layerGradientStart = layerGradientStart,
        layerGradientEnd = layerGradientEnd,
        layerWarning = layerWarning,
        layerCritical = layerCritical,
        layerInformation = layerInformation,
        actionWarning = actionWarning,
        actionCritical = actionCritical,
        actionInformation = actionInformation,
        formDefault = formDefault,
        textOnColorPrimary = textOnColorPrimary,
        iconPrimaryInactive = iconPrimaryInactive,
        iconActive = iconActive,
        iconOnColor = iconOnColor,
        iconOnColorDisabled = iconOnColorDisabled,
        iconActionPrimary = iconActionPrimary,
        borderAccent = borderAccent,
        ripple = ripple,
        tabActive = tabActive,
        tabInactive = tabInactive,
        information = information,
        surfaceDimVariant = surfaceDimVariant,
        success = success,
        iconNormal = iconNormal,
        iconInverse = iconInverse,
        iconSecondary = iconSecondary,
        textNormal = textNormal,
        textSecondary = textSecondary,
        textInverse = textInverse,
        controlsInputFocusFill = controlsInputFocusFill,
        controlsButtonSecondaryNormal = controlsButtonSecondaryNormal,
        controlsButtonPrimaryNormal = controlsButtonPrimaryNormal,
        backgroundPlate = backgroundPlate
    )
}

val darkColorPalette = AcornColors(
    layer2 = PhotonColors.DarkGrey30,
    layerAccent = PhotonColors.Violet40,
    layer3 = CustomColorsDark.BackgroundNormal,// бекраунд под табами
    layerAccentNonOpaque = CustomColorsDark.BackgroundNormal,
    layerGradientStart = CustomColorsDark.BackgroundNormal,
    layerGradientEnd = CustomColorsDark.BackgroundNormal,
    layerWarning = CustomColorsDark.BackgroundNormal,
    layerCritical = CustomColorsDark.BackgroundNormal,
    layerInformation = PhotonColors.Blue50,
    actionWarning = PhotonColors.Yellow40A41,
    actionCritical = PhotonColors.Pink70A69,
    actionInformation = PhotonColors.Blue60,
    formDefault = CustomColorsDark.BackgroundPlate,
    textOnColorPrimary = CustomColorsDark.IconAccent,
    iconPrimaryInactive = PhotonColors.LightGrey05A60,
    iconActive = CustomColorsDark.BackgroundPlate,
    iconOnColor = PhotonColors.LightGrey05,
    iconOnColorDisabled = PhotonColors.LightGrey05A40,
    iconActionPrimary = PhotonColors.LightGrey05,
    borderAccent = CustomColorsDark.BackgroundPlate,
    ripple = PhotonColors.White,
    tabActive = CustomColorsDark.ControlsTabSecondarySelected,
    tabInactive = CustomColorsDark.ControlsTabNormal,
    information = PhotonColors.Blue30,
    surfaceDimVariant = CustomColorsDark.ControlsButtonSecondaryNormal,// в трех точках плашки
    success = PhotonColors.Green50,
    iconNormal = CustomColorsDark.IconNormal,
    iconInverse = CustomColorsDark.IconInverse,
    iconSecondary = CustomColorsDark.IconSecondary,
    textNormal = CustomColorsDark.TextNormal,
    textSecondary = CustomColorsDark.TextSecondary,
    textInverse = CustomColorsDark.TextInverse,
    controlsInputFocusFill = CustomColorsDark.ControlsInputFocusFill,
    controlsButtonSecondaryNormal = CustomColorsDark.ControlsButtonSecondaryNormal,
    controlsButtonPrimaryNormal = CustomColorsDark.ControlsButtonPrimaryNormal,
    backgroundPlate = CustomColorsDark.BackgroundPlate
)

val lightColorPalette = AcornColors(
    layer2 = PhotonColors.White,
    layer3 = CustomColorsLight.BackgroundNormal,
    layerAccent = PhotonColors.Ink20,
    layerAccentNonOpaque = PhotonColors.Violet70A12,
    layerGradientStart = PhotonColors.Violet70,
    layerGradientEnd = PhotonColors.Violet60,
    layerWarning = PhotonColors.Yellow20,
    layerCritical = PhotonColors.Red10,
    layerInformation = PhotonColors.Blue50A44,
    actionWarning = PhotonColors.Yellow60A40,
    actionCritical = PhotonColors.Red30,
    actionInformation = PhotonColors.Blue50,
    formDefault = PhotonColors.DarkGrey90,
    textOnColorPrimary = CustomColorsDark.IconAccent,
    iconPrimaryInactive = PhotonColors.DarkGrey90A60,
    iconActive = PhotonColors.Ink20,
    iconOnColor = PhotonColors.LightGrey05,
    iconOnColorDisabled = PhotonColors.LightGrey05A40,
    iconActionPrimary = PhotonColors.LightGrey05,
    borderAccent = PhotonColors.Ink20,
    ripple = PhotonColors.Black,
    tabActive = CustomColorsLight.ControlsTabSecondarySelected,
    tabInactive = CustomColorsLight.ControlsTabNormal,
    information = PhotonColors.Blue60,
    surfaceDimVariant = CustomColorsLight.ControlsButtonSecondaryNormal,
    success = PhotonColors.Green80,
    iconNormal = CustomColorsLight.IconNormal,
    iconInverse = CustomColorsLight.IconInverse,
    iconSecondary = CustomColorsLight.IconSecondary,
    textNormal = CustomColorsLight.TextNormal,
    textSecondary = CustomColorsLight.TextSecondary,
    textInverse = CustomColorsLight.TextInverse,
    controlsInputFocusFill = CustomColorsLight.ControlsInputFocusFill,
    controlsButtonSecondaryNormal = CustomColorsLight.ControlsButtonSecondaryNormal,
    controlsButtonPrimaryNormal = CustomColorsLight.ControlsButtonPrimaryNormal,
    backgroundPlate = CustomColorsLight.BackgroundPlate
)

val privateColorPalette = darkColorPalette.copy(
    layer2 = PhotonColors.Violet90,
    layer3 = PhotonColors.Ink90,
    tabActive = PhotonColors.Purple60,
    tabInactive = PhotonColors.Ink90,
    surfaceDimVariant = PhotonColors.Ink90,
)

@Suppress("LongParameterList")
private fun buildColorScheme(
    primary: Color,
    primaryContainer: Color,
    inversePrimary: Color,
    secondary: Color,
    secondaryContainer: Color,
    tertiary: Color,
    tertiaryContainer: Color,
    surface: Color,
    onSurface: Color,
    surfaceTint: Color,
    inverseSurface: Color,
    inverseOnSurface: Color,
    error: Color,
    errorContainer: Color,
    outline: Color,
    outlineVariant: Color,
    scrim: Color,
    surfaceBright: Color,
    surfaceDim: Color,
    surfaceContainer: Color,
    surfaceContainerHigh: Color,
    surfaceContainerHighest: Color,
    surfaceContainerLow: Color,
    surfaceContainerLowest: Color,
): ColorScheme = ColorScheme(
    primary = primary,
    onPrimary = inverseOnSurface,
    primaryContainer = primaryContainer,
    onPrimaryContainer = onSurface,
    inversePrimary = inversePrimary,
    secondary = secondary,
    onSecondary = inverseOnSurface,
    secondaryContainer = secondaryContainer,
    onSecondaryContainer = onSurface,
    tertiary = tertiary,
    onTertiary = inverseOnSurface,
    tertiaryContainer = tertiaryContainer,
    onTertiaryContainer = onSurface,
    background = surface,
    onBackground = onSurface,
    surface = surface,
    onSurface = onSurface,
    surfaceVariant = surfaceContainerHighest,
    onSurfaceVariant = secondary,
    surfaceTint = surfaceTint,
    inverseSurface = inverseSurface,
    inverseOnSurface = inverseOnSurface,
    error = error,
    onError = inverseOnSurface,
    errorContainer = errorContainer,
    onErrorContainer = onSurface,
    outline = outline,
    outlineVariant = outlineVariant,
    scrim = scrim,
    surfaceBright = surfaceBright,
    surfaceDim = surfaceDim,
    surfaceContainer = surfaceContainer,
    surfaceContainerHigh = surfaceContainerHigh,
    surfaceContainerHighest = surfaceContainerHighest,
    surfaceContainerLow = surfaceContainerLow,
    surfaceContainerLowest = surfaceContainerLowest,
    primaryFixed = PhotonColors.Violet05,
    primaryFixedDim = primaryContainer,
    onPrimaryFixed = PhotonColors.DarkGrey90,
    onPrimaryFixedVariant = inverseOnSurface,
    secondaryFixed = secondaryContainer,
    secondaryFixedDim = secondaryContainer,
    onSecondaryFixed = onSurface,
    onSecondaryFixedVariant = inverseOnSurface,
    tertiaryFixed = tertiaryContainer,
    tertiaryFixedDim = tertiaryContainer,
    onTertiaryFixed = onSurface,
    onTertiaryFixedVariant = inverseOnSurface,
)

/**
 * Returns a dark Material color scheme mapped from Acorn.
 */
fun acornDarkColorScheme(): ColorScheme = buildColorScheme(
    primary = PhotonColors.Violet10,
    primaryContainer = PhotonColors.Violet80,
    inversePrimary = PhotonColors.Violet70,
    secondary = PhotonColors.LightGrey40,
    secondaryContainer = Color(0xFF4B3974),
    tertiary = PhotonColors.Violet20,
    tertiaryContainer = PhotonColors.Pink80,
    surface = PhotonColors.DarkGrey60,
    onSurface = PhotonColors.LightGrey05,
    surfaceTint = PhotonColors.LightGrey05A34,
    inverseSurface = PhotonColors.LightGrey40,
    inverseOnSurface = PhotonColors.DarkGrey90,
    error = PhotonColors.Red20,
    errorContainer = PhotonColors.Red80,
    outline = PhotonColors.LightGrey80,
    outlineVariant = PhotonColors.DarkGrey05,
    scrim = PhotonColors.DarkGrey90A95,
    surfaceBright = PhotonColors.DarkGrey40,
    surfaceDim = PhotonColors.DarkGrey80,
    surfaceContainer = PhotonColors.DarkGrey60,
    surfaceContainerHigh = PhotonColors.DarkGrey50,
    surfaceContainerHighest = PhotonColors.DarkGrey40,
    surfaceContainerLow = PhotonColors.DarkGrey70,
    surfaceContainerLowest = PhotonColors.DarkGrey80,
)

/**
 * Returns a light Material color scheme mapped from Acorn.
 */
fun acornLightColorScheme(): ColorScheme = buildColorScheme(
    primary = PhotonColors.Ink20,
    primaryContainer = PhotonColors.Violet05,
    inversePrimary = PhotonColors.Violet20,
    secondary = PhotonColors.DarkGrey05,
    secondaryContainer = Color(0xFFE6E0F5),
    tertiary = PhotonColors.Violet70,
    tertiaryContainer = PhotonColors.Pink05,
    surface = PhotonColors.LightGrey10,
    onSurface = PhotonColors.DarkGrey90,
    surfaceTint = PhotonColors.DarkGrey05A43,
    inverseSurface = PhotonColors.DarkGrey60,
    inverseOnSurface = PhotonColors.LightGrey05,
    error = PhotonColors.Red70,
    errorContainer = PhotonColors.Red05,
    outline = PhotonColors.LightGrey90,
    outlineVariant = PhotonColors.LightGrey30,
    scrim = PhotonColors.DarkGrey30A95,
    surfaceBright = PhotonColors.White,
    surfaceDim = PhotonColors.LightGrey30,
    surfaceContainer = PhotonColors.LightGrey10,
    surfaceContainerHigh = PhotonColors.LightGrey20,
    surfaceContainerHighest = PhotonColors.LightGrey30,
    surfaceContainerLow = PhotonColors.LightGrey05,
    surfaceContainerLowest = PhotonColors.White,
)

/**
 * Returns a private Material color scheme mapped from Acorn.
 */
fun acornPrivateColorScheme(): ColorScheme = buildColorScheme(
    primary = PhotonColors.Violet10,
    primaryContainer = PhotonColors.Violet80,
    inversePrimary = PhotonColors.Violet70,
    secondary = PhotonColors.LightGrey40,
    secondaryContainer = Color(0xFF4B3974),
    tertiary = PhotonColors.Violet20,
    tertiaryContainer = PhotonColors.Pink80,
    surface = Color(0xFF342B4A),
    onSurface = PhotonColors.LightGrey05,
    surfaceTint = PhotonColors.Violet60,
    inverseSurface = PhotonColors.LightGrey40,
    inverseOnSurface = PhotonColors.DarkGrey90,
    error = PhotonColors.Red20,
    errorContainer = PhotonColors.Red80,
    outline = PhotonColors.LightGrey80,
    outlineVariant = PhotonColors.DarkGrey05,
    scrim = PhotonColors.DarkGrey90A95,
    surfaceBright = Color(0xFF413857),
    surfaceDim = PhotonColors.Ink90,
    surfaceContainer = Color(0xFF342B4A),
    surfaceContainerHigh = Color(0xFF3B3251),
    surfaceContainerHighest = Color(0xFF413857),
    surfaceContainerLow = Color(0xFF281C3D),
    surfaceContainerLowest = PhotonColors.Ink90,
)

// M3 color scheme extensions

/**
 * @see AcornColors.information
 */
val ColorScheme.information: Color
    @Composable
    @ReadOnlyComposable
    get() = AcornTheme.colors.information

/**
 * @see AcornColors.surfaceDimVariant
 */
val ColorScheme.surfaceDimVariant: Color
    @Composable
    @ReadOnlyComposable
    get() = AcornTheme.colors.surfaceDimVariant

/**
 * @see AcornColors.success
 */
val ColorScheme.success: Color
    @Composable
    @ReadOnlyComposable
    get() = AcornTheme.colors.success
