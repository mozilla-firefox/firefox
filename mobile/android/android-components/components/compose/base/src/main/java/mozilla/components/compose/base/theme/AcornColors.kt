/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:Suppress("MagicNumber")

package mozilla.components.compose.base.theme

import androidx.compose.material3.ColorScheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.ReadOnlyComposable
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.graphics.Color
import mozilla.components.ui.colors.NovaColors

/** A custom Color Palette for Mozilla Firefox for Android (Fenix). */
@Suppress("LongParameterList")
@Stable
class AcornColors(
    information: Color,
    onInformation: Color,
    informationContainer: Color,
    onInformationContainer: Color,
    success: Color,
    onSuccess: Color,
    successContainer: Color,
    onSuccessContainer: Color,
    warning: Color,
    onWarning: Color,
    warningContainer: Color,
    onWarningContainer: Color,
    surfaceDimVariant: Color,
    surfaceContainerSelected: Color,
    autofillText: Color,
    selectedText: Color,
    iconPrivate: Color,
    sheetOutline: Color,
) {
    /*
     * M3 color scheme extensions that do not have a mapped value from Acorn
     */

    /** Attention-grabbing color against surface for fills, icons, and text, indicating neutral information. */
    internal var information by mutableStateOf(information)
        private set

    /** Text and icons against information. */
    internal var onInformation by mutableStateOf(onInformation)
        private set

    /** Less prominent fill color against surface, for neutral information. */
    internal var informationContainer by mutableStateOf(informationContainer)
        private set

    /** Text and icons against information container. */
    internal var onInformationContainer by mutableStateOf(onInformationContainer)
        private set

    /** Attention-grabbing color against surface for fills, icons, and text, indicating successful information */
    internal var success by mutableStateOf(success)
        private set

    /** Text and icons against success. */
    internal var onSuccess by mutableStateOf(onSuccess)
        private set

    /** Less prominent fill color against surface, for success information. */
    internal var successContainer by mutableStateOf(successContainer)
        private set

    /** Text and icons against success container. */
    internal var onSuccessContainer by mutableStateOf(onSuccessContainer)
        private set

    /** Attention-grabbing color against surface for fills, icons, and text, indicating warning information. */
    internal var warning by mutableStateOf(warning)
        private set

    /** Text and icons against warning. */
    internal var onWarning by mutableStateOf(onWarning)
        private set

    /** Less prominent fill color against surface, for warning information. */
    internal var warningContainer by mutableStateOf(warningContainer)
        private set

    /** Text and icons against warning container. */
    internal var onWarningContainer by mutableStateOf(onWarningContainer)
        private set

    /** Slightly dimmer surface color in light theme. */
    internal var surfaceDimVariant by mutableStateOf(surfaceDimVariant)
        private set

    /** Neutral selected container color. */
    internal var surfaceContainerSelected by mutableStateOf(surfaceContainerSelected)
        private set

    /** Highlighted autofill text color. */
    internal var autofillText by mutableStateOf(autofillText)
        private set

    /** Highlighted selected text color. */
    internal var selectedText by mutableStateOf(selectedText)
        private set

    /** Private mode icon. */
    internal var iconPrivate by mutableStateOf(iconPrivate)
        private set

    /** Matches the surface color, except in OLED mode where it provides hierarchy. */
    internal var sheetOutline by mutableStateOf(sheetOutline)
        private set

    /** Updates the existing colors with the provided [AcornColors]. */
    fun update(other: AcornColors) {
        information = other.information
        onInformation = other.onInformation
        informationContainer = other.informationContainer
        onInformationContainer = other.onInformationContainer
        success = other.success
        onSuccess = other.onSuccess
        successContainer = other.successContainer
        onSuccessContainer = other.onSuccessContainer
        warning = other.warning
        onWarning = other.onWarning
        warningContainer = other.warningContainer
        onWarningContainer = other.onWarningContainer
        surfaceDimVariant = other.surfaceDimVariant
        surfaceContainerSelected = other.surfaceContainerSelected
        autofillText = other.autofillText
        selectedText = other.selectedText
        iconPrivate = other.iconPrivate
        sheetOutline = other.sheetOutline
    }

    /** Return a copy of this [AcornColors] and optionally overriding any of the provided values. */
    fun copy(
        information: Color = this.information,
        onInformation: Color = this.onInformation,
        informationContainer: Color = this.informationContainer,
        onInformationContainer: Color = this.onInformationContainer,
        success: Color = this.success,
        onSuccess: Color = this.onSuccess,
        successContainer: Color = this.successContainer,
        onSuccessContainer: Color = this.onSuccessContainer,
        warning: Color = this.warning,
        onWarning: Color = this.onWarning,
        warningContainer: Color = this.warningContainer,
        onWarningContainer: Color = this.onWarningContainer,
        surfaceDimVariant: Color = this.surfaceDimVariant,
        surfaceContainerSelected: Color = this.surfaceContainerSelected,
        autofillText: Color = this.autofillText,
        selectedText: Color = this.selectedText,
        iconPrivate: Color = this.iconPrivate,
        sheetOutline: Color = this.sheetOutline,
    ): AcornColors =
        AcornColors(
            information = information,
            onInformation = onInformation,
            informationContainer = informationContainer,
            onInformationContainer = onInformationContainer,
            success = success,
            onSuccess = onSuccess,
            successContainer = successContainer,
            onSuccessContainer = onSuccessContainer,
            warning = warning,
            onWarning = onWarning,
            warningContainer = warningContainer,
            onWarningContainer = onWarningContainer,
            surfaceDimVariant = surfaceDimVariant,
            surfaceContainerSelected = surfaceContainerSelected,
            autofillText = autofillText,
            selectedText = selectedText,
            iconPrivate = iconPrivate,
            sheetOutline = sheetOutline,
        )
}

