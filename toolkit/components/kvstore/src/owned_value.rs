/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use nsstring::{nsCString, nsString};
use rkv::OwnedValue;
use std::convert::TryInto;
use storage_variant::{DataType, NsIVariantExt, VariantType};
use xpcom::{interfaces::nsIVariant, RefPtr};

use crate::error::KeyValueError;

pub fn owned_to_variant(owned: OwnedValue) -> Result<RefPtr<nsIVariant>, KeyValueError> {
    match owned {
        OwnedValue::Bool(val) => Ok(val.into_variant()),
        OwnedValue::I64(val) => Ok(val.into_variant()),
        OwnedValue::F64(val) => Ok(val.into_variant()),
        OwnedValue::Str(ref val) => Ok(nsString::from(val).into_variant()),

        // kvstore doesn't (yet?) support these types of OwnedValue,
        // and we should never encounter them, but we need to exhaust
        // all possible variants of the OwnedValue enum.
        OwnedValue::Instant(_) => Err(KeyValueError::UnsupportedOwned),
        OwnedValue::Json(_) => Err(KeyValueError::UnsupportedOwned),
        OwnedValue::U64(_) => Err(KeyValueError::UnsupportedOwned),
        OwnedValue::Uuid(_) => Err(KeyValueError::UnsupportedOwned),
        OwnedValue::Blob(_) => Err(KeyValueError::UnsupportedOwned),
    }
}

pub fn variant_to_owned(variant: &nsIVariant) -> Result<Option<OwnedValue>, KeyValueError> {
    let data_type = variant.get_data_type();

    match data_type.try_into() {
        Ok(DataType::Int32) => {
            let mut val: i32 = 0;
            unsafe { variant.GetAsInt32(&mut val) }.to_result()?;
            Ok(Some(OwnedValue::I64(val.into())))
        }
        Ok(DataType::Int64) => {
            let mut val: i64 = 0;
            unsafe { variant.GetAsInt64(&mut val) }.to_result()?;
            Ok(Some(OwnedValue::I64(val)))
        }
        Ok(DataType::Double) => {
            let mut val: f64 = 0.0;
            unsafe { variant.GetAsDouble(&mut val) }.to_result()?;
            Ok(Some(OwnedValue::F64(val)))
        }
        Ok(DataType::CString)
        | Ok(DataType::CharStr)
        | Ok(DataType::StringSizeIs)
        | Ok(DataType::Utf8String) => {
            let mut val: nsCString = nsCString::new();
            unsafe { variant.GetAsAUTF8String(&mut *val) }.to_result()?;
            let s = std::str::from_utf8(&val)?;
            Ok(Some(OwnedValue::Str(s.into())))
        }
        Ok(DataType::AString) | Ok(DataType::WCharStr) | Ok(DataType::WStringSizeIs) => {
            let mut val: nsString = nsString::new();
            unsafe { variant.GetAsAString(&mut *val) }.to_result()?;
            let str = String::from_utf16(&val)?;
            Ok(Some(OwnedValue::Str(str)))
        }
        Ok(DataType::Bool) => {
            let mut val: bool = false;
            unsafe { variant.GetAsBool(&mut val) }.to_result()?;
            Ok(Some(OwnedValue::Bool(val)))
        }
        Ok(DataType::Void) | Ok(DataType::EmptyArray) | Ok(DataType::Empty) => Ok(None),
        Err(_) => Err(KeyValueError::UnsupportedVariant(data_type)),
    }
}
