/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/PaymentMethodChangeEvent.h"

#include "BasicCardPayment.h"
#include "PaymentRequestUtils.h"
#include "mozilla/HoldDropJSObjects.h"
#include "mozilla/dom/PaymentRequestUpdateEvent.h"

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_CLASS(PaymentMethodChangeEvent)
NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(PaymentMethodChangeEvent,
                                                PaymentRequestUpdateEvent)
  mozilla::DropJSObjects(tmp);
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(PaymentMethodChangeEvent,
                                                  PaymentRequestUpdateEvent)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN_INHERITED(PaymentMethodChangeEvent,
                                               PaymentRequestUpdateEvent)
  NS_IMPL_CYCLE_COLLECTION_TRACE_JS_MEMBER_CALLBACK(mMethodDetails)
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_IMPL_ADDREF_INHERITED(PaymentMethodChangeEvent, PaymentRequestUpdateEvent)
NS_IMPL_RELEASE_INHERITED(PaymentMethodChangeEvent, PaymentRequestUpdateEvent)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(PaymentMethodChangeEvent)
NS_INTERFACE_MAP_END_INHERITING(PaymentRequestUpdateEvent)

already_AddRefed<PaymentMethodChangeEvent>
PaymentMethodChangeEvent::Constructor(
    mozilla::dom::EventTarget* aOwner, const nsAString& aType,
    const PaymentRequestUpdateEventInit& aEventInitDict,
    const nsAString& aMethodName, const ChangeDetails& aMethodDetails) {
  RefPtr<PaymentMethodChangeEvent> e = new PaymentMethodChangeEvent(aOwner);
  bool trusted = e->Init(aOwner);
  e->InitEvent(aType, aEventInitDict.mBubbles, aEventInitDict.mCancelable);
  e->SetTrusted(trusted);
  e->SetComposed(aEventInitDict.mComposed);
  e->SetMethodName(aMethodName);
  e->SetMethodDetails(aMethodDetails);
  return e.forget();
}

already_AddRefed<PaymentMethodChangeEvent>
PaymentMethodChangeEvent::Constructor(
    const GlobalObject& aGlobal, const nsAString& aType,
    const PaymentMethodChangeEventInit& aEventInitDict) {
  nsCOMPtr<mozilla::dom::EventTarget> owner =
      do_QueryInterface(aGlobal.GetAsSupports());
  RefPtr<PaymentMethodChangeEvent> e = new PaymentMethodChangeEvent(owner);
  bool trusted = e->Init(owner);
  e->InitEvent(aType, aEventInitDict.mBubbles, aEventInitDict.mCancelable);
  e->SetTrusted(trusted);
  e->SetComposed(aEventInitDict.mComposed);
  e->init(aEventInitDict);
  return e.forget();
}

PaymentMethodChangeEvent::PaymentMethodChangeEvent(EventTarget* aOwner)
    : PaymentRequestUpdateEvent(aOwner) {
  MOZ_ASSERT(aOwner);
  mozilla::HoldJSObjects(this);
}

void PaymentMethodChangeEvent::init(
    const PaymentMethodChangeEventInit& aEventInitDict) {
  mMethodName.Assign(aEventInitDict.mMethodName);
  mMethodDetails = aEventInitDict.mMethodDetails;
}

void PaymentMethodChangeEvent::GetMethodName(nsAString& aMethodName) {
  aMethodName.Assign(mMethodName);
}

void PaymentMethodChangeEvent::SetMethodName(const nsAString& aMethodName) {
  mMethodName = aMethodName;
}

void PaymentMethodChangeEvent::GetMethodDetails(
    JSContext* aCx, JS::MutableHandle<JSObject*> aRetVal) {
  MOZ_ASSERT(aCx);

  if (mMethodDetails) {
    aRetVal.set(mMethodDetails.get());
    return;
  }

  RefPtr<BasicCardService> service = BasicCardService::GetService();
  MOZ_ASSERT(service);
  aRetVal.set(nullptr);
  switch (mInternalDetails.type()) {
    case ChangeDetails::GeneralMethodDetails: {
      const GeneralDetails& rawDetails = mInternalDetails.generalDetails();
      DeserializeToJSObject(rawDetails.details, aCx, aRetVal);
      break;
    }
    case ChangeDetails::BasicCardMethodDetails: {
      const BasicCardDetails& rawDetails = mInternalDetails.basicCardDetails();
      BasicCardChangeDetails basicCardDetails;
      PaymentOptions options;
      mRequest->GetOptions(options);
      if (options.mRequestBillingAddress) {
        if (!rawDetails.billingAddress.country.IsEmpty() ||
            !rawDetails.billingAddress.addressLine.IsEmpty() ||
            !rawDetails.billingAddress.region.IsEmpty() ||
            !rawDetails.billingAddress.regionCode.IsEmpty() ||
            !rawDetails.billingAddress.city.IsEmpty() ||
            !rawDetails.billingAddress.dependentLocality.IsEmpty() ||
            !rawDetails.billingAddress.postalCode.IsEmpty() ||
            !rawDetails.billingAddress.sortingCode.IsEmpty() ||
            !rawDetails.billingAddress.organization.IsEmpty() ||
            !rawDetails.billingAddress.recipient.IsEmpty() ||
            !rawDetails.billingAddress.phone.IsEmpty()) {
          nsCOMPtr<nsPIDOMWindowInner> window =
              do_QueryInterface(GetParentObject());
          basicCardDetails.mBillingAddress =
              new PaymentAddress(window, rawDetails.billingAddress.country,
                                 rawDetails.billingAddress.addressLine,
                                 rawDetails.billingAddress.region,
                                 rawDetails.billingAddress.regionCode,
                                 rawDetails.billingAddress.city,
                                 rawDetails.billingAddress.dependentLocality,
                                 rawDetails.billingAddress.postalCode,
                                 rawDetails.billingAddress.sortingCode,
                                 rawDetails.billingAddress.organization,
                                 rawDetails.billingAddress.recipient,
                                 rawDetails.billingAddress.phone);
        }
      }
      MOZ_ASSERT(aCx);
      JS::Rooted<JS::Value> value(aCx);
      if (NS_WARN_IF(!basicCardDetails.ToObjectInternal(aCx, &value))) {
        return;
      }
      aRetVal.set(&value.toObject());
      break;
    }
    default: {
      break;
    }
  }
}

void PaymentMethodChangeEvent::SetMethodDetails(
    const ChangeDetails& aMethodDetails) {
  mInternalDetails = aMethodDetails;
}

PaymentMethodChangeEvent::~PaymentMethodChangeEvent() {
  mozilla::DropJSObjects(this);
}

JSObject* PaymentMethodChangeEvent::WrapObjectInternal(
    JSContext* aCx, JS::Handle<JSObject*> aGivenProto) {
  return PaymentMethodChangeEvent_Binding::Wrap(aCx, this, aGivenProto);
}

}  // namespace mozilla::dom
