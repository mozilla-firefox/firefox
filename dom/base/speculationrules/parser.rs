/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

use log::info;
use selectors::SelectorList;
use serde_json::{Map, Value};
use style::gecko_bindings::structs::URLExtraData_sDummy;
use style::selector_parser::SelectorParser;
use style::stylesheets::UrlExtraData;
use thin_vec::{thin_vec, ThinVec};
use url::Url;
use urlpattern::{UrlPattern, UrlPatternInit};

use crate::{
    Eagerness, Predicate, ReferrerPolicy, Source, SpeculationRule, SpeculationRuleParseError,
    SpeculationRuleSet, UrlSearchVariance,
};

fn log_to_console(msg: &str) {
    // TODO(avandolder): Log to developer console as well.
    info!("speculation rules: {}", msg);
}

fn parse_url_pattern(value: Value, base_url: &Url) -> Option<urlpattern::UrlPattern> {
    let init = match value {
        Value::String(s) => UrlPatternInit::parse_constructor_string::<regex::Regex>(
            s.as_str(),
            Some(base_url.clone()),
        )
        .ok()?,
        Value::Object(mut o) => {
            for (key, value) in o.iter() {
                match key.as_str() {
                    "protocol" | "username" | "password" | "hostname" | "port" | "pathname"
                    | "search" | "hash" | "baseURL" => {
                        if !value.is_string() {
                            return None;
                        }
                    }
                    _ => return None,
                }
            }

            let mut get_string_value = move |key| {
                o.get_mut(key).and_then(|v| match v.take() {
                    Value::String(s) => Some(s),
                    _ => None,
                })
            };

            let base_url = if let Some(s) = get_string_value("baseURL") {
                Url::parse(s.as_str()).ok()?
            } else {
                base_url.clone()
            };

            UrlPatternInit {
                protocol: get_string_value("protocol"),
                username: get_string_value("username"),
                password: get_string_value("password"),
                hostname: get_string_value("hostname"),
                port: get_string_value("port"),
                pathname: get_string_value("pathname"),
                search: get_string_value("search"),
                hash: get_string_value("hash"),
                base_url: Some(base_url),
            }
        }
        _ => return None,
    };

    UrlPattern::parse(init, Default::default()).ok()
}

enum PredicateKey {
    And,
    Or,
    Not,
    HrefMatches,
    SelectorMatches,
}

impl TryFrom<&str> for PredicateKey {
    type Error = ();
    fn try_from(value: &str) -> Result<Self, Self::Error> {
        use PredicateKey::*;
        match value {
            "and" => Ok(And),
            "or" => Ok(Or),
            "not" => Ok(Not),
            "href_matches" => Ok(HrefMatches),
            "selector_matches" => Ok(SelectorMatches),
            _ => Err(()),
        }
    }
}

