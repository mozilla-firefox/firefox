/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { GeckoViewModule } from "resource://gre/modules/GeckoViewModule.sys.mjs";

/**
 * A request is pending from the moment the launch event is sent until either
 * its player is set up or a newer request supersedes it, and active from then
 * until the window closes. The embedder can only show one window, so a request
 * supersedes whatever came before it, whichever session it came from.
 *
 * @typedef {object} PiP
 * @property {"pending"|"active"} state
 * @property {object} sourceDispatcher
 *   EventDispatcher of the source session.
 * @property {object} videoRef
 *   ContentDOMReference to the video element, handed to the player actor to
 *   clone. Absent for a placement no request has claimed.
 * @property {Element} playerBrowser
 *   Active only. Browser of the player session showing the cloned video.
 * @property {number} bcGroupId
 *   Pending only. Browsing context group of the source, which the player
 *   session is opened in.
 * @property {string} remoteType
 *   Pending only. Content process the player session has to be opened in, for
 *   the same reason.
 * @property {?string} token
 *   Pending only. Placement token handed to the embedder, until a session is
 *   opened against it. Null once consumed: a token places a single session.
 * @property {Function} resolve
 *   Pending only. Settles the promise returned by request() once the player is
 *   set up.
 * @property {Function} reject
 *   Pending only. Settles that promise if setting the player up failed.
 */

/** @type {?PiP} */
let gPiP = null;

/**
 * Process-global state coordinating a picture-in-picture window with the session
 * whose video it clones.
 */
export const GeckoViewPictureInPictureController = {
  // `sourceDispatcher` must be the EventDispatcher of the session the video lives in: the embedder
  // identifies the tab to return to when the picture-in-picture window is expanded by which
  // session the launch event arrives on.
  request(sourceWGP, videoRef, sourceDispatcher) {
    const bc = sourceWGP.browsingContext;
    const bcGroupId = bc.group.id;
    const remoteType = sourceWGP.domProcess.remoteType;

    if (!sourceDispatcher) {
      return Promise.reject(
        new Error("No EventDispatcher for the requesting session")
      );
    }

    // The window already clones this video. A second player would not help, since a video is
    // cloned into a single element and the old player stops the clone when it unloads.
    if (
      gPiP?.state === "active" &&
      gPiP.videoRef.browsingContextId === videoRef.browsingContextId &&
      gPiP.videoRef.id === videoRef.id
    ) {
      return Promise.resolve();
    }

    // A request that supersedes an unanswered one takes its placement with it.
    this._takePending()?.reject?.(
      new Error("Superseded by a newer picture-in-picture request")
    );

    const token = this.mintPlacementToken(bcGroupId, remoteType);

    const { promise, resolve, reject } = Promise.withResolvers();
    Object.assign(gPiP, {
      videoRef,
      sourceDispatcher,
      resolve,
      reject,
    });

    sourceDispatcher.sendRequest("GeckoView:PictureInPicture:Launch", {
      placementToken: token,
    });

    return promise;
  },

  // Mints a token standing for the placement a session cloning the video has to be opened in, to
  // be handed to the embedder. Good for a single session, until it is consumed.
  mintPlacementToken(bcGroupId, remoteType) {
    const token = Services.uuid.generateUUID().toString();
    gPiP = { state: "pending", bcGroupId, remoteType, token };
    return token;
  },

  // Returns the browsing context group and remote type `token` stands for, and invalidates it so
  // that it places a single session. Returns null for a token that was never handed out, was
  // already used, or belongs to a request that has since been superseded.
  consumePlacement(token) {
    if (gPiP?.token !== token) {
      warn`No placement for token ${token}, opening the session unplaced`;
      return null;
    }
    gPiP.token = null;
    const { bcGroupId, remoteType } = gPiP;
    return { browsingContextGroupId: bcGroupId, remoteType };
  },

  // Requests picture-in-picture for the video the focused frame is playing, or otherwise for a
  // paused video that has played, preferring a playing video in any frame over a paused one.
  async requestForCurrentMedia(browser, sourceDispatcher) {
    // The session's own browsing context only reaches the top document, whose <video> elements
    // are the ones outside any frame, so a video in an iframe would be invisible to it and lives
    // in another process. Focus names the frame the user last interacted with.
    const bc = Services.focus.focusedContentBrowsingContext;
    if (bc?.top !== browser.browsingContext) {
      await this._requestForAnyFrame(browser, sourceDispatcher, null, null);
      return;
    }

    const wgp = bc.currentWindowGlobal;
    const found = await wgp
      .getActor("GeckoViewPictureInPictureSource")
      .sendQuery("GeckoView:PictureInPicture:FindVideo");
    if (found && !found.paused) {
      await this.request(wgp, found.videoRef, sourceDispatcher);
      return;
    }
    await this._requestForAnyFrame(
      browser,
      sourceDispatcher,
      wgp,
      found ? { wgp, videoRef: found.videoRef } : null
    );
  },

  // The embedder asks from outside the page, so the user may have moved focus off the frame playing
  // the video since starting it. Falls back to the first frame of the session, in tree order, that
  // is playing a video, then to pausedFallback, then to the first frame with a paused video.
  // askedWgp is a frame that was already asked and is skipped.
  async _requestForAnyFrame(
    browser,
    sourceDispatcher,
    askedWgp,
    pausedFallback
  ) {
    const pausedCandidates = pausedFallback ? [pausedFallback] : [];
    for (const bc of browser.browsingContext.getAllBrowsingContextsInSubtree()) {
      const wgp = bc.currentWindowGlobal;
      if (!wgp || wgp === askedWgp) {
        continue;
      }
      let found;
      try {
        found = await wgp
          .getActor("GeckoViewPictureInPictureSource")
          .sendQuery("GeckoView:PictureInPicture:FindVideo");
      } catch (e) {
        // The frame navigated or went away while it was being asked.
        continue;
      }
      if (!found) {
        continue;
      }
      if (!found.paused) {
        await this.request(wgp, found.videoRef, sourceDispatcher);
        return;
      }
      pausedCandidates.push({ wgp, videoRef: found.videoRef });
    }
    // A candidate's frame may have navigated or gone away while later frames were being asked.
    const fallback = pausedCandidates.find(
      c =>
        !c.wgp.isClosed && c.wgp.browsingContext?.currentWindowGlobal === c.wgp
    );
    if (fallback) {
      await this.request(fallback.wgp, fallback.videoRef, sourceDispatcher);
    }
  },

  // Called when Gecko decided the picture-in-picture window should close, which reaches as a
  // "PictureInPicture:Close" message from the player actor. Toolkit's content-side player code
  // sends it when the cloned video ends, is removed from the page, or its tab is discarded. The
  // embedder owns the window, so we ask it to tear it down. This is the mirror of
  // [forgetSession], where the embedder closed the window first and only tells us after.
  askEmbedderToClose(playerBrowser) {
    this.forgetPlayer(playerBrowser)?.sendRequest(
      "GeckoView:PictureInPicture:Close"
    );
    return Promise.resolve();
  },

  // Drops the state of the window `playerBrowser` hosts. A player that is not the active one
  // belongs to a window that was already replaced, so it leaves the state alone. Returns the
  // source session's dispatcher, or null if there was nothing to drop.
  forgetPlayer(playerBrowser) {
    if (gPiP?.state !== "active" || gPiP.playerBrowser !== playerBrowser) {
      return null;
    }
    const { sourceDispatcher } = gPiP;
    gPiP = null;
    return sourceDispatcher;
  },

  // Sets up the player in the session opened for this browsing context group.
  claimPlayerSession(browser) {
    const bcGroupId = browser.browsingContext.group.id;
    if (gPiP?.state !== "pending" || gPiP.bcGroupId !== bcGroupId) {
      return;
    }

    // A token minted without a video to clone places the session and nothing more.
    const pending = this._takePending();
    if (!pending.videoRef) {
      return;
    }

    this._setupPlayer(browser, pending);
  },

  // Drops the pending request, and with it the placement its token stands for.
  _takePending() {
    if (gPiP?.state !== "pending") {
      return null;
    }
    const pending = gPiP;
    gPiP = null;
    return pending;
  },

  // Drops the state of the source session `sourceDispatcher` belongs to, either because the
  // embedder tore its window down itself, unlike [askEmbedderToClose] which asks it to, or because
  // the session went away without its window being closed first. Without this a closed session
  // keeps a browsing context group that a later session can be handed.
  forgetSession(sourceDispatcher, reason) {
    if (gPiP?.sourceDispatcher !== sourceDispatcher) {
      return;
    }
    if (gPiP.state === "pending") {
      this._takePending()?.reject?.(new Error(reason));
      return;
    }
    gPiP = null;
  },

  async _setupPlayer(
    playerBrowser,
    { videoRef, sourceDispatcher, resolve, reject }
  ) {
    try {
      await playerBrowser.browsingContext.currentWindowGlobal
        .getActor("GeckoViewPictureInPicturePlayer")
        .setupPlayer(videoRef);
      // A request that landed while we were switching state from pending to active takes priority.
      if (gPiP) {
        reject(new Error("Superseded by a newer picture-in-picture request"));
        return;
      }
      gPiP = { state: "active", sourceDispatcher, playerBrowser, videoRef };
      resolve();
    } catch (e) {
      // A newer request is using the window by now, so it must be left open.
      if (!gPiP) {
        sourceDispatcher.sendRequest("GeckoView:PictureInPicture:Close");
      }
      reject(e);
    }
  },
};

