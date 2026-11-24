/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! A media query list:
//!
//! https://drafts.csswg.org/mediaqueries/#typedef-media-query-list

use super::{MediaQuery, Qualifier};
use crate::context::QuirksMode;
use crate::derives::*;
use crate::device::Device;
use crate::dom::AttributeTracker;
use crate::error_reporting::ContextualParseError;
use crate::parser::ParserContext;
use crate::queries::boolean_expression::BooleanExprTest;
use crate::stylesheets::CustomMediaEvaluator;
use crate::values::computed;
use cssparser::Token;
use cssparser::{Delimiter, Parser};
use selectors::kleene_value::KleeneValue;
use style_traits::ParsingMode;

/// A type that encapsulates a media query list.
#[derive(Clone, MallocSizeOf, ToCss, ToShmem)]
#[css(comma, derive_debug)]
pub struct MediaList {
    /// The list of media queries.
    #[css(iterable)]
    pub media_queries: Vec<MediaQuery>,
}

impl MediaList {
    /// Parse a media query list from CSS.
    ///
    /// Always returns a media query list. If any invalid media query is
    /// found, the media query list is only filled with the equivalent of
    /// "not all", see:
    ///
    /// <https://drafts.csswg.org/mediaqueries/#error-handling>
    pub fn parse(context: &mut ParserContext, input: &mut Parser) -> Self {
        context.with_parsing_mode(ParsingMode::MEDIA_QUERY_CONDITION, |context| {
            if input.is_exhausted() {
                return Self::empty();
            }

            let mut media_queries = vec![];
            loop {
                let start_position = input.position();
                let start_location = input.current_source_location();
                match input.parse_until_before(Delimiter::Comma, |i| MediaQuery::parse(context, i))
                {
                    Ok(mq) => {
                        media_queries.push(mq);
                    },
                    Err(err) => {
                        media_queries.push(MediaQuery::never_matching());
                        let error = ContextualParseError::InvalidMediaRule(
                            input.slice_from(start_position),
                            err,
                        );
                        context.log_css_error(start_location, error);
                    },
                }

                match input.next() {
                    Ok(&Token::Comma) => {},
                    Ok(_) => unreachable!(),
                    Err(_) => break,
                }
            }

            MediaList { media_queries }
        })
    }

    /// Create an empty MediaList.
    pub fn empty() -> Self {
        MediaList {
            media_queries: vec![],
        }
    }

    /// Evaluate a whole `MediaList` against `Device`.
    pub fn evaluate(
        &self,
        device: &Device,
        quirks_mode: QuirksMode,
        custom: &mut CustomMediaEvaluator,
    ) -> bool {
        computed::Context::for_media_query_evaluation(device, quirks_mode, |context| {
            self.matches(context, custom).to_bool(/* unknown = */ false)
        })
    }

    /// Evaluate the current `MediaList` with a pre-existing context and custom-media evaluator.
    pub fn matches(
        &self,
        context: &computed::Context,
        custom: &mut CustomMediaEvaluator,
    ) -> KleeneValue {
        // Check if it is an empty media query list or any queries match.
        // https://drafts.csswg.org/mediaqueries-4/#mq-list
        if self.media_queries.is_empty() {
            return KleeneValue::True;
        }
        KleeneValue::any(self.media_queries.iter(), |mq| {
            let mut query_match = if mq.media_type.matches(context.device().media_type()) {
                mq.condition.as_ref().map_or(KleeneValue::True, |c| {
                    c.evaluate(context, custom, &mut AttributeTracker::new_dummy())
                })
            } else {
                KleeneValue::False
            };
            // Apply the logical NOT qualifier to the result
            if matches!(mq.qualifier, Some(Qualifier::Not)) {
                query_match = !query_match;
            }
            query_match
        })
    }

    /// Whether this `MediaList` contains no media queries.
    pub fn is_empty(&self) -> bool {
        self.media_queries.is_empty()
    }

    /// Whether this `MediaList` depends on the viewport size.
    pub fn is_viewport_dependent(&self) -> bool {
        self.media_queries.iter().any(|q| q.is_viewport_dependent())
    }

    /// Append a new media query item to the media list.
    /// <https://drafts.csswg.org/cssom/#dom-medialist-appendmedium>
    ///
    /// Returns true if added, false if fail to parse the medium string.
    pub fn append_medium(&mut self, context: &ParserContext, new_medium: &str) -> bool {
        let mut parser = Parser::new(new_medium);
        let new_query = match MediaQuery::parse(context, &mut parser) {
            Ok(query) => query,
            Err(_) => {
                return false;
            },
        };
        // This algorithm doesn't actually matches the current spec,
        // but it matches the behavior of Gecko and Edge.
        // See https://github.com/w3c/csswg-drafts/issues/697
        self.media_queries.retain(|query| query != &new_query);
        self.media_queries.push(new_query);
        true
    }

    /// Delete a media query from the media list.
    /// <https://drafts.csswg.org/cssom/#dom-medialist-deletemedium>
    ///
    /// Returns true if found and deleted, false otherwise.
    pub fn delete_medium(&mut self, context: &ParserContext, old_medium: &str) -> bool {
        let mut parser = Parser::new(old_medium);
        let old_query = match MediaQuery::parse(context, &mut parser) {
            Ok(query) => query,
            Err(_) => {
                return false;
            },
        };
        let old_len = self.media_queries.len();
        self.media_queries.retain(|query| query != &old_query);
        old_len != self.media_queries.len()
    }
}
