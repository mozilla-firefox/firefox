/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */
use crate::defaults::{DefaultsHasher, DefaultsMerger, DefaultsValidator};
use crate::error::FMLError::InvalidFeatureError;
use crate::error::{FMLError, Result};
use crate::frontend::{
    AboutBlock, ExampleBlock, FeatureExampleMetadata, FeatureMetadata, InlineExampleBlock,
};
use crate::schema::{SchemaHasher, SchemaValidator, TypeQuery};
use crate::util::loaders::FilePath;
use anyhow::{bail, Error, Result as AnyhowResult};
use serde::{Deserialize, Serialize};
use serde_json::{Map, Value};
use std::collections::{BTreeMap, BTreeSet, HashMap, HashSet};
use std::fmt::{Display, Formatter};

#[derive(Eq, PartialEq, Hash, Debug, Clone)]
pub enum TargetLanguage {
    Kotlin,
    Swift,
    IR,
    ExperimenterYAML,
    ExperimenterJSON,
}

impl TargetLanguage {
    pub fn extension(&self) -> &str {
        match self {
            TargetLanguage::Kotlin => "kt",
            TargetLanguage::Swift => "swift",
            TargetLanguage::IR => "ir.json",
            TargetLanguage::ExperimenterJSON => "json",
            TargetLanguage::ExperimenterYAML => "yaml",
        }
    }

    pub fn from_extension(path: &str) -> AnyhowResult<TargetLanguage> {
        if let Some((_, extension)) = path.rsplit_once('.') {
            extension.try_into()
        } else {
            bail!("Unknown or unsupported target language: \"{}\"", path)
        }
    }
}

impl TryFrom<&str> for TargetLanguage {
    type Error = Error;
    fn try_from(value: &str) -> AnyhowResult<Self> {
        Ok(match value.to_ascii_lowercase().as_str() {
            "kotlin" | "kt" | "kts" => TargetLanguage::Kotlin,
            "swift" => TargetLanguage::Swift,
            "ir.json" => TargetLanguage::IR,
            "yaml" => TargetLanguage::ExperimenterYAML,
            "json" => TargetLanguage::ExperimenterJSON,
            _ => bail!("Unknown or unsupported target language: \"{}\"", value),
        })
    }
}

/// The `TypeRef` enum defines a reference to a type.
///
/// Other types will be defined in terms of these enum values.
///
/// They represent the types available via the current `Variables` API—
/// some primitives and structural types— and can be represented by
/// Kotlin, Swift and JSON Schema.
///
#[non_exhaustive]
#[derive(Debug, Clone, PartialEq, Deserialize, Serialize, Hash, Eq)]
pub enum TypeRef {
    // Current primitives.
    String,
    Int,
    Boolean,

    // String-alias
    StringAlias(String),

    // Strings can be coerced into a few types.
    // The types here will require the app's bundle or context to look
    // up the final value.
    BundleText,
    BundleImage,

    Enum(String),
    // JSON objects can represent a data class.
    Object(String),

    // JSON objects can also represent a `Map<String, V>` or a `Map` with
    // keys that can be derived from a string.
    StringMap(Box<TypeRef>),
    // We can coerce the String keys into Enums, so this represents that.
    EnumMap(Box<TypeRef>, Box<TypeRef>),

    List(Box<TypeRef>),
    Option(Box<TypeRef>),
}

impl Display for TypeRef {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::String => f.write_str("String"),
            Self::Int => f.write_str("Int"),
            Self::Boolean => f.write_str("Boolean"),
            Self::BundleImage => f.write_str("Image"),
            Self::BundleText => f.write_str("Text"),
            Self::StringAlias(v) => f.write_str(v),
            Self::Enum(v) => f.write_str(v),
            Self::Object(v) => f.write_str(v),
            Self::Option(v) => f.write_fmt(format_args!("Option<{v}>")),
            Self::List(v) => f.write_fmt(format_args!("List<{v}>")),
            Self::StringMap(v) => f.write_fmt(format_args!("Map<String, {v}>")),
            Self::EnumMap(k, v) => f.write_fmt(format_args!("Map<{k}, {v}>")),
        }
    }
}

impl TypeRef {
    pub(crate) fn supports_prefs(&self) -> bool {
        match self {
            Self::Boolean | Self::String | Self::Int | Self::StringAlias(_) | Self::BundleText => {
                true
            }
            // There may be a chance that we can get Self::Option to work, but not at this time.
            // This may be done by adding a branch to this match and adding a `preference_getter` to
            // the `OptionalCodeType`.
            _ => false,
        }
    }

    pub(crate) fn supports_gecko_prefs(&self, lax_pref_validation: bool) -> bool {
        match self {
            Self::Option(boxed) => matches!(**boxed, Self::Boolean | Self::Int | Self::String),
            Self::Boolean | Self::Int | Self::String => lax_pref_validation,
            _ => false,
        }
    }

