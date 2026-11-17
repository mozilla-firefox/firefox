// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

// Representation and management of connection IDs.

use std::{
    borrow::Borrow,
    cell::{Ref, RefCell},
    cmp::{max, min},
    fmt::{self, Debug, Display, Formatter},
    ops::Deref,
    rc::Rc,
};

use neqo_common::{
    Buffer, Decoder, Encoder, expect_usize,
    hex::{Hex, HexWithLen},
    to_u64,
};
use nss::{random, randomize};
use smallvec::{SmallVec, smallvec};
use tracing::info;

use crate::{
    Error, Res,
    frame::{FrameEncoder as _, FrameType},
    packet, recovery,
    stateless_reset::Token as Srt,
    stats::FrameStats,
};

#[derive(Clone, Default, Eq, Hash, PartialEq)]
pub struct ConnectionId {
    cid: SmallVec<[u8; Self::MAX_LEN]>,
}

impl ConnectionId {
    pub const MAX_LEN: usize = 20;

    /// # Panics
    /// When `len` is larger than `ConnectionId::MAX_LEN`.
    #[must_use]
    pub fn generate(len: usize) -> Self {
        assert!(matches!(len, 0..=Self::MAX_LEN));
        let mut cid = smallvec![0; len];
        randomize(&mut cid);
        Self { cid }
    }

    // Apply a wee bit of greasing here in picking a length between 8 and 20 bytes long.
    #[must_use]
    pub fn generate_initial() -> Self {
        let v = random::<1>()[0];
        // Bias selection toward picking 8 (>50% of the time).
        let len: usize = max(8, 5 + (v & (v >> 4))).into();
        Self::generate(len)
    }

    #[must_use]
    pub fn as_cid_ref(&self) -> ConnectionIdRef<'_> {
        ConnectionIdRef::from(&self.cid[..])
    }
}

impl AsRef<[u8]> for ConnectionId {
    fn as_ref(&self) -> &[u8] {
        self.borrow()
    }
}

impl Borrow<[u8]> for ConnectionId {
    fn borrow(&self) -> &[u8] {
        &self.cid
    }
}

impl From<SmallVec<[u8; Self::MAX_LEN]>> for ConnectionId {
    fn from(cid: SmallVec<[u8; Self::MAX_LEN]>) -> Self {
        Self { cid }
    }
}

impl<T: AsRef<[u8]> + ?Sized> From<&T> for ConnectionId {
    fn from(buf: &T) -> Self {
        Self::from(SmallVec::from_slice(buf.as_ref()))
    }
}

impl<'a> From<ConnectionIdRef<'a>> for ConnectionId {
    fn from(cidref: ConnectionIdRef<'a>) -> Self {
        Self::from(SmallVec::from_slice(cidref.cid))
    }
}

impl Deref for ConnectionId {
    type Target = [u8];

    fn deref(&self) -> &Self::Target {
        &self.cid
    }
}

impl Debug for ConnectionId {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        f.write_str("CID ")?;
        HexWithLen::fmt(f, &self.cid)
    }
}

impl Display for ConnectionId {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        Hex::fmt(f, &self.cid)
    }
}

impl<'a> PartialEq<ConnectionIdRef<'a>> for ConnectionId {
    fn eq(&self, other: &ConnectionIdRef<'a>) -> bool {
        &self.cid[..] == other.cid
    }
}

#[derive(Hash, Eq, PartialEq, Clone, Copy)]
pub struct ConnectionIdRef<'a> {
    cid: &'a [u8],
}

impl Debug for ConnectionIdRef<'_> {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        f.write_str("CID ")?;
        HexWithLen::fmt(f, self.cid)
    }
}

impl Display for ConnectionIdRef<'_> {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        Hex::fmt(f, self.cid)
    }
}

impl<'a, T: AsRef<[u8]> + ?Sized> From<&'a T> for ConnectionIdRef<'a> {
    fn from(cid: &'a T) -> Self {
        Self { cid: cid.as_ref() }
    }
}

impl Deref for ConnectionIdRef<'_> {
    type Target = [u8];

    fn deref(&self) -> &Self::Target {
        self.cid
    }
}

impl PartialEq<ConnectionId> for ConnectionIdRef<'_> {
    fn eq(&self, other: &ConnectionId) -> bool {
        self.cid == &other.cid[..]
    }
}

