/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.menu.item

import android.view.View
import androidx.annotation.ColorRes
import androidx.annotation.DrawableRes
import androidx.annotation.VisibleForTesting
import androidx.appcompat.content.res.AppCompatResources
import androidx.appcompat.widget.AppCompatCheckBox
import androidx.core.content.ContextCompat
import mozilla.components.browser.menu.BrowserMenu
import mozilla.components.browser.menu.R
import mozilla.components.support.ktx.android.util.dpToPx

/**
 * A browser menu item with image and label and a custom checkbox.
 *
 * @param imageResource ID of a drawable resource to be shown as icon.
 * @param iconTintColorResource Optional ID of color resource to tint the icon.
 * @param label The visible label of this menu item.
 * @param textColorResource Optional ID of color resource to tint the text.
 * @param enabled Sets the enabled status for the view. By default, it is true.
 * @param labelListener Callback to be invoked when this menu item is clicked.
 * @param primaryStateIconResource ID of a drawable resource for checkbox drawable in primary state.
 * @param secondaryStateIconResource ID of a drawable resource for checkbox drawable in secondary state.
 * @param isCollapsingMenuLimit Whether this menu item can serve as the limit of a collapsing menu.
 * @param isSticky whether this item menu should not be scrolled offscreen (downwards or upwards depending on the menu
 *   position).
 * @param iconTintColorResource Optional ID of color resource to tint the checkbox drawable.
 * @param primaryLabel The visible label of the checkbox in primary state.
 * @param secondaryLabel The visible label of this menu item in secondary state.
 * @param isInPrimaryState Lambda to return true/false to indicate checkbox primary or secondary state.
 * @param onCheckedChangedListener Callback to be invoked when checkbox is clicked.
 */
class BrowserMenuImageTextCheckboxButton(
    @DrawableRes imageResource: Int,
    private val label: String,
    @ColorRes iconTintColorResource: Int = NO_ID,
    @param:ColorRes internal val textColorResource: Int = NO_ID,
    enabled: Boolean = true,
    @get:VisibleForTesting internal val labelListener: () -> Unit,
    @param:DrawableRes val primaryStateIconResource: Int,
    @param:DrawableRes val secondaryStateIconResource: Int,
    @param:ColorRes internal val tintColorResource: Int = NO_ID,
    private val primaryLabel: String,
    private val secondaryLabel: String,
    override val isCollapsingMenuLimit: Boolean = false,
    override val isSticky: Boolean = false,
    val isInPrimaryState: () -> Boolean = { true },
    private val onCheckedChangedListener: (Boolean) -> Unit,
) :
    BrowserMenuImageText(
        label,
        imageResource,
        iconTintColorResource,
        textColorResource,
        enabled,
        isCollapsingMenuLimit,
        isSticky,
        labelListener,
    ) {
    override var visible: () -> Boolean = { true }

    override fun getLayoutResource(): Int = R.layout.mozac_browser_menu_item_image_text_checkbox_button

    override fun bind(menu: BrowserMenu, view: View) {
        super.bind(menu, view)

        view.findViewById<View>(R.id.accessibilityRegion).apply {
            setOnClickListener { labelListener.invoke() }
            contentDescription = label
        }

        bindCheckbox(menu, view.findViewById(R.id.checkbox) as AppCompatCheckBox)
    }

    private fun bindCheckbox(menu: BrowserMenu, button: AppCompatCheckBox) {
        val buttonText = if (isInPrimaryState()) primaryLabel else secondaryLabel
        val tintColor = ContextCompat.getColor(button.context, tintColorResource)
        val buttonDrawableIcon =
            if (isInPrimaryState()) {
                AppCompatResources.getDrawable(button.context, primaryStateIconResource)
            } else {
                AppCompatResources.getDrawable(button.context, secondaryStateIconResource)
            }
        buttonDrawableIcon?.setTint(tintColor)
        val displayMetrics = button.context.resources.displayMetrics

        buttonDrawableIcon?.setBounds(
            0,
            0,
            CHECKBOX_ICON_SIZE_DP.dpToPx(displayMetrics),
            CHECKBOX_ICON_SIZE_DP.dpToPx(displayMetrics),
        )

        button.apply {
            text = buttonText
            setTextColor(tintColor)
            setCompoundDrawables(buttonDrawableIcon, null, null, null)
            isChecked = !isInPrimaryState()

            setOnCheckedChangeListener { _, isChecked ->
                onCheckedChangedListener(isChecked)
                menu.dismiss()
            }
        }
    }

    companion object {
        private const val CHECKBOX_ICON_SIZE_DP = 19
    }
}
