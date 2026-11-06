/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"
#include "js/PropertyAndElement.h"  // JS_DefineProperty
#include "jsapi.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/CycleCollectedJSContext.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/OriginAttributes.h"
#include "mozilla/Preferences.h"
#include "mozilla/RangeBoundary.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/IPCTransferable.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/SimpleGlobalObject.h"
#include "nsCOMPtr.h"
#include "nsComponentManagerUtils.h"
#include "nsContentUtils.h"
#include "nsGkAtoms.h"
#include "nsITransferable.h"
#include "nsNetUtil.h"
#include "nsTArray.h"

using namespace mozilla::dom;

struct IsURIInListMatch {
  nsLiteralCString pattern;
  bool firstMatch, secondMatch;
};

std::ostream& operator<<(std::ostream& aStream,
                         const nsContentUtils::ParsedRange& aParsedRange) {
  if (aParsedRange.Start()) {
    aStream << *aParsedRange.Start();
  }

  aStream << "-";

  if (aParsedRange.End()) {
    aStream << *aParsedRange.End();
  }

  return aStream;
}

TEST(DOM_Base_ContentUtils, IsURIInList)
{
  nsCOMPtr<nsIURI> uri, subURI;
  nsresult rv = NS_NewURI(getter_AddRefs(uri),
                          "https://example.com/path/favicon.ico#"_ns);
  ASSERT_TRUE(rv == NS_OK);

  rv = NS_NewURI(getter_AddRefs(subURI),
                 "http://sub.example.com/favicon.ico?"_ns);
  ASSERT_TRUE(rv == NS_OK);

  static constexpr IsURIInListMatch patterns[] = {
      {"bar.com,*.example.com,example.com,foo.com"_ns, true, true},
      {"bar.com,example.com,*.example.com,foo.com"_ns, true, true},
      {"*.example.com,example.com,foo.com"_ns, true, true},
      {"example.com,*.example.com,foo.com"_ns, true, true},
      {"*.example.com,example.com"_ns, true, true},
      {"example.com,*.example.com"_ns, true, true},
      {"*.example.com/,example.com/"_ns, true, true},
      {"example.com/,*.example.com/"_ns, true, true},
      {"*.example.com/pa,example.com/pa"_ns, false, false},
      {"example.com/pa,*.example.com/pa"_ns, false, false},
      {"*.example.com/pa/,example.com/pa/"_ns, false, false},
      {"example.com/pa/,*.example.com/pa/"_ns, false, false},
      {"*.example.com/path,example.com/path"_ns, false, false},
      {"example.com/path,*.example.com/path"_ns, false, false},
      {"*.example.com/path/,example.com/path/"_ns, true, false},
      {"example.com/path/,*.example.com/path/"_ns, true, false},
      {"*.example.com/favicon.ico"_ns, false, true},
      {"example.com/path/favicon.ico"_ns, true, false},
      {"*.example.com"_ns, false, true},
      {"example.com"_ns, true, false},
      {"foo.com"_ns, false, false},
      {"*.foo.com"_ns, false, false},
  };

  for (auto& entry : patterns) {
    bool result = nsContentUtils::IsURIInList(uri, entry.pattern);
    ASSERT_EQ(result, entry.firstMatch) << "Matching " << entry.pattern;

    result = nsContentUtils::IsURIInList(subURI, entry.pattern);
    ASSERT_EQ(result, entry.secondMatch) << "Matching " << entry.pattern;
  }
}

TEST(DOM_Base_ContentUtils,
     StringifyJSON_EmptyValue_UndefinedIsNullStringLiteral)
{
  JS::Rooted<JSObject*> globalObject(
      mozilla::dom::RootingCx(),
      mozilla::dom::SimpleGlobalObject::Create(
          mozilla::dom::SimpleGlobalObject::GlobalType::BindingDetail));
  mozilla::dom::AutoJSAPI jsAPI;
  ASSERT_TRUE(jsAPI.Init(globalObject));
  JSContext* cx = jsAPI.cx();
  nsAutoString serializedValue;

  ASSERT_TRUE(nsContentUtils::StringifyJSON(cx, JS::UndefinedHandleValue,
                                            serializedValue,
                                            UndefinedIsNullStringLiteral));
  ASSERT_TRUE(serializedValue.EqualsLiteral("null"));
}

