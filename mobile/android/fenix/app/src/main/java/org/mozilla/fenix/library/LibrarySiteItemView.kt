/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.library

import android.content.Context
import android.util.AttributeSet
import android.view.LayoutInflater
import android.widget.ImageButton
import android.widget.ImageView
import android.widget.TextView
import androidx.constraintlayout.widget.ConstraintLayout
import androidx.core.content.res.use
import androidx.core.view.isVisible
import mozilla.components.support.ktx.android.view.pixelSizeFor
import org.mozilla.fenix.R
import org.mozilla.fenix.databinding.LibrarySiteItemBinding
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.loadIntoView
import org.mozilla.fenix.selection.SelectionHolder
import org.mozilla.fenix.selection.SelectionInteractor

class LibrarySiteItemView
@JvmOverloads
constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0,
    defStyleRes: Int = 0,
) : ConstraintLayout(context, attrs, defStyleAttr, defStyleRes) {

    private val binding =
        LibrarySiteItemBinding.inflate(
            LayoutInflater.from(context),
            this,
        )

    init {
        minHeight = pixelSizeFor(R.dimen.library_item_height)
        val selectableBackground =
            context.obtainStyledAttributes(intArrayOf(android.R.attr.selectableItemBackground)).use {
                it.getResourceId(0, 0)
            }
        if (background == null && selectableBackground != 0) {
            setBackgroundResource(selectableBackground)
        }
    }

    val titleView: TextView
        get() = binding.title

    val urlView: TextView
        get() = binding.url

    val iconView: ImageView
        get() = binding.favicon

    val overflowView: ImageButton
        get() = binding.overflowMenu

    /** Change visibility of parts of this view based on what type of item is being represented. */
    fun displayAs(mode: ItemType) {
        urlView.isVisible = mode == ItemType.SITE
    }

    /** Changes the icon to show a check mark if [isSelected] */
    fun changeSelected(isSelected: Boolean) {
        binding.icon.displayedChild = if (isSelected) 1 else 0
    }

    fun loadFavicon(url: String) {
        context.components.core.icons.loadIntoView(iconView, url)
    }

    fun <T> setSelectionInteractor(item: T, holder: SelectionHolder<T>, interactor: SelectionInteractor<T>) {
        setOnClickListener {
            val selected = holder.selectedItems
            when {
                selected.isEmpty() -> interactor.open(item)
                item in selected -> interactor.deselect(item)
                else -> interactor.select(item)
            }
        }

        setOnLongClickListener {
            if (holder.selectedItems.isEmpty()) {
                interactor.select(item)
                true
            } else {
                false
            }
        }

        iconView.setOnClickListener {
            if (item in holder.selectedItems) {
                interactor.deselect(item)
            } else {
                interactor.select(item)
            }
        }
    }

    enum class ItemType {
        SITE,
        FOLDER,
    }
}
