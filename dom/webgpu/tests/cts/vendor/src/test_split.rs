//! Functionality for splitting CTS tests at certain paths into more tests and files in
//! [`crate::main`].

use std::collections::BTreeSet;

/// The value portion of a key-value pair used in [`crate::main`] to split tests into more tests or
/// files.
#[derive(Debug)]
pub(crate) struct Config<O> {
    /// The new base name used for the sibling directory that this entry will split off from the
    /// original CTS test.
    ///
    /// Chunking WPT tests happens at a directory level in Firefox's CI. If we split into a child
    /// directory, instead of a sibling directory, then we would actually cause the child test to
    /// be run in multiple chunks. Therefore, it's required to split tests into siblings.
    pub new_sibling_basename: &'static str,
    /// How to split the test this entry refers to.
    pub split_by: SplitBy<O>,
}

impl<O> Config<O> {
    pub fn map_observed_values<T>(self, f: impl FnOnce(O) -> T) -> Config<T> {
        let Self {
            new_sibling_basename,
            split_by,
        } = self;
        Config {
            new_sibling_basename,
            split_by: split_by.map_observed_values(f),
        }
    }
}

/// A [`Config::split_by`] value.
#[derive(Debug)]
pub(crate) enum SplitBy<O> {
    FirstParam {
        /// The name of the first parameter in the test, to be used as validation that we are
        /// actually pointing to the correct test.
        expected_name: &'static str,
        /// The method by which a test should be divided.
        split_to: SplitParamsTo,
        /// Values collected during [`Entry::process`], corresponding to a new test entry each.
        observed_values: O,
    },
}

impl SplitBy<()> {
    /// Convenience for constructing [`SplitBy::FirstParam`].
    pub fn first_param(name: &'static str, split_to: SplitParamsTo) -> Self {
        Self::FirstParam {
            expected_name: name,
            split_to,
            observed_values: (),
        }
    }
}

impl<O> SplitBy<O> {
    pub fn map_observed_values<T>(self, f: impl FnOnce(O) -> T) -> SplitBy<T> {
        match self {
            Self::FirstParam {
                expected_name,
                split_to,
                observed_values,
            } => {
                let observed_values = f(observed_values);
                SplitBy::FirstParam {
                    expected_name,
                    split_to,
                    observed_values,
                }
            }
        }
    }
}

/// A [SplitBy::FirstParam::split_to].
#[derive(Debug)]
pub(crate) enum SplitParamsTo {
    /// Place new test entries as siblings in the same file.
    SeparateTestsInSameFile,
}

#[derive(Debug)]
pub(crate) struct Entry<'a> {
    /// Whether this
    pub seen: SeenIn,
    pub config: Config<BTreeSet<&'a str>>,
}

impl Entry<'_> {
    pub fn from_config(config: Config<()>) -> Self {
        Self {
            seen: SeenIn::nowhere(),
            config: config.map_observed_values(|()| BTreeSet::new()),
        }
    }
}

/// An [`Entry::seen`].
#[derive(Debug, Default)]
pub(crate) struct SeenIn {
    pub listing: bool,
    pub wpt_files: bool,
}

impl SeenIn {
    /// Default value: all seen locations set to `false`.
    pub fn nowhere() -> Self {
        Self::default()
    }
}

impl<'a> Entry<'a> {
    /// Accumulates a line from the test listing script in upstream CTS.
    ///
    /// Line is expected to have a full CTS test path, including at least one case parameter, i.e.:
    ///
    /// ```
    /// webgpu:path,to,test,group:test:param1="value1";…
    /// ```
    ///
    /// Note that `;…` is not strictly necessary, when there is only a single case parameter for
    /// the test.
    ///
    /// See [`crate::main`] for more details on how upstream CTS' listing script is invoked.
    pub(crate) fn process_listing_line(
        &mut self,
        test_group_and_later_path: &'a str,
    ) -> miette::Result<()> {
        let rest = test_group_and_later_path;

        let Self { seen, config } = self;

        let Config {
            new_sibling_basename: _,
            split_by,
        } = config;

        match split_by {
            SplitBy::FirstParam {
                ref expected_name,
                split_to: _,
                observed_values,
            } => {
                // NOTE: This only parses strings with no escaped characters. We may need different
                // values later, at which point we'll have to consider what to do here.
                let (ident, rest) = rest.split_once("=").ok_or_else(|| {
                    miette::diagnostic!("failed to get start of value of first arg")
                })?;

                if ident != *expected_name {
                    return Err(miette::diagnostic!(
                        "expected {:?}, got {:?}",
                        expected_name,
                        ident
                    )
                    .into());
                }

                let value = rest
                    .split_once(';')
                    .map(|(value, _rest)| value)
                    .unwrap_or(rest);

                // TODO: parse as JSON?

                observed_values.insert(value);
            }
        }
        seen.listing = true;

        Ok(())
    }
}

/// Iterate over `entries`' [`seen` members](Entry::seen), accumulating one of their fields and
/// panicking if any are `false`
///
/// Used in [`crate::main`] to check that a specific [`Entry::seen`] fields have been set to `true`
/// for each entry configured.
pub(crate) fn assert_seen<'a>(
    entries: impl Iterator<Item = (&'a &'a str, &'a Entry<'a>)>,
    mut in_: impl FnMut(&'a SeenIn) -> &'a bool,
) {
    let mut unseen = Vec::new();
    entries.for_each(|(test_path, entry)| {
        if !*in_(&entry.seen) {
            unseen.push(test_path);
        }
    });
    if !unseen.is_empty() {
        panic!(
            concat!(
                "did not find the following test split config. entries ",
                "in test listing output: {:#?}",
            ),
            unseen
        );
    }
}