TEST(DOM_Base_ContentUtils, StringifyJSON_Object_UndefinedIsNullStringLiteral)
{
  JS::Rooted<JSObject*> globalObject(
      mozilla::dom::RootingCx(),
      mozilla::dom::SimpleGlobalObject::Create(
          mozilla::dom::SimpleGlobalObject::GlobalType::BindingDetail));
  mozilla::dom::AutoJSAPI jsAPI;
  ASSERT_TRUE(jsAPI.Init(globalObject));
  JSContext* cx = jsAPI.cx();
  nsAutoString serializedValue;

  JS::Rooted<JSObject*> jsObj(cx, JS_NewPlainObject(cx));
  JS::Rooted<JSString*> valueStr(cx, JS_NewStringCopyZ(cx, "Hello World!"));
  ASSERT_TRUE(JS_DefineProperty(cx, jsObj, "key1", valueStr, JSPROP_ENUMERATE));
  JS::Rooted<JS::Value> jsValue(cx, JS::ObjectValue(*jsObj));

  ASSERT_TRUE(nsContentUtils::StringifyJSON(cx, jsValue, serializedValue,
                                            UndefinedIsNullStringLiteral));

  ASSERT_TRUE(serializedValue.EqualsLiteral("{\"key1\":\"Hello World!\"}"));
}

TEST(DOM_Base_ContentUtils, StringifyJSON_EmptyValue_UndefinedIsVoidString)
{
  JS::Rooted<JSObject*> globalObject(
      mozilla::dom::RootingCx(),
      mozilla::dom::SimpleGlobalObject::Create(
          mozilla::dom::SimpleGlobalObject::GlobalType::BindingDetail));
  mozilla::dom::AutoJSAPI jsAPI;
  ASSERT_TRUE(jsAPI.Init(globalObject));
  JSContext* cx = jsAPI.cx();
  nsAutoString serializedValue;

  ASSERT_TRUE(nsContentUtils::StringifyJSON(
      cx, JS::UndefinedHandleValue, serializedValue, UndefinedIsVoidString));

  ASSERT_TRUE(serializedValue.IsVoid());
}

TEST(DOM_Base_ContentUtils, StringifyJSON_Object_UndefinedIsVoidString)
{
  JS::Rooted<JSObject*> globalObject(
      mozilla::dom::RootingCx(),
      mozilla::dom::SimpleGlobalObject::Create(
          mozilla::dom::SimpleGlobalObject::GlobalType::BindingDetail));
  mozilla::dom::AutoJSAPI jsAPI;
  ASSERT_TRUE(jsAPI.Init(globalObject));
  JSContext* cx = jsAPI.cx();
  nsAutoString serializedValue;

  JS::Rooted<JSObject*> jsObj(cx, JS_NewPlainObject(cx));
  JS::Rooted<JSString*> valueStr(cx, JS_NewStringCopyZ(cx, "Hello World!"));
  ASSERT_TRUE(JS_DefineProperty(cx, jsObj, "key1", valueStr, JSPROP_ENUMERATE));
  JS::Rooted<JS::Value> jsValue(cx, JS::ObjectValue(*jsObj));

  ASSERT_TRUE(nsContentUtils::StringifyJSON(cx, jsValue, serializedValue,
                                            UndefinedIsVoidString));

  ASSERT_TRUE(serializedValue.EqualsLiteral("{\"key1\":\"Hello World!\"}"));
}

TEST(DOM_Base_ContentUtils, ParseSingleRangeHeader)
{
  // Parsing a simple range should succeed
  EXPECT_EQ(nsContentUtils::ParseSingleRangeRequest("bytes=0-42"_ns, false),
            mozilla::Some(nsContentUtils::ParsedRange(mozilla::Some(0),
                                                      mozilla::Some(42))));

  // Range containing a invalid rangeStart should fail
  EXPECT_EQ(nsContentUtils::ParseSingleRangeRequest("bytes= t-200"_ns, true),
            mozilla::Nothing());

  // Range containing whitespace, with allowWhitespace=false should fail.
  EXPECT_EQ(nsContentUtils::ParseSingleRangeRequest("bytes= 2-200"_ns, false),
            mozilla::Nothing());

  // Range containing whitespace, with allowWhitespace=true should succeed
  EXPECT_EQ(
      nsContentUtils::ParseSingleRangeRequest("bytes \t= 2 - 200"_ns, true),
      mozilla::Some(
          nsContentUtils::ParsedRange(mozilla::Some(2), mozilla::Some(200))));

  // Range containing invalid whitespace should fail
  EXPECT_EQ(
      nsContentUtils::ParseSingleRangeRequest("bytes \r= 2 - 200"_ns, true),
      mozilla::Nothing());

  // Range without a rangeStart should succeed
  EXPECT_EQ(nsContentUtils::ParseSingleRangeRequest("bytes\t=\t-200"_ns, true),
            mozilla::Some(nsContentUtils::ParsedRange(mozilla::Nothing(),
                                                      mozilla::Some(200))));

  // Range without a rangeEnd should succeed
  EXPECT_EQ(nsContentUtils::ParseSingleRangeRequest("bytes=55-"_ns, true),
            mozilla::Some(nsContentUtils::ParsedRange(mozilla::Some(55),
                                                      mozilla::Nothing())));

  // Range without a rangeStart or rangeEnd should fail
  EXPECT_EQ(nsContentUtils::ParseSingleRangeRequest("bytes\t=\t-"_ns, true),
            mozilla::Nothing());

  // Range with extra characters should fail
  EXPECT_EQ(nsContentUtils::ParseSingleRangeRequest("bytes=0-42 "_ns, true),
            mozilla::Nothing());

  // Range with rangeStart > rangeEnd should fail
  EXPECT_EQ(nsContentUtils::ParseSingleRangeRequest("bytes=42-0 "_ns, true),
            mozilla::Nothing());
}

