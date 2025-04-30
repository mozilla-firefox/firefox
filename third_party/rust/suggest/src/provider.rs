/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

use std::{
    collections::{HashMap, HashSet},
    fmt,
};

use rusqlite::{
    types::{FromSql, FromSqlError, FromSqlResult, ToSql, ToSqlOutput, ValueRef},
    Result as RusqliteResult,
};

use crate::rs::{Collection, SuggestRecordType};

#[cfg(test)]
use serde_json::Value as JsonValue;

#[cfg(test)]
use crate::testing::{MockAttachment, MockIcon, MockRecord};

/// Record types from these providers will be ingested when consumers do not
/// specify providers in `SuggestIngestionConstraints`.
pub(crate) const DEFAULT_INGEST_PROVIDERS: [SuggestionProvider; 5] = [
    SuggestionProvider::Amp,
    SuggestionProvider::Wikipedia,
    SuggestionProvider::Amo,
    SuggestionProvider::Yelp,
    SuggestionProvider::Mdn,
];

/// A provider is a source of search suggestions.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Hash, uniffi::Enum)]
#[repr(u8)]
pub enum SuggestionProvider {
    Amp = 1,
    Wikipedia = 2,
    Amo = 3,
    Pocket = 4,
    Yelp = 5,
    Mdn = 6,
    Weather = 7,
    Fakespot = 8,
    Dynamic = 9,
}

impl fmt::Display for SuggestionProvider {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Amp => write!(f, "amp"),
            Self::Wikipedia => write!(f, "wikipedia"),
            Self::Amo => write!(f, "amo"),
            Self::Pocket => write!(f, "pocket"),
            Self::Yelp => write!(f, "yelp"),
            Self::Mdn => write!(f, "mdn"),
            Self::Weather => write!(f, "weather"),
            Self::Fakespot => write!(f, "fakespot"),
            Self::Dynamic => write!(f, "dynamic"),
        }
    }
}

impl FromSql for SuggestionProvider {
    fn column_result(value: ValueRef<'_>) -> FromSqlResult<Self> {
        let v = value.as_i64()?;
        u8::try_from(v)
            .ok()
            .and_then(SuggestionProvider::from_u8)
            .ok_or_else(|| FromSqlError::OutOfRange(v))
    }
}

impl SuggestionProvider {
    pub fn all() -> [Self; 9] {
        [
            Self::Amp,
            Self::Wikipedia,
            Self::Amo,
            Self::Pocket,
            Self::Yelp,
            Self::Mdn,
            Self::Weather,
            Self::Fakespot,
            Self::Dynamic,
        ]
    }

    #[inline]
    pub(crate) fn from_u8(v: u8) -> Option<Self> {
        match v {
            1 => Some(Self::Amp),
            2 => Some(Self::Wikipedia),
            3 => Some(Self::Amo),
            4 => Some(Self::Pocket),
            5 => Some(Self::Yelp),
            6 => Some(Self::Mdn),
            7 => Some(Self::Weather),
            8 => Some(Self::Fakespot),
            9 => Some(Self::Dynamic),
            _ => None,
        }
    }

    /// The collection that stores the provider's primary record.
    pub(crate) fn primary_collection(&self) -> Collection {
        match self {
            Self::Amp => Collection::Amp,
            Self::Fakespot => Collection::Fakespot,
            _ => Collection::Other,
        }
    }

    /// The provider's primary record type.
    pub(crate) fn primary_record_type(&self) -> SuggestRecordType {
        match self {
            Self::Amp => SuggestRecordType::Amp,
            Self::Wikipedia => SuggestRecordType::Wikipedia,
            Self::Amo => SuggestRecordType::Amo,
            Self::Pocket => SuggestRecordType::Pocket,
            Self::Yelp => SuggestRecordType::Yelp,
            Self::Mdn => SuggestRecordType::Mdn,
            Self::Weather => SuggestRecordType::Weather,
            Self::Fakespot => SuggestRecordType::Fakespot,
            Self::Dynamic => SuggestRecordType::Dynamic,
        }
    }

