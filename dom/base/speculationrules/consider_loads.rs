/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

use std::collections::{BTreeSet, HashMap};

use selectors::context::{
    MatchingContext, MatchingForInvalidation, MatchingMode, NeedsSelectorFlags, SelectorCaches,
};
use selectors::matching::matches_selector_list;
use selectors::Element as _;
use style::dom::{TDocument, TElement, TNode};
use style::gecko::wrapper::GeckoElement;
use style::gecko_bindings::structs::Element;
use thin_vec::ThinVec;
use url::Url;
use urlpattern::UrlPatternMatchInput;

use crate::{
    LinkElement, Predicate, PrefetchCandidate, PrefetchCandidates, ReferrerPolicy, SpeculationRule,
    SpeculationRuleSet,
};

impl Predicate {
    // https://html.spec.whatwg.org/#dr-predicate-matches
    pub fn matches(&self, element: &Element, selector_caches: &mut SelectorCaches) -> bool {
        match self {
            Self::Conjunction(clauses) => clauses
                .iter()
                .all(|clause| clause.matches(element, selector_caches)),
            Self::Disjunction(clauses) => clauses
                .iter()
                .any(|clause| clause.matches(element, selector_caches)),
            Self::Negation(clause) => !clause.matches(element, selector_caches),
            Self::UrlPattern(patterns) => patterns.iter().any(|pattern| {
                pattern
                    .test(UrlPatternMatchInput::Url(match element.href_url() {
                        Some(url) => url,
                        None => return false,
                    }))
                    .unwrap_or(false)
            }),
            Self::Selector(selectors) => {
                let element = GeckoElement(element);
                let mut context = MatchingContext::new(
                    MatchingMode::Normal,
                    None,
                    selector_caches,
                    element.as_node().owner_doc().quirks_mode(),
                    NeedsSelectorFlags::No,
                    MatchingForInvalidation::No,
                );
                // The scoping root is the element's root, so we leave the scope_element field unset.
                context.current_host = element.containing_shadow_host().map(|e| e.opaque());
                matches_selector_list(selectors, element, &mut context)
            }
        }
    }
}

impl SpeculationRule {
    pub fn consider_speculative_loads(
        &self,
        candidates: &mut ThinVec<PrefetchCandidate>,
        elements: &[&Element],
        selector_caches: &mut SelectorCaches,
    ) {
        // https://html.spec.whatwg.org/#inner-consider-speculative-loads-steps
        // Step 3.1.1-3.1.2.
        // We do not currently implement cross-origin prefetch, and so do not implement
        // cross-origin prefetch IP anonymization.
        let anonymization_policy = None;

        // Step 3.1.4.
        if let Some(predicate) = &self.predicate {
            candidates.extend(elements.iter().filter_map(|&element| {
                if !predicate.matches(&element, selector_caches) {
                    return None;
                }
                Some(PrefetchCandidate {
                    no_vary_search_hint: self.no_vary_search_hint.clone(),
                    eagerness: self.eagerness,
                    referrer_policy: if self.referrer_policy == ReferrerPolicy::Empty {
                        element.referrer_policy()
                    } else {
                        self.referrer_policy
                    },
                    tags: self.tags.iter().cloned().collect(),
                    anonymization_policy: anonymization_policy.clone(),
                    url: element.href_url()?,
                })
            }));
        } else {
            // Step 3.1.3.
            candidates.extend(self.urls.iter().map(|url| PrefetchCandidate {
                url: url.clone(),
                no_vary_search_hint: self.no_vary_search_hint.clone(),
                eagerness: self.eagerness,
                // Computing a speculative load referrer policy given rule and a null link
                // is equivalent to just using the rule's referrer policy.
                referrer_policy: self.referrer_policy,
                tags: self.tags.iter().cloned().collect(),
                anonymization_policy: anonymization_policy.clone(),
            }));
        }
    }
}

impl SpeculationRuleSet {
    pub fn consider_speculative_loads(
        &self,
        candidates: &mut ThinVec<PrefetchCandidate>,
        elements: &[&Element],
    ) {
        // https://html.spec.whatwg.org/#inner-consider-speculative-loads-steps
        // Step 3.1.
        let mut selector_caches = SelectorCaches::default();
        self.0.iter().for_each(|rule| {
            rule.consider_speculative_loads(candidates, elements, &mut selector_caches)
        });
    }
}