val darkColorPalette =
    AcornColors(
        information = NovaColors.Blue30,
        onInformation = NovaColors.Gray80,
        informationContainer = NovaColors.Blue70,
        onInformationContainer = NovaColors.VioletDesaturated0,
        success = NovaColors.Green30,
        onSuccess = NovaColors.Gray80,
        successContainer = NovaColors.Green70,
        onSuccessContainer = NovaColors.VioletDesaturated0,
        warning = NovaColors.Yellow30,
        onWarning = NovaColors.Gray80,
        warningContainer = NovaColors.Yellow70,
        onWarningContainer = NovaColors.VioletDesaturated0,
        surfaceDimVariant = NovaColors.Gray80,
        surfaceContainerSelected = NovaColors.Gray55,
        autofillText = NovaColors.VioletDesaturated30A55,
        selectedText = NovaColors.Gray45A80,
        iconPrivate = NovaColors.Violet50,
        sheetOutline = NovaColors.Gray75,
    )

val lightColorPalette =
    AcornColors(
        information = NovaColors.Blue50,
        onInformation = NovaColors.White,
        informationContainer = NovaColors.Blue10,
        onInformationContainer = NovaColors.VioletDesaturated90,
        success = NovaColors.Green50,
        onSuccess = NovaColors.White,
        successContainer = NovaColors.Green10,
        onSuccessContainer = NovaColors.VioletDesaturated90,
        warning = NovaColors.Yellow50,
        onWarning = NovaColors.White,
        warningContainer = NovaColors.Yellow10,
        onWarningContainer = NovaColors.VioletDesaturated90,
        surfaceDimVariant = NovaColors.Gray10,
        surfaceContainerSelected = NovaColors.Gray25,
        autofillText = NovaColors.VioletDesaturated30,
        selectedText = NovaColors.Gray35,
        iconPrivate = NovaColors.Violet50,
        sheetOutline = NovaColors.Gray5,
    )

