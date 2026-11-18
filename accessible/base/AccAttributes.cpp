/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AccAttributes.h"

#include "StyleInfo.h"
#include "mozilla/ToString.h"
#include "nsAtom.h"

namespace mozilla::a11y {

bool AccAttributes::GetAttribute(nsAtom* aAttrName,
                                 nsAString& aAttrValue) const {
  if (auto value = mData.Lookup(aAttrName)) {
    StringFromValueAndName(aAttrName, *value, aAttrValue);
    return true;
  }

  return false;
}

void AccAttributes::StringFromValueAndName(nsAtom* /*aAttrName*/,
                                           const AttrValueType& aValue,
                                           nsAString& aValueString) {
  aValueString.Truncate();

  aValue.match(
      [&aValueString](const bool& val) {
        aValueString.Assign(val ? u"true" : u"false");
      },
      [&aValueString](const float& val) {
        aValueString.AppendFloat(val * 100);
        aValueString.Append(u"%");
      },
      [&aValueString](const double& val) { aValueString.AppendFloat(val); },
      [&aValueString](const int32_t& val) { aValueString.AppendInt(val); },
      [&aValueString](const RefPtr<nsAtom>& val) {
        val->ToString(aValueString);
      },
      [&aValueString](const nsTArray<int32_t>& val) {
        if (const size_t len = val.Length()) {
          for (size_t i = 0; i < len - 1; i++) {
            aValueString.AppendInt(val[i]);
            aValueString.Append(u", ");
          }
          aValueString.AppendInt(val[len - 1]);
        } else {
          // The array is empty
          NS_WARNING(
              "Hmm, should we have used a DeleteEntry() for this instead?");
          aValueString.Append(u"[ ]");
        }
      },
      [&aValueString](const CSSCoord& val) {
        aValueString.AppendFloat(val);
        aValueString.Append(u"px");
      },
      [&aValueString](const FontSize& val) {
        aValueString.AppendInt(val.mValue);
        aValueString.Append(u"pt");
      },
      [&aValueString](const Color& val) {
        StyleInfo::FormatColor(val.mValue, aValueString);
      },
      [&aValueString](const DeleteEntry& /*val*/) {
        aValueString.Append(u"-delete-entry-");
      },
      [&aValueString](const UniquePtr<nsString>& val) {
        aValueString.Assign(*val);
      },
      [&aValueString](const RefPtr<AccAttributes>& val) {
        if (val) {
          aValueString.AppendPrintf("AccAttributes: %s",
                                    ToString(*val).c_str());
        } else {
          aValueString.AssignASCII("<null>");
        }
      },
      [&aValueString](const uint64_t& val) { aValueString.AppendInt(val); },
      [&aValueString](const UniquePtr<AccGroupInfo>& /*val*/) {
        aValueString.Assign(u"AccGroupInfo{...}");
      },
      [&aValueString](const UniquePtr<gfx::Matrix4x4>& val) {
        aValueString.AppendPrintf("Matrix4x4=%s", ToString(*val).c_str());
      },
      [&aValueString](const UniquePtr<nsRect>& val) {
        aValueString.AppendPrintf("nsRect=%s", ToString(*val).c_str());
      },
      [&aValueString](const nsTArray<uint64_t>& val) {
        if (const size_t len = val.Length()) {
          for (size_t i = 0; i < len - 1; i++) {
            aValueString.AppendInt(val[i]);
            aValueString.Append(u", ");
          }
          aValueString.AppendInt(val[len - 1]);
        } else {
          // The array is empty
          NS_WARNING(
              "Hmm, should we have used a DeleteEntry() for this instead?");
          aValueString.Append(u"[ ]");
        }
      },
      [&aValueString](const nsTArray<TextOffsetAttribute>& val) {
        if (const size_t len = val.Length()) {
          for (size_t i = 0; i < len - 1; i++) {
            aValueString.AppendPrintf("(%d, %d, ", val[i].mStartOffset,
                                      val[i].mEndOffset);
            aValueString.Append(nsAtomString(val[i].mAttribute));
            aValueString.Append(u"), ");
          }
          aValueString.AppendPrintf("(%d, %d, ", val[len - 1].mStartOffset,
                                    val[len - 1].mEndOffset);
          aValueString.Append(nsAtomString(val[len - 1].mAttribute));
          aValueString += ')';
        } else {
          // The array is empty
          NS_WARNING(
              "Hmm, should we have used a DeleteEntry() for this instead?");
          aValueString.Append(u"[ ]");
        }
      },
      [&aValueString](const WritingMode& val) {
        aValueString.AppendPrintf("WritingModes: %x", val.GetBits());
      },
      [&aValueString](const nsTArray<RefPtr<nsAtom>>& val) {
        if (const size_t len = val.Length()) {
          for (size_t i = 0; i < len - 1; i++) {
            aValueString.Append(val[i]->GetUTF16String());
            aValueString.Append(u" ");
          }
          aValueString.Append(val[len - 1]->GetUTF16String());
        } else {
          // The array is empty
          NS_WARNING(
              "Hmm, should we have used a DeleteEntry() for this instead?");
          aValueString.Append(u"");
        }
      });
}

void AccAttributes::Update(AccAttributes* aOther) {
  for (auto iter = aOther->mData.Iter(); !iter.Done(); iter.Next()) {
    if (iter.Data().is<DeleteEntry>()) {
      mData.Remove(iter.Key());
    } else {
      mData.InsertOrUpdate(iter.Key(), std::move(iter.Data()));
    }
    iter.Remove();
  }
}

void AccAttributes::RemoveIdentical(const AccAttributes* aOther) {
  for (auto iter = mData.Iter(); !iter.Done(); iter.Next()) {
    if (const auto otherEntry = aOther->mData.Lookup(iter.Key())) {
      if (iter.Data().is<UniquePtr<nsString>>()) {
        // Because we store nsString in a UniquePtr, we must handle it specially
        // so we compare the string and not the pointer.
        if (!otherEntry->is<UniquePtr<nsString>>()) {
          continue;
        }
        const auto& thisStr = iter.Data().as<UniquePtr<nsString>>();
        const auto& otherStr = otherEntry->as<UniquePtr<nsString>>();
        if (*thisStr != *otherStr) {
          continue;
        }
      } else if (iter.Data() != otherEntry.Data()) {
        continue;
      }

      iter.Remove();
    }
  }
}

bool AccAttributes::Equal(const AccAttributes* aOther) const {
  if (Count() != aOther->Count()) {
    return false;
  }
  for (auto iter = mData.ConstIter(); !iter.Done(); iter.Next()) {
    const auto otherEntry = aOther->mData.Lookup(iter.Key());
    if (!otherEntry) {
      return false;
    }
    if (iter.Data().is<UniquePtr<nsString>>()) {
      // Because we store nsString in a UniquePtr, we must handle it specially
      // so we compare the string and not the pointer.
      if (!otherEntry->is<UniquePtr<nsString>>()) {
        return false;
      }
      const auto& thisStr = iter.Data().as<UniquePtr<nsString>>();
      const auto& otherStr = otherEntry->as<UniquePtr<nsString>>();
      if (*thisStr != *otherStr) {
        return false;
      }
    } else if (iter.Data() != otherEntry.Data()) {
      return false;
    }
  }
  return true;
}

void AccAttributes::CopyTo(AccAttributes* aDest, CopyReq aCopyReq) const {
  for (auto iter = mData.ConstIter(); !iter.Done(); iter.Next()) {
    if (aCopyReq && !aCopyReq(iter.Key())) {
      continue;
    }

    iter.Data().match(
        [&iter, &aDest](const bool& val) {
          aDest->mData.InsertOrUpdate(iter.Key(), AsVariant(val));
        },
        [&iter, &aDest](const float& val) {
          aDest->mData.InsertOrUpdate(iter.Key(), AsVariant(val));
        },
        [&iter, &aDest](const double& val) {
          aDest->mData.InsertOrUpdate(iter.Key(), AsVariant(val));
        },
        [&iter, &aDest](const int32_t& val) {
          aDest->mData.InsertOrUpdate(iter.Key(), AsVariant(val));
        },
        [&iter, &aDest](const RefPtr<nsAtom>& val) {
          aDest->mData.InsertOrUpdate(iter.Key(), AsVariant(val));
        },
        [](const nsTArray<int32_t>& /*val*/) {
          // We don't copy arrays.
          MOZ_ASSERT_UNREACHABLE(
              "Trying to copy an AccAttributes containing an array");
        },
        [&iter, &aDest](const CSSCoord& val) {
          aDest->mData.InsertOrUpdate(iter.Key(), AsVariant(val));
        },
        [&iter, &aDest](const FontSize& val) {
          aDest->mData.InsertOrUpdate(iter.Key(), AsVariant(val));
        },
        [&iter, &aDest](const Color& val) {
          aDest->mData.InsertOrUpdate(iter.Key(), AsVariant(val));
        },
        [](const DeleteEntry& /*val*/) {
          // We don't copy DeleteEntry.
          MOZ_ASSERT_UNREACHABLE(
              "Trying to copy an AccAttributes containing a DeleteEntry");
        },
        [&iter, &aDest](const UniquePtr<nsString>& val) {
          aDest->SetAttributeStringCopy(iter.Key(), *val);
        },
        [](const RefPtr<AccAttributes>& /*val*/) {
          // We don't copy nested AccAttributes.
          MOZ_ASSERT_UNREACHABLE(
              "Trying to copy an AccAttributes containing an AccAttributes");
        },
        [&iter, &aDest](const uint64_t& val) {
          aDest->mData.InsertOrUpdate(iter.Key(), AsVariant(val));
        },
        [](const UniquePtr<AccGroupInfo>& /*val*/) {
          MOZ_ASSERT_UNREACHABLE(
              "Trying to copy an AccAttributes containing an AccGroupInfo");
        },
        [](const UniquePtr<gfx::Matrix4x4>& /*val*/) {
          MOZ_ASSERT_UNREACHABLE(
              "Trying to copy an AccAttributes containing a matrix");
        },
        [](const UniquePtr<nsRect>& /*val*/) {
          MOZ_ASSERT_UNREACHABLE(
              "Trying to copy an AccAttributes containing a nsrect");
        },
        [](const nsTArray<uint64_t>& /*val*/) {
          // We don't copy arrays.
          MOZ_ASSERT_UNREACHABLE(
              "Trying to copy an AccAttributes containing an array");
        },
        [](const nsTArray<TextOffsetAttribute>& /*val*/) {
          // We don't copy arrays.
          MOZ_ASSERT_UNREACHABLE(
              "Trying to copy an AccAttributes containing an array");
        },
        [&iter, &aDest](const WritingMode& val) {
          aDest->mData.InsertOrUpdate(iter.Key(), AsVariant(val));
        },
        [](const nsTArray<RefPtr<nsAtom>>& /*val*/) {
          // We don't copy arrays.
          MOZ_ASSERT_UNREACHABLE(
              "Trying to copy an AccAttributes containing an array");
        });
  }
}

#ifdef A11Y_LOG
void AccAttributes::DebugPrint(const char* aPrefix,
                               const AccAttributes& aAttributes) {
  printf("%s %s\n", aPrefix, ToString(aAttributes).c_str());
}
#endif

size_t AccAttributes::SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) {
  size_t size =
      aMallocSizeOf(this) + mData.ShallowSizeOfExcludingThis(aMallocSizeOf);

  for (auto iter : *this) {
    size += iter.SizeOfExcludingThis(aMallocSizeOf);
  }

  return size;
}