    pub(crate) fn name(&self) -> Option<&str> {
        match self {
            Self::Enum(s) | Self::Object(s) | Self::StringAlias(s) => Some(s),
            _ => None,
        }
    }
}

/**
 * An identifier derived from a `FilePath` of a top-level or importable FML file.
 *
 * An FML module is the conceptual FML file (and included FML files) that a single
 * Kotlin or Swift file. It can be imported by other FML modules.
 *
 * It is somewhat distinct from the `FilePath` enum for three reasons:
 *
 * - a file path can specify a non-canonical representation of the path
 * - a file path is difficult to serialize/deserialize
 * - a module identifies the cluster of FML files that map to a single generated
 *   Kotlin or Swift file; this difference can be seen as: files can be included,
 *   modules can be imported.
 */
#[derive(PartialEq, Eq, PartialOrd, Ord, Debug, Hash, Clone, Serialize, Deserialize)]
pub enum ModuleId {
    Local(String),
    Remote(String),
}

impl Default for ModuleId {
    fn default() -> Self {
        Self::Local("none".to_string())
    }
}

impl TryFrom<&FilePath> for ModuleId {
    type Error = FMLError;
    fn try_from(path: &FilePath) -> Result<Self> {
        Ok(match path {
            FilePath::Local(p) => {
                // We do this map_err here because the IO Error message that comes out of `canonicalize`
                // doesn't include the problematic file path.
                let p = p.canonicalize().map_err(|e| {
                    FMLError::InvalidPath(format!("{}: {}", e, p.as_path().display()))
                })?;
                ModuleId::Local(p.display().to_string())
            }
            FilePath::Remote(u) => ModuleId::Remote(u.to_string()),
            FilePath::GitHub(p) => ModuleId::Remote(p.default_download_url_as_str()),
        })
    }
}

impl Display for ModuleId {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(match self {
            ModuleId::Local(s) | ModuleId::Remote(s) => s,
        })
    }
}

pub trait TypeFinder {
    fn all_types(&self) -> HashSet<TypeRef> {
        let mut types = HashSet::new();
        self.find_types(&mut types);
        types
    }

    fn find_types(&self, types: &mut HashSet<TypeRef>);
}

impl TypeFinder for TypeRef {
    fn find_types(&self, types: &mut HashSet<TypeRef>) {
        if types.insert(self.clone()) {
            match self {
                TypeRef::List(v) | TypeRef::Option(v) | TypeRef::StringMap(v) => {
                    v.find_types(types)
                }
                TypeRef::EnumMap(k, v) => {
                    k.find_types(types);
                    v.find_types(types);
                }
                _ => {}
            }
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq, Default)]
pub struct FeatureManifest {
    #[serde(skip)]
    pub(crate) id: ModuleId,

    #[serde(skip_serializing_if = "Option::is_none")]
    #[serde(default)]
    pub(crate) channel: Option<String>,

    #[serde(rename = "enums")]
    #[serde(default)]
    pub(crate) enum_defs: BTreeMap<String, EnumDef>,
    #[serde(rename = "objects")]
    #[serde(default)]
    pub(crate) obj_defs: BTreeMap<String, ObjectDef>,
    #[serde(rename = "features")]
    pub(crate) feature_defs: BTreeMap<String, FeatureDef>,
    #[serde(default)]
    pub(crate) about: AboutBlock,

    #[serde(default)]
    pub(crate) imported_features: BTreeMap<ModuleId, BTreeSet<String>>,

    #[serde(default)]
    pub(crate) all_imports: BTreeMap<ModuleId, FeatureManifest>,
}

impl TypeFinder for FeatureManifest {
    fn find_types(&self, types: &mut HashSet<TypeRef>) {
        for e in self.enum_defs.values() {
            e.find_types(types);
        }
        for o in self.iter_object_defs() {
            o.find_types(types);
        }
        for f in self.iter_feature_defs() {
            f.find_types(types);
        }
    }
}

#[cfg(test)]
impl FeatureManifest {
    pub(crate) fn add_feature(&mut self, feature: FeatureDef) {
        self.feature_defs.insert(feature.name(), feature);
    }
}

impl FeatureManifest {
    pub(crate) fn new(
        id: ModuleId,
        channel: Option<&str>,
        features: BTreeMap<String, FeatureDef>,
        enums: BTreeMap<String, EnumDef>,
        objects: BTreeMap<String, ObjectDef>,
        about: AboutBlock,
    ) -> Self {
        Self {
            id,
            channel: channel.map(str::to_string),
            about,
            enum_defs: enums,
            obj_defs: objects,
            feature_defs: features,

            ..Default::default()
        }
    }

