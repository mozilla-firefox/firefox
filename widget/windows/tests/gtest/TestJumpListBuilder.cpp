/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <objectarray.h>
#include <propkey.h>
#include <propvarutil.h>
#include <shobjidl.h>
#include <windows.h>

#include "JumpListBuilder.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mozilla/CmdLineAndEnvUtils.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/PromiseNativeHandler.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/ToJSValue.h"
#include "mozilla/dom/WindowsJumpListShortcutDescriptionBinding.h"

using namespace mozilla;
using namespace testing;
using mozilla::dom::AutoJSAPI;
using mozilla::dom::Promise;
using mozilla::dom::PromiseNativeHandler;
using mozilla::dom::ToJSValue;
using mozilla::dom::WindowsJumpListShortcutDescription;
using mozilla::widget::JumpListBackend;
using mozilla::widget::JumpListBuilder;

/**
 * GMock matcher that ensures that two LPCWSTRs match.
 */
MATCHER_P(LPCWSTREq, value, "The equivalent of StrEq for LPCWSTRs") {
  return (wcscmp(arg, value)) == 0;
}

/**
 * GMock matcher that ensures that a IObjectArray* contains nsIShellLinkW's
 * that match an equivalent set of nsTArray<WindowsJumpListShortcutDescriptions>
 */
MATCHER_P(ShellLinksEq, descs,
          "Comparing generated IShellLinkW with "
          "WindowsJumpListShortcutDescription definitions") {
  uint32_t count = 0;
  HRESULT hr = arg->GetCount(&count);
  if (FAILED(hr) || count != descs->Length()) {
    return false;
  }

  for (uint32_t i = 0; i < descs->Length(); ++i) {
    RefPtr<IShellLinkW> link;
    if (FAILED(arg->GetAt(i, IID_IShellLinkW,
                          static_cast<void**>(getter_AddRefs(link))))) {
      return false;
    }

    if (!link) {
      return false;
    }

    const WindowsJumpListShortcutDescription& desc = descs->ElementAt(i);

    // We'll now compare each member of the WindowsJumpListShortcutDescription
    // with what is stored in the IShellLink.

    // WindowsJumpListShortcutDescription.title
    IPropertyStore* propStore = nullptr;
    hr = link->QueryInterface(IID_IPropertyStore, (LPVOID*)&propStore);
    if (FAILED(hr)) {
      return false;
    }

    PROPVARIANT pv;
    hr = propStore->GetValue(PKEY_Title, &pv);
    if (FAILED(hr)) {
      return false;
    }

    wchar_t title[PKEYSTR_MAX];
    hr = PropVariantToString(pv, title, PKEYSTR_MAX);
    if (FAILED(hr)) {
      return false;
    }

    if (!desc.mTitle.Equals(title)) {
      return false;
    }

    // WindowsJumpListShortcutDescription.path
    wchar_t pathBuf[MAX_PATH];
    hr = link->GetPath(pathBuf, MAX_PATH, nullptr, SLGP_SHORTPATH);
    if (FAILED(hr)) {
      return false;
    }

    if (!desc.mPath.Equals(pathBuf)) {
      return false;
    }

    // WindowsJumpListShortcutDescription.arguments (optional)
    wchar_t argsBuf[MAX_PATH];
    hr = link->GetArguments(argsBuf, MAX_PATH);
    if (FAILED(hr)) {
      return false;
    }

    if (desc.mArguments.WasPassed()) {
      mozilla::CommandLineParserWin<char16_t> parser;
      parser.HandleCommandLine(nsDependentString(argsBuf));

      const mozilla::dom::Sequence<nsString>& args = desc.mArguments.Value();

      bool same = std::equal(
          parser.Argv(), parser.Argv() + parser.Argc(), args.cbegin(),
          args.cend(), [](const char16_t* aParsed, const nsAString& aExpected) {
            return aExpected == nsDependentString(aParsed);
          });
      if (!same) {
        return false;
      }
    } else {
      // Otherwise, the arguments should be empty.
      if (wcsnlen(argsBuf, MAX_PATH) != 0) {
        return false;
      }
    }

    // WindowsJumpListShortcutDescription.description
    wchar_t descBuf[MAX_PATH];
    hr = link->GetDescription(descBuf, MAX_PATH);
    if (FAILED(hr)) {
      return false;
    }

    if (!desc.mDescription.Equals(descBuf)) {
      return false;
    }

    // WindowsJumpListShortcutDescription.iconPath and
    // WindowsJumpListShortcutDescription.fallbackIconIndex
    int iconIdx = 0;
    wchar_t iconPathBuf[MAX_PATH];
    hr = link->GetIconLocation(iconPathBuf, MAX_PATH, &iconIdx);
    if (FAILED(hr)) {
      return false;
    }

    if (desc.mIconPath.WasPassed() && !desc.mIconPath.Value().IsEmpty()) {
      // If the WindowsJumpListShortcutDescription supplied an iconPath,
      // then it should match iconPathBuf and have an icon index of 0.
      if (!desc.mIconPath.Value().Equals(iconPathBuf) || iconIdx != 0) {
        return false;
      }
    } else {
      // Otherwise, the iconPathBuf should equal the
      // WindowsJumpListShortcutDescription path, and the iconIdx should match
      // the fallbackIconIndex.
      if (!desc.mPath.Equals(iconPathBuf) ||
          desc.mFallbackIconIndex != iconIdx) {
        return false;
      }
    }
  }

  return true;
}

