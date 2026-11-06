/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use super::zip::{read_archive_file_as_string, read_zip, Archive};
use super::{language_info::LanguageInfo, LanguageBundle};
use crate::config::installation_resource_path;
use crate::prefs::Prefs;
use crate::std::path::{Path, PathBuf};
use anyhow::Context;

/// The configuration relevant for langpack selection.
pub struct Config<'a> {
    pub prefs: &'a Prefs,
    pub profile_dir: &'a Path,
    pub base_locales: &'a [String],
    pub useragent_locale: Option<&'a str>,
}

/// An installed langpack.
pub struct Langpack {
    identifier: String,
    path: PathBuf,
}

impl Config<'_> {
    /// Get the current langpack in use.
    ///
    /// This attempts other configured languages if langpack files are missing.
    ///
    /// Returns `Ok(None)` if no locales in pref and the useragent locale are available. This also
    /// returns `Ok(None)` if the locale specified is the base installation locale, as there is no
    /// langpack for that locale.
    pub fn current_langpack(&self) -> anyhow::Result<Option<Langpack>> {
        let mut locales = locales_from_prefs(self.prefs)?
            .into_iter()
            .flatten()
            .chain(self.useragent_locale.map(str::to_string));

        if locales.clone().count() == 0 {
            return Ok(None);
        }

        // Use the first language that is either the base locale or we can find a langpack extension for
        locales
            .find_map(|lang| {
                if self.base_locales.contains(&lang) {
                    Some(None)
                } else {
                    find_langpack_extension(self.profile_dir, &lang).map(|path| {
                        Some(Langpack {
                            identifier: lang,
                            path,
                        })
                    })
                }
            })
            .with_context(|| {
                format!("couldn't locate langpack for requested locales ({locales:?})")
            })
    }
}

impl Langpack {
    /// Load the localized string bundle for the langpack.
    pub fn load(&self) -> anyhow::Result<LanguageBundle> {
        self.language_info().and_then(LanguageInfo::load_strings)
    }

    /// Read the language info for the langpack.
    fn language_info(&self) -> anyhow::Result<LanguageInfo> {
        let Self { identifier, path } = self;
        let mut zip = read_zip(path)?;

        let ftl_definitions = read_archive_ftl_definitions(&mut zip, identifier)?;
        let ftl_branding = read_archive_ftl_branding(&mut zip, identifier).unwrap_or_else(|e| {
            log::warn!(
                "failed to read branding for {identifier} from {}: {e:#}",
                path.display()
            );
            log::info!("using fallback branding info");
            LanguageInfo::default().ftl_branding
        });

        Ok(LanguageInfo {
            identifier: identifier.clone(),
            ftl_definitions,
            ftl_branding,
        })
    }
}

fn locales_from_prefs(prefs: &Prefs) -> anyhow::Result<Option<Vec<String>>> {
    // Logic dictated by https://firefox-source-docs.mozilla.org/intl/locale.html#requested-locales

    let Some(langs) = prefs.requested_locales() else {
        // If the pref is unset, the installation locale should be used.
        log::debug!(
            "no locale pref set in {}; using installation locale",
            prefs.display_paths()
        );
        return Ok(None);
    };

    Ok(Some(if langs.is_empty() {
        // If the pref is an empty string, use the OS locale settings.
        let os_locales: Vec<String> = sys_locale::get_locales().collect();
        if os_locales.is_empty() {
            log::debug!("no OS locales found");
            return Ok(None);
        }
        log::debug!(
            "locale pref blank in {}; using OS locale settings ({os_locales:?})",
            prefs.display_paths()
        );
        os_locales
    } else {
        langs.into_iter().map(|s| s.to_owned()).collect()
    }))
}

/// Find the langpack extension for the given locale.
fn find_langpack_extension(profile_dir: &Path, locale: &str) -> Option<PathBuf> {
    let path_tail = format!("extensions/langpack-{locale}@firefox.mozilla.org.xpi");

    // Check profile extensions
    let profile_path = profile_dir.join(&path_tail);
    if profile_path.exists() {
        return Some(profile_path);
    }

    // Check installation extensions
    let install_path = installation_resource_path().join(format!("browser/{}", &path_tail));
    if install_path.exists() {
        return Some(install_path);
    }

    None
}

fn read_archive_ftl_definitions(
    langpack: &mut Archive,
    language_identifier: &str,
) -> anyhow::Result<String> {
    let path = format!("localization/{language_identifier}/crashreporter/crashreporter.ftl");
    read_archive_file_as_string(langpack, &path)
}

fn read_archive_ftl_branding(
    langpack: &mut Archive,
    language_identifier: &str,
) -> anyhow::Result<String> {
    let path = format!("browser/localization/{language_identifier}/branding/brand.ftl");
    read_archive_file_as_string(langpack, &path)
}
