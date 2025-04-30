/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::collections::HashSet;

use crate::{LabeledTimingSample, Suggestion, SuggestionProvider, SuggestionProviderConstraints};

/// A query for suggestions to show in the address bar.
#[derive(Clone, Debug, Default, uniffi::Record)]
pub struct SuggestionQuery {
    pub keyword: String,
    pub providers: Vec<SuggestionProvider>,
    #[uniffi(default = None)]
    pub provider_constraints: Option<SuggestionProviderConstraints>,
    #[uniffi(default = None)]
    pub limit: Option<i32>,
}

#[derive(uniffi::Record)]
pub struct QueryWithMetricsResult {
    pub suggestions: Vec<Suggestion>,
    /// Samples for the `suggest.query_time` metric
    pub query_times: Vec<LabeledTimingSample>,
}

impl SuggestionQuery {
    // Builder style methods for creating queries (mostly used by the test code)

    pub fn all_providers(keyword: &str) -> Self {
        Self {
            keyword: keyword.to_string(),
            providers: Vec::from(SuggestionProvider::all()),
            ..Self::default()
        }
    }

    pub fn with_providers(keyword: &str, providers: Vec<SuggestionProvider>) -> Self {
        Self {
            keyword: keyword.to_string(),
            providers,
            ..Self::default()
        }
    }

    pub fn all_providers_except(keyword: &str, provider: SuggestionProvider) -> Self {
        Self::with_providers(
            keyword,
            SuggestionProvider::all()
                .into_iter()
                .filter(|p| *p != provider)
                .collect(),
        )
    }

    pub fn amp(keyword: &str) -> Self {
        Self {
            keyword: keyword.into(),
            providers: vec![SuggestionProvider::Amp],
            ..Self::default()
        }
    }

    pub fn wikipedia(keyword: &str) -> Self {
        Self {
            keyword: keyword.into(),
            providers: vec![SuggestionProvider::Wikipedia],
            ..Self::default()
        }
    }

    pub fn amo(keyword: &str) -> Self {
        Self {
            keyword: keyword.into(),
            providers: vec![SuggestionProvider::Amo],
            ..Self::default()
        }
    }

    pub fn pocket(keyword: &str) -> Self {
        Self {
            keyword: keyword.into(),
            providers: vec![SuggestionProvider::Pocket],
            ..Self::default()
        }
    }

    pub fn yelp(keyword: &str) -> Self {
        Self {
            keyword: keyword.into(),
            providers: vec![SuggestionProvider::Yelp],
            ..Self::default()
        }
    }

    pub fn mdn(keyword: &str) -> Self {
        Self {
            keyword: keyword.into(),
            providers: vec![SuggestionProvider::Mdn],
            ..Self::default()
        }
    }

    pub fn fakespot(keyword: &str) -> Self {
        Self {
            keyword: keyword.into(),
            providers: vec![SuggestionProvider::Fakespot],
            ..Self::default()
        }
    }

    pub fn weather(keyword: &str) -> Self {
        Self {
            keyword: keyword.into(),
            providers: vec![SuggestionProvider::Weather],
            ..Self::default()
        }
    }

    pub fn dynamic(keyword: &str, suggestion_types: &[&str]) -> Self {
        Self {
            keyword: keyword.into(),
            providers: vec![SuggestionProvider::Dynamic],
            provider_constraints: Some(SuggestionProviderConstraints {
                dynamic_suggestion_types: Some(
                    suggestion_types.iter().map(|s| s.to_string()).collect(),
                ),
                ..SuggestionProviderConstraints::default()
            }),
            ..Self::default()
        }
    }

    pub fn limit(self, limit: i32) -> Self {
        Self {
            limit: Some(limit),
            ..self
        }
    }

    /// Create an FTS query term for our keyword(s)
    pub(crate) fn fts_query(&self) -> FtsQuery<'_> {
        FtsQuery::new(&self.keyword)
    }
}

pub struct FtsQuery<'a> {
    pub match_arg: String,
    pub match_arg_without_prefix_match: String,
    pub is_prefix_query: bool,
    keyword_terms: Vec<&'a str>,
}