impl PrefetchCandidates {
    pub fn group(&mut self) {
        // https://html.spec.whatwg.org/#inner-consider-speculative-loads-steps
        // This corresponds to Steps 5 and 6, however, the groups are ultimately stored
        // in place of the original candidates.

        // The spec forms one group per candidate, containing that candidate plus every other
        // redundant candidate that is at least as eager as it. Two candidates with the same url
        // and the same eagerness therefore produce identical groups, so we only need to keep one
        // representative per (url, eagerness) pair.
        // Rather than maintaining a list of candidates in each group, we only store the single
        // representative candidate, and append the tags from each other candidate onto it.
        // Also, as we do not currently implement No-Vary-Search hints for speculation rules,
        // we can consider candidates redundant if their urls are equal.
        let mut buckets: HashMap<Url, Vec<PrefetchCandidate>> = HashMap::new();

        for candidate in std::mem::take(&mut self.0) {
            buckets
                .entry(candidate.url.clone())
                .or_default()
                .push(candidate);
        }

        let mut groups = ThinVec::new();
        for mut bucket in buckets.into_values() {
            // Walking from the most eager candidate to the least eager one lets us accumulate the
            // tags of every at-least-as-eager candidate as we go.
            bucket.sort_by_key(|c| std::cmp::Reverse(c.eagerness));

            // Step 7.1.2, which calls #collect-tags-from-speculative-load-candidates, collects the
            // tags from all the candidates in the group, sorted in ascending order, with nulls at
            // the front. Since we only store the representative candidate in the group, we instead
            // collect the tags as we go, storing them in a b-tree set, which keeps them in sorted
            // order.
            let mut tags = BTreeSet::new();
            let mut representative: Option<PrefetchCandidate> = None;
            for mut candidate in bucket {
                // The tags of a strictly less eager candidate don't belong to the group being
                // accumulated, so that group has to be emitted before they are folded in.
                if representative
                    .as_ref()
                    .is_some_and(|rep| rep.eagerness != candidate.eagerness)
                {
                    let mut rep = representative.take().expect("checked just above");
                    rep.tags = tags.clone();
                    groups.push(rep);
                }
                tags.append(&mut candidate.tags);
                representative.get_or_insert(candidate);
            }
            if let Some(mut rep) = representative {
                rep.tags = tags;
                groups.push(rep);
            }
        }

        // The candidates here end up in a randomized order, unlike the fixed order within the spec.
        // However, as this only determines the order prefetches will be fired in, and not which
        // prefetches will be fired, this should be equivalent.
        self.0 = groups;
    }
}

#[cfg(test)]
mod tests {
    use nsstring::nsACString;

    use super::*;
    use crate::{Eagerness, UrlSearchVariance};

    // The crate declares these bindings for functions and statics implemented in
    // Gecko, which aren't available when it is built on its own for tests.

    // thin-vec's gecko-ffi feature points every empty ThinVec at Gecko's shared
    // empty nsTArray header, which is a zeroed nsTArrayHeader.
    #[allow(non_upper_case_globals)]
    #[unsafe(no_mangle)]
    static sEmptyTArrayHeader: [u32; 2] = [0, 0];

    #[unsafe(no_mangle)]
    extern "C" fn Gecko_Element_GetHrefURI(_element: &Element, _spec: &mut nsACString) -> bool {
        unimplemented!()
    }

    #[unsafe(no_mangle)]
    extern "C" fn Gecko_Element_GetReferrerPolicy(_element: &Element) -> ReferrerPolicy {
        unimplemented!()
    }

    type Group = (String, Eagerness, Vec<Option<String>>);

    fn candidate(url: &str, eagerness: Eagerness, tags: &[Option<&str>]) -> PrefetchCandidate {
        PrefetchCandidate {
            url: Url::parse(url).unwrap(),
            no_vary_search_hint: UrlSearchVariance::Default,
            eagerness,
            referrer_policy: ReferrerPolicy::Empty,
            tags: tags.iter().map(|tag| tag.map(str::to_string)).collect(),
            anonymization_policy: None,
        }
    }

