/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Retrieves and displays icons in native menu items on macOS.
 */

#ifndef nsMenuItemIconX_h_
#define nsMenuItemIconX_h_

#import <Cocoa/Cocoa.h>

#include "mozilla/WeakPtr.h"
#include "mozilla/widget/IconLoader.h"

class nsIconLoaderService;
class nsIURI;
class nsIContent;
class nsIPrincipal;
class imgRequestProxy;
class nsMenuParentX;
class nsPresContext;

namespace mozilla {
class ComputedStyle;
}

class nsMenuItemIconX final : public mozilla::widget::IconLoader::Listener {
 public:
  class Listener {
   public:
    virtual void IconUpdated() = 0;
  };

  explicit nsMenuItemIconX(Listener* aListener);
  ~nsMenuItemIconX();

  // SetupIcon starts the icon load. Once the icon has loaded,
  // nsMenuParentX::IconUpdated will be called. The icon image needs to be
  // retrieved from GetIconImage(). If aContent is an icon-less menuitem,
  // GetIconImage() will return nil. If it does have an icon, GetIconImage()
  // will return a transparent placeholder icon during the load and the actual
  // icon when the load is completed.
  // While macOS opens menus without showing them, the load is deferred instead
  // and HasDeferredLoad() returns true until SetupIcon is called again.
  void SetupIcon(nsIContent* aContent);

  bool HasDeferredLoad() const { return mHasDeferredLoad; }

  // Implements this method for mozilla::widget::IconLoader::Listener.
  // Called once the icon load is complete.
  nsresult OnComplete(imgIContainer* aImage) override;

  // Returns a weak reference to the icon image that is owned by this class. Can
  // return nil.
  NSImage* GetIconImage() const { return mIconImage; }

 protected:
  // Returns whether there should be an icon.
  bool StartIconLoad(nsIContent* aContent);

  Listener* mListener;  // [weak]
  RefPtr<const mozilla::ComputedStyle> mComputedStyle;
  mozilla::WeakPtr<nsPresContext> mPresContext;
  NSImage* mIconImage = nil;  // [strong]
  RefPtr<mozilla::widget::IconLoader> mIconLoader;
  bool mHasDeferredLoad = false;
};

#endif  // nsMenuItemIconX_h_
