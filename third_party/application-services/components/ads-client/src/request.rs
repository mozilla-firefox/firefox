/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use parking_lot::{Condvar, Mutex};
use url::Url;

use crate::{
    ads::PlacementId,
    mars::{ad_request::AdPlacementRequest, ReportReason},
    worker::DispatchRequest,
};
use std::{
    collections::{HashMap, HashSet, VecDeque},
    sync::Arc,
    time::Duration,
};

pub const MAXIMUM_ADS_BATCH_COUNT: usize = 100;

// Structure for storing and queuing and storing `DispatchRequest`s sent to background worker.
// AdPlacementRequest will be merged together, allowing fewer network requests to be sent.
#[derive(Clone)]
pub struct RequestQueue {
    // If the `inner` mutex is locked to add data to the queue, the ready_cond must be notified after data is added.
    inner: Arc<Mutex<RequestQueueInner>>,
    // Condvar is updated if new data is queued to be retrievable by `next()`. Can be waited with: `wait_available`
    ready_cond: Arc<Condvar>,
}

struct RequestQueueInner {
    queued_ads: HashMap<PlacementId, AdPlacementRequest>,

    // The ordered queue of non-AdPlacementRequest commands to iterate through.
    request_queue: VecDeque<QueuedRequest>,
}

impl Default for RequestQueue {
    fn default() -> Self {
        Self::new()
    }
}

impl RequestQueue {
    pub fn new() -> RequestQueue {
        let inner = RequestQueueInner {
            queued_ads: HashMap::new(),
            request_queue: VecDeque::new(),
        };
        RequestQueue {
            inner: Arc::new(Mutex::new(inner)),
            ready_cond: Arc::new(Condvar::new()),
        }
    }

    pub fn clear(&self) {
        // Get lock and clear data. No need to update 'ready' condvar, as this can't result in more data.
        let mut inner = self.inner.lock();
        inner.request_queue = VecDeque::new();
        inner.queued_ads = HashMap::new();
    }

    // Wait for queue to have data retrievable by `next()`, with an optional timeout.
    // Uses a condvar internally to ensure a thread can wait with minimal CPU usage.
    // If multiple threads call `wait_available`, all will be notified on availability (so, competing threads may not all receive data from `next()`)
    // Returns `true` if data is available to be queried by `next()`, and false otherwise (eg: if timeout is reached)
    pub fn wait_available(&self, timeout: Option<Duration>) -> bool {
        let mut waiting = self.inner.lock();

        // Return immediately if data is currently available
        if waiting.next_available() {
            return true;
        }

        if let Some(timeout) = timeout {
            if self.ready_cond.wait_for(&mut waiting, timeout).timed_out() {
                return false;
            }
        } else {
            self.ready_cond.wait(&mut waiting);
        }
        waiting.next_available()
    }

    // Pops the next available DispatchRequest to be run by the worker.
    // This will prioritize any ad_requests, batched, followed by any queued QueuedRequests (eg: RecordClick, etc)
    pub fn next(&self) -> Option<DispatchRequest> {
        let mut inner = self.inner.lock();

        // First, if any ad requests are queued, batch the first `MAXIMUM_ADS_BATCH_COUNT` and resolve those.
        let num_ads = inner.queued_ads.len().min(MAXIMUM_ADS_BATCH_COUNT);
        let mut ad_requests: Vec<_> = inner.queued_ads.drain().collect();
        if !ad_requests.is_empty() {
            let requeue_requests = ad_requests.split_off(num_ads);
            inner.queued_ads = requeue_requests.into_iter().collect();
            return Some(DispatchRequest::RequestAds {
                ad_requests: ad_requests.into_iter().map(|(_, v)| v).collect(),
            });
        }

        // Otherwise, pop the next queue-able command.
        inner.request_queue.pop_front().map(Into::into)
    }