    #[allow(unused)]
    pub(crate) fn validate_manifest_for_lang(&self, lang: &TargetLanguage) -> Result<()> {
        if !&self.about.supports(lang) {
            return Err(FMLError::ValidationError(
                "about".to_string(),
                format!(
                    "Manifest file {file} is unable to generate {lang} files",
                    file = &self.id,
                    lang = &lang.extension(),
                ),
            ));
        }
        for child in self.all_imports.values() {
            child.validate_manifest_for_lang(lang)?;
        }
        Ok(())
    }

    #[allow(dead_code)]
    pub fn validate_manifest(&self) -> Result<()> {
        self.validate_manifest_with(false)
    }

    pub fn validate_manifest_with(&self, lax_gecko_pref_validation: bool) -> Result<()> {
        // We then validate that each type_ref is valid
        self.validate_schema_with(lax_gecko_pref_validation)?;
        self.validate_defaults_with(lax_gecko_pref_validation)?;

        // Validating the imported manifests.
        // This is not only validating the well formed-ness of the imported manifests
        // but also the defaults that are sent into the child manifests.
        for child in self.all_imports.values() {
            child.validate_manifest_with(lax_gecko_pref_validation)?;
        }
        Ok(())
    }

    #[allow(dead_code)]
    fn validate_schema(&self) -> Result<(), FMLError> {
        self.validate_schema_with(false)
    }

    fn validate_schema_with(&self, lax_gecko_pref_validation: bool) -> Result<(), FMLError> {
        let validator = SchemaValidator::new(&self.enum_defs, &self.obj_defs)
            .with_lax_gecko_pref_validation(lax_gecko_pref_validation);
        for object in self.iter_object_defs() {
            validator.validate_object_def(object)?;
        }
        for feature_def in self.iter_feature_defs() {
            validator.validate_feature_def(feature_def)?;
        }
        validator.validate_prefs(self)?;
        Ok(())
    }

    #[allow(dead_code)]
    fn validate_defaults(&self) -> Result<()> {
        self.validate_defaults_with(false)
    }

    fn validate_defaults_with(&self, lax_gecko_pref_validation: bool) -> Result<()> {
        let validator = DefaultsValidator::new(&self.enum_defs, &self.obj_defs)
            .with_lax_gecko_pref_validation(lax_gecko_pref_validation);
        for object in self.iter_object_defs() {
            validator.validate_object_def(object)?;
        }
        for feature in self.iter_feature_defs() {
            validator.validate_feature_def(feature)?;
        }
        Ok(())
    }

    pub fn iter_enum_defs(&self) -> impl Iterator<Item = &EnumDef> {
        self.enum_defs.values()
    }

    pub fn iter_all_enum_defs(&self) -> impl Iterator<Item = (&FeatureManifest, &EnumDef)> {
        let enums = self.iter_enum_defs().map(move |o| (self, o));
        let imported: Vec<_> = self
            .all_imports
            .values()
            .flat_map(|fm| fm.iter_all_enum_defs())
            .collect();
        enums.chain(imported)
    }

    pub fn iter_object_defs(&self) -> impl Iterator<Item = &ObjectDef> {
        self.obj_defs.values()
    }

    pub fn iter_all_object_defs(&self) -> impl Iterator<Item = (&FeatureManifest, &ObjectDef)> {
        let objects = self.iter_object_defs().map(move |o| (self, o));
        let imported: Vec<_> = self
            .all_imports
            .values()
            .flat_map(|fm| fm.iter_all_object_defs())
            .collect();
        objects.chain(imported)
    }

    pub fn iter_feature_defs(&self) -> impl Iterator<Item = &FeatureDef> {
        self.feature_defs.values()
    }

    pub fn iter_gecko_prefs(&self) -> impl Iterator<Item = &GeckoPrefDef> {
        self.iter_feature_defs()
            .filter(|f| f.has_gecko_prefs())
            .flat_map(|f| {
                f.feature_mapped_to_prop_and_gecko_pref()
                    .iter()
                    .flat_map(|p| p.1.clone())
                    .collect::<Vec<_>>()
            })
            .map(|p| p.1)
    }

    pub fn iter_features_with_prefs(
        &self,
    ) -> impl Iterator<Item = (String, Vec<(String, &GeckoPrefDef)>)> {
        self.iter_feature_defs()
            .filter(|f| f.has_gecko_prefs())
            .flat_map(|f| f.feature_mapped_to_prop_and_gecko_pref())
    }

    pub fn iter_all_feature_defs(&self) -> impl Iterator<Item = (&FeatureManifest, &FeatureDef)> {
        let features = self.iter_feature_defs().map(move |f| (self, f));
        let imported: Vec<_> = self
            .all_imports
            .values()
            .flat_map(|fm| fm.iter_all_feature_defs())
            .collect();
        features.chain(imported)
    }

    #[allow(unused)]
    pub(crate) fn iter_imported_files(&self) -> Vec<ImportedModule<'_>> {
        let map = &self.all_imports;

