mod asynchronous;
mod synchronous;

use crate::{
    env::ErrorReporter,
    errors::L10nRegistrySetupError,
    fluent::FluentBundle,
    source::{FileSource, ResourceId},
};
use fluent_bundle::FluentResource;
use fluent_fallback::generator::BundleGenerator;
use rustc_hash::FxHashSet;
use std::{
    cell::{Ref, RefCell, RefMut},
    collections::BTreeSet,
    rc::Rc,
};
use unic_langid::LanguageIdentifier;

pub use asynchronous::GenerateBundles;
pub use synchronous::GenerateBundlesSync;

pub type FluentResourceSet = Vec<Rc<FluentResource>>;

/// The shared information that makes up the configuration the L10nRegistry. It is
/// broken out into a separate struct so that it can be shared via an Rc pointer.
#[derive(Default)]
struct Shared<P, B> {
    metasources: RefCell<MetaSources>,
    provider: P,
    bundle_adapter: Option<B>,
}

/// [FileSources](FileSource) represent a single directory location to look for .ftl
/// files. These are Stored in a [Vec]. For instance, in a built version of Firefox with
/// the en-US locale, each [FileSource] may represent a different folder with many
/// different files.
///
/// Firefox supports other *meta sources* for localization files in the form of language
/// packs which can be downloaded from the addon store. These language packs then would
/// be a separate metasource than the app' language. This [MetaSources] adds another [Vec]
/// over the [Vec] of [FileSources](FileSource) in order to provide a unified way to
/// iterate over all possible [FileSource] locations to finally obtain the final bundle.
///
/// This structure uses an [Rc] to point to the [FileSource] so that a shallow copy
/// of these [FileSources](FileSource) can be obtained for iteration. This makes
/// it quick to copy the list of [MetaSources] for iteration, and guards against
/// invalidating that async nature of iteration when the underlying data mutates.
///
/// Note that the async iteration of bundles is still only happening in one thread,
/// and is not multi-threaded. The processing is just split over time.
///
/// The [MetaSources] are ultimately owned by the [Shared] in a [RefCell] so that the
/// source of truth can be mutated, and shallow copies of the [MetaSources] used for
/// iteration will be unaffected.
///
/// Deriving [Clone] here is a relatively cheap operation, since the [Rc] will be cloned,
/// and point to the original [FileSource].
#[derive(Default, Clone)]
pub struct MetaSources(Vec<Vec<Rc<FileSource>>>);

impl MetaSources {
    /// Iterate over all FileSources in all MetaSources.
    pub fn filesources(&self) -> impl Iterator<Item = &Rc<FileSource>> {
        self.0.iter().flatten()
    }

    /// Iterate over all FileSources in all MetaSources.
    pub fn iter_mut(&mut self) -> impl Iterator<Item = &mut Vec<Rc<FileSource>>> {
        self.0.iter_mut()
    }

    /// The number of metasources.
    pub fn len(&self) -> usize {
        self.0.len()
    }

    /// If there are no metasources.
    pub fn is_empty(&self) -> bool {
        self.0.is_empty()
    }

    /// Clears out all metasources.
    pub fn clear(&mut self) {
        self.0.clear();
    }

    /// Clears out only empty metasources.
    pub fn clear_empty_metasources(&mut self) {
        self.0.retain(|metasource| !metasource.is_empty());
    }

    /// Adds a [FileSource] to its appropriate metasource.
    pub fn add_filesource(&mut self, new_source: FileSource) {
        if let Some(metasource) = self
            .0
            .iter_mut()
            .find(|source| source[0].metasource == new_source.metasource)
        {
            // A metasource was found, add to the existing one.
            metasource.push(Rc::new(new_source));
        } else {
            // Create a new metasource.
            self.0.push(vec![Rc::new(new_source)]);
        }
    }

    /// Adds a [FileSource] to its appropriate metasource.
    pub fn update_filesource(&mut self, new_source: &FileSource) -> bool {
        if let Some(metasource) = self
            .0
            .iter_mut()
            .find(|source| source[0].metasource == new_source.metasource)
        {
            if let Some(idx) = metasource.iter().position(|source| **source == *new_source) {
                *metasource.get_mut(idx).unwrap() = Rc::new(new_source.clone());
                return true;
            }
        }
        false
    }

