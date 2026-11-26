/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

use anyhow::{bail, Context, Result};
use clap::Parser;
use md5::{Digest, Md5};
use std::collections::BTreeSet;
use std::fs::{read_to_string, write};
use std::path::PathBuf;

#[derive(Parser, Debug)]
#[command(version, about = "A tool for generating the adult_set.bin from a list of base domains.", long_about = None)]
struct Cli {
    /// The path to a text file with one base domain per line. Blank lines are
    /// ignored.
    input_file: PathBuf,

    /// The path to write the adult_set.bin file, which are the bytes for the
    /// hash set of adult sites.
    output_file: PathBuf,
}

fn main() -> Result<()> {
    let cli = Cli::parse();
    let contents = read_to_string(&cli.input_file)
        .with_context(|| format!("Unable to read {}", cli.input_file.display()))?;

    let domains = parse_domains(&contents)?;

    write(&cli.output_file, hash_domains(&domains))
        .with_context(|| format!("Unable to write {}", cli.output_file.display()))?;
    println!(
        "Wrote {} domains to {}",
        domains.len(),
        cli.output_file.display()
    );

    Ok(())
}

fn parse_domains(contents: &str) -> Result<BTreeSet<String>> {
    let mut domains = BTreeSet::new();
    for (index, line) in contents.lines().enumerate() {
        let domain = line.trim().to_lowercase();
        if domain.is_empty() {
            continue;
        }
        let is_valid = domain.contains('.')
            && !domain.starts_with('.')
            && !domain.ends_with('.')
            && !domain.contains(|c: char| c.is_whitespace() || "/:?#@".contains(c));
        if !is_valid {
            bail!("Line {}: {:?} is not a valid domain", index + 1, line);
        }
        domains.insert(domain);
    }
    Ok(domains)
}

fn hash_domains(domains: &BTreeSet<String>) -> Vec<u8> {
    domains
        .iter()
        .flat_map(|domain| Md5::digest(domain.as_bytes()))
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_domains_normalizes_and_skips() {
        let domains = parse_domains("\n  Example.COM \nexample.com\nexample.co.uk\r\n").unwrap();
        assert_eq!(
            domains.into_iter().collect::<Vec<_>>(),
            vec!["example.co.uk", "example.com"]
        );
    }

    #[test]
    fn test_parse_domains_rejects_non_domains() {
        for bad in [
            "https://example.com",
            "example.com/path",
            "example com",
            "localhost",
            ".example.com",
            "example.com.",
            "# example.com",
        ] {
            let err = parse_domains(&format!("ok.com\n{bad}\n")).unwrap_err();
            assert!(err.to_string().starts_with("Line 2:"), "{bad}: {err}");
        }
    }

    #[test]
    fn test_hash_domains_is_md5_of_each_domain() {
        let domains = parse_domains("b.com\na.com\n").unwrap();
        let bytes = hash_domains(&domains);
        assert_eq!(bytes.len(), 32);
        assert_eq!(&bytes[..16], Md5::digest(b"a.com").as_slice());
        assert_eq!(&bytes[16..], Md5::digest(b"b.com").as_slice());
    }
}