    /// Other record types and their collections that the provider depends on.
    fn secondary_record_types(&self) -> Option<HashMap<Collection, HashSet<SuggestRecordType>>> {
        match self {
            Self::Amp => Some(HashMap::from([(
                Collection::Amp,
                HashSet::from([SuggestRecordType::Icon]),
            )])),
            Self::Wikipedia => Some(HashMap::from([(
                Collection::Other,
                HashSet::from([SuggestRecordType::Icon]),
            )])),
            Self::Yelp => Some(HashMap::from([(
                Collection::Other,
                HashSet::from([SuggestRecordType::Icon, SuggestRecordType::Geonames]),
            )])),
            Self::Weather => Some(HashMap::from([(
                Collection::Other,
                HashSet::from([SuggestRecordType::Geonames]),
            )])),
            Self::Fakespot => Some(HashMap::from([(
                Collection::Fakespot,
                HashSet::from([SuggestRecordType::Icon]),
            )])),
            _ => None,
        }
    }

    /// All record types and their collections that the provider depends on,
    /// including primary and secondary records.
    pub(crate) fn record_types_by_collection(
        &self,
    ) -> HashMap<Collection, HashSet<SuggestRecordType>> {
        let mut rts = self.secondary_record_types().unwrap_or_default();
        rts.entry(self.primary_collection())
            .or_default()
            .insert(self.primary_record_type());
        rts
    }
}

impl ToSql for SuggestionProvider {
    fn to_sql(&self) -> RusqliteResult<ToSqlOutput<'_>> {
        Ok(ToSqlOutput::from(*self as u8))
    }
}

#[cfg(test)]
impl SuggestionProvider {
    pub fn record(&self, record_id: &str, attachment: JsonValue) -> MockRecord {
        self.full_record(record_id, None, Some(MockAttachment::Json(attachment)))
    }

    pub fn empty_record(&self, record_id: &str) -> MockRecord {
        self.full_record(record_id, None, None)
    }

    pub fn full_record(
        &self,
        record_id: &str,
        inline_data: Option<JsonValue>,
        attachment: Option<MockAttachment>,
    ) -> MockRecord {
        MockRecord {
            collection: self.primary_collection(),
            record_type: self.primary_record_type(),
            id: record_id.to_string(),
            inline_data,
            attachment,
        }
    }

    pub fn icon(&self, icon: MockIcon) -> MockRecord {
        MockRecord {
            collection: self.primary_collection(),
            record_type: SuggestRecordType::Icon,
            id: format!("icon-{}", icon.id),
            inline_data: None,
            attachment: Some(MockAttachment::Icon(icon)),
        }
    }
}

/// Some providers manage multiple suggestion subtypes. Queries, ingests, and
/// other operations on those providers must be constrained to a desired subtype.
#[derive(Clone, Default, Debug, uniffi::Record)]
pub struct SuggestionProviderConstraints {
    /// Which dynamic suggestions should we fetch or ingest? Corresponds to the
    /// `suggestion_type` value in dynamic suggestions remote settings records.
    #[uniffi(default = None)]
    pub dynamic_suggestion_types: Option<Vec<String>>,
    /// Which strategy should we use for the AMP queries?
    /// Use None for the default strategy.
    #[uniffi(default = None)]
    pub amp_alternative_matching: Option<AmpMatchingStrategy>,
}

#[derive(Clone, Debug, uniffi::Enum)]
pub enum AmpMatchingStrategy {
    /// Disable keywords added via keyword expansion.
    /// This eliminates keywords that for terms related to the "real" keywords, for example
    /// misspellings like "underarmor" instead of "under armor"'.
    NoKeywordExpansion = 1, // The desktop consumer assumes this starts at `1`
    /// Use FTS matching against the full keywords, joined together.
    FtsAgainstFullKeywords,
    /// Use FTS matching against the title field
    FtsAgainstTitle,
}

impl AmpMatchingStrategy {
    pub fn uses_fts(&self) -> bool {
        matches!(self, Self::FtsAgainstFullKeywords | Self::FtsAgainstTitle)
    }
}
