/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

use std::error::Error;
use std::io::Cursor;

use log::{debug, warn};

use prio::field::Field128;
use prio::vdaf::prio3::{Prio3Histogram, Prio3Sum, Prio3SumVec};
use prio::vdaf::prio3::{Prio3InputShare, Prio3PublicShare};
use thin_vec::ThinVec;

pub mod types;
use types::HpkeConfig;
use types::PlaintextInputShare;
use types::Report;
use types::ReportID;
use types::ReportMetadata;
use types::Time;

use prio::codec::Encode;
use prio::codec::{decode_u16_items, encode_u32_items};
use prio::vdaf::Client;

use crate::types::HpkeCiphertext;

extern "C" {
    pub fn dapHpkeEncryptOneshot(
        aKey: *const u8,
        aKeyLength: u32,
        aInfo: *const u8,
        aInfoLength: u32,
        aAad: *const u8,
        aAadLength: u32,
        aPlaintext: *const u8,
        aPlaintextLength: u32,
        aOutputEncapsulatedKey: &mut ThinVec<u8>,
        aOutputShare: &mut ThinVec<u8>,
    ) -> bool;
}

struct SumMeasurement {
    value: u32,
    bits: usize,
}

struct SumVecMeasurement<'a> {
    value: &'a ThinVec<u32>,
    bits: usize,
}

struct HistogramMeasurement {
    index: u32,
    length: usize,
}

enum Role {
    Leader = 2,
    Helper = 3,
}

// While Prio allows more than two aggregators, in practice for DAP we only ever
// use a Leader and Helper.
const NUM_AGGREGATORS: u8 = 2;

/// A minimal wrapper around the FFI function which mostly just converts datatypes.
fn hpke_encrypt_wrapper(
    plain_share: &Vec<u8>,
    aad: &Vec<u8>,
    info: &Vec<u8>,
    hpke_config: &HpkeConfig,
) -> Result<HpkeCiphertext, Box<dyn std::error::Error>> {
    let mut encrypted_share = ThinVec::<u8>::new();
    let mut encapsulated_key = ThinVec::<u8>::new();
    unsafe {
        if !dapHpkeEncryptOneshot(
            hpke_config.public_key.as_ptr(),
            hpke_config.public_key.len() as u32,
            info.as_ptr(),
            info.len() as u32,
            aad.as_ptr(),
            aad.len() as u32,
            plain_share.as_ptr(),
            plain_share.len() as u32,
            &mut encapsulated_key,
            &mut encrypted_share,
        ) {
            return Err(Box::from("Encryption failed."));
        }
    }

    Ok(HpkeCiphertext {
        config_id: hpke_config.id,
        enc: encapsulated_key.to_vec(),
        payload: encrypted_share.to_vec(),
    })
}

const SEED_SIZE: usize = 16;
fn encode_prio3_shares(
    public_share: Prio3PublicShare<SEED_SIZE>,
    input_shares: Vec<Prio3InputShare<Field128, SEED_SIZE>>,
) -> Result<(Vec<u8>, Vec<Vec<u8>>), Box<dyn std::error::Error>> {
    debug_assert_eq!(input_shares.len(), NUM_AGGREGATORS as usize);

    let encoded_input_shares = input_shares
        .iter()
        .map(|s| s.get_encoded())
        .collect::<Result<Vec<_>, _>>()?;
    let encoded_public_share = public_share.get_encoded()?;
    Ok((encoded_public_share, encoded_input_shares))
}

trait Shardable {
    fn shard(
        &self,
        nonce: &[u8; 16],
    ) -> Result<(Vec<u8>, Vec<Vec<u8>>), Box<dyn std::error::Error>>;
}

impl Shardable for SumMeasurement {
    fn shard(
        &self,
        nonce: &[u8; 16],
    ) -> Result<(Vec<u8>, Vec<Vec<u8>>), Box<dyn std::error::Error>> {
        let prio = Prio3Sum::new_sum(NUM_AGGREGATORS, self.bits)?;

        let (public_share, input_shares) = prio.shard(&(self.value as u128), nonce)?;

        encode_prio3_shares(public_share, input_shares)
    }
}