pub trait ConnectionIdDecoder {
    /// Decodes a connection ID from the provided decoder.
    fn decode_cid<'a>(&self, dec: &mut Decoder<'a>) -> Option<ConnectionIdRef<'a>>;
}

pub trait ConnectionIdGenerator: ConnectionIdDecoder {
    /// Generates a connection ID.  This can return `None` if the generator
    /// is exhausted.
    fn generate_cid(&mut self) -> Option<ConnectionId>;
    /// Indicates whether the connection IDs are zero-length.
    /// If this returns true, `generate_cid` must always produce an empty value
    /// and never `None`.
    /// If this returns false, `generate_cid` must never produce an empty value,
    /// though it can return `None`.
    ///
    /// You should not need to implement this: if you want zero-length connection IDs,
    /// use `EmptyConnectionIdGenerator` instead.
    fn generates_empty_cids(&self) -> bool {
        false
    }
}

/// An `EmptyConnectionIdGenerator` generates empty connection IDs.
#[derive(Default)]
pub struct EmptyConnectionIdGenerator {}

impl ConnectionIdDecoder for EmptyConnectionIdGenerator {
    fn decode_cid<'a>(&self, _: &mut Decoder<'a>) -> Option<ConnectionIdRef<'a>> {
        Some(ConnectionIdRef::from(&[]))
    }
}

impl ConnectionIdGenerator for EmptyConnectionIdGenerator {
    fn generate_cid(&mut self) -> Option<ConnectionId> {
        Some(ConnectionId::from(&[]))
    }
    fn generates_empty_cids(&self) -> bool {
        true
    }
}

/// An `RandomConnectionIdGenerator` produces connection IDs of
/// a fixed length and random content.  No effort is made to
/// prevent collisions.
pub struct RandomConnectionIdGenerator {
    len: usize,
}

impl RandomConnectionIdGenerator {
    #[must_use]
    pub const fn new(len: usize) -> Self {
        Self { len }
    }
}

impl ConnectionIdDecoder for RandomConnectionIdGenerator {
    fn decode_cid<'a>(&self, dec: &mut Decoder<'a>) -> Option<ConnectionIdRef<'a>> {
        dec.decode(self.len).map(ConnectionIdRef::from)
    }
}

impl ConnectionIdGenerator for RandomConnectionIdGenerator {
    fn generate_cid(&mut self) -> Option<ConnectionId> {
        let mut buf = smallvec![0; self.len];
        randomize(&mut buf);
        Some(ConnectionId::from(buf))
    }

    fn generates_empty_cids(&self) -> bool {
        self.len == 0
    }
}

/// A single connection ID, as saved from `NEW_CONNECTION_ID`.
/// This is templated so that the connection ID entries from a peer can be
/// saved with a stateless reset token.  Local entries don't need that.
#[derive(Debug, PartialEq, Eq, Clone)]
pub struct ConnectionIdEntry<SRT: Clone + PartialEq> {
    /// The sequence number.
    seqno: u64,
    /// The connection ID.
    cid: ConnectionId,
    /// The corresponding stateless reset token.
    srt: SRT,
}

impl ConnectionIdEntry<Srt> {
    /// Create the first entry, which won't have a stateless reset token.
    pub fn initial_remote(cid: ConnectionId) -> Self {
        Self::new(Self::SEQNO_INITIAL, cid, Srt::random())
    }

    /// Create an empty for when the peer chooses empty connection IDs.
    /// This uses a special sequence number just because it can.
    pub fn empty_remote() -> Self {
        Self::new(
            ConnectionIdManager::SEQNO_EMPTY,
            ConnectionId::from(&[]),
            Srt::random(),
        )
    }

    /// Determine whether this is a valid stateless reset.
    pub fn is_stateless_reset(&self, token: &Srt) -> bool {
        // A sequence number of 2^62 or more has no corresponding stateless reset token.
        (self.seqno < (1 << 62)) && self.srt.eq(token)
    }

    /// Return true if the two contain any equal parts.
    fn any_part_equal(&self, other: &Self) -> bool {
        self.seqno == other.seqno || self.cid == other.cid || self.srt == other.srt
    }

    /// The sequence number of this entry.
    pub const fn sequence_number(&self) -> u64 {
        self.seqno
    }