val privateColorPalette =
    darkColorPalette.copy(
        surfaceDimVariant = Color(0xFF11042B),
        surfaceContainerSelected = NovaColors.VioletDesaturated60,
        selectedText = NovaColors.Gray45A81,
        sheetOutline = NovaColors.VioletDesaturated90,
    )

val oledColorPalette =
    darkColorPalette.copy(
        informationContainer = NovaColors.Blue80,
        onInformationContainer = NovaColors.Gray20,
        successContainer = NovaColors.Green80,
        onSuccessContainer = NovaColors.Gray20,
        warningContainer = NovaColors.Yellow80,
        onWarningContainer = NovaColors.Gray20,
        surfaceDimVariant = NovaColors.Gray90,
        surfaceContainerSelected = NovaColors.Gray60,
        autofillText = NovaColors.VioletDesaturated60,
        selectedText = NovaColors.Gray60,
        sheetOutline = NovaColors.Gray65,
    )

/** Returns a dark Material color scheme mapped from Acorn. */
fun acornDarkColorScheme(): ColorScheme =
    darkColorScheme(
        primary = NovaColors.Violet20,
        onPrimary = NovaColors.Gray80,
        primaryContainer = NovaColors.Violet60,
        onPrimaryContainer = NovaColors.VioletDesaturated0,
        inversePrimary = NovaColors.Violet70,
        secondary = NovaColors.Gray20,
        onSecondary = NovaColors.Gray80,
        secondaryContainer = NovaColors.VioletDesaturated70,
        onSecondaryContainer = NovaColors.VioletDesaturated0,
        tertiary = NovaColors.Violet30,
        onTertiary = NovaColors.Gray80,
        tertiaryContainer = NovaColors.VioletDesaturated90,
        onTertiaryContainer = NovaColors.VioletDesaturated0,
        background = NovaColors.Gray75,
        onBackground = NovaColors.VioletDesaturated0,
        surface = NovaColors.Gray75,
        onSurface = NovaColors.VioletDesaturated0,
        surfaceVariant = NovaColors.Gray65,
        onSurfaceVariant = NovaColors.VioletDesaturated0A70,
        surfaceTint = NovaColors.Gray50,
        inverseSurface = NovaColors.Gray30,
        inverseOnSurface = NovaColors.Gray80,
        error = NovaColors.Red30,
        onError = NovaColors.Gray80,
        errorContainer = NovaColors.Red70,
        onErrorContainer = NovaColors.VioletDesaturated0,
        outline = NovaColors.Gray45,
        outlineVariant = NovaColors.Gray60,
        scrim = NovaColors.BlackA50,
        surfaceBright = NovaColors.Gray65,
        surfaceDim = NovaColors.Gray85,
        surfaceContainer = NovaColors.Gray75,
        surfaceContainerHigh = NovaColors.Gray70,
        surfaceContainerHighest = NovaColors.Gray65,
        surfaceContainerLow = NovaColors.Gray80,
        surfaceContainerLowest = NovaColors.Gray85,
    )