impl Shardable for SumVecMeasurement<'_> {
    fn shard(
        &self,
        nonce: &[u8; 16],
    ) -> Result<(Vec<u8>, Vec<Vec<u8>>), Box<dyn std::error::Error>> {
        let chunk_length = prio::vdaf::prio3::optimal_chunk_length(self.bits * self.value.len());
        let prio =
            Prio3SumVec::new_sum_vec(NUM_AGGREGATORS, self.bits, self.value.len(), chunk_length)?;

        let measurement: Vec<u128> = self.value.iter().map(|e| (*e as u128)).collect();
        let (public_share, input_shares) = prio.shard(&measurement, nonce)?;

        encode_prio3_shares(public_share, input_shares)
    }
}

impl Shardable for HistogramMeasurement {
    fn shard(
        &self,
        nonce: &[u8; 16],
    ) -> Result<(Vec<u8>, Vec<Vec<u8>>), Box<dyn std::error::Error>> {
        let chunk_length = prio::vdaf::prio3::optimal_chunk_length(self.length);
        let prio = Prio3Histogram::new_histogram(NUM_AGGREGATORS, self.length, chunk_length)?;

        let (public_share, input_shares) = prio.shard(&(self.index as usize), nonce)?;

        encode_prio3_shares(public_share, input_shares)
    }
}

// Decode advertised HPKE configurations and pick a supported mode.
fn select_hpke_config(encoded: &ThinVec<u8>) -> Result<HpkeConfig, Box<dyn Error>> {
    let hpke_configs: Vec<HpkeConfig> = decode_u16_items(&(), &mut Cursor::new(encoded))?;

    // Our supported HPKE algorithms with constants from RFC-9180.
    const SUPPORTED_KEM: u16 = 0x20; // DHKEM(X25519, HKDF-SHA256)
    const SUPPORTED_KDF: u16 = 0x01; // HKDF-SHA256
    const SUPPORTED_AEAD: u16 = 0x01; // AES-128-GCM

    for config in hpke_configs {
        if config.kem_id == SUPPORTED_KEM
            && config.kdf_id == SUPPORTED_KDF
            && config.aead_id == SUPPORTED_AEAD
        {
            return Ok(config);
        }
    }

    Err("No suitable HPKE config found.".into())
}

/// This function creates a full report - ready to send - for a measurement.
///
/// To do that it also needs the HPKE configurations for the endpoints and some
/// additional data which is part of the authentication.
fn get_dap_report_internal<T: Shardable>(
    leader_hpke_config_encoded: &ThinVec<u8>,
    helper_hpke_config_encoded: &ThinVec<u8>,
    measurement: &T,
    task_id: &ThinVec<u8>,
    time_precision: u64,
) -> Result<Report, Box<dyn std::error::Error>> {
    let leader_hpke_config = select_hpke_config(leader_hpke_config_encoded)?;
    let helper_hpke_config = select_hpke_config(helper_hpke_config_encoded)?;

    let report_id = ReportID::generate();
    let (encoded_public_share, encoded_input_shares) = measurement.shard(report_id.as_ref())?;

    let plaintext_input_shares: Vec<Vec<u8>> = encoded_input_shares
        .into_iter()
        .map(|encoded_input_share| {
            PlaintextInputShare {
                extensions: Vec::new(),
                payload: encoded_input_share,
            }
            .get_encoded()
        })
        .collect::<Result<Vec<_>, _>>()?;
    debug!("Plaintext input shares computed.");

    let time = Time::generate(time_precision);
    let metadata = ReportMetadata { report_id, time };

    // This quote from the standard describes which info and aad to use for the encryption:
    //     enc, payload = SealBase(pk,
    //         "dap-09 input share" || 0x01 || server_role,
    //         input_share_aad, plaintext_input_share)
    // https://www.ietf.org/archive/id/draft-ietf-ppm-dap-09.html#name-upload-request
    let mut info = b"dap-09 input share\x01".to_vec();

    assert_eq!(task_id.len(), 32);
    let mut aad = Vec::from(task_id.as_ref());
    metadata.encode(&mut aad)?;
    encode_u32_items(&mut aad, &(), &encoded_public_share)?;

    info.push(Role::Leader as u8);
    let leader_payload =
        hpke_encrypt_wrapper(&plaintext_input_shares[0], &aad, &info, &leader_hpke_config)?;
    debug!("Leader payload encrypted.");
    info.pop();

    info.push(Role::Helper as u8);
    let helper_payload =
        hpke_encrypt_wrapper(&plaintext_input_shares[1], &aad, &info, &helper_hpke_config)?;
    debug!("Helper payload encrypted.");
    info.pop();

    Ok(Report {
        metadata,
        public_share: encoded_public_share,
        leader_encrypted_input_share: leader_payload,
        helper_encrypted_input_share: helper_payload,
    })
}