    /// Write the entry out in a `NEW_CONNECTION_ID` frame.
    /// Returns `true` if the frame was written, `false` if there is insufficient space.
    pub fn write<B: Buffer>(
        &self,
        builder: &mut packet::Builder<B>,
        stats: &mut FrameStats,
    ) -> bool {
        let len = 1 + Encoder::varint_len(self.seqno) + 1 + 1 + self.cid.len() + Srt::LEN;
        if builder.remaining() < len {
            return false;
        }

        builder.encode_frame(FrameType::NewConnectionId, |b| {
            b.encode_varint(self.seqno);
            b.encode_varint(0u64);
            b.encode_vec(1, &self.cid);
            b.encode(&self.srt);
        });
        stats.new_connection_id += 1;
        true
    }

    pub fn is_empty(&self) -> bool {
        self.seqno == ConnectionIdManager::SEQNO_EMPTY || self.cid.is_empty()
    }
}

impl<T: Clone + PartialEq> ConnectionIdEntry<T> {
    const SEQNO_INITIAL: u64 = 0;
}

impl ConnectionIdEntry<()> {
    /// Create an initial entry.
    pub const fn initial_local(cid: ConnectionId) -> Self {
        Self::new(0, cid, ())
    }
}

impl<SRT: Clone + PartialEq> ConnectionIdEntry<SRT> {
    pub const fn new(seqno: u64, cid: ConnectionId, srt: SRT) -> Self {
        Self { seqno, cid, srt }
    }

    /// Update the stateless reset token.  This panics if the sequence number is non-zero.
    pub fn set_stateless_reset_token(&mut self, srt: SRT) {
        assert_eq!(self.seqno, Self::SEQNO_INITIAL);
        self.srt = srt;
    }

    /// Replace the connection ID.  This panics if the sequence number is non-zero.
    pub fn update_cid(&mut self, cid: ConnectionId) {
        assert_eq!(self.seqno, Self::SEQNO_INITIAL);
        self.cid = cid;
    }

    pub const fn connection_id(&self) -> &ConnectionId {
        &self.cid
    }

    pub const fn reset_token(&self) -> &SRT {
        &self.srt
    }
}

pub type RemoteConnectionIdEntry = ConnectionIdEntry<Srt>;

/// A collection of connection IDs that are indexed by a sequence number.
/// Used to store connection IDs that are provided by a peer.
#[derive(Debug, Default)]
pub struct ConnectionIdStore<SRT: Clone + PartialEq> {
    cids: SmallVec<[ConnectionIdEntry<SRT>; 8]>,
}

impl<SRT: Clone + PartialEq> ConnectionIdStore<SRT> {
    pub fn retire(&mut self, seqno: u64) {
        self.cids.retain(|c| c.seqno != seqno);
    }

    pub fn contains(&self, cid: ConnectionIdRef) -> bool {
        self.cids.iter().any(|c| c.cid == cid)
    }

    pub fn next(&mut self) -> Option<ConnectionIdEntry<SRT>> {
        if self.cids.is_empty() {
            None
        } else {
            Some(self.cids.remove(0))
        }
    }

    pub fn len(&self) -> usize {
        self.cids.len()
    }
}

impl ConnectionIdStore<Srt> {
    pub fn add_remote(&mut self, entry: ConnectionIdEntry<Srt>) -> Res<()> {
        // It's OK if this perfectly matches an existing entry.
        if self.cids.iter().any(|c| c == &entry) {
            return Ok(());
        }
        // It's not OK if any individual piece matches though.
        if self.cids.iter().any(|c| c.any_part_equal(&entry)) {
            info!("ConnectionIdStore found reused part in NEW_CONNECTION_ID");
            return Err(Error::ProtocolViolation);
        }

        // Insert in order so that we use them in order where possible.
        if let Err(idx) = self.cids.binary_search_by_key(&entry.seqno, |e| e.seqno) {
            self.cids.insert(idx, entry);
            Ok(())
        } else {
            Err(Error::ProtocolViolation)
        }
    }

    // Retire connection IDs and return the sequence numbers of those that were retired.
    pub fn retire_prior_to(&mut self, retire_prior: u64) -> Vec<u64> {
        let mut retired = Vec::new();
        self.cids.retain(|e| {
            if !e.is_empty() && e.seqno < retire_prior {
                retired.push(e.seqno);
                false
            } else {
                true
            }
        });
        retired
    }
}

impl ConnectionIdStore<()> {
    fn add_local(&mut self, entry: ConnectionIdEntry<()>) {
        self.cids.push(entry);
    }
}

pub struct ConnectionIdDecoderRef<'a> {
    generator: Ref<'a, dyn ConnectionIdGenerator>,
}

