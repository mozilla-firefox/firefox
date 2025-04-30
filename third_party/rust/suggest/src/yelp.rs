/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

use rusqlite::types::ToSqlOutput;
use rusqlite::{named_params, Result as RusqliteResult, ToSql};
use sql_support::ConnExt;
use url::form_urlencoded;

use crate::{
    db::SuggestDao,
    provider::SuggestionProvider,
    rs::{DownloadedYelpSuggestion, SuggestRecordId},
    suggestion::Suggestion,
    Result, SuggestionQuery,
};

#[derive(Clone, Copy, Debug, Eq, PartialEq, Hash)]
#[repr(u8)]
enum Modifier {
    Pre = 0,
    Post = 1,
    Yelp = 2,
    LocationSign = 3,
}

impl ToSql for Modifier {
    fn to_sql(&self) -> RusqliteResult<ToSqlOutput<'_>> {
        Ok(ToSqlOutput::from(*self as u8))
    }
}

#[derive(Clone, Copy, Eq, PartialEq)]
enum FindFrom {
    First,
    Last,
}

/// This module assumes like following query.
/// "Yelp-modifier? Pre-modifier? Subject Post-modifier? (Location-modifier | Location-sign Location?)? Yelp-modifier?"
/// For example, the query below is valid.
/// "Yelp (Yelp-modifier) Best(Pre-modifier) Ramen(Subject) Delivery(Post-modifier) In(Location-sign) Tokyo(Location)"
/// Also, as everything except Subject is optional, "Ramen" will be also valid query.
/// However, "Best Best Ramen" and "Ramen Best" is out of the above appearance order rule,
/// parsing will be failed. Also, every words except Location needs to be registered in DB.
/// Please refer to the query test in store.rs for all of combination.
/// Currently, the maximum query length is determined while referring to having word lengths in DB
/// and location names.
/// max subject: 50 + pre-modifier: 10 + post-modifier: 10 + location-sign: 7 + location: 50 = 127 = 150.
const MAX_QUERY_LENGTH: usize = 150;

/// The max number of words consisting the modifier. To improve the SQL performance by matching with
/// "keyword=:modifier" (please see is_modifier()), define this how many words we should check.
const MAX_MODIFIER_WORDS_NUMBER: usize = 2;

/// At least this many characters must be typed for a subject to be matched.
const SUBJECT_PREFIX_MATCH_THRESHOLD: usize = 2;

