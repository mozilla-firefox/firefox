// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

#![cfg_attr(
    feature = "bench",
    expect(
        clippy::missing_errors_doc,
        clippy::missing_panics_doc,
        clippy::must_use_candidate,
        reason = "These items are only public API when the `bench` feature is enabled."
    )
)]

use std::{
    cell::RefCell,
    cmp::{max, min},
    fmt::{self, Display, Formatter},
    mem,
    ops::Range,
    rc::Rc,
    time::Instant,
};

use enum_map::{Enum as _, EnumMap};
use neqo_common::{
    Buffer, Encoder, Role,
    hex::{Hex, HexSnipMiddle},
    to_u64,
};
pub use nss::Epoch;
use nss::{
    Agent, AntiReplay, Cipher, Error as CryptoError, HandshakeState, Mode, PrivateKey, PublicKey,
    Record, RecordList, RecordProtection as Aead, RecordProtectionOps as _, ResumptionToken,
    SymKey, TLS_AES_128_GCM_SHA256, TLS_AES_256_GCM_SHA384, TLS_CHACHA20_POLY1305_SHA256,
    TLS_CT_HANDSHAKE, TLS_GRP_EC_SECP256R1, TLS_GRP_EC_SECP384R1, TLS_GRP_EC_SECP521R1,
    TLS_GRP_EC_X25519, TLS_GRP_KEM_MLKEM768X25519, TLS_VERSION_1_3, ZeroRttChecker, hkdf, hp,
    random,
};
use tracing::{debug, info, trace};

use crate::{
    ConnectionParameters, Error, Res,
    cid::ConnectionIdRef,
    frame::{FrameEncoder as _, FrameType},
    packet, recovery,
    recv_stream::RxStreamOrderer,
    send_stream::TxBuffer,
    sni::find_sni,
    stats::FrameStats,
    tparams::{TpZeroRttChecker, TransportParameters, TransportParametersHandler},
    tracking::PacketNumberSpace,
    version::Version,
};

/// The number of invocations remaining on a write cipher before we try
/// to update keys.  This has to be much smaller than the number returned
/// by `CryptoDxState::limit` or updates will happen too often.  As we don't
/// need to ask permission to update, this can be quite small.
pub const UPDATE_WRITE_KEYS_AT: packet::Number = 100;

// This is a testing kludge that allows for overwriting the number of
// invocations of the next cipher to operate.  With this, it is possible
// to test what happens when the number of invocations reaches 0, or
// when it hits `UPDATE_WRITE_KEYS_AT` and an automatic update should occur.
// This is a little crude, but it saves a lot of plumbing.
#[cfg(test)]
thread_local!(pub static OVERWRITE_INVOCATIONS: RefCell<Option<packet::Number>> = RefCell::default());

#[derive(Debug)]
pub struct Crypto {
    version: Version,
    protocols: Vec<String>,
    tls: Agent,
    streams: CryptoStreams,
    states: CryptoStates,
    /// Whether the resend-early allowance in Section 6.2.3 of RFC 9002 has been used.
    early_resend_used: bool,
}

type TpHandler = Rc<RefCell<TransportParametersHandler>>;

impl Crypto {
    pub fn new(
        version: Version,
        conn_params: &ConnectionParameters,
        mut agent: Agent,
        protocols: Vec<String>,
        tphandler: TpHandler,
    ) -> Res<Self> {
        agent.set_version_range(TLS_VERSION_1_3, TLS_VERSION_1_3)?;
        agent.set_ciphers(&[
            TLS_AES_128_GCM_SHA256,
            TLS_AES_256_GCM_SHA384,
            TLS_CHACHA20_POLY1305_SHA256,
        ])?;
        agent.set_groups(if conn_params.mlkem_enabled() {
            &[
                TLS_GRP_KEM_MLKEM768X25519,
                TLS_GRP_EC_X25519,
                TLS_GRP_EC_SECP256R1,
                TLS_GRP_EC_SECP384R1,
                TLS_GRP_EC_SECP521R1,
            ]
        } else {
            &[
                TLS_GRP_EC_X25519,
                TLS_GRP_EC_SECP256R1,
                TLS_GRP_EC_SECP384R1,
                TLS_GRP_EC_SECP521R1,
            ]
        })?;
        if let Agent::Client(c) = &mut agent {
            // Configure clients to send additional key shares to reduce the rate of HRRs
            // when enabling MLKEM.
            c.send_additional_key_shares(usize::from(conn_params.mlkem_enabled()))?;

            // Always enable 0-RTT on the client, but the server needs
            // more configuration passed to server_enable_0rtt.
            c.enable_0rtt()?;
        }
        agent.set_alpn(&protocols)?;
        agent.disable_end_of_early_data()?;
        let extension = match version {
            Version::Version2 | Version::Version1 => 0x39,
            #[cfg(feature = "draft-29")]
            Version::Draft29 => 0xffa5,
        };
        agent.extension_handler(extension, tphandler)?;
        Ok(Self {
            version,
            protocols,
            tls: agent,
            streams: CryptoStreams::default(),
            states: CryptoStates::default(),
            early_resend_used: false,
        })
    }

    /// Get the name of the server.  (Only works for the client currently).
    pub fn server_name(&self) -> Option<&str> {
        if let Agent::Client(c) = &self.tls {
            Some(c.server_name())
        } else {
            None
        }
    }

    /// Get the set of enabled protocols.
    pub fn protocols(&self) -> &[String] {
        &self.protocols
    }

    /// Whether the local role is that of a server.
    const fn is_server(&self) -> bool {
        matches!(self.tls, Agent::Server(_))
    }

