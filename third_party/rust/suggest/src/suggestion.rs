/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

use chrono::Local;

use crate::db::DEFAULT_SUGGESTION_SCORE;

/// The template parameter for a timestamp in a "raw" sponsored suggestion URL.
const TIMESTAMP_TEMPLATE: &str = "%YYYYMMDDHH%";

/// The length, in bytes, of a timestamp in a "cooked" sponsored suggestion URL.
///
/// Cooked timestamps don't include the leading or trailing `%`, so this is
/// 2 bytes shorter than [`TIMESTAMP_TEMPLATE`].
const TIMESTAMP_LENGTH: usize = 10;

/// A suggestion from the database to show in the address bar.
#[derive(Clone, Debug, PartialEq, uniffi::Enum)]
pub enum Suggestion {
    Amp {
        title: String,
        url: String,
        raw_url: String,
        icon: Option<Vec<u8>>,
        icon_mimetype: Option<String>,
        full_keyword: String,
        block_id: i64,
        advertiser: String,
        iab_category: String,
        impression_url: String,
        click_url: String,
        raw_click_url: String,
        score: f64,
        fts_match_info: Option<FtsMatchInfo>,
    },
    Pocket {
        title: String,
        url: String,
        score: f64,
        is_top_pick: bool,
    },
    Wikipedia {
        title: String,
        url: String,
        icon: Option<Vec<u8>>,
        icon_mimetype: Option<String>,
        full_keyword: String,
    },
    Amo {
        title: String,
        url: String,
        icon_url: String,
        description: String,
        rating: Option<String>,
        number_of_ratings: i64,
        guid: String,
        score: f64,
    },
    Yelp {
        url: String,
        title: String,
        icon: Option<Vec<u8>>,
        icon_mimetype: Option<String>,
        score: f64,
        has_location_sign: bool,
        subject_exact_match: bool,
        location_param: String,
    },
    Mdn {
        title: String,
        url: String,
        description: String,
        score: f64,
    },
    Weather {
        city: Option<String>,
        region: Option<String>,
        country: Option<String>,
        latitude: Option<f64>,
        longitude: Option<f64>,
        score: f64,
    },
    Fakespot {
        fakespot_grade: String,
        product_id: String,
        rating: f64,
        title: String,
        total_reviews: i64,
        url: String,
        icon: Option<Vec<u8>>,
        icon_mimetype: Option<String>,
        score: f64,
        // Details about the FTS match.  For performance reasons, this is only calculated for the
        // result with the highest score.  We assume that only one that will be shown to the user
        // and therefore the only one we'll collect metrics for.
        match_info: Option<FtsMatchInfo>,
    },
    Dynamic {
        suggestion_type: String,
        data: Option<serde_json::Value>,
        /// This value is optionally defined in the suggestion's remote settings
        /// data and is an opaque token used for dismissing the suggestion in
        /// lieu of a URL. If `Some`, the suggestion can be dismissed by passing
        /// the wrapped string to [crate::SuggestStore::dismiss_suggestion].
        dismissal_key: Option<String>,
        score: f64,
    },
}

/// Additional data about how an FTS match was made
#[derive(Debug, Clone, PartialEq, uniffi::Record)]
pub struct FtsMatchInfo {
    /// Was this a prefix match (`water b` matched against `water bottle`)
    pub prefix: bool,
    /// Did the match require stemming? (`run shoes` matched against `running shoes`)
    pub stemming: bool,
}

impl PartialOrd for Suggestion {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        Some(self.cmp(other))
    }
}

impl Ord for Suggestion {
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        other
            .score()
            .partial_cmp(&self.score())
            .unwrap_or(std::cmp::Ordering::Equal)
    }
}

impl Suggestion {
    /// Get the suggestion's dismissal key, which should be stored in the
    /// `dismissed_suggestions` table when the suggestion is dismissed. Some
    /// suggestions may not have dismissal keys and cannot be dismissed.
    pub fn dismissal_key(&self) -> Option<&str> {
        match self {
            Self::Amp { full_keyword, .. } => {
                if !full_keyword.is_empty() {
                    Some(full_keyword)
                } else {
                    self.raw_url()
                }
            }
            Self::Dynamic { dismissal_key, .. } => dismissal_key.as_deref(),
            Self::Pocket { .. }
            | Self::Wikipedia { .. }
            | Self::Amo { .. }
            | Self::Yelp { .. }
            | Self::Mdn { .. }
            | Self::Weather { .. }
            | Self::Fakespot { .. } => self.raw_url(),
        }
    }

