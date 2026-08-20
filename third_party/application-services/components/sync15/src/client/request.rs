/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use super::storage_client::Sync15ClientResponse;
use crate::ServerTimestamp;
use crate::bso::OutgoingEncryptedBso;
use crate::error::{self, Error as ErrorKind, Result, debug, info, warn};
use serde_derive::*;
use std::collections::HashMap;
use std::default::Default;
use std::ops::Deref;
use sync_guid::Guid;
use viaduct::status_codes;

/// Manages a pair of (byte, count) limits for a PostQueue, such as
/// (max_post_bytes, max_post_records) or (max_total_bytes, max_total_records).
#[derive(Debug, Clone)]
struct LimitTracker {
    max_bytes: usize,
    max_records: usize,
    cur_bytes: usize,
    cur_records: usize,
}

impl LimitTracker {
    pub fn new(max_bytes: usize, max_records: usize) -> LimitTracker {
        LimitTracker {
            max_bytes,
            max_records,
            cur_bytes: 0,
            cur_records: 0,
        }
    }

    pub fn clear(&mut self) {
        self.cur_records = 0;
        self.cur_bytes = 0;
    }

    pub fn can_add_record(&self, payload_size: usize) -> bool {
        // Desktop does the cur_bytes check as exclusive, but we shouldn't see any servers that
        // don't have https://github.com/mozilla-services/server-syncstorage/issues/73
        self.cur_records < self.max_records && self.cur_bytes + payload_size <= self.max_bytes
    }

    pub fn can_never_add(&self, record_size: usize) -> bool {
        record_size >= self.max_bytes
    }

    pub fn record_added(&mut self, record_size: usize) {
        assert!(
            self.can_add_record(record_size),
            "LimitTracker::record_added caller must check can_add_record"
        );
        self.cur_records += 1;
        self.cur_bytes += record_size;
    }
}

#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct InfoConfiguration {
    /// The maximum size in bytes of the overall HTTP request body that will be accepted by the
    /// server.
    #[serde(default = "default_max_request_bytes")]
    pub max_request_bytes: usize,

    /// The maximum number of records that can be uploaded to a collection in a single POST request.
    #[serde(default = "usize::max_value")]
    pub max_post_records: usize,

    /// The maximum combined size in bytes of the record payloads that can be uploaded to a
    /// collection in a single POST request.
    #[serde(default = "usize::max_value")]
    pub max_post_bytes: usize,

    /// The maximum total number of records that can be uploaded to a collection as part of a
    /// batched upload.
    #[serde(default = "usize::max_value")]
    pub max_total_records: usize,

    /// The maximum total combined size in bytes of the record payloads that can be uploaded to a
    /// collection as part of a batched upload.
    #[serde(default = "usize::max_value")]
    pub max_total_bytes: usize,

    /// The maximum size of an individual BSO payload, in bytes.
    #[serde(default = "default_max_record_payload_bytes")]
    pub max_record_payload_bytes: usize,
}

// This is annoying but seems to be the only way to do it...
fn default_max_request_bytes() -> usize {
    260 * 1024
}
fn default_max_record_payload_bytes() -> usize {
    256 * 1024
}

impl Default for InfoConfiguration {
    #[inline]
    fn default() -> InfoConfiguration {
        InfoConfiguration {
            max_request_bytes: default_max_request_bytes(),
            max_record_payload_bytes: default_max_record_payload_bytes(),
            max_post_records: usize::MAX,
            max_post_bytes: usize::MAX,
            max_total_records: usize::MAX,
            max_total_bytes: usize::MAX,
        }
    }
}

#[derive(Clone, Debug, Default, Deserialize, Serialize)]
pub struct InfoCollections(pub(crate) HashMap<String, ServerTimestamp>);

impl InfoCollections {
    pub fn new(collections: HashMap<String, ServerTimestamp>) -> InfoCollections {
        InfoCollections(collections)
    }
}

impl Deref for InfoCollections {
    type Target = HashMap<String, ServerTimestamp>;

    fn deref(&self) -> &HashMap<String, ServerTimestamp> {
        &self.0
    }
}

#[derive(Debug, Clone, Deserialize)]
pub struct UploadResult {
    batch: Option<String>,
    /// Maps record id => why failed
    #[serde(default = "HashMap::new")]
    pub failed: HashMap<Guid, String>,
    /// Vec of ids
    #[serde(default = "Vec::new")]
    pub success: Vec<Guid>,
}

pub type PostResponse = Sync15ClientResponse<UploadResult>;

#[derive(Debug, Clone, Eq, PartialEq, Ord, PartialOrd, Hash)]
pub enum BatchState {
    Unsupported,
    NoBatch,
    InBatch(String),
}

#[derive(Debug)]
pub struct PostQueue<Post, OnResponse> {
    poster: Post,
    on_response: OnResponse,
    post_limits: LimitTracker,
    batch_limits: LimitTracker,
    max_payload_bytes: usize,
    max_request_bytes: usize,
    queued: Vec<u8>,
    batch: BatchState,
    last_modified: ServerTimestamp,
}