/**
 * This is a helper class that allows our tests to wait for a native DOM Promise
 * to resolve, and get the JS::Value that the Promise resolves with. This is
 * expected to run on the main thread.
 */
class WaitForResolver : public PromiseNativeHandler {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(WaitForResolver, override)

  NS_IMETHODIMP QueryInterface(REFNSIID aIID, void** aInstancePtr) override {
    nsresult rv = NS_ERROR_UNEXPECTED;
    NS_INTERFACE_TABLE0(WaitForResolver)

    return rv;
  }

  WaitForResolver() : mIsDone(false) {}

  void ResolvedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                        ErrorResult& aError) override {
    mResult = aValue;
    mIsDone = true;
  }

  void RejectedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                        ErrorResult& aError) override {
    ASSERT_TRUE(false);  // Should never reach here.
  }

  /**
   * Spins a nested event loop and blocks until the Promise has resolved.
   */
  void SpinUntilResolved() {
    SpinEventLoopUntil("WaitForResolver::SpinUntilResolved"_ns,
                       [&]() { return mIsDone; });
  }

  /**
   * Spins a nested event loop and blocks until the Promise has resolved,
   * after which the JS::Value that the Promise resolves with is returned via
   * the aRetval outparam.
   *
   * @param {JS::MutableHandle<JS::Value>} aRetval
   *   The outparam for the JS::Value that the Promise resolves with.
   */
  void SpinUntilResolvedWithResult(JS::MutableHandle<JS::Value> aRetval) {
    SpinEventLoopUntil("WaitForResolver::SpinUntilResolved"_ns,
                       [&]() { return mIsDone; });
    aRetval.set(mResult);
  }

 private:
  virtual ~WaitForResolver() = default;

  JS::Heap<JS::Value> mResult;
  bool mIsDone;
};

/**
 * An implementation of JumpListBackend that is instrumented using the GMock
 * framework to record calls. Unlike the NativeJumpListBackend, this backend
 * is expected to be instantiated on the main thread and passed as an argument
 * to the JumpListBuilder's worker thread. Testers should wait for the methods
 * that call these functions to resolve their Promises before checking the
 * recorded values.
 */
class TestingJumpListBackend : public JumpListBackend {
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(JumpListBackend, override)

  TestingJumpListBackend() : mMonitor("TestingJumpListBackend::mMonitor") {}

  virtual bool IsAvailable() override { return true; }

  MOCK_METHOD(HRESULT, SetAppID, (LPCWSTR));
  MOCK_METHOD(HRESULT, BeginList, (UINT*, REFIID, void**));
  MOCK_METHOD(HRESULT, AddUserTasks, (IObjectArray*));
  MOCK_METHOD(HRESULT, AppendCategory, (LPCWSTR, IObjectArray*));
  MOCK_METHOD(HRESULT, CommitList, ());
  MOCK_METHOD(HRESULT, AbortList, ());
  MOCK_METHOD(HRESULT, DeleteList, (LPCWSTR));

  virtual HRESULT AppendKnownCategory(KNOWNDESTCATEGORY category) override {
    return 0;
  }

  // In one case (construction), an operation occurs off of the main thread that
  // we must wait for without an associated Promise.
  Monitor& GetMonitor() { return mMonitor; }

 protected:
  virtual ~TestingJumpListBackend() override {};

 private:
  Monitor mMonitor;
};

/**
 * A helper function that creates some fake WindowsJumpListShortcutDescription
 * objects as well as JS::Value representations of those objects. These are
 * returned to the caller through outparams.
 *
 * @param {JSContext*} aCx
 *   The current JSContext in the execution environment.
 * @param {uint32_t} howMany
 *   The number of WindowsJumpListShortcutDescriptions to generate.
 * @param {boolean} longDescription
 *   True if the description should be greater than MAX_PATH (260 characters).
 * @param {nsTArray<WindowsJumpListShortcutDescription>&} aArray
 *   The outparam for the array of generated
 * WindowsJumpListShortcutDescriptions.
 * @param {JS::Handle<JSObject*>} aJSArrayObj
 *   The outparam for the array of JS::Value's representing the generated
 *   WindowsJumpListShortcutDescriptions.
 */
void GenerateWindowsJumpListShortcutDescriptions(
    JSContext* aCx, uint32_t howMany, bool longDescription,
    nsTArray<WindowsJumpListShortcutDescription>& aArray,
    JS::Handle<JSObject*> aJSArrayObj) {
  for (uint32_t i = 0; i < howMany; ++i) {
    WindowsJumpListShortcutDescription desc;
    nsAutoString title(u"Test Task #");
    title.AppendInt(i);
    desc.mTitle = title;

    nsAutoString path(u"C:\\Some\\Test\\Path.exe");
    desc.mPath = path;
    nsAutoString description;

    if (longDescription) {
      description.AppendPrintf(
          "For item #%i, this is a very very very very VERY VERY very very "
          "very very very very very very very very very very VERY VERY very "
          "very very very very very very very very very very very VERY VERY "
          "very very very very very very very very very very very very VERY "
          "VERY very very very very very very very very very very very very "
          "VERY VERY very very very very very very very very very very very "
          "very VERY VERY very very very very very very very very long test "
          "description for an item",
          i);
    } else {
      description.AppendPrintf("This is a test description for an item #%i", i);
    }

    desc.mDescription = description;
    desc.mFallbackIconIndex = 0;

    if (!(i % 2)) {
      desc.mArguments.Construct(nsTArray<nsString>{
          u"-arg1"_ns, u"argument with a space"_ns, u"-arg3"_ns});
      nsAutoString iconPath(u"C:\\Some\\icon.png");
      desc.mIconPath.Construct(iconPath);
    }

    aArray.AppendElement(desc);
    JS::Rooted<JS::Value> descJSValue(aCx);
    ASSERT_TRUE(ToJSValue(aCx, desc, &descJSValue));

    MOZ_ALWAYS_TRUE(JS_SetElement(aCx, aJSArrayObj, i, descJSValue));
  }
}