    /// Get the URL for this suggestion, if present
    pub fn url(&self) -> Option<&str> {
        match self {
            Self::Amp { url, .. }
            | Self::Pocket { url, .. }
            | Self::Wikipedia { url, .. }
            | Self::Amo { url, .. }
            | Self::Yelp { url, .. }
            | Self::Mdn { url, .. }
            | Self::Fakespot { url, .. } => Some(url),
            Self::Weather { .. } | Self::Dynamic { .. } => None,
        }
    }

    /// Get the raw URL for this suggestion, if present
    ///
    /// This is the same as `url` except for Amp.  In that case, `url` is the URL after being
    /// "cooked" using template interpolation, while `raw_url` is the URL template.
    pub fn raw_url(&self) -> Option<&str> {
        match self {
            Self::Amp { raw_url, .. } => Some(raw_url),
            Self::Pocket { .. }
            | Self::Wikipedia { .. }
            | Self::Amo { .. }
            | Self::Yelp { .. }
            | Self::Mdn { .. }
            | Self::Weather { .. }
            | Self::Fakespot { .. }
            | Self::Dynamic { .. } => self.url(),
        }
    }

    pub fn title(&self) -> &str {
        match self {
            Self::Amp { title, .. }
            | Self::Pocket { title, .. }
            | Self::Wikipedia { title, .. }
            | Self::Amo { title, .. }
            | Self::Yelp { title, .. }
            | Self::Mdn { title, .. }
            | Self::Fakespot { title, .. } => title,
            _ => "untitled",
        }
    }

    pub fn icon_data(&self) -> Option<&[u8]> {
        match self {
            Self::Amp { icon, .. }
            | Self::Wikipedia { icon, .. }
            | Self::Yelp { icon, .. }
            | Self::Fakespot { icon, .. } => icon.as_deref(),
            _ => None,
        }
    }

    pub fn score(&self) -> f64 {
        match self {
            Self::Amp { score, .. }
            | Self::Pocket { score, .. }
            | Self::Amo { score, .. }
            | Self::Yelp { score, .. }
            | Self::Mdn { score, .. }
            | Self::Weather { score, .. }
            | Self::Fakespot { score, .. }
            | Self::Dynamic { score, .. } => *score,
            Self::Wikipedia { .. } => DEFAULT_SUGGESTION_SCORE,
        }
    }

    pub fn fts_match_info(&self) -> Option<&FtsMatchInfo> {
        match self {
            Self::Fakespot { match_info, .. } => match_info.as_ref(),
            _ => None,
        }
    }
}

#[cfg(test)]
/// Testing utilitise
impl Suggestion {
    pub fn with_fakespot_keyword_bonus(mut self) -> Self {
        match &mut self {
            Self::Fakespot { score, .. } => {
                *score += 0.01;
            }
            _ => panic!("Not Suggestion::Fakespot"),
        }
        self
    }

    pub fn with_fakespot_product_type_bonus(mut self, bonus: f64) -> Self {
        match &mut self {
            Self::Fakespot { score, .. } => {
                *score += 0.001 * bonus;
            }
            _ => panic!("Not Suggestion::Fakespot"),
        }
        self
    }
}

impl Eq for Suggestion {}
/// Replaces all template parameters in a "raw" sponsored suggestion URL,
/// producing a "cooked" URL with real values.
pub(crate) fn cook_raw_suggestion_url(raw_url: &str) -> String {
    let timestamp = Local::now().format("%Y%m%d%H").to_string();
    debug_assert!(timestamp.len() == TIMESTAMP_LENGTH);
    // "Raw" sponsored suggestion URLs must not contain more than one timestamp
    // template parameter, so we replace just the first occurrence.
    raw_url.replacen(TIMESTAMP_TEMPLATE, &timestamp, 1)
}

