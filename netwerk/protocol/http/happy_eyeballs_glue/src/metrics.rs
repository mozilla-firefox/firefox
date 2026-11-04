/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Glean metrics for the Happy Eyeballs algorithm.

use firefox_on_glean::metrics::netwerk as glean;
use std::collections::HashMap;
use std::time::{Duration, Instant};

struct DnsInfo {
    start: Instant,
    record_type: happy_eyeballs::DnsRecordType,
}

struct ConnInfo {
    index: u32,
}

enum Outcome {
    Succeeded { info: ConnInfo, elapsed: Duration },
    Failed { elapsed: Duration },
}

pub(crate) struct Metrics {
    start: Instant,
    first_attempt_dispatched: bool,
    dns_infos: HashMap<happy_eyeballs::Id, DnsInfo>,
    conn_infos: HashMap<happy_eyeballs::Id, ConnInfo>,
    attempt_count: u32,
    cancelled_count: u32,
    // Arrival times of positive DNS answers (A, AAAA or HTTPS).
    first_positive: Option<Instant>,
    last_positive_before_first_attempt: Option<Instant>,
    last_positive: Option<Instant>,
    // The address family the algorithm waits for before moving on without the
    // resolution delay timer, and whether it and the HTTPS record were
    // answered before the first connection attempt.
    preferred: happy_eyeballs::DnsRecordType,
    preferred_answered: bool,
    https_answered: bool,
    // Record type of the last answer before the first connection attempt, if
    // that answer was negative.
    last_negative: Option<happy_eyeballs::DnsRecordType>,
    alt_svc_h3: bool,
    https_record_received: bool,
    https_rr_h3: bool,
    https_rr_ech: bool,
    https_rr_ipv4hint: bool,
    https_rr_ipv6hint: bool,
    // The resolver type (DoH/TRR vs native) that produced the HTTPS record.
    is_trr: bool,
    outcome: Option<Outcome>,
}

impl Metrics {
    pub(crate) fn new(
        alt_svc: &[happy_eyeballs::AltSvc],
        preferred: happy_eyeballs::DnsRecordType,
    ) -> Self {
        let alt_svc_h3 = alt_svc
            .iter()
            .any(|a| matches!(a.http_version, happy_eyeballs::HttpVersion::H3));
        Self {
            start: Instant::now(),
            first_attempt_dispatched: false,
            dns_infos: HashMap::new(),
            conn_infos: HashMap::new(),
            attempt_count: 0,
            cancelled_count: 0,
            first_positive: None,
            last_positive_before_first_attempt: None,
            last_positive: None,
            preferred,
            preferred_answered: false,
            https_answered: false,
            last_negative: None,
            alt_svc_h3,
            https_record_received: false,
            https_rr_h3: false,
            https_rr_ech: false,
            https_rr_ipv4hint: false,
            https_rr_ipv6hint: false,
            is_trr: false,
            outcome: None,
        }
    }

    pub(crate) fn dns_query_started(
        &mut self,
        id: happy_eyeballs::Id,
        record_type: happy_eyeballs::DnsRecordType,
    ) {
        self.dns_infos.insert(
            id,
            DnsInfo {
                start: Instant::now(),
                record_type,
            },
        );
    }

    pub(crate) fn dns_response(&mut self, id: happy_eyeballs::Id, positive: bool, is_trr: bool) {
        // Any record resolved via TRR marks the connection as DoH-resolved. The
        // resolver is consistent across a connection's lookups, so OR-ing is
        // order-independent and robust to failed responses that carry no
        // resolver signal.
        self.is_trr |= is_trr;
        let Some(info) = self.dns_infos.remove(&id) else {
            return;
        };
        let now = Instant::now();
        let elapsed_ms = now.duration_since(info.start).as_millis() as i64;
        let label = dns_record_type_label(info.record_type);
        glean::happy_eyeballs_dns_resolution_time
            .get(label)
            .accumulate_single_sample_signed(elapsed_ms);

        if positive {
            self.first_positive.get_or_insert(now);
            self.last_positive = Some(now);
        }

        // Only answers that arrive before the first connection attempt inform
        // the resolution delay; once racing begins the delay no longer applies,
        // so later answers (including ones from queries a failed attempt
        // triggers) are left out of the until_first_attempt spread.
        if !self.first_attempt_dispatched {
            self.preferred_answered |= info.record_type == self.preferred;
            self.https_answered |= info.record_type == happy_eyeballs::DnsRecordType::Https;
            if positive {
                self.last_positive_before_first_attempt = Some(now);
                self.last_negative = None;
            } else {
                self.last_negative = Some(info.record_type);
            }
        }
    }

    pub(crate) fn dns_response_https(
        &mut self,
        id: happy_eyeballs::Id,
        infos: &[happy_eyeballs::ServiceInfo],
        is_trr: bool,
    ) {
        self.https_record_received |= !infos.is_empty();
        self.https_rr_h3 |= infos.iter().any(|i| {
            i.alpn_http_versions
                .contains(&happy_eyeballs::HttpVersion::H3)
        });
        self.https_rr_ech |= infos.iter().any(|i| {
            i.ech_config
                .as_ref()
                .is_some_and(|e| !e.as_ref().is_empty())
        });
        self.https_rr_ipv4hint |= infos.iter().any(|i| !i.ipv4_hints.is_empty());
        self.https_rr_ipv6hint |= infos.iter().any(|i| !i.ipv6_hints.is_empty());
        self.dns_response(id, !infos.is_empty(), is_trr);
    }