/** Returns a light Material color scheme mapped from Acorn. */
fun acornLightColorScheme(): ColorScheme =
    lightColorScheme(
        primary = NovaColors.Violet70,
        onPrimary = NovaColors.White,
        primaryContainer = NovaColors.Violet20,
        onPrimaryContainer = NovaColors.VioletDesaturated90,
        inversePrimary = NovaColors.Violet20,
        secondary = NovaColors.Gray50,
        onSecondary = NovaColors.White,
        secondaryContainer = NovaColors.VioletDesaturated10,
        onSecondaryContainer = NovaColors.VioletDesaturated90,
        tertiary = NovaColors.Violet50,
        onTertiary = NovaColors.White,
        tertiaryContainer = NovaColors.Violet0,
        onTertiaryContainer = NovaColors.VioletDesaturated90,
        background = NovaColors.Gray5,
        onBackground = NovaColors.VioletDesaturated90,
        surface = NovaColors.Gray5,
        onSurface = NovaColors.VioletDesaturated90,
        surfaceVariant = NovaColors.Gray15,
        onSurfaceVariant = NovaColors.VioletDesaturated90A70,
        surfaceTint = NovaColors.Gray30,
        inverseSurface = NovaColors.Gray70,
        inverseOnSurface = NovaColors.White,
        error = NovaColors.Red50,
        onError = NovaColors.White,
        errorContainer = NovaColors.Red10,
        onErrorContainer = NovaColors.VioletDesaturated90,
        outline = NovaColors.Gray45,
        outlineVariant = NovaColors.Gray15,
        scrim = NovaColors.BlackA50,
        surfaceBright = NovaColors.White,
        surfaceDim = NovaColors.Gray15,
        surfaceContainer = NovaColors.Gray5,
        surfaceContainerHigh = NovaColors.Gray10,
        surfaceContainerHighest = NovaColors.Gray15,
        surfaceContainerLow = NovaColors.Gray0,
        surfaceContainerLowest = NovaColors.White,
    )

/** Returns a private Material color scheme mapped from Acorn. */
fun acornPrivateColorScheme(): ColorScheme =
    darkColorScheme(
        primary = NovaColors.Violet20,
        onPrimary = NovaColors.Gray80,
        primaryContainer = NovaColors.Violet60,
        onPrimaryContainer = NovaColors.VioletDesaturated0,
        inversePrimary = NovaColors.Violet70,
        secondary = NovaColors.Gray20,
        onSecondary = NovaColors.Gray80,
        secondaryContainer = NovaColors.Violet70,
        onSecondaryContainer = NovaColors.VioletDesaturated0,
        tertiary = NovaColors.Violet30,
        onTertiary = NovaColors.Gray80,
        tertiaryContainer = NovaColors.VioletDesaturated90,
        onTertiaryContainer = NovaColors.VioletDesaturated0,
        background = NovaColors.VioletDesaturated90,
        onBackground = NovaColors.VioletDesaturated0,
        surface = NovaColors.VioletDesaturated90,
        onSurface = NovaColors.VioletDesaturated0,
        surfaceVariant = NovaColors.VioletDesaturated80,
        onSurfaceVariant = NovaColors.VioletDesaturated0A70,
        surfaceTint = NovaColors.VioletDesaturated60,
        inverseSurface = NovaColors.Gray30,
        inverseOnSurface = NovaColors.Gray80,
        error = NovaColors.Red30,
        onError = NovaColors.Gray80,
        errorContainer = NovaColors.Red70,
        onErrorContainer = NovaColors.VioletDesaturated0,
        outline = NovaColors.Gray45,
        outlineVariant = Color(0xFF332752),
        scrim = NovaColors.BlackA50,
        surfaceBright = NovaColors.VioletDesaturated80,
        surfaceDim = Color(0xFF0D0321),
        surfaceContainer = NovaColors.VioletDesaturated90,
        surfaceContainerHigh = Color(0xFF20163A),
        surfaceContainerHighest = NovaColors.VioletDesaturated80,
        surfaceContainerLow = Color(0xFF11042B),
        surfaceContainerLowest = Color(0xFF0D0321),
    )

