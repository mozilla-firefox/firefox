// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

// This file implements functions necessary for address validation.

#![cfg_attr(
    fuzzing,
    expect(
        clippy::missing_errors_doc,
        clippy::missing_panics_doc,
        clippy::must_use_candidate,
        reason = "`addr_valid` is only public API when fuzzing."
    )
)]

use std::{
    net::{IpAddr, SocketAddr},
    time::{Duration, Instant},
};

use neqo_common::{Buffer, Decoder, Encoder, Role, expect_usize};
use nss::{
    constants::{TLS_AES_128_GCM_SHA256, TLS_VERSION_1_3},
    selfencrypt::SelfEncrypt,
};
use tracing::{info, trace};

use crate::{
    Res,
    cid::ConnectionId,
    frame::{FrameEncoder as _, FrameType},
    packet, recovery,
    stats::FrameStats,
};

/// A prefix we add to Retry tokens to distinguish them from `NEW_TOKEN` tokens.
const TOKEN_IDENTIFIER_RETRY: &[u8] = &[0x52, 0x65, 0x74, 0x72, 0x79];
/// A prefix on `NEW_TOKEN` tokens, that is maximally Hamming distant from `NEW_TOKEN`.
/// Together, these need to have a low probability of collision, even if there is
/// corruption of individual bits in transit.
const TOKEN_IDENTIFIER_NEW_TOKEN: &[u8] = &[0xad, 0x9a, 0x8b, 0x8d, 0x86];

const _: () = assert!(TOKEN_IDENTIFIER_RETRY.len() == TOKEN_IDENTIFIER_NEW_TOKEN.len());

/// The maximum number of tokens we'll save from `NEW_TOKEN` frames.
/// This should be the same as the value of `MAX_TICKETS` in `nss`.
const MAX_NEW_TOKEN: usize = 4;
/// The number of tokens we'll track for the purposes of looking for duplicates.
/// This is based on how many might be received over a period where could be
/// retransmissions.  It should be at least `MAX_NEW_TOKEN`.
const MAX_SAVED_TOKENS: usize = 8;

/// `ValidateAddress` determines what sort of address validation is performed.
/// In short, this determines when a Retry packet is sent.
#[derive(Debug, PartialEq, Eq)]
pub enum ValidateAddress {
    /// Require address validation never.
    Never,
    /// Require address validation unless a `NEW_TOKEN` token is provided.
    NoToken,
    /// Require address validation even if a `NEW_TOKEN` token is provided.
    Always,
}

pub enum AddressValidationResult {
    Pass,
    ValidRetry(ConnectionId),
    Validate,
    Invalid,
}

pub struct AddressValidation {
    /// What sort of validation is performed.
    validation: ValidateAddress,
    /// A self-encryption object used for protecting Retry tokens.
    self_encrypt: SelfEncrypt,
    /// When this object was created.
    start_time: Instant,
}

impl AddressValidation {
    pub fn new(now: Instant, validation: ValidateAddress) -> Res<Self> {
        Ok(Self {
            validation,
            self_encrypt: SelfEncrypt::new(TLS_VERSION_1_3, TLS_AES_128_GCM_SHA256)?,
            start_time: now,
        })
    }

    fn encode_aad(peer_address: SocketAddr, retry: bool) -> Encoder {
        // Let's be "clever" by putting the peer's address in the AAD.
        // We don't need to encode these into the token as they should be
        // available when we need to check the token.
        let mut aad = Encoder::default();
        if retry {
            aad.encode(TOKEN_IDENTIFIER_RETRY);
        } else {
            aad.encode(TOKEN_IDENTIFIER_NEW_TOKEN);
        }
        match peer_address.ip() {
            IpAddr::V4(a) => {
                aad.encode_byte(4);
                aad.encode(a.octets());
            }
            IpAddr::V6(a) => {
                aad.encode_byte(6);
                aad.encode(a.octets());
            }
        }
        if retry {
            aad.encode_uint(2, peer_address.port());
        }
        aad
    }