    pub fn server_enable_0rtt<Z: ZeroRttChecker + 'static>(
        &mut self,
        tphandler: TpHandler,
        anti_replay: &AntiReplay,
        zero_rtt_checker: Z,
    ) -> Res<()> {
        if let Agent::Server(s) = &mut self.tls {
            Ok(s.enable_0rtt(
                anti_replay,
                0xffff_ffff,
                TpZeroRttChecker::wrap(tphandler, zero_rtt_checker),
            )?)
        } else {
            panic!("not a server");
        }
    }

    pub fn server_enable_ech(
        &mut self,
        config: u8,
        public_name: &str,
        sk: &PrivateKey,
        pk: &PublicKey,
    ) -> Res<()> {
        if let Agent::Server(s) = &mut self.tls {
            s.enable_ech(config, public_name, sk, pk)?;
            Ok(())
        } else {
            panic!("not a client");
        }
    }

    pub fn client_enable_ech<A: AsRef<[u8]>>(&mut self, ech_config_list: A) -> Res<()> {
        if let Agent::Client(c) = &mut self.tls {
            c.enable_ech(ech_config_list)?;
            Ok(())
        } else {
            panic!("not a client");
        }
    }

    /// Get the active ECH configuration, which is empty if ECH is disabled.
    pub fn ech_config(&self) -> &[u8] {
        self.tls.ech_config()
    }

    pub fn handshake(
        &mut self,
        now: Instant,
        space: PacketNumberSpace,
        data: Option<&[u8]>,
    ) -> Res<&HandshakeState> {
        let input = data.map(|d| {
            #[cfg(feature = "build-fuzzing-corpus")]
            if space == PacketNumberSpace::Initial && self.is_server() {
                neqo_common::write_item_to_fuzzing_corpus("find_sni", d);
            }
            let rec = Record {
                ct: TLS_CT_HANDSHAKE,
                epoch: space.into(),
                data: d.to_vec(),
            };
            trace!("Handshake record received {rec:?} ");
            rec
        });

        match self.tls.handshake_raw(now, input) {
            Ok(output) => {
                self.buffer_records(output)?;
                Ok(self.tls.state())
            }
            Err(CryptoError::EchRetry(v)) => Err(Error::EchRetry(v)),
            Err(e) => {
                info!("Handshake failed {e:?}");
                Err(self
                    .tls
                    .alert()
                    .map_or(Error::Crypto(e), Error::CryptoAlert))
            }
        }
    }

    /// Enable 0-RTT and return `true` if it is enabled successfully.
    pub fn enable_0rtt(&mut self, version: Version) -> Res<bool> {
        let info = self.tls.preinfo()?;
        // `info.early_data()` returns false for a server,
        // so use `early_data_cipher()` to tell if 0-RTT is enabled.
        let Some(cipher) = info.early_data_cipher() else {
            return Ok(false);
        };
        let (dir, secret) = if self.is_server() {
            (
                CryptoDxDirection::Read,
                self.tls.read_secret(Epoch::ZeroRtt),
            )
        } else {
            (
                CryptoDxDirection::Write,
                self.tls.write_secret(Epoch::ZeroRtt),
            )
        };
        let secret = secret.ok_or(Error::Internal)?;
        self.states.set_0rtt_keys(version, dir, &secret, cipher)?;
        Ok(true)
    }

    pub const fn set_version(&mut self, version: Version) {
        self.version = version;
    }

    /// Lock in a compatible upgrade.
    pub fn confirm_version(&mut self, confirmed: Version) -> Res<()> {
        self.states.confirm_version(self.version, confirmed)?;
        self.version = confirmed;
        Ok(())
    }

    /// Returns true if new handshake keys were installed.
    pub fn install_keys(&mut self) -> Res<bool> {
        if self.tls.state().is_final() {
            Ok(false)
        } else {
            let installed_hs = self.install_handshake_keys()?;
            if self.is_server() {
                self.maybe_install_application_write_key(self.version)?;
            }
            Ok(installed_hs)
        }
    }

    fn install_handshake_keys(&mut self) -> Res<bool> {
        trace!("[{self}] Attempt to install handshake keys");
        let Some(write_secret) = self.tls.write_secret(Epoch::Handshake) else {
            // No keys is fine.
            return Ok(false);
        };
        let read_secret = self
            .tls
            .read_secret(Epoch::Handshake)
            .ok_or(Error::Internal)?;
        let cipher = match self.tls.info() {
            None => self.tls.preinfo()?.cipher_suite(),
            Some(info) => Some(info.cipher_suite()),
        }
        .ok_or(Error::Internal)?;
        self.states
            .set_handshake_keys(self.version, &write_secret, &read_secret, cipher)?;
        debug!("[{self}] Handshake keys installed");
        Ok(true)
    }

    #[must_use]
    pub const fn has_handshake_keys(&self) -> bool {
        self.states.handshake.is_some() || self.states.app_write.is_some()
    }

    fn maybe_install_application_write_key(&mut self, version: Version) -> Res<()> {
        trace!("[{self}] Attempt to install application write key");
        if let Some(secret) = self.tls.write_secret(Epoch::ApplicationData) {
            self.states.set_application_write_key(version, &secret)?;
            debug!("[{self}] Application write key installed");
        }
        Ok(())
    }

    pub fn install_application_keys(&mut self, version: Version, expire_0rtt: Instant) -> Res<()> {
        self.maybe_install_application_write_key(version)?;
        // The write key might have been installed earlier, but it should
        // always be installed now.
        debug_assert!(self.states.app_write.is_some());
        let read_secret = self
            .tls
            .read_secret(Epoch::ApplicationData)
            .ok_or(Error::Internal)?;
        self.states
            .set_application_read_key(version, &read_secret, expire_0rtt)?;
        debug!("[{self}] application read keys installed");
        Ok(())
    }

    /// Buffer crypto records for sending.
    pub fn buffer_records(&mut self, records: RecordList) -> Res<()> {
        for r in records {
            if r.ct != TLS_CT_HANDSHAKE {
                return Err(Error::ProtocolViolation);
            }
            trace!("[{self}] Adding CRYPTO data {r:?}");
            self.streams.send(r.epoch.into(), &r.data)?;
        }
        Ok(())
    }

    pub fn write_frame<B: Buffer>(
        &mut self,
        space: PacketNumberSpace,
        sni_slicing: bool,
        builder: &mut packet::Builder<B>,
        tokens: &mut recovery::Tokens,
        stats: &mut FrameStats,
    ) {
        self.streams
            .write_frame(space, sni_slicing, builder, tokens, stats);
    }

    pub fn acked(&mut self, space: PacketNumberSpace, token: &CryptoRecoveryToken) {
        debug!(
            "Acked crypto frame space={space} offset={} length={}",
            token.offset, token.length
        );
        self.streams.acked(space, token);
    }

    pub fn lost(&mut self, space: PacketNumberSpace, token: &CryptoRecoveryToken) {
        info!(
            "Lost crypto frame space={space} offset={} length={}",
            token.offset, token.length
        );
        self.streams.lost(space, token);
    }

    /// Mark any outstanding frames in the indicated space as "lost" so
    /// that they can be sent again.
    pub fn resend_unacked(&mut self, space: PacketNumberSpace) {
        self.streams.resend_unacked(space);
    }

    /// Resend unACK'ed Initial and Handshake data earlier than PTO, per Section 6.2.3 of RFC 9002.
    /// Does nothing after the first call that finds data, until [`Self::reset_early_resend`].
    pub fn resend_unacked_early(&mut self) {
        if self.early_resend_used {
            return;
        }
        let initial = self.streams.resend_unacked(PacketNumberSpace::Initial);
        let handshake = self.streams.resend_unacked(PacketNumberSpace::Handshake);
        self.early_resend_used = initial || handshake;
        if self.early_resend_used {
            debug!("Resending unACK'ed CRYPTO ahead of the PTO");
        }
    }

    /// A Retry effectively restarts the connection (Section 6.3 of RFC 9002), so re-arm allowance.
    pub const fn reset_early_resend(&mut self) {
        self.early_resend_used = false;
    }

    #[cfg(test)]
    pub const fn early_resend_used(&self) -> bool {
        self.early_resend_used
    }

    /// Discard state for a packet number space and return true
    /// if something was discarded.
    pub fn discard(&mut self, space: PacketNumberSpace) -> bool {
        self.streams.discard(space);
        self.states.discard(space)
    }

    pub fn create_resumption_token(
        &mut self,
        new_token: Option<&[u8]>,
        tps: &TransportParameters,
        version: Version,
        rtt: u64,
    ) -> Option<ResumptionToken> {
        if let Agent::Client(ref mut c) = self.tls {
            c.resumption_token().as_ref().map(|t| {
                trace!("TLS token {}", Hex::new(t.as_ref()));
                let mut enc = Encoder::default();
                enc.encode_uint(4, version.wire_version());
                enc.encode_varint(rtt);
                enc.encode_vvec_with(|enc_inner| {
                    tps.encode(enc_inner);
                });
                enc.encode_vvec(new_token.unwrap_or(&[]));
                enc.encode(t.as_ref());
                debug!("resumption token {}", HexSnipMiddle::new(enc.as_ref()));
                ResumptionToken::new(enc.into(), t.expiration_time())
            })
        } else {
            unreachable!("It is a server");
        }
    }

    pub fn has_resumption_token(&self) -> bool {
        if let Agent::Client(c) = &self.tls {
            c.has_resumption_token()
        } else {
            unreachable!("It is a server");
        }
    }

    #[must_use]
    pub const fn tls_mut(&mut self) -> &mut Agent {
        &mut self.tls
    }

    #[must_use]
    pub const fn tls(&self) -> &Agent {
        &self.tls
    }

    #[must_use]
    pub const fn streams(&self) -> &CryptoStreams {
        &self.streams
    }

    #[must_use]
    pub const fn streams_mut(&mut self) -> &mut CryptoStreams {
        &mut self.streams
    }

    #[must_use]
    pub const fn states(&self) -> &CryptoStates {
        &self.states
    }

    #[must_use]
    pub const fn states_mut(&mut self) -> &mut CryptoStates {
        &mut self.states
    }
}

impl Display for Crypto {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        write!(f, "Crypto")
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CryptoDxDirection {
    Read,
    Write,
}

impl From<CryptoDxDirection> for Mode {
    fn from(dir: CryptoDxDirection) -> Self {
        match dir {
            CryptoDxDirection::Read => Self::Decrypt,
            CryptoDxDirection::Write => Self::Encrypt,
        }
    }
}

#[derive(Debug)]
pub struct CryptoDxState {
    /// The QUIC version.
    version: Version,
    /// Whether packets protected with this state will be read or written.
    direction: CryptoDxDirection,
    /// The epoch of this crypto state.  This initially tracks TLS epochs
    /// via DTLS: 0 = initial, 1 = 0-RTT, 2 = handshake, 3 = application.
    /// But we don't need to keep that, and QUIC isn't limited in how
    /// many times keys can be updated, so we don't use `u16` for this.
    epoch: usize,
    aead: Aead,
    hpkey: hp::Key,
    /// This tracks the range of packet numbers that have been seen.  This allows
    /// for verifying that packet numbers before a key update are strictly lower
    /// than packet numbers after a key update.
    used_pn: Range<packet::Number>,
    /// This is the minimum packet number that is allowed.
    min_pn: packet::Number,
    /// The total number of operations that are remaining before the keys
    /// become exhausted and can't be used any more.
    invocations: packet::Number,
    /// The basis of the invocation limits in `invocations`.
    largest_packet_len: usize,
}

const INITIAL_LARGEST_PACKET_LEN: usize = 1 << 11; // 2048

impl CryptoDxState {
    pub fn new(
        version: Version,
        direction: CryptoDxDirection,
        epoch: Epoch,
        secret: &SymKey,
        cipher: Cipher,
        min_pn: packet::Number,
    ) -> Res<Self> {
        debug!(
            "Making {direction:?} {epoch:?} CryptoDxState, v={version:?} cipher={cipher} min_pn={min_pn}",
        );
        let hplabel = String::from(version.label_prefix()) + "hp";
        Ok(Self {
            version,
            direction,
            epoch: usize::from(epoch),
            aead: Aead::new(
                TLS_VERSION_1_3,
                cipher,
                secret,
                version.label_prefix(),
                Mode::from(direction),
            )?,
            hpkey: hp::Key::extract(TLS_VERSION_1_3, cipher, secret, &hplabel)?,
            used_pn: min_pn..min_pn,
            min_pn,
            invocations: Self::limit(direction, cipher),
            largest_packet_len: INITIAL_LARGEST_PACKET_LEN,
        })
    }

