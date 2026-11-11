/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use base64::prelude::BASE64_STANDARD;
use base64::Engine;
use std::cmp::Ordering;
use std::fs::{read_dir, File};
use std::io::{BufRead, BufReader, Write};
use std::path::{Path, PathBuf};

#[derive(Eq, PartialEq)]
pub struct TrustAnchor {
    pub bytes: Vec<u8>,
    pub subject: Vec<u8>,
    pub subject_start: u16,
    pub subject_len: u16,
}

impl PartialOrd for TrustAnchor {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        self.subject.partial_cmp(&other.subject)
    }
}

impl TrustAnchor {
    pub fn new(bytes: Vec<u8>) -> TrustAnchor {
        let (_, _, subject) =
            rsclientcerts_util::read_encoded_certificate_identifiers(bytes.as_slice())
                .expect("Couldn't decode certificate.");
        let subject_start = bytes
            .windows(subject.len())
            .position(|s| s == subject)
            .expect("subject should appear in bytes");
        let subject_start: u16 = subject_start
            .try_into()
            .expect("subject start hopefully fits in u16");
        let subject_len = subject
            .len()
            .try_into()
            .expect("subject length hopefully fits in u16");
        TrustAnchor {
            bytes,
            subject,
            subject_start,
            subject_len,
        }
    }
}

pub fn read_trust_anchors(
    trust_anchor_filename_or_directory: PathBuf,
) -> std::io::Result<Vec<TrustAnchor>> {
    let mut trust_anchors = if trust_anchor_filename_or_directory.is_dir() {
        let mut trust_anchors = Vec::new();
        for dir_entry in read_dir(trust_anchor_filename_or_directory)? {
            let path = dir_entry?.path();
            if let Some(extension) = path.extension() {
                if extension == "pem" {
                    trust_anchors.append(&mut read_trust_anchors_from(path)?);
                }
            }
        }
        trust_anchors
    } else {
        read_trust_anchors_from(trust_anchor_filename_or_directory)?
    };

    trust_anchors.sort_by_cached_key(|trust_anchor| trust_anchor.subject.clone());
    Ok(trust_anchors)
}

pub fn read_trust_anchors_from(
    trust_anchor_file_path: PathBuf,
) -> std::io::Result<Vec<TrustAnchor>> {
    let trust_anchor_file = File::open(&trust_anchor_file_path)?;
    let reader = BufReader::new(trust_anchor_file);
    let mut maybe_current_trust_anchor: Option<Vec<String>> = None;
    let mut trust_anchors = Vec::new();
    for line in reader.lines() {
        let line = line.expect(
            format!(
                "Couldn't read contents of trust anchors file '{}'",
                trust_anchor_file_path.display()
            )
            .as_str(),
        );
        match line.as_str() {
            "-----BEGIN CERTIFICATE-----" => {
                maybe_current_trust_anchor.replace(Vec::new());
            }
            "-----END CERTIFICATE-----" => {
                let current_trust_anchor = maybe_current_trust_anchor
                    .take()
                    .expect("END CERTIFICATE without BEGIN CERTIFICATE?");
                let base64 = current_trust_anchor.join("");
                let bytes = BASE64_STANDARD
                    .decode(base64)
                    .expect("Couldn't base64-decode trust anchor.");
                let trust_anchor = TrustAnchor::new(bytes);
                trust_anchors.push(trust_anchor);
            }
            _ => {
                if let Some(current_trust_anchor) = maybe_current_trust_anchor.as_mut() {
                    current_trust_anchor.push(line);
                }
            }
        }
    }
    Ok(trust_anchors)
}

pub fn emit_trust_anchors(
    out: &mut dyn Write,
    prefix: &str,
    trust_anchors_filename_or_directory: &str,
) -> std::io::Result<()> {
    let trust_anchors_path = Path::new(trust_anchors_filename_or_directory);
    let trust_anchors = read_trust_anchors(trust_anchors_path.to_path_buf())?;
    for (index, trust_anchor) in trust_anchors.iter().enumerate() {
        writeln!(
            out,
            "static {prefix}TRUST_ANCHOR_{index:0>4}_BYTES: &[u8] = &{:?};",
            trust_anchor.bytes
        )?;
    }

    writeln!(
        out,
        "pub (crate) static {prefix}TRUST_ANCHORS: [TrustAnchor; {num_trust_anchors}] = [",
        num_trust_anchors = trust_anchors.len()
    )?;
    for (index, trust_anchor) in trust_anchors.iter().enumerate() {
        writeln!(out, "    TrustAnchor {{")?;
        writeln!(
            out,
            "        bytes: {prefix}TRUST_ANCHOR_{index:0>4}_BYTES,"
        )?;
        writeln!(
            out,
            "        subject: ({}, {}),",
            trust_anchor.subject_start, trust_anchor.subject_len
        )?;
        writeln!(out, "    }},")?;
    }
    writeln!(out, "];")?;
    Ok(())
}