    pub fn generate_token(
        &self,
        dcid: Option<&ConnectionId>,
        peer_address: SocketAddr,
        now: Instant,
    ) -> Res<Vec<u8>> {
        const EXPIRATION_RETRY: Duration = Duration::from_secs(5);
        const EXPIRATION_NEW_TOKEN: Duration = Duration::from_hours(24);

        // TODO(mt) rotate keys on a fixed schedule.
        let retry = dcid.is_some();
        let mut data = Encoder::default();
        let end = now
            + if retry {
                EXPIRATION_RETRY
            } else {
                EXPIRATION_NEW_TOKEN
            };
        let end_millis = u32::try_from(end.duration_since(self.start_time).as_millis())?;
        data.encode_uint(4, end_millis);
        if let Some(dcid) = dcid {
            data.encode(dcid);
        }

        // Include the token identifier ("Retry"/~) in the AAD, then keep it for plaintext.
        let mut buf = Self::encode_aad(peer_address, retry);
        let encrypted = self.self_encrypt.seal(buf.as_ref(), data.as_ref())?;
        #[cfg(feature = "build-fuzzing-corpus")]
        let mut corpus_data = buf.as_ref()[TOKEN_IDENTIFIER_RETRY.len()..].to_vec();
        buf.truncate(TOKEN_IDENTIFIER_RETRY.len());
        buf.encode(&encrypted);
        let token: Vec<u8> = buf.into();
        #[cfg(feature = "build-fuzzing-corpus")]
        {
            if !retry {
                // Port is not validated for NEW_TOKEN, so use 0 as a placeholder.
                corpus_data.extend_from_slice(&[0, 0]);
            }
            corpus_data.extend_from_slice(&token);
            neqo_common::write_item_to_fuzzing_corpus("addr_valid", &corpus_data);
        }
        Ok(token)
    }

    /// This generates a token for use with Retry.
    pub fn generate_retry_token(
        &self,
        dcid: &ConnectionId,
        peer_address: SocketAddr,
        now: Instant,
    ) -> Res<Vec<u8>> {
        self.generate_token(Some(dcid), peer_address, now)
    }

    /// This generates a token for use with `NEW_TOKEN`.
    pub fn generate_new_token(&self, peer_address: SocketAddr, now: Instant) -> Res<Vec<u8>> {
        self.generate_token(None, peer_address, now)
    }

    pub fn set_validation(&mut self, validation: ValidateAddress) {
        trace!("AddressValidation {self:p}: set to {validation:?}");
        self.validation = validation;
    }

    /// Decrypts `token` and returns the connection ID it contains.
    /// Returns a tuple with a boolean indicating whether this thinks
    /// that the token was a Retry token, and a connection ID, that is
    /// None if the token wasn't successfully decrypted.
    fn decrypt_token(
        &self,
        token: &[u8],
        peer_address: SocketAddr,
        retry: bool,
        now: Instant,
    ) -> Option<ConnectionId> {
        let peer_addr = Self::encode_aad(peer_address, retry);
        let data = self.self_encrypt.open(peer_addr.as_ref(), token).ok()?;
        let mut dec = Decoder::new(&data);
        {
            let d = dec.decode_uint::<u32>()?;
            let end = self.start_time + Duration::from_millis(u64::from(d));
            if end < now {
                trace!("Expired token: {end:?} vs. {now:?}");
                return None;
            }
        }
        Some(ConnectionId::from(dec.decode_remainder()))
    }

    /// Calculate the Hamming difference between our identifier and the target.
    /// Less than one difference per byte indicates that it is likely not a Retry.
    /// This generous interpretation allows for a lot of damage in transit.
    /// Note that if this check fails, then the token will be treated like it came
    /// from `NEW_TOKEN` instead.  If there truly is corruption of packets that causes
    /// validation failure, it will be a failure that we try to recover from.
    fn is_likely_retry(token: &[u8]) -> bool {
        let difference: u32 = token
            .iter()
            .zip(TOKEN_IDENTIFIER_RETRY.iter())
            .map(|(a, b)| (a ^ b).count_ones())
            .sum();
        expect_usize(difference) < TOKEN_IDENTIFIER_RETRY.len()
    }

