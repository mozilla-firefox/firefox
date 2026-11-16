/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_a11y_uiaRawElmProvider_h_
#define mozilla_a11y_uiaRawElmProvider_h_

#include <objbase.h>
#include <stdint.h>
#include <uiautomation.h>

#include <initializer_list>

#include "nsString.h"

template <class T>
class nsTArray;
template <class T>
class RefPtr;

namespace mozilla {
namespace a11y {

class Accessible;
enum class RelationType;

struct UiaRegistrations {
  PROPERTYID mAccessibleActions = 0;
  PROPERTYID mIsWebContentRoot = 0;
  PROPERTYID mMathML = 0;
};

/**
 * IRawElementProviderSimple implementation (maintains IAccessibleEx approach).
 */
class uiaRawElmProvider : public IAccessibleEx,
                          public IRawElementProviderSimple,
                          public IRawElementProviderFragment,
                          public IInvokeProvider,
                          public IToggleProvider,
                          public IExpandCollapseProvider,
                          public IScrollItemProvider,
                          public IValueProvider,
                          public IRangeValueProvider,
                          public ISelectionProvider,
                          public ISelectionItemProvider,
                          public ITextChildProvider {
 public:
  static constexpr enum ProviderOptions kProviderOptions =
      static_cast<enum ProviderOptions>(ProviderOptions_ServerSideProvider |
                                        ProviderOptions_UseComThreading |
                                        ProviderOptions_HasNativeIAccessible);

  static void RaiseUiaEventForGeckoEvent(Accessible* aAcc,
                                         uint32_t aGeckoEvent);
  static void RaiseUiaEventForStateChange(Accessible* aAcc, uint64_t aState,
                                          bool aEnabled);
  static void RaiseUiaNotificationEvent(Accessible* aAcc,
                                        const nsAString& aAnnouncement,
                                        uint16_t aPriority);

  // IUnknown
  STDMETHODIMP QueryInterface(REFIID aIid, void** aInterface);

  // IAccessibleEx
  virtual HRESULT STDMETHODCALLTYPE GetObjectForChild(
      /* [in] */ long aIdChild,
      /* [retval][out] */ __RPC__deref_out_opt IAccessibleEx** aAccEx);

  virtual HRESULT STDMETHODCALLTYPE GetIAccessiblePair(
      /* [out] */ __RPC__deref_out_opt IAccessible** aAcc,
      /* [out] */ __RPC__out long* aIdChild);

  virtual HRESULT STDMETHODCALLTYPE GetRuntimeId(
      /* [retval][out] */ __RPC__deref_out_opt SAFEARRAY** aRuntimeIds);

  virtual HRESULT STDMETHODCALLTYPE ConvertReturnedElement(
      /* [in] */ __RPC__in_opt IRawElementProviderSimple* aRawElmProvider,
      /* [out] */ __RPC__deref_out_opt IAccessibleEx** aAccEx);

  // IRawElementProviderSimple
  virtual /* [propget] */ HRESULT STDMETHODCALLTYPE get_ProviderOptions(
      /* [retval][out] */ __RPC__out enum ProviderOptions* aProviderOptions);

  virtual HRESULT STDMETHODCALLTYPE GetPatternProvider(
      /* [in] */ PATTERNID aPatternId,
      /* [retval][out] */ __RPC__deref_out_opt IUnknown** aPatternProvider);

  virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(
      /* [in] */ PROPERTYID aPropertyId,
      /* [retval][out] */ __RPC__out VARIANT* aPropertyValue);

  virtual /* [propget] */ HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(
      /* [retval][out] */ __RPC__deref_out_opt IRawElementProviderSimple**
          aRawElmProvider);

  // IRawElementProviderFragment
  virtual HRESULT STDMETHODCALLTYPE Navigate(
      /* [in] */ enum NavigateDirection aDirection,
      /* [retval][out] */ __RPC__deref_out_opt IRawElementProviderFragment**
          aRetVal);

  // GetRuntimeId is shared with IAccessibleEx.

  virtual HRESULT STDMETHODCALLTYPE get_BoundingRectangle(
      /* [retval][out] */ __RPC__out struct UiaRect* aRetVal);

  virtual HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(
      /* [retval][out] */ __RPC__deref_out_opt SAFEARRAY** aRetVal);

  virtual HRESULT STDMETHODCALLTYPE SetFocus(void);

  virtual /* [propget] */ HRESULT STDMETHODCALLTYPE get_FragmentRoot(
      /* [retval][out] */ __RPC__deref_out_opt IRawElementProviderFragmentRoot**
          aRetVal);

  // IInvokeProvider
  virtual HRESULT STDMETHODCALLTYPE Invoke(void);

  // IToggleProvider
  virtual HRESULT STDMETHODCALLTYPE Toggle(void);

  virtual /* [propget] */ HRESULT STDMETHODCALLTYPE get_ToggleState(
      /* [retval][out] */ __RPC__out enum ToggleState* aRetVal);

  // IExpandCollapseProvider
  virtual HRESULT STDMETHODCALLTYPE Expand(void);

  virtual HRESULT STDMETHODCALLTYPE Collapse(void);

  virtual /* [propget] */ HRESULT STDMETHODCALLTYPE get_ExpandCollapseState(
      /* [retval][out] */ __RPC__out enum ExpandCollapseState* aRetVal);

  // IScrollItemProvider
  virtual HRESULT STDMETHODCALLTYPE ScrollIntoView(void);

  // IValueProvider
  virtual HRESULT STDMETHODCALLTYPE SetValue(
      /* [in] */ __RPC__in LPCWSTR val);

  virtual /* [propget] */ HRESULT STDMETHODCALLTYPE get_Value(
      /* [retval][out] */ __RPC__deref_out_opt BSTR* pRetVal);

  virtual /* [propget] */ HRESULT STDMETHODCALLTYPE get_IsReadOnly(
      /* [retval][out] */ __RPC__out BOOL* pRetVal);

  // IRangeValueProvider
  virtual HRESULT STDMETHODCALLTYPE SetValue(
      /* [in] */ double aVal);

  virtual /* [propget] */ HRESULT STDMETHODCALLTYPE get_Value(
      /* [retval][out] */ __RPC__out double* aRetVal);

  // get_IsReadOnly is shared with IValueProvider.

  virtual /* [propget] */ HRESULT STDMETHODCALLTYPE get_Maximum(
      /* [retval][out] */ __RPC__out double* aRetVal);

  virtual /* [propget] */ HRESULT STDMETHODCALLTYPE get_Minimum(
      /* [retval][out] */ __RPC__out double* aRetVal);

  virtual /* [propget] */ HRESULT STDMETHODCALLTYPE get_LargeChange(
      /* [retval][out] */ __RPC__out double* aRetVal);

  virtual /* [propget] */ HRESULT STDMETHODCALLTYPE get_SmallChange(
      /* [retval][out] */ __RPC__out double* aRetVal);

  // ISelectionProvider
  virtual HRESULT STDMETHODCALLTYPE GetSelection(
      /* [retval][out] */ __RPC__deref_out_opt SAFEARRAY** aRetVal);

  virtual /* [propget] */ HRESULT STDMETHODCALLTYPE get_CanSelectMultiple(
      /* [retval][out] */ __RPC__out BOOL* aRetVal);

  virtual /* [propget] */ HRESULT STDMETHODCALLTYPE get_IsSelectionRequired(
      /* [retval][out] */ __RPC__out BOOL* aRetVal);

  // ISelectionItemProvider methods
  virtual HRESULT STDMETHODCALLTYPE Select(void);

  virtual HRESULT STDMETHODCALLTYPE AddToSelection(void);

  virtual HRESULT STDMETHODCALLTYPE RemoveFromSelection(void);

  virtual /* [propget] */ HRESULT STDMETHODCALLTYPE get_IsSelected(
      /* [retval][out] */ __RPC__out BOOL* aRetVal);

  virtual /* [propget] */ HRESULT STDMETHODCALLTYPE get_SelectionContainer(
      /* [retval][out] */ __RPC__deref_out_opt IRawElementProviderSimple**
          aRetVal);

  // ITextChildProvider
  virtual /* [propget] */ HRESULT STDMETHODCALLTYPE get_TextContainer(
      /* [retval][out] */ __RPC__deref_out_opt IRawElementProviderSimple**
          aRetVal);

  virtual /* [propget] */ HRESULT STDMETHODCALLTYPE get_TextRange(
      /* [retval][out] */ __RPC__deref_out_opt ITextRangeProvider** aRetVal);

 private:
  Accessible* Acc() const;
  bool IsControl();
  long GetControlType() const;
  bool HasTogglePattern();
  bool HasExpandCollapsePattern();
  bool HasValuePattern() const;
  template <class Interface>
  RefPtr<Interface> GetPatternFromDerived();
  bool HasSelectionItemPattern();
  SAFEARRAY* AccRelationsToUiaArray(
      std::initializer_list<RelationType> aTypes) const;
  Accessible* GetLabeledBy() const;
  long GetLandmarkType() const;
  void GetLocalizedLandmarkType(nsAString& aLocalized) const;
  long GetLiveSetting() const;
  bool IsWebContentRoot() const;
};

SAFEARRAY* AccessibleArrayToUiaArray(const nsTArray<Accessible*>& aAccs);

/**
 * Get ids for custom UI Automation properties and events which are not defined
 * by Windows and therefore need to be registered at runtime. This function will
 * register the properties and events the first time it is called. Thereafter,
 * cached ids will be returned, since they are valid for the lifetime of the
 * process.
 */
const UiaRegistrations& GetUiaRegistrations();

}  // namespace a11y
}  // namespace mozilla

#endif
