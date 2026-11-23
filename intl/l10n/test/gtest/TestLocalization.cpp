/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"
#include "mozilla/intl/Localization.h"

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::intl;

TEST(Intl_Localization, FormatValueSyncMissing)
{
  nsTArray<nsCString> resIds = {
      "toolkit/global/handlerDialog.ftl"_ns,
  };
  RefPtr<Localization> l10n = Localization::Create(resIds, true);

  auto l10nId = "non-existing-l10n-id"_ns;
  IgnoredErrorResult rv;
  nsAutoCString result;

  l10n->FormatValueSync(l10nId, result, rv);
  ASSERT_FALSE(rv.Failed());
  ASSERT_TRUE(result.IsEmpty());
}

TEST(Intl_Localization, FormatValueSync)
{
  nsTArray<nsCString> resIds = {
      "toolkit/global/handlerDialog.ftl"_ns,
  };
  RefPtr<Localization> l10n = Localization::Create(resIds, true);

  auto l10nId = "permission-dialog-unset-description"_ns;
  IgnoredErrorResult rv;
  nsAutoCString result;

  l10n->FormatValueSync(l10nId, result, rv);
  ASSERT_FALSE(rv.Failed());
  ASSERT_FALSE(result.IsEmpty());
}

TEST(Intl_Localization, FormatValueSyncWithArgs)
{
  nsTArray<nsCString> resIds = {
      "toolkit/global/handlerDialog.ftl"_ns,
  };
  RefPtr<Localization> l10n = Localization::Create(resIds, true);

  auto l10nId = "permission-dialog-description"_ns;

  auto l10nArgs = dom::Optional<intl::L10nArgs>();
  l10nArgs.Construct();

  auto dirArg = l10nArgs.Value().Entries().AppendElement();
  dirArg->mKey = "scheme"_ns;
  dirArg->mValue.SetValue().SetAsUTF8String().Assign("Foo"_ns);

  IgnoredErrorResult rv;
  nsAutoCString result;

  l10n->FormatValueSync(l10nId, l10nArgs, result, rv);
  ASSERT_FALSE(rv.Failed());
  ASSERT_TRUE(result.Find("Foo"_ns) > -1);
}

TEST(Intl_Localization, FormatValueWithoutGlobal)
{
  nsTArray<nsCString> resIds = {
      "toolkit/global/handlerDialog.ftl"_ns,
  };
  RefPtr<Localization> l10n = Localization::Create(resIds, false);

  IgnoredErrorResult rv;
  RefPtr<Promise> promise =
      l10n->FormatValue("permission-dialog-unset-description"_ns, rv);
  ASSERT_TRUE(rv.Failed());
  ASSERT_FALSE(promise);
}

TEST(Intl_Localization, FormatMessagesSync)
{
  nsTArray<nsCString> resIds = {
      "toolkit/global/handlerDialog.ftl"_ns,
  };
  RefPtr<Localization> l10n = Localization::Create(resIds, true);

  dom::Sequence<dom::OwningUTF8StringOrL10nIdArgs> l10nIds;
  auto* l10nId = l10nIds.AppendElement(fallible);
  ASSERT_TRUE(l10nId);
  l10nId->SetAsUTF8String().Assign("permission-dialog-unset-description"_ns);

  IgnoredErrorResult rv;
  nsTArray<dom::Nullable<dom::L10nMessage>> result;

  l10n->FormatMessagesSync(l10nIds, result, rv);
  ASSERT_FALSE(rv.Failed());
  ASSERT_FALSE(result.IsEmpty());
}

TEST(Intl_Localization, FormatMessagesSyncWithArgs)
{
  nsTArray<nsCString> resIds = {
      "toolkit/global/handlerDialog.ftl"_ns,
  };
  RefPtr<Localization> l10n = Localization::Create(resIds, true);

  dom::Sequence<dom::OwningUTF8StringOrL10nIdArgs> l10nIds;
  L10nIdArgs& key0 = l10nIds.AppendElement(fallible)->SetAsL10nIdArgs();
  key0.mId.Assign("permission-dialog-description"_ns);
  auto arg = key0.mArgs.SetValue().Entries().AppendElement();
  arg->mKey = "scheme"_ns;
  arg->mValue.SetValue().SetAsUTF8String().Assign("Foo"_ns);

  L10nIdArgs& key1 = l10nIds.AppendElement(fallible)->SetAsL10nIdArgs();
  key1.mId.Assign("chooser-window"_ns);

  IgnoredErrorResult rv;
  nsTArray<dom::Nullable<dom::L10nMessage>> result;

  l10n->FormatMessagesSync(l10nIds, result, rv);
  ASSERT_FALSE(rv.Failed());
  ASSERT_TRUE(result.Length() == 2);
  ASSERT_TRUE(result.ElementAt(0).Value().mValue.Find("Foo"_ns) > -1);

  auto fmtAttr = result.ElementAt(1).Value().mAttributes.Value();
  ASSERT_TRUE(fmtAttr.Length() == 2);
  ASSERT_FALSE(fmtAttr.ElementAt(0).mName.IsEmpty());
  ASSERT_FALSE(fmtAttr.ElementAt(0).mValue.IsEmpty());
}