    // Add a `DispatchRequest` to the `RequestQueue` (primary entry point for queuing requests).
    // If new helper functions are added, they must include a convdvar notification.
    pub fn push_request(&self, request: DispatchRequest) {
        // Split DispatchRequest into either a queue-able request, or a batchable ad request, and queue it.
        match request {
            DispatchRequest::RequestAds { ad_requests } => self.push_ad_requests(ad_requests),
            DispatchRequest::RecordImpression { url } => {
                self.push_queued_request(QueuedRequest::RecordImpression { url });
            }
            DispatchRequest::RecordClick { url } => {
                self.push_queued_request(QueuedRequest::RecordClick { url });
            }
            DispatchRequest::ReportAd { url, reason } => {
                self.push_queued_request(QueuedRequest::ReportAd { url, reason });
            }
        }
    }

    fn push_ad_requests(&self, ad_requests: HashSet<AdPlacementRequest>) {
        if ad_requests.is_empty() {
            return;
        }

        let mut inner = self.inner.lock();
        for ad_request in ad_requests {
            inner
                .queued_ads
                .insert(ad_request.placement.clone().into(), ad_request);
        }

        // Queue was written to, so we notify any threads with `wait_available`.
        self.ready_cond.notify_all();
    }

    fn push_queued_request(&self, queued_command: QueuedRequest) {
        let mut inner = self.inner.lock();
        inner.request_queue.push_back(queued_command);

        // Queue was written to, so we notify any threads with `wait_available`.
        self.ready_cond.notify_all();
    }
}

impl RequestQueueInner {
    pub(crate) fn next_available(&self) -> bool {
        !self.queued_ads.is_empty() || !self.request_queue.is_empty()
    }
}

// Queue-able command (ReportAd, etc.)
#[derive(Clone, Debug, PartialEq)]
enum QueuedRequest {
    RecordClick { url: Url },
    RecordImpression { url: Url },
    ReportAd { reason: ReportReason, url: Url },
}

impl From<QueuedRequest> for DispatchRequest {
    fn from(value: QueuedRequest) -> Self {
        match value {
            QueuedRequest::ReportAd { reason, url } => DispatchRequest::ReportAd { reason, url },
            QueuedRequest::RecordClick { url } => DispatchRequest::RecordClick { url },
            QueuedRequest::RecordImpression { url } => DispatchRequest::RecordImpression { url },
        }
    }
}

#[cfg(test)]
mod tests {
    use std::{collections::HashSet, time::Duration};
    use url::Url;

    use crate::request::{
        AdPlacementRequest, DispatchRequest, RequestQueue, MAXIMUM_ADS_BATCH_COUNT,
    };

    fn extract_request_ads(req: &mut DispatchRequest) -> Option<&mut HashSet<AdPlacementRequest>> {
        #[allow(irrefutable_let_patterns)]
        if let DispatchRequest::RequestAds {
            ref mut ad_requests,
            ..
        } = req
        {
            Some(ad_requests)
        } else {
            None
        }
    }

    fn example_request_ads_dispatch(identifiers: &[usize]) -> DispatchRequest {
        let mut ad_requests = HashSet::new();
        for id in identifiers {
            ad_requests.insert(AdPlacementRequest {
                content: None,
                count: 4,
                placement: format!("test_placement_{id}"),
            });
        }
        DispatchRequest::RequestAds { ad_requests }
    }

    #[test]
    fn identical_command_comes_out() {
        let request = example_request_ads_dispatch(&[0]);
        let command = example_request_ads_dispatch(&[0]);
        let queue = RequestQueue::new();

        queue.push_request(request.clone());
        let retrieved_dispatch = queue.next().expect("Request should exist in queue");
        assert_eq!(command, retrieved_dispatch);
    }

    #[test]
    fn batch_similar_requests() {
        let request_0 = example_request_ads_dispatch(&[0]);
        let request_1 = example_request_ads_dispatch(&[1]);
        let dispatch = example_request_ads_dispatch(&[0, 1]);
        let queue = RequestQueue::new();

        // Having two "placement_0" and one "placement_1" should result in returning a single DispatchRequest with one "placement_0" and one "placement_1"
        queue.push_request(request_0.clone());
        queue.push_request(request_0.clone());
        queue.push_request(request_1.clone());

        let retrieved_dispatch = queue.next().expect("Request should exist in queue");

        assert!(queue.next().is_none());

        {
            let inner = queue.inner.lock();
            assert!(inner.queued_ads.is_empty());
            assert!(inner.request_queue.is_empty());
        }

        assert_eq!(dispatch, retrieved_dispatch);
    }