/// Wraps the function above with minor C interop.
/// Mostly it turns any error result into a return value of false.
#[no_mangle]
pub extern "C" fn dapGetReportPrioSum(
    leader_hpke_config_encoded: &ThinVec<u8>,
    helper_hpke_config_encoded: &ThinVec<u8>,
    measurement: u32,
    task_id: &ThinVec<u8>,
    bits: u32,
    time_precision: u64,
    out_report: &mut ThinVec<u8>,
) -> bool {
    let Ok(report) = get_dap_report_internal::<SumMeasurement>(
        leader_hpke_config_encoded,
        helper_hpke_config_encoded,
        &SumMeasurement {
            value: measurement,
            bits: bits as usize,
        },
        task_id,
        time_precision,
    ) else {
        warn!("Creating report failed!");
        return false;
    };
    let Ok(encoded_report) = report.get_encoded() else {
        warn!("Encoding report failed!");
        return false;
    };
    out_report.extend(encoded_report);
    true
}

#[no_mangle]
pub extern "C" fn dapGetReportPrioSumVec(
    leader_hpke_config_encoded: &ThinVec<u8>,
    helper_hpke_config_encoded: &ThinVec<u8>,
    measurement: &ThinVec<u32>,
    task_id: &ThinVec<u8>,
    bits: u32,
    time_precision: u64,
    out_report: &mut ThinVec<u8>,
) -> bool {
    let Ok(report) = get_dap_report_internal::<SumVecMeasurement>(
        leader_hpke_config_encoded,
        helper_hpke_config_encoded,
        &SumVecMeasurement {
            value: measurement,
            bits: bits as usize,
        },
        task_id,
        time_precision,
    ) else {
        warn!("Creating report failed!");
        return false;
    };
    let Ok(encoded_report) = report.get_encoded() else {
        warn!("Encoding report failed!");
        return false;
    };
    out_report.extend(encoded_report);
    true
}

#[no_mangle]
pub extern "C" fn dapGetReportPrioHistogram(
    leader_hpke_config_encoded: &ThinVec<u8>,
    helper_hpke_config_encoded: &ThinVec<u8>,
    measurement: u32,
    task_id: &ThinVec<u8>,
    length: u32,
    time_precision: u64,
    out_report: &mut ThinVec<u8>,
) -> bool {
    let Ok(report) = get_dap_report_internal::<HistogramMeasurement>(
        leader_hpke_config_encoded,
        helper_hpke_config_encoded,
        &HistogramMeasurement {
            index: measurement,
            length: length as usize,
        },
        task_id,
        time_precision,
    ) else {
        warn!("Creating report failed!");
        return false;
    };
    let Ok(encoded_report) = report.get_encoded() else {
        warn!("Encoding report failed!");
        return false;
    };
    out_report.extend(encoded_report);
    true
}
