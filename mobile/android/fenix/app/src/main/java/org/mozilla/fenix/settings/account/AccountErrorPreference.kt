/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.account

import android.content.Context
import android.content.res.ColorStateList
import android.graphics.Paint
import android.util.AttributeSet
import androidx.annotation.ColorRes
import androidx.annotation.DrawableRes
import androidx.annotation.StringRes
import androidx.core.content.ContextCompat
import androidx.core.view.isGone
import androidx.preference.Preference
import androidx.preference.PreferenceViewHolder
import mozilla.components.ui.icons.R as iconR
import org.mozilla.fenix.R
import org.mozilla.fenix.databinding.PreferenceAccountErrorBinding

/**
 * A preference that displays an account or sync error with an action to resolve it.
 *
 * @param context Context used to access resources.
 * @param attrs Attributes supplied by the preference layout.
 * @param defStyleAttr Default style attribute for the preference.
 */
class AccountErrorPreference
@JvmOverloads
constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = android.R.attr.preferenceStyle,
) : Preference(context, attrs, defStyleAttr) {

    /**
     * Describes the error shown by this preference, including its colors, icon, title, optional summary, and action
     * label.
     *
     * [SyncError] represents a sync problem and may include a summary. [AccountError] represents an authentication
     * problem and prompts the user to sign in.
     *
     * @property backgroundColor Resource ID of the background color.
     * @property contentColor Resource ID of the color used for the icon and text.
     * @property icon Drawable resource ID for the error icon.
     * @property title String resource ID for the error title.
     * @property summary Optional summary displayed below the title.
     * @property actionLabel String resource ID for the action label.
     */
    sealed class State(
        @ColorRes val backgroundColor: Int,
        @ColorRes val contentColor: Int,
        @DrawableRes val icon: Int,
        @StringRes val title: Int,
        open val summary: String?,
        @StringRes val actionLabel: Int,
    ) {
        /** A sync error, optionally including a summary. */
        data class SyncError(override val summary: String?) :
            State(
                backgroundColor = R.color.fx_mobile_error_container,
                contentColor = R.color.fx_mobile_on_error_container,
                icon = iconR.drawable.mozac_ic_critical_24,
                title = R.string.account_sync_error_title,
                summary = summary,
                actionLabel = R.string.account_sync_error_retry,
            )

        /** An account authentication error requiring sign-in. */
        data object AccountError :
            State(
                backgroundColor = R.color.fx_mobile_warning_container,
                contentColor = R.color.fx_mobile_on_warning_container,
                icon = iconR.drawable.mozac_ic_warning_24,
                title = R.string.account_error_title,
                summary = null,
                actionLabel = R.string.preferences_sign_in,
            )
    }

    /** The error to display. Changing it refreshes the preference. */
    var state: State = State.AccountError
        set(value) {
            field = value
            notifyChanged()
        }

    /** Called when the user clicks the card. */
    var onCardClick: (() -> Unit)? = null

    init {
        layoutResource = R.layout.preference_account_error
    }

    override fun onClick() {
        super.onClick()
        onCardClick?.invoke()
    }

    override fun onBindViewHolder(holder: PreferenceViewHolder) {
        super.onBindViewHolder(holder)
        val binding = PreferenceAccountErrorBinding.bind(holder.itemView)
        binding.root.backgroundTintList = ColorStateList.valueOf(ContextCompat.getColor(context, state.backgroundColor))
        binding.accountErrorIcon.apply {
            setImageResource(state.icon)
            setColorFilter(ContextCompat.getColor(context, state.contentColor))
        }
        binding.accountErrorTitle.apply {
            text = context.getString(state.title)
            setTextColor(ContextCompat.getColor(context, state.contentColor))
        }
        binding.accountErrorSummary.apply {
            text = state.summary
            isGone = state.summary == null
            setTextColor(ContextCompat.getColor(context, state.contentColor))
        }
        binding.accountErrorAction.apply {
            text = context.getString(state.actionLabel)
            paintFlags = paintFlags or Paint.UNDERLINE_TEXT_FLAG
            setTextColor(ContextCompat.getColor(context, state.contentColor))
        }
    }
}
