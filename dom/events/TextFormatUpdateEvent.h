/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_TextFormatUpdateEvent_h_
#define mozilla_dom_TextFormatUpdateEvent_h_

#include "mozilla/dom/Event.h"
#include "mozilla/dom/TextFormatUpdateEventBinding.h"

namespace mozilla::dom {

class TextFormatUpdateEvent final : public Event {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(TextFormatUpdateEvent, Event)

  static already_AddRefed<TextFormatUpdateEvent> Constructor(
      const GlobalObject& aGlobal, const nsAString& aType,
      const TextFormatUpdateEventInit& aOptions);

  JSObject* WrapObjectInternal(JSContext* aCx,
                               JS::Handle<JSObject*> aGivenProto) override;

  void GetTextFormats(nsTArray<RefPtr<TextFormat>>& aRetVal);

 private:
  TextFormatUpdateEvent(EventTarget* aOwner,
                        const TextFormatUpdateEventInit& aOptions);
  ~TextFormatUpdateEvent() = default;
  nsTArray<RefPtr<TextFormat>> mTextFormats;
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_TextFormatUpdateEvent_h_