/**
 * GeckoViewModule for the requests a session makes about the picture-in-picture
 * window showing one of its videos. One instance per session; the shared state
 * it acts on lives in [GeckoViewPictureInPictureController] above.
 */
export class GeckoViewPictureInPicture extends GeckoViewModule {
  onInit() {
    debug`onInit`;
    this.registerListener([
      "GeckoView:PictureInPicture:Request",
      "GeckoView:PictureInPicture:Exit",
    ]);

    // A session opened against a placement is the player for the request that minted its token.
    if (this.settings.pictureInPictureToken) {
      GeckoViewPictureInPictureController.claimPlayerSession(this.browser);
    }
  }

  onDestroy() {
    if (this.settings.pictureInPictureToken) {
      GeckoViewPictureInPictureController.forgetPlayer(this.browser);
      return;
    }
    GeckoViewPictureInPictureController.forgetSession(
      this.eventDispatcher,
      "Requesting session went away"
    );
  }

  onEvent(aEvent, aData, aCallback) {
    debug`onEvent: event=${aEvent}, data=${aData}`;
    switch (aEvent) {
      case "GeckoView:PictureInPicture:Request":
        GeckoViewPictureInPictureController.requestForCurrentMedia(
          this.browser,
          this.eventDispatcher
        ).then(
          () => aCallback.onSuccess(null),
          e => aCallback.onError(e.message)
        );
        break;
      case "GeckoView:PictureInPicture:Exit":
        GeckoViewPictureInPictureController.forgetSession(
          this.eventDispatcher,
          "Picture-in-picture closed before setup"
        );
        aCallback.onSuccess(null);
        break;
    }
  }
}

const { debug, warn } = GeckoViewPictureInPicture.initLogging(
  "GeckoViewPictureInPicture"
);