    #[test]
    fn split_off_too_many_ads() {
        let queue = RequestQueue::new();

        // Queue enough of this command to go one-over the limit.
        for i in 0..(MAXIMUM_ADS_BATCH_COUNT + 1) {
            let request = example_request_ads_dispatch(&[i]);
            queue.push_request(request.clone());
        }

        let mut retrieved_dispatch_many =
            queue.next().expect("First command should exist in queue");
        let mut retrieved_dispatch_overflow =
            queue.next().expect("Second command should exist in queue");

        assert!(queue.next().is_none());
        {
            let inner = queue.inner.lock();
            assert!(inner.queued_ads.is_empty());
            assert!(inner.request_queue.is_empty());
        }

        // We do not check for a precise match with sample data here, because HashMap<..>s are unsorted, so which placement gets pushed to the next one is random.
        // It is also irrelevant to the MARS request.
        // (So for example, if we have 101 requests, and a batch size of 100, the one excluded may be placement 1, placement 37, etc.)
        // Here, we specifically only check for the number of requests provided.
        // Retrieved command should have `MAXIMUM_ADS_BATCH_COUNT` requests.
        let retrieved_dispatch_many_ads = extract_request_ads(&mut retrieved_dispatch_many)
            .expect("Example DispatchRequest should be RequestAds variant");
        assert_eq!(retrieved_dispatch_many_ads.len(), MAXIMUM_ADS_BATCH_COUNT);

        // Retrieved overflow should have 1 request, matching the original command.
        let retrieved_dispatch_overflow_ads = extract_request_ads(&mut retrieved_dispatch_overflow)
            .expect("Example DispatchRequest should be RequestAds variant");
        assert_eq!(retrieved_dispatch_overflow_ads.len(), 1);
    }

    #[test]
    fn waiting_if_available_returns_successfully_push_queued_request() {
        let queue = RequestQueue::new();
        let queue_clone = queue.clone();

        // Check immediate result for `wait_available`
        queue_clone.push_request(DispatchRequest::RecordClick {
            url: Url::parse("https://example.net").unwrap(),
        });
        let available = queue.wait_available(Some(Duration::from_secs(2)));
        assert!(available);
        assert!(queue.next().is_some());

        // Check result in `wait_available` added by another thread while waiting.
        std::thread::spawn(move || {
            std::thread::sleep(Duration::from_secs(1));
            println!("Pushing request.");
            queue_clone.push_request(DispatchRequest::RecordClick {
                url: Url::parse("https://example.net").unwrap(),
            });
        });

        assert_eq!(queue.next(), None);
        println!("Waiting for lock.");
        let available = queue.wait_available(Some(Duration::from_secs(2)));
        assert!(available);
        assert!(queue.next().is_some());
        assert!(queue.next().is_none());
    }

    #[test]
    fn waiting_if_available_returns_successfully_push_ad_request() {
        let queue = RequestQueue::new();
        let queue_clone = queue.clone();

        // Check immediate result for `wait_available`
        queue_clone.push_request(example_request_ads_dispatch(&[1]));
        let available = queue.wait_available(Some(Duration::from_secs(2)));
        assert!(available);
        assert!(queue.next().is_some());

        // Check result in `wait_available` added by another thread while waiting.
        std::thread::spawn(move || {
            std::thread::sleep(Duration::from_secs(1));
            println!("Pushing request.");
            queue_clone.push_request(example_request_ads_dispatch(&[1]));
        });

        assert_eq!(queue.next(), None);
        println!("Waiting for lock.");
        let available = queue.wait_available(Some(Duration::from_secs(2)));
        assert!(available);
        assert!(queue.next().is_some());
        assert!(queue.next().is_none());
    }

    #[test]
    fn waiting_if_available_returns_unavailable() {
        let queue = RequestQueue::new();

        // Check immediate result for `wait_available`
        let available = queue.wait_available(Some(Duration::from_secs(2)));
        assert!(!available);
        assert!(queue.next().is_none());
    }
}
