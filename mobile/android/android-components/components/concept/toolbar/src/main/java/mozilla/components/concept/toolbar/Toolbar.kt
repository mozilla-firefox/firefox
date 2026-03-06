/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.toolbar

import android.graphics.drawable.Drawable
import android.text.style.ForegroundColorSpan
import android.view.View
import android.view.View.NO_ID
import android.view.ViewGroup
import android.widget.ImageButton
import android.widget.ImageView
import androidx.annotation.ColorInt
import androidx.annotation.ColorRes
import androidx.annotation.Dimension
import androidx.annotation.Dimension.Companion.DP
import androidx.annotation.DrawableRes
import androidx.appcompat.content.res.AppCompatResources
import androidx.appcompat.widget.AppCompatImageButton
import androidx.appcompat.widget.AppCompatImageView
import mozilla.components.support.base.android.Padding
import mozilla.components.support.ktx.android.content.res.resolveAttribute
import mozilla.components.support.ktx.android.view.setPadding
import java.lang.ref.WeakReference

/**
 * Interface to be implemented by components that provide browser toolbar functionality.
 */
@Suppress("TooManyFunctions")
interface Toolbar : ScrollableToolbar {
    /**
     * Sets/Gets the title to be displayed on the toolbar.
     */
    var title: String

    /**
     * Sets/Gets the URL to be displayed on the toolbar.
     */
    var url: CharSequence

    /**
     * Sets/gets private mode.
     *
     * In private mode the IME should not update any personalized data such as typing history and personalized language
     * model based on what the user typed.
     */
    var private: Boolean

    /**
     * Sets/Gets the site info icon to be displayed on the toolbar.
     */
    var siteInfo: SiteInfo

    /**
     * Sets/Gets the highlight icon to be displayed on the toolbar.
     */
    var highlight: Highlight

    /**
     * Sets/Gets the site tracking protection state to be displayed on the toolbar.
     */
    var siteTrackingProtection: SiteTrackingProtection

    /**
     * Displays the currently used search terms as part of this Toolbar.
     *
     * @param searchTerms the search terms used by the current session
     */
    fun setSearchTerms(searchTerms: String)

    /**
     * Displays the given loading progress. Expects values in the range [0, 100].
     */
    fun displayProgress(progress: Int)

    /**
     * Should be called by an activity when the user pressed the back key of the device.
     *
     * @return Returns true if the back press event was handled and should not be propagated further.
     */
    fun onBackPressed(): Boolean

    /**
     * Should be called by the host activity when it enters the stop state.
     */
    fun onStop()

    /**
     * Registers the given function to be invoked when the user selected a new URL i.e. is done
     * editing.
     *
     * If the function returns `true` then the toolbar will automatically switch to "display mode". Otherwise it
     * remains in "edit mode".
     *
     * @param listener the listener function
     */
    fun setOnUrlCommitListener(listener: (String) -> Boolean)

    /**
     * Registers the given function to be invoked when users changes text in the toolbar.
     *
     * @param filter A function which will perform autocompletion and send results to [AutocompleteDelegate].
     */
    fun setAutocompleteListener(filter: suspend (String, AutocompleteDelegate) -> Unit)

    /**
     * Attempt to restart the autocomplete functionality with the current user input.
     */
    fun refreshAutocomplete() = Unit

    /**
     * Adds an action to be displayed on the right side of the toolbar in display mode.
     *
     * Related:
     * https://developer.mozilla.org/en-US/Add-ons/WebExtensions/user_interface/Browser_action
     */
    fun addBrowserAction(action: Action)

    /**
     * Removes a previously added browser action (see [addBrowserAction]). If the the provided
     * actions was never added, this method has no effect.
     *
     * @param action the action to remove.
     */
    fun removeBrowserAction(action: Action)

    /**
     * Removes a previously added page action (see [addBrowserAction]). If the the provided
     * actions was never added, this method has no effect.
     *
     * @param action the action to remove.
     */
    fun removePageAction(action: Action)

    /**
     * Removes a previously added navigation action (see [addNavigationAction]). If the the provided
     * actions was never added, this method has no effect.
     *
     * @param action the action to remove.
     */
    fun removeNavigationAction(action: Action)

