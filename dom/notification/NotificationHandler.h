/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_NOTIFICATION_NotificationHandler_H_
#define DOM_NOTIFICATION_NotificationHandler_H_

#include "ErrorList.h"
#include "nsINotificationHandler.h"
#include "nsReadableUtils.h"

class nsIPrincipal;
class nsIURI;
namespace mozilla::dom {
class IPCNotification;
}  // namespace mozilla::dom

namespace mozilla::dom::notification {

nsresult RespondOnClick(nsIPrincipal* aPrincipal, const nsAString& aScope,
                        const IPCNotification& aNotification,
                        const nsAString& aActionName);

nsresult OpenWindowFor(nsIPrincipal* aPrincipal, nsIURI* aURI = nullptr);

class NotificationHandler final : public nsINotificationHandler {
 public:
  NS_DECL_ISUPPORTS

  static already_AddRefed<NotificationHandler> GetSingleton();

  NS_IMETHOD RespondOnClick(nsIPrincipal* aPrincipal,
                            const nsAString& aNotificationId,
                            const nsAString& aActionName, bool aAutoClosed,
                            Promise** aResult) override;

 private:
  ~NotificationHandler() = default;
};

}  // namespace mozilla::dom::notification

#endif  // DOM_NOTIFICATION_NotificationHandler_H_