    pub fn validate(
        &self,
        token: &[u8],
        peer_address: SocketAddr,
        now: Instant,
    ) -> AddressValidationResult {
        trace!("AddressValidation {self:p}: validate {:?}", self.validation);

        if token.is_empty() {
            if self.validation == ValidateAddress::Never {
                info!("AddressValidation: no token; accepting");
                return AddressValidationResult::Pass;
            }
            info!("AddressValidation: no token; validating");
            return AddressValidationResult::Validate;
        }
        if token.len() <= TOKEN_IDENTIFIER_RETRY.len() {
            // Treat bad tokens strictly.
            info!("AddressValidation: too short token");
            return AddressValidationResult::Invalid;
        }
        let retry = Self::is_likely_retry(token);
        let enc = &token[TOKEN_IDENTIFIER_RETRY.len()..];
        // Note that this allows the token identifier part to be corrupted.
        // That's OK here as we don't depend on that being authenticated.
        #[expect(clippy::option_if_let_else, reason = "Alternative is less readable.")]
        match self.decrypt_token(enc, peer_address, retry, now) {
            Some(cid) => {
                if retry {
                    // This is from Retry, so we should have an ODCID >= 8.
                    if cid.len() >= 8 {
                        info!("AddressValidation: valid Retry token for {cid}");
                        AddressValidationResult::ValidRetry(cid)
                    } else {
                        panic!("AddressValidation: Retry token with small CID {cid}");
                    }
                } else if cid.is_empty() {
                    // An empty connection ID means NEW_TOKEN.
                    if self.validation == ValidateAddress::Always {
                        info!("AddressValidation: valid NEW_TOKEN token; validating again");
                        AddressValidationResult::Validate
                    } else {
                        info!("AddressValidation: valid NEW_TOKEN token; accepting");
                        AddressValidationResult::Pass
                    }
                } else {
                    panic!("AddressValidation: NEW_TOKEN token with CID {cid}");
                }
            }
            None => {
                // From here on, we have a token that we couldn't decrypt.
                // We've either lost the keys or we've received junk.
                if retry {
                    // If this looked like a Retry, treat it as being bad.
                    info!("AddressValidation: invalid Retry token; rejecting");
                    AddressValidationResult::Invalid
                } else if self.validation == ValidateAddress::Never {
                    // We don't require validation, so OK.
                    info!("AddressValidation: invalid NEW_TOKEN token; accepting");
                    AddressValidationResult::Pass
                } else {
                    // This might be an invalid NEW_TOKEN token, or a valid one
                    // for which we have since lost the keys.  Check again.
                    info!("AddressValidation: invalid NEW_TOKEN token; validating again");
                    AddressValidationResult::Validate
                }
            }
        }
    }
}

pub enum NewTokenState {
    Client {
        /// Tokens that haven't been taken yet.
        pending: Vec<Vec<u8>>,
        /// Tokens that have been taken, saved so that we can discard duplicates.
        old: Vec<Vec<u8>>,
    },
    Server(NewTokenSender),
}

impl NewTokenState {
    pub fn new(role: Role) -> Self {
        match role {
            Role::Client => Self::Client {
                pending: Vec::new(),
                old: Vec::new(),
            },
            Role::Server => Self::Server(NewTokenSender::default()),
        }
    }

    /// Is there a token available?
    pub const fn has_token(&self) -> bool {
        match self {
            Self::Client { pending, .. } => !pending.is_empty(),
            Self::Server(..) => false,
        }
    }

    /// If this is a client, take a token if there is one.
    /// If this is a server, panic.
    pub fn take_token(&mut self) -> Option<&[u8]> {
        if let Self::Client { pending, old } = self {
            pending.pop().map(|t| {
                if old.len() >= MAX_SAVED_TOKENS {
                    old.remove(0);
                }
                old.push(t);
                old[old.len() - 1].as_slice()
            })
        } else {
            unreachable!();
        }
    }

    /// If this is a client, save a token.
    /// If this is a server, panic.
    pub fn save_token(&mut self, token: Vec<u8>) {
        if let Self::Client { pending, old } = self {
            for t in old.iter().rev().chain(pending.iter().rev()) {
                if t == &token {
                    info!("NewTokenState discarding duplicate NEW_TOKEN");
                    return;
                }
            }

            if pending.len() >= MAX_NEW_TOKEN {
                pending.remove(0);
            }
            pending.push(token);
        } else {
            unreachable!();
        }
    }

    /// If this is a server, maybe send a frame.
    /// If this is a client, do nothing.
    pub fn write_frames<B: Buffer>(
        &mut self,
        builder: &mut packet::Builder<B>,
        tokens: &mut recovery::Tokens,
        stats: &mut FrameStats,
    ) {
        if let Self::Server(sender) = self {
            sender.write_frames(builder, tokens, stats);
        }
    }

    /// If this a server, buffer a `NEW_TOKEN` for sending.
    /// If this is a client, panic.
    pub fn send_new_token(&mut self, token: Vec<u8>) {
        if let Self::Server(sender) = self {
            sender.send_new_token(token);
        } else {
            unreachable!();
        }
    }

    /// If this a server, process a lost signal for a `NEW_TOKEN` frame.
    /// If this is a client, panic.
    pub fn lost(&mut self, seqno: usize) {
        if let Self::Server(sender) = self {
            sender.lost(seqno);
        } else {
            unreachable!();
        }
    }

    /// If this a server, process remove the acknowledged `NEW_TOKEN` frame.
    /// If this is a client, panic.
    pub fn acked(&mut self, seqno: usize) {
        if let Self::Server(sender) = self {
            sender.acked(seqno);
        } else {
            unreachable!();
        }
    }
}

struct NewTokenFrameStatus {
    seqno: usize,
    token: Vec<u8>,
    needs_sending: bool,
}

impl NewTokenFrameStatus {
    const fn len(&self) -> usize {
        1 + Encoder::vvec_len(self.token.len())
    }
}