    fn expected(url: &str, eagerness: Eagerness, tags: &[Option<&str>]) -> Group {
        (
            url.to_string(),
            eagerness,
            tags.iter().map(|tag| tag.map(str::to_string)).collect(),
        )
    }

    // Groups the given candidates, sorting the result by url and eagerness, as
    // `group` leaves the groups in an arbitrary order.
    fn group(candidates: Vec<PrefetchCandidate>) -> Vec<Group> {
        let mut candidates = PrefetchCandidates(candidates.into_iter().collect());
        candidates.group();

        let mut groups: Vec<Group> = candidates
            .0
            .iter()
            .map(|c| {
                (
                    c.url.to_string(),
                    c.eagerness,
                    c.tags.iter().cloned().collect(),
                )
            })
            .collect();
        groups.sort_by(|a, b| a.0.cmp(&b.0).then(a.1.cmp(&b.1)));
        groups
    }

    #[test]
    fn no_candidates() {
        assert!(group(vec![]).is_empty());
    }

    #[test]
    fn distinct_urls_are_not_grouped_together() {
        assert_eq!(
            group(vec![
                candidate("https://example.com/a", Eagerness::Eager, &[Some("a")]),
                candidate("https://example.com/b", Eagerness::Eager, &[Some("b")]),
            ]),
            [
                expected("https://example.com/a", Eagerness::Eager, &[Some("a")]),
                expected("https://example.com/b", Eagerness::Eager, &[Some("b")]),
            ]
        );
    }

    #[test]
    fn identical_url_and_eagerness_are_merged() {
        assert_eq!(
            group(vec![
                candidate("https://example.com/a", Eagerness::Immediate, &[Some("a1")]),
                candidate("https://example.com/a", Eagerness::Immediate, &[Some("a2")]),
            ]),
            [expected(
                "https://example.com/a",
                Eagerness::Immediate,
                &[Some("a1"), Some("a2")]
            )]
        );
    }

    #[test]
    fn less_eager_group_collects_tags_of_more_eager_candidates() {
        assert_eq!(
            group(vec![
                candidate("https://example.com/a", Eagerness::Moderate, &[Some("b1")]),
                candidate("https://example.com/a", Eagerness::Immediate, &[Some("a1")]),
                candidate("https://example.com/a", Eagerness::Immediate, &[Some("a2")]),
            ]),
            [
                expected(
                    "https://example.com/a",
                    Eagerness::Moderate,
                    &[Some("a1"), Some("a2"), Some("b1")]
                ),
                expected(
                    "https://example.com/a",
                    Eagerness::Immediate,
                    &[Some("a1"), Some("a2")]
                ),
            ]
        );
    }

    #[test]
    fn tags_accumulate_across_every_eagerness_level() {
        assert_eq!(
            group(vec![
                candidate(
                    "https://example.com/a",
                    Eagerness::Conservative,
                    &[Some("c")]
                ),
                candidate("https://example.com/a", Eagerness::Moderate, &[Some("m")]),
                candidate("https://example.com/a", Eagerness::Eager, &[Some("e")]),
                candidate("https://example.com/a", Eagerness::Immediate, &[Some("i")]),
            ]),
            [
                expected(
                    "https://example.com/a",
                    Eagerness::Conservative,
                    &[Some("c"), Some("e"), Some("i"), Some("m")]
                ),
                expected(
                    "https://example.com/a",
                    Eagerness::Moderate,
                    &[Some("e"), Some("i"), Some("m")]
                ),
                expected(
                    "https://example.com/a",
                    Eagerness::Eager,
                    &[Some("e"), Some("i")]
                ),
                expected("https://example.com/a", Eagerness::Immediate, &[Some("i")]),
            ]
        );
    }

    #[test]
    fn null_tags_are_collected_first() {
        assert_eq!(
            group(vec![
                candidate("https://example.com/b", Eagerness::Eager, &[Some("e1")]),
                candidate("https://example.com/b", Eagerness::Conservative, &[None]),
            ]),
            [
                expected(
                    "https://example.com/b",
                    Eagerness::Conservative,
                    &[None, Some("e1")]
                ),
                expected("https://example.com/b", Eagerness::Eager, &[Some("e1")]),
            ]
        );
    }
}