        self.imported_features
            .iter()
            .filter_map(|(id, features)| {
                let fm = map.get(id).to_owned()?;
                Some(ImportedModule::new(fm, features))
            })
            .collect()
    }

    pub fn find_object(&self, nm: &str) -> Option<&ObjectDef> {
        self.obj_defs.get(nm)
    }

    pub fn find_enum(&self, nm: &str) -> Option<&EnumDef> {
        self.enum_defs.get(nm)
    }

    pub fn get_feature(&self, nm: &str) -> Option<&FeatureDef> {
        self.feature_defs.get(nm)
    }

    pub fn get_coenrolling_feature_ids(&self) -> Vec<String> {
        self.iter_all_feature_defs()
            .filter(|(_, f)| f.allow_coenrollment())
            .map(|(_, f)| f.name())
            .collect()
    }

    pub fn find_feature(&self, nm: &str) -> Option<(&FeatureManifest, &FeatureDef)> {
        if let Some(f) = self.get_feature(nm) {
            Some((self, f))
        } else {
            self.all_imports.values().find_map(|fm| fm.find_feature(nm))
        }
    }

    pub fn find_import(&self, id: &ModuleId) -> Option<&FeatureManifest> {
        self.all_imports.get(id)
    }

    pub fn default_json(&self) -> Value {
        Value::Object(
            self.iter_all_feature_defs()
                .map(|(_, f)| (f.name(), f.default_json()))
                .collect(),
        )
    }

    /// This function is used to validate a new value for a feature. It accepts a feature name and
    /// a feature value, and returns a Result containing a FeatureDef.
    ///
    /// If the value is invalid for the feature, it will return an Err result.
    ///
    /// If the value is valid for the feature, it will return an Ok result with a new FeatureDef
    /// with the supplied feature value applied to the feature's property defaults.
    pub fn validate_feature_config(
        &self,
        feature_name: &str,
        feature_value: Value,
    ) -> Result<FeatureDef> {
        let (manifest, feature_def) = self
            .find_feature(feature_name)
            .ok_or_else(|| InvalidFeatureError(feature_name.to_string()))?;

        let merger = DefaultsMerger::new(&manifest.obj_defs, Default::default(), None);
        let merged_value = merger.merge_feature_config(feature_def, &feature_value);

        let validator = DefaultsValidator::new(&manifest.enum_defs, &manifest.obj_defs);
        let errors = validator.get_errors(feature_def, &merged_value, &feature_value);
        validator.guard_errors(feature_def, &merged_value, errors)?;

        let mut feature_def = feature_def.clone();
        merger.overwrite_defaults(&mut feature_def, &merged_value);
        Ok(feature_def)
    }

    #[allow(dead_code)]
    #[cfg(feature = "client-lib")]
    pub(crate) fn merge_and_errors(
        &self,
        feature_def: &FeatureDef,
        feature_value: &Value,
    ) -> (Value, Vec<crate::editing::FeatureValidationError>) {
        let merger = DefaultsMerger::new(&self.obj_defs, Default::default(), None);
        let merged_value = merger.merge_feature_config(feature_def, feature_value);

        let validator = DefaultsValidator::new(&self.enum_defs, &self.obj_defs);
        let errors = validator.get_errors(feature_def, &merged_value, feature_value);
        (merged_value, errors)
    }
}

impl FeatureManifest {
    pub(crate) fn feature_types(&self, feature_def: &FeatureDef) -> HashSet<TypeRef> {
        TypeQuery::new(&self.obj_defs).all_types(feature_def)
    }

    pub(crate) fn feature_schema_hash(&self, feature_def: &FeatureDef) -> String {
        let hasher = SchemaHasher::new(&self.enum_defs, &self.obj_defs);
        let hash = hasher.hash(feature_def) & 0xffffffff;
        format!("{hash:x}")
    }

    pub(crate) fn feature_defaults_hash(&self, feature_def: &FeatureDef) -> String {
        let hasher = DefaultsHasher::new(&self.obj_defs);
        let hash = hasher.hash(feature_def) & 0xffffffff;
        format!("{hash:x}")
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq, Default)]
pub struct FeatureDef {
    pub(crate) name: String,
    #[serde(flatten)]
    pub(crate) metadata: FeatureMetadata,
    pub(crate) props: Vec<PropDef>,
    pub(crate) allow_coenrollment: bool,
    #[serde(default)]
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub(crate) examples: Vec<FeatureExample>,
}

impl FeatureDef {
    pub fn new(name: &str, doc: &str, props: Vec<PropDef>, allow_coenrollment: bool) -> Self {
        Self {
            name: name.into(),
            metadata: FeatureMetadata {
                description: doc.into(),
                ..Default::default()
            },
            props,
            allow_coenrollment,
            ..Default::default()
        }
    }
    pub fn name(&self) -> String {
        self.name.clone()
    }
    pub fn doc(&self) -> String {
        self.metadata.description.clone()
    }
    pub fn props(&self) -> Vec<PropDef> {
        self.props.clone()
    }
    pub fn allow_coenrollment(&self) -> bool {
        self.allow_coenrollment
    }

