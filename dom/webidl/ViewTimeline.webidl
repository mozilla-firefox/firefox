/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * https://drafts.csswg.org/scroll-animations-1/#viewtimeline-interface
 *
 * Copyright © 2015 W3C® (MIT, ERCIM, Keio), All Rights Reserved. W3C
 * liability, trademark and document use rules apply.
 */

[Exposed=Window, Pref="layout.css.scroll-driven-animations.viewtimeline.enabled"]
interface ViewTimeline : ScrollTimeline {
  // FIXME: Bug 2016879 - Add the constructor.
  readonly attribute Element subject;
  // FIXME: Bug 2016880 - Use CSSNumericValue, not double.
  // Also see https://github.com/w3c/csswg-drafts/issues/13844 re nullability.
  readonly attribute double? startOffset;
  readonly attribute double? endOffset;
};