pub trait BatchPoster {
    /// Note: Last argument (reference to the batch poster) is provided for the purposes of testing
    /// Important: Poster should not report non-success HTTP statuses as errors!!
    fn post<P, O>(
        &self,
        body: Vec<u8>,
        xius: ServerTimestamp,
        batch: Option<String>,
        commit: bool,
        queue: &PostQueue<P, O>,
    ) -> Result<PostResponse>;
}

// We don't just use a FnMut here since we want to override it in mocking for RefCell<TestType>,
// which we can't do for FnMut since neither FnMut nor RefCell are defined here. Also, this
// is somewhat better for documentation.
pub trait PostResponseHandler {
    fn handle_response(&mut self, r: PostResponse, mid_batch: bool) -> Result<()>;
}

#[derive(Debug, Clone)]
pub(crate) struct NormalResponseHandler {
    pub failed_ids: Vec<Guid>,
    pub successful_ids: Vec<Guid>,
    pub allow_failed: bool,
    pub pending_failed: Vec<Guid>,
    pub pending_success: Vec<Guid>,
}

impl NormalResponseHandler {
    pub fn new(allow_failed: bool) -> NormalResponseHandler {
        NormalResponseHandler {
            failed_ids: vec![],
            successful_ids: vec![],
            pending_failed: vec![],
            pending_success: vec![],
            allow_failed,
        }
    }
}

impl PostResponseHandler for NormalResponseHandler {
    fn handle_response(&mut self, r: PostResponse, mid_batch: bool) -> error::Result<()> {
        match r {
            Sync15ClientResponse::Success { record, .. } => {
                if !record.failed.is_empty() && !self.allow_failed {
                    return Err(ErrorKind::RecordUploadFailed);
                }
                for id in record.success.iter() {
                    self.pending_success.push(id.clone());
                }
                for kv in record.failed.iter() {
                    self.pending_failed.push(kv.0.clone());
                }
                if !mid_batch {
                    self.successful_ids.append(&mut self.pending_success);
                    self.failed_ids.append(&mut self.pending_failed);
                }
                Ok(())
            }
            _ => Err(r.create_storage_error()),
        }
    }
}