    /// Get a metasource by index, but provide a nice error message if the index
    /// is out of bounds.
    pub fn get(&self, metasource_idx: usize) -> &Vec<Rc<FileSource>> {
        if let Some(metasource) = self.0.get(metasource_idx) {
            return &metasource;
        }
        panic!(
            "Metasource index of {} is out of range of the list of {} meta sources.",
            metasource_idx,
            self.0.len()
        );
    }

    /// Get a [FileSource] from a metasource, but provide a nice error message if the
    /// index is out of bounds.
    pub fn filesource(&self, metasource_idx: usize, filesource_idx: usize) -> &FileSource {
        let metasource = self.get(metasource_idx);
        let reversed_idx = metasource.len() - 1 - filesource_idx;
        if let Some(file_source) = metasource.get(reversed_idx) {
            return file_source;
        }
        panic!(
            "File source index of {} is out of range of the list of {} file sources.",
            filesource_idx,
            metasource.len()
        );
    }

    /// Get a [FileSource] by name from a metasource. This is useful for testing.
    #[cfg(feature = "test-fluent")]
    pub fn file_source_by_name(&self, metasource_idx: usize, name: &str) -> Option<&FileSource> {
        use std::borrow::Borrow;
        self.get(metasource_idx)
            .iter()
            .find(|&source| source.name == name)
            .map(|source| source.borrow())
    }

    /// Get an iterator for the [FileSources](FileSource) that match the [LanguageIdentifier]
    /// and [ResourceId].
    #[cfg(feature = "test-fluent")]
    pub fn get_sources_for_resource<'l>(
        &'l self,
        metasource_idx: usize,
        langid: &'l LanguageIdentifier,
        resource_id: &'l ResourceId,
    ) -> impl Iterator<Item = &FileSource> {
        use std::borrow::Borrow;
        self.get(metasource_idx)
            .iter()
            .filter(move |source| source.has_file(langid, resource_id) != Some(false))
            .map(|source| source.borrow())
    }
}

/// The [BundleAdapter] can adapt the bundle to the environment with such actions as
/// setting the platform, and hooking up functions such as Fluent's DATETIME and
/// NUMBER formatting functions.
pub trait BundleAdapter {
    fn adapt_bundle(&self, bundle: &mut FluentBundle);
}

/// The L10nRegistry is the main struct for owning the registry information.
///
/// `P` - A provider
/// `B` - A bundle adapter
#[derive(Clone)]
pub struct L10nRegistry<P, B> {
    shared: Rc<Shared<P, B>>,
}

impl<P, B> L10nRegistry<P, B> {
    /// Create a new [L10nRegistry] from a provider.
    pub fn with_provider(provider: P) -> Self {
        Self {
            shared: Rc::new(Shared {
                metasources: Default::default(),
                provider,
                bundle_adapter: None,
            }),
        }
    }

    /// Set the bundle adapter. See [BundleAdapter] for more information.
    pub fn set_bundle_adapter(&mut self, bundle_adapter: B) -> Result<(), L10nRegistrySetupError>
    where
        B: BundleAdapter,
    {
        let shared = Rc::get_mut(&mut self.shared).ok_or(L10nRegistrySetupError::RegistryLocked)?;
        shared.bundle_adapter = Some(bundle_adapter);
        Ok(())
    }