impl Predicate {
    // https://html.spec.whatwg.org/#parse-a-document-rule-predicate
    fn parse(input: Value, document_base_url: &Url, base_url: &Url) -> Option<Self> {
        // Step 1.
        let mut m = match input {
            Value::Object(value) => value,
            _ => {
                log_to_console("Document rule predicate was invalid");
                return None;
            }
        };

        // Step 2.
        let mut predicate_types = m.iter_mut().filter_map(|(key, value)| {
            PredicateKey::try_from(key.as_str())
                .map(|key| (key, value.take()))
                .ok()
        });
        let predicate_type = predicate_types.next();
        if predicate_type.is_none() || predicate_types.next().is_some() {
            log_to_console("Document rule predicate was empty or ambiguous");
            return None;
        }

        match predicate_type.unwrap() {
            // Step 4.
            (key @ (PredicateKey::And | PredicateKey::Or), value) => {
                // Step 4.1.
                if m.len() > 1 {
                    log_to_console("Document rule predicate had unexpected extra options");
                    return None;
                }
                // Steps 4.2-4.4.
                let clauses = match value {
                    Value::Array(mut raw_clauses) => raw_clauses
                        .iter_mut()
                        .map(|raw_clause| {
                            Self::parse(raw_clause.take(), document_base_url, base_url)
                        })
                        .collect::<Option<ThinVec<_>>>()?,
                    _ => {
                        log_to_console("Document rule predicate had an invalid clause list");
                        return None;
                    }
                };
                // Steps 4.5-4.6.
                if let PredicateKey::And = key {
                    Some(Predicate::Conjunction(clauses))
                } else {
                    Some(Predicate::Disjunction(clauses))
                }
            }
            // Step 5.
            (PredicateKey::Not, value) => {
                if m.len() > 1 {
                    log_to_console("Document rule predicate had unexpected extra options");
                    return None;
                }
                let clause = Self::parse(value, document_base_url, base_url)?;
                Some(Predicate::Negation(Box::new(clause)))
            }
            // Step 6.
            (PredicateKey::HrefMatches, value) => {
                // Step 6.1.
                if m.keys()
                    .any(|key| *key != "href_matches" && *key != "relative_to")
                {
                    log_to_console("Document rule predicate had unexpected extra options");
                    return None;
                }
                // Step 6.2.
                let base_url = match m.get("relative_to") {
                    None => base_url,
                    Some(Value::String(s)) if s == "ruleset" => base_url,
                    Some(Value::String(s)) if s == "document" => document_base_url,
                    _ => {
                        log_to_console("Supplied relative-to value was invalid");
                        return None;
                    }
                };
                // Steps 6.3-6.7.
                let patterns = match value {
                    Value::Array(mut array) => {
                        let Some(patterns) = array
                            .iter_mut()
                            .map(|raw_pattern| parse_url_pattern(raw_pattern.take(), base_url))
                            .collect::<Option<ThinVec<_>>>()
                        else {
                            log_to_console("Supplied URL pattern was invalid");
                            return None;
                        };
                        patterns
                    }
                    _ => {
                        if let Some(pattern) = parse_url_pattern(value, base_url) {
                            thin_vec![pattern]
                        } else {
                            log_to_console("Supplied URL pattern was invalid");
                            return None;
                        }
                    }
                };
                Some(Predicate::UrlPattern(patterns))
            }
            // Step 7.
            (PredicateKey::SelectorMatches, raw_selectors) => {
                // Step 7.1.
                if m.len() > 1 {
                    log_to_console("Document rule predicate had unexpected extra options");
                    return None;
                }
                // Steps 7.2-7.3.
                let raw_selectors = match raw_selectors {
                    Value::Array(value) => value,
                    value @ Value::String(_) => vec![value],
                    _ => {
                        log_to_console("Supplied selector list was invalid.");
                        return None;
                    }
                };
                // Steps 7.4-7.6.
                let url_data = unsafe {
                    let dummy = &(*&raw const URLExtraData_sDummy).mRawPtr;
                    debug_assert!(!dummy.is_null());
                    UrlExtraData::from_ptr_ref(dummy)
                };
                let mut selectors = Vec::new();
                for raw_selector in raw_selectors {
                    let Value::String(s) = raw_selector else {
                        log_to_console("Supplied selector must be a string");
                        return None;
                    };
                    let Ok(list) = SelectorParser::parse_author_origin_no_namespace(&s, url_data)
                    else {
                        log_to_console(&format!("Supplied selector '{}' was invalid", s));
                        return None;
                    };
                    selectors.extend_from_slice(list.slice());
                }
                Some(Predicate::Selector(SelectorList::from_iter(
                    selectors.into_iter(),
                )))
            }
        }
    }
}

