/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.interactor

import org.mozilla.fenix.home.bookmarks.interactor.BookmarksInteractor
import org.mozilla.fenix.home.pocket.interactor.PocketStoriesInteractor
import org.mozilla.fenix.home.privatebrowsing.interactor.PrivateBrowsingInteractor
import org.mozilla.fenix.home.recentsyncedtabs.interactor.RecentSyncedTabInteractor
import org.mozilla.fenix.home.recenttabs.interactor.RecentTabInteractor
import org.mozilla.fenix.home.recentvisits.interactor.RecentVisitsInteractor
import org.mozilla.fenix.home.search.HomeSearchInteractor
import org.mozilla.fenix.home.sessioncontrol.CollectionInteractor
import org.mozilla.fenix.home.sessioncontrol.MessageCardInteractor
import org.mozilla.fenix.home.sessioncontrol.SetupChecklistInteractor
import org.mozilla.fenix.home.sessioncontrol.TabSessionInteractor
import org.mozilla.fenix.home.sessioncontrol.TrackingProtectionInteractor
import org.mozilla.fenix.home.sessioncontrol.WallpaperInteractor
import org.mozilla.fenix.home.sports.SportsWidgetInteractor
import org.mozilla.fenix.home.termsofuse.PrivacyNoticeBannerInteractor
import org.mozilla.fenix.home.toolbar.ToolbarInteractor
import org.mozilla.fenix.home.topsites.interactor.TopSiteInteractor

/** Homepage interactor for interactions with the homepage UI. */
interface HomepageInteractor :
    CollectionInteractor,
    TopSiteInteractor,
    TabSessionInteractor,
    ToolbarInteractor,
    HomeSearchInteractor,
    MessageCardInteractor,
    PrivacyNoticeBannerInteractor,
    RecentTabInteractor,
    RecentSyncedTabInteractor,
    BookmarksInteractor,
    RecentVisitsInteractor,
    PocketStoriesInteractor,
    PrivateBrowsingInteractor,
    WallpaperInteractor,
    SetupChecklistInteractor,
    TrackingProtectionInteractor,
    SportsWidgetInteractor