    pub fn new_initial(
        version: Version,
        direction: CryptoDxDirection,
        label: &str,
        dcid: &[u8],
        min_pn: packet::Number,
    ) -> Res<Self> {
        trace!("new_initial {version:?} {}", ConnectionIdRef::from(dcid));
        let salt = version.initial_salt();
        let cipher = TLS_AES_128_GCM_SHA256;
        let initial_secret = hkdf::extract(
            TLS_VERSION_1_3,
            cipher,
            Some(&hkdf::import_key(TLS_VERSION_1_3, salt)?),
            &hkdf::import_key(TLS_VERSION_1_3, dcid)?,
        )?;

        let secret = hkdf::expand_label(TLS_VERSION_1_3, cipher, &initial_secret, &[], label)?;

        Self::new(version, direction, Epoch::Initial, &secret, cipher, min_pn)
    }

    /// Determine the confidentiality and integrity limits for the cipher.
    fn limit(direction: CryptoDxDirection, cipher: Cipher) -> packet::Number {
        match direction {
            // This uses the smaller limits for 2^16 byte packets
            // as we don't control incoming packet size.
            CryptoDxDirection::Read => match cipher {
                TLS_AES_128_GCM_SHA256 => 1 << 52,
                TLS_AES_256_GCM_SHA384 => packet::Number::MAX,
                TLS_CHACHA20_POLY1305_SHA256 => 1 << 36,
                _ => unreachable!(),
            },
            // This uses the larger limits for 2^11 byte packets.
            CryptoDxDirection::Write => match cipher {
                TLS_AES_128_GCM_SHA256 | TLS_AES_256_GCM_SHA384 => 1 << 28,
                TLS_CHACHA20_POLY1305_SHA256 => packet::Number::MAX,
                _ => unreachable!(),
            },
        }
    }

    fn invoked(&mut self) -> Res<()> {
        #[cfg(test)]
        if let Some(i) = OVERWRITE_INVOCATIONS.take() {
            tracing::warn!("Setting {:?} invocations to {i}", self.direction);
            self.invocations = i;
        }
        self.invocations = self
            .invocations
            .checked_sub(1)
            .ok_or(Error::KeysExhausted)?;
        Ok(())
    }

    /// Determine whether we should initiate a key update.
    pub fn should_update(&self) -> bool {
        // There is no point in updating read keys as the limit is global.
        debug_assert_eq!(self.direction, CryptoDxDirection::Write);
        self.invocations <= UPDATE_WRITE_KEYS_AT
    }

    pub fn next(&self, next_secret: &SymKey, cipher: Cipher) -> Res<Self> {
        let pn = self.next_pn();
        // We count invocations of each write key just for that key, but all
        // attempts to invocations to read count toward a single limit.
        // This doesn't count use of Handshake keys.
        let invocations = if self.direction == CryptoDxDirection::Read {
            self.invocations
        } else {
            Self::limit(CryptoDxDirection::Write, cipher)
        };
        Ok(Self {
            version: self.version,
            direction: self.direction,
            epoch: self.epoch + 1,
            aead: Aead::new(
                TLS_VERSION_1_3,
                cipher,
                next_secret,
                self.version.label_prefix(),
                Mode::from(self.direction),
            )?,
            hpkey: self.hpkey.try_clone()?,
            used_pn: pn..pn,
            min_pn: pn,
            invocations,
            largest_packet_len: INITIAL_LARGEST_PACKET_LEN,
        })
    }

    #[must_use]
    pub const fn version(&self) -> Version {
        self.version
    }

    #[must_use]
    pub const fn key_phase(&self) -> bool {
        // Epoch 3 => 0, 4 => 1, 5 => 0, 6 => 1, ...
        self.epoch & 1 != 1
    }

    #[must_use]
    pub const fn epoch(&self) -> usize {
        self.epoch
    }

    /// This is a continuation of a previous, so adjust the range accordingly.
    /// Fail if the two ranges overlap.  Do nothing if the directions don't match.
    pub fn continuation(&mut self, prev: &Self) -> Res<()> {
        debug_assert_eq!(self.direction, prev.direction);
        let next = prev.next_pn();
        self.min_pn = next;
        if self.used_pn.is_empty() {
            self.used_pn = next..next;
            Ok(())
        } else if prev.used_pn.end > self.used_pn.start {
            debug!(
                "[{self}] Found packet with too new packet number {} > {}, compared to {prev}",
                self.used_pn.start, prev.used_pn.end,
            );
            Err(Error::PacketNumberOverlap)
        } else {
            self.used_pn.start = next;
            Ok(())
        }
    }

    /// Mark a packet number as used.  If this is too low, reject it.
    /// Note that this won't catch a value that is too high if packets protected with
    /// old keys are received after a key update.  That needs to be caught elsewhere.
    pub fn used(&mut self, pn: packet::Number) -> Res<()> {
        if pn < self.min_pn {
            debug!(
                "[{self}] Found packet with too old packet number: {pn} < {}",
                self.min_pn
            );
            return Err(Error::PacketNumberOverlap);
        }
        if self.used_pn.start == self.used_pn.end {
            self.used_pn.start = pn;
        }
        self.used_pn.end = max(pn + 1, self.used_pn.end);
        Ok(())
    }

    #[must_use]
    pub fn needs_update(&self) -> bool {
        // Only initiate a key update if we have processed exactly one packet
        // and we are in an epoch greater than 3.
        self.used_pn.start + 1 == self.used_pn.end
            && self.epoch > usize::from(Epoch::ApplicationData)
    }

    #[must_use]
    pub fn can_update(&self, largest_acknowledged: Option<packet::Number>) -> bool {
        largest_acknowledged.map_or_else(
            || self.epoch == usize::from(Epoch::ApplicationData),
            |la| self.used_pn.contains(&la),
        )
    }

    pub fn compute_mask(
        &self,
        sample: &[u8; hp::Key::SAMPLE_SIZE],
    ) -> Res<[u8; hp::Key::SAMPLE_SIZE]> {
        let mask = self.hpkey.mask(sample)?;
        trace!(
            "[{self}] HP sample={} mask={}",
            Hex::new(sample),
            Hex::new(mask)
        );
        Ok(mask)
    }

    #[must_use]
    pub const fn next_pn(&self) -> packet::Number {
        self.used_pn.end
    }

    pub fn encrypt(
        &mut self,
        pn: packet::Number,
        hdr: Range<usize>,
        data: &mut [u8],
    ) -> Res<usize> {
        debug_assert_eq!(self.direction, CryptoDxDirection::Write);
        trace!(
            "[{self}] encrypt_in_place pn={pn} hdr={} body={}",
            Hex::new(data[hdr.clone()].as_ref()),
            Hex::new(data[hdr.end..].as_ref())
        );

        // The numbers in `Self::limit` assume a maximum packet size of `LIMIT`.
        // Adjust them as we encounter larger packets.
        let body_len = data.len() - hdr.len() - self.aead.expansion();
        debug_assert!(body_len <= u16::MAX.into());
        if body_len > self.largest_packet_len {
            let new_bits = usize::leading_zeros(self.largest_packet_len - 1)
                - usize::leading_zeros(body_len - 1);
            self.invocations >>= new_bits;
            self.largest_packet_len = body_len;
        }
        self.invoked()?;

        let (prev, data) = data.split_at_mut(hdr.end);
        // `prev` may have already-encrypted packets this one is being coalesced with.
        // Use only the actual current header for AAD.
        let len = self.aead.encrypt_in_place(pn, &prev[hdr], data)?;

        trace!("[{self}] encrypt ct={}", Hex::new(&data[..len]));
        debug_assert_eq!(pn, self.next_pn());
        self.used(pn)?;
        Ok(len)
    }

    #[must_use]
    pub fn expansion(&self) -> usize {
        self.aead.expansion()
    }

