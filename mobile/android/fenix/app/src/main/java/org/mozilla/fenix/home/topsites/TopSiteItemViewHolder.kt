/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.topsites

import android.annotation.SuppressLint
import android.content.res.ColorStateList
import android.view.MotionEvent
import android.view.View
import androidx.appcompat.content.res.AppCompatResources.getDrawable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.toArgb
import androidx.core.content.ContextCompat
import androidx.core.view.isVisible
import androidx.core.widget.TextViewCompat
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.Dispatchers.IO
import kotlinx.coroutines.Dispatchers.Main
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import mozilla.components.browser.menu.BrowserMenu
import mozilla.components.feature.top.sites.TopSite
import mozilla.components.lib.state.ext.flowScoped
import mozilla.components.support.ktx.android.content.getColorFromAttr
import org.mozilla.fenix.R
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.databinding.TopSiteItemBinding
import org.mozilla.fenix.ext.bitmapForUrl
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.isSystemInDarkTheme
import org.mozilla.fenix.ext.loadIntoView
import org.mozilla.fenix.home.sessioncontrol.TopSiteInteractor
import org.mozilla.fenix.settings.SupportUtils
import org.mozilla.fenix.utils.view.ViewHolder

@SuppressLint("ClickableViewAccessibility")
class TopSiteItemViewHolder(
    view: View,
    appStore: AppStore,
    private val viewLifecycleOwner: LifecycleOwner,
    private val interactor: TopSiteInteractor,
) : ViewHolder(view) {
    private lateinit var topSite: TopSite
    private val binding = TopSiteItemBinding.bind(view)
    private val menu: BrowserMenu by lazy {
        val topSiteMenu = TopSiteItemMenu(
            context = view.context,
            topSite = topSite,
        ) { item ->
            when (item) {
                is TopSiteItemMenu.Item.OpenInPrivateTab -> interactor.onOpenInPrivateTabClicked(
                    topSite,
                )
                is TopSiteItemMenu.Item.EditTopSite -> interactor.onEditTopSiteClicked(
                    topSite,
                )
                is TopSiteItemMenu.Item.RemoveTopSite -> {
                    interactor.onRemoveTopSiteClicked(topSite)
                }
                is TopSiteItemMenu.Item.Settings -> interactor.onSettingsClicked()
                is TopSiteItemMenu.Item.SponsorPrivacy -> interactor.onSponsorPrivacyClicked()
            }
        }

        topSiteMenu.menuBuilder.build(view.context)
    }

    init {
        itemView.setOnLongClickListener {
            interactor.onTopSiteLongClicked(topSite)
            menu.show(anchor = it)
            true
        }

        // Handle a mouse right click as a long press
        itemView.setOnTouchListener { v, event ->
            if (event.action == MotionEvent.ACTION_DOWN && event.buttonState == MotionEvent.BUTTON_SECONDARY) {
                v.performLongClick()
                return@setOnTouchListener true
            } else if (event.action == MotionEvent.ACTION_CANCEL) {
                menu.dismiss()
                return@setOnTouchListener v.onTouchEvent(event)
            }

            return@setOnTouchListener v.onTouchEvent(event)
        }

        appStore.flowScoped(viewLifecycleOwner) { flow ->
            flow.map { state -> state.wallpaperState }
                .distinctUntilChanged()
                .collect { currentState ->
                    var backgroundColor = ContextCompat.getColor(view.context, R.color.fx_mobile_layer_color_2)

                    currentState.runIfWallpaperCardColorsAreAvailable { cardColorLight, cardColorDark ->
                        backgroundColor = if (view.context.isSystemInDarkTheme()) {
                            cardColorDark
                        } else {
                            cardColorLight
                        }
                    }

                    binding.faviconCard.setCardBackgroundColor(backgroundColor)

                    val textColor = currentState.currentWallpaper.textColor
                    if (textColor != null) {
                        val color = Color(textColor).toArgb()
                        val colorList = ColorStateList.valueOf(color)
                        binding.topSiteTitle.setTextColor(color)
                        binding.topSiteSubtitle.setTextColor(color)
                        TextViewCompat.setCompoundDrawableTintList(binding.topSiteTitle, colorList)
                    } else {
                        binding.topSiteTitle.setTextColor(
                            view.context.getColorFromAttr(R.attr.textPrimary),
                        )
                        binding.topSiteSubtitle.setTextColor(
                            view.context.getColorFromAttr(R.attr.textSecondary),
                        )
                        TextViewCompat.setCompoundDrawableTintList(binding.topSiteTitle, null)
                    }
                }
        }
    }

    fun bind(topSite: TopSite, position: Int) {
        itemView.setOnClickListener {
            interactor.onSelectTopSite(topSite, position)
        }

        binding.topSiteTitle.text = topSite.title
        binding.topSiteSubtitle.isVisible = topSite is TopSite.Provided

        if (topSite is TopSite.Pinned || topSite is TopSite.Default) {
            val pinIndicator = getDrawable(itemView.context, R.drawable.ic_new_pin)
            binding.topSiteTitle.setCompoundDrawablesWithIntrinsicBounds(pinIndicator, null, null, null)
        } else {
            binding.topSiteTitle.setCompoundDrawablesWithIntrinsicBounds(null, null, null, null)
        }

        if (topSite is TopSite.Provided) {
            viewLifecycleOwner.lifecycleScope.launch(IO) {
                itemView.context.components.core.client.bitmapForUrl(topSite.imageUrl)?.let { bitmap ->
                    withContext(Main) {
                        binding.faviconImage.setImageBitmap(bitmap)
                        interactor.onTopSiteImpression(topSite, position)
                    }
                }
            }
        } else {
            when (topSite.url) {
                SupportUtils.POCKET_TRENDING_URL -> {
                    binding.faviconImage.setImageDrawable(getDrawable(itemView.context, R.drawable.ic_pocket))
                }
                else -> {
                    itemView.context.components.core.icons.loadIntoView(binding.faviconImage, topSite.url)
                }
            }
        }

        this.topSite = topSite
    }

    companion object {
        const val LAYOUT_ID = R.layout.top_site_item
    }
}