    pub(crate) fn connection_attempt_started(&mut self, id: happy_eyeballs::Id) {
        self.attempt_count += 1;

        if !self.first_attempt_dispatched {
            self.first_attempt_dispatched = true;
            let elapsed_ms = self.start.elapsed().as_millis() as i64;
            glean::happy_eyeballs_time_to_first_attempt.accumulate_single_sample_signed(elapsed_ms);
            self.record_last_answer_to_attempt();
        }

        self.conn_infos.insert(
            id,
            ConnInfo {
                index: self.attempt_count,
            },
        );
    }

    /// Records the time from the last positive DNS answer to the first
    /// connection attempt, labeled by what ended the wait: the resolution delay
    /// timer while the preferred address family and/or HTTPS record were still
    /// pending, or a negative answer for one of them.
    fn record_last_answer_to_attempt(&self) {
        let Some(last) = self.last_positive_before_first_attempt else {
            return;
        };
        let wait_ms = last.elapsed().as_millis() as i64;
        let label = match (
            self.preferred_answered,
            self.https_answered,
            self.last_negative,
        ) {
            _ if wait_ms == 0 => "no_wait",
            (false, false, _) => "both_timeout",
            (false, true, _) => "address_timeout",
            (true, false, _) => "https_timeout",
            (true, true, Some(happy_eyeballs::DnsRecordType::Https)) => "https_negative",
            (
                true,
                true,
                Some(happy_eyeballs::DnsRecordType::A | happy_eyeballs::DnsRecordType::Aaaa),
            ) => "address_negative",
            // The last arm can record values >= 1ms into the no_wait label. This
            // can happen due to processing delay between the last answer
            // arriving and the attempt being started, but is not the algorithm
            // blocking.
            (true, true, None) => "no_wait",
        };
        glean::happy_eyeballs_last_answer_to_attempt
            .get(label)
            .accumulate_single_sample_signed(wait_ms);
    }

    pub(crate) fn connection_cancelled(&mut self, id: happy_eyeballs::Id) {
        if self.conn_infos.remove(&id).is_some() {
            self.cancelled_count += 1;
        }
    }

    pub(crate) fn connection_succeeded(&mut self, id: happy_eyeballs::Id) {
        if let Some(info) = self.conn_infos.remove(&id) {
            self.outcome = Some(Outcome::Succeeded {
                info,
                elapsed: self.start.elapsed(),
            });
        }
    }

    pub(crate) fn failed(&mut self) {
        self.outcome = Some(Outcome::Failed {
            elapsed: self.start.elapsed(),
        });
    }
}

impl Drop for Metrics {
    fn drop(&mut self) {
        let Some(ref outcome) = self.outcome else {
            return;
        };

        let outcome_label = match outcome {
            Outcome::Succeeded { .. } => "succeeded",
            Outcome::Failed { .. } => "failed",
        };

        let elapsed = match outcome {
            Outcome::Succeeded { elapsed, .. } | Outcome::Failed { elapsed } => *elapsed,
        };
        if let (Some(first), Some(last)) =
            (self.first_positive, self.last_positive_before_first_attempt)
        {
            let spread = last.duration_since(first);
            glean::happy_eyeballs_dns_answer_spread
                .accumulate_single_sample_signed(spread.as_millis() as i64);
            glean::happy_eyeballs_dns_answer_spread_v2
                .get("until_first_attempt")
                .accumulate_raw_duration(spread);
        }
        if let (Some(first), Some(last)) = (self.first_positive, self.last_positive) {
            glean::happy_eyeballs_dns_answer_spread_v2
                .get("until_end")
                .accumulate_raw_duration(last.duration_since(first));
        }

        let elapsed_ms = elapsed.as_millis() as i64;
        match outcome {
            Outcome::Succeeded { .. } => glean::happy_eyeballs_end_to_end_time_succeeded
                .accumulate_single_sample_signed(elapsed_ms),
            Outcome::Failed { .. } => glean::happy_eyeballs_end_to_end_time_failed
                .accumulate_single_sample_signed(elapsed_ms),
        }

        glean::happy_eyeballs_connection_attempt_count
            .get(outcome_label)
            .accumulate_single_sample_signed(self.attempt_count.into());

        glean::happy_eyeballs_cancelled_attempt_count
            .accumulate_single_sample_signed(self.cancelled_count.into());

        if let Outcome::Succeeded { info, .. } = outcome {
            glean::happy_eyeballs_winning_attempt_index
                .accumulate_single_sample_signed(info.index.into());
        }

        let h3_discovery_label = match (self.alt_svc_h3, self.https_rr_h3) {
            (false, false) => "none",
            (true, false) => "altsvc_only",
            (false, true) => "https_rr_only",
            (true, true) => "both",
        };
        let resolver = if self.is_trr { "doh" } else { "native" };
        glean::happy_eyeballs_h3_discovery_by_resolver
            .get(resolver, h3_discovery_label)
            .add(1);

        if self.https_record_received {
            let resolver = if self.is_trr { "doh" } else { "native" };
            let metric = &glean::happy_eyeballs_https_rr_features_by_resolver;
            metric.get(resolver, "total").add(1);
            if self.https_rr_h3 {
                metric.get(resolver, "h3_alpn").add(1);
            }
            if self.https_rr_ech {
                metric.get(resolver, "ech").add(1);
            }
            if self.https_rr_ipv4hint {
                metric.get(resolver, "ipv4hint").add(1);
            }
            if self.https_rr_ipv6hint {
                metric.get(resolver, "ipv6hint").add(1);
            }
        }
    }
}

fn dns_record_type_label(rt: happy_eyeballs::DnsRecordType) -> &'static str {
    match rt {
        happy_eyeballs::DnsRecordType::A => "a",
        happy_eyeballs::DnsRecordType::Aaaa => "aaaa",
        happy_eyeballs::DnsRecordType::Https => "https",
    }
}