    pub fn decrypt(
        &mut self,
        pn: packet::Number,
        hdr: Range<usize>,
        data: &mut [u8],
    ) -> Res<usize> {
        debug_assert_eq!(self.direction, CryptoDxDirection::Read);
        trace!(
            "[{self}] decrypt_in_place pn={pn} hdr={} body={}",
            Hex::new(data[hdr.clone()].as_ref()),
            Hex::new(data[hdr.end..].as_ref())
        );
        self.invoked()?;
        let (hdr, data) = data.split_at_mut(hdr.end);
        let len = self.aead.decrypt_in_place(pn, hdr, data)?;
        self.used(pn)?;
        Ok(len)
    }

    #[cfg(all(not(feature = "disable-encryption"), any(test, feature = "bench")))]
    pub fn test_default_write() -> Self {
        Self::test_default_with_direction(CryptoDxDirection::Write)
    }

    #[cfg(all(not(feature = "disable-encryption"), any(test, feature = "bench")))]
    pub fn test_default_read() -> Self {
        Self::test_default_with_direction(CryptoDxDirection::Read)
    }

    #[cfg(all(not(feature = "disable-encryption"), any(test, feature = "bench")))]
    fn test_default_with_direction(direction: CryptoDxDirection) -> Self {
        // This matches the value in packet.rs
        const CLIENT_CID: &[u8] = &[0x83, 0x94, 0xc8, 0xf0, 0x3e, 0x51, 0x57, 0x08];
        Self::new_initial(Version::default(), direction, "server in", CLIENT_CID, 0)
            .expect("state created")
    }

    /// Get the amount of extra padding packets protected with this profile need.
    /// This is the difference between the size of the header protection sample
    /// and the AEAD expansion.
    pub fn extra_padding(&self) -> usize {
        hp::Key::SAMPLE_SIZE.saturating_sub(self.expansion())
    }
}

impl Display for CryptoDxState {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        write!(f, "epoch {} {:?}", self.epoch, self.direction)
    }
}

#[derive(Debug)]
pub struct CryptoState {
    tx: CryptoDxState,
    rx: CryptoDxState,
}

/// `CryptoDxAppData` wraps the state necessary for one direction of application data keys.
/// This includes the secret needed to generate the next set of keys.
#[derive(Debug)]
pub struct CryptoDxAppData {
    dx: CryptoDxState,
    cipher: Cipher,
    // Not the secret used to create `self.dx`, but the one needed for the next iteration.
    next_secret: SymKey,
}

impl CryptoDxAppData {
    pub fn new(
        version: Version,
        dir: CryptoDxDirection,
        secret: &SymKey,
        cipher: Cipher,
    ) -> Res<Self> {
        Ok(Self {
            dx: CryptoDxState::new(version, dir, Epoch::ApplicationData, secret, cipher, 0)?,
            cipher,
            next_secret: Self::update_secret(version, cipher, secret)?,
        })
    }

    fn update_secret(version: Version, cipher: Cipher, secret: &SymKey) -> Res<SymKey> {
        let label = String::from(version.label_prefix()) + "ku";
        let next = hkdf::expand_label(TLS_VERSION_1_3, cipher, secret, &[], &label)?;
        Ok(next)
    }

    pub fn next(&self) -> Res<Self> {
        if self.dx.epoch == usize::MAX {
            // Guard against too many key updates.
            return Err(Error::KeysExhausted);
        }
        let next_secret = Self::update_secret(self.dx.version, self.cipher, &self.next_secret)?;
        Ok(Self {
            dx: self.dx.next(&self.next_secret, self.cipher)?,
            cipher: self.cipher,
            next_secret,
        })
    }

    pub const fn epoch(&self) -> usize {
        self.dx.epoch
    }
}

/// All of the keying material needed for a connection.
///
/// Note that the methods on this struct take a version but those are only ever
/// used for Initial keys; a version has been selected at the time we need to
/// get other keys, so those have fixed versions.
#[derive(Debug, Default)]
pub struct CryptoStates {
    initials: EnumMap<Version, Option<CryptoState>>,
    handshake: Option<CryptoState>,
    zero_rtt: Option<CryptoDxState>, // One direction only!
    cipher: Cipher,
    app_write: Option<CryptoDxAppData>,
    app_read: Option<CryptoDxAppData>,
    app_read_next: Option<CryptoDxAppData>,
    // If this is set, then we have noticed a genuine update.
    // Once this time passes, we should switch in new keys.
    read_update_time: Option<Instant>,
    // The read epoch of the most recent key update we have responded to.
    // A duplicate of the packet that carried an update decrypts with the same
    // keys and would otherwise be detected as that update again; comparing
    // against this stops us from responding to the same update twice.
    read_update_epoch: Option<usize>,
}

impl CryptoStates {
    fn initials_is_empty(&self) -> bool {
        self.initials.values().flatten().count() == 0
    }

    /// Select a `CryptoDxState` and `CryptoSpace` for the given `PacketNumberSpace`.
    /// This selects 0-RTT keys for `PacketNumberSpace::ApplicationData` if 1-RTT keys are
    /// not yet available.
    pub fn select_tx_mut(
        &mut self,
        version: Version,
        space: PacketNumberSpace,
    ) -> Option<(Epoch, &mut CryptoDxState)> {
        match space {
            PacketNumberSpace::Initial => self
                .tx_mut(version, Epoch::Initial)
                .map(|dx| (Epoch::Initial, dx)),
            PacketNumberSpace::Handshake => self
                .tx_mut(version, Epoch::Handshake)
                .map(|dx| (Epoch::Handshake, dx)),
            PacketNumberSpace::ApplicationData => {
                if let Some(app) = self.app_write.as_mut() {
                    Some((Epoch::ApplicationData, &mut app.dx))
                } else {
                    self.zero_rtt.as_mut().map(|dx| (Epoch::ZeroRtt, dx))
                }
            }
        }
    }

