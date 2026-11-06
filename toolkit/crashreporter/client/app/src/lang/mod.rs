/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

mod langpack;
mod language_info;
mod omnijar;
mod zip;

use crate::prefs::Prefs;
use fluent::{bundle::FluentBundle, FluentArgs, FluentResource};
use intl_memoizer::concurrent::IntlLangMemoizer;
use language_info::LanguageInfo;
use std::borrow::Cow;
use std::collections::BTreeMap;

/// Get the localized strings.
pub fn load() -> LangStrings {
    let mut bundles = Vec::new();
    match omnijar::read() {
        Ok(infos) => bundles.extend(infos.into_iter().filter_map(|info| {
            let id = info.identifier.clone();
            match info.load_strings() {
                Ok(bundle) => Some(bundle),
                Err(e) => {
                    log::error!("failed to load localization bundle for {id}: {e:#}");
                    None
                }
            }
        })),
        Err(e) => {
            log::error!("failed to read localization data from the omnijar ({e:#})");
        }
    }
    bundles.push(
        LanguageInfo::default()
            .load_strings()
            .expect("the default/fallback LanguageInfo failed to load"),
    );
    LangStrings::new(bundles)
}

/// A bundle of localized strings.
pub struct LangStrings {
    bundles: Vec<LanguageBundle>,
}

#[cfg(test)]
impl Default for LangStrings {
    fn default() -> Self {
        LangStrings::new(vec![LanguageInfo::default().load_strings().unwrap()])
    }
}

struct LanguageBundle {
    bundle: FluentBundle<FluentResource, IntlLangMemoizer>,
    rtl: bool,
}

impl LanguageBundle {
    fn new(bundle: FluentBundle<FluentResource, IntlLangMemoizer>, rtl: bool) -> Self {
        LanguageBundle { bundle, rtl }
    }

    /// Return the language identifier string for the primary locale.
    fn locale(&self) -> String {
        self.bundle.locales.first().unwrap().to_string()
    }

    /// Return whether the localized language has right-to-left text flow.
    fn is_rtl(&self) -> bool {
        self.rtl
    }

    fn get(&self, index: &str, args: &FluentArgs) -> anyhow::Result<String> {
        let Some(pattern) = self.bundle.get_message(index).and_then(|m| m.value()) else {
            anyhow::bail!("failed to get fluent message for {index}");
        };
        let mut errs = Vec::new();
        let ret = self.bundle.format_pattern(pattern, Some(&args), &mut errs);
        if !errs.is_empty() {
            anyhow::bail!("errors while formatting pattern in {index}: {errs:?}");
        }
        Ok(ret.into_owned())
    }
}

/// Arguments to build a localized string.
pub type LangStringsArgs<'a> = BTreeMap<&'a str, Cow<'a, str>>;

impl LangStrings {
    fn new(bundles: Vec<LanguageBundle>) -> Self {
        debug_assert!(!bundles.is_empty());
        LangStrings { bundles }
    }

    /// Return the language identifier string for the primary locale.
    pub fn locale(&self) -> String {
        self.bundles[0].locale()
    }

    /// Return whether the localized language has right-to-left text flow.
    pub fn is_rtl(&self) -> bool {
        self.bundles[0].is_rtl()
    }

    /// Add a localized string bundle from the configured locale langpack, if any.
    ///
    /// This adds the loaded langpack (if successful) as the first/default bundle to be used, using
    /// all existing bundles as fallbacks.
    pub fn add_langpack(
        &mut self,
        prefs: &Prefs,
        profile_dir: &crate::std::path::Path,
        useragent_locale: Option<&str>,
    ) -> anyhow::Result<()> {
        let base_locales: Vec<String> = self.bundles.iter().map(|b| b.locale()).collect();
        if let Some(langpack) = (langpack::Config {
            prefs,
            profile_dir,
            base_locales: &base_locales,
            useragent_locale,
        })
        .current_langpack()?
        {
            self.bundles.insert(0, langpack.load()?);
        }
        Ok(())
    }

    pub fn get(&self, index: &str, args: LangStringsArgs) -> String {
        let mut fluent_args = FluentArgs::with_capacity(args.len());
        for (k, v) in args {
            fluent_args.set(k, v);
        }

        for bundle in &self.bundles {
            match bundle.get(index, &fluent_args) {
                Ok(v) => return v,
                Err(e) => {
                    log::error!(
                        "failed to get localized string from {} bundle: {e}",
                        bundle.locale()
                    );
                }
            }
        }
        // Just show the fluent index. This will make the error somewhat obvious, and it also
        // matches how at least some parts of Firefox UI behave in a similar situation.
        index.to_owned()
    }

    pub fn builder<'a>(&'a self, index: &'a str) -> LangStringBuilder<'a> {
        LangStringBuilder {
            strings: self,
            index,
            args: Default::default(),
        }
    }
}

/// A localized string builder.
pub struct LangStringBuilder<'a> {
    strings: &'a LangStrings,
    index: &'a str,
    args: LangStringsArgs<'a>,
}

impl<'a> LangStringBuilder<'a> {
    /// Set an argument for the string.
    pub fn arg<V: Into<Cow<'a, str>>>(mut self, key: &'a str, value: V) -> Self {
        self.args.insert(key, value.into());
        self
    }

    /// Get the localized string.
    pub fn get(self) -> String {
        self.strings.get(self.index, self.args)
    }
}