/// Determines whether a "raw" sponsored suggestion URL is equivalent to a
/// "cooked" URL. The two URLs are equivalent if they are identical except for
/// their replaced template parameters, which can be different.
#[uniffi::export]
pub fn raw_suggestion_url_matches(raw_url: &str, cooked_url: &str) -> bool {
    let Some((raw_url_prefix, raw_url_suffix)) = raw_url.split_once(TIMESTAMP_TEMPLATE) else {
        return raw_url == cooked_url;
    };
    let (Some(cooked_url_prefix), Some(cooked_url_suffix)) = (
        cooked_url.get(..raw_url_prefix.len()),
        cooked_url.get(raw_url_prefix.len() + TIMESTAMP_LENGTH..),
    ) else {
        return false;
    };
    if raw_url_prefix != cooked_url_prefix || raw_url_suffix != cooked_url_suffix {
        return false;
    }
    let maybe_timestamp =
        &cooked_url[raw_url_prefix.len()..raw_url_prefix.len() + TIMESTAMP_LENGTH];
    maybe_timestamp.bytes().all(|b| b.is_ascii_digit())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn cook_url_with_template_parameters() {
        let raw_url_with_one_timestamp = "https://example.com?a=%YYYYMMDDHH%";
        let cooked_url_with_one_timestamp = cook_raw_suggestion_url(raw_url_with_one_timestamp);
        assert_eq!(
            cooked_url_with_one_timestamp.len(),
            raw_url_with_one_timestamp.len() - 2
        );
        assert_ne!(raw_url_with_one_timestamp, cooked_url_with_one_timestamp);

        let raw_url_with_trailing_segment = "https://example.com?a=%YYYYMMDDHH%&b=c";
        let cooked_url_with_trailing_segment =
            cook_raw_suggestion_url(raw_url_with_trailing_segment);
        assert_eq!(
            cooked_url_with_trailing_segment.len(),
            raw_url_with_trailing_segment.len() - 2
        );
        assert_ne!(
            raw_url_with_trailing_segment,
            cooked_url_with_trailing_segment
        );
    }

    #[test]
    fn cook_url_without_template_parameters() {
        let raw_url_without_timestamp = "https://example.com?b=c";
        let cooked_url_without_timestamp = cook_raw_suggestion_url(raw_url_without_timestamp);
        assert_eq!(raw_url_without_timestamp, cooked_url_without_timestamp);
    }

    #[test]
    fn url_with_template_parameters_matches() {
        let raw_url_with_one_timestamp = "https://example.com?a=%YYYYMMDDHH%";
        let raw_url_with_trailing_segment = "https://example.com?a=%YYYYMMDDHH%&b=c";

        // Equivalent, except for their replaced template parameters.
        assert!(raw_suggestion_url_matches(
            raw_url_with_one_timestamp,
            "https://example.com?a=0000000000"
        ));
        assert!(raw_suggestion_url_matches(
            raw_url_with_trailing_segment,
            "https://example.com?a=1111111111&b=c"
        ));

        // Different lengths.
        assert!(!raw_suggestion_url_matches(
            raw_url_with_one_timestamp,
            "https://example.com?a=1234567890&c=d"
        ));
        assert!(!raw_suggestion_url_matches(
            raw_url_with_one_timestamp,
            "https://example.com?a=123456789"
        ));
        assert!(!raw_suggestion_url_matches(
            raw_url_with_trailing_segment,
            "https://example.com?a=0987654321"
        ));
        assert!(!raw_suggestion_url_matches(
            raw_url_with_trailing_segment,
            "https://example.com?a=0987654321&b=c&d=e"
        ));

        // Different query parameter names.
        assert!(!raw_suggestion_url_matches(
            raw_url_with_one_timestamp,         // `a`.
            "https://example.com?b=4444444444"  // `b`.
        ));
        assert!(!raw_suggestion_url_matches(
            raw_url_with_trailing_segment,          // `a&b`.
            "https://example.com?a=5555555555&c=c"  // `a&c`.
        ));

        // Not a timestamp.
        assert!(!raw_suggestion_url_matches(
            raw_url_with_one_timestamp,
            "https://example.com?a=bcdefghijk"
        ));
        assert!(!raw_suggestion_url_matches(
            raw_url_with_trailing_segment,
            "https://example.com?a=bcdefghijk&b=c"
        ));
    }

    #[test]
    fn url_without_template_parameters_matches() {
        let raw_url_without_timestamp = "https://example.com?b=c";

        assert!(raw_suggestion_url_matches(
            raw_url_without_timestamp,
            "https://example.com?b=c"
        ));
        assert!(!raw_suggestion_url_matches(
            raw_url_without_timestamp,
            "http://example.com"
        ));
        assert!(!raw_suggestion_url_matches(
            raw_url_without_timestamp, // `a`.
            "http://example.com?a=c"   // `b`.
        ));
        assert!(!raw_suggestion_url_matches(
            raw_url_without_timestamp,
            "https://example.com?b=c&d=e"
        ));
    }
}