    pub fn tx_mut<'a>(
        &'a mut self,
        version: Version,
        epoch: Epoch,
    ) -> Option<&'a mut CryptoDxState> {
        let tx = |k: Option<&'a mut CryptoState>| k.map(|dx| &mut dx.tx);
        match epoch {
            Epoch::Initial => tx(self.initials[version].as_mut()),
            Epoch::ZeroRtt => self
                .zero_rtt
                .as_mut()
                .filter(|z| z.direction == CryptoDxDirection::Write),
            Epoch::Handshake => tx(self.handshake.as_mut()),
            Epoch::ApplicationData => self.app_write.as_mut().map(|app| &mut app.dx),
        }
    }

    pub fn tx<'a>(&'a self, version: Version, epoch: Epoch) -> Option<&'a CryptoDxState> {
        let tx = |k: Option<&'a CryptoState>| k.map(|dx| &dx.tx);
        match epoch {
            Epoch::Initial => tx(self.initials[version].as_ref()),
            Epoch::ZeroRtt => self
                .zero_rtt
                .as_ref()
                .filter(|z| z.direction == CryptoDxDirection::Write),
            Epoch::Handshake => tx(self.handshake.as_ref()),
            Epoch::ApplicationData => self.app_write.as_ref().map(|app| &app.dx),
        }
    }

    pub fn select_tx(
        &self,
        version: Version,
        space: PacketNumberSpace,
    ) -> Option<(Epoch, &CryptoDxState)> {
        match space {
            PacketNumberSpace::Initial => self
                .tx(version, Epoch::Initial)
                .map(|dx| (Epoch::Initial, dx)),
            PacketNumberSpace::Handshake => self
                .tx(version, Epoch::Handshake)
                .map(|dx| (Epoch::Handshake, dx)),
            PacketNumberSpace::ApplicationData => self.app_write.as_ref().map_or_else(
                || self.zero_rtt.as_ref().map(|dx| (Epoch::ZeroRtt, dx)),
                |app| Some((Epoch::ApplicationData, &app.dx)),
            ),
        }
    }

    /// When decrypting Initial packets, there are potentially multiple active versions.
    /// The `used_pn` range tracks what has been received on the version that was used.
    /// But if the version changes, the version we select might have a value of 0,
    /// rather than the actual value, which can cause packet number recovery to fail.
    /// To avoid that, have the indicated `version` continue from the previous version.
    /// This only needs to be run once, so run it when getting header protection.
    fn maybe_continue_initial_rx(&mut self, version: Version) {
        // Only do this if this version hasn't been used...
        if self.initials[version]
            .as_ref()
            .is_none_or(|dx| dx.rx.next_pn() != 0)
        {
            return;
        }
        // ... and some other version has been.
        // This assumes that there is just one other version in use,
        // as the spec requires.
        let Some(other) = self
            .initials
            .iter()
            .find_map(|(k, v)| v.as_ref().is_some_and(|z| z.rx.next_pn() > 0).then_some(k))
        else {
            return;
        };
        debug_assert_ne!(version, other);

        // This uses the take-modify-restore pattern to avoid
        // having the borrow checker complain.
        // It *ignores* errors from the `continuation()`
        // so that the restore step isn't skipped.
        //
        // This doesn't need to be full anti-replay.
        // Each version has separate keys, so nonce reuse is OK.
        // After this, we might reject packets if the peer
        // does reuse nonces, but they aren't allowed to do that.
        //
        // Note: these `if let Some(...)` conditions are always true.
        if let Some(mut next) = self.initials[version].take() {
            if let Some(prev) = &self.initials[other] {
                _ = next.rx.continuation(&prev.rx);
            }
            self.initials[version] = Some(next);
        }
    }

    pub fn rx_hp(&mut self, version: Version, epoch: Epoch) -> Option<&mut CryptoDxState> {
        match epoch {
            Epoch::ApplicationData => self.app_read.as_mut().map(|ar| &mut ar.dx),
            Epoch::Initial => {
                self.maybe_continue_initial_rx(version);
                self.rx(version, epoch, false)
            }
            _ => self.rx(version, epoch, false),
        }
    }

    pub fn rx<'a>(
        &'a mut self,
        version: Version,
        epoch: Epoch,
        key_phase: bool,
    ) -> Option<&'a mut CryptoDxState> {
        let rx = |x: Option<&'a mut CryptoState>| x.map(|dx| &mut dx.rx);
        match epoch {
            Epoch::Initial => rx(self.initials[version].as_mut()),
            Epoch::ZeroRtt => self
                .zero_rtt
                .as_mut()
                .filter(|z| z.direction == CryptoDxDirection::Read),
            Epoch::Handshake => rx(self.handshake.as_mut()),
            Epoch::ApplicationData => {
                let f = |a: Option<&'a mut CryptoDxAppData>| {
                    a.filter(|ar| ar.dx.key_phase() == key_phase)
                };
                // XOR to reduce the leakage about which key is chosen.
                f(self.app_read.as_mut())
                    .xor(f(self.app_read_next.as_mut()))
                    .map(|ar| &mut ar.dx)
            }
        }
    }

    /// Whether keys for processing packets in the indicated space are pending.
    /// This allows the caller to determine whether to save a packet for later
    /// when keys are not available.
    /// NOTE: 0-RTT keys are not considered here.  The expectation is that a
    /// server will have to save 0-RTT packets in a different place.  Though it
    /// is possible to attribute 0-RTT packets to an existing connection if there
    /// is a multi-packet Initial, that is an unusual circumstance, so we
    /// don't do caching for that in those places that call this function.
    pub fn rx_pending(&self, space: Epoch) -> bool {
        match space {
            Epoch::Initial | Epoch::ZeroRtt => false,
            Epoch::Handshake => self.handshake.is_none() && !self.initials_is_empty(),
            Epoch::ApplicationData => self.app_read.is_none(),
        }
    }

    /// Create the initial crypto state.
    /// Note that the version here can change and that's OK.
    pub fn init<'v, V>(
        &mut self,
        versions: V,
        role: Role,
        dcid: &[u8],
        randomize_first_pn: bool,
    ) -> Res<()>
    where
        V: IntoIterator<Item = &'v Version>,
    {
        const CLIENT_INITIAL_LABEL: &str = "client in";
        const SERVER_INITIAL_LABEL: &str = "server in";

        let (write, read) = match role {
            Role::Client => (CLIENT_INITIAL_LABEL, SERVER_INITIAL_LABEL),
            Role::Server => (SERVER_INITIAL_LABEL, CLIENT_INITIAL_LABEL),
        };

        let min_pn = if randomize_first_pn {
            let r = random::<2>();
            // A random starting packet number that is mostly less than 64,
            // but can go as high as 1024, in three parts:
            // - A value from 0..31.
            // - A value from 0..1024 in steps of 32, but only one time in eight.
            // - An extra 1, just to ensure that the result is always non-zero.
            packet::Number::from(r[0] & 0x1f)
                + (packet::Number::from(r[1].saturating_sub(224)) << 5)
                + 1
        } else {
            0
        };

        for v in versions {
            debug!(
                "[{self}] Creating initial cipher state v={v:?}, role={role:?} dcid={}",
                Hex::new(dcid)
            );

            let mut initial = CryptoState {
                tx: CryptoDxState::new_initial(*v, CryptoDxDirection::Write, write, dcid, min_pn)?,
                rx: CryptoDxState::new_initial(*v, CryptoDxDirection::Read, read, dcid, 0)?,
            };
            if let Some(prev) = &self.initials[*v] {
                info!(
                    "[{self}] Continue packet numbers for initial after retry (write is {:?})",
                    prev.rx.used_pn,
                );
                initial.rx.continuation(&prev.rx)?;
                initial.tx.continuation(&prev.tx)?;
            }
            self.initials[*v] = Some(initial);
        }
        Ok(())
    }

    /// At a server, we can be more targeted in initializing.
    /// Initialize on demand: either to decrypt Initial packets that we receive
    /// or after a version has been selected.
    /// This is maybe slightly inefficient in the first case, because we might
    /// not need the send keys if the packet is subsequently discarded, but
    /// the overall effort is small enough to write off.
    pub fn init_server(
        &mut self,
        version: Version,
        dcid: &[u8],
        randomize_first_pn: bool,
    ) -> Res<()> {
        if self.initials[version].is_none() {
            self.init(&[version], Role::Server, dcid, randomize_first_pn)?;
        }
        Ok(())
    }

    pub fn confirm_version(&mut self, orig: Version, confirmed: Version) -> Res<()> {
        if orig != confirmed {
            // Note that on the server, we might not have initials for |orig| if it
            // was configured for |orig| and only |confirmed| Initial packets arrived.
            if let Ok([Some(prev), next]) = self
                .initials
                .as_mut_slice()
                .get_disjoint_mut([orig.into_usize(), confirmed.into_usize()])
            {
                let next = next.as_mut().ok_or(Error::VersionNegotiation)?;
                next.rx.continuation(&prev.rx)?;
                next.tx.continuation(&prev.tx)?;
            }
        }
        Ok(())
    }

    pub fn set_0rtt_keys(
        &mut self,
        version: Version,
        dir: CryptoDxDirection,
        secret: &SymKey,
        cipher: Cipher,
    ) -> Res<()> {
        trace!("[{self}] install 0-RTT keys");
        self.zero_rtt = Some(CryptoDxState::new(
            version,
            dir,
            Epoch::ZeroRtt,
            secret,
            cipher,
            0,
        )?);
        Ok(())
    }

    /// Discard keys and return true if that happened.
    pub fn discard(&mut self, space: PacketNumberSpace) -> bool {
        match space {
            PacketNumberSpace::Initial => {
                let empty = self.initials_is_empty();
                self.initials.clear();
                !empty
            }
            PacketNumberSpace::Handshake => self.handshake.take().is_some(),
            PacketNumberSpace::ApplicationData => panic!("Can't drop application data keys"),
        }
    }

    pub fn discard_0rtt_keys(&mut self) {
        trace!("[{self}] discard 0-RTT keys");
        assert!(
            self.app_read.is_none(),
            "Can't discard 0-RTT after setting application keys"
        );
        self.zero_rtt = None;
    }

    pub fn set_handshake_keys(
        &mut self,
        version: Version,
        write_secret: &SymKey,
        read_secret: &SymKey,
        cipher: Cipher,
    ) -> Res<()> {
        self.cipher = cipher;
        self.handshake = Some(CryptoState {
            tx: CryptoDxState::new(
                version,
                CryptoDxDirection::Write,
                Epoch::Handshake,
                write_secret,
                cipher,
                0,
            )?,
            rx: CryptoDxState::new(
                version,
                CryptoDxDirection::Read,
                Epoch::Handshake,
                read_secret,
                cipher,
                0,
            )?,
        });
        Ok(())
    }

    pub fn set_application_write_key(&mut self, version: Version, secret: &SymKey) -> Res<()> {
        debug_assert!(self.app_write.is_none());
        debug_assert_ne!(self.cipher, 0);
        let mut app = CryptoDxAppData::new(version, CryptoDxDirection::Write, secret, self.cipher)?;
        if let Some(z) = &self.zero_rtt
            && z.direction == CryptoDxDirection::Write
        {
            app.dx.continuation(z)?;
        }
        self.zero_rtt = None;
        self.app_write = Some(app);
        Ok(())
    }

    pub fn set_application_read_key(
        &mut self,
        version: Version,
        secret: &SymKey,
        expire_0rtt: Instant,
    ) -> Res<()> {
        debug_assert!(self.app_write.is_some(), "should have write keys installed");
        debug_assert!(self.app_read.is_none());
        let mut app = CryptoDxAppData::new(version, CryptoDxDirection::Read, secret, self.cipher)?;
        if let Some(z) = &self.zero_rtt {
            if z.direction == CryptoDxDirection::Read {
                app.dx.continuation(z)?;
            }
            self.read_update_time = Some(expire_0rtt);
        }
        self.app_read_next = Some(app.next()?);
        self.app_read = Some(app);
        Ok(())
    }

    /// Update the write keys.
    pub fn initiate_key_update(&mut self, largest_acknowledged: Option<packet::Number>) -> Res<()> {
        // Only update if we are able to. We can only do this if we have
        // received an acknowledgement for a packet in the current phase.
        // Also, skip this if we are waiting for read keys on the existing
        // key update to be rolled over.
        let write = &self.app_write.as_ref().ok_or(Error::Internal)?.dx;
        if write.can_update(largest_acknowledged) && self.read_update_time.is_none() {
            // This call additionally checks that we don't advance to the next
            // epoch while a key update is in progress.
            if self.maybe_update_write()? {
                Ok(())
            } else {
                debug!("[{self}] Write keys already updated");
                Err(Error::KeyUpdateBlocked)
            }
        } else {
            debug!("[{self}] Waiting for ACK or blocked on read key timer");
            Err(Error::KeyUpdateBlocked)
        }
    }

    /// Try to update, and return true if it happened.
    fn maybe_update_write(&mut self) -> Res<bool> {
        // Update write keys.  But only do so if the write keys are not already
        // ahead of the read keys.  If we initiated the key update, the write keys
        // will already be ahead.
        debug_assert!(self.read_update_time.is_none());
        let write = &self.app_write.as_ref().ok_or(Error::Internal)?;
        let read = &self.app_read.as_ref().ok_or(Error::Internal)?;
        if write.epoch() == read.epoch() {
            debug!("[{self}] Update write keys to epoch={}", write.epoch() + 1);
            self.app_write = Some(write.next()?);
            Ok(true)
        } else {
            Ok(false)
        }
    }

    /// Check whether write keys are close to running out of invocations.
    /// If that is close, update them if possible.  Failing to update at
    /// this stage is cause for a fatal error.
    pub fn auto_update(&mut self) -> Res<()> {
        if let Some(app_write) = self.app_write.as_ref()
            && app_write.dx.should_update()
        {
            info!("[{self}] Initiating automatic key update");
            if !self.maybe_update_write()? {
                return Err(Error::KeysExhausted);
            }
        }
        Ok(())
    }

    fn has_0rtt_read(&self) -> bool {
        self.zero_rtt
            .as_ref()
            .is_some_and(|z| z.direction == CryptoDxDirection::Read)
    }

    /// Prepare to update read keys.  This doesn't happen immediately as
    /// we want to ensure that we can continue to receive any delayed
    /// packets that use the old keys.  So we just set a timer.
    pub fn key_update_received(&mut self, epoch: usize, expiration: Instant) -> Res<()> {
        // Respond to each key update once. A duplicate of the packet that
        // carried the update decrypts with the same keys and is detected as
        // the same update again; responding twice would advance the write keys
        // a second time and trip an assertion in `maybe_update_write`.
        if self.read_update_epoch.is_some_and(|e| epoch <= e) {
            trace!("[{self}] Ignoring duplicate key update for epoch {epoch}");
            return Ok(());
        }
        self.read_update_epoch = Some(epoch);

        trace!("[{self}] Key update received");
        // If we received a key update, then we assume that the peer has
        // acknowledged a packet we sent in this epoch. It's OK to do that
        // because they aren't allowed to update without first having received
        // something from us. If the ACK isn't in the packet that triggered this
        // key update, it must be in some other packet they have sent.
        _ = self.maybe_update_write()?;

        // We shouldn't have 0-RTT keys at this point, but if we do, dump them.
        debug_assert_eq!(self.read_update_time.is_some(), self.has_0rtt_read());
        if self.has_0rtt_read() {
            self.zero_rtt = None;
        }
        self.read_update_time = Some(expiration);
        Ok(())
    }

    #[must_use]
    pub const fn update_time(&self) -> Option<Instant> {
        self.read_update_time
    }

    /// Check if time has passed for updating key update parameters.
    /// If it has, then swap keys over and allow more key updates to be initiated.
    /// This is also used to discard 0-RTT read keys at the server in the same way.
    pub fn check_key_update(&mut self, now: Instant) -> Res<()> {
        if let Some(expiry) = self.read_update_time {
            // If enough time has passed, then install new keys and clear the timer.
            if now >= expiry {
                if self.has_0rtt_read() {
                    trace!("[{self}] Discarding 0-RTT keys");
                    self.zero_rtt = None;
                } else {
                    trace!("[{self}] Rotating read keys");
                    mem::swap(&mut self.app_read, &mut self.app_read_next);
                    self.app_read_next =
                        Some(self.app_read.as_ref().ok_or(Error::Internal)?.next()?);
                }
                self.read_update_time = None;
            }
        }
        Ok(())
    }

    /// Get the current/highest epoch.  This returns (write, read) epochs.
    #[cfg(test)]
    pub fn get_epochs(&self) -> (Option<usize>, Option<usize>) {
        let to_epoch = |app: &Option<CryptoDxAppData>| app.as_ref().map(|a| a.dx.epoch);
        (to_epoch(&self.app_write), to_epoch(&self.app_read))
    }

    /// While we are awaiting the completion of a key update, we might receive
    /// valid packets that are protected with old keys. We need to ensure that
    /// these don't carry packet numbers higher than those in packets protected
    /// with the newer keys.  To ensure that, this is called after every decryption.
    pub fn check_pn_overlap(&mut self) -> Res<()> {
        // We only need to do the check while we are waiting for read keys to be updated.
        if self.read_update_time.is_some() {
            trace!("[{self}] Checking for PN overlap");
            let next_dx = &mut self.app_read_next.as_mut().ok_or(Error::Internal)?.dx;
            next_dx.continuation(&self.app_read.as_ref().ok_or(Error::Internal)?.dx)?;
        }
        Ok(())
    }

    /// Make some state for removing protection in tests.
    #[cfg(all(not(feature = "disable-encryption"), any(test, feature = "bench")))]
    pub fn test_default() -> Self {
        let read = |epoch| {
            let mut dx = CryptoDxState::test_default_read();
            dx.epoch = epoch;
            dx
        };
        let app_read = |epoch| CryptoDxAppData {
            dx: read(epoch),
            cipher: TLS_AES_128_GCM_SHA256,
            next_secret: hkdf::import_key(TLS_VERSION_1_3, &[0xaa; 32]).expect("key is valid"),
        };
        let initials = EnumMap::from_fn(|v| {
            (v == Version::Version1).then(|| CryptoState {
                tx: CryptoDxState::test_default_write(),
                rx: read(0),
            })
        });
        Self {
            initials,
            handshake: None,
            zero_rtt: None,
            cipher: TLS_AES_128_GCM_SHA256,
            // This isn't used, but the epoch is read to check for a key update.
            app_write: Some(app_read(3)),
            app_read: Some(app_read(3)),
            app_read_next: Some(app_read(4)),
            read_update_time: None,
            read_update_epoch: None,
        }
    }

    #[cfg(all(not(feature = "disable-encryption"), test))]
    #[cfg(test)]
    pub(crate) fn test_chacha() -> Self {
        const SECRET: &[u8] = &[
            0x9a, 0xc3, 0x12, 0xa7, 0xf8, 0x77, 0x46, 0x8e, 0xbe, 0x69, 0x42, 0x27, 0x48, 0xad,
            0x00, 0xa1, 0x54, 0x43, 0xf1, 0x82, 0x03, 0xa0, 0x7d, 0x60, 0x60, 0xf6, 0x88, 0xf3,
            0x0f, 0x21, 0x63, 0x2b,
        ];
        let secret = hkdf::import_key(TLS_VERSION_1_3, SECRET).unwrap();
        let app_read = |epoch| CryptoDxAppData {
            dx: CryptoDxState {
                version: Version::Version1,
                direction: CryptoDxDirection::Read,
                epoch,
                aead: Aead::new(
                    TLS_VERSION_1_3,
                    TLS_CHACHA20_POLY1305_SHA256,
                    &secret,
                    "quic ", // This is a v1 test so hard-code the label.
                    Mode::Decrypt,
                )
                .unwrap(),
                hpkey: hp::Key::extract(
                    TLS_VERSION_1_3,
                    TLS_CHACHA20_POLY1305_SHA256,
                    &secret,
                    "quic hp",
                )
                .unwrap(),
                used_pn: 0..645_971_972,
                min_pn: 0,
                invocations: 10,
                largest_packet_len: INITIAL_LARGEST_PACKET_LEN,
            },
            cipher: TLS_CHACHA20_POLY1305_SHA256,
            next_secret: secret.clone(),
        };
        Self {
            initials: EnumMap::default(),
            handshake: None,
            zero_rtt: None,
            cipher: TLS_CHACHA20_POLY1305_SHA256,
            app_write: Some(app_read(3)),
            app_read: Some(app_read(3)),
            app_read_next: Some(app_read(4)),
            read_update_time: None,
            read_update_epoch: None,
        }
    }
}