    pub fn default_json(&self) -> Value {
        let mut props = Map::new();

        for prop in self.props().iter() {
            props.insert(prop.name(), prop.default());
        }

        Value::Object(props)
    }

    pub fn has_prefs(&self) -> bool {
        self.props.iter().any(|p| p.has_prefs())
    }

    pub fn has_gecko_prefs(&self) -> bool {
        self.props.iter().any(|p| p.has_gecko_prefs())
    }

    pub fn get_string_aliases(&self) -> HashMap<&str, &PropDef> {
        let mut res: HashMap<_, _> = Default::default();
        for p in &self.props {
            if let Some(TypeRef::StringAlias(s)) = &p.string_alias {
                res.insert(s.as_str(), p);
            }
        }
        res
    }

    pub fn get_prop(&self, name: &str) -> Option<&PropDef> {
        self.props.iter().find(|p| p.name == name)
    }

    pub fn feature_mapped_to_prop_and_gecko_pref(
        &self,
    ) -> Vec<(String, Vec<(String, &GeckoPrefDef)>)> {
        self.props
            .iter()
            .filter(|p| p.has_gecko_prefs() && p.gecko_pref.is_some())
            .map(|p| (p.gecko_pref.as_ref(), p.name()))
            .map(|(p, n)| (p, (n, self.name())))
            .rfold(Vec::new(), |mut acc: Vec<(String, Vec<(String, &GeckoPrefDef)>)>, (pref, (prop, feature)): (Option<&GeckoPrefDef>, (String, String))| {
                match acc.iter_mut().find(|p| p.0 == feature) {
                    Some((_, ref mut props)) => {
                        props.push((prop, pref.unwrap()));
                    }
                    None => {
                        let props = vec![(prop, pref.unwrap())];
                        acc.push((feature, props));
                    }
                }
                acc
            })
    }
}

impl TypeFinder for FeatureDef {
    fn find_types(&self, types: &mut HashSet<TypeRef>) {
        for p in self.props() {
            p.find_types(types);
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq, Default)]
pub struct EnumDef {
    pub name: String,
    pub doc: String,
    pub variants: Vec<VariantDef>,
}

impl EnumDef {
    pub fn name(&self) -> String {
        self.name.clone()
    }
    pub fn doc(&self) -> String {
        self.doc.clone()
    }
    pub fn variants(&self) -> Vec<VariantDef> {
        self.variants.clone()
    }
}

impl TypeFinder for EnumDef {
    fn find_types(&self, types: &mut HashSet<TypeRef>) {
        types.insert(TypeRef::Enum(self.name()));
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq, Default)]
pub struct VariantDef {
    pub(crate) name: String,
    pub(crate) doc: String,
}
impl VariantDef {
    pub fn new(name: &str, doc: &str) -> Self {
        Self {
            name: name.into(),
            doc: doc.into(),
        }
    }
    pub fn name(&self) -> String {
        self.name.clone()
    }
    pub fn doc(&self) -> String {
        self.doc.clone()
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq, Default)]
pub struct ObjectDef {
    pub(crate) name: String,
    pub(crate) doc: String,
    pub(crate) props: Vec<PropDef>,
}

impl ObjectDef {
    pub(crate) fn name(&self) -> String {
        self.name.clone()
    }
    pub(crate) fn doc(&self) -> String {
        self.doc.clone()
    }
    pub fn props(&self) -> Vec<PropDef> {
        self.props.clone()
    }