impl<'a> FtsQuery<'a> {
    fn new(keyword: &'a str) -> Self {
        // Parse the `keyword` field into a set of keywords.
        //
        // This is used when passing the keywords into an FTS search.  It:
        //   - Strips out any `():^*"` chars.  These are typically used for advanced searches, which
        //     we don't support and it would be weird to only support for FTS searches.
        //   - splits on whitespace to get a list of individual keywords
        let keywords = Self::split_terms(keyword);
        if keywords.is_empty() {
            return Self {
                keyword_terms: keywords,
                match_arg: String::from(r#""""#),
                match_arg_without_prefix_match: String::from(r#""""#),
                is_prefix_query: false,
            };
        }
        // Quote each term from `query` and join them together
        let mut sqlite_match = keywords
            .iter()
            .map(|keyword| format!(r#""{keyword}""#))
            .collect::<Vec<_>>()
            .join(" ");
        // If the input is > 3 characters, and there's no whitespace at the end.
        // We want to append a `*` char to the end to do a prefix match on it.
        let total_chars = keywords.iter().fold(0, |count, s| count + s.len());
        let query_ends_in_whitespace = keyword.ends_with(' ');
        let prefix_match = (total_chars > 3) && !query_ends_in_whitespace;
        let sqlite_match_without_prefix_match = sqlite_match.clone();
        if prefix_match {
            sqlite_match.push('*');
        }
        Self {
            keyword_terms: keywords,
            is_prefix_query: prefix_match,
            match_arg: sqlite_match,
            match_arg_without_prefix_match: sqlite_match_without_prefix_match,
        }
    }

    /// Try to figure out if a FTS match required stemming
    ///
    /// To test this, we have to try to mimic the SQLite FTS logic. This code doesn't do it
    /// perfectly, but it should return the correct result most of the time.
    pub fn match_required_stemming(&self, title: &str) -> bool {
        let title = title.to_lowercase();
        let split_title = Self::split_terms(&title);

        !self.keyword_terms.iter().enumerate().all(|(i, keyword)| {
            split_title.iter().any(|title_word| {
                let last_keyword = i == self.keyword_terms.len() - 1;

                if last_keyword && self.is_prefix_query {
                    title_word.starts_with(keyword)
                } else {
                    title_word == keyword
                }
            })
        })
    }

    fn split_terms(phrase: &str) -> Vec<&str> {
        phrase
            .split([' ', '(', ')', ':', '^', '*', '"', ','])
            .filter(|s| !s.is_empty())
            .collect()
    }
}

/// Given a list of full keywords, create an FTS string to match against.
///
/// Creates a string with de-duped keywords.
pub fn full_keywords_to_fts_content<'a>(
    full_keywords: impl IntoIterator<Item = &'a str>,
) -> String {
    let parts: HashSet<_> = full_keywords
        .into_iter()
        .flat_map(str::split_whitespace)
        .map(str::to_lowercase)
        .collect();
    let mut result = String::new();
    for (i, part) in parts.into_iter().enumerate() {
        if i != 0 {
            result.push(' ');
        }
        result.push_str(&part);
    }
    result
}

#[cfg(test)]
mod test {
    use super::*;
    use std::collections::HashMap;

    fn check_parse_keywords(input: &str, expected: Vec<&str>) {
        let query = SuggestionQuery::all_providers(input);
        assert_eq!(query.fts_query().keyword_terms, expected);
    }

    #[test]
    fn test_quote() {
        check_parse_keywords("foo", vec!["foo"]);
        check_parse_keywords("foo bar", vec!["foo", "bar"]);
        // Special chars should be stripped
        check_parse_keywords("\"foo()* ^bar:\"", vec!["foo", "bar"]);
        // test some corner cases
        check_parse_keywords("", vec![]);
        check_parse_keywords(" ", vec![]);
        check_parse_keywords("   foo     bar       ", vec!["foo", "bar"]);
        check_parse_keywords("foo:bar", vec!["foo", "bar"]);
    }

    fn check_fts_query(input: &str, expected: &str) {
        let query = SuggestionQuery::all_providers(input);
        assert_eq!(query.fts_query().match_arg, expected);
    }

    #[test]
    fn test_fts_query() {
        // String with < 3 chars shouldn't get a prefix query
        check_fts_query("r", r#""r""#);
        check_fts_query("ru", r#""ru""#);
        check_fts_query("run", r#""run""#);
        // After 3 chars, we should append `*` to the last term to make it a prefix query
        check_fts_query("runn", r#""runn"*"#);
        check_fts_query("running", r#""running"*"#);
        // The total number of chars is counted, not the number of chars in the last term
        check_fts_query("running s", r#""running" "s"*"#);
        // if the input ends in whitespace, then don't do a prefix query
        check_fts_query("running ", r#""running""#);
        // Special chars are filtered out
        check_fts_query("running*\"()^: s", r#""running" "s"*"#);
        check_fts_query("running *\"()^: s", r#""running" "s"*"#);
        // Special chars shouldn't count towards the input size when deciding whether to do a
        // prefix query or not
        check_fts_query("r():", r#""r""#);
        // Test empty strings
        check_fts_query("", r#""""#);
        check_fts_query(" ", r#""""#);
        check_fts_query("()", r#""""#);
    }

    #[test]
    fn test_fts_query_match_required_stemming() {
        // These don't require stemming, since each keyword matches a term in the title
        assert!(!FtsQuery::new("running shoes").match_required_stemming("running shoes"));
        assert!(
            !FtsQuery::new("running shoes").match_required_stemming("new balance running shoes")
        );
        // Case changes shouldn't matter
        assert!(!FtsQuery::new("running shoes").match_required_stemming("Running Shoes"));
        // This doesn't require stemming, since `:` is not part of the word
        assert!(!FtsQuery::new("running shoes").match_required_stemming("Running: Shoes"));
        // This requires the keywords to be stemmed in order to match
        assert!(FtsQuery::new("run shoes").match_required_stemming("running shoes"));
        // This didn't require stemming, since the last keyword was a prefix match
        assert!(!FtsQuery::new("running sh").match_required_stemming("running shoes"));
        // This does require stemming (we know it wasn't a prefix match since there's not enough
        // characters).
        assert!(FtsQuery::new("run").match_required_stemming("running shoes"));
    }

    #[test]
    fn test_full_keywords_to_fts_content() {
        check_full_keywords_to_fts_content(["a", "b", "c"], "a b c");
        check_full_keywords_to_fts_content(["a", "b c"], "a b c");
        check_full_keywords_to_fts_content(["a", "b c a"], "a b c");
        check_full_keywords_to_fts_content(["a", "b C A"], "a b c");
    }

    fn check_full_keywords_to_fts_content<const N: usize>(input: [&str; N], expected: &str) {
        let mut expected_counts = HashMap::<&str, usize>::new();
        let mut actual_counts = HashMap::<&str, usize>::new();
        for term in expected.split_whitespace() {
            *expected_counts.entry(term).or_default() += 1;
        }
        let fts_content = full_keywords_to_fts_content(input);
        for term in fts_content.split_whitespace() {
            *actual_counts.entry(term).or_default() += 1;
        }
        assert_eq!(actual_counts, expected_counts);
    }
}