impl<Poster, OnResponse> PostQueue<Poster, OnResponse>
where
    Poster: BatchPoster,
    OnResponse: PostResponseHandler,
{
    pub fn new(
        config: &InfoConfiguration,
        ts: ServerTimestamp,
        poster: Poster,
        on_response: OnResponse,
    ) -> PostQueue<Poster, OnResponse> {
        PostQueue {
            poster,
            on_response,
            last_modified: ts,
            post_limits: LimitTracker::new(config.max_post_bytes, config.max_post_records),
            batch_limits: LimitTracker::new(config.max_total_bytes, config.max_total_records),
            batch: BatchState::NoBatch,
            max_payload_bytes: config.max_record_payload_bytes,
            max_request_bytes: config.max_request_bytes,
            queued: Vec::new(),
        }
    }

    #[inline]
    fn in_batch(&self) -> bool {
        !matches!(&self.batch, BatchState::Unsupported | BatchState::NoBatch)
    }

    pub fn enqueue(&mut self, record: &OutgoingEncryptedBso) -> Result<bool> {
        let payload_length = record.serialized_payload_len();

        if self.post_limits.can_never_add(payload_length)
            || self.batch_limits.can_never_add(payload_length)
            || payload_length >= self.max_payload_bytes
        {
            warn!(
                "Single record too large to submit to server ({} b)",
                payload_length
            );
            return Ok(false);
        }

        // Write directly into `queued` but undo if necessary (the vast majority of the time
        // it won't be necessary). If we hit a problem we need to undo that, but the only error
        // case we have to worry about right now is in flush()
        let item_start = self.queued.len();

        // This is conservative but can't hurt.
        self.queued.reserve(payload_length + 2);

        // Either the first character in an array, or a comma separating
        // it from the previous item.
        let c = if self.queued.is_empty() { b'[' } else { b',' };
        self.queued.push(c);

        // This unwrap is fine, since serde_json's failure case is HashMaps that have non-object
        // keys, which is impossible. If you decide to change this part, you *need* to call
        // `self.queued.truncate(item_start)` here in the failure case!
        serde_json::to_writer(&mut self.queued, &record).unwrap();

        let item_end = self.queued.len();

        debug_assert!(
            item_end >= payload_length,
            "EncryptedPayload::serialized_len is bugged"
        );

        // The + 1 is only relevant for the final record, which will have a trailing ']'.
        let item_len = item_end - item_start + 1;

        if item_len >= self.max_request_bytes {
            self.queued.truncate(item_start);
            warn!(
                "Single record too large to submit to server ({} b)",
                item_len
            );
            return Ok(false);
        }

        let can_post_record = self.post_limits.can_add_record(payload_length);
        let can_batch_record = self.batch_limits.can_add_record(payload_length);
        let can_send_record = self.queued.len() < self.max_request_bytes;

        if !can_post_record || !can_send_record || !can_batch_record {
            debug!(
                "PostQueue flushing! (can_post = {}, can_send = {}, can_batch = {})",
                can_post_record, can_send_record, can_batch_record
            );
            // "unwrite" the record.
            self.queued.truncate(item_start);
            // Flush whatever we have queued.
            self.flush(!can_batch_record)?;
            // And write it again.
            let c = if self.queued.is_empty() { b'[' } else { b',' };
            self.queued.push(c);
            serde_json::to_writer(&mut self.queued, &record).unwrap();
        }

        self.post_limits.record_added(payload_length);
        self.batch_limits.record_added(payload_length);

        Ok(true)
    }

    pub fn flush(&mut self, want_commit: bool) -> Result<()> {
        if self.queued.is_empty() {
            assert!(
                !self.in_batch(),
                "Bug: Somehow we're in a batch but have no queued records"
            );
            // Nothing to do!
            return Ok(());
        }

        self.queued.push(b']');
        let batch_id = match &self.batch {
            // Not the first post and we know we have no batch semantics.
            BatchState::Unsupported => None,
            // First commit in possible batch
            BatchState::NoBatch => Some("true".into()),
            // In a batch and we have a batch id.
            BatchState::InBatch(s) => Some(s.clone()),
        };

        info!(
            "Posting {} records of {} bytes",
            self.post_limits.cur_records,
            self.queued.len()
        );

        let is_commit = want_commit && batch_id.is_some();
        // Weird syntax for calling a function object that is a property.
        let resp_or_error = self.poster.post(
            self.queued.clone(),
            self.last_modified,
            batch_id,
            is_commit,
            self,
        );

        self.queued.truncate(0);

        if want_commit || self.batch == BatchState::Unsupported {
            self.batch_limits.clear();
        }
        self.post_limits.clear();

        let resp = resp_or_error?;

        let (status, last_modified, record) = match resp {
            Sync15ClientResponse::Success {
                status,
                last_modified,
                ref record,
                ..
            } => (status, last_modified, record),
            _ => {
                self.on_response.handle_response(resp, !want_commit)?;
                // on_response() should always fail!
                unreachable!();
            }
        };

        if want_commit || self.batch == BatchState::Unsupported {
            self.last_modified = last_modified;
        }

        if want_commit {
            debug!("Committed batch {:?}", self.batch);
            self.batch = BatchState::NoBatch;
            self.on_response.handle_response(resp, false)?;
            return Ok(());
        }

        if status != status_codes::ACCEPTED {
            if self.in_batch() {
                return Err(ErrorKind::ServerBatchProblem(
                    "Server responded non-202 success code while a batch was in progress",
                ));
            }
            self.last_modified = last_modified;
            self.batch = BatchState::Unsupported;
            self.batch_limits.clear();
            self.on_response.handle_response(resp, false)?;
            return Ok(());
        }

        let batch_id = record
            .batch
            .as_ref()
            .ok_or({
                ErrorKind::ServerBatchProblem("Invalid server response: 202 without a batch ID")
            })?
            .clone();

        match &self.batch {
            BatchState::Unsupported => {
                warn!("Server changed its mind about supporting batching mid-batch...");
            }

            BatchState::InBatch(cur_id) => {
                if cur_id != &batch_id {
                    return Err(ErrorKind::ServerBatchProblem(
                        "Invalid server response: 202 without a batch ID",
                    ));
                }
            }
            _ => {}
        }

        // Can't change this in match arms without NLL
        self.batch = BatchState::InBatch(batch_id);
        self.last_modified = last_modified;

        self.on_response.handle_response(resp, true)?;

        Ok(())
    }
}

#[derive(Clone)]
pub struct UploadInfo {
    pub successful_ids: Vec<Guid>,
    pub failed_ids: Vec<Guid>,
    pub modified_timestamp: ServerTimestamp,
}