    pub(crate) fn find_prop(&self, nm: &str) -> PropDef {
        self.props
            .iter()
            .find(|p| p.name == nm)
            .unwrap_or_else(|| unreachable!("Can't find {}. This is a bug in FML", nm))
            .clone()
    }
}
impl TypeFinder for ObjectDef {
    fn find_types(&self, types: &mut HashSet<TypeRef>) {
        for p in self.props() {
            p.find_types(types);
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "lowercase")]
pub enum PrefBranch {
    Default,
    User,
}

impl Display for PrefBranch {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        match self {
            PrefBranch::Default => f.write_str("default"),
            PrefBranch::User => f.write_str("user"),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct GeckoPrefDef {
    pub(crate) pref: String,
    pub(crate) branch: PrefBranch,
}

impl GeckoPrefDef {
    pub fn pref(&self) -> String {
        self.pref.clone()
    }
    pub fn branch(&self) -> PrefBranch {
        self.branch.clone()
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct PropDef {
    pub(crate) name: String,
    pub(crate) doc: String,
    #[serde(rename = "type")]
    pub(crate) typ: TypeRef,
    pub(crate) default: Literal,
    #[serde(default)]
    #[serde(skip_serializing_if = "Option::is_none")]
    pub(crate) pref_key: Option<String>,
    #[serde(default)]
    #[serde(skip_serializing_if = "Option::is_none")]
    pub(crate) gecko_pref: Option<GeckoPrefDef>,
    #[serde(default)]
    #[serde(skip_serializing_if = "Option::is_none")]
    pub(crate) string_alias: Option<TypeRef>,
}

impl PropDef {
    pub fn name(&self) -> String {
        self.name.clone()
    }
    pub fn doc(&self) -> String {
        self.doc.clone()
    }
    pub fn typ(&self) -> TypeRef {
        self.typ.clone()
    }
    pub fn default(&self) -> Literal {
        self.default.clone()
    }
    pub fn has_prefs(&self) -> bool {
        self.pref_key.is_some() && self.typ.supports_prefs()
    }
    pub fn has_gecko_prefs(&self) -> bool {
        self.gecko_pref.is_some() && self.typ.supports_gecko_prefs(false)
    }
    pub fn pref_key(&self) -> Option<String> {
        self.pref_key.clone()
    }
    pub fn gecko_pref(&self) -> Option<GeckoPrefDef> {
        self.gecko_pref.clone()
    }
}

impl TypeFinder for PropDef {
    fn find_types(&self, types: &mut HashSet<TypeRef>) {
        self.typ.find_types(types);
    }
}

pub type Literal = Value;

#[derive(Debug, Clone)]
pub(crate) struct ImportedModule<'a> {
    pub(crate) fm: &'a FeatureManifest,
    features: &'a BTreeSet<String>,
}

impl<'a> ImportedModule<'a> {
    pub(crate) fn new(fm: &'a FeatureManifest, features: &'a BTreeSet<String>) -> Self {
        Self { fm, features }
    }

    pub(crate) fn about(&self) -> &AboutBlock {
        &self.fm.about
    }

    pub(crate) fn features(&self) -> Vec<&'a FeatureDef> {
        let fm = self.fm;
        self.features
            .iter()
            .filter_map(|f| fm.get_feature(f))
            .collect()
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, Default, PartialEq, Eq)]
pub(crate) struct FeatureExample {
    pub(crate) metadata: FeatureExampleMetadata,
    pub(crate) value: Value,
}

impl From<&ExampleBlock> for FeatureExample {
    fn from(value: &ExampleBlock) -> Self {
        match value {
            ExampleBlock::Inline(InlineExampleBlock { metadata, value }) => Self {
                metadata: metadata.to_owned(),
                value: value.to_owned(),
            },
            _ => unreachable!(
                "Examples should have been inlined by now. This is a bug in nimbus-fml"
            ),
        }
    }
}

#[cfg(test)]
pub mod unit_tests {
    use serde_json::json;

    use super::*;
    use crate::error::Result;
    use crate::fixtures::intermediate_representation::get_simple_homescreen_feature;

    #[test]
    fn can_ir_represent_smoke_test() -> Result<()> {
        let reference_manifest = get_simple_homescreen_feature();
        let json_string = serde_json::to_string(&reference_manifest)?;
        let manifest_from_json: FeatureManifest = serde_json::from_str(&json_string)?;

        assert_eq!(reference_manifest, manifest_from_json);

        Ok(())
    }

    #[test]
    fn validate_good_feature_manifest() -> Result<()> {
        let fm = get_simple_homescreen_feature();
        fm.validate_manifest()
    }

    #[test]
    fn validate_allow_coenrollment() -> Result<()> {
        let mut fm = get_simple_homescreen_feature();
        fm.add_feature(FeatureDef::new(
            "some_def",
            "my lovely qtest doc",
            vec![PropDef::new(
                "some prop",
                &TypeRef::String,
                &json!("default"),
            )],
            true,
        ));
        fm.validate_manifest()?;
        let coenrolling_ids = fm.get_coenrolling_feature_ids();
        assert_eq!(coenrolling_ids, vec!["some_def".to_string()]);

        Ok(())
    }
}

#[cfg(test)]
mod imports_tests {
    use super::*;

    use serde_json::json;

    use crate::fixtures::intermediate_representation::{
        get_feature_manifest, get_one_prop_feature_manifest,
        get_one_prop_feature_manifest_with_imports,
    };