    /**
     * Declare that the actions (navigation actions, browser actions, page actions) have changed and
     * should be updated if needed.
     */
    fun invalidateActions()

    /**
     * Adds an action to be displayed on the right side of the URL in display mode.
     *
     * Related:
     * https://developer.mozilla.org/en-US/Add-ons/WebExtensions/user_interface/Page_actions
     */
    fun addPageAction(action: Action)

    /**
     * Adds an action to be displayed on the far left side of the URL in display mode.
     */
    fun addNavigationAction(action: Action)

    /**
     * Adds an action to be displayed at the start of the URL in edit mode.
     */
    fun addEditActionStart(action: Action)

    /**
     * Adds an action to be displayed at the end of the URL in edit mode.
     */
    fun addEditActionEnd(action: Action)

    /**
     * Removes an action at the end of the URL in edit mode.
     */
    fun removeEditActionEnd(action: Action)

    /**
     * Hides the menu button in display mode.
     */
    fun hideMenuButton()

    /**
     * Shows the menu button in display mode.
     */
    fun showMenuButton()

    /**
     * Sets the horizontal padding in display mode.
     */
    fun setDisplayHorizontalPadding(horizontalPadding: Int)

    /**
     * Casts this toolbar to an Android View object.
     */
    fun asView(): View = this as View

    /**
     * Registers the given listener to be invoked when the user edits the URL.
     */
    fun setOnEditListener(listener: OnEditListener)

    /**
     * Switches to URL displaying mode (from editing mode) if supported by the toolbar implementation.
     */
    fun displayMode()

    /**
     * Switches to URL editing mode (from display mode) if supported by the toolbar implementation,
     * and focuses the URL input field based on the cursor selection.
     *
     * @param cursorPlacement Where the cursor should be set after focusing on the URL input field.
     */
    fun editMode(cursorPlacement: CursorPlacement = CursorPlacement.ALL)

    /**
     * Dismisses the display toolbar popup menu
     */
    fun dismissMenu()

    /**
     * Listener to be invoked when the user edits the URL.
     */
    interface OnEditListener {
        /**
         * Fired when the toolbar switches to edit mode.
         */
        fun onStartEditing() = Unit

        /**
         * Fired when the user presses the back button while in edit mode.
         */
        fun onCancelEditing(): Boolean = true

        /**
         * Fired when the toolbar switches back to display mode.
         */
        fun onStopEditing() = Unit

        /**
         * Fired whenever the user changes the text in the address bar.
         */
        fun onTextChanged(text: String) = Unit

        /**
         * Fired when user clears input by tapping the clear input button.
         */
        fun onInputCleared() = Unit
    }

    /**
     * Generic interface for actions to be added to the toolbar.
     */
    interface Action {
        val visible: () -> Boolean
            get() = { true }

        val autoHide: () -> Boolean
            get() = { false }

        val weight: () -> Int
            get() = { -1 }

        fun createView(parent: ViewGroup): View

        fun bind(view: View)
    }