/**
 * Tests construction and that the application ID is properly passed to the
 * backend.
 */
TEST(JumpListBuilder, Construction)
{
  RefPtr<StrictMock<TestingJumpListBackend>> testBackend =
      new StrictMock<TestingJumpListBackend>();
  ASSERT_TRUE(testBackend);

  nsAutoString aumid(u"TestApplicationID");
  LPCWSTR passedID = aumid.get();
  // Construction of our class (or any class of that matter) does not return a
  // Promise that we can wait on to ensure that the background thread got the
  // right information. We therefore use a monitor on the testing backend as
  // well as an EXPECT_CALL to block execution of the test until the background
  // work has completed.
  Monitor& mon = testBackend->GetMonitor();
  MonitorAutoLock lock(mon);
  EXPECT_CALL(*testBackend, SetAppID(LPCWSTREq(passedID))).WillOnce([&mon] {
    MonitorAutoLock lock(mon);
    mon.Notify();
    return S_OK;
  });

  nsCOMPtr<nsIJumpListBuilder> builder =
      new JumpListBuilder(aumid, /* aPrivateBrowsing */ false, testBackend);
  ASSERT_TRUE(builder);

  // This is the amount of time that we will wait for the background thread to
  // respond before considering it a timeout failure.
  const int kWaitTimeoutMs = 5000;

  ASSERT_TRUE(mon.Wait(TimeDuration::FromMilliseconds(kWaitTimeoutMs)) !=
              CVStatus::Timeout);
}

/**
 * Tests calling CheckForRemovals and receiving a series of removed jump list
 * entries. Calling CheckForRemovals should call the following methods on the
 * backend, in order:
 *
 * - SetAppID
 * - AbortList
 * - BeginList
 * - AbortList
 */
TEST(JumpListBuilder, CheckForRemovals)
{
  RefPtr<StrictMock<TestingJumpListBackend>> testBackend =
      new StrictMock<TestingJumpListBackend>();
  nsAutoString aumid(u"TestApplicationID");
  // We set up this expectation here because SetAppID will be called soon
  // after construction of the JumpListBuilder via the background thread.
  EXPECT_CALL(*testBackend, SetAppID(_)).Times(1);

  nsCOMPtr<nsIJumpListBuilder> builder =
      new JumpListBuilder(aumid, /* aPrivateBrowsing */ false, testBackend);
  ASSERT_TRUE(builder);

  EXPECT_CALL(*testBackend, AbortList()).Times(2);

  // Let's prepare BeginList to return a two entry collection of IShellLinks.
  // The first IShellLink will have the URL be https://example.com, the second
  // will have the URL be https://mozilla.org.
  EXPECT_CALL(*testBackend, BeginList)
      .WillOnce([](UINT* pcMinSlots, REFIID riid, void** ppv) {
        RefPtr<IObjectCollection> collection;
        HRESULT hr = CoCreateInstance(
            CLSID_EnumerableObjectCollection, nullptr, CLSCTX_INPROC_SERVER,
            IID_IObjectCollection, getter_AddRefs(collection));
        MOZ_RELEASE_ASSERT(SUCCEEDED(hr));

        RefPtr<IShellLinkW> link;
        hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                              IID_IShellLinkW, getter_AddRefs(link));
        MOZ_RELEASE_ASSERT(SUCCEEDED(hr));

        nsAutoString firstLinkHref(u"https://example.com"_ns);
        link->SetArguments(firstLinkHref.get());

        nsAutoString appPath(u"C:\\Tmp\\firefox.exe"_ns);
        link->SetIconLocation(appPath.get(), 0);

        collection->AddObject(link);

        // AddObject stores the interface pointer rather than copying the
        // link's values, so the second entry needs its own IShellLink.
        RefPtr<IShellLinkW> secondLink;
        hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                              IID_IShellLinkW, getter_AddRefs(secondLink));
        MOZ_RELEASE_ASSERT(SUCCEEDED(hr));

        nsAutoString secondLinkHref(u"https://mozilla.org"_ns);
        secondLink->SetArguments(secondLinkHref.get());
        secondLink->SetIconLocation(appPath.get(), 0);
        collection->AddObject(secondLink);

        RefPtr<IObjectArray> pArray;
        hr = collection->QueryInterface(IID_IObjectArray,
                                        getter_AddRefs(pArray));
        MOZ_RELEASE_ASSERT(SUCCEEDED(hr));

        *ppv = static_cast<IObjectArray*>(pArray);
        (static_cast<IUnknown*>(*ppv))->AddRef();

        // This is the default value to return, according to
        // https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-icustomdestinationlist-beginlist
        *pcMinSlots = 10;

        return S_OK;
      });

  AutoJSAPI jsapi;
  MOZ_ALWAYS_TRUE(jsapi.Init(xpc::PrivilegedJunkScope()));
  JSContext* cx = jsapi.cx();
  RefPtr<Promise> promise;
  nsresult rv = builder->CheckForRemovals(cx, getter_AddRefs(promise));
  ASSERT_TRUE(NS_SUCCEEDED(rv));
  ASSERT_TRUE(promise);

  auto resolver = MakeRefPtr<WaitForResolver>();
  promise->AppendNativeHandler(resolver);
  JS::Rooted<JS::Value> result(cx);
  resolver->SpinUntilResolvedWithResult(&result);

  ASSERT_TRUE(result.isObject());
  JS::Rooted<JSObject*> obj(cx, result.toObjectOrNull());

  bool isArray;
  ASSERT_TRUE(JS::IsArrayObject(cx, obj, &isArray));
  ASSERT_TRUE(isArray);

  // We should expect to see 2 URL strings returned in the array.
  uint32_t length = 0;
  ASSERT_TRUE(JS::GetArrayLength(cx, obj, &length));
  ASSERT_EQ(length, 2U);

  // The first one should be https://example.com
  JS::Rooted<JS::Value> firstURLValue(cx);
  ASSERT_TRUE(JS_GetElement(cx, obj, 0, &firstURLValue));
  JS::Rooted<JSString*> firstURLJSString(cx, firstURLValue.toString());
  nsAutoJSString firstURLAutoString;
  ASSERT_TRUE(firstURLAutoString.init(cx, firstURLJSString));

  ASSERT_TRUE(firstURLAutoString.EqualsLiteral("https://example.com"));

  // The second one should be https://mozilla.org
  JS::Rooted<JS::Value> secondURLValue(cx);
  ASSERT_TRUE(JS_GetElement(cx, obj, 1, &secondURLValue));
  JS::Rooted<JSString*> secondURLJSString(cx, secondURLValue.toString());
  nsAutoJSString secondURLAutoString;
  ASSERT_TRUE(secondURLAutoString.init(cx, secondURLJSString));

  ASSERT_TRUE(secondURLAutoString.EqualsLiteral("https://mozilla.org"));
}