TEST(DOM_Base_ContentUtils, IsAllowedNonCorsRange)
{
  EXPECT_EQ(nsContentUtils::IsAllowedNonCorsRange("bytes=-200"_ns), false);
  EXPECT_EQ(nsContentUtils::IsAllowedNonCorsRange("bytes= 200-"_ns), false);
  EXPECT_EQ(nsContentUtils::IsAllowedNonCorsRange("bytes=201-200"_ns), false);
  EXPECT_EQ(nsContentUtils::IsAllowedNonCorsRange("bytes=200-201 "_ns), false);
  EXPECT_EQ(nsContentUtils::IsAllowedNonCorsRange("bytes=200-"_ns), true);
  EXPECT_EQ(nsContentUtils::IsAllowedNonCorsRange("bytes=200-201"_ns), true);
  EXPECT_EQ(nsContentUtils::IsAllowedNonCorsRange("bytes=-200 "_ns), false);
}

TEST(DOM_Base_ContentUtils, MaybeFixIPv6Host)
{
  struct TestCase {
    const char* input;
    const char* expectedOutput;
  };

  const TestCase testCases[] = {
      // Hosts containing colons without brackets - brackets should be added
      {"2001:db8::1", "[2001:db8::1]"},  // IPv6 address
      {"::1", "[::1]"},                  // IPv6 loopback
      {"::", "[::]"},                    // IPv6 unspecified address
      // Hosts containing colons but already with brackets - should remain
      // unchanged
      {"[2001:db8::1]", "[2001:db8::1]"},
      {"[::1]", "[::1]"},
      // Hosts without colons - should remain unchanged
      {"example.com", "example.com"},
      {"localhost", "localhost"},
      {"", ""},  // Empty string
      // Single colon - length less than 2, so brackets are not added
      {":", ":"},
      // Hosts starting with '[' or ending with ']', but not both - no brackets
      // are added.
      {"[example.com", "[example.com"},
      {"example.com]", "example.com]"},
      // Hosts with misordered brackets - no brackets are added
      {"]example[", "]example["},
      // Hosts with length less than 2 - no brackets added
      {"a", "a"},
      // Hosts with colons but length less than 2 - no brackets added
      {":", ":"},
  };

  for (const auto& testCase : testCases) {
    nsCString host(testCase.input);
    nsContentUtils::MaybeFixIPv6Host(host);
    ASSERT_TRUE(host.Equals(testCase.expectedOutput))
    << "Input: '" << testCase.input << "', Expected: '"
    << testCase.expectedOutput << "', Got: '" << host.get() << "'";
  }
}

// Appends a string-typed item to `aData`. The data payload is irrelevant for
// flavor-filtering tests, but a valid union member must be set.
static void AppendStringItem(IPCTransferableData& aData, const char* aFlavor,
                             const nsAString& aValue) {
  IPCTransferableDataItem* item = aData.items().AppendElement();
  item->flavor() = nsCString(aFlavor);
  item->data() = IPCTransferableDataString{mozilla::ipc::BigBuffer(
      mozilla::AsBytes(mozilla::Span(aValue.BeginReading(), aValue.Length())))};
}

static bool TransferableHasFlavor(nsITransferable* aTransferable,
                                  const char* aFlavor) {
  nsTArray<nsCString> flavors;
  EXPECT_TRUE(
      NS_SUCCEEDED(aTransferable->FlavorsTransferableCanExport(flavors)));
  return flavors.Contains(nsCString(aFlavor));
}

