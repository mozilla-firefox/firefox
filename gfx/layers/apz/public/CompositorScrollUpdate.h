/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_CompositorScrollUpdate_h
#define mozilla_layers_CompositorScrollUpdate_h

#include "Units.h"

namespace mozilla {
namespace layers {

/**
 * An update sent by APZ to interesting consumers (e.g. GeckoView)
 * to inform them, on every composite, about the effective visual
 * scroll offset and zoom level of the root content APZC at
 * composition time.
 */
struct CompositorScrollUpdate {
  /**
   * The compositor state (scroll offset and zoom level) after the update.
   */
  struct Metrics {
    CSSPoint mVisualScrollOffset;
    CSSToParentLayerScale mZoom;

    bool operator==(const Metrics& aOther) const;
    bool operator!=(const Metrics& aOther) const { return !(*this == aOther); }
  };

  /**
   * Describes the source of a CompositorScrollUpdate.
   *
   * This is used for populating the `source` field of
   * `GeckoSession.ScrollPositionUpdate`, and should be kept in sync
   * with the constants defined in that Java struct.
   */
  enum class Source {
    // The scroll position changed as a direct result of user interaction.
    UserInteraction,
    // The scroll position changed programmatically. This can includes changes
    // caused by script on the page, and changes caused by the browser engine
    // such as scrolling an element into view.
    Other
  };

  Metrics mMetrics;
  Source mSource;

  bool operator==(const CompositorScrollUpdate& aOther) const;
  bool operator!=(const CompositorScrollUpdate& aOther) const {
    return !(*this == aOther);
  }
};

}  // namespace layers
}  // namespace mozilla

#endif  // mozilla_layers_CompositorScrollUpdate_h