/**
 * Tests calling CheckForRemovals and receiving a jump list entry with a very
 * long URL doesn't result in JumpListBuilder truncating the URL before handing
 * it back to the caller. Expects the following calls in order:
 *
 * - SetAppID
 * - AbortList
 * - BeginList
 * - AbortList
 */
TEST(JumpListBuilder, CheckForRemovalsLongURL)
{
  RefPtr<StrictMock<TestingJumpListBackend>> testBackend =
      new StrictMock<TestingJumpListBackend>();
  nsAutoString aumid(u"TestApplicationID");
  // We set up this expectation here because SetAppID will be called soon
  // after construction of the JumpListBuilder via the background thread.
  EXPECT_CALL(*testBackend, SetAppID(_)).Times(1);

  nsCOMPtr<nsIJumpListBuilder> builder =
      new JumpListBuilder(aumid, /* aPrivateBrowsing */ false, testBackend);
  ASSERT_TRUE(builder);

  EXPECT_CALL(*testBackend, AbortList()).Times(2);

  constexpr static const nsLiteralString veryLongHref(
      u"https://example.verylongurl.test/first/second/third/fourth/fifth/"
      "sixth/seventh/eighth/ninth/tenth/eleventh/twelfth/thirteenth/"
      "fourteenth/fifteenth-path-item/some/more/junk/after/that/more/more/"
      "more/more/more/more/more/more/more/more/more/more/more/more/more/more/"
      "more/more/more/more/more/more/more/more/more/more/more"_ns);
  // This test ensures that URLs longer than MAX_PATH do not get truncated by
  // JumpListBuilder or one of its utilities, so we must ensure that the static
  // URL we just defined is actually longer than MAX_PATH.
  static_assert(veryLongHref.Length() > MAX_PATH);

  // Let's prepare BeginList to return a one entry collection of IShellLinks.
  // The IShellLink will have the URL be very long - over MAX_PATH characters.
  EXPECT_CALL(*testBackend, BeginList)
      .WillOnce([](UINT* pcMinSlots, REFIID riid, void** ppv) {
        RefPtr<IObjectCollection> collection;
        HRESULT hr = CoCreateInstance(
            CLSID_EnumerableObjectCollection, nullptr, CLSCTX_INPROC_SERVER,
            IID_IObjectCollection, getter_AddRefs(collection));
        MOZ_RELEASE_ASSERT(SUCCEEDED(hr));

        RefPtr<IShellLinkW> link;
        hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                              IID_IShellLinkW, getter_AddRefs(link));
        MOZ_RELEASE_ASSERT(SUCCEEDED(hr));

        link->SetArguments(veryLongHref.get());

        nsAutoString appPath(u"C:\\Tmp\\firefox.exe"_ns);
        link->SetIconLocation(appPath.get(), 0);

        collection->AddObject(link);

        RefPtr<IObjectArray> pArray;
        hr = collection->QueryInterface(IID_IObjectArray,
                                        getter_AddRefs(pArray));
        MOZ_RELEASE_ASSERT(SUCCEEDED(hr));

        *ppv = static_cast<IObjectArray*>(pArray);
        (static_cast<IUnknown*>(*ppv))->AddRef();

        // This is the default value to return, according to
        // https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-icustomdestinationlist-beginlist
        *pcMinSlots = 10;

        return S_OK;
      });

  AutoJSAPI jsapi;
  MOZ_ALWAYS_TRUE(jsapi.Init(xpc::PrivilegedJunkScope()));
  JSContext* cx = jsapi.cx();
  RefPtr<Promise> promise;
  nsresult rv = builder->CheckForRemovals(cx, getter_AddRefs(promise));
  ASSERT_TRUE(NS_SUCCEEDED(rv));
  ASSERT_TRUE(promise);

  auto resolver = MakeRefPtr<WaitForResolver>();
  promise->AppendNativeHandler(resolver);
  JS::Rooted<JS::Value> result(cx);
  resolver->SpinUntilResolvedWithResult(&result);

  ASSERT_TRUE(result.isObject());
  JS::Rooted<JSObject*> obj(cx, result.toObjectOrNull());

  bool isArray;
  ASSERT_TRUE(JS::IsArrayObject(cx, obj, &isArray));
  ASSERT_TRUE(isArray);

  // We should expect to see 1 URL string returned in the array.
  uint32_t length = 0;
  ASSERT_TRUE(JS::GetArrayLength(cx, obj, &length));
  ASSERT_EQ(length, 1U);

  // The URL should match veryLongHref
  JS::Rooted<JS::Value> returnedURLValue(cx);
  ASSERT_TRUE(JS_GetElement(cx, obj, 0, &returnedURLValue));
  JS::Rooted<JSString*> returnedURLValueJSString(cx,
                                                 returnedURLValue.toString());
  nsAutoJSString returnedURLValueAutoString;
  ASSERT_TRUE(returnedURLValueAutoString.init(cx, returnedURLValueJSString));

  ASSERT_TRUE(returnedURLValueAutoString.Equals(veryLongHref));
}

