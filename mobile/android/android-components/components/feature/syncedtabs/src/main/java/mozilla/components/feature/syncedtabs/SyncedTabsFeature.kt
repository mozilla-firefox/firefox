/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.syncedtabs

import android.content.Context
import androidx.lifecycle.LifecycleOwner
import kotlin.coroutines.CoroutineContext
import kotlinx.coroutines.Dispatchers
import mozilla.components.browser.storage.sync.Tab
import mozilla.components.feature.syncedtabs.commands.SyncedTabsCommands
import mozilla.components.feature.syncedtabs.controller.DefaultController
import mozilla.components.feature.syncedtabs.controller.SyncedTabsController
import mozilla.components.feature.syncedtabs.interactor.DefaultInteractor
import mozilla.components.feature.syncedtabs.interactor.SyncedTabsInteractor
import mozilla.components.feature.syncedtabs.presenter.DefaultPresenter
import mozilla.components.feature.syncedtabs.presenter.SyncedTabsPresenter
import mozilla.components.feature.syncedtabs.storage.SyncedTabsStorage
import mozilla.components.feature.syncedtabs.view.SyncedTabsView
import mozilla.components.service.fxa.manager.FxaAccountManager
import mozilla.components.support.base.feature.LifecycleAwareFeature

/**
 * Feature implementation that will keep a [SyncedTabsView] notified with other synced device tabs for the Firefox Sync
 * account.
 *
 * @param storage The synced tabs storage that stores the current device's and remote device tabs.
 * @param commands The queue used to send synced tabs commands to other devices.
 * @param accountManager Firefox Account Manager that holds a Firefox Sync account.
 * @param view An implementor of [SyncedTabsView] that will be notified of changes.
 * @param lifecycleOwner Android Lifecycle Owner to bind observers onto.
 * @param coroutineContext A coroutine context that can be used to perform work off the main thread.
 * @param onTabClicked Invoked when a tab is selected by the user on the [SyncedTabsView].
 * @param controller See [SyncedTabsController].
 * @param presenter See [SyncedTabsPresenter].
 * @param interactor See [SyncedTabsInteractor].
 */
class SyncedTabsFeature(
    context: Context,
    storage: SyncedTabsStorage,
    commands: SyncedTabsCommands,
    accountManager: FxaAccountManager,
    view: SyncedTabsView,
    lifecycleOwner: LifecycleOwner,
    coroutineContext: CoroutineContext = Dispatchers.IO,
    onTabClicked: (Tab) -> Unit,
    private val controller: SyncedTabsController =
        DefaultController(
            storage,
            accountManager,
            view,
            coroutineContext,
        ),
    private val presenter: SyncedTabsPresenter =
        DefaultPresenter(
            context,
            controller,
            commands,
            accountManager,
            view,
            lifecycleOwner,
        ),
    private val interactor: SyncedTabsInteractor =
        DefaultInteractor(
            controller,
            view,
            onTabClicked,
        ),
) : LifecycleAwareFeature {

    override fun start() {
        presenter.start()
        interactor.start()
    }

    override fun stop() {
        presenter.stop()
        interactor.stop()
    }
}
