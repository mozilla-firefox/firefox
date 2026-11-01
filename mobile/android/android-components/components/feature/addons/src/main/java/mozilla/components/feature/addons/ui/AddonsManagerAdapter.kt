/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.addons.ui

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Paint
import android.graphics.Typeface
import android.os.Build
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.view.accessibility.AccessibilityNodeInfo
import android.widget.ImageView
import android.widget.RatingBar
import android.widget.TextView
import androidx.annotation.ColorRes
import androidx.annotation.DimenRes
import androidx.annotation.DrawableRes
import androidx.annotation.StringRes
import androidx.annotation.VisibleForTesting
import androidx.appcompat.content.res.AppCompatResources
import androidx.compose.ui.platform.ComposeView
import androidx.core.content.ContextCompat
import androidx.core.view.ViewCompat
import androidx.core.view.isInvisible
import androidx.core.view.isVisible
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.ListAdapter
import mozilla.components.browser.state.action.ExtensionsProcessAction
import mozilla.components.browser.state.selector.pendingExtensionUpdatesNeedingAttention
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.feature.addons.Addon
import mozilla.components.feature.addons.R
import mozilla.components.feature.addons.ui.CustomViewHolder.AddonViewHolder
import mozilla.components.feature.addons.ui.CustomViewHolder.FooterViewHolder
import mozilla.components.feature.addons.ui.CustomViewHolder.HeaderViewHolder
import mozilla.components.feature.addons.ui.CustomViewHolder.SectionViewHolder
import mozilla.components.feature.addons.ui.CustomViewHolder.UnsupportedSectionViewHolder
import mozilla.components.support.ktx.android.content.appName
import mozilla.components.support.ktx.android.content.appVersionName

private const val VIEW_HOLDER_TYPE_SECTION = 0
private const val VIEW_HOLDER_TYPE_NOT_YET_SUPPORTED_SECTION = 1
private const val VIEW_HOLDER_TYPE_ADDON = 2
private const val VIEW_HOLDER_TYPE_FOOTER = 3
private const val VIEW_HOLDER_TYPE_HEADER = 4

/**
 * An adapter for displaying add-on items. This will display information related to the state of an add-on such as
 * recommended, unsupported or installed. In addition, it will perform actions such as installing an add-on.
 *
 * @property addonsManagerDelegate Delegate that will provides method for handling the add-on items.
 * @param addons The list of add-ons to display.
 * @property style Indicates how items should look like.
 * @property excludedAddonIDs The list of add-on IDs to be excluded from the recommended section.
 */