/**
 * Tests calling PopulateJumpList with empty arguments, which should call the
 * following methods on the backend, in order:
 *
 * - SetAppID
 * - AbortList
 * - BeginList
 * - CommitList
 *
 * This should result in an empty jump list for the user.
 */
TEST(JumpListBuilder, PopulateJumpListEmpty)
{
  RefPtr<StrictMock<TestingJumpListBackend>> testBackend =
      new StrictMock<TestingJumpListBackend>();
  nsAutoString aumid(u"TestApplicationID");
  // We set up this expectation here because SetAppID will be called soon
  // after construction of the JumpListBuilder via the background thread.
  EXPECT_CALL(*testBackend, SetAppID(_)).Times(1);

  nsCOMPtr<nsIJumpListBuilder> builder =
      new JumpListBuilder(aumid, /* aPrivateBrowsing */ false, testBackend);
  ASSERT_TRUE(builder);

  AutoJSAPI jsapi;
  MOZ_ALWAYS_TRUE(jsapi.Init(xpc::PrivilegedJunkScope()));
  JSContext* cx = jsapi.cx();
  RefPtr<Promise> promise;

  JS::Rooted<JSObject*> taskDescsObj(cx, JS::NewArrayObject(cx, 0));
  JS::Rooted<JS::Value> taskDescsJSVal(cx, JS::ObjectValue(*taskDescsObj));

  nsAutoString customTitle(u"");

  JS::Rooted<JSObject*> customDescsObj(cx, JS::NewArrayObject(cx, 0));
  JS::Rooted<JS::Value> customDescsJSVal(cx, JS::ObjectValue(*customDescsObj));

  EXPECT_CALL(*testBackend, AbortList()).Times(1);
  EXPECT_CALL(*testBackend, BeginList(_, _, _)).Times(1);
  EXPECT_CALL(*testBackend, CommitList()).Times(1);
  EXPECT_CALL(*testBackend, DeleteList(_)).Times(0);

  nsresult rv =
      builder->PopulateJumpList(taskDescsJSVal, customTitle, customDescsJSVal,
                                cx, getter_AddRefs(promise));
  ASSERT_TRUE(NS_SUCCEEDED(rv));
  ASSERT_TRUE(promise);

  auto resolver = MakeRefPtr<WaitForResolver>();
  promise->AppendNativeHandler(resolver);
  JS::Rooted<JS::Value> result(cx);
  resolver->SpinUntilResolved();
}

/**
 * Tests calling PopulateJumpList with only tasks, and no custom items.
 * This should call the following methods on the backend, in order:
 *
 * - SetAppID
 * - AbortList
 * - BeginList
 * - AddUserTasks
 * - CommitList
 *
 * This should result in a jump list with just tasks shown to the user, and
 * no custom section.
 */