impl SpeculationRule {
    // https://html.spec.whatwg.org/#parse-a-speculation-rule
    fn parse(
        m: &mut Map<String, Value>,
        ruleset_level_tag: &Option<String>,
        document_base_url: &Url,
        base_url: &Url,
    ) -> Option<Self> {
        // Step 2.
        for key in m.keys() {
            match key.as_str() {
                "source"
                | "urls"
                | "where"
                | "relative_to"
                | "eagerness"
                | "referrer_policy"
                | "tag"
                | "requires"
                | "expects_no_vary_search"
                | "target_hint" => {}
                _ => {
                    log_to_console("Speculation rule has unrecognized keys");
                    return None;
                }
            }
        }

        // Steps 3-7.
        let source = match m.get("source") {
            Some(value) => match value.as_str() {
                Some("document") => Source::Document,
                Some("list") => Source::List,
                _ => {
                    log_to_console("Invalid source was specified");
                    return None;
                }
            },
            _ if m.contains_key("urls") && !m.contains_key("where") => Source::List,
            _ if m.contains_key("where") && !m.contains_key("urls") => Source::Document,
            _ => {
                log_to_console("Source could not be inferred");
                return None;
            }
        };

        // Steps 8-11.
        let (urls, predicate) = match source {
            Source::List => {
                // Step 10.1.
                if m.contains_key("where") {
                    log_to_console("Conflicting sources for rule");
                    return None;
                }
                // Step 10.2.
                let base_url = match m.get("relative_to") {
                    None => base_url,
                    Some(Value::String(s)) if s == "ruleset" => base_url,
                    Some(Value::String(s)) if s == "document" => document_base_url,
                    _ => {
                        log_to_console("Supplied relative-to value was invalid");
                        return None;
                    }
                };
                // Step 10.3-10.4.
                //
                match m.get("urls") {
                    Some(Value::Array(url_list)) => (
                        url_list
                            .iter()
                            .filter_map(|url| {
                                // Step 10.4.1.
                                if !url.is_string() {
                                    log_to_console("Supplied URL must be a string");
                                    return Some(None);
                                }
                                match base_url.join(url.as_str().unwrap()) {
                                    Err(_) => {
                                        log_to_console(&format!(
                                            "Supplied URL string '{}' was unparseable with base url '{}'",
                                            url.as_str().unwrap(),
                                            base_url
                                        ));
                                        None
                                    }
                                    Ok(parsed_url)
                                        if parsed_url.scheme() != "http"
                                            && parsed_url.scheme() != "https" =>
                                    {
                                        log_to_console(&format!(
                                            "Supplied URL string '{}' had invalid scheme",
                                            parsed_url
                                        ));
                                        None
                                    }
                                    Ok(parsed_url) => Some(Some(parsed_url)),
                                }
                            })
                            .collect::<Option<ThinVec<_>>>()?,
                        None,
                    ),
                    _ => {
                        log_to_console("Supplied URL list was invalid");
                        return None;
                    }
                }
            }
            Source::Document => {
                // Step 11.1.
                if m.contains_key("urls") || m.contains_key("relative_to") {
                    log_to_console("Conflicting sources for rule");
                    return None;
                }
                // Steps 11.2-11.4.
                (
                    thin_vec![],
                    Some(if let Some(value) = m.get_mut("where") {
                        Predicate::parse(value.take(), document_base_url, base_url)?
                    } else {
                        Predicate::Conjunction(thin_vec![])
                    }),
                )
            }
        };

        // Steps 12-13.
        let eagerness = m
            .get_mut("eagerness")
            .map(|eagerness| serde_json::from_value(eagerness.take()))
            .unwrap_or_else(|| match source {
                Source::List => Ok(Eagerness::Immediate),
                Source::Document => Ok(Eagerness::Conservative),
            })
            .inspect_err(|_| log_to_console("Eagerness was invalid"))
            .ok()?;
        // Steps 14-15.
        let referrer_policy = m
            .get_mut("referrer_policy")
            .map(|referrer_policy| serde_json::from_value(referrer_policy.take()))
            .unwrap_or(Ok(ReferrerPolicy::Empty))
            .inspect_err(|_| log_to_console("Referrer policy was invalid"))
            .ok()?;

        // Step 16
        let mut tags = thin_vec![];
        // Step 17
        if ruleset_level_tag.is_some() {
            tags.push(ruleset_level_tag.clone());
        }
        // Step 18
        match parse_tag(m) {
            Ok(t)
                if m.contains_key("tag")
                    && (ruleset_level_tag.is_none() || t != *ruleset_level_tag) =>
            {
                tags.push(t)
            }
            Ok(_) => {}
            Err(_) => {
                log_to_console("Speculation rule tag was invalid");
                return None;
            }
        }
        // Step 19
        if tags.is_empty() {
            tags.push(None);
        }
        // Step 20
        debug_assert!(matches!(tags.len(), 1 | 2));

        // Steps 21-22.
        let requirements = m
            .get_mut("requires")
            .map(|requirements| serde_json::from_value(requirements.take()))
            .unwrap_or(Ok(thin_vec![]))
            .inspect_err(|_| log_to_console("Requirements were not understood"))
            .ok()?;
        // Steps 23-24.
        let no_vary_search_hint = match m.get_mut("expects_no_vary_search").map(Value::take) {
            Some(Value::String(s)) => UrlSearchVariance::String(s), // TODO(avandolder): parse the URL search variance in s
            Some(_) => {
                log_to_console("No-Vary-Search hint was invalid");
                return None;
            }
            None => UrlSearchVariance::Default,
        };
        // Step 25.
        Some(SpeculationRule {
            urls,
            predicate,
            source,
            eagerness,
            referrer_policy,
            tags,
            requirements,
            no_vary_search_hint,
        })
    }
}

fn parse_rule_list(
    m: &mut Map<String, Value>,
    t: &str,
    tag: Option<String>,
    document_base_url: &Url,
    base_url: &Url,
) -> ThinVec<SpeculationRule> {
    match m.get_mut(t) {
        Some(Value::Array(list)) => list
            .iter_mut()
            .filter_map(move |v| match v.take() {
                Value::Object(mut m) => {
                    SpeculationRule::parse(&mut m, &tag, document_base_url, base_url)
                }
                _ => {
                    log_to_console("A speculation rule must be a JSON object.");
                    None
                }
            })
            .collect(),
        Some(_) => {
            log_to_console("A speculation rule list must be a JSON array.");
            thin_vec![]
        }
        _ => {
            log_to_console(&format!("No speculation rules for {}", t));
            thin_vec![]
        }
    }
}

// https://html.spec.whatwg.org/#speculation-rule-tag
fn parse_tag(m: &mut Map<String, Value>) -> Result<Option<String>, SpeculationRuleParseError> {
    match m.get_mut("tag").map(Value::take) {
        Some(Value::String(s)) if s.as_bytes().iter().all(|b| (0x20..=0x7E).contains(b)) => {
            Ok(Some(s))
        }
        Some(Value::Null) => Ok(None),
        Some(_) => Err(SpeculationRuleParseError::InvalidTag),
        None => Ok(None),
    }
}

impl SpeculationRuleSet {
    // https://html.spec.whatwg.org/#parse-a-speculation-rule-set-string
    pub fn parse(
        source: &str,
        document_base_url: &Url,
        base_url: &Url,
    ) -> Result<Self, SpeculationRuleParseError> {
        match serde_json::from_str(source) {
            Ok(Value::Object(mut m)) => {
                let tag = parse_tag(&mut m)?;
                Ok(SpeculationRuleSet(parse_rule_list(
                    &mut m,
                    "prefetch",
                    tag,
                    document_base_url,
                    base_url,
                )))
            }
            _ => Err(SpeculationRuleParseError::TopLevelValueMustBeJsonObject),
        }
    }
}
