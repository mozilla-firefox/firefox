// This file is part of ICU4X. For terms of use, please see the file
// called LICENSE at the top level of the ICU4X source tree
// (online at: https://github.com/unicode-org/icu4x/blob/main/LICENSE ).

use crate::codepointtrie::{
    CodePointTrie, CodePointTrieHeader, FastCodePointTrie, SmallCodePointTrie, TrieValue,
    TypedCodePointTrie,
};
use serde::{de::Error, Deserialize, Deserializer, Serialize, Serializer};
use zerofrom::ZeroFrom;
use zerovec::ZeroVec;

#[derive(Serialize, Deserialize)]
pub struct CodePointTrieSerde<'trie, T: TrieValue> {
    header: CodePointTrieHeader,
    #[serde(borrow)]
    index: ZeroVec<'trie, u16>,
    #[serde(borrow)]
    data: ZeroVec<'trie, T>,
}

impl<T: TrieValue + Serialize> Serialize for CodePointTrie<'_, T> {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        let ser = CodePointTrieSerde {
            header: self.header,
            index: ZeroFrom::zero_from(&self.index),
            data: ZeroFrom::zero_from(&self.data),
        };
        ser.serialize(serializer)
    }
}

impl<T: TrieValue + Serialize> Serialize for SmallCodePointTrie<'_, T> {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        self.as_untyped_ref().serialize(serializer)
    }
}

impl<T: TrieValue + Serialize> Serialize for FastCodePointTrie<'_, T> {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        self.as_untyped_ref().serialize(serializer)
    }
}

impl<'de, 'trie, T: TrieValue + Deserialize<'de>> Deserialize<'de> for CodePointTrie<'trie, T>
where
    'de: 'trie,
{
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        let de = CodePointTrieSerde::deserialize(deserializer)?;
        // SAFETY:
        // `validate_fields` upholds the invariants for the fields that
        // fast-path access without bound checks relies on.
        let error_value = match CodePointTrie::validate_fields(&de.header, &de.index, &de.data) {
            Ok(v) => v,
            Err(e) => {
                match e {
                    super::CodePointTrieError::FromDeserialized { reason } => {
                        // Not supposed to be returned by `validate_fields`.
                        debug_assert!(false);
                        return Err(D::Error::custom(reason));
                    }
                    super::CodePointTrieError::EmptyDataVector => {
                        return Err(D::Error::custom("CodePointTrie must be constructed from data vector with at least one element"));
                    }
                    super::CodePointTrieError::IndexTooShortForFastAccess => {
                        return Err(D::Error::custom("CodePointTrie must be constructed from index vector long enough to accommodate fast-path access"));
                    }
                    super::CodePointTrieError::DataTooShortForFastAccess => {
                        return Err(D::Error::custom("CodePointTrie must be constructed from data vector long enough to accommodate fast-path access"));
                    }
                    super::CodePointTrieError::DataTooShortForAsciiAccess => {
                        return Err(D::Error::custom("CodePointTrie must be constructed from data vector long enough to accommodate direct ASCII access"));
                    }
                    super::CodePointTrieError::IndexInconsistentWithAsciiAccess => {
                        return Err(D::Error::custom("CodePointTrie must be constructed from index vector whose first two items are consistent with direct ASCII access"));
                    }
                }
            }
        };
        // Field invariants upheld: Checked by `validate_fields` above.
        Ok(CodePointTrie {
            header: de.header,
            index: de.index,
            data: de.data,
            error_value,
        })
    }
}

impl<'de, 'trie, T: TrieValue + Deserialize<'de>> Deserialize<'de> for SmallCodePointTrie<'trie, T>
where
    'de: 'trie,
{
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        let untyped_de = CodePointTrie::deserialize(deserializer)?;
        let Ok(de) = <SmallCodePointTrie<_>>::try_from(untyped_de) else {
            return Err(D::Error::custom(
                "SmallCodePointTrie must have small-mode data",
            ));
        };
        Ok(de)
    }
}

impl<'de, 'trie, T: TrieValue + Deserialize<'de>> Deserialize<'de> for FastCodePointTrie<'trie, T>
where
    'de: 'trie,
{
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        let untyped_de = CodePointTrie::deserialize(deserializer)?;
        let Ok(de) = <FastCodePointTrie<_>>::try_from(untyped_de) else {
            return Err(D::Error::custom(
                "FastCodePointTrie must have fast-mode data",
            ));
        };
        Ok(de)
    }
}