TEST(JumpListBuilder, PopulateJumpListOnlyTasks)
{
  RefPtr<StrictMock<TestingJumpListBackend>> testBackend =
      new StrictMock<TestingJumpListBackend>();
  nsAutoString aumid(u"TestApplicationID");
  // We set up this expectation here because SetAppID will be called soon
  // after construction of the JumpListBuilder via the background thread.
  EXPECT_CALL(*testBackend, SetAppID(_)).Times(1);

  nsCOMPtr<nsIJumpListBuilder> builder =
      new JumpListBuilder(aumid, /* aPrivateBrowsing */ false, testBackend);
  ASSERT_TRUE(builder);

  AutoJSAPI jsapi;
  MOZ_ALWAYS_TRUE(jsapi.Init(xpc::PrivilegedJunkScope()));
  JSContext* cx = jsapi.cx();
  RefPtr<Promise> promise;

  JS::Rooted<JSObject*> taskDescsObj(cx, JS::NewArrayObject(cx, 0));
  JS::Rooted<JS::Value> taskDescsJSVal(cx, JS::ObjectValue(*taskDescsObj));
  nsTArray<WindowsJumpListShortcutDescription> taskDescs;
  GenerateWindowsJumpListShortcutDescriptions(cx, 2, false, taskDescs,
                                              taskDescsObj);

  nsAutoString customTitle(u"");
  JS::Rooted<JSObject*> customDescsObj(cx, JS::NewArrayObject(cx, 0));
  JS::Rooted<JS::Value> customDescsJSVal(cx, JS::ObjectValue(*customDescsObj));

  EXPECT_CALL(*testBackend, AbortList()).Times(1);
  EXPECT_CALL(*testBackend, BeginList(_, _, _)).Times(1);
  EXPECT_CALL(*testBackend, AddUserTasks(ShellLinksEq(&taskDescs))).Times(1);

  EXPECT_CALL(*testBackend, AppendCategory(_, _)).Times(0);
  EXPECT_CALL(*testBackend, CommitList()).Times(1);
  EXPECT_CALL(*testBackend, DeleteList(_)).Times(0);

  nsresult rv =
      builder->PopulateJumpList(taskDescsJSVal, customTitle, customDescsJSVal,
                                cx, getter_AddRefs(promise));
  ASSERT_TRUE(NS_SUCCEEDED(rv));
  ASSERT_TRUE(promise);

  auto resolver = MakeRefPtr<WaitForResolver>();
  promise->AppendNativeHandler(resolver);
  JS::Rooted<JS::Value> result(cx);
  resolver->SpinUntilResolved();
}

/**
 * Tests calling PopulateJumpList with only custom items, and no tasks.
 * This should call the following methods on the backend, in order:
 *
 * - SetAppID
 * - AbortList
 * - BeginList
 * - AppendCategory
 * - CommitList
 *
 * This should result in a jump list with just custom items shown to the user,
 * and no tasks.
 */
TEST(JumpListBuilder, PopulateJumpListOnlyCustomItems)
{
  RefPtr<StrictMock<TestingJumpListBackend>> testBackend =
      new StrictMock<TestingJumpListBackend>();
  nsAutoString aumid(u"TestApplicationID");
  // We set up this expectation here because SetAppID will be called soon
  // after construction of the JumpListBuilder via the background thread.
  EXPECT_CALL(*testBackend, SetAppID(_)).Times(1);

  nsCOMPtr<nsIJumpListBuilder> builder =
      new JumpListBuilder(aumid, /* aPrivateBrowsing */ false, testBackend);
  ASSERT_TRUE(builder);

  AutoJSAPI jsapi;
  MOZ_ALWAYS_TRUE(jsapi.Init(xpc::PrivilegedJunkScope()));
  JSContext* cx = jsapi.cx();
  RefPtr<Promise> promise;

  JS::Rooted<JSObject*> taskDescsObj(cx, JS::NewArrayObject(cx, 0));
  JS::Rooted<JS::Value> taskDescsJSVal(cx);
  taskDescsJSVal.setObject(*taskDescsObj);

  nsAutoString customTitle(u"My custom title");

  JS::Rooted<JSObject*> customDescsObj(cx, JS::NewArrayObject(cx, 0));
  JS::Rooted<JS::Value> customDescsJSVal(cx, JS::ObjectValue(*customDescsObj));
  nsTArray<WindowsJumpListShortcutDescription> descs;
  GenerateWindowsJumpListShortcutDescriptions(cx, 2, false, descs,
                                              customDescsObj);

  EXPECT_CALL(*testBackend, AbortList()).Times(1);
  EXPECT_CALL(*testBackend, BeginList(_, _, _)).Times(1);
  EXPECT_CALL(*testBackend, AddUserTasks(_)).Times(0);

  EXPECT_CALL(*testBackend, AppendCategory(LPCWSTREq(customTitle.get()),
                                           ShellLinksEq(&descs)))
      .Times(1);
  EXPECT_CALL(*testBackend, CommitList()).Times(1);
  EXPECT_CALL(*testBackend, DeleteList(_)).Times(0);

  nsresult rv =
      builder->PopulateJumpList(taskDescsJSVal, customTitle, customDescsJSVal,
                                cx, getter_AddRefs(promise));
  ASSERT_TRUE(NS_SUCCEEDED(rv));
  ASSERT_TRUE(promise);

  auto resolver = MakeRefPtr<WaitForResolver>();
  promise->AppendNativeHandler(resolver);
  JS::Rooted<JS::Value> result(cx);
  resolver->SpinUntilResolved();
}

/**
 * Tests calling PopulateJumpList with tasks and custom items.
 * This should call the following methods on the backend, in order:
 *
 * - SetAppID
 * - AbortList
 * - BeginList
 * - AddUserTasks
 * - AppendCategory
 * - CommitList
 *
 * This should result in a jump list with both built-in tasks as well as
 * custom tasks with a custom label.
 */