size_t AccAttributes::Entry::SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) {
  size_t size = 0;

  // We don't count the size of Name() since it's counted by the atoms table
  // memory reporter.

  if (mValue->is<nsTArray<int32_t>>()) {
    size += mValue->as<nsTArray<int32_t>>().ShallowSizeOfExcludingThis(
        aMallocSizeOf);
  } else if (mValue->is<UniquePtr<nsString>>()) {
    // String data will never be shared.
    size += mValue->as<UniquePtr<nsString>>()->SizeOfIncludingThisIfUnshared(
        aMallocSizeOf);
  } else if (mValue->is<RefPtr<AccAttributes>>()) {
    size +=
        mValue->as<RefPtr<AccAttributes>>()->SizeOfIncludingThis(aMallocSizeOf);
  } else if (mValue->is<UniquePtr<AccGroupInfo>>()) {
    size += mValue->as<UniquePtr<AccGroupInfo>>()->SizeOfIncludingThis(
        aMallocSizeOf);
  } else if (mValue->is<UniquePtr<gfx::Matrix4x4>>()) {
    size += aMallocSizeOf(mValue->as<UniquePtr<gfx::Matrix4x4>>().get());
  } else if (mValue->is<UniquePtr<nsRect>>()) {
    size += aMallocSizeOf(mValue->as<UniquePtr<nsRect>>().get());
  } else if (mValue->is<nsTArray<uint64_t>>()) {
    size += mValue->as<nsTArray<uint64_t>>().ShallowSizeOfExcludingThis(
        aMallocSizeOf);
  } else if (mValue->is<nsTArray<RefPtr<nsAtom>>>()) {
    size += mValue->as<nsTArray<RefPtr<nsAtom>>>().ShallowSizeOfExcludingThis(
        aMallocSizeOf);
  } else {
    // This type is stored directly and already counted or is an atom and
    // stored and counted in the atoms table.
    // Assert that we have exhausted all the remaining variant types.
    MOZ_ASSERT(mValue->is<RefPtr<nsAtom>>() || mValue->is<bool>() ||
               mValue->is<float>() || mValue->is<double>() ||
               mValue->is<int32_t>() || mValue->is<uint64_t>() ||
               mValue->is<CSSCoord>() || mValue->is<FontSize>() ||
               mValue->is<Color>() || mValue->is<DeleteEntry>());
  }

  return size;
}

std::ostream& operator<<(std::ostream& aStream,
                         const AccAttributes& aAttributes) {
  if (aAttributes.Count() == 0) {
    aStream << "{ empty }";
    return aStream;
  }
  aStream << "{\n";
  nsAutoStringN<2> separator{};
  nsAutoString scratch;
  for (const AccAttributes::Entry entry : aAttributes) {
    aStream << separator << "  ";
    entry.NameAsString(scratch);
    aStream << scratch << ": ";
    entry.ValueAsString(scratch);
    aStream << scratch;
    separator.AssignASCII(",\n");
  }
  aStream << "\n}";
  return aStream;
}

}  // namespace mozilla::a11y