impl SuggestDao<'_> {
    /// Inserts the suggestions for Yelp attachment into the database.
    pub(crate) fn insert_yelp_suggestions(
        &mut self,
        record_id: &SuggestRecordId,
        suggestion: &DownloadedYelpSuggestion,
    ) -> Result<()> {
        for keyword in &suggestion.subjects {
            self.scope.err_if_interrupted()?;
            self.conn.execute_cached(
                "INSERT INTO yelp_subjects(record_id, keyword) VALUES(:record_id, :keyword)",
                named_params! {
                    ":record_id": record_id.as_str(),
                    ":keyword": keyword,
                },
            )?;
        }

        for keyword in &suggestion.pre_modifiers {
            self.scope.err_if_interrupted()?;
            self.conn.execute_cached(
                "INSERT INTO yelp_modifiers(record_id, type, keyword) VALUES(:record_id, :type, :keyword)",
                named_params! {
                    ":record_id": record_id.as_str(),
                    ":type": Modifier::Pre,
                    ":keyword": keyword,
                },
            )?;
        }

        for keyword in &suggestion.post_modifiers {
            self.scope.err_if_interrupted()?;
            self.conn.execute_cached(
                "INSERT INTO yelp_modifiers(record_id, type, keyword) VALUES(:record_id, :type, :keyword)",
                named_params! {
                    ":record_id": record_id.as_str(),
                    ":type": Modifier::Post,
                    ":keyword": keyword,
                },
            )?;
        }

        for keyword in &suggestion.yelp_modifiers {
            self.scope.err_if_interrupted()?;
            self.conn.execute_cached(
                "INSERT INTO yelp_modifiers(record_id, type, keyword) VALUES(:record_id, :type, :keyword)",
                named_params! {
                    ":record_id": record_id.as_str(),
                    ":type": Modifier::Yelp,
                    ":keyword": keyword,
                },
            )?;
        }

        for keyword in &suggestion.location_signs {
            self.scope.err_if_interrupted()?;
            self.conn.execute_cached(
                "INSERT INTO yelp_modifiers(record_id, type, keyword) VALUES(:record_id, :type, :keyword)",
                named_params! {
                    ":record_id": record_id.as_str(),
                    ":type": Modifier::LocationSign,
                    ":keyword": keyword,
                },
            )?;
        }

        self.scope.err_if_interrupted()?;
        self.conn.execute_cached(
            "INSERT INTO yelp_custom_details(record_id, icon_id, score) VALUES(:record_id, :icon_id, :score)",
            named_params! {
                ":record_id": record_id.as_str(),
                ":icon_id": suggestion.icon_id,
                ":score": suggestion.score,
            },
        )?;

        Ok(())
    }

    /// Fetch Yelp suggestion from given user's query.
    pub(crate) fn fetch_yelp_suggestions(
        &self,
        query: &SuggestionQuery,
    ) -> Result<Vec<Suggestion>> {
        if !query.providers.contains(&SuggestionProvider::Yelp) {
            return Ok(vec![]);
        }

        if query.keyword.len() > MAX_QUERY_LENGTH {
            return Ok(vec![]);
        }

        let query_vec: Vec<_> = query.keyword.split_whitespace().collect();
        let mut query_words: &[&str] = &query_vec;

        let pre_yelp_modifier_tuple =
            self.find_modifier(query_words, Modifier::Yelp, FindFrom::First)?;
        if let Some((_, rest)) = pre_yelp_modifier_tuple {
            query_words = rest;
        }

        let pre_modifier_tuple = self.find_modifier(query_words, Modifier::Pre, FindFrom::First)?;
        if let Some((_, rest)) = pre_modifier_tuple {
            query_words = rest;
        }

        let Some(subject_tuple) = self.find_subject(query_words)? else {
            return Ok(vec![]);
        };
        query_words = subject_tuple.2;

        let post_modifier_tuple =
            self.find_modifier(query_words, Modifier::Post, FindFrom::First)?;
        if let Some((_, rest)) = post_modifier_tuple {
            query_words = rest;
        }

        let location_sign_tuple =
            self.find_modifier(query_words, Modifier::LocationSign, FindFrom::First)?;
        if let Some((_, rest)) = location_sign_tuple {
            query_words = rest;
        }

        let post_yelp_modifier_tuple =
            self.find_modifier(query_words, Modifier::Yelp, FindFrom::Last)?;
        if let Some((_, rest)) = post_yelp_modifier_tuple {
            query_words = rest;
        }

        let location = if query_words.is_empty() {
            None
        } else {
            Some(query_words.join(" "))
        };

        let (icon, icon_mimetype, score) = self.fetch_custom_details()?;
        let builder = SuggestionBuilder {
            subject: &subject_tuple.0,
            subject_exact_match: subject_tuple.1,
            pre_modifier: pre_modifier_tuple.map(|(words, _)| words.to_string()),
            post_modifier: post_modifier_tuple.map(|(words, _)| words.to_string()),
            location_sign: location_sign_tuple.map(|(words, _)| words.to_string()),
            location,
            icon,
            icon_mimetype,
            score,
        };
        Ok(vec![builder.into()])
    }

    /// Find the modifier for given query and modifier type.
    /// Find from last word, if set FindFrom::Last to find_from.
    /// It returns Option<tuple> as follows:
    /// (
    ///   String: The keyword in DB (but the case is inherited by query).
    ///   &[&str]: Words after removed matching modifier.
    /// )
    fn find_modifier<'a>(
        &self,
        query_words: &'a [&'a str],
        modifier_type: Modifier,
        find_from: FindFrom,
    ) -> Result<Option<(String, &'a [&'a str])>> {
        if query_words.is_empty() {
            return Ok(None);
        }

        for n in (1..=std::cmp::min(MAX_MODIFIER_WORDS_NUMBER, query_words.len())).rev() {
            let Some((candidate_chunk, rest)) = (match find_from {
                FindFrom::First => query_words.split_at_checked(n),
                FindFrom::Last => query_words
                    .split_at_checked(query_words.len() - n)
                    .map(|(front, back)| (back, front)),
            }) else {
                continue;
            };

            let mut candidate = candidate_chunk.join(" ");

            if let Some(keyword_lowercase) = self.conn.try_query_one::<String, _>(
                if n == query_words.len() {
                    "
                    SELECT keyword FROM yelp_modifiers
                    WHERE type = :type AND keyword BETWEEN :word AND :word || x'FFFF'
                    LIMIT 1
                    "
                } else {
                    "
                    SELECT keyword FROM yelp_modifiers
                    WHERE type = :type AND keyword = :word
                    LIMIT 1
                    "
                },
                named_params! {
                    ":type": modifier_type,
                    ":word": candidate.to_lowercase(),
                },
                true,
            )? {
                // Preserve the query as the user typed it including its case.
                candidate.push_str(keyword_lowercase.get(candidate.len()..).unwrap_or_default());
                return Ok(Some((candidate, rest)));
            }
        }

        Ok(None)
    }

    /// Find the subject for given query.
    /// It returns Option<tuple> as follows:
    /// (
    ///   String: The keyword in DB (but the case is inherited by query).
    ///   bool: Whether or not the keyword is exact match.
    ///   &[&str]: Words after removed matching subject.
    /// )
    fn find_subject<'a>(
        &self,
        query_words: &'a [&'a str],
    ) -> Result<Option<(String, bool, &'a [&'a str])>> {
        if query_words.is_empty() {
            return Ok(None);
        }

        let mut query_string = query_words.join(" ");

        // This checks if keyword is a substring of the query.
        if let Some(keyword_lowercase) = self.conn.try_query_one::<String, _>(
            "SELECT keyword
             FROM yelp_subjects
             WHERE :query BETWEEN keyword AND keyword || ' ' || x'FFFF'
             ORDER BY LENGTH(keyword) ASC, keyword ASC
             LIMIT 1",
            named_params! {
                ":query": query_string.to_lowercase(),
            },
            true,
        )? {
            // Preserve the query as the user typed it including its case.
            return Ok(query_string.get(0..keyword_lowercase.len()).map(|keyword| {
                let count = keyword.split_whitespace().count();
                (
                    keyword.to_string(),
                    true,
                    query_words.get(count..).unwrap_or_default(),
                )
            }));
        };

        if query_string.len() < SUBJECT_PREFIX_MATCH_THRESHOLD {
            return Ok(None);
        }

        // Oppositely, this checks if the query is a substring of keyword.
        if let Some(keyword_lowercase) = self.conn.try_query_one::<String, _>(
            "SELECT keyword
             FROM yelp_subjects
             WHERE keyword BETWEEN :query AND :query || x'FFFF'
             ORDER BY LENGTH(keyword) ASC, keyword ASC
             LIMIT 1",
            named_params! {
                ":query": query_string.to_lowercase(),
            },
            true,
        )? {
            // Preserve the query as the user typed it including its case.
            return Ok(keyword_lowercase
                .get(query_string.len()..)
                .map(|keyword_rest| {
                    query_string.push_str(keyword_rest);
                    let count =
                        std::cmp::min(query_words.len(), query_string.split_whitespace().count());
                    (
                        query_string,
                        false,
                        query_words.get(count..).unwrap_or_default(),
                    )
                }));
        };

        Ok(None)
    }

    /// Fetch the custom details for Yelp suggestions.
    /// It returns the location tuple as follows:
    /// (
    ///   Option<Vec<u8>>: Icon data. If not found, returns None.
    ///   Option<String>: Mimetype of the icon data. If not found, returns None.
    ///   f64: Reflects score field in the yelp_custom_details table.
    /// )
    ///
    /// Note that there should be only one record in `yelp_custom_details`
    /// as all the Yelp assets are stored in the attachment of a single record
    /// on Remote Settings. The following query will perform a table scan against
    /// `yelp_custom_details` followed by an index search against `icons`,
    /// which should be fine since there is only one record in the first table.
    fn fetch_custom_details(&self) -> Result<(Option<Vec<u8>>, Option<String>, f64)> {
        let result = self.conn.query_row_and_then_cachable(
            r#"
            SELECT
              i.data, i.mimetype, y.score
            FROM
              yelp_custom_details y
            LEFT JOIN
              icons i
              ON y.icon_id = i.id
            LIMIT
              1
            "#,
            (),
            |row| -> Result<_> {
                Ok((
                    row.get::<_, Option<Vec<u8>>>(0)?,
                    row.get::<_, Option<String>>(1)?,
                    row.get::<_, f64>(2)?,
                ))
            },
            true,
        )?;

        Ok(result)
    }
}

