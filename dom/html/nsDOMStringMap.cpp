/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsDOMStringMap.h"

#include "jsapi.h"
#include "mozilla/dom/DOMStringMapBinding.h"
#include "nsContentUtils.h"
#include "nsError.h"
#include "nsGenericHTMLElement.h"

using namespace mozilla;
using namespace mozilla::dom;

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(nsDOMStringMap)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(nsDOMStringMap)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mElement)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(nsDOMStringMap)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
  // Check that mElement exists in case the unlink code is run more than once.
  if (tmp->mElement) {
    // Call back to element to null out weak reference to this object.
    tmp->mElement->ClearDataset();
    tmp->mElement->RemoveMutationObserver(tmp);
    tmp->mElement = nullptr;
  }
  tmp->mExpandoAndGeneration.OwnerUnlinked();
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsDOMStringMap)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsIMutationObserver)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsDOMStringMap)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsDOMStringMap)

nsDOMStringMap::nsDOMStringMap(Element* aElement)
    : mElement(aElement), mRemovingProp(false) {
  mElement->AddMutationObserver(this);
}

nsDOMStringMap::~nsDOMStringMap() {
  // Check if element still exists, may have been unlinked by cycle collector.
  if (mElement) {
    // Call back to element to null out weak reference to this object.
    mElement->ClearDataset();
    mElement->RemoveMutationObserver(this);
  }
}

DocGroup* nsDOMStringMap::GetDocGroup() const {
  return mElement ? mElement->GetDocGroup() : nullptr;
}

/* virtual */
JSObject* nsDOMStringMap::WrapObject(JSContext* cx,
                                     JS::Handle<JSObject*> aGivenProto) {
  return DOMStringMap_Binding::Wrap(cx, this, aGivenProto);
}

void nsDOMStringMap::NamedGetter(const nsAString& aProp, bool& found,
                                 DOMString& aResult) const {
  nsAutoString attr;

  if (!DataPropToAttr(aProp, attr)) {
    found = false;
    return;
  }

  found = mElement->GetAttr(attr, aResult);
}

// https://html.spec.whatwg.org/#dom-domstringmap-setitem
void nsDOMStringMap::NamedSetter(const nsAString& aProp,
                                 const nsAString& aValue, ErrorResult& rv) {
  nsAutoString attr;
  if (!DataPropToAttr(aProp, attr)) {
    rv.Throw(NS_ERROR_DOM_SYNTAX_ERR);
    return;
  }

  if (!nsContentUtils::IsValidAttributeLocalName(attr)) {
    rv.ThrowInvalidCharacterError("Invalid attribute name");
    return;
  }

  RefPtr<nsAtom> attrAtom = NS_AtomizeMainThread(attr);
  MOZ_ASSERT(attrAtom, "Should be infallible");

  nsresult res = mElement->SetAttr(kNameSpaceID_None, attrAtom, aValue, true,
                                   IsKnownNewAttr::No);
  if (NS_FAILED(res)) {
    rv.Throw(res);
  }
}

void nsDOMStringMap::NamedDeleter(const nsAString& aProp, bool& found) {
  // Currently removing property, attribute is already removed.
  if (mRemovingProp) {
    found = false;
    return;
  }

  nsAutoString attr;
  if (!DataPropToAttr(aProp, attr)) {
    found = false;
    return;
  }

  RefPtr<nsAtom> attrAtom = NS_AtomizeMainThread(attr);
  MOZ_ASSERT(attrAtom, "Should be infallible");

  mRemovingProp = true;
  mElement->UnsetAttr(kNameSpaceID_None, attrAtom, true, &found);
  mRemovingProp = false;
}