impl<'a> Deref for ConnectionIdDecoderRef<'a> {
    type Target = dyn ConnectionIdDecoder + 'a;

    fn deref(&self) -> &Self::Target {
        &*self.generator
    }
}

/// A connection ID manager looks after the generation of connection IDs,
/// the set of connection IDs that are valid for the connection, and the
/// generation of `NEW_CONNECTION_ID` frames.
pub struct ConnectionIdManager {
    /// The `ConnectionIdGenerator` instance that is used to create connection IDs.
    generator: Rc<RefCell<dyn ConnectionIdGenerator>>,
    /// The connection IDs that we will accept.
    /// This includes any we advertise in `NEW_CONNECTION_ID` that haven't been bound to a path
    /// yet. During the handshake at the server, it also includes the randomized DCID pick by
    /// the client.
    connection_ids: ConnectionIdStore<()>,
    /// The maximum number of connection IDs this will accept.  This is at least 2 and won't
    /// be more than `Self::ACTIVE_LIMIT`.
    limit: usize,
    /// The next sequence number that will be used for sending `NEW_CONNECTION_ID` frames.
    next_seqno: u64,
    /// Outstanding, but lost `NEW_CONNECTION_ID` frames will be stored here.
    lost_new_connection_id: Vec<ConnectionIdEntry<Srt>>,
}

impl ConnectionIdManager {
    pub const ACTIVE_LIMIT: usize = 8;

    /// The maximum number of connection IDs that may be pending retirement.
    /// RFC 9000, Section 5.1.2 recommends allowing for at least twice
    /// `active_connection_id_limit`.
    pub const MAX_RETIRE_QUEUE: usize = 2 * Self::ACTIVE_LIMIT;

    /// A special value.  See `ConnectionIdManager::add_odcid`.
    const SEQNO_ODCID: u64 = u64::MAX;

    /// A special value.  See `ConnectionIdEntry::empty_remote`.
    const SEQNO_EMPTY: u64 = u64::MAX - 1;

    pub const SEQNO_PREFERRED: u64 = 1;

    pub fn new(generator: Rc<RefCell<dyn ConnectionIdGenerator>>, initial: ConnectionId) -> Self {
        let mut connection_ids = ConnectionIdStore::default();
        connection_ids.add_local(ConnectionIdEntry::initial_local(initial));
        Self {
            generator,
            connection_ids,
            // A note about initializing the limit to 2.
            // For a server, the number of connection IDs that are tracked at the point that
            // it is first possible to send `NEW_CONNECTION_ID` is 2.  One is the client-generated
            // destination connection (stored with a sequence number of `HANDSHAKE_SEQNO`); the
            // other being the handshake value (seqno 0).  As a result, `NEW_CONNECTION_ID`
            // won't be sent until after the handshake completes, because this initial
            // value remains until the connection completes and transport parameters are handled.
            limit: 2,
            next_seqno: 1,
            lost_new_connection_id: Vec::new(),
        }
    }

    pub fn generator(&self) -> Rc<RefCell<dyn ConnectionIdGenerator>> {
        Rc::clone(&self.generator)
    }

