/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <limits.h>

#include "mozilla/UniquePtrExtensions.h"
#include "nsString.h"

#include "mozStorageError.h"
#include "mozStoragePrivateHelpers.h"
#include "mozStorageBindingParams.h"
#include "Variant.h"

namespace mozilla::storage {

////////////////////////////////////////////////////////////////////////////////
//// Local Helper Objects

namespace {

struct BindingColumnData {
  BindingColumnData(sqlite3_stmt* aStmt, int aColumn)
      : stmt(aStmt), column(aColumn) {}
  sqlite3_stmt* stmt;
  int column;
};

////////////////////////////////////////////////////////////////////////////////
//// Variant Specialization Functions (variantToSQLiteT)

int sqlite3_T_int(BindingColumnData aData, int aValue) {
  return ::sqlite3_bind_int(aData.stmt, aData.column + 1, aValue);
}

int sqlite3_T_int64(BindingColumnData aData, sqlite3_int64 aValue) {
  return ::sqlite3_bind_int64(aData.stmt, aData.column + 1, aValue);
}

int sqlite3_T_double(BindingColumnData aData, double aValue) {
  return ::sqlite3_bind_double(aData.stmt, aData.column + 1, aValue);
}

int sqlite3_T_text(BindingColumnData aData, const nsCString& aValue) {
  return ::sqlite3_bind_text(aData.stmt, aData.column + 1, aValue.get(),
                             aValue.Length(), SQLITE_TRANSIENT);
}

int sqlite3_T_text16(BindingColumnData aData, const nsString& aValue) {
  return ::sqlite3_bind_text16(
      aData.stmt, aData.column + 1, aValue.get(),
      aValue.Length() * sizeof(char16_t),  // Length in bytes!
      SQLITE_TRANSIENT);
}

int sqlite3_T_null(BindingColumnData aData) {
  return ::sqlite3_bind_null(aData.stmt, aData.column + 1);
}

int sqlite3_T_blob(BindingColumnData aData, const void* aBlob, int aSize) {
  return ::sqlite3_bind_blob(aData.stmt, aData.column + 1, aBlob, aSize, free);
}

int sqlite3_T_array(BindingColumnData aData, void* aArray, int aSize,
                    int aType) {
  // In debug builds ensure that the statement includes at least one `carray()`.
  MOZ_ASSERT(
      ::strstr(::sqlite3_sql(aData.stmt), "carray(") ||
          ::strstr(::sqlite3_sql(aData.stmt), "CARRAY("),
      "Binding arrays to SQL statements requires using the carray() function.");

  if (aType == CARRAY_TEXT) {
    // We don't manage the string buffers lifecycle, thus let SQLite make its
    // own copy.
    int srv = ::sqlite3_carray_bind(aData.stmt, aData.column + 1, aArray, aSize,
                                    aType, SQLITE_TRANSIENT);
    free(aArray);
    return srv;
  }
  return ::sqlite3_carray_bind(aData.stmt, aData.column + 1, aArray, aSize,
                               aType, free);
}

#include "variantToSQLiteT_impl.h"

}  // namespace

////////////////////////////////////////////////////////////////////////////////
//// BindingParams

BindingParams::BindingParams(mozIStorageBindingParamsArray* aOwningArray,
                             Statement* aOwningStatement)
    : mLocked(false),
      mOwningArray(aOwningArray),
      mOwningStatement(aOwningStatement),
      mParamCount(0) {
  (void)mOwningStatement->GetParameterCount(&mParamCount);
  mParameters.SetCapacity(mParamCount);
}

BindingParams::BindingParams(mozIStorageBindingParamsArray* aOwningArray)
    : mLocked(false),
      mOwningArray(aOwningArray),
      mOwningStatement(nullptr),
      mParamCount(0) {}

AsyncBindingParams::AsyncBindingParams(
    mozIStorageBindingParamsArray* aOwningArray)
    : BindingParams(aOwningArray) {}

void BindingParams::lock() {
  NS_ASSERTION(mLocked == false, "Parameters have already been locked!");
  mLocked = true;

  // We no longer need to hold a reference to our statement or our owning array.
  // The array owns us at this point, and it will own a reference to the
  // statement.
  mOwningStatement = nullptr;
  mOwningArray = nullptr;
}

void BindingParams::unlock(Statement* aOwningStatement) {
  NS_ASSERTION(mLocked == true, "Parameters were not yet locked!");
  mLocked = false;
  mOwningStatement = aOwningStatement;
}

const mozIStorageBindingParamsArray* BindingParams::getOwner() const {
  return mOwningArray;
}

////////////////////////////////////////////////////////////////////////////////
//// nsISupports

NS_IMPL_ISUPPORTS(BindingParams, mozIStorageBindingParams,
                  IStorageBindingParamsInternal)

////////////////////////////////////////////////////////////////////////////////
//// IStorageBindingParamsInternal

already_AddRefed<mozIStorageError> BindingParams::bind(
    sqlite3_stmt* aStatement) {
  // Iterate through all of our stored data, and bind it.
  for (size_t i = 0; i < mParameters.Length(); i++) {
    int rc = variantToSQLiteT(BindingColumnData(aStatement, i), mParameters[i]);
    if (rc != SQLITE_OK) {
      // We had an error while trying to bind.  Now we need to create an error
      // object with the right message.  Note that we special case
      // SQLITE_MISMATCH, but otherwise get the message from SQLite.
      const char* msg = "Could not covert nsIVariant to SQLite type.";
      if (rc != SQLITE_MISMATCH) {
        msg = ::sqlite3_errmsg(::sqlite3_db_handle(aStatement));
      }

      nsCOMPtr<mozIStorageError> err(new Error(rc, msg));
      return err.forget();
    }
  }

  return nullptr;
}

already_AddRefed<mozIStorageError> AsyncBindingParams::bind(
    sqlite3_stmt* aStatement) {
  nsCOMPtr<mozIStorageError> err;

  for (const auto& entry : mNamedParameters) {
    const nsACString& key = entry.GetKey();

    // We do not accept any forms of names other than ":name", but we need to
    // add the colon for SQLite.
    nsAutoCString name(":");
    name.Append(key);
    int oneIdx = ::sqlite3_bind_parameter_index(aStatement, name.get());

    if (oneIdx == 0) {
      nsAutoCString errMsg(key);
      errMsg.AppendLiteral(" is not a valid named parameter.");
      err = new Error(SQLITE_RANGE, errMsg.get());
      return err.forget();
    }

    if (mParameters.Length() <= static_cast<uint32_t>(oneIdx - 1)) {
      mParameters.SetLength(oneIdx - 1);
      mParameters.AppendElement(entry.GetRawPtr());
    } else {
      mParameters.ReplaceElementAt(oneIdx - 1, entry.GetRawPtr());
    }
  }

  // Now bind using the super class.
  return BindingParams::bind(aStatement);
}

///////////////////////////////////////////////////////////////////////////////
//// mozIStorageBindingParams

NS_IMETHODIMP
BindingParams::BindByName(const nsACString& aName, nsIVariant* aValue) {
  NS_ENSURE_FALSE(mLocked, NS_ERROR_UNEXPECTED);

  // Get the column index that we need to store this at.
  uint32_t index;
  nsresult rv = mOwningStatement->GetParameterIndex(aName, &index);
  NS_ENSURE_SUCCESS(rv, rv);

  return BindByIndex(index, aValue);
}

NS_IMETHODIMP
AsyncBindingParams::BindByName(const nsACString& aName, nsIVariant* aValue) {
  NS_ENSURE_FALSE(mLocked, NS_ERROR_UNEXPECTED);

  RefPtr<Variant_base> variant = convertVariantToStorageVariant(aValue);
  if (!variant) return NS_ERROR_UNEXPECTED;
  mNamedParameters.InsertOrUpdate(aName, std::move(variant));
  return NS_OK;
}

NS_IMETHODIMP
BindingParams::BindUTF8StringByName(const nsACString& aName,
                                    const nsACString& aValue) {
  nsCOMPtr<nsIVariant> value(new UTF8TextVariant(aValue));
  NS_ENSURE_TRUE(value, NS_ERROR_OUT_OF_MEMORY);

  return BindByName(aName, value);
}

NS_IMETHODIMP
BindingParams::BindStringByName(const nsACString& aName,
                                const nsAString& aValue) {
  nsCOMPtr<nsIVariant> value(new TextVariant(aValue));
  NS_ENSURE_TRUE(value, NS_ERROR_OUT_OF_MEMORY);

  return BindByName(aName, value);
}

NS_IMETHODIMP
BindingParams::BindDoubleByName(const nsACString& aName, double aValue) {
  nsCOMPtr<nsIVariant> value(new FloatVariant(aValue));
  NS_ENSURE_TRUE(value, NS_ERROR_OUT_OF_MEMORY);

  return BindByName(aName, value);
}

NS_IMETHODIMP
BindingParams::BindInt32ByName(const nsACString& aName, int32_t aValue) {
  nsCOMPtr<nsIVariant> value(new IntegerVariant(aValue));
  NS_ENSURE_TRUE(value, NS_ERROR_OUT_OF_MEMORY);

  return BindByName(aName, value);
}

NS_IMETHODIMP
BindingParams::BindInt64ByName(const nsACString& aName, int64_t aValue) {
  nsCOMPtr<nsIVariant> value(new IntegerVariant(aValue));
  NS_ENSURE_TRUE(value, NS_ERROR_OUT_OF_MEMORY);

  return BindByName(aName, value);
}

NS_IMETHODIMP
BindingParams::BindNullByName(const nsACString& aName) {
  nsCOMPtr<nsIVariant> value(new NullVariant());
  NS_ENSURE_TRUE(value, NS_ERROR_OUT_OF_MEMORY);

  return BindByName(aName, value);
}

NS_IMETHODIMP
BindingParams::BindBlobByName(const nsACString& aName, const uint8_t* aValue,
                              uint32_t aValueSize) {
  NS_ENSURE_ARG_MAX(aValueSize, INT_MAX);
  std::pair<const void*, int> data(static_cast<const void*>(aValue),
                                   int(aValueSize));
  nsCOMPtr<nsIVariant> value(new BlobVariant(data));
  NS_ENSURE_TRUE(value, NS_ERROR_OUT_OF_MEMORY);

  return BindByName(aName, value);
}

NS_IMETHODIMP
BindingParams::BindBlobArrayByName(const nsACString& aName,
                                   const nsTArray<uint8_t>& aValue) {
  return BindBlobByName(aName, aValue.Elements(), aValue.Length());
}

NS_IMETHODIMP
BindingParams::BindStringAsBlobByName(const nsACString& aName,
                                      const nsAString& aValue) {
  return DoBindStringAsBlobByName(this, aName, aValue);
}

NS_IMETHODIMP
BindingParams::BindUTF8StringAsBlobByName(const nsACString& aName,
                                          const nsACString& aValue) {
  return DoBindStringAsBlobByName(this, aName, aValue);
}

NS_IMETHODIMP
BindingParams::BindAdoptedBlobByName(const nsACString& aName, uint8_t* aValue,
                                     uint32_t aValueSize) {
  UniqueFreePtr<uint8_t> uniqueValue(aValue);
  NS_ENSURE_ARG_MAX(aValueSize, INT_MAX);
  std::pair<uint8_t*, int> data(uniqueValue.release(), int(aValueSize));
  nsCOMPtr<nsIVariant> value(new AdoptedBlobVariant(data));

  return BindByName(aName, value);
}

NS_IMETHODIMP
BindingParams::BindArrayOfIntegersByName(const nsACString& aName,
                                         const nsTArray<int64_t>& aValue) {
  NS_ENSURE_ARG_MAX(aValue.Length(), INT_MAX);
  std::pair<const void*, int> data(static_cast<const void*>(aValue.Elements()),
                                   int(aValue.Length()));
  nsCOMPtr<nsIVariant> value(new ArrayOfIntegersVariant(data));
  NS_ENSURE_TRUE(value, NS_ERROR_OUT_OF_MEMORY);

  return BindByName(aName, value);
}

NS_IMETHODIMP
BindingParams::BindArrayOfDoublesByName(const nsACString& aName,
                                        const nsTArray<double>& aValue) {
  NS_ENSURE_ARG_MAX(aValue.Length(), INT_MAX);
  std::pair<const void*, int> data(static_cast<const void*>(aValue.Elements()),
                                   int(aValue.Length()));
  nsCOMPtr<nsIVariant> value(new ArrayOfDoublesVariant(data));
  NS_ENSURE_TRUE(value, NS_ERROR_OUT_OF_MEMORY);

  return BindByName(aName, value);
}

NS_IMETHODIMP
BindingParams::BindArrayOfStringsByName(const nsACString& aName,
                                        const nsTArray<nsString>& aValue) {
  NS_ENSURE_ARG_MAX(aValue.Length(), INT_MAX);
  nsTArray<nsCString> UTF8Strings(aValue.Length());
  for (const nsString& str : aValue) {
    UTF8Strings.AppendElement(NS_ConvertUTF16toUTF8(str));
  }
  std::pair<const void*, int> data(
      static_cast<const void*>(UTF8Strings.Elements()),
      int(UTF8Strings.Length()));
  // The variant will make a copy of all the buffers.
  nsCOMPtr<nsIVariant> value(new ArrayOfUTF8StringsVariant(data));
  NS_ENSURE_TRUE(value, NS_ERROR_OUT_OF_MEMORY);

  return BindByName(aName, value);
}

NS_IMETHODIMP
BindingParams::BindArrayOfUTF8StringsByName(const nsACString& aName,
                                            const nsTArray<nsCString>& aValue) {
  NS_ENSURE_ARG_MAX(aValue.Length(), INT_MAX);
  std::pair<const void*, int> data(static_cast<const void*>(aValue.Elements()),
                                   int(aValue.Length()));
  // The variant will make a copy of all the buffers.
  nsCOMPtr<nsIVariant> value(new ArrayOfUTF8StringsVariant(data));
  NS_ENSURE_TRUE(value, NS_ERROR_OUT_OF_MEMORY);

  return BindByName(aName, value);
}

NS_IMETHODIMP
BindingParams::BindByIndex(uint32_t aIndex, nsIVariant* aValue) {
  NS_ENSURE_FALSE(mLocked, NS_ERROR_UNEXPECTED);
  ENSURE_INDEX_VALUE(aIndex, mParamCount);

  // Store the variant for later use.
  RefPtr<Variant_base> variant = convertVariantToStorageVariant(aValue);
  if (!variant) return NS_ERROR_UNEXPECTED;
  if (mParameters.Length() <= aIndex) {
    (void)mParameters.SetLength(aIndex);
    (void)mParameters.AppendElement(variant);
  } else {
    mParameters.ReplaceElementAt(aIndex, variant);
  }
  return NS_OK;
}

NS_IMETHODIMP
AsyncBindingParams::BindByIndex(uint32_t aIndex, nsIVariant* aValue) {
  NS_ENSURE_FALSE(mLocked, NS_ERROR_UNEXPECTED);
  // In the asynchronous case we do not know how many parameters there are to
  // bind to, so we cannot check the validity of aIndex.

  RefPtr<Variant_base> variant = convertVariantToStorageVariant(aValue);
  if (!variant) return NS_ERROR_UNEXPECTED;
  if (mParameters.Length() <= aIndex) {
    mParameters.SetLength(aIndex);
    mParameters.AppendElement(variant);
  } else {
    mParameters.ReplaceElementAt(aIndex, variant);
  }
  return NS_OK;
}

NS_IMETHODIMP
BindingParams::BindUTF8StringByIndex(uint32_t aIndex,
                                     const nsACString& aValue) {
  nsCOMPtr<nsIVariant> value(new UTF8TextVariant(aValue));
  NS_ENSURE_TRUE(value, NS_ERROR_OUT_OF_MEMORY);

  return BindByIndex(aIndex, value);
}

NS_IMETHODIMP
BindingParams::BindStringByIndex(uint32_t aIndex, const nsAString& aValue) {
  nsCOMPtr<nsIVariant> value(new TextVariant(aValue));
  NS_ENSURE_TRUE(value, NS_ERROR_OUT_OF_MEMORY);

  return BindByIndex(aIndex, value);
}

NS_IMETHODIMP
BindingParams::BindDoubleByIndex(uint32_t aIndex, double aValue) {
  nsCOMPtr<nsIVariant> value(new FloatVariant(aValue));
  NS_ENSURE_TRUE(value, NS_ERROR_OUT_OF_MEMORY);

  return BindByIndex(aIndex, value);
}

NS_IMETHODIMP
BindingParams::BindInt32ByIndex(uint32_t aIndex, int32_t aValue) {
  nsCOMPtr<nsIVariant> value(new IntegerVariant(aValue));
  NS_ENSURE_TRUE(value, NS_ERROR_OUT_OF_MEMORY);

  return BindByIndex(aIndex, value);
}

NS_IMETHODIMP
BindingParams::BindInt64ByIndex(uint32_t aIndex, int64_t aValue) {
  nsCOMPtr<nsIVariant> value(new IntegerVariant(aValue));
  NS_ENSURE_TRUE(value, NS_ERROR_OUT_OF_MEMORY);

  return BindByIndex(aIndex, value);
}

NS_IMETHODIMP
BindingParams::BindNullByIndex(uint32_t aIndex) {
  nsCOMPtr<nsIVariant> value(new NullVariant());
  NS_ENSURE_TRUE(value, NS_ERROR_OUT_OF_MEMORY);

  return BindByIndex(aIndex, value);
}

NS_IMETHODIMP
BindingParams::BindBlobByIndex(uint32_t aIndex, const uint8_t* aValue,
                               uint32_t aValueSize) {
  NS_ENSURE_ARG_MAX(aValueSize, INT_MAX);
  std::pair<const void*, int> data(static_cast<const void*>(aValue),
                                   int(aValueSize));
  nsCOMPtr<nsIVariant> value(new BlobVariant(data));
  NS_ENSURE_TRUE(value, NS_ERROR_OUT_OF_MEMORY);

  return BindByIndex(aIndex, value);
}

NS_IMETHODIMP
BindingParams::BindBlobArrayByIndex(uint32_t aIndex,
                                    const nsTArray<uint8_t>& aValue) {
  return BindBlobByIndex(aIndex, aValue.Elements(), aValue.Length());
}

NS_IMETHODIMP
BindingParams::BindStringAsBlobByIndex(uint32_t aIndex,
                                       const nsAString& aValue) {
  return DoBindStringAsBlobByIndex(this, aIndex, aValue);
}

NS_IMETHODIMP
BindingParams::BindUTF8StringAsBlobByIndex(uint32_t aIndex,
                                           const nsACString& aValue) {
  return DoBindStringAsBlobByIndex(this, aIndex, aValue);
}

NS_IMETHODIMP
BindingParams::BindAdoptedBlobByIndex(uint32_t aIndex, uint8_t* aValue,
                                      uint32_t aValueSize) {
  UniqueFreePtr<uint8_t> uniqueValue(aValue);
  NS_ENSURE_ARG_MAX(aValueSize, INT_MAX);
  std::pair<uint8_t*, int> data(uniqueValue.release(), int(aValueSize));
  nsCOMPtr<nsIVariant> value(new AdoptedBlobVariant(data));

  return BindByIndex(aIndex, value);
}

NS_IMETHODIMP
BindingParams::BindArrayOfIntegersByIndex(uint32_t aIndex,
                                          const nsTArray<int64_t>& aValue) {
  NS_ENSURE_ARG_MAX(aValue.Length(), INT_MAX);
  std::pair<const void*, int> data(static_cast<const void*>(aValue.Elements()),
                                   int(aValue.Length()));
  nsCOMPtr<nsIVariant> value(new ArrayOfIntegersVariant(data));
  NS_ENSURE_TRUE(value, NS_ERROR_OUT_OF_MEMORY);

  return BindByIndex(aIndex, value);
}

NS_IMETHODIMP
BindingParams::BindArrayOfDoublesByIndex(uint32_t aIndex,
                                         const nsTArray<double>& aValue) {
  NS_ENSURE_ARG_MAX(aValue.Length(), INT_MAX);
  std::pair<const void*, int> data(static_cast<const void*>(aValue.Elements()),
                                   int(aValue.Length()));
  nsCOMPtr<nsIVariant> value(new ArrayOfDoublesVariant(data));
  NS_ENSURE_TRUE(value, NS_ERROR_OUT_OF_MEMORY);

  return BindByIndex(aIndex, value);
}

NS_IMETHODIMP
BindingParams::BindArrayOfStringsByIndex(uint32_t aIndex,
                                         const nsTArray<nsString>& aValue) {
  NS_ENSURE_ARG_MAX(aValue.Length(), INT_MAX);
  nsTArray<nsCString> UTF8Strings(aValue.Length());
  for (const nsString& str : aValue) {
    UTF8Strings.AppendElement(NS_ConvertUTF16toUTF8(str).get());
  }
  std::pair<const void*, int> data(
      static_cast<const void*>(UTF8Strings.Elements()),
      int(UTF8Strings.Length()));
  // The variant will make a copy of all the buffers.
  nsCOMPtr<nsIVariant> value(new ArrayOfUTF8StringsVariant(data));
  NS_ENSURE_TRUE(value, NS_ERROR_OUT_OF_MEMORY);

  return BindByIndex(aIndex, value);
}

NS_IMETHODIMP
BindingParams::BindArrayOfUTF8StringsByIndex(
    uint32_t aIndex, const nsTArray<nsCString>& aValue) {
  NS_ENSURE_ARG_MAX(aValue.Length(), INT_MAX);
  std::pair<const void*, int> data(static_cast<const void*>(aValue.Elements()),
                                   int(aValue.Length()));
  // The variant will make a copy of all the buffers.
  nsCOMPtr<nsIVariant> value(new ArrayOfUTF8StringsVariant(data));
  NS_ENSURE_TRUE(value, NS_ERROR_OUT_OF_MEMORY);

  return BindByIndex(aIndex, value);
}

}  // namespace mozilla::storage