static already_AddRefed<nsITransferable> NewTransferable() {
  nsCOMPtr<nsITransferable> transferable =
      do_CreateInstance("@mozilla.org/widget/transferable;1");
  EXPECT_TRUE(transferable);
  if (transferable) {
    EXPECT_TRUE(NS_SUCCEEDED(transferable->Init(nullptr)));
  }
  return transferable.forget();
}

// When filtering is requested, kFilePromiseMime is a "file promise" flavor that
// must be stripped, while ordinary known flavors are kept.
TEST(DOM_Base_ContentUtils, IPCTransferableDataToTransferable)
{
  // On Windows the file-promise flavors are only stripped when
  // clipboard.imageAsFile.enabled is false (its default). Pin it so the test is
  // deterministic across platforms.
  mozilla::Preferences::SetBool("clipboard.imageAsFile.enabled", false);

  IPCTransferableData data;
  AppendStringItem(data, kTextMime, u"hello"_ns);
  AppendStringItem(data, kFilePromiseMime, u"promise"_ns);
  AppendStringItem(data, kFilePromiseURLMime, u"promiseURL"_ns);
  AppendStringItem(data, kFilePromiseDestFilename, u"promiseDestFilename"_ns);
  AppendStringItem(data, kFilePromiseDirectoryMime, u"promiseDirectory"_ns);

  nsCOMPtr<nsITransferable> transferable = NewTransferable();
  ASSERT_TRUE(transferable);

  ASSERT_TRUE(NS_SUCCEEDED(nsContentUtils::IPCTransferableDataToTransferable(
      data, /* aAddDataFlavor */ true, transferable,
      /* aFilterUnknownFlavors */ true)));

  EXPECT_TRUE(TransferableHasFlavor(transferable, kTextMime))
      << "known flavor should be preserved";
  EXPECT_FALSE(TransferableHasFlavor(transferable, kFilePromiseMime))
      << "kFilePromiseMime should be stripped when filtering";
  EXPECT_FALSE(TransferableHasFlavor(transferable, kFilePromiseURLMime))
      << "kFilePromiseURLMime should be stripped when filtering";
  EXPECT_FALSE(TransferableHasFlavor(transferable, kFilePromiseDestFilename))
      << "kFilePromiseDestFilename should be stripped when filtering";
  EXPECT_FALSE(TransferableHasFlavor(transferable, kFilePromiseDirectoryMime))
      << "kFilePromiseDirectoryMime should be stripped when filtering";

  transferable = NewTransferable();
  ASSERT_TRUE(transferable);

  // Test without filtering.
  ASSERT_TRUE(NS_SUCCEEDED(nsContentUtils::IPCTransferableDataToTransferable(
      data, /* aAddDataFlavor */ true, transferable,
      /* aFilterUnknownFlavors */ false)));

  EXPECT_TRUE(TransferableHasFlavor(transferable, kTextMime))
      << "known flavor should be preserved";
  EXPECT_TRUE(TransferableHasFlavor(transferable, kFilePromiseMime))
      << "kFilePromiseMime should be stripped when filtering";
  EXPECT_TRUE(TransferableHasFlavor(transferable, kFilePromiseURLMime))
      << "kFilePromiseURLMime should be stripped when filtering";
  EXPECT_TRUE(TransferableHasFlavor(transferable, kFilePromiseDestFilename))
      << "kFilePromiseDestFilename should be stripped when filtering";
  EXPECT_TRUE(TransferableHasFlavor(transferable, kFilePromiseDirectoryMime))
      << "kFilePromiseDirectoryMime should be stripped when filtering";
}

namespace {

class SetFlagMicroTask final : public mozilla::MicroTaskRunnable {
 public:
  explicit SetFlagMicroTask(bool* aFlag) : mFlag(aFlag) {}
  MOZ_CAN_RUN_SCRIPT void Run(mozilla::AutoSlowOperation& aAso) override {
    *mFlag = true;
  }