struct SuggestionBuilder<'a> {
    subject: &'a str,
    subject_exact_match: bool,
    pre_modifier: Option<String>,
    post_modifier: Option<String>,
    location_sign: Option<String>,
    location: Option<String>,
    icon: Option<Vec<u8>>,
    icon_mimetype: Option<String>,
    score: f64,
}

impl<'a> From<SuggestionBuilder<'a>> for Suggestion {
    fn from(builder: SuggestionBuilder<'a>) -> Suggestion {
        let description = [
            builder.pre_modifier.as_deref(),
            Some(builder.subject),
            builder.post_modifier.as_deref(),
        ]
        .iter()
        .flatten()
        .copied()
        .collect::<Vec<_>>()
        .join(" ");

        // https://www.yelp.com/search?find_desc={description}&find_loc={location}
        let mut url = String::from("https://www.yelp.com/search?");
        let mut parameters = form_urlencoded::Serializer::new(String::new());
        parameters.append_pair("find_desc", &description);
        if let Some(location) = &builder.location {
            parameters.append_pair("find_loc", location);
        }
        url.push_str(&parameters.finish());

        let title = [
            builder.pre_modifier.as_deref(),
            Some(builder.subject),
            builder.post_modifier.as_deref(),
            builder.location_sign.as_deref(),
            builder.location.as_deref(),
        ]
        .iter()
        .flatten()
        .copied()
        .collect::<Vec<_>>()
        .join(" ");

        Suggestion::Yelp {
            url,
            title,
            icon: builder.icon,
            icon_mimetype: builder.icon_mimetype,
            score: builder.score,
            has_location_sign: builder.location_sign.is_some(),
            subject_exact_match: builder.subject_exact_match,
            location_param: "find_loc".to_string(),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::{store::tests::TestStore, testing::*, SuggestIngestionConstraints};

    #[test]
    fn yelp_functions() -> anyhow::Result<()> {
        before_each();

        let store = TestStore::new(
            MockRemoteSettingsClient::default()
                .with_record(SuggestionProvider::Yelp.record("data-4", json!([ramen_yelp()])))
                .with_record(SuggestionProvider::Yelp.icon(yelp_favicon())),
        );

        store.ingest(SuggestIngestionConstraints {
            providers: Some(vec![SuggestionProvider::Yelp]),
            ..SuggestIngestionConstraints::all_providers()
        });

        store.read(|dao| {
            type FindModifierTestCase<'a> =
                (&'a str, Modifier, FindFrom, Option<(String, &'a [&'a str])>);
            let find_modifer_tests: &[FindModifierTestCase] = &[
                // Query, Modifier, FindFrom, Expected result.
                ("", Modifier::Pre, FindFrom::First, None),
                ("", Modifier::Post, FindFrom::First, None),
                ("", Modifier::Yelp, FindFrom::First, None),
                // Single word modifier.
                (
                    "b",
                    Modifier::Pre,
                    FindFrom::First,
                    Some(("best".to_string(), &[])),
                ),
                (
                    "be",
                    Modifier::Pre,
                    FindFrom::First,
                    Some(("best".to_string(), &[])),
                ),
                (
                    "bes",
                    Modifier::Pre,
                    FindFrom::First,
                    Some(("best".to_string(), &[])),
                ),
                (
                    "best",
                    Modifier::Pre,
                    FindFrom::First,
                    Some(("best".to_string(), &[])),
                ),
                (
                    "best ",
                    Modifier::Pre,
                    FindFrom::First,
                    Some(("best".to_string(), &[])),
                ),
                (
                    "best r",
                    Modifier::Pre,
                    FindFrom::First,
                    Some(("best".to_string(), &["r"])),
                ),
                (
                    "best ramen",
                    Modifier::Pre,
                    FindFrom::First,
                    Some(("best".to_string(), &["ramen"])),
                ),
                (
                    "best spicy ramen",
                    Modifier::Pre,
                    FindFrom::First,
                    Some(("best".to_string(), &["spicy", "ramen"])),
                ),
                (
                    "delivery",
                    Modifier::Post,
                    FindFrom::First,
                    Some(("delivery".to_string(), &[])),
                ),
                (
                    "yelp",
                    Modifier::Yelp,
                    FindFrom::First,
                    Some(("yelp".to_string(), &[])),
                ),
                (
                    "same_modifier",
                    Modifier::Pre,
                    FindFrom::First,
                    Some(("same_modifier".to_string(), &[])),
                ),
                (
                    "same_modifier",
                    Modifier::Post,
                    FindFrom::First,
                    Some(("same_modifier".to_string(), &[])),
                ),
                ("same_modifier", Modifier::Yelp, FindFrom::First, None),
                // Multiple word modifier.
                (
                    "s",
                    Modifier::Pre,
                    FindFrom::First,
                    Some(("same_modifier".to_string(), &[])),
                ),
                (
                    "su",
                    Modifier::Pre,
                    FindFrom::First,
                    Some(("super best".to_string(), &[])),
                ),
                (
                    "super",
                    Modifier::Pre,
                    FindFrom::First,
                    Some(("super best".to_string(), &[])),
                ),
                (
                    "super b",
                    Modifier::Pre,
                    FindFrom::First,
                    Some(("super best".to_string(), &[])),
                ),
                (
                    "super be",
                    Modifier::Pre,
                    FindFrom::First,
                    Some(("super best".to_string(), &[])),
                ),
                (
                    "super bes",
                    Modifier::Pre,
                    FindFrom::First,
                    Some(("super best".to_string(), &[])),
                ),
                (
                    "super best",
                    Modifier::Pre,
                    FindFrom::First,
                    Some(("super best".to_string(), &[])),
                ),
                (
                    "super best ramen",
                    Modifier::Pre,
                    FindFrom::First,
                    Some(("super best".to_string(), &["ramen"])),
                ),
                (
                    "super delivery",
                    Modifier::Post,
                    FindFrom::First,
                    Some(("super delivery".to_string(), &[])),
                ),
                (
                    "yelp keyword",
                    Modifier::Yelp,
                    FindFrom::First,
                    Some(("yelp keyword".to_string(), &[])),
                ),
                // Different modifier or findfrom.
                ("best ramen", Modifier::Post, FindFrom::First, None),
                ("best ramen", Modifier::Yelp, FindFrom::First, None),
                ("best ramen", Modifier::Pre, FindFrom::Last, None),
                (
                    "ramen best",
                    Modifier::Pre,
                    FindFrom::Last,
                    Some(("best".to_string(), &["ramen"])),
                ),
                // Keywords similar to modifire.
                ("bestabc", Modifier::Post, FindFrom::First, None),
                ("bestabc ramen", Modifier::Post, FindFrom::First, None),
                // Keep chars case.
                (
                    "BeSt SpIcY rAmEn",
                    Modifier::Pre,
                    FindFrom::First,
                    Some(("BeSt".to_string(), &["SpIcY", "rAmEn"])),
                ),
                (
                    "SpIcY rAmEn DeLiVeRy",
                    Modifier::Post,
                    FindFrom::Last,
                    Some(("DeLiVeRy".to_string(), &["SpIcY", "rAmEn"])),
                ),
                // Prefix match is available only for last words.
                ("be ramen", Modifier::Pre, FindFrom::First, None),
                ("bes ramen", Modifier::Pre, FindFrom::First, None),
            ];
            for (query, modifier, findfrom, expected) in find_modifer_tests {
                assert_eq!(
                    dao.find_modifier(
                        &query.split_whitespace().collect::<Vec<_>>(),
                        *modifier,
                        *findfrom
                    )?,
                    *expected
                );
            }

            type FindSubjectTestCase<'a> = (&'a str, Option<(String, bool, &'a [&'a str])>);
            let find_subject_tests: &[FindSubjectTestCase] = &[
                // Query, Expected result.
                ("", None),
                ("r", None),
                ("ra", Some(("rats".to_string(), false, &[]))),
                ("ram", Some(("ramen".to_string(), false, &[]))),
                ("rame", Some(("ramen".to_string(), false, &[]))),
                ("ramen", Some(("ramen".to_string(), true, &[]))),
                ("spi", Some(("spicy ramen".to_string(), false, &[]))),
                ("spicy ra ", Some(("spicy ramen".to_string(), false, &[]))),
                ("spicy ramen", Some(("spicy ramen".to_string(), true, &[]))),
                (
                    "spicy ramen gogo",
                    Some(("spicy ramen".to_string(), true, &["gogo"])),
                ),
                (
                    "SpIcY rAmEn GoGo",
                    Some(("SpIcY rAmEn".to_string(), true, &["GoGo"])),
                ),
                ("ramenabc", None),
                ("ramenabc xyz", None),
                ("spicy ramenabc", None),
                ("spicy ramenabc xyz", None),
                ("ramen abc", Some(("ramen".to_string(), true, &["abc"]))),
            ];
            for (query, expected) in find_subject_tests {
                assert_eq!(
                    dao.find_subject(&query.split_whitespace().collect::<Vec<_>>())?,
                    *expected
                );
            }

            type FindLocationSignTestCase<'a> = (&'a str, Option<(String, &'a [&'a str])>);
            let find_location_sign_tests: &[FindLocationSignTestCase] = &[
                // Query, Expected result.
                ("", None),
                ("n", Some(("near".to_string(), &[]))),
                ("ne", Some(("near".to_string(), &[]))),
                ("nea", Some(("near".to_string(), &[]))),
                ("near", Some(("near".to_string(), &[]))),
                ("near ", Some(("near".to_string(), &[]))),
                ("near b", Some(("near by".to_string(), &[]))),
                ("near by", Some(("near by".to_string(), &[]))),
                ("near by a", Some(("near by".to_string(), &["a"]))),
                // Prefix match is available only for last words.
                ("nea r", None),
            ];
            for (query, expected) in find_location_sign_tests {
                assert_eq!(
                    dao.find_modifier(
                        &query.split_whitespace().collect::<Vec<_>>(),
                        Modifier::LocationSign,
                        FindFrom::First
                    )?,
                    *expected
                );
            }

            Ok(())
        })?;

        Ok(())
    }
}