impl<Poster> PostQueue<Poster, NormalResponseHandler> {
    // TODO: should take by move
    pub fn completed_upload_info(&mut self) -> UploadInfo {
        let mut result = UploadInfo {
            successful_ids: Vec::with_capacity(self.on_response.successful_ids.len()),
            failed_ids: Vec::with_capacity(
                self.on_response.failed_ids.len()
                    + self.on_response.pending_failed.len()
                    + self.on_response.pending_success.len(),
            ),
            modified_timestamp: self.last_modified,
        };

        result
            .successful_ids
            .append(&mut self.on_response.successful_ids);

        result.failed_ids.append(&mut self.on_response.failed_ids);
        result
            .failed_ids
            .append(&mut self.on_response.pending_failed);
        result
            .failed_ids
            .append(&mut self.on_response.pending_success);

        result
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::EncryptedPayload;
    use crate::bso::{IncomingEncryptedBso, OutgoingEncryptedBso, OutgoingEnvelope};
    use lazy_static::lazy_static;
    use std::cell::RefCell;
    use std::collections::VecDeque;
    use std::rc::Rc;

    #[derive(Debug, Clone)]
    struct PostedData {
        body: String,
        _xius: ServerTimestamp,
        batch: Option<String>,
        commit: bool,
        payload_bytes: usize,
        records: usize,
    }

    impl PostedData {
        fn records_as_json(&self) -> Vec<serde_json::Value> {
            let values =
                serde_json::from_str::<serde_json::Value>(&self.body).expect("Posted invalid json");
            // Check that they actually deserialize as what we want
            let records_or_err =
                serde_json::from_value::<Vec<IncomingEncryptedBso>>(values.clone());
            records_or_err.expect("Failed to deserialize data");
            serde_json::from_value(values).unwrap()
        }
    }

    #[derive(Debug, Clone)]
    struct BatchInfo {
        id: Option<String>,
        posts: Vec<PostedData>,
        bytes: usize,
        records: usize,
    }

    #[derive(Debug, Clone)]
    struct TestPoster {
        all_posts: Vec<PostedData>,
        responses: VecDeque<PostResponse>,
        batches: Vec<BatchInfo>,
        cur_batch: Option<BatchInfo>,
        cfg: InfoConfiguration,
    }

    type TestPosterRef = Rc<RefCell<TestPoster>>;
    impl TestPoster {
        pub fn new<T>(cfg: &InfoConfiguration, responses: T) -> TestPosterRef
        where
            T: Into<VecDeque<PostResponse>>,
        {
            Rc::new(RefCell::new(TestPoster {
                all_posts: vec![],
                responses: responses.into(),
                batches: vec![],
                cur_batch: None,
                cfg: cfg.clone(),
            }))
        }
        // Adds &mut
        fn do_post<T, O>(
            &mut self,
            body: &[u8],
            xius: ServerTimestamp,
            batch: Option<String>,
            commit: bool,
            queue: &PostQueue<T, O>,
        ) -> Sync15ClientResponse<UploadResult> {
            let mut post = PostedData {
                body: String::from_utf8(body.into()).expect("Posted invalid utf8..."),
                batch: batch.clone(),
                _xius: xius,
                commit,
                payload_bytes: 0,
                records: 0,
            };

            assert!(body.len() <= self.cfg.max_request_bytes);

            let (num_records, record_payload_bytes) = {
                let recs = post.records_as_json();
                assert!(recs.len() <= self.cfg.max_post_records);
                assert!(recs.len() <= self.cfg.max_total_records);
                let payload_bytes: usize = recs
                    .iter()
                    .map(|r| {
                        let len = r["payload"]
                            .as_str()
                            .expect("Non string payload property")
                            .len();
                        assert!(len <= self.cfg.max_record_payload_bytes);
                        len
                    })
                    .sum();
                assert!(payload_bytes <= self.cfg.max_post_bytes);
                assert!(payload_bytes <= self.cfg.max_total_bytes);

                assert_eq!(queue.post_limits.cur_bytes, payload_bytes);
                assert_eq!(queue.post_limits.cur_records, recs.len());
                (recs.len(), payload_bytes)
            };
            post.payload_bytes = record_payload_bytes;
            post.records = num_records;

            self.all_posts.push(post.clone());
            let response = self.responses.pop_front().unwrap();

            let record = match response {
                Sync15ClientResponse::Success { ref record, .. } => record,
                _ => {
                    panic!("only success codes are used in this test");
                }
            };

            if let Some(cur_batch) = &self.cur_batch {
                assert_eq!(
                    batch, cur_batch.id,
                    "We're in a batch but got the wrong batch id"
                );
            } else {
                assert!(
                    batch.is_none() || batch == Some("true".into()),
                    "We shouldn't be in a batch now"
                );
                self.cur_batch = Some(BatchInfo {
                    id: record.batch.clone(),
                    posts: vec![],
                    records: 0,
                    bytes: 0,
                });
            }

            {
                let batch = self.cur_batch.as_mut().unwrap();
                batch.posts.push(post);
                batch.records += num_records;
                batch.bytes += record_payload_bytes;

                assert!(batch.bytes <= self.cfg.max_total_bytes);
                assert!(batch.records <= self.cfg.max_total_records);

                assert_eq!(batch.records, queue.batch_limits.cur_records);
                assert_eq!(batch.bytes, queue.batch_limits.cur_bytes);
            }

            if commit || record.batch.is_none() {
                let batch = self.cur_batch.take().unwrap();
                self.batches.push(batch);
            }

            response
        }

        fn do_handle_response(&mut self, _: PostResponse, mid_batch: bool) {
            assert_eq!(mid_batch, self.cur_batch.is_some());
        }
    }
    impl BatchPoster for TestPosterRef {
        fn post<T, O>(
            &self,
            body: Vec<u8>,
            xius: ServerTimestamp,
            batch: Option<String>,
            commit: bool,
            queue: &PostQueue<T, O>,
        ) -> Result<PostResponse> {
            Ok(self.borrow_mut().do_post(&body, xius, batch, commit, queue))
        }
    }

    impl PostResponseHandler for TestPosterRef {
        fn handle_response(&mut self, r: PostResponse, mid_batch: bool) -> Result<()> {
            self.borrow_mut().do_handle_response(r, mid_batch);
            Ok(())
        }
    }

    type MockedPostQueue = PostQueue<TestPosterRef, TestPosterRef>;

    fn pq_test_setup(
        cfg: InfoConfiguration,
        lm: i64,
        resps: Vec<PostResponse>,
    ) -> (MockedPostQueue, TestPosterRef) {
        let tester = TestPoster::new(&cfg, resps);
        let pq = PostQueue::new(&cfg, ServerTimestamp(lm), tester.clone(), tester.clone());
        (pq, tester)
    }

    fn fake_response<'a, T: Into<Option<&'a str>>>(status: u16, lm: i64, batch: T) -> PostResponse {
        assert!(status_codes::is_success_code(status));
        Sync15ClientResponse::Success {
            status,
            last_modified: ServerTimestamp(lm),
            record: UploadResult {
                batch: batch.into().map(Into::into),
                failed: HashMap::new(),
                success: vec![],
            },
            route: "test/path".into(),
        }
    }

    lazy_static! {
        // ~40b
        static ref PAYLOAD_OVERHEAD: usize = {
            let payload = EncryptedPayload {
                iv: "".into(),
                hmac: "".into(),
                ciphertext: "".into()
            };
            serde_json::to_string(&payload).unwrap().len()
        };
        // ~80b
        static ref TOTAL_RECORD_OVERHEAD: usize = {
            let val = serde_json::to_value(OutgoingEncryptedBso::new(OutgoingEnvelope {
                    id: "".into(),
                    sortindex: None,
                    ttl: None,
                },
                EncryptedPayload {
                    iv: "".into(),
                    hmac: "".into(),
                    ciphertext: "".into()
                },
            )).unwrap();
            serde_json::to_string(&val).unwrap().len()
        };
        // There's some subtlety in how we calculate this having to do with the fact that
        // the quotes in the payload are escaped but the escape chars count to the request len
        // and *not* to the payload len (the payload len check happens after json parsing the
        // top level object).
        static ref NON_PAYLOAD_OVERHEAD: usize = {
            *TOTAL_RECORD_OVERHEAD - *PAYLOAD_OVERHEAD
        };
    }

    // Actual record size (for max_request_len) will be larger by some amount
    fn make_record(payload_size: usize) -> OutgoingEncryptedBso {
        assert!(payload_size > *PAYLOAD_OVERHEAD);
        let ciphertext_len = payload_size - *PAYLOAD_OVERHEAD;
        OutgoingEncryptedBso::new(
            OutgoingEnvelope {
                id: "".into(),
                sortindex: None,
                ttl: None,
            },
            EncryptedPayload {
                iv: "".into(),
                hmac: "".into(),
                ciphertext: "x".repeat(ciphertext_len),
            },
        )
    }

    fn request_bytes_for_payloads(payloads: &[usize]) -> usize {
        1 + payloads
            .iter()
            .map(|&size| size + 1 + *NON_PAYLOAD_OVERHEAD)
            .sum::<usize>()
    }

    #[test]
    fn test_pq_basic() {
        let cfg = InfoConfiguration {
            max_request_bytes: 1000,
            max_record_payload_bytes: 1000,
            ..InfoConfiguration::default()
        };
        let time = 11_111_111_000;
        let (mut pq, tester) = pq_test_setup(
            cfg,
            time,
            vec![fake_response(status_codes::OK, time + 100_000, None)],
        );

        pq.enqueue(&make_record(100)).unwrap();
        pq.flush(true).unwrap();

        let t = tester.borrow();
        assert!(t.cur_batch.is_none());
        assert_eq!(t.all_posts.len(), 1);
        assert_eq!(t.batches.len(), 1);
        assert_eq!(t.batches[0].posts.len(), 1);
        assert_eq!(t.batches[0].records, 1);
        assert_eq!(t.batches[0].bytes, 100);
        assert_eq!(
            t.batches[0].posts[0].body.len(),
            request_bytes_for_payloads(&[100])
        );
    }

    #[test]
    fn test_pq_max_request_bytes_no_batch() {
        let cfg = InfoConfiguration {
            max_request_bytes: 250,
            ..InfoConfiguration::default()
        };
        let time = 11_111_111_000;
        let (mut pq, tester) = pq_test_setup(
            cfg,
            time,
            vec![
                fake_response(status_codes::OK, time + 100_000, None),
                fake_response(status_codes::OK, time + 200_000, None),
            ],
        );

        // Note that the total record overhead is around 85 bytes
        let payload_size = 100 - *NON_PAYLOAD_OVERHEAD;
        pq.enqueue(&make_record(payload_size)).unwrap(); // total size == 102; [r]
        pq.enqueue(&make_record(payload_size)).unwrap(); // total size == 203; [r,r]
        pq.enqueue(&make_record(payload_size)).unwrap(); // too big, 2nd post.
        pq.flush(true).unwrap();

        let t = tester.borrow();
        assert!(t.cur_batch.is_none());
        assert_eq!(t.all_posts.len(), 2);
        assert_eq!(t.batches.len(), 2);
        assert_eq!(t.batches[0].posts.len(), 1);
        assert_eq!(t.batches[0].records, 2);
        assert_eq!(t.batches[0].bytes, payload_size * 2);
        assert_eq!(t.batches[0].posts[0].batch, Some("true".into()));
        assert_eq!(
            t.batches[0].posts[0].body.len(),
            request_bytes_for_payloads(&[payload_size, payload_size])
        );

        assert_eq!(t.batches[1].posts.len(), 1);
        assert_eq!(t.batches[1].records, 1);
        assert_eq!(t.batches[1].bytes, payload_size);
        // We know at this point that the server does not support batching.
        assert_eq!(t.batches[1].posts[0].batch, None);
        assert!(!t.batches[1].posts[0].commit);
        assert_eq!(
            t.batches[1].posts[0].body.len(),
            request_bytes_for_payloads(&[payload_size])
        );
    }

    #[test]
    fn test_pq_max_record_payload_bytes_no_batch() {
        let cfg = InfoConfiguration {
            max_record_payload_bytes: 150,
            max_request_bytes: 350,
            ..InfoConfiguration::default()
        };
        let time = 11_111_111_000;
        let (mut pq, tester) = pq_test_setup(
            cfg,
            time,
            vec![
                fake_response(status_codes::OK, time + 100_000, None),
                fake_response(status_codes::OK, time + 200_000, None),
            ],
        );

        // Note that the total record overhead is around 85 bytes
        let payload_size = 100 - *NON_PAYLOAD_OVERHEAD;
        pq.enqueue(&make_record(payload_size)).unwrap(); // total size == 102; [r]
        let enqueued = pq.enqueue(&make_record(151)).unwrap(); // still 102
        assert!(!enqueued, "Should not have fit");
        pq.enqueue(&make_record(payload_size)).unwrap();
        pq.flush(true).unwrap();

        let t = tester.borrow();
        assert!(t.cur_batch.is_none());
        assert_eq!(t.all_posts.len(), 1);
        assert_eq!(t.batches.len(), 1);
        assert_eq!(t.batches[0].posts.len(), 1);
        assert_eq!(t.batches[0].records, 2);
        assert_eq!(t.batches[0].bytes, payload_size * 2);
        assert_eq!(
            t.batches[0].posts[0].body.len(),
            request_bytes_for_payloads(&[payload_size, payload_size])
        );
    }

    #[test]
    fn test_pq_single_batch() {
        let cfg = InfoConfiguration::default();
        let time = 11_111_111_000;
        let (mut pq, tester) = pq_test_setup(
            cfg,
            time,
            vec![fake_response(
                status_codes::ACCEPTED,
                time + 100_000,
                Some("1234"),
            )],
        );

        let payload_size = 100 - *NON_PAYLOAD_OVERHEAD;
        pq.enqueue(&make_record(payload_size)).unwrap();
        pq.enqueue(&make_record(payload_size)).unwrap();
        pq.enqueue(&make_record(payload_size)).unwrap();
        pq.flush(true).unwrap();

        let t = tester.borrow();
        assert!(t.cur_batch.is_none());
        assert_eq!(t.all_posts.len(), 1);
        assert_eq!(t.batches.len(), 1);
        assert_eq!(t.batches[0].id.as_ref().unwrap(), "1234");
        assert_eq!(t.batches[0].posts.len(), 1);
        assert_eq!(t.batches[0].records, 3);
        assert_eq!(t.batches[0].bytes, payload_size * 3);
        assert!(t.batches[0].posts[0].commit);
        assert_eq!(
            t.batches[0].posts[0].body.len(),
            request_bytes_for_payloads(&[payload_size, payload_size, payload_size])
        );
    }

    #[test]
    fn test_pq_multi_post_batch_bytes() {
        let cfg = InfoConfiguration {
            max_post_bytes: 200,
            ..InfoConfiguration::default()
        };
        let time = 11_111_111_000;
        let (mut pq, tester) = pq_test_setup(
            cfg,
            time,
            vec![
                fake_response(status_codes::ACCEPTED, time, Some("1234")),
                fake_response(status_codes::ACCEPTED, time + 100_000, Some("1234")),
            ],
        );

        pq.enqueue(&make_record(100)).unwrap();
        pq.enqueue(&make_record(100)).unwrap();
        // POST
        pq.enqueue(&make_record(100)).unwrap();
        pq.flush(true).unwrap(); // COMMIT

        let t = tester.borrow();
        assert!(t.cur_batch.is_none());
        assert_eq!(t.all_posts.len(), 2);
        assert_eq!(t.batches.len(), 1);
        assert_eq!(t.batches[0].posts.len(), 2);
        assert_eq!(t.batches[0].records, 3);
        assert_eq!(t.batches[0].bytes, 300);

        assert_eq!(t.batches[0].posts[0].batch.as_ref().unwrap(), "true");
        assert_eq!(t.batches[0].posts[0].records, 2);
        assert_eq!(t.batches[0].posts[0].payload_bytes, 200);
        assert!(!t.batches[0].posts[0].commit);
        assert_eq!(
            t.batches[0].posts[0].body.len(),
            request_bytes_for_payloads(&[100, 100])
        );

        assert_eq!(t.batches[0].posts[1].batch.as_ref().unwrap(), "1234");
        assert_eq!(t.batches[0].posts[1].records, 1);
        assert_eq!(t.batches[0].posts[1].payload_bytes, 100);
        assert!(t.batches[0].posts[1].commit);
        assert_eq!(
            t.batches[0].posts[1].body.len(),
            request_bytes_for_payloads(&[100])
        );
    }

    #[test]
    fn test_pq_multi_post_batch_records() {
        let cfg = InfoConfiguration {
            max_post_records: 3,
            ..InfoConfiguration::default()
        };
        let time = 11_111_111_000;
        let (mut pq, tester) = pq_test_setup(
            cfg,
            time,
            vec![
                fake_response(status_codes::ACCEPTED, time, Some("1234")),
                fake_response(status_codes::ACCEPTED, time, Some("1234")),
                fake_response(status_codes::ACCEPTED, time + 100_000, Some("1234")),
            ],
        );

        pq.enqueue(&make_record(100)).unwrap();
        pq.enqueue(&make_record(100)).unwrap();
        pq.enqueue(&make_record(100)).unwrap();
        // POST
        pq.enqueue(&make_record(100)).unwrap();
        pq.enqueue(&make_record(100)).unwrap();
        pq.enqueue(&make_record(100)).unwrap();
        // POST
        pq.enqueue(&make_record(100)).unwrap();
        pq.flush(true).unwrap(); // COMMIT

        let t = tester.borrow();
        assert!(t.cur_batch.is_none());
        assert_eq!(t.all_posts.len(), 3);
        assert_eq!(t.batches.len(), 1);
        assert_eq!(t.batches[0].posts.len(), 3);
        assert_eq!(t.batches[0].records, 7);
        assert_eq!(t.batches[0].bytes, 700);

        assert_eq!(t.batches[0].posts[0].batch.as_ref().unwrap(), "true");
        assert_eq!(t.batches[0].posts[0].records, 3);
        assert_eq!(t.batches[0].posts[0].payload_bytes, 300);
        assert!(!t.batches[0].posts[0].commit);
        assert_eq!(
            t.batches[0].posts[0].body.len(),
            request_bytes_for_payloads(&[100, 100, 100])
        );

        assert_eq!(t.batches[0].posts[1].batch.as_ref().unwrap(), "1234");
        assert_eq!(t.batches[0].posts[1].records, 3);
        assert_eq!(t.batches[0].posts[1].payload_bytes, 300);
        assert!(!t.batches[0].posts[1].commit);
        assert_eq!(
            t.batches[0].posts[1].body.len(),
            request_bytes_for_payloads(&[100, 100, 100])
        );

        assert_eq!(t.batches[0].posts[2].batch.as_ref().unwrap(), "1234");
        assert_eq!(t.batches[0].posts[2].records, 1);
        assert_eq!(t.batches[0].posts[2].payload_bytes, 100);
        assert!(t.batches[0].posts[2].commit);
        assert_eq!(
            t.batches[0].posts[2].body.len(),
            request_bytes_for_payloads(&[100])
        );
    }

    #[test]
    #[allow(clippy::cognitive_complexity)]
    fn test_pq_multi_post_multi_batch_records() {
        let cfg = InfoConfiguration {
            max_post_records: 3,
            max_total_records: 5,
            ..InfoConfiguration::default()
        };
        let time = 11_111_111_000;
        let (mut pq, tester) = pq_test_setup(
            cfg,
            time,
            vec![
                fake_response(status_codes::ACCEPTED, time, Some("1234")),
                fake_response(status_codes::ACCEPTED, time + 100_000, Some("1234")),
                fake_response(status_codes::ACCEPTED, time + 100_000, Some("abcd")),
                fake_response(status_codes::ACCEPTED, time + 200_000, Some("abcd")),
            ],
        );

        pq.enqueue(&make_record(100)).unwrap();
        pq.enqueue(&make_record(100)).unwrap();
        pq.enqueue(&make_record(100)).unwrap();
        // POST
        pq.enqueue(&make_record(100)).unwrap();
        pq.enqueue(&make_record(100)).unwrap();
        // POST + COMMIT
        pq.enqueue(&make_record(100)).unwrap();
        pq.enqueue(&make_record(100)).unwrap();
        pq.enqueue(&make_record(100)).unwrap();
        // POST
        pq.enqueue(&make_record(100)).unwrap();
        pq.flush(true).unwrap(); // COMMIT

        let t = tester.borrow();
        assert!(t.cur_batch.is_none());
        assert_eq!(t.all_posts.len(), 4);
        assert_eq!(t.batches.len(), 2);
        assert_eq!(t.batches[0].posts.len(), 2);
        assert_eq!(t.batches[1].posts.len(), 2);

        assert_eq!(t.batches[0].records, 5);
        assert_eq!(t.batches[1].records, 4);

        assert_eq!(t.batches[0].bytes, 500);
        assert_eq!(t.batches[1].bytes, 400);

        assert_eq!(t.batches[0].posts[0].batch.as_ref().unwrap(), "true");
        assert_eq!(t.batches[0].posts[0].records, 3);
        assert_eq!(t.batches[0].posts[0].payload_bytes, 300);
        assert!(!t.batches[0].posts[0].commit);
        assert_eq!(
            t.batches[0].posts[0].body.len(),
            request_bytes_for_payloads(&[100, 100, 100])
        );

        assert_eq!(t.batches[0].posts[1].batch.as_ref().unwrap(), "1234");
        assert_eq!(t.batches[0].posts[1].records, 2);
        assert_eq!(t.batches[0].posts[1].payload_bytes, 200);
        assert!(t.batches[0].posts[1].commit);
        assert_eq!(
            t.batches[0].posts[1].body.len(),
            request_bytes_for_payloads(&[100, 100])
        );

        assert_eq!(t.batches[1].posts[0].batch.as_ref().unwrap(), "true");
        assert_eq!(t.batches[1].posts[0].records, 3);
        assert_eq!(t.batches[1].posts[0].payload_bytes, 300);
        assert!(!t.batches[1].posts[0].commit);
        assert_eq!(
            t.batches[1].posts[0].body.len(),
            request_bytes_for_payloads(&[100, 100, 100])
        );

        assert_eq!(t.batches[1].posts[1].batch.as_ref().unwrap(), "abcd");
        assert_eq!(t.batches[1].posts[1].records, 1);
        assert_eq!(t.batches[1].posts[1].payload_bytes, 100);
        assert!(t.batches[1].posts[1].commit);
        assert_eq!(
            t.batches[1].posts[1].body.len(),
            request_bytes_for_payloads(&[100])
        );
    }

    #[test]
    #[allow(clippy::cognitive_complexity)]
    fn test_pq_multi_post_multi_batch_bytes() {
        let cfg = InfoConfiguration {
            max_post_bytes: 300,
            max_total_bytes: 500,
            ..InfoConfiguration::default()
        };
        let time = 11_111_111_000;
        let (mut pq, tester) = pq_test_setup(
            cfg,
            time,
            vec![
                fake_response(status_codes::ACCEPTED, time, Some("1234")),
                fake_response(status_codes::ACCEPTED, time + 100_000, Some("1234")), // should commit
                fake_response(status_codes::ACCEPTED, time + 100_000, Some("abcd")),
                fake_response(status_codes::ACCEPTED, time + 200_000, Some("abcd")), // should commit
            ],
        );

        pq.enqueue(&make_record(100)).unwrap();
        pq.enqueue(&make_record(100)).unwrap();
        pq.enqueue(&make_record(100)).unwrap();
        assert_eq!(pq.last_modified.0, time);
        // POST
        pq.enqueue(&make_record(100)).unwrap();
        pq.enqueue(&make_record(100)).unwrap();
        // POST + COMMIT
        pq.enqueue(&make_record(100)).unwrap();
        assert_eq!(pq.last_modified.0, time + 100_000);
        pq.enqueue(&make_record(100)).unwrap();
        pq.enqueue(&make_record(100)).unwrap();

        // POST
        pq.enqueue(&make_record(100)).unwrap();
        assert_eq!(pq.last_modified.0, time + 100_000);
        pq.flush(true).unwrap(); // COMMIT

        assert_eq!(pq.last_modified.0, time + 200_000);

        let t = tester.borrow();
        assert!(t.cur_batch.is_none());
        assert_eq!(t.all_posts.len(), 4);
        assert_eq!(t.batches.len(), 2);
        assert_eq!(t.batches[0].posts.len(), 2);
        assert_eq!(t.batches[1].posts.len(), 2);

        assert_eq!(t.batches[0].records, 5);
        assert_eq!(t.batches[1].records, 4);

        assert_eq!(t.batches[0].bytes, 500);
        assert_eq!(t.batches[1].bytes, 400);

        assert_eq!(t.batches[0].posts[0].batch.as_ref().unwrap(), "true");
        assert_eq!(t.batches[0].posts[0].records, 3);
        assert_eq!(t.batches[0].posts[0].payload_bytes, 300);
        assert!(!t.batches[0].posts[0].commit);
        assert_eq!(
            t.batches[0].posts[0].body.len(),
            request_bytes_for_payloads(&[100, 100, 100])
        );

        assert_eq!(t.batches[0].posts[1].batch.as_ref().unwrap(), "1234");
        assert_eq!(t.batches[0].posts[1].records, 2);
        assert_eq!(t.batches[0].posts[1].payload_bytes, 200);
        assert!(t.batches[0].posts[1].commit);
        assert_eq!(
            t.batches[0].posts[1].body.len(),
            request_bytes_for_payloads(&[100, 100])
        );

        assert_eq!(t.batches[1].posts[0].batch.as_ref().unwrap(), "true");
        assert_eq!(t.batches[1].posts[0].records, 3);
        assert_eq!(t.batches[1].posts[0].payload_bytes, 300);
        assert!(!t.batches[1].posts[0].commit);
        assert_eq!(
            t.batches[1].posts[0].body.len(),
            request_bytes_for_payloads(&[100, 100, 100])
        );

        assert_eq!(t.batches[1].posts[1].batch.as_ref().unwrap(), "abcd");
        assert_eq!(t.batches[1].posts[1].records, 1);
        assert_eq!(t.batches[1].posts[1].payload_bytes, 100);
        assert!(t.batches[1].posts[1].commit);
        assert_eq!(
            t.batches[1].posts[1].body.len(),
            request_bytes_for_payloads(&[100])
        );
    }

    // TODO: Test
    //
    // - error cases!!! We don't test our handling of server errors at all!
    // - mixed bytes/record limits
    //
    // A lot of these have good examples in test_postqueue.js on deskftop sync
}