 private:
  bool* const mFlag;
};

already_AddRefed<Document> CreateWindowlessDocument() {
  nsCOMPtr<nsIURI> uri;
  NS_NewURI(getter_AddRefs(uri), "data:text/html,");

  RefPtr<mozilla::BasePrincipal> principal =
      mozilla::BasePrincipal::CreateContentPrincipal(
          uri, mozilla::OriginAttributes());
  MOZ_RELEASE_ASSERT(principal);

  nsCOMPtr<Document> doc;
  MOZ_ALWAYS_SUCCEEDS(NS_NewDOMDocument(getter_AddRefs(doc),
                                        u""_ns,   // aNamespaceURI
                                        u""_ns,   // aQualifiedName
                                        nullptr,  // aDoctype
                                        uri, uri, principal,
                                        LoadedAsData::No,  // aLoadedAsData
                                        nullptr,           // aEventObject
                                        DocumentFlavor::HTML));
  MOZ_RELEASE_ASSERT(doc);
  return doc.forget();
}

MOZ_CAN_RUN_SCRIPT_BOUNDARY void AppendEmptyScript(Document* aDocument) {
  RefPtr<Element> script = aDocument->CreateHTMLElement(nsGkAtoms::script);
  mozilla::IgnoredErrorResult rv;
  aDocument->AppendChild(*script, rv);
  MOZ_RELEASE_ASSERT(!rv.Failed());
}

MOZ_CAN_RUN_SCRIPT_BOUNDARY void EnterAndLeaveMicroTask() {
  mozilla::nsAutoMicroTask mt;
}

}  // namespace

// An empty inline script normally performs a microtask checkpoint, but not in
// a document without a window, which can't run script.
TEST(DOM_Base_ContentUtils, EmptyScriptInWindowlessDocumentSkipsCheckpoint)
{
  mozilla::CycleCollectedJSContext* ccjs =
      mozilla::CycleCollectedJSContext::Get();
  ASSERT_TRUE(ccjs);
  // Leaving a microtask only performs a checkpoint at the outermost level.
  ASSERT_EQ(ccjs->MicroTaskLevel(), 0u);

  RefPtr<Document> doc = CreateWindowlessDocument();
  ASSERT_FALSE(doc->GetInnerWindow());

  bool ran = false;
  ccjs->DispatchToMicroTask(mozilla::MakeAndAddRef<SetFlagMicroTask>(&ran));

  AppendEmptyScript(doc);
  EXPECT_FALSE(ran);

  // A plain nsAutoMicroTask does perform the checkpoint, which shows the
  // microtask really was pending.
  EnterAndLeaveMicroTask();
  EXPECT_TRUE(ran);
}

TEST(DOM_Base_ContentUtils, ComparePointsAncestorLimiter)
{
  using namespace mozilla;
  RefPtr<Document> doc = CreateWindowlessDocument();
  RefPtr<Element> root = doc->CreateHTMLElement(nsGkAtoms::div);
  RefPtr<Element> first = doc->CreateHTMLElement(nsGkAtoms::p);
  RefPtr<Element> last = doc->CreateHTMLElement(nsGkAtoms::p);
  RefPtr<Element> outside = doc->CreateHTMLElement(nsGkAtoms::div);
  IgnoredErrorResult error;
  root->AppendChild(*first, error);
  ASSERT_FALSE(error.Failed());
  root->AppendChild(*last, error);
  ASSERT_FALSE(error.Failed());
  auto compare = [&](const RawRangeBoundary& aFirst,
                     const RawRangeBoundary& aLast, const nsINode* aLimiter) {
    return nsContentUtils::ComparePoints<TreeKind::DOM>(aFirst, aLast, nullptr,
                                                        aLimiter);
  };
  EXPECT_EQ(
      compare(RawRangeBoundary(first, 0u), RawRangeBoundary(last, 0u), root),
      Some(-1));
  EXPECT_EQ(
      compare(RawRangeBoundary(last, 0u), RawRangeBoundary(first, 0u), root),
      Some(1));
  EXPECT_EQ(
      compare(RawRangeBoundary(root, 0u), RawRangeBoundary(last, 0u), root),
      Some(-1));
  EXPECT_EQ(
      compare(RawRangeBoundary(first, 0u), RawRangeBoundary(first, 0u), first),
      Some(0));
  EXPECT_TRUE(
      compare(RawRangeBoundary(first, 0u), RawRangeBoundary(last, 0u), first)
          .isNothing());
  EXPECT_TRUE(
      compare(RawRangeBoundary(outside, 0u), RawRangeBoundary(first, 0u), root)
          .isNothing());
  EXPECT_EQ(compare(RawRangeBoundary::FromChild(*first),
                    RawRangeBoundary::FromChild(*last), root),
            Some(-1));
  EXPECT_EQ(compare(RawRangeBoundary(root, 0u), RawRangeBoundary::After(*first),
                    root),
            Some(-1));
  EXPECT_TRUE(compare(RawRangeBoundary::After(*first),
                      RawRangeBoundary(outside, 0u), root)
                  .isNothing());
  EXPECT_EQ(
      compare(RawRangeBoundary(first, 0u), RawRangeBoundary(last, 0u), nullptr),
      Some(-1));
}