    /**
     * An action button to be added to the toolbar.
     *
     * @param imageDrawable The drawable to be shown.
     * @param contentDescription The content description to use.
     * @param visible Lambda that returns true or false to indicate whether this button should be shown.
     * @param autoHide Lambda that returns true or false to indicate whether this button should auto hide.
     * @param weight Lambda that returns an integer to indicate weight of an action. The lesser the weight,
     * the closer it is to the url. A default weight -1 indicates, the position is not cared for
     * and action will be appended at the end.
     * @param padding A optional custom padding.
     * @param iconTintColorResource Optional ID of color resource to tint the icon.
     * @param longClickListener Callback that will be invoked whenever the button is long-pressed.
     * @param listener Callback that will be invoked whenever the button is pressed
     */
    open class ActionButton(
        val imageDrawable: Drawable? = null,
        val contentDescription: String,
        override val visible: () -> Boolean = { true },
        override val autoHide: () -> Boolean = { false },
        override val weight: () -> Int = { -1 },
        private val background: Int = 0,
        private val padding: Padding? = null,
        @param:ColorRes val iconTintColorResource: Int = ViewGroup.NO_ID,
        private val longClickListener: (() -> Unit)? = null,
        private val listener: () -> Unit,
    ) : Action {
        private var view: WeakReference<AppCompatImageButton>? = null

        override fun createView(parent: ViewGroup): View =
            AppCompatImageButton(parent.context).also { imageButton ->
                view = WeakReference(imageButton)

                imageButton.setImageDrawable(imageDrawable)
                imageButton.contentDescription = contentDescription
                //imageButton.setTintResource(iconTintColorResource)
                imageButton.setOnClickListener { listener.invoke() }
                imageButton.setOnLongClickListener {
                    longClickListener?.invoke()
                    true
                }
                imageButton.isLongClickable = longClickListener != null

                val backgroundResource = if (background == 0) {
                    parent.context.theme.resolveAttribute(android.R.attr.selectableItemBackgroundBorderless)
                } else {
                    background
                }

                imageButton.setBackgroundResource(backgroundResource)
                padding?.let { imageButton.setPadding(it) }
            }

        /**
         * Changes the content description and the tint colour of the view.
         *
         * @param contentDescription The content description to use.
         * @param tintColorResource ID of color resource to tint the icon.
         */
        fun updateView(
            contentDescription: String? = null,
            @ColorRes tintColorResource: Int = ViewGroup.NO_ID,
        ) {
            view?.get()?.let {
                it.contentDescription = contentDescription
                //it.setTintResource(tintColorResource)
            }
        }

        override fun bind(view: View) = Unit
    }

    /**
     * An action button with two states, selected and unselected. When the button is pressed, the
     * state changes automatically.
     *
     * @param imageDrawable The drawable to be shown if the button is in unselected state.
     * @param imageSelectedDrawable The  drawable to be shown if the button is in selected state.
     * @param contentDescription The content description to use if the button is in unselected state.
     * @param contentDescriptionSelected The content description to use if the button is in selected state.
     * @param visible Lambda that returns true or false to indicate whether this button should be shown.
     * @param weight Lambda that returns an integer to indicate weight of an action. The lesser the weight,
     * the closer it is to the url. A default weight -1 indicates, the position is not cared for
     * and action will be appended at the end.
     * @param selected Sets whether this button should be selected initially.
     * @param padding A optional custom padding.
     * @param listener Callback that will be invoked whenever the checked state changes.
     */
    open class ActionToggleButton(
        internal val imageDrawable: Drawable,
        internal val imageSelectedDrawable: Drawable,
        private val contentDescription: String,
        private val contentDescriptionSelected: String,
        override val visible: () -> Boolean = { true },
        override val weight: () -> Int = { -1 },
        private var selected: Boolean = false,
        @param:DrawableRes private val background: Int = 0,
        private val padding: Padding? = null,
        private val listener: (Boolean) -> Unit,
    ) : Action {
        private var view: WeakReference<ImageButton>? = null

        override fun createView(parent: ViewGroup): View = AppCompatImageButton(parent.context).also { imageButton ->
            view = WeakReference(imageButton)

            imageButton.scaleType = ImageView.ScaleType.CENTER
            imageButton.setOnClickListener { toggle() }
            imageButton.isSelected = selected

            updateViewState()

            val backgroundResource = if (background == 0) {
                parent.context.theme.resolveAttribute(android.R.attr.selectableItemBackgroundBorderless)
            } else {
                background
            }

            imageButton.setBackgroundResource(backgroundResource)
            padding?.let { imageButton.setPadding(it) }
        }

        /**
         * Changes the selected state of the action to the inverse of its current state.
         *
         * @param notifyListener If true (default) the listener will be notified about the state change.
         */
        fun toggle(notifyListener: Boolean = true) {
            setSelected(!selected, notifyListener)
        }

        /**
         * Changes the selected state of the action.
         *
         * @param selected The new selected state
         * @param notifyListener If true (default) the listener will be notified about a state change.
         */
        fun setSelected(selected: Boolean, notifyListener: Boolean = true) {
            if (this.selected == selected) {
                // Nothing to do here.
                return
            }

            this.selected = selected
            updateViewState()

            if (notifyListener) {
                listener.invoke(selected)
            }
        }

        /**
         * Returns the current selected state of the action.
         */
        fun isSelected() = selected

        private fun updateViewState() {
            view?.get()?.let {
                it.isSelected = selected

                if (selected) {
                    it.setImageDrawable(imageSelectedDrawable)
                    it.contentDescription = contentDescriptionSelected
                } else {
                    it.setImageDrawable(imageDrawable)
                    it.contentDescription = contentDescription
                }
            }
        }

        override fun bind(view: View) = Unit
    }