TEST(JumpListBuilder, PopulateJumpList)
{
  RefPtr<StrictMock<TestingJumpListBackend>> testBackend =
      new StrictMock<TestingJumpListBackend>();
  nsAutoString aumid(u"TestApplicationID");
  // We set up this expectation here because SetAppID will be called soon
  // after construction of the JumpListBuilder via the background thread.
  EXPECT_CALL(*testBackend, SetAppID(_)).Times(1);

  nsCOMPtr<nsIJumpListBuilder> builder =
      new JumpListBuilder(aumid, /* aPrivateBrowsing */ false, testBackend);
  ASSERT_TRUE(builder);

  AutoJSAPI jsapi;
  MOZ_ALWAYS_TRUE(jsapi.Init(xpc::PrivilegedJunkScope()));
  JSContext* cx = jsapi.cx();
  RefPtr<Promise> promise;

  JS::Rooted<JSObject*> taskDescsObj(cx, JS::NewArrayObject(cx, 0));
  JS::Rooted<JS::Value> taskDescsJSVal(cx, JS::ObjectValue(*taskDescsObj));
  nsTArray<WindowsJumpListShortcutDescription> taskDescs;
  GenerateWindowsJumpListShortcutDescriptions(cx, 2, false, taskDescs,
                                              taskDescsObj);

  nsAutoString customTitle(u"My custom title");

  JS::Rooted<JSObject*> customDescsObj(cx, JS::NewArrayObject(cx, 0));
  JS::Rooted<JS::Value> customDescsJSVal(cx, JS::ObjectValue(*customDescsObj));
  nsTArray<WindowsJumpListShortcutDescription> customDescs;
  GenerateWindowsJumpListShortcutDescriptions(cx, 2, false, customDescs,
                                              customDescsObj);

  EXPECT_CALL(*testBackend, AbortList()).Times(1);
  EXPECT_CALL(*testBackend, BeginList(_, _, _)).Times(1);
  EXPECT_CALL(*testBackend, AddUserTasks(ShellLinksEq(&taskDescs))).Times(1);

  EXPECT_CALL(*testBackend, AppendCategory(LPCWSTREq(customTitle.get()),
                                           ShellLinksEq(&customDescs)))
      .Times(1);
  EXPECT_CALL(*testBackend, CommitList()).Times(1);
  EXPECT_CALL(*testBackend, DeleteList(_)).Times(0);

  nsresult rv =
      builder->PopulateJumpList(taskDescsJSVal, customTitle, customDescsJSVal,
                                cx, getter_AddRefs(promise));
  ASSERT_TRUE(NS_SUCCEEDED(rv));
  ASSERT_TRUE(promise);

  auto resolver = MakeRefPtr<WaitForResolver>();
  promise->AppendNativeHandler(resolver);
  JS::Rooted<JS::Value> result(cx);
  resolver->SpinUntilResolved();
}

/**
 * Tests calling PopulateJumpList with tasks and custom items, but makes it so
 * that AppendCategory returns E_ACCESSDENIED, which can occur if Windows is
 * configured to not show recently opened items. The PopulateJumpList Promise
 * should still resolve.
 *
 * - SetAppID
 * - AbortList
 * - BeginList
 * - AddUserTasks
 * - AppendCategory
 * - CommitList
 *
 * This should result in a jump list with just built-in tasks.
 */
TEST(JumpListBuilder, PopulateJumpListNoOpenedItems)
{
  RefPtr<StrictMock<TestingJumpListBackend>> testBackend =
      new StrictMock<TestingJumpListBackend>();
  nsAutoString aumid(u"TestApplicationID");
  // We set up this expectation here because SetAppID will be called soon
  // after construction of the JumpListBuilder via the background thread.
  EXPECT_CALL(*testBackend, SetAppID(_)).Times(1);

  nsCOMPtr<nsIJumpListBuilder> builder =
      new JumpListBuilder(aumid, /* aPrivateBrowsing */ false, testBackend);
  ASSERT_TRUE(builder);

  AutoJSAPI jsapi;
  MOZ_ALWAYS_TRUE(jsapi.Init(xpc::PrivilegedJunkScope()));
  JSContext* cx = jsapi.cx();
  RefPtr<Promise> promise;

  JS::Rooted<JSObject*> taskDescsObj(cx, JS::NewArrayObject(cx, 0));
  JS::Rooted<JS::Value> taskDescsJSVal(cx, JS::ObjectValue(*taskDescsObj));
  nsTArray<WindowsJumpListShortcutDescription> taskDescs;
  GenerateWindowsJumpListShortcutDescriptions(cx, 2, false, taskDescs,
                                              taskDescsObj);

  nsAutoString customTitle(u"My custom title");

  JS::Rooted<JSObject*> customDescsObj(cx, JS::NewArrayObject(cx, 0));
  JS::Rooted<JS::Value> customDescsJSVal(cx, JS::ObjectValue(*customDescsObj));
  nsTArray<WindowsJumpListShortcutDescription> customDescs;
  GenerateWindowsJumpListShortcutDescriptions(cx, 2, false, customDescs,
                                              customDescsObj);

  EXPECT_CALL(*testBackend, AbortList()).Times(1);
  EXPECT_CALL(*testBackend, BeginList(_, _, _)).Times(1);
  EXPECT_CALL(*testBackend, AddUserTasks(ShellLinksEq(&taskDescs))).Times(1);

  EXPECT_CALL(*testBackend, AppendCategory(LPCWSTREq(customTitle.get()),
                                           ShellLinksEq(&customDescs)))
      .WillOnce([] { return E_ACCESSDENIED; });

  EXPECT_CALL(*testBackend, CommitList()).Times(1);
  EXPECT_CALL(*testBackend, DeleteList(_)).Times(0);

  nsresult rv =
      builder->PopulateJumpList(taskDescsJSVal, customTitle, customDescsJSVal,
                                cx, getter_AddRefs(promise));
  ASSERT_TRUE(NS_SUCCEEDED(rv));
  ASSERT_TRUE(promise);

  auto resolver = MakeRefPtr<WaitForResolver>();
  promise->AppendNativeHandler(resolver);
  JS::Rooted<JS::Value> result(cx);
  resolver->SpinUntilResolved();
}