@Suppress("LargeClass")
class AddonsManagerAdapter(
    private val addonsManagerDelegate: AddonsManagerAdapterDelegate,
    addons: List<Addon>,
    private val style: Style? = null,
    private val excludedAddonIDs: List<String> = emptyList(),
    private val store: BrowserStore,
) : ListAdapter<Any, CustomViewHolder>(DifferCallback) {
    /**
     * Represents all the add-ons that will be distributed in multiple headers like enabled, recommended and
     * unsupported, this help have the data source of the items, displayed in the UI.
     */
    @VisibleForTesting
    internal var addonsMap: MutableMap<String, Addon> = addons.associateBy({ it.id }, { it }).toMutableMap()

    init {
        submitList(createListWithSections(addons, excludedAddonIDs))
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): CustomViewHolder {
        return when (viewType) {
            VIEW_HOLDER_TYPE_ADDON -> createAddonViewHolder(parent)
            VIEW_HOLDER_TYPE_SECTION -> createSectionViewHolder(parent)
            VIEW_HOLDER_TYPE_NOT_YET_SUPPORTED_SECTION -> createUnsupportedSectionViewHolder(parent)
            VIEW_HOLDER_TYPE_FOOTER -> createFooterSectionViewHolder(parent)
            VIEW_HOLDER_TYPE_HEADER -> createHeaderSectionViewHolder(parent)
            else -> throw IllegalArgumentException("Unrecognized viewType")
        }
    }

    override fun onViewRecycled(holder: CustomViewHolder) {
        super.onViewRecycled(holder)
        when (holder) {
            is AddonViewHolder -> {
                // Prevent the previous icon from showing while scrolling.
                holder.iconView.setImageBitmap(null)
            }

            else -> {}
        }
    }

    private fun createSectionViewHolder(parent: ViewGroup): CustomViewHolder {
        val context = parent.context
        val inflater = LayoutInflater.from(context)
        val view = inflater.inflate(R.layout.mozac_feature_addons_section_item, parent, false)
        val titleView = view.findViewById<TextView>(R.id.title)
        val divider = view.findViewById<View>(R.id.divider)
        return SectionViewHolder(view, titleView, divider)
    }

    private fun createFooterSectionViewHolder(parent: ViewGroup): CustomViewHolder {
        val context = parent.context
        val inflater = LayoutInflater.from(context)
        val view =
            inflater.inflate(
                R.layout.mozac_feature_addons_footer_section_item,
                parent,
                false,
            )
        return FooterViewHolder(view)
    }

    private fun createHeaderSectionViewHolder(parent: ViewGroup): CustomViewHolder {
        val context = parent.context
        val inflater = LayoutInflater.from(context)
        val view =
            inflater.inflate(
                R.layout.mozac_feature_addons_header_section_item,
                parent,
                false,
            )
        val restartButton = view.findViewById<TextView>(R.id.restart_button)

        return HeaderViewHolder(view, restartButton)
    }

    private fun createUnsupportedSectionViewHolder(parent: ViewGroup): CustomViewHolder {
        val context = parent.context
        val inflater = LayoutInflater.from(context)
        val view =
            inflater.inflate(
                R.layout.mozac_feature_addons_section_unsupported_section_item,
                parent,
                false,
            )
        val titleView = view.findViewById<TextView>(R.id.title)
        val descriptionView = view.findViewById<TextView>(R.id.description)

        return UnsupportedSectionViewHolder(view, titleView, descriptionView)
    }

    private fun createAddonViewHolder(parent: ViewGroup): AddonViewHolder {
        val context = parent.context
        val inflater = LayoutInflater.from(context)
        val view = inflater.inflate(R.layout.mozac_feature_addons_item, parent, false)
        val contentWrapperView = view.findViewById<View>(R.id.add_on_content_wrapper)
        val iconView = view.findViewById<ImageView>(R.id.add_on_icon)
        val titleView = view.findViewById<TextView>(R.id.add_on_name)
        val summaryView = view.findViewById<TextView>(R.id.add_on_description)
        val ratingView = view.findViewById<RatingBar>(R.id.rating)
        val ratingAccessibleView = view.findViewById<TextView>(R.id.rating_accessibility)
        val reviewCountView = view.findViewById<TextView>(R.id.review_count)
        val addButton = view.findViewById<ImageView>(R.id.add_button)
        val allowedInPrivateBrowsingLabel = view.findViewById<ImageView>(R.id.allowed_in_private_browsing_label)
        val messageBarWarningView = view.findViewById<View>(R.id.add_on_messagebar_warning)
        val messageBarErrorView = view.findViewById<View>(R.id.add_on_messagebar_error)
        val messageBarUpdateView = view.findViewById<ComposeView>(R.id.add_on_messagebar_update)

        return AddonViewHolder(
            view,
            contentWrapperView,
            iconView,
            titleView,
            summaryView,
            ratingView,
            ratingAccessibleView,
            reviewCountView,
            addButton,
            allowedInPrivateBrowsingLabel,
            messageBarWarningView,
            messageBarErrorView,
            messageBarUpdateView,
        )
    }

    override fun getItemViewType(position: Int): Int {
        return when (getItem(position)) {
            is Addon -> VIEW_HOLDER_TYPE_ADDON
            is Section -> VIEW_HOLDER_TYPE_SECTION
            is NotYetSupportedSection -> VIEW_HOLDER_TYPE_NOT_YET_SUPPORTED_SECTION
            is FooterSection -> VIEW_HOLDER_TYPE_FOOTER
            is HeaderSection -> VIEW_HOLDER_TYPE_HEADER
            else -> throw IllegalArgumentException("items[position] has unrecognized type")
        }
    }

    @SuppressLint("RecyclerView")
    override fun onBindViewHolder(holder: CustomViewHolder, position: Int) {
        val item = getItem(position)

        // Configure an accessibility delegate for each item.
        holder.itemView.accessibilityDelegate =
            object : View.AccessibilityDelegate() {
                override fun onInitializeAccessibilityNodeInfo(host: View, info: AccessibilityNodeInfo) {
                    super.onInitializeAccessibilityNodeInfo(host, info)

                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                        info.collectionItemInfo =
                            AccessibilityNodeInfo.CollectionItemInfo(
                                holder.bindingAdapterPosition,
                                1,
                                1,
                                1,
                                holder is SectionViewHolder,
                            )
                    } else {
                        @Suppress("DEPRECATION")
                        info.collectionItemInfo =
                            AccessibilityNodeInfo.CollectionItemInfo.obtain(
                                holder.bindingAdapterPosition,
                                1,
                                1,
                                1,
                                holder is SectionViewHolder,
                            )
                    }
                }
            }

        when (holder) {
            is SectionViewHolder -> bindSection(holder, item as Section, position)
            is AddonViewHolder -> bindAddon(holder, item as Addon)
            is UnsupportedSectionViewHolder ->
                bindNotYetSupportedSection(
                    holder,
                    item as NotYetSupportedSection,
                )
            is FooterViewHolder -> bindFooterButton(holder)
            is HeaderViewHolder -> bindHeaderButton(holder)
        }
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun bindSection(holder: SectionViewHolder, section: Section, position: Int) {
        holder.titleView.setText(section.title)
        ViewCompat.setAccessibilityHeading(holder.titleView, true)

        style?.let {
            holder.divider.isVisible = it.visibleDividers && position != 0
            it.maybeSetSectionsTextColor(holder.titleView)
            it.maybeSetSectionsTypeFace(holder.titleView)
            it.maybeSetSectionsDividerStyle(holder.divider)
        }
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun bindNotYetSupportedSection(
        holder: UnsupportedSectionViewHolder,
        section: NotYetSupportedSection,
    ) {
        val unsupportedAddons = addonsMap.values.filter { it.inUnsupportedSection() }
        val context = holder.itemView.context
        holder.titleView.setText(section.title)
        holder.descriptionView.text =
            if (unsupportedAddons.size == 1) {
                context.getString(R.string.mozac_feature_addons_unsupported_caption_2)
            } else {
                context.getString(
                    R.string.mozac_feature_addons_unsupported_caption_plural_2,
                    unsupportedAddons.size.toString(),
                )
            }

        holder.itemView.setOnClickListener {
            addonsManagerDelegate.onNotYetSupportedSectionClicked(unsupportedAddons)
        }
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun bindFooterButton(holder: FooterViewHolder) {
        holder.itemView.setOnClickListener {
            addonsManagerDelegate.onFindMoreAddonsButtonClicked()
        }
    }

    internal fun bindHeaderButton(holder: HeaderViewHolder) {
        holder.restartButton.setOnClickListener {
            store.dispatch(ExtensionsProcessAction.EnabledAction)
            // Remove the notification.
            submitList(currentList.filter { item: Any -> item != HeaderSection })
        }
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun bindAddon(
        holder: AddonViewHolder,
        addon: Addon,
        appName: String = holder.itemView.context.appName,
        appVersion: String = holder.itemView.context.appVersionName,
    ) {
        val context = holder.itemView.context
        addon.rating?.let {
            val reviewCount = context.getString(R.string.mozac_feature_addons_user_rating_count_2)
            val ratingContentDescription =
                String.format(
                    context.getString(R.string.mozac_feature_addons_rating_content_description_2),
                    it.average,
                )
            holder.ratingView.contentDescription = ratingContentDescription
            // Android RatingBar is not very accessibility-friendly, we will use non visible TextView
            // for contentDescription for the TalkBack feature
            holder.ratingAccessibleView.text = ratingContentDescription
            holder.ratingView.rating = it.average
            holder.reviewCountView.text = String.format(reviewCount, getFormattedAmount(it.reviews))
        }

        val addonName = addon.displayName(context)

        holder.titleView.text = addonName

        if (addon.translatableSummary.isNotEmpty()) {
            holder.summaryView.text = addon.translateSummary(context)
        } else {
            holder.summaryView.visibility = View.GONE
        }

        holder.itemView.tag = addon
        // Attach the on click listener to the content wrapper so that it doesn't overlap with the install button.
        holder.contentWrapperView.setOnClickListener {
            addonsManagerDelegate.onAddonItemClicked(addon)
        }

        holder.addButton.isInvisible = addon.isInstalled()
        holder.addButton.contentDescription =
            context.getString(
                R.string.mozac_feature_addons_install_addon_content_description_2,
                addonName,
            )
        holder.addButton.setOnClickListener {
            if (!addon.isInstalled()) {
                addonsManagerDelegate.onInstallAddonButtonClicked(addon)
            }
        }

        holder.allowedInPrivateBrowsingLabel.isVisible = addon.isAllowedInPrivateBrowsing()
        style?.maybeSetPrivateBrowsingLabelDrawable(holder.allowedInPrivateBrowsingLabel)

        holder.iconView.setIcon(addon)

        style?.maybeSetAddonNameTextColor(holder.titleView)
        style?.maybeSetAddonSummaryTextColor(holder.summaryView)

        val pendingUpdateMessage =
            if (store.state.pendingExtensionUpdatesNeedingAttention.containsKey(addon.id)) {
                context.getString(R.string.mozac_feature_addons_update_requires_new_settings, addonName)
            } else {
                null
            }

        bindMessageBars(
            context,
            holder.messageBarWarningView,
            holder.messageBarErrorView,
            holder.messageBarUpdateView,
            onLearnMoreLinkClicked = { link -> addonsManagerDelegate.onLearnMoreLinkClicked(link, addon) },
            addon,
            addonName,
            appName,
            appVersion,
            pendingUpdateMessage = pendingUpdateMessage,
            onReviewUpdateClicked = { addonsManagerDelegate.onReviewExtensionUpdateClicked(addon) },
            onCancelUpdateClicked = { addonsManagerDelegate.onCancelExtensionUpdateClicked(addon) },
        )
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun createListWithSections(addons: List<Addon>, excludedAddonIDs: List<String> = emptyList()): List<Any> {
        val itemsWithSections = ArrayList<Any>()
        val installedAddons = ArrayList<Addon>()
        val recommendedAddons = ArrayList<Addon>()
        val disabledAddons = ArrayList<Addon>()
        val unsupportedAddons = ArrayList<Addon>()

        addons.forEach { addon ->
            when {
                addon.inUnsupportedSection() -> unsupportedAddons.add(addon)
                addon.inRecommendedSection() -> recommendedAddons.add(addon)
                addon.inInstalledSection() -> installedAddons.add(addon)
                addon.inDisabledSection() -> disabledAddons.add(addon)
            }
        }

        // Calls are safe, except in tests since the store is mocked in most cases.
        @Suppress("UNNECESSARY_SAFE_CALL")
        if (store?.state?.extensionsProcessDisabled == true) {
            itemsWithSections.add(HeaderSection)
        }

        // Add installed section and addons if available
        if (installedAddons.isNotEmpty()) {
            itemsWithSections.add(Section(R.string.mozac_feature_addons_enabled, false))
            itemsWithSections.addAll(installedAddons)
        }

        // Add disabled section and addons if available
        if (disabledAddons.isNotEmpty()) {
            itemsWithSections.add(Section(R.string.mozac_feature_addons_disabled_section, true))
            itemsWithSections.addAll(disabledAddons)
        }

        // Add recommended section and addons if available
        if (recommendedAddons.isNotEmpty()) {
            itemsWithSections.add(Section(R.string.mozac_feature_addons_recommended_section, true))
            val filteredRecommendedAddons = recommendedAddons.filter {
                it.id !in excludedAddonIDs
            }
            itemsWithSections.addAll(filteredRecommendedAddons)
        }

        // Add unsupported section
        if (unsupportedAddons.isNotEmpty()) {
            itemsWithSections.add(NotYetSupportedSection(R.string.mozac_feature_addons_unavailable_section))
        }

        if (addonsManagerDelegate.shouldShowFindMoreAddonsButton()) {
            itemsWithSections.add(FooterSection)
        }

        return itemsWithSections
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal data class Section(
        @param:StringRes val title: Int,
        val visibleDivider: Boolean = true,
    )

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal data class NotYetSupportedSection(@param:StringRes val title: Int)

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE) internal object FooterSection

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE) internal object HeaderSection

    /** Allows to customize how items should look like. */
    data class Style(
        @param:ColorRes val sectionsTextColor: Int? = null,
        @param:ColorRes val addonNameTextColor: Int? = null,
        @param:ColorRes val addonSummaryTextColor: Int? = null,
        val sectionsTypeFace: Typeface? = null,
        @param:DrawableRes val addonAllowPrivateBrowsingLabelDrawableRes: Int? = null,
        val visibleDividers: Boolean = true,
        @param:ColorRes val dividerColor: Int? = null,
        @param:DimenRes val dividerHeight: Int? = null,
    ) {
        internal fun maybeSetSectionsTextColor(textView: TextView) {
            sectionsTextColor?.let {
                val color = ContextCompat.getColor(textView.context, it)
                textView.setTextColor(color)
            }
        }

        internal fun maybeSetSectionsTypeFace(textView: TextView) {
            sectionsTypeFace?.let {
                textView.typeface = it
            }
        }

        internal fun maybeSetAddonNameTextColor(textView: TextView) {
            addonNameTextColor?.let {
                val color = ContextCompat.getColor(textView.context, it)
                textView.setTextColor(color)
            }
        }

        internal fun maybeSetAddonSummaryTextColor(textView: TextView) {
            addonSummaryTextColor?.let {
                val color = ContextCompat.getColor(textView.context, it)
                textView.setTextColor(color)
            }
        }

        internal fun maybeSetPrivateBrowsingLabelDrawable(imageView: ImageView) {
            addonAllowPrivateBrowsingLabelDrawableRes?.let {
                imageView.setImageDrawable(AppCompatResources.getDrawable(imageView.context, it))
            }
        }

        internal fun maybeSetSectionsDividerStyle(divider: View) {
            dividerColor?.let {
                divider.setBackgroundColor(it)
            }
            dividerHeight?.let {
                divider.layoutParams.height = divider.context.resources.getDimensionPixelOffset(it)
            }
        }
    }

    /**
     * Update the portion of the list that contains the provided [addon].
     *
     * @property addon The add-on to be updated.
     */
    fun updateAddon(addon: Addon) {
        addonsMap[addon.id] = addon
        submitList(createListWithSections(addonsMap.values.toList(), excludedAddonIDs))
    }

    /**
     * Updates only the portion of the list that changes between the current list and the new provided [addons]. Be
     * aware that updating a subset of the visible list is not supported, [addons] will replace the current list, but
     * only the add-ons that have been changed will be updated in the UI. If you provide a subset it will replace the
     * current list.
     *
     * @property addons A list of add-on to replace the actual list.
     */
    fun updateAddons(addons: List<Addon>) {
        addonsMap = addons.associateBy({ it.id }, { it }).toMutableMap()
        submitList(createListWithSections(addons))
    }

    @Suppress("UndocumentedPublicClass")
    companion object {
        /**
         * Bind an add-on to the message bars layout.
         *
         * @param context UI context
         * @param messageBarWarningView The view of the "warning" message bar.
         * @param messageBarErrorView The view of the "error" messagebar.
         * @param messageBarUpdateView The view of the "update" message bar.
         * @param onLearnMoreLinkClicked A callback function that reacts to a click on a learn more link. The link is
         *   passed to this function. In most cases, we'll want to open the link in a tab.
         * @param addon The [Addon] to bind.
         * @param addonName The add-on name.
         * @param appName The application name.
         * @param appVersion The application version.
         * @param pendingUpdateMessage The message of the "update" message bar, or null to hide it.
         * @param onReviewUpdateClicked Invoked when "Review changes" is clicked on the "update" message bar.
         * @param onCancelUpdateClicked Invoked when the close button of the "update" message bar is clicked.
         */
        @Suppress("LongParameterList")
        fun bindMessageBars(
            context: Context,
            messageBarWarningView: View,
            messageBarErrorView: View,
            messageBarUpdateView: ComposeView,
            onLearnMoreLinkClicked: (AddonsManagerAdapterDelegate.LearnMoreLinks) -> Unit,
            addon: Addon,
            addonName: String,
            appName: String,
            appVersion: String,
            pendingUpdateMessage: String? = null,
            onReviewUpdateClicked: () -> Unit = {},
            onCancelUpdateClicked: () -> Unit = {},
        ) {
            // Make the message-bars invisible by default.
            messageBarWarningView.isVisible = false
            messageBarErrorView.isVisible = false
            messageBarUpdateView.isVisible = false

            if (pendingUpdateMessage != null) {
                messageBarUpdateView.setContent {
                    AcornTheme {
                        PendingExtensionUpdateBanner(
                            message = pendingUpdateMessage,
                            onReviewClick = onReviewUpdateClicked,
                            onCancelClick = onCancelUpdateClicked,
                        )
                    }
                }
                messageBarUpdateView.isVisible = true
            }

            val messageBarErrorTextView = messageBarErrorView.findViewById<TextView>(R.id.add_on_messagebar_error_text)
            val messageBarErrorLearnMoreLink =
                messageBarErrorView.findViewById<TextView>(R.id.add_on_messagebar_error_learn_more_link)
            // Ensure the link is visible when this view holder gets recycled.
            messageBarErrorLearnMoreLink.isVisible = true
            // This learn more link should be underlined.
            messageBarErrorLearnMoreLink.paintFlags = Paint.UNDERLINE_TEXT_FLAG
            messageBarErrorLearnMoreLink.text = context.getString(R.string.mozac_feature_addons_status_learn_more)

            if (addon.isDisabledAsBlocklisted()) {
                messageBarErrorTextView.text = context.getString(R.string.mozac_feature_addons_status_blocklisted_1)
                // We need to adjust the link text because the BLOCKLISTED_ADDON link isn't a SUMO page.
                messageBarErrorLearnMoreLink.text = context.getString(R.string.mozac_feature_addons_status_see_details)
                messageBarErrorLearnMoreLink.setOnClickListener {
                    onLearnMoreLinkClicked(AddonsManagerAdapterDelegate.LearnMoreLinks.BLOCKLISTED_ADDON)
                }
                messageBarErrorView.isVisible = true
            } else if (addon.isDisabledAsNotCorrectlySigned()) {
                messageBarErrorTextView.text =
                    context.getString(R.string.mozac_feature_addons_status_unsigned, addonName)
                messageBarErrorLearnMoreLink.setOnClickListener {
                    onLearnMoreLinkClicked(AddonsManagerAdapterDelegate.LearnMoreLinks.ADDON_NOT_CORRECTLY_SIGNED)
                }
                messageBarErrorView.isVisible = true
            } else if (addon.isDisabledAsIncompatible()) {
                messageBarErrorTextView.text =
                    context.getString(
                        R.string.mozac_feature_addons_status_incompatible,
                        addonName,
                        appName,
                        appVersion,
                    )
                // There is no link when the add-on is disabled because it isn't compatible with the application
                // version.
                messageBarErrorLearnMoreLink.isVisible = false
                messageBarErrorView.isVisible = true
            } else if (addon.isSoftBlocked()) {
                messageBarWarningView.findViewById<TextView>(R.id.add_on_messagebar_warning_text).text =
                    context.getString(
                        // Soft-blocked add-ons can be re-enabled. That's why we check whether the add-on is enabled
                        // first.
                        if (addon.isEnabled()) {
                            R.string.mozac_feature_addons_status_softblocked_re_enabled_2
                        } else {
                            R.string.mozac_feature_addons_status_softblocked_2
                        }
                    )
                // This learn more link should be underlined.
                messageBarWarningView
                    .findViewById<TextView>(R.id.add_on_messagebar_warning_learn_more_link)
                    .paintFlags = Paint.UNDERLINE_TEXT_FLAG
                messageBarWarningView
                    .findViewById<TextView>(R.id.add_on_messagebar_warning_learn_more_link)
                    .setOnClickListener {
                        onLearnMoreLinkClicked(AddonsManagerAdapterDelegate.LearnMoreLinks.BLOCKLISTED_ADDON)
                    }
                messageBarWarningView.isVisible = true
            }
        }
    }

    internal object DifferCallback : DiffUtil.ItemCallback<Any>() {
        override fun areItemsTheSame(oldItem: Any, newItem: Any): Boolean {
            return when {
                oldItem is Addon && newItem is Addon -> oldItem.id == newItem.id
                oldItem is Section && newItem is Section -> oldItem.title == newItem.title
                oldItem is NotYetSupportedSection && newItem is NotYetSupportedSection -> oldItem.title == newItem.title
                else -> false
            }
        }

        @SuppressLint("DiffUtilEquals")
        override fun areContentsTheSame(oldItem: Any, newItem: Any): Boolean {
            return oldItem == newItem
        }
    }
}

private fun Addon.inUnsupportedSection() = isInstalled() && !isSupported()

private fun Addon.inRecommendedSection() = !isInstalled()

private fun Addon.inInstalledSection() = isInstalled() && isSupported() && isEnabled()

private fun Addon.inDisabledSection() = isInstalled() && isSupported() && !isEnabled()