    pub fn decoder(&self) -> ConnectionIdDecoderRef<'_> {
        ConnectionIdDecoderRef {
            generator: self.generator.deref().borrow(),
        }
    }

    fn generates_empty_cids(&self) -> bool {
        self.generator.deref().borrow().generates_empty_cids()
    }

    /// Generate a connection ID and stateless reset token for a preferred address.
    pub fn preferred_address_cid(&mut self) -> Res<(ConnectionId, Srt)> {
        if self.generates_empty_cids() {
            return Err(Error::ConnectionIdsExhausted);
        }
        match self.generator.borrow_mut().generate_cid() {
            Some(cid) => {
                assert_ne!(cid.len(), 0);
                debug_assert_eq!(self.next_seqno, Self::SEQNO_PREFERRED);
                self.connection_ids.add_local(ConnectionIdEntry::new(
                    self.next_seqno,
                    cid.clone(),
                    (),
                ));
                self.next_seqno += 1;
                Ok((cid, Srt::random()))
            }
            None => Err(Error::ConnectionIdsExhausted),
        }
    }

    pub fn is_valid(&self, cid: ConnectionIdRef) -> bool {
        self.connection_ids.contains(cid)
    }

    pub fn retire(&mut self, seqno: u64) -> Res<()> {
        if self.generates_empty_cids() {
            return Err(Error::ProtocolViolation);
        }
        // RFC 9000, Section 19.16: receipt of a RETIRE_CONNECTION_ID frame whose
        // sequence number is greater than any we have issued (i.e. not below the
        // next sequence number to be used) is a connection error.
        if seqno >= self.next_seqno {
            return Err(Error::ProtocolViolation);
        }

        // TODO(mt) - consider keeping connection IDs around for a short while.

        self.connection_ids.retire(seqno);
        self.lost_new_connection_id.retain(|cid| cid.seqno != seqno);
        Ok(())
    }

    /// During the handshake, a server needs to regard the client's choice of destination
    /// connection ID as valid.  This function saves it in the store in a special place.
    /// Note that this is only done *after* an Initial packet from the client is
    /// successfully processed.
    pub fn add_odcid(&mut self, cid: ConnectionId) {
        let entry = ConnectionIdEntry::new(Self::SEQNO_ODCID, cid, ());
        self.connection_ids.add_local(entry);
    }

    /// Stop treating the original destination connection ID as valid.
    pub fn remove_odcid(&mut self) {
        self.connection_ids.retire(Self::SEQNO_ODCID);
    }

    pub fn set_limit(&mut self, limit: u64) {
        debug_assert!(limit >= 2);
        // ACTIVE_LIMIT is usize and we use min, so this fits usize.
        self.limit = expect_usize(min(to_u64(Self::ACTIVE_LIMIT), limit));
    }

    pub fn write_frames<B: Buffer>(
        &mut self,
        builder: &mut packet::Builder<B>,
        tokens: &mut recovery::Tokens,
        stats: &mut FrameStats,
    ) {
        if self.generates_empty_cids() {
            debug_assert_eq!(
                self.generator
                    .borrow_mut()
                    .generate_cid()
                    .expect("OK in debug assert")
                    .len(),
                0
            );
            return;
        }

        while let Some(entry) = self.lost_new_connection_id.pop() {
            if entry.write(builder, stats) {
                tokens.push(recovery::Token::NewConnectionId(Box::new(entry)));
            } else {
                // This shouldn't happen often.
                self.lost_new_connection_id.push(entry);
                break;
            }
        }

        // Keep writing while we have fewer than the limit of active connection IDs
        // and while there is room for more.  This uses the longest connection ID
        // length to simplify (assuming Retire Prior To is just 1 byte).
        while self.connection_ids.len() < self.limit && builder.remaining() >= 47 {
            let maybe_cid = self.generator.borrow_mut().generate_cid();
            if let Some(cid) = maybe_cid {
                assert_ne!(cid.len(), 0);
                let seqno = self.next_seqno;
                self.next_seqno += 1;
                self.connection_ids
                    .add_local(ConnectionIdEntry::new(seqno, cid.clone(), ()));

                // TODO: generate the stateless reset tokens from the connection ID and a key.
                let entry = ConnectionIdEntry::new(seqno, cid, Srt::random());
                entry.write(builder, stats);
                tokens.push(recovery::Token::NewConnectionId(Box::new(entry)));
            } else {
                break;
            }
        }
    }

    pub fn lost(&mut self, entry: &ConnectionIdEntry<Srt>) {
        self.lost_new_connection_id.push(entry.clone());
    }

    pub fn acked(&mut self, entry: &ConnectionIdEntry<Srt>) {
        self.lost_new_connection_id
            .retain(|e| e.seqno != entry.seqno);
    }
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {
    use std::{cell::RefCell, collections::VecDeque, rc::Rc};

    use neqo_common::{Decoder, Encoder};
    use test_fixture::fixture_init;

    use crate::{
        Token as Srt,
        cid::{
            ConnectionId, ConnectionIdDecoder, ConnectionIdEntry, ConnectionIdGenerator,
            ConnectionIdManager, ConnectionIdRef,
        },
        packet, recovery,
        stats::FrameStats,
    };

    #[derive(Default)]
    struct ScriptedConnectionIdGenerator {
        cids: VecDeque<Option<ConnectionId>>,
    }

    impl ConnectionIdDecoder for ScriptedConnectionIdGenerator {
        fn decode_cid<'a>(&self, dec: &mut Decoder<'a>) -> Option<ConnectionIdRef<'a>> {
            dec.decode(1).map(ConnectionIdRef::from)
        }
    }

    impl ConnectionIdGenerator for ScriptedConnectionIdGenerator {
        fn generate_cid(&mut self) -> Option<ConnectionId> {
            // Fail promptly if a write retries after exhaustion instead of hanging the test.
            let Some(cid) = self.cids.pop_front() else {
                panic!("unexpected CID generation attempt");
            };
            cid
        }
    }

    #[test]
    fn cid_generator_exhaustion() {
        fixture_init();
        let generator = Rc::new(RefCell::new(ScriptedConnectionIdGenerator::default()));
        let manager_generator = Rc::clone(&generator);
        let mut manager = ConnectionIdManager::new(manager_generator, ConnectionId::from(&[0]));
        manager.set_limit(3);
        let enc = Encoder::with_capacity(1024);
        let mut builder = packet::Builder::short(enc, false, Some(&[]), 1024);
        let mut tokens = recovery::Tokens::new();
        let mut stats = FrameStats::default();

        // Exhaustion must leave the packet and CID state unchanged, including on later writes.
        for _ in 0..2 {
            generator.borrow_mut().cids.push_back(None);
            let remaining = builder.remaining();
            manager.write_frames(&mut builder, &mut tokens, &mut stats);
            assert_eq!(builder.remaining(), remaining);
            assert!(tokens.is_empty());
            assert_eq!(stats.new_connection_id, 0);
            assert_eq!(manager.connection_ids.len(), 1);
            assert_eq!(manager.next_seqno, 1);
        }

        // A generator can provide more IDs on a later write, then run out partway through.
        generator
            .borrow_mut()
            .cids
            .extend([Some(ConnectionId::from(&[1])), None]);
        manager.write_frames(&mut builder, &mut tokens, &mut stats);
        assert_eq!(tokens.len(), 1);
        assert_eq!(stats.new_connection_id, 1);
        assert_eq!(manager.connection_ids.len(), 2);
        assert_eq!(manager.next_seqno, 2);
        assert!(manager.is_valid(ConnectionIdRef::from(&[1])));

        // Previously issued IDs can still be retransmitted while generation is exhausted.
        let recovery::Token::NewConnectionId(entry) = tokens.pop().unwrap() else {
            panic!("expected a NEW_CONNECTION_ID recovery token");
        };
        manager.lost(&entry);
        generator.borrow_mut().cids.push_back(None);
        manager.write_frames(&mut builder, &mut tokens, &mut stats);
        assert_eq!(tokens.len(), 1);
        assert_eq!(stats.new_connection_id, 2);
        assert_eq!(manager.connection_ids.len(), 2);
        assert_eq!(manager.next_seqno, 2);
        assert!(manager.lost_new_connection_id.is_empty());
        let recovery::Token::NewConnectionId(retransmitted) = tokens.pop().unwrap() else {
            panic!("expected a NEW_CONNECTION_ID recovery token");
        };
        assert_eq!(retransmitted, entry);

        generator
            .borrow_mut()
            .cids
            .push_back(Some(ConnectionId::from(&[2])));
        manager.write_frames(&mut builder, &mut tokens, &mut stats);
        assert_eq!(tokens.len(), 1);
        assert_eq!(stats.new_connection_id, 3);
        assert_eq!(manager.connection_ids.len(), 3);
        assert_eq!(manager.next_seqno, 3);
        assert!(manager.is_valid(ConnectionIdRef::from(&[2])));
        assert!(generator.borrow().cids.is_empty());
    }

    #[test]
    fn generate_initial_cid() {
        fixture_init();
        for _ in 0..100 {
            let cid = ConnectionId::generate_initial();
            assert!(
                matches!(cid.len(), 8..=ConnectionId::MAX_LEN),
                "connection ID length {cid:?}",
            );
        }
    }

    fn new_connection_id_frame_len(entry: &ConnectionIdEntry<Srt>) -> usize {
        1 + Encoder::varint_len(entry.sequence_number())
            + 1
            + 1
            + entry.connection_id().len()
            + Srt::LEN
    }

    /// A write with exactly the right remaining space must succeed.
    #[test]
    fn write_succeeds_with_exact_remaining() {
        fixture_init();
        let entry = ConnectionIdEntry::new(1, ConnectionId::from(&[0xab]), Srt::random());
        let len = new_connection_id_frame_len(&entry);
        // Capacity = len + 1 so that Builder::short (which consumes 1 byte) leaves exactly `len`.
        let enc = Encoder::with_capacity(len + 1);
        let mut builder = packet::Builder::short(enc, false, Some(&[]), len + 1);
        assert_eq!(builder.remaining(), len, "exactly `len` bytes remaining");
        assert!(
            entry.write(&mut builder, &mut FrameStats::default()),
            "write must succeed when remaining == len"
        );
    }

    #[test]
    fn write_checks_length_correctly() {
        fixture_init();
        let entry = ConnectionIdEntry::new(1, ConnectionId::from(&[]), Srt::random());
        let limit = new_connection_id_frame_len(&entry);
        let enc = Encoder::with_capacity(limit);
        let mut builder = packet::Builder::short(enc, false, Some(&[]), limit);
        assert_eq!(
            builder.remaining(),
            limit - 1,
            "Builder::short consumed one byte"
        );
        assert!(
            !entry.write(&mut builder, &mut FrameStats::default()),
            "couldn't write frame into too-short builder",
        );
    }

    #[test]
    fn connection_id_debug_format() {
        let cid = ConnectionId::from(&[0x01, 0x02]);
        assert_eq!(format!("{cid:?}"), "CID [2]: 0102");
    }

    #[test]
    fn connection_id_ref_debug_format() {
        let bytes = [0xde, 0xad];
        let cid_ref = ConnectionIdRef::from(&bytes[..]);
        assert_eq!(format!("{cid_ref:?}"), "CID [2]: dead");
    }

    #[test]
    fn empty_connection_id_generator() {
        use crate::cid::{ConnectionIdGenerator as _, EmptyConnectionIdGenerator};
        let mut g = EmptyConnectionIdGenerator::default();
        assert!(g.generates_empty_cids());
        let cid = g.generate_cid().expect("generates Some");
        assert!(cid.is_empty());
    }

    #[test]
    fn is_stateless_reset_seqno_boundary() {
        fixture_init();
        let srt = Srt::random();
        // Sequence number < 2^62 should match SRT.
        let entry = ConnectionIdEntry::new((1 << 62) - 1, ConnectionId::from(&[1]), srt.clone());
        assert!(entry.is_stateless_reset(&srt));
        // Sequence number >= 2^62 has no valid SRT (should return false).
        let entry_high = ConnectionIdEntry::new(1 << 62, ConnectionId::from(&[1]), srt.clone());
        assert!(!entry_high.is_stateless_reset(&srt));
    }

    #[test]
    fn connection_id_entry_is_empty() {
        fixture_init();
        let srt = Srt::random();
        // SEQNO_EMPTY makes it empty.
        let empty_seqno = ConnectionIdEntry::new(
            ConnectionIdManager::SEQNO_EMPTY,
            ConnectionId::from(&[1]),
            srt.clone(),
        );
        assert!(empty_seqno.is_empty());
        // Empty CID also makes it empty.
        let empty_cid = ConnectionIdEntry::new(42, ConnectionId::from(&[]), srt.clone());
        assert!(empty_cid.is_empty());
        // Non-empty seqno and CID is not empty.
        let non_empty = ConnectionIdEntry::new(1, ConnectionId::from(&[1]), srt);
        assert!(!non_empty.is_empty());
    }

    #[test]
    fn random_cid_generator_empty() {
        use crate::cid::{ConnectionIdGenerator as _, RandomConnectionIdGenerator};
        fixture_init();
        let mut gen_empty = RandomConnectionIdGenerator::new(0);
        assert!(gen_empty.generates_empty_cids());
        let empty_cid = gen_empty.generate_cid().unwrap();
        assert_eq!(empty_cid.len(), 0);
        assert_eq!(empty_cid.to_string(), "");
        let mut gen_nonempty = RandomConnectionIdGenerator::new(8);
        assert!(!gen_nonempty.generates_empty_cids());
        let nonempty_cid = gen_nonempty.generate_cid().unwrap();
        assert_eq!(nonempty_cid.len(), 8);
        assert_eq!(nonempty_cid.to_string().len(), 16); // 8 bytes = 16 hex chars
    }

    #[test]
    fn connection_id_ref_display() {
        use super::ConnectionIdRef;
        let bytes = [0x01, 0x02, 0x03];
        let cid = ConnectionId::from(&bytes);
        let cid_ref = ConnectionIdRef::from(&bytes);
        assert_eq!(cid.to_string(), "010203");
        assert_eq!(cid_ref.to_string(), cid.to_string());
    }
}
