/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.accounts

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapShader
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Shader
import androidx.appcompat.content.res.AppCompatResources
import androidx.core.graphics.createBitmap
import kotlin.math.min
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.flow.distinctUntilChangedBy
import kotlinx.coroutines.launch
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.data.MenuItemSummary
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.MenuItemIcon
import mozilla.components.compose.menu.ui.MenuItemIconBitmap
import mozilla.components.compose.menu.ui.MenuItemIconDrawable
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.compose.menu.ui.MenuItemState
import mozilla.components.concept.fetch.Client
import mozilla.components.service.fxa.manager.AccountState.Authenticated
import mozilla.components.service.fxa.manager.AccountState.AuthenticationProblem
import mozilla.components.service.fxa.manager.AccountState.NotAuthenticated
import mozilla.components.service.fxa.manager.AccountState.Unknown
import mozilla.components.service.fxa.store.SyncState
import mozilla.components.service.fxa.store.SyncStore
import mozilla.components.support.ktx.android.util.dpToPx
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.MenuAccessPoint
import org.mozilla.fenix.components.menu.MenuHost
import org.mozilla.fenix.components.menu.MenuItemProvider
import org.mozilla.fenix.components.menu.MenuTarget
import org.mozilla.fenix.components.menu.store.MenuAction
import org.mozilla.fenix.components.menu.toFenixFxAEntryPoint
import org.mozilla.fenix.ext.bitmapForUrl

private const val AVATAR_SIZE_DP = 24

/** Provides the menu item for the current Mozilla account state. */
class MozillaAccountMenuItemProvider(
    private val syncStore: SyncStore,
    private val httpClient: Client,
    context: Context,
    private val accessPoint: MenuAccessPoint,
    target: MenuTarget,
    scope: CoroutineScope,
) : MenuItemProvider {
    private val avatarSize = AVATAR_SIZE_DP.dpToPx(context.resources.displayMetrics)
    private val warningIcon =
        AppCompatResources.getDrawable(
                context,
                iconsR.drawable.mozac_ic_avatar_warning_circle_fill_multicolor_24,
            )
            ?.let(::MenuItemIconDrawable)
            ?: MenuItemIconRes(iconsR.drawable.mozac_ic_avatar_warning_circle_fill_multicolor_24)
    private val mutableItems: MutableStateFlow<List<MenuItem>>? =
        if (target == MenuTarget.CustomizeMenu) {
            null
        } else {
            MutableStateFlow(listOf(syncStore.state.toMenuItem()))
        }

    override val itemsFlow: StateFlow<List<MenuItem>> =
        mutableItems?.asStateFlow() ?: MutableStateFlow(listOf(DEFAULT_MENU_ITEM)).asStateFlow()

    init {
        if (target != MenuTarget.CustomizeMenu) {
            scope.launch {
                syncStore.stateFlow
                    .distinctUntilChangedBy { it.account to it.accountState }
                    .collectLatest { state ->
                        updateMenuItem(state)
                    }
            }
        }
    }

    private suspend fun updateMenuItem(state: SyncState) {
        val items = mutableItems ?: return
        items.value = listOf(state.toMenuItem())

        if (state.accountState == AuthenticationProblem) {
            return
        }

        val avatarUrl = state.account?.avatar?.url ?: return
        val avatar = httpClient.bitmapForUrl(avatarUrl, avatarSize, avatarSize)?.toCircularBitmap() ?: return

        items.value = listOf(state.toMenuItem(icon = MenuItemIconBitmap(avatar)))
    }

    private fun SyncState.toMenuItem(icon: MenuItemIcon = defaultIcon): MenuItem =
        StandardMenuItem(
            title = MenuItemTitle(title),
            summary = summary,
            icon = icon,
            onClickEvent =
                MenuAction.Navigate.MozillaAccount(
                    accountState = accountState,
                    accesspoint = accessPoint,
                ),
        )

    private val SyncState.defaultIcon: MenuItemIcon
        get() =
            when (accountState) {
                AuthenticationProblem -> warningIcon
                else -> MenuItemIconRes(iconsR.drawable.mozac_ic_avatar_circle_24)
            }

    override fun handles(event: MenuEvent) = event is MenuAction.Navigate.MozillaAccount

    override fun onEvent(event: MenuEvent, menu: MenuHost) {
        if (event !is MenuAction.Navigate.MozillaAccount) return

        menu.navigate(
            when (event.accountState) {
                Authenticated -> NavGraphDirections.actionGlobalAccountSettingsFragment()
                AuthenticationProblem ->
                    NavGraphDirections.actionGlobalAccountProblemFragment(
                        entrypoint = event.accesspoint.toFenixFxAEntryPoint()
                    )
                NotAuthenticated,
                Unknown ->
                    NavGraphDirections.actionGlobalTurnOnSync(entrypoint = event.accesspoint.toFenixFxAEntryPoint())
            }
        )
    }

    companion object {
        private val DEFAULT_MENU_ITEM =
            StandardMenuItem(
                title = MenuItemTitle(Text.Resource(R.string.browser_menu_sign_in)),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_avatar_circle_24),
            )
    }
}

/** What to call the account. */
private val SyncState.title: Text
    get() =
        when (accountState) {
            NotAuthenticated,
            Unknown -> Text.Resource(R.string.browser_menu_sign_in)

            AuthenticationProblem ->
                account?.displayName?.let(Text::String)
                    ?: account?.email?.let(Text::String)
                    ?: Text.Resource(R.string.browser_menu_sign_back_in_to_sync)

            Authenticated ->
                account?.displayName?.let(Text::String)
                    ?: account?.email?.let(Text::String)
                    ?: Text.Resource(R.string.browser_menu_account_settings)
        }

private val SyncState.summary: MenuItemSummary
    get() =
        when (accountState) {
            NotAuthenticated,
            Unknown ->
                MenuItemSummary(
                    text = Text.Resource(R.string.browser_menu_sign_in_caption_3),
                    maxLines = 2,
                )

            AuthenticationProblem ->
                MenuItemSummary(
                    text = Text.Resource(R.string.browser_menu_syncing_paused_caption),
                    state = MenuItemState.WARNING,
                    maxLines = 2,
                )

            Authenticated ->
                MenuItemSummary(
                    text = Text.Resource(R.string.browser_menu_signed_in_caption),
                    maxLines = 2,
                )
        }

private fun Bitmap.toCircularBitmap(): Bitmap {
    val size = min(width, height)
    val square = Bitmap.createBitmap(this, (width - size) / 2, (height - size) / 2, size, size)
    val result = createBitmap(size, size)
    val paint =
        Paint(Paint.ANTI_ALIAS_FLAG).apply {
            shader = BitmapShader(square, Shader.TileMode.CLAMP, Shader.TileMode.CLAMP)
        }

    Canvas(result).drawCircle(size / 2f, size / 2f, size / 2f, paint)
    return result
}