    pub fn try_borrow_metasources(&self) -> Result<Ref<'_, MetaSources>, L10nRegistrySetupError> {
        self.shared
            .metasources
            .try_borrow()
            .map_err(|_| L10nRegistrySetupError::RegistryLocked)
    }

    pub fn try_borrow_metasources_mut(
        &self,
    ) -> Result<RefMut<'_, MetaSources>, L10nRegistrySetupError> {
        self.shared
            .metasources
            .try_borrow_mut()
            .map_err(|_| L10nRegistrySetupError::RegistryLocked)
    }

    /// Adds a new [FileSource] to the registry and to its appropriate metasource. If the
    /// metasource for this [FileSource] does not exist, then it is created.
    pub fn register_sources(
        &self,
        new_sources: Vec<FileSource>,
    ) -> Result<(), L10nRegistrySetupError> {
        for new_source in new_sources {
            self.try_borrow_metasources_mut()?
                .add_filesource(new_source);
        }
        Ok(())
    }

    /// Update the information about sources already stored in the registry. Each
    /// [FileSource] provided must exist, or else a [L10nRegistrySetupError] will
    /// be returned.
    pub fn update_sources(
        &self,
        new_sources: Vec<FileSource>,
    ) -> Result<(), L10nRegistrySetupError> {
        for new_source in new_sources {
            if !self
                .try_borrow_metasources_mut()?
                .update_filesource(&new_source)
            {
                return Err(L10nRegistrySetupError::MissingSource {
                    name: new_source.name,
                });
            }
        }
        Ok(())
    }

    /// Remove the provided sources. If a metasource becomes empty after this operation,
    /// the metasource is also removed.
    pub fn remove_sources<S>(&self, del_sources: Vec<S>) -> Result<(), L10nRegistrySetupError>
    where
        S: ToString,
    {
        let del_sources: Vec<String> = del_sources.into_iter().map(|s| s.to_string()).collect();

        for metasource in self.try_borrow_metasources_mut()?.iter_mut() {
            metasource.retain(|source| !del_sources.contains(&source.name));
        }

        self.try_borrow_metasources_mut()?.clear_empty_metasources();

        Ok(())
    }

    /// Clears out all metasources and sources.
    pub fn clear_sources(&self) -> Result<(), L10nRegistrySetupError> {
        self.try_borrow_metasources_mut()?.clear();
        Ok(())
    }

    /// Flattens out all metasources and returns the complete list of source names.
    pub fn get_source_names(&self) -> Result<Vec<String>, L10nRegistrySetupError> {
        Ok(self
            .try_borrow_metasources()?
            .filesources()
            .map(|s| s.name.clone())
            .collect())
    }

    /// Checks if any metasources has a source, by the name.
    pub fn has_source(&self, name: &str) -> Result<bool, L10nRegistrySetupError> {
        Ok(self
            .try_borrow_metasources()?
            .filesources()
            .any(|source| source.name == name))
    }

    /// Get a [FileSource] by name by searching through all meta sources.
    pub fn file_source_by_name(
        &self,
        name: &str,
    ) -> Result<Option<FileSource>, L10nRegistrySetupError> {
        Ok(self
            .try_borrow_metasources()?
            .filesources()
            .find(|source| source.name == name)
            .map(|source| (**source).clone()))
    }

    /// Returns a sorted, unique list of locale names from all sources.
    pub fn get_available_locales(&self) -> Result<Vec<LanguageIdentifier>, L10nRegistrySetupError> {
        let mut result = BTreeSet::new();
        let metasources = self.try_borrow_metasources()?;
        for source in metasources.filesources() {
            for locale in source.locales() {
                result.insert(locale);
            }
        }
        Ok(result.into_iter().map(|l| l.to_owned()).collect())
    }
}

/// Defines how to generate bundles synchronously and asynchronously.
impl<P, B> BundleGenerator for L10nRegistry<P, B>
where
    P: ErrorReporter + Clone,
    B: BundleAdapter + Clone,
{
    type Resource = Rc<FluentResource>;
    type Iter = GenerateBundlesSync<P, B>;
    type Stream = GenerateBundles<P, B>;
    type LocalesIter = std::vec::IntoIter<LanguageIdentifier>;

    /// The synchronous version of the bundle generator. This is hooked into Gecko
    /// code via the `l10nregistry_generate_bundles_sync` function.
    fn bundles_iter(
        &self,
        locales: Self::LocalesIter,
        resource_ids: FxHashSet<ResourceId>,
    ) -> Self::Iter {
        let resource_ids = resource_ids.into_iter().collect();
        self.generate_bundles_sync(locales, resource_ids)
    }

    /// The asynchronous version of the bundle generator. This is hooked into Gecko
    /// code via the `l10nregistry_generate_bundles` function.
    fn bundles_stream(
        &self,
        locales: Self::LocalesIter,
        resource_ids: FxHashSet<ResourceId>,
    ) -> Self::Stream {
        let resource_ids = resource_ids.into_iter().collect();
        self.generate_bundles(locales, resource_ids)
            .expect("Unable to get the MetaSources.")
    }
}