    /**
     * An "empty" action with a desired width to be used as "placeholder".
     *
     * @param desiredWidth The desired width in density independent pixels for this action.
     * @param padding A optional custom padding.
     */
    open class ActionSpace(
        @param:Dimension(unit = DP) private val desiredWidth: Int,
        private val padding: Padding? = null,
    ) : Action {
        override fun createView(parent: ViewGroup): View = View(parent.context).apply {
            minimumWidth = desiredWidth
            padding?.let { this.setPadding(it) }
        }

        override fun bind(view: View) = Unit
    }

    /**
     * An action that just shows a static, non-clickable image.
     *
     * @param imageDrawable The drawable to be shown.
     * @param contentDescription Optional content description to be used. If no content description
     *                           is provided then this view will be treated as not important for
     *                           accessibility.
     * @param padding A optional custom padding.
     */
    open class ActionImage(
        private val imageDrawable: Drawable,
        private val contentDescription: String? = null,
        private val padding: Padding? = null,
    ) : Action {

        override fun createView(parent: ViewGroup): View = AppCompatImageView(parent.context).also { image ->
            image.minimumWidth = imageDrawable.intrinsicWidth
            image.setImageDrawable(imageDrawable)

            image.contentDescription = contentDescription
            image.importantForAccessibility = if (contentDescription.isNullOrEmpty()) {
                View.IMPORTANT_FOR_ACCESSIBILITY_NO
            } else {
                View.IMPORTANT_FOR_ACCESSIBILITY_AUTO
            }
            padding?.let { pd -> image.setPadding(pd) }
        }

        override fun bind(view: View) = Unit
    }

    /**
     * An enum for the different site info icons that can be displayed.
     */
    enum class SiteInfo {
        INSECURE,
        SECURE,
        LOCAL_PDF,
    }

    /**
     * Indicates which tracking protection status a site has.
     */
    enum class SiteTrackingProtection {
        /**
         * The site has tracking protection enabled, but none trackers have been blocked or detected.
         */
        ON_NO_TRACKERS_BLOCKED,

        /**
         * The site has tracking protection enabled, and trackers have been blocked or detected.
         */
        ON_TRACKERS_BLOCKED,

        /**
         * Tracking protection has been disabled for a specific site.
         */
        OFF_FOR_A_SITE,

        /**
         * Tracking protection has been disabled for all sites.
         */
        OFF_GLOBALLY,
    }

    /**
     * Indicates the reason why a highlight icon is shown or hidden.
     */
    enum class Highlight {
        /**
         * The site has changed its permissions from their default values.
         */
        PERMISSIONS_CHANGED,

        /**
         * The site does not show a dot indicator.
         */
        NONE,
    }

    /**
     * Indicates where the cursor should be set after focusing on the URL input field.
     */
    enum class CursorPlacement {
        /**
         * All of the text in the input field should be selected.
         */
        ALL,

        /**
         * No text should be selected and the cursor should be placed at the end of the text.
         */
        END,
    }

    /**
     * Registrable domain foreground color span.
     *
     * This simple class extension is used so that we can filter for it elsewhere.
     */
    class RegistrableDomainColorSpan(
        @ColorInt color: Int,
    ) : ForegroundColorSpan(color)
}

private fun AppCompatImageButton.setTintResource(
    @ColorRes tintColorResource: Int,
) {
    if (tintColorResource != NO_ID) {
        imageTintList = AppCompatResources.getColorStateList(context, tintColorResource)
    }
}