impl Display for CryptoStates {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        write!(f, "CryptoStates")
    }
}

#[derive(Debug, Default)]
pub struct CryptoStream {
    tx: TxBuffer,
    rx: RxStreamOrderer,
    /// The end of the highest range ever sent; unlike `tx`, losses don't lower it.
    sent: u64,
}

#[derive(Debug)]
pub enum CryptoStreams {
    Initial {
        initial: CryptoStream,
        handshake: CryptoStream,
        application: CryptoStream,
    },
    Handshake {
        handshake: CryptoStream,
        application: CryptoStream,
    },
    ApplicationData {
        application: CryptoStream,
    },
}

impl CryptoStreams {
    /// Keep around 64k if a server wants to push excess data at us.
    const BUFFER_LIMIT: u64 = 65536;

    pub fn discard(&mut self, space: PacketNumberSpace) {
        match space {
            PacketNumberSpace::Initial => {
                if let Self::Initial {
                    handshake,
                    application,
                    ..
                } = self
                {
                    *self = Self::Handshake {
                        handshake: mem::take(handshake),
                        application: mem::take(application),
                    };
                }
            }
            PacketNumberSpace::Handshake => {
                if let Self::Handshake { application, .. } = self {
                    *self = Self::ApplicationData {
                        application: mem::take(application),
                    };
                } else if matches!(self, Self::Initial { .. }) {
                    panic!("Discarding handshake before initial discarded");
                }
            }
            PacketNumberSpace::ApplicationData => {
                panic!("Discarding application data crypto streams")
            }
        }
    }