void nsDOMStringMap::GetSupportedNames(nsTArray<nsString>& aNames) {
  uint32_t attrCount = mElement->GetAttrCount();

  // Iterate through all the attributes and add property
  // names corresponding to data attributes to return array.
  for (uint32_t i = 0; i < attrCount; ++i) {
    const nsAttrName* attrName = mElement->GetAttrNameAt(i);
    // Skip the ones that are not in the null namespace
    if (attrName->NamespaceID() != kNameSpaceID_None) {
      continue;
    }

    nsAutoString prop;
    if (!AttrToDataProp(nsDependentAtomString(attrName->LocalName()), prop)) {
      continue;
    }

    aNames.AppendElement(prop);
  }
}

/**
 * Converts a dataset property name to the corresponding data attribute name.
 * (ex. aBigFish to data-a-big-fish).
 */
bool nsDOMStringMap::DataPropToAttr(const nsAString& aProp,
                                    nsAutoString& aResult) {
  // aResult is an autostring, so don't worry about setting its capacity:
  // SetCapacity is slow even when it's a no-op and we already have enough
  // storage there for most cases, probably.
  aResult.AppendLiteral("data-");

  // Iterate property by character to form attribute name.
  // Return syntax error if there is a sequence of "-" followed by a character
  // in the range "a" to "z".
  // Replace capital characters with "-" followed by lower case character.
  // Otherwise, simply append character to attribute name.
  const char16_t* start = aProp.BeginReading();
  const char16_t* end = aProp.EndReading();
  const char16_t* cur = start;
  for (; cur < end; ++cur) {
    const char16_t* next = cur + 1;
    if (char16_t('-') == *cur && next < end && char16_t('a') <= *next &&
        *next <= char16_t('z')) {
      // Syntax error if character following "-" is in range "a" to "z".
      return false;
    }

    if (char16_t('A') <= *cur && *cur <= char16_t('Z')) {
      // Append the characters in the range [start, cur)
      aResult.Append(start, cur - start);
      // Uncamel-case characters in the range of "A" to "Z".
      aResult.Append(char16_t('-'));
      aResult.Append(*cur - 'A' + 'a');
      start = next;  // We've already appended the thing at *cur
    }
  }

  aResult.Append(start, cur - start);

  return true;
}

/**
 * Converts a data attribute name to the corresponding dataset property name.
 * (ex. data-a-big-fish to aBigFish).
 */
bool nsDOMStringMap::AttrToDataProp(const nsAString& aAttr,
                                    nsAutoString& aResult) {
  // If the attribute name does not begin with "data-" then it can not be
  // a data attribute.
  if (!StringBeginsWith(aAttr, u"data-"_ns)) {
    return false;
  }

  // Start reading attribute from first character after "data-".
  const char16_t* cur = aAttr.BeginReading() + 5;
  const char16_t* end = aAttr.EndReading();

  // Don't try to mess with aResult's capacity: the probably-no-op SetCapacity()
  // call is not that fast.

  // Iterate through attrName by character to form property name.
  // If there is a sequence of "-" followed by a character in the range "a" to
  // "z" then replace with upper case letter.
  // Otherwise append character to property name.
  for (; cur < end; ++cur) {
    const char16_t* next = cur + 1;
    if (char16_t('-') == *cur && next < end && char16_t('a') <= *next &&
        *next <= char16_t('z')) {
      // Upper case the lower case letters that follow a "-".
      aResult.Append(*next - 'a' + 'A');
      // Consume character to account for "-" character.
      ++cur;
    } else {
      // Simply append character if camel case is not necessary.
      aResult.Append(*cur);
    }
  }

  return true;
}

void nsDOMStringMap::AttributeChanged(Element* aElement, int32_t aNameSpaceID,
                                      nsAtom* aAttribute, AttrModType aModType,
                                      const nsAttrValue* aOldValue) {
  if (IsAdditionOrRemoval(aModType) && aNameSpaceID == kNameSpaceID_None &&
      StringBeginsWith(nsDependentAtomString(aAttribute), u"data-"_ns)) {
    ++mExpandoAndGeneration.generation;
  }
}