    #[test]
    fn test_iter_object_defs_deep_iterates_on_all_imports() -> Result<()> {
        let prop_i = PropDef::new(
            "key_i",
            &TypeRef::Object("SampleObjImported".into()),
            &json!({
                "string": "bobo",
            }),
        );
        let obj_defs_i = vec![ObjectDef::new(
            "SampleObjImported",
            &[PropDef::new("string", &TypeRef::String, &json!("a string"))],
        )];
        let fm_i = get_one_prop_feature_manifest(obj_defs_i, vec![], &prop_i);

        let prop = PropDef::new(
            "key",
            &TypeRef::Object("SampleObj".into()),
            &json!({
                "string": "bobo",
            }),
        );
        let obj_defs = vec![ObjectDef::new(
            "SampleObj",
            &[PropDef::new("string", &TypeRef::String, &json!("a string"))],
        )];
        let fm = get_one_prop_feature_manifest_with_imports(
            obj_defs,
            vec![],
            &prop,
            BTreeMap::from([(ModuleId::Local("test".into()), fm_i)]),
        );

        let names: Vec<String> = fm.iter_all_object_defs().map(|(_, o)| o.name()).collect();

        assert_eq!(names[0], "SampleObj".to_string());
        assert_eq!(names[1], "SampleObjImported".to_string());

        Ok(())
    }

    #[test]
    fn test_iter_feature_defs_deep_iterates_on_all_imports() -> Result<()> {
        let prop_i = PropDef::new("key_i", &TypeRef::String, &json!("string"));
        let fm_i = get_one_prop_feature_manifest(vec![], vec![], &prop_i);

        let prop = PropDef::new("key", &TypeRef::String, &json!("string"));
        let fm = get_one_prop_feature_manifest_with_imports(
            vec![],
            vec![],
            &prop,
            BTreeMap::from([(ModuleId::Local("test".into()), fm_i)]),
        );

        let names: Vec<String> = fm
            .iter_all_feature_defs()
            .map(|(_, f)| f.props[0].name())
            .collect();

        assert_eq!(names[0], "key".to_string());
        assert_eq!(names[1], "key_i".to_string());

        Ok(())
    }

    #[test]
    fn test_find_feature_deep_finds_across_all_imports() -> Result<()> {
        let fm_i = get_feature_manifest(
            vec![],
            vec![],
            vec![FeatureDef {
                name: "feature_i".into(),
                ..Default::default()
            }],
            BTreeMap::new(),
        );

        let fm = get_feature_manifest(
            vec![],
            vec![],
            vec![FeatureDef {
                name: "feature".into(),
                ..Default::default()
            }],
            BTreeMap::from([(ModuleId::Local("test".into()), fm_i)]),
        );

        let feature = fm.find_feature("feature_i");

        assert!(feature.is_some());

        Ok(())
    }

    #[test]
    fn test_get_coenrolling_feature_finds_across_all_imports() -> Result<()> {
        let fm_i = get_feature_manifest(
            vec![],
            vec![],
            vec![
                FeatureDef {
                    name: "coenrolling_import_1".into(),
                    allow_coenrollment: true,
                    ..Default::default()
                },
                FeatureDef {
                    name: "coenrolling_import_2".into(),
                    allow_coenrollment: true,
                    ..Default::default()
                },
            ],
            BTreeMap::new(),
        );

        let fm = get_feature_manifest(
            vec![],
            vec![],
            vec![
                FeatureDef {
                    name: "coenrolling_feature".into(),
                    allow_coenrollment: true,
                    ..Default::default()
                },
                FeatureDef {
                    name: "non_coenrolling_feature".into(),
                    allow_coenrollment: false,
                    ..Default::default()
                },
            ],
            BTreeMap::from([(ModuleId::Local("test".into()), fm_i)]),
        );

        let coenrolling_features = fm.get_coenrolling_feature_ids();
        let expected = vec![
            "coenrolling_feature".to_string(),
            "coenrolling_import_1".to_string(),
            "coenrolling_import_2".to_string(),
        ];

        assert_eq!(coenrolling_features, expected);

        Ok(())
    }

    #[test]
    fn test_no_coenrolling_feature_finds_across_all_imports() -> Result<()> {
        let fm_i = get_feature_manifest(
            vec![],
            vec![],
            vec![FeatureDef {
                name: "not_coenrolling_import".into(),
                allow_coenrollment: false,
                ..Default::default()
            }],
            BTreeMap::new(),
        );

        let fm = get_feature_manifest(
            vec![],
            vec![],
            vec![
                FeatureDef {
                    name: "non_coenrolling_feature_1".into(),
                    allow_coenrollment: false,
                    ..Default::default()
                },
                FeatureDef {
                    name: "non_coenrolling_feature_2".into(),
                    allow_coenrollment: false,
                    ..Default::default()
                },
            ],
            BTreeMap::from([(ModuleId::Local("test".into()), fm_i)]),
        );

        let coenrolling_features = fm.get_coenrolling_feature_ids();
        let expected: Vec<String> = vec![];

        assert_eq!(coenrolling_features, expected);

        Ok(())
    }