    pub fn send(&mut self, space: PacketNumberSpace, data: &[u8]) -> Res<()> {
        self.get_mut(space)
            .ok_or(Error::ProtocolViolation)?
            .tx
            .send(data);
        Ok(())
    }

    /// Buffer a CRYPTO frame. Returns whether all of it had already been received.
    /// # Errors
    /// `CryptoBufferExceeded` when too much data is buffered, or when it is in too many ranges.
    pub fn inbound_frame(
        &mut self,
        space: PacketNumberSpace,
        offset: u64,
        data: &[u8],
    ) -> Res<bool> {
        let rx = &mut self.get_mut(space).ok_or(Error::Internal)?.rx;
        // Nothing this far ahead can ever be delivered.
        if !data.is_empty() && offset > rx.retired() + Self::BUFFER_LIMIT {
            return Err(Error::CryptoBufferExceeded);
        }
        let duplicate = !data.is_empty() && rx.already_received(offset, data.len());
        rx.inbound_frame(offset, data)
            .map_err(|_| Error::CryptoBufferExceeded)?;
        if rx.received() - rx.retired() <= Self::BUFFER_LIMIT {
            Ok(duplicate)
        } else {
            Err(Error::CryptoBufferExceeded)
        }
    }

    pub fn data_ready(&self, space: PacketNumberSpace) -> bool {
        self.get(space).is_some_and(|cs| cs.rx.data_ready())
    }

    pub fn read_to_end(&mut self, space: PacketNumberSpace, buf: &mut Vec<u8>) -> Res<usize> {
        Ok(self
            .get_mut(space)
            .ok_or(Error::ProtocolViolation)?
            .rx
            .read_to_end(buf))
    }

    pub fn acked(&mut self, space: PacketNumberSpace, token: &CryptoRecoveryToken) {
        if let Some(cs) = self.get_mut(space) {
            cs.tx.mark_as_acked(token.offset, token.length);
        }
    }

    pub fn lost(&mut self, space: PacketNumberSpace, token: &CryptoRecoveryToken) {
        // See BZ 1624800, ignore lost packets in spaces we've dropped keys
        if let Some(cs) = self.get_mut(space) {
            cs.tx.mark_as_lost(token.offset, token.length);
        }
    }

    /// Resend any Initial or Handshake CRYPTO frames that might be outstanding.
    /// This can help speed up handshake times. Returns whether anything was queued to resend.
    pub fn resend_unacked(&mut self, space: PacketNumberSpace) -> bool {
        space != PacketNumberSpace::ApplicationData
            && self.get_mut(space).is_some_and(|cs| cs.tx.unmark_sent())
    }

    pub fn is_empty(&mut self, space: PacketNumberSpace) -> bool {
        self.get_mut(space).is_none_or(|cs| cs.tx.is_empty())
    }

    /// Whether everything written to `space` has been sent at least once.
    pub fn sent_all(&self, space: PacketNumberSpace) -> bool {
        self.get(space).is_none_or(|cs| cs.sent >= cs.tx.used())
    }

    const fn get(&self, space: PacketNumberSpace) -> Option<&CryptoStream> {
        let (initial, hs, app) = match self {
            Self::Initial {
                initial,
                handshake,
                application,
            } => (Some(initial), Some(handshake), Some(application)),
            Self::Handshake {
                handshake,
                application,
            } => (None, Some(handshake), Some(application)),
            Self::ApplicationData { application } => (None, None, Some(application)),
        };
        match space {
            PacketNumberSpace::Initial => initial,
            PacketNumberSpace::Handshake => hs,
            PacketNumberSpace::ApplicationData => app,
        }
    }

    const fn get_mut(&mut self, space: PacketNumberSpace) -> Option<&mut CryptoStream> {
        let (initial, hs, app) = match self {
            Self::Initial {
                initial,
                handshake,
                application,
            } => (Some(initial), Some(handshake), Some(application)),
            Self::Handshake {
                handshake,
                application,
            } => (None, Some(handshake), Some(application)),
            Self::ApplicationData { application } => (None, None, Some(application)),
        };
        match space {
            PacketNumberSpace::Initial => initial,
            PacketNumberSpace::Handshake => hs,
            PacketNumberSpace::ApplicationData => app,
        }
    }