/** Returns an OLED dark Material color scheme mapped from Acorn. */
fun acornOledColorScheme(): ColorScheme =
    acornDarkColorScheme()
        .copy(
            primaryContainer = NovaColors.Violet70,
            onPrimaryContainer = NovaColors.Gray20,
            secondaryContainer = NovaColors.VioletDesaturated80,
            onSecondaryContainer = NovaColors.Gray20,
            tertiaryContainer = NovaColors.Gray90,
            onTertiaryContainer = NovaColors.Gray20,
            background = NovaColors.Black,
            onBackground = NovaColors.Gray20,
            surface = NovaColors.Black,
            onSurface = NovaColors.Gray20,
            onSurfaceVariant = NovaColors.Gray40,
            errorContainer = NovaColors.Red80,
            onErrorContainer = NovaColors.Gray20,
            outlineVariant = NovaColors.Gray65,
            surfaceBright = NovaColors.Gray75,
            surfaceDim = NovaColors.Black,
            surfaceContainer = NovaColors.Black,
            surfaceContainerHigh = NovaColors.Gray90,
            surfaceContainerHighest = NovaColors.Gray75,
            surfaceContainerLow = NovaColors.Gray90,
            surfaceContainerLowest = NovaColors.Black,
        )

// M3 color scheme extensions

/** @see AcornColors.information */
val ColorScheme.information: Color
    @Composable @ReadOnlyComposable get() = AcornTheme.colors.information

/** @see AcornColors.onInformation */
val ColorScheme.onInformation: Color
    @Composable @ReadOnlyComposable get() = AcornTheme.colors.onInformation

/** @see AcornColors.informationContainer */
val ColorScheme.informationContainer: Color
    @Composable @ReadOnlyComposable get() = AcornTheme.colors.informationContainer

/** @see AcornColors.onInformationContainer */
val ColorScheme.onInformationContainer: Color
    @Composable @ReadOnlyComposable get() = AcornTheme.colors.onInformationContainer

/** @see AcornColors.success */
val ColorScheme.success: Color
    @Composable @ReadOnlyComposable get() = AcornTheme.colors.success

/** @see AcornColors.onSuccess */
val ColorScheme.onSuccess: Color
    @Composable @ReadOnlyComposable get() = AcornTheme.colors.onSuccess

/** @see AcornColors.successContainer */
val ColorScheme.successContainer: Color
    @Composable @ReadOnlyComposable get() = AcornTheme.colors.successContainer

/** @see AcornColors.onSuccessContainer */
val ColorScheme.onSuccessContainer: Color
    @Composable @ReadOnlyComposable get() = AcornTheme.colors.onSuccessContainer

/** @see AcornColors.warning */
val ColorScheme.warning: Color
    @Composable @ReadOnlyComposable get() = AcornTheme.colors.warning

/** @see AcornColors.onWarning */
val ColorScheme.onWarning: Color
    @Composable @ReadOnlyComposable get() = AcornTheme.colors.onWarning

/** @see AcornColors.warningContainer */
val ColorScheme.warningContainer: Color
    @Composable @ReadOnlyComposable get() = AcornTheme.colors.warningContainer

/** @see AcornColors.onWarningContainer */
val ColorScheme.onWarningContainer: Color
    @Composable @ReadOnlyComposable get() = AcornTheme.colors.onWarningContainer

/** @see AcornColors.surfaceDimVariant */
val ColorScheme.surfaceDimVariant: Color
    @Composable @ReadOnlyComposable get() = AcornTheme.colors.surfaceDimVariant

/** @see AcornColors.surfaceContainerSelected */
val ColorScheme.surfaceContainerSelected: Color
    @Composable @ReadOnlyComposable get() = AcornTheme.colors.surfaceContainerSelected

/** @see AcornColors.sheetOutline */
val ColorScheme.sheetOutline: Color
    @Composable @ReadOnlyComposable get() = AcornTheme.colors.sheetOutline

/** @see AcornColors.autofillText */
val ColorScheme.autofillText: Color
    @Composable @ReadOnlyComposable get() = AcornTheme.colors.autofillText

/** @see AcornColors.selectedText */
val ColorScheme.selectedText: Color
    @Composable @ReadOnlyComposable get() = AcornTheme.colors.selectedText

/** @see AcornColors.iconPrivate */
val ColorScheme.iconPrivate: Color
    @Composable @ReadOnlyComposable get() = AcornTheme.colors.iconPrivate