    #[test]
    fn test_default_json_works_across_all_imports() -> Result<()> {
        let fm_i = get_feature_manifest(
            vec![],
            vec![],
            vec![FeatureDef {
                name: "feature_i".into(),
                props: vec![PropDef::new(
                    "prop_i_1",
                    &TypeRef::String,
                    &json!("prop_i_1_value"),
                )],
                ..Default::default()
            }],
            BTreeMap::new(),
        );

        let fm = get_feature_manifest(
            vec![],
            vec![],
            vec![FeatureDef {
                name: "feature".into(),
                props: vec![PropDef::new(
                    "prop_1",
                    &TypeRef::String,
                    &json!("prop_1_value"),
                )],
                ..Default::default()
            }],
            BTreeMap::from([(ModuleId::Local("test".into()), fm_i)]),
        );

        let json = fm.default_json();
        assert_eq!(
            json.get("feature_i").unwrap().get("prop_i_1").unwrap(),
            &json!("prop_i_1_value")
        );
        assert_eq!(
            json.get("feature").unwrap().get("prop_1").unwrap(),
            &json!("prop_1_value")
        );

        Ok(())
    }
}

#[cfg(test)]
mod feature_config_tests {
    use serde_json::json;

    use super::*;
    use crate::fixtures::intermediate_representation::get_feature_manifest;

    #[test]
    fn test_validate_feature_config_success() -> Result<()> {
        let fm = get_feature_manifest(
            vec![],
            vec![],
            vec![FeatureDef {
                name: "feature".into(),
                props: vec![PropDef::new(
                    "prop_1",
                    &TypeRef::String,
                    &json!("prop_1_value"),
                )],
                ..Default::default()
            }],
            BTreeMap::new(),
        );

        let result = fm.validate_feature_config("feature", json!({ "prop_1": "new value" }))?;
        assert_eq!(result.props[0].default, json!("new value"));

        Ok(())
    }

    #[test]
    fn test_validate_feature_config_invalid_feature_name() -> Result<()> {
        let fm = get_feature_manifest(
            vec![],
            vec![],
            vec![FeatureDef {
                name: "feature".into(),
                props: vec![PropDef::new(
                    "prop_1",
                    &TypeRef::String,
                    &json!("prop_1_value"),
                )],
                ..Default::default()
            }],
            BTreeMap::new(),
        );

        let result = fm.validate_feature_config("feature-1", json!({ "prop_1": "new value" }));
        assert!(result.is_err());
        assert_eq!(
            result.err().unwrap().to_string(),
            "Feature `feature-1` not found on manifest".to_string()
        );

        Ok(())
    }

    #[test]
    fn test_validate_feature_config_invalid_feature_prop_name() -> Result<()> {
        let fm = get_feature_manifest(
            vec![],
            vec![],
            vec![FeatureDef {
                name: "feature".into(),
                props: vec![PropDef::new(
                    "prop_1",
                    &TypeRef::Option(Box::new(TypeRef::String)),
                    &Value::Null,
                )],
                ..Default::default()
            }],
            BTreeMap::new(),
        );

        let result = fm.validate_feature_config("feature", json!({"prop": "new value"}));
        assert!(result.is_err());
        assert_eq!(
            result.err().unwrap().to_string(),
            "Validation Error at features/feature: Invalid property \"prop\"; did you mean \"prop_1\"?"
        );

        Ok(())
    }

    #[test]
    fn test_validate_feature_config_invalid_feature_prop_value() -> Result<()> {
        let fm = get_feature_manifest(
            vec![],
            vec![],
            vec![FeatureDef {
                name: "feature".into(),
                props: vec![PropDef::new(
                    "prop_1",
                    &TypeRef::String,
                    &json!("prop_1_value"),
                )],
                ..Default::default()
            }],
            BTreeMap::new(),
        );

        let result = fm.validate_feature_config(
            "feature",
            json!({
                "prop_1": 1,
            }),
        );
        assert!(result.is_err());
        assert_eq!(
            result.err().unwrap().to_string(),
            "Validation Error at features/feature.prop_1: Invalid value 1 for type String"
                .to_string()
        );

        Ok(())
    }

    #[test]
    fn test_validate_feature_config_errors_on_invalid_object_prop() -> Result<()> {
        let obj_defs = vec![ObjectDef::new(
            "SampleObj",
            &[PropDef::new("string", &TypeRef::String, &json!("a string"))],
        )];
        let fm = get_feature_manifest(
            obj_defs,
            vec![],
            vec![FeatureDef {
                name: "feature".into(),
                props: vec![PropDef::new(
                    "prop_1",
                    &TypeRef::Object("SampleObj".into()),
                    &json!({
                        "string": "a value"
                    }),
                )],
                ..Default::default()
            }],
            BTreeMap::new(),
        );

        let result = fm.validate_feature_config(
            "feature",
            json!({
                "prop_1": {
                    "invalid-prop": "invalid-prop value"
                }
            }),
        );

        assert!(result.is_err());
        assert_eq!(
            result.err().unwrap().to_string(),
            "Validation Error at features/feature.prop_1#SampleObj: Invalid property \"invalid-prop\"; did you mean \"string\"?"
        );

        Ok(())
    }
}