#[derive(Default)]
pub struct NewTokenSender {
    /// The unacknowledged `NEW_TOKEN` frames we are yet to send.
    tokens: Vec<NewTokenFrameStatus>,
    /// A sequence number that is used to track individual tokens
    /// by reference (so that recovery tokens can be simple).
    next_seqno: usize,
}

impl NewTokenSender {
    /// Add a token to be sent.
    pub fn send_new_token(&mut self, token: Vec<u8>) {
        self.tokens.push(NewTokenFrameStatus {
            seqno: self.next_seqno,
            token,
            needs_sending: true,
        });
        self.next_seqno += 1;
    }

    pub fn write_frames<B: Buffer>(
        &mut self,
        builder: &mut packet::Builder<B>,
        tokens: &mut recovery::Tokens,
        stats: &mut FrameStats,
    ) {
        for t in &mut self.tokens {
            if t.needs_sending && t.len() <= builder.remaining() {
                t.needs_sending = false;

                builder.encode_frame(FrameType::NewToken, |b| {
                    b.encode_vvec(&t.token);
                });

                tokens.push(recovery::Token::NewToken(t.seqno));
                stats.new_token += 1;
            }
        }
    }

    pub fn lost(&mut self, seqno: usize) {
        for t in &mut self.tokens {
            if t.seqno == seqno {
                t.needs_sending = true;
                break;
            }
        }
    }

    pub fn acked(&mut self, seqno: usize) {
        self.tokens.retain(|i| i.seqno != seqno);
    }
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {
    use neqo_common::{Encoder, Role};

    use super::{NewTokenFrameStatus, NewTokenSender, NewTokenState};

    const ONE: &[u8] = &[1, 2, 3];
    const TWO: &[u8] = &[4, 5];

    #[test]
    fn new_token_frame_status_len() {
        let token = vec![1u8, 2, 3];
        let expected = 1 + Encoder::vvec_len(token.len());
        let status = NewTokenFrameStatus {
            seqno: 0,
            token,
            needs_sending: true,
        };
        assert_eq!(status.len(), expected);
    }

    #[test]
    fn new_token_sender_seqno_increments() {
        let mut sender = NewTokenSender::default();
        sender.send_new_token(vec![1]);
        assert_eq!(sender.tokens[0].seqno, 0);
        assert_eq!(sender.next_seqno, 1);
        sender.send_new_token(vec![2]);
        assert_eq!(sender.tokens[1].seqno, 1);
        assert_eq!(sender.next_seqno, 2);
    }

    #[test]
    fn new_token_sender_lost_marks_needs_sending() {
        let mut sender = NewTokenSender::default();
        sender.send_new_token(vec![1]);
        sender.tokens[0].needs_sending = false; // pretend it was sent
        sender.lost(0);
        assert!(
            sender.tokens[0].needs_sending,
            "lost must re-enable needs_sending"
        );
    }

    #[test]
    fn new_token_sender_acked_removes_token() {
        let mut sender = NewTokenSender::default();
        sender.send_new_token(vec![1]);
        sender.send_new_token(vec![2]);
        sender.acked(0);
        assert_eq!(sender.tokens.len(), 1);
        assert_eq!(sender.tokens[0].seqno, 1, "only seqno=1 should remain");
    }

    #[test]
    fn duplicate_saved() {
        let mut tokens = NewTokenState::new(Role::Client);
        tokens.save_token(ONE.to_vec());
        tokens.save_token(TWO.to_vec());
        tokens.save_token(ONE.to_vec());
        assert!(tokens.has_token());
        assert!(tokens.take_token().is_some()); // probably TWO
        assert!(tokens.has_token());
        assert!(tokens.take_token().is_some()); // probably ONE
        assert!(!tokens.has_token());
        assert!(tokens.take_token().is_none());
    }

    #[test]
    fn duplicate_after_take() {
        let mut tokens = NewTokenState::new(Role::Client);
        tokens.save_token(ONE.to_vec());
        tokens.save_token(TWO.to_vec());
        assert!(tokens.has_token());
        assert!(tokens.take_token().is_some()); // probably TWO
        tokens.save_token(ONE.to_vec());
        assert!(tokens.has_token());
        assert!(tokens.take_token().is_some()); // probably ONE
        assert!(!tokens.has_token());
        assert!(tokens.take_token().is_none());
    }

    #[test]
    fn duplicate_after_empty() {
        let mut tokens = NewTokenState::new(Role::Client);
        tokens.save_token(ONE.to_vec());
        tokens.save_token(TWO.to_vec());
        assert!(tokens.has_token());
        assert!(tokens.take_token().is_some()); // probably TWO
        assert!(tokens.has_token());
        assert!(tokens.take_token().is_some()); // probably ONE
        tokens.save_token(ONE.to_vec());
        assert!(!tokens.has_token());
        assert!(tokens.take_token().is_none());
    }
}