    pub fn write_frame<B: Buffer>(
        &mut self,
        space: PacketNumberSpace,
        sni_slicing: bool,
        builder: &mut packet::Builder<B>,
        tokens: &mut recovery::Tokens,
        stats: &mut FrameStats,
    ) {
        fn write_chunk<B: Buffer>(
            offset: u64,
            data: &[u8],
            builder: &mut packet::Builder<B>,
        ) -> Option<(u64, usize)> {
            let mut header_len = 1 + Encoder::varint_len(offset) + 1;

            // Don't bother if there isn't room for the header and some data.
            if builder.remaining() < header_len + 1 {
                return None;
            }
            // Calculate length of data based on the minimum of:
            // - available data
            // - remaining space, less the header, which counts only one byte for the length at
            //   first to avoid underestimating length
            let length = min(data.len(), builder.remaining() - header_len);
            header_len += Encoder::varint_len(to_u64(length)) - 1;
            let length = min(data.len(), builder.remaining() - header_len);

            builder.encode_frame(FrameType::Crypto, |b| {
                b.encode_varint(offset);
                b.encode_vvec(&data[..length]);
            });
            Some((offset, length))
        }

        fn mark_as_sent(
            cs: &mut CryptoStream,
            space: PacketNumberSpace,
            tokens: &mut recovery::Tokens,
            offset: u64,
            len: usize,
            stats: &mut FrameStats,
        ) {
            cs.tx.mark_as_sent(offset, len);
            cs.sent = max(cs.sent, offset + to_u64(len));
            debug!("CRYPTO for {space} offset={offset}, len={len}");
            tokens.push(recovery::Token::Crypto(CryptoRecoveryToken {
                offset,
                length: len,
            }));
            stats.crypto += 1;
        }

        #[expect(clippy::type_complexity, reason = "Yeah, a bit complex but still OK.")]
        const fn limit_chunks<'a>(
            left: (u64, &'a [u8]),
            right: (u64, &'a [u8]),
            limit: usize,
        ) -> ((u64, &'a [u8]), (u64, &'a [u8])) {
            let (left_offset, mut left) = left;
            let (mut right_offset, mut right) = right;
            if left.len() + right.len() <= limit {
                // Nothing to do. Both chunks will fit into one packet, meaning the SNI isn't spread
                // over multiple packets. But at least it's in two unordered CRYPTO frames.
            } else if left.len() <= limit {
                // `left` is short enough to fit into this packet. So send from the *end*
                // of `right`, so that the second half of the SNI is in another packet.
                let right_len = right.len() + left.len() - limit;
                right_offset += to_u64(right_len);
                (_, right) = right.split_at(right_len);
            } else if right.len() <= limit {
                // `right` is short enough to fit into this packet. So only send a part of `left`.
                // The SNI begins at the end of `left`, so send the beginnig of it in this packet.
                (left, _) = left.split_at(limit - right.len());
            } else {
                // Both chunks are too long to fit into one packet. Just send a part of each.
                (left, _) = left.split_at(limit / 2);
                (right, _) = right.split_at(limit / 2);
            }
            ((left_offset, left), (right_offset, right))
        }

        let Some(cs) = self.get_mut(space) else {
            return;
        };
        while let Some((offset, data)) = cs.tx.next_bytes() {
            #[cfg(feature = "build-fuzzing-corpus")]
            if offset == 0 {
                neqo_common::write_item_to_fuzzing_corpus("find_sni", data);
            }
            let written = if sni_slicing && offset == 0 {
                if let Some(sni) = find_sni(data) {
                    // Cut the crypto data in two at the midpoint of the SNI and swap the chunks.
                    let mid = sni.start.midpoint(sni.end);
                    let (left, right) = data.split_at(mid);

                    // Truncate the chunks so we can fit them into roughly evenly-filled packets.
                    let packets_needed = data.len().div_ceil(builder.limit());
                    let limit = data.len() / packets_needed;
                    let ((left_offset, left), (right_offset, right)) =
                        limit_chunks((offset, left), (offset + to_u64(mid), right), limit);
                    (
                        write_chunk(right_offset, right, builder),
                        write_chunk(left_offset, left, builder),
                    )
                } else {
                    // No SNI found, write the entire data.
                    (write_chunk(offset, data, builder), None)
                }
            } else {
                // SNI slicing disabled or data not at offset 0, write the entire data.
                (write_chunk(offset, data, builder), None)
            };

            match written {
                (None, None) => break,
                (None, Some((offset, len))) | (Some((offset, len)), None) => {
                    mark_as_sent(cs, space, tokens, offset, len, stats);
                }
                (Some((offset1, len1)), Some((offset2, len2))) => {
                    mark_as_sent(cs, space, tokens, offset1, len1, stats);
                    mark_as_sent(cs, space, tokens, offset2, len2, stats);
                    // We only end up in this arm if we successfully sliced above. In that case,
                    // don't try and fit more crypto data into this packet.
                    break;
                }
            }
        }
    }
}

impl Default for CryptoStreams {
    fn default() -> Self {
        Self::Initial {
            initial: CryptoStream::default(),
            handshake: CryptoStream::default(),
            application: CryptoStream::default(),
        }
    }
}

/// The packet number space is not recorded because it can be derived from the carrying packet's
/// type.
#[derive(Debug, Clone)]
pub struct CryptoRecoveryToken {
    offset: u64,
    length: usize,
}

#[cfg(all(test, not(feature = "disable-encryption")))]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {
    use neqo_common::hex::Hex;
    use nss::{TLS_CHACHA20_POLY1305_SHA256, TLS_VERSION_1_3, hkdf};
    use test_fixture::fixture_init;

    use super::{CryptoDxAppData, CryptoDxDirection, CryptoDxState};
    use crate::Version;

    #[test]
    fn key_update_secrets() {
        // The initial secret and first update are from RFC 9001, Appendix A.5,
        // and RFC 9369, Appendix A.5. The second update applies HKDF-Expand-Label
        // again with SHA-256 and the respective "quic ku" or "quicv2 ku" label.
        const SECRET: &[u8] = &[
            0x9a, 0xc3, 0x12, 0xa7, 0xf8, 0x77, 0x46, 0x8e, 0xbe, 0x69, 0x42, 0x27, 0x48, 0xad,
            0x00, 0xa1, 0x54, 0x43, 0xf1, 0x82, 0x03, 0xa0, 0x7d, 0x60, 0x60, 0xf6, 0x88, 0xf3,
            0x0f, 0x21, 0x63, 0x2b,
        ];
        const V1_SECRETS: [&str; 2] = [
            "1223504755036d556342ee9361d253421a826c9ecdf3c7148684b36b714881f9",
            "ef172661d26526b8adddf9497f88649df5786fa7d2f49a2341da624e8d7f3f94",
        ];
        const V2_SECRETS: [&str; 2] = [
            "c69374c49e3d2a9466fa689e49d476db5d0dfbc87d32ceeaa6343fd0ae4c7d88",
            "7f81b8fa265dac8413d60045461c28d11a0b70300c479c44310d34284fd780bc",
        ];

        fixture_init();
        let secret = hkdf::import_key(TLS_VERSION_1_3, SECRET).unwrap();
        for version in Version::all() {
            let expected = match version {
                Version::Version2 => V2_SECRETS,
                Version::Version1 => V1_SECRETS,
                #[cfg(feature = "draft-29")]
                Version::Draft29 => V1_SECRETS,
            };
            for direction in [CryptoDxDirection::Read, CryptoDxDirection::Write] {
                let mut app =
                    CryptoDxAppData::new(version, direction, &secret, TLS_CHACHA20_POLY1305_SHA256)
                        .unwrap();
                for expected_secret in expected {
                    assert_eq!(
                        Hex::new(app.next_secret.key_data().unwrap()).to_string(),
                        expected_secret,
                        "{version:?} {direction:?} epoch {}",
                        app.epoch(),
                    );
                    app = app.next().unwrap();
                }
            }
        }
    }

    #[test]
    fn crypto_dx_state_display() {
        fixture_init();
        let dx = CryptoDxState::test_default_write();
        assert_eq!(dx.to_string(), "epoch 0 Write");
    }
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod buffer_limits {
    use neqo_common::{Encoder, to_u64};

    use super::{CryptoStreams, PacketNumberSpace, RxStreamOrderer};
    use crate::{Error, packet, recovery, stats::FrameStats};

    /// How many one-byte ranges, each separated by a one-byte gap, to offer.
    const RANGES: u64 = to_u64(RxStreamOrderer::MAX_GAPS) * 2;

    // Offsets have to stay inside `BUFFER_LIMIT`.
    const _: () = assert!(2 * RANGES < CryptoStreams::BUFFER_LIMIT);

    #[test]
    fn crypto_offset_beyond_buffer() {
        let offset = CryptoStreams::BUFFER_LIMIT + 1;
        assert_eq!(
            CryptoStreams::default().inbound_frame(PacketNumberSpace::Initial, offset, &[0; 1]),
            Err(Error::CryptoBufferExceeded)
        );
        assert_eq!(
            CryptoStreams::default().inbound_frame(PacketNumberSpace::Initial, offset, &[]),
            Ok(false) // Empty data is not duplicate data.
        );
    }

    /// The same bytes twice are duplicate; a partial overlap is not.
    #[test]
    fn crypto_duplicate_data() {
        const SPACE: PacketNumberSpace = PacketNumberSpace::Initial;
        let mut cs = CryptoStreams::default();
        assert_eq!(cs.inbound_frame(SPACE, 0, &[0; 2]), Ok(false));
        assert_eq!(cs.inbound_frame(SPACE, 0, &[0; 2]), Ok(true), "duplicate");
        assert_eq!(cs.inbound_frame(SPACE, 1, &[0; 2]), Ok(false), "partial");
    }

    #[test]
    fn crypto_too_many_ranges() {
        let mut cs = CryptoStreams::default();
        let rejected = (0..RANGES).find_map(|i| {
            cs.inbound_frame(PacketNumberSpace::Initial, 2 * i + 1, &[0; 1])
                .err()
        });
        assert_eq!(rejected, Some(Error::CryptoBufferExceeded));
    }

    /// Lost data still counts as sent.
    #[test]
    fn crypto_sent_all_after_loss() {
        let hs = PacketNumberSpace::Handshake;
        let mut cs = CryptoStreams::default();
        cs.send(hs, &[0; 4]).unwrap();
        assert!(!cs.sent_all(hs));

        let mut builder =
            packet::Builder::short(Encoder::default(), false, None::<&[u8]>, packet::LIMIT);
        let (mut tokens, mut stats) = (Vec::new(), FrameStats::default());
        cs.write_frame(hs, false, &mut builder, &mut tokens, &mut stats);
        assert!(cs.sent_all(hs));

        let Some(recovery::Token::Crypto(token)) = tokens.pop() else {
            panic!("expected a CRYPTO token");
        };
        cs.lost(hs, &token);
        assert!(!cs.is_empty(hs));
        assert!(cs.sent_all(hs));
    }
}