/**
 * Tests calling ClearJumpList calls the following:
 *
 * - SetAppID
 * - DeleteList (passing the aumid)
 *
 * This results in an empty jump list for the user.
 */
TEST(JumpListBuilder, ClearJumpList)
{
  RefPtr<StrictMock<TestingJumpListBackend>> testBackend =
      new StrictMock<TestingJumpListBackend>();
  nsAutoString aumid(u"TestApplicationID");
  // We set up this expectation here because SetAppID will be called soon
  // after construction of the JumpListBuilder via the background thread.
  EXPECT_CALL(*testBackend, SetAppID(_)).Times(1);

  nsCOMPtr<nsIJumpListBuilder> builder =
      new JumpListBuilder(aumid, /* aPrivateBrowsing */ false, testBackend);
  ASSERT_TRUE(builder);

  AutoJSAPI jsapi;
  MOZ_ALWAYS_TRUE(jsapi.Init(xpc::PrivilegedJunkScope()));
  JSContext* cx = jsapi.cx();
  RefPtr<Promise> promise;

  EXPECT_CALL(*testBackend, AbortList()).Times(0);
  EXPECT_CALL(*testBackend, BeginList(_, _, _)).Times(0);
  EXPECT_CALL(*testBackend, AddUserTasks(_)).Times(0);

  EXPECT_CALL(*testBackend, AppendCategory(_, _)).Times(0);
  EXPECT_CALL(*testBackend, CommitList()).Times(0);
  EXPECT_CALL(*testBackend, DeleteList(LPCWSTREq(aumid.get()))).Times(1);

  nsresult rv = builder->ClearJumpList(cx, getter_AddRefs(promise));
  ASSERT_TRUE(NS_SUCCEEDED(rv));
  ASSERT_TRUE(promise);

  auto resolver = MakeRefPtr<WaitForResolver>();
  promise->AppendNativeHandler(resolver);
  JS::Rooted<JS::Value> result(cx);
  resolver->SpinUntilResolved();
}

/**
 * Test that a WindowsJumpListShortcutDescription with a description
 * longer than MAX_PATH gets truncated to MAX_PATH. This is because a
 * description longer than MAX_PATH will cause CommitList to fail.
 */
TEST(JumpListBuilder, TruncateDescription)
{
  RefPtr<StrictMock<TestingJumpListBackend>> testBackend =
      new StrictMock<TestingJumpListBackend>();
  nsAutoString aumid(u"TestApplicationID");
  // We set up this expectation here because SetAppID will be called soon
  // after construction of the JumpListBuilder via the background thread.
  EXPECT_CALL(*testBackend, SetAppID(_)).Times(1);

  nsCOMPtr<nsIJumpListBuilder> builder =
      new JumpListBuilder(aumid, /* aPrivateBrowsing */ false, testBackend);
  ASSERT_TRUE(builder);

  AutoJSAPI jsapi;
  MOZ_ALWAYS_TRUE(jsapi.Init(xpc::PrivilegedJunkScope()));
  JSContext* cx = jsapi.cx();
  RefPtr<Promise> promise;

  JS::Rooted<JSObject*> taskDescsObj(cx, JS::NewArrayObject(cx, 0));
  JS::Rooted<JS::Value> taskDescsJSVal(cx, JS::ObjectValue(*taskDescsObj));
  nsTArray<WindowsJumpListShortcutDescription> taskDescs;
  GenerateWindowsJumpListShortcutDescriptions(cx, 2, true, taskDescs,
                                              taskDescsObj);

  nsAutoString customTitle(u"My custom title");

  JS::Rooted<JSObject*> customDescsObj(cx, JS::NewArrayObject(cx, 0));
  JS::Rooted<JS::Value> customDescsJSVal(cx, JS::ObjectValue(*customDescsObj));
  nsTArray<WindowsJumpListShortcutDescription> customDescs;
  GenerateWindowsJumpListShortcutDescriptions(cx, 2, true, customDescs,
                                              customDescsObj);

  // We expect the long descriptions to be truncated to 260 characters, so
  // we'll truncate the descriptions here ourselves.
  for (auto& taskDesc : taskDescs) {
    taskDesc.mDescription.SetLength(MAX_PATH - 1);
  }
  for (auto& customDesc : customDescs) {
    customDesc.mDescription.SetLength(MAX_PATH - 1);
  }

  EXPECT_CALL(*testBackend, AbortList()).Times(1);
  EXPECT_CALL(*testBackend, BeginList(_, _, _)).Times(1);
  EXPECT_CALL(*testBackend, AddUserTasks(ShellLinksEq(&taskDescs))).Times(1);

  EXPECT_CALL(*testBackend, AppendCategory(LPCWSTREq(customTitle.get()),
                                           ShellLinksEq(&customDescs)))
      .Times(1);
  EXPECT_CALL(*testBackend, CommitList()).Times(1);
  EXPECT_CALL(*testBackend, DeleteList(_)).Times(0);

  nsresult rv =
      builder->PopulateJumpList(taskDescsJSVal, customTitle, customDescsJSVal,
                                cx, getter_AddRefs(promise));
  ASSERT_TRUE(NS_SUCCEEDED(rv));
  ASSERT_TRUE(promise);

  auto resolver = MakeRefPtr<WaitForResolver>();
  promise->AppendNativeHandler(resolver);
  JS::Rooted<JS::Value> result(cx);
  resolver->SpinUntilResolved();
}
