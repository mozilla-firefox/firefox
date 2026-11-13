/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::collections::{BTreeMap, BTreeSet, HashMap, HashSet};

use serde_json::Value;

use crate::{
    defaults::DefaultsMerger,
    error::{FMLError, Result},
    frontend::{
        ExampleBlock, FeatureAdditionChoices, FeatureAdditions, ImportBlock, InlineExampleBlock,
        ManifestFrontEnd, PartialExampleBlock, PathOnly, Types,
    },
    intermediate_representation::{FeatureManifest, ModuleId, TypeRef},
    util::loaders::{FileLoader, FilePath},
};

fn parse_typeref_string(input: String) -> Result<(String, Option<String>)> {
    // Split the string into the TypeRef and the name
    let mut object_type_iter = input.split(&['<', '>'][..]);

    // This should be the TypeRef type (except for )
    let type_ref_name = object_type_iter.next().unwrap().trim();

    if ["String", "Int", "Boolean"].contains(&type_ref_name) {
        return Ok((type_ref_name.to_string(), None));
    }

    // This should be the name or type of the Object
    match object_type_iter.next() {
        Some(object_type_name) => Ok((
            type_ref_name.to_string(),
            Some(object_type_name.to_string()),
        )),
        None => Ok((type_ref_name.to_string(), None)),
    }
}

pub(crate) fn get_typeref_from_string(
    input: String,
    types: &HashMap<String, TypeRef>,
) -> Result<TypeRef, FMLError> {
    let (type_ref, type_name) = parse_typeref_string(input)?;

    Ok(match type_ref.as_str() {
        "String" => TypeRef::String,
        "Int" => TypeRef::Int,
        "Boolean" => TypeRef::Boolean,
        "BundleText" | "Text" => TypeRef::BundleText,
        "BundleImage" | "Drawable" | "Image" => TypeRef::BundleImage,
        "Enum" => TypeRef::Enum(type_name.unwrap()),
        "Object" => TypeRef::Object(type_name.unwrap()),
        "List" => TypeRef::List(Box::new(get_typeref_from_string(
            type_name.unwrap(),
            types,
        )?)),
        "Option" => TypeRef::Option(Box::new(get_typeref_from_string(
            type_name.unwrap(),
            types,
        )?)),
        "Map" => {
            // Maps take a little extra massaging to get the key and value types
            let type_name = type_name.unwrap();
            let mut map_type_info_iter = type_name.split(',');

            let key_type = map_type_info_iter.next().unwrap().to_string();
            let value_type = map_type_info_iter.next().unwrap().trim().to_string();

            if key_type.eq("String") {
                TypeRef::StringMap(Box::new(get_typeref_from_string(value_type, types)?))
            } else {
                TypeRef::EnumMap(
                    Box::new(get_typeref_from_string(key_type, types)?),
                    Box::new(get_typeref_from_string(value_type, types)?),
                )
            }
        }
        type_name => types.get(type_name).cloned().ok_or_else(|| {
            FMLError::TypeParsingError(format!("{type_name} is not a recognized FML type"))
        })?,
    })
}

#[derive(Debug)]
pub struct Parser {
    files: FileLoader,
    source: FilePath,
}

impl Parser {
    pub fn new(files: FileLoader, source: FilePath) -> Result<Parser> {
        Ok(Parser { source, files })
    }

    pub fn load_frontend(files: FileLoader, source: &str) -> Result<ManifestFrontEnd> {
        let source = files.file_path(source)?;
        let parser: Parser = Parser::new(files, source)?;
        let mut loading = HashSet::new();
        parser.load_manifest(&parser.source, &mut loading)
    }

    // This method loads a manifest, including resolving the includes and merging the included files
    // into this top level one.
    // It recursively calls itself and then calls `merge_manifest`.
    pub fn load_manifest(
        &self,
        path: &FilePath,
        loading: &mut HashSet<ModuleId>,
    ) -> Result<ManifestFrontEnd> {
        let id: ModuleId = path.try_into()?;
        let files = &self.files;

        let mut parent = files
            .read::<ManifestFrontEnd>(path)
            .map_err(|e| FMLError::FMLModuleError(id.clone(), e.to_string()))?;

        // We canonicalize the paths to the import files really soon after the loading so when we merge
        // other included files, we cam match up the files that _they_ import, the concatenate the default
        // blocks for their features.
        self.canonicalize_import_paths(path, &mut parent.imports)
            .map_err(|e| FMLError::FMLModuleError(id.clone(), e.to_string()))?;

        self.inline_manifest_resources(path, &mut parent)?;

        loading.insert(id.clone());
        parent
            .includes()
            .iter()
            .try_fold(parent, |parent: ManifestFrontEnd, f| {
                let src_path = files.join(path, f)?;
                let child_id = ModuleId::try_from(&src_path)?;
                Ok(if !loading.contains(&child_id) {
                    let manifest = self.load_manifest(&src_path, loading)?;
                    self.merge_manifest(&src_path, parent, &src_path, manifest)
                        .map_err(|e| FMLError::FMLModuleError(id.clone(), e.to_string()))?
                } else {
                    parent
                })
            })
    }

    // Attempts to merge two manifests: a child into a parent.
    // The `child_path` is needed to report errors.
    fn merge_manifest(
        &self,
        parent_path: &FilePath,
        parent: ManifestFrontEnd,
        child_path: &FilePath,
        child: ManifestFrontEnd,
    ) -> Result<ManifestFrontEnd> {
        self.check_can_merge_manifest(parent_path, &parent, child_path, &child)?;

        // Child must not specify any features, objects or enums that the parent has.
        let features = merge_map(
            &parent.features,
            &child.features,
            "Features",
            "features",
            child_path,
        )?;

        let p_types = &parent.legacy_types.unwrap_or(parent.types);
        let c_types = &child.legacy_types.unwrap_or(child.types);

        let objects = merge_map(
            &c_types.objects,
            &p_types.objects,
            "Objects",
            "objects",
            child_path,
        )?;
        let enums = merge_map(&c_types.enums, &p_types.enums, "Enums", "enums", child_path)?;

        let imports = self.merge_import_block_list(&parent.imports, &child.imports)?;

        // The child's features are about to become the parent's, so its suppressions
        // have to come with them.
        let mut no_lint = parent.no_lint.clone();
        for name in &child.no_lint {
            if !no_lint.contains(name) {
                no_lint.push(name.clone());
            }
        }

        let merged = ManifestFrontEnd {
            features,
            types: Types { enums, objects },
            legacy_types: None,
            imports,
            no_lint,
            ..parent
        };

        Ok(merged)
    }

    fn inline_manifest_resources(
        &self,
        path: &FilePath,
        manifest: &mut ManifestFrontEnd,
    ) -> Result<()> {
        for feature in manifest.features.values_mut() {
            let as_typed = &feature.examples;
            let mut inlined = Vec::with_capacity(as_typed.len());
            for example in as_typed {
                inlined.push(example.inline(&self.files, path)?);
            }
            feature.examples = inlined;
        }

        for import in &mut manifest.imports {
            let mut features: BTreeMap<String, FeatureAdditionChoices> = Default::default();
            for (feature_id, additions) in &import.features {
                let additions: FeatureAdditions = additions.clone().into();
                features.insert(
                    feature_id.clone(),
                    additions.inline(&self.files, path)?.into(),
                );
            }
            import.features = features;
        }

        Ok(())
    }

    /// Load a manifest and all its imports, recursively if necessary.
    ///
    /// We populate a map of `FileId` to `FeatureManifest`s, so to avoid unnecessary clones,
    /// we return a `FileId` even when the file has already been imported.
    fn load_imports(
        &self,
        current: &FilePath,
        channel: Option<&str>,
        imports: &mut BTreeMap<ModuleId, FeatureManifest>,
        // includes: &mut HashSet<ModuleId>,
    ) -> Result<ModuleId> {
        let id = current.try_into()?;
        if imports.contains_key(&id) {
            return Ok(id);
        }
        // We put a terminus in here, to make sure we don't try and load more than once.
        imports.insert(id.clone(), Default::default());

        // This loads the manifest in its frontend format (i.e. direct from YAML via serde), including
        // all the `includes` for this manifest.
        let frontend = self.load_manifest(current, &mut HashSet::new())?;

        // Aside: tiny quality of life improvement. In the case where only one channel is supported,
        // we use it. This helps with globbing directories where the app wants to keep the feature definition
        // away from the feature configuration.
        let channel = if frontend.channels.len() == 1 {
            frontend.channels.first().map(String::as_str)
        } else {
            channel
        };

        let mut manifest = frontend.get_intermediate_representation(&id, channel)?;

        // We're now going to go through all the imports in the manifest YAML.
        // Each of the import blocks will have a path, and a Map<FeatureId, List<DefaultBlock>>
        // This loop does the work of merging the default blocks back into the imported manifests.
        // We'll then attach all the manifests to the root (i.e. the one we're generating code for today), in `imports`.
        // We associate only the feature ids with the manifest we're loading in this method.
        let mut imported_feature_id_map = BTreeMap::new();

        for block in &frontend.imports {
            // 1. Load the imported manifests in to the hash map.
            let path = self.files.join(current, &block.path)?;
            // The channel comes from the importer, rather than the command or the imported file.
            let child_id = self.load_imports(&path, Some(&block.channel), imports)?;
            let child_manifest = imports.get_mut(&child_id).expect("just loaded this file");

            // We detect that there are no name collisions after the loading has finished, with `check_can_import_manifest`.
            // We can't do it greedily, because of transitive imports may cause collisions, but we'll check here for better error
            // messages.
            check_can_import_manifest(&manifest, child_manifest)?;

            // We detect that the imported files have language specific files in `validate_manifest_for_lang()`.
            // We can't do it now because we don't yet know what this run is going to generate.

            // 2. We'll build a set of feature names that this manifest imports from the child manifest.
            // This will be the only thing we add directly to the manifest we load in this method.
            let mut feature_ids = BTreeSet::new();

            // 3. For each of the features in each of the imported files, the user can specify new defaults that should
            //    merge into/overwrite the defaults specified in the imported file. Let's do that now:
            // a. Prepare a DefaultsMerger, with an object map.
            let merger = DefaultsMerger::new(
                &child_manifest.obj_defs,
                frontend.channels.clone(),
                channel.map(str::to_string),
            );

            // b. Prepare a feature map that we'll alter in place.
            //    EXP- 2540 If we want to support re-exporting/encapsulating features then we will need to change
            //    this to be a more recursive look up. e.g. change `FeatureManifest.feature_defs` to be a `BTreeMap`.
            let feature_map = &mut child_manifest.feature_defs;

            // c. Iterate over the features we want to add to the original feature:
            //    - by adding to the list of examples.
            //    - by overriding default values.
            for (f, feature_additions) in &block.features {
                let feature_def = feature_map.get_mut(f).ok_or_else(|| {
                    FMLError::FMLModuleError(
                        id.clone(),
                        format!("Cannot override defaults for `{f}` feature from {child_id}"),
                    )
                })?;
                // FeatureAdditions holds the extra examples and defaults for this feature.
                let additions: FeatureAdditions = feature_additions.clone().into();

                // d.i) Append the imported list of examples to the original feature examples and…
                feature_def
                    .examples
                    .extend(additions.examples.iter().map(Into::into));

                // d.ii) Merge the overrides in place into the FeatureDefs
                merger
                    .merge_feature_defaults(feature_def, &Some(additions.defaults))
                    .map_err(|e| FMLError::FMLModuleError(child_id.clone(), e.to_string()))?;

                feature_ids.insert(f.clone());
            }

            // 4. Associate the imports as children of this manifest.
            imported_feature_id_map.insert(child_id.clone(), feature_ids);
        }

        manifest.imported_features = imported_feature_id_map;
        imports.insert(id.clone(), manifest);

        Ok(id)
    }

    pub fn get_intermediate_representation(
        &self,
        channel: Option<&str>,
    ) -> Result<FeatureManifest, FMLError> {
        let mut manifests = BTreeMap::new();
        let id = self.load_imports(&self.source, channel, &mut manifests)?;
        let mut fm = manifests
            .remove(&id)
            .expect("Top level manifest should always be present");

        for child in manifests.values() {
            check_can_import_manifest(&fm, child)?;
        }

        fm.all_imports = manifests;

        Ok(fm)
    }
}

impl Parser {
    fn check_can_merge_manifest(
        &self,
        parent_path: &FilePath,
        parent: &ManifestFrontEnd,
        child_path: &FilePath,
        child: &ManifestFrontEnd,
    ) -> Result<()> {
        if !child.channels.is_empty() {
            let child = &child.channels;
            let child = child.iter().collect::<HashSet<&String>>();
            let parent = &parent.channels;
            let parent = parent.iter().collect::<HashSet<&String>>();
            if !child.is_subset(&parent) {
                return Err(FMLError::ValidationError(
                    "channels".to_string(),
                    format!(
                        "Included manifest should not define its own channels: {}",
                        child_path
                    ),
                ));
            }
        }

        if let Some(about) = &child.about {
            if !about.is_includable() {
                return Err(FMLError::ValidationError(
                "about".to_string(),
                format!("Only files that don't already correspond to generated files may be included: file has a `class` and `package`/`module` name: {}", child_path),
            ));
            }
        }

        let mut map = Default::default();
        self.check_can_merge_imports(parent_path, &parent.imports, &mut map)?;
        self.check_can_merge_imports(child_path, &child.imports, &mut map)?;

        Ok(())
    }

    fn canonicalize_import_paths(
        &self,
        path: &FilePath,
        blocks: &mut Vec<ImportBlock>,
    ) -> Result<()> {
        for ib in blocks {
            let p = &self.files.join(path, &ib.path)?;
            ib.path = p.canonicalize()?.to_string();
        }
        Ok(())
    }

    fn check_can_merge_imports(
        &self,
        path: &FilePath,
        blocks: &Vec<ImportBlock>,
        map: &mut HashMap<String, String>,
    ) -> Result<()> {
        for b in blocks {
            let id = &b.path;
            let channel = &b.channel;
            let existing = map.insert(id.clone(), channel.clone());
            if let Some(v) = existing {
                if &v != channel {
                    return Err(FMLError::FMLModuleError(
                        path.try_into()?,
                        format!(
                            "File {} is imported with two different channels: {} and {}",
                            id, v, &channel
                        ),
                    ));
                }
            }
        }
        Ok(())
    }

    fn merge_import_block_list(
        &self,
        parent: &[ImportBlock],
        child: &[ImportBlock],
    ) -> Result<Vec<ImportBlock>> {
        let mut map = parent
            .iter()
            .map(|im| (im.path.clone(), im.clone()))
            .collect::<HashMap<_, _>>();

        for cib in child {
            let path = &cib.path;
            if let Some(pib) = map.get(path) {
                // We'll define an ordering here: the parent will come after the child
                // so the top-level one will override the lower level ones.
                // In practice, this shouldn't make a difference.
                let merged = merge_import_block(cib, pib)?;
                map.insert(path.clone(), merged);
            } else {
                map.insert(path.clone(), cib.clone());
            }
        }

        Ok(map.values().map(|b| b.to_owned()).collect::<Vec<_>>())
    }
}

fn merge_map<T: Clone>(
    a: &BTreeMap<String, T>,
    b: &BTreeMap<String, T>,
    display_key: &str,
    key: &str,
    child_path: &FilePath,
) -> Result<BTreeMap<String, T>> {
    let mut set = HashSet::new();

    let (a, b) = if a.len() < b.len() { (a, b) } else { (b, a) };

    let mut map = b.clone();

    for (k, v) in a {
        if map.contains_key(k) {
            set.insert(k.clone());
        } else {
            map.insert(k.clone(), v.clone());
        }
    }

    if set.is_empty() {
        Ok(map)
    } else {
        Err(FMLError::ValidationError(
            format!("{}/{:?}", key, set),
            format!(
                "{} cannot be defined twice, overloaded definition detected at {}",
                display_key, child_path,
            ),
        ))
    }
}

fn merge_import_block(a: &ImportBlock, b: &ImportBlock) -> Result<ImportBlock> {
    let mut block = a.clone();

    for (id, additions) in &b.features {
        block
            .features
            .entry(id.clone())
            .and_modify(|existing| existing.merge(additions))
            .or_insert(additions.clone());
    }
    Ok(block)
}

/// Check if this parent can import this child.
fn check_can_import_manifest(parent: &FeatureManifest, child: &FeatureManifest) -> Result<()> {
    check_can_import_list(parent, child, "enum", |fm: &FeatureManifest| {
        fm.enum_defs.keys().collect()
    })?;
    check_can_import_list(parent, child, "objects", |fm: &FeatureManifest| {
        fm.obj_defs.keys().collect()
    })?;
    check_can_import_list(parent, child, "features", |fm: &FeatureManifest| {
        fm.feature_defs.keys().collect()
    })?;

    Ok(())
}

fn check_can_import_list(
    parent: &FeatureManifest,
    child: &FeatureManifest,
    key: &str,
    f: fn(&FeatureManifest) -> HashSet<&String>,
) -> Result<()> {
    let p = f(parent);
    let c = f(child);
    let intersection = p.intersection(&c).collect::<HashSet<_>>();
    if !intersection.is_empty() {
        Err(FMLError::ValidationError(
            key.to_string(),
            format!(
                "`{}` types {:?} conflict when {} imports {}",
                key, &intersection, &parent.id, &child.id
            ),
        ))
    } else {
        Ok(())
    }
}

impl ExampleBlock {
    fn inline(&self, files: &FileLoader, root: &FilePath) -> Result<Self> {
        Ok(match self {
            Self::Inline(_) => self.clone(),
            Self::Partial(PartialExampleBlock { metadata, path }) => {
                let file = files.join(root, path)?;
                let value: Value = files.read(&file)?;
                Self::Inline(InlineExampleBlock {
                    metadata: metadata.to_owned(),
                    value,
                })
            }
            Self::BarePath(path) | Self::Path(PathOnly { path }) => {
                let file = files.join(root, path)?;
                let value: InlineExampleBlock = files.read(&file)?;
                Self::Inline(value)
            }
        })
    }
}

impl FeatureAdditionChoices {
    fn merge(&mut self, other: &Self) {
        match (self, other) {
            (Self::FeatureAdditions(a), Self::FeatureAdditions(b)) => a.merge(b),
            _ => unreachable!("FeatureAdditionChoices should have been rationalized already. This is a bug in nimbus-fml"),
        };
    }
}

impl FeatureAdditions {
    fn inline(&self, files: &FileLoader, root: &FilePath) -> Result<Self> {
        let examples = self
            .examples
            .iter()
            .map(|ex| ex.inline(files, root))
            .collect::<Result<_>>()?;
        Ok(Self {
            examples,
            defaults: self.defaults.clone(),
        })
    }

    fn merge(&mut self, other: &Self) {
        self.examples.extend(other.examples.clone());
        self.defaults.extend(other.defaults.clone());
    }
}

#[cfg(test)]
mod unit_tests {

    use std::{
        path::{Path, PathBuf},
        vec,
    };

    use serde_json::json;

    use super::*;
    use crate::{
        error::Result,
        frontend::ImportBlock,
        intermediate_representation::{PropDef, VariantDef},
        util::{join, pkg_dir},
    };

    #[test]
    fn test_parse_from_front_end_representation() -> Result<()> {
        let path = join(pkg_dir(), "fixtures/fe/nimbus_features.yaml");
        let path = Path::new(&path);
        let files = FileLoader::default()?;
        let parser = Parser::new(files, path.into())?;
        let ir = parser.get_intermediate_representation(Some("release"))?;

        // Validate parsed enums
        assert!(ir.enum_defs.len() == 1);
        let enum_def = &ir.enum_defs["PlayerProfile"];
        assert!(enum_def.name == *"PlayerProfile");
        assert!(enum_def.doc == *"This is an enum type");
        assert!(enum_def.variants.contains(&VariantDef {
            name: "adult".to_string(),
            doc: "This represents an adult player profile".to_string()
        }));
        assert!(enum_def.variants.contains(&VariantDef {
            name: "child".to_string(),
            doc: "This represents a child player profile".to_string()
        }));

        // Validate parsed objects
        assert!(ir.obj_defs.len() == 1);
        let obj_def = &ir.obj_defs["Button"];
        assert!(obj_def.name == *"Button");
        assert!(obj_def.doc == *"This is a button object");
        assert!(obj_def.props.contains(&PropDef::with_doc(
            "label",
            "This is the label for the button",
            &TypeRef::String,
            &serde_json::json!("REQUIRED FIELD")
        )));
        assert!(obj_def.props.contains(&PropDef::with_doc(
            "color",
            "This is the color of the button",
            &TypeRef::Option(Box::new(TypeRef::String)),
            &serde_json::Value::Null
        )));

        // Validate parsed features
        assert!(ir.feature_defs.len() == 1);
        let feature_def = ir.get_feature("dialog-appearance").unwrap();
        assert!(feature_def.name == *"dialog-appearance");
        assert!(feature_def.doc() == *"This is the appearance of the dialog");
        let positive_button = feature_def
            .props
            .iter()
            .find(|x| x.name == "positive")
            .unwrap();
        assert!(positive_button.name == *"positive");
        assert!(positive_button.doc == *"This is a positive button");
        assert!(positive_button.typ == TypeRef::Object("Button".to_string()));
        // We verify that the label, which came from the field default is "Ok then"
        // and the color default, which came from the feature default is "green"
        assert!(positive_button.default.get("label").unwrap().as_str() == Some("Ok then"));
        assert!(positive_button.default.get("color").unwrap().as_str() == Some("green"));
        let negative_button = feature_def
            .props
            .iter()
            .find(|x| x.name == "negative")
            .unwrap();
        assert!(negative_button.name == *"negative");
        assert!(negative_button.doc == *"This is a negative button");
        assert!(negative_button.typ == TypeRef::Object("Button".to_string()));
        assert!(negative_button.default.get("label").unwrap().as_str() == Some("Not this time"));
        assert!(negative_button.default.get("color").unwrap().as_str() == Some("red"));
        let background_color = feature_def
            .props
            .iter()
            .find(|x| x.name == "background-color")
            .unwrap();
        assert!(background_color.name == *"background-color");
        assert!(background_color.doc == *"This is the background color");
        assert!(background_color.typ == TypeRef::String);
        assert!(background_color.default.as_str() == Some("white"));
        let player_mapping = feature_def
            .props
            .iter()
            .find(|x| x.name == "player-mapping")
            .unwrap();
        assert!(player_mapping.name == *"player-mapping");
        assert!(player_mapping.doc == *"This is the map of the player type to a button");
        assert!(
            player_mapping.typ
                == TypeRef::EnumMap(
                    Box::new(TypeRef::Enum("PlayerProfile".to_string())),
                    Box::new(TypeRef::Object("Button".to_string()))
                )
        );
        assert!(
            player_mapping.default
                == json!({
                    "child": {
                        "label": "Play game!",
                        "color": "green"
                    },
                    "adult": {
                        "label": "Play game!",
                        "color": "blue",
                    }
                })
        );

        Ok(())
    }

    #[test]
    fn test_merging_defaults() -> Result<()> {
        let path = join(pkg_dir(), "fixtures/fe/default_merging.yaml");
        let path = Path::new(&path);
        let files = FileLoader::default()?;
        let parser = Parser::new(files, path.into())?;
        let ir = parser.get_intermediate_representation(Some("release"))?;
        let feature_def = ir.get_feature("dialog-appearance").unwrap();
        let positive_button = feature_def
            .props
            .iter()
            .find(|x| x.name == "positive")
            .unwrap();
        // We validate that the no-channel feature level default got merged back
        assert_eq!(
            positive_button
                .default
                .get("alt-text")
                .unwrap()
                .as_str()
                .unwrap(),
            "Go Ahead!"
        );
        // We validate that the original field level default don't get lost if no
        // feature level default with the same name exists
        assert_eq!(
            positive_button
                .default
                .get("label")
                .unwrap()
                .as_str()
                .unwrap(),
            "Ok then"
        );
        // We validate that feature level default overwrite field level defaults if one exists
        // in the field level, it's blue, but on the feature level it's green
        assert_eq!(
            positive_button
                .default
                .get("color")
                .unwrap()
                .as_str()
                .unwrap(),
            "green"
        );
        // We now re-run this, but merge back the nightly channel instead
        let files = FileLoader::default()?;
        let parser = Parser::new(files, path.into())?;
        let ir = parser.get_intermediate_representation(Some("nightly"))?;
        let feature_def = ir.get_feature("dialog-appearance").unwrap();
        let positive_button = feature_def
            .props
            .iter()
            .find(|x| x.name == "positive")
            .unwrap();
        // We validate that feature level default overwrite field level defaults if one exists
        // in the field level, it's blue, but on the feature level it's bright-red
        // note that it's bright-red because we merged back the `nightly`
        // channel, instead of the `release` channel that merges back
        // by default
        assert_eq!(
            positive_button
                .default
                .get("color")
                .unwrap()
                .as_str()
                .unwrap(),
            "bright-red"
        );
        // We against validate that regardless
        // of the channel, the no-channel feature level default got merged back
        assert_eq!(
            positive_button
                .default
                .get("alt-text")
                .unwrap()
                .as_str()
                .unwrap(),
            "Go Ahead!"
        );
        Ok(())
    }

    #[test]
    fn test_convert_to_typeref_string() -> Result<()> {
        // Testing converting to TypeRef::String
        let types = Default::default();
        assert_eq!(
            get_typeref_from_string("String".to_string(), &types).unwrap(),
            TypeRef::String
        );
        get_typeref_from_string("string".to_string(), &types).unwrap_err();
        get_typeref_from_string("str".to_string(), &types).unwrap_err();

        Ok(())
    }

    #[test]
    fn test_convert_to_typeref_int() -> Result<()> {
        // Testing converting to TypeRef::Int
        let types = Default::default();
        assert_eq!(
            get_typeref_from_string("Int".to_string(), &types).unwrap(),
            TypeRef::Int
        );
        get_typeref_from_string("integer".to_string(), &types).unwrap_err();
        get_typeref_from_string("int".to_string(), &types).unwrap_err();

        Ok(())
    }

    #[test]
    fn test_convert_to_typeref_boolean() -> Result<()> {
        // Testing converting to TypeRef::Boolean
        let types = Default::default();
        assert_eq!(
            get_typeref_from_string("Boolean".to_string(), &types).unwrap(),
            TypeRef::Boolean
        );
        get_typeref_from_string("boolean".to_string(), &types).unwrap_err();
        get_typeref_from_string("bool".to_string(), &types).unwrap_err();

        Ok(())
    }

    #[test]
    fn test_convert_to_typeref_bundletext() -> Result<()> {
        // Testing converting to TypeRef::BundleText
        let types = Default::default();
        get_typeref_from_string("bundletext(something)".to_string(), &types).unwrap_err();
        get_typeref_from_string("BundleText()".to_string(), &types).unwrap_err();

        // The commented out lines below represent areas we need better
        // type checking on, but are ignored for now

        // get_typeref_from_string("BundleText".to_string()).unwrap_err();
        // get_typeref_from_string("BundleText<>".to_string()).unwrap_err();
        // get_typeref_from_string("BundleText<21>".to_string()).unwrap_err();

        Ok(())
    }

    #[test]
    fn test_convert_to_typeref_bundleimage() -> Result<()> {
        // Testing converting to TypeRef::BundleImage
        let types = Default::default();
        assert_eq!(
            get_typeref_from_string("BundleImage<test_name>".to_string(), &types).unwrap(),
            TypeRef::BundleImage
        );
        get_typeref_from_string("bundleimage(something)".to_string(), &types).unwrap_err();
        get_typeref_from_string("BundleImage()".to_string(), &types).unwrap_err();

        // The commented out lines below represent areas we need better
        // type checking on, but are ignored for now

        // get_typeref_from_string("BundleImage".to_string()).unwrap_err();
        // get_typeref_from_string("BundleImage<>".to_string()).unwrap_err();
        // get_typeref_from_string("BundleImage<21>".to_string()).unwrap_err();

        Ok(())
    }

    #[test]
    fn test_convert_to_typeref_enum() -> Result<()> {
        // Testing converting to TypeRef::Enum
        let types = Default::default();
        assert_eq!(
            get_typeref_from_string("Enum<test_name>".to_string(), &types).unwrap(),
            TypeRef::Enum("test_name".to_string())
        );
        get_typeref_from_string("enum(something)".to_string(), &types).unwrap_err();
        get_typeref_from_string("Enum()".to_string(), &types).unwrap_err();

        // The commented out lines below represent areas we need better
        // type checking on, but are ignored for now

        // get_typeref_from_string("Enum".to_string()).unwrap_err();
        // get_typeref_from_string("Enum<>".to_string()).unwrap_err();
        // get_typeref_from_string("Enum<21>".to_string()).unwrap_err();

        Ok(())
    }

    #[test]
    fn test_convert_to_typeref_object() -> Result<()> {
        // Testing converting to TypeRef::Object
        let types = Default::default();
        assert_eq!(
            get_typeref_from_string("Object<test_name>".to_string(), &types).unwrap(),
            TypeRef::Object("test_name".to_string())
        );
        get_typeref_from_string("object(something)".to_string(), &types).unwrap_err();
        get_typeref_from_string("Object()".to_string(), &types).unwrap_err();

        // The commented out lines below represent areas we need better
        // type checking on, but are ignored for now

        // get_typeref_from_string("Object".to_string()).unwrap_err();
        // get_typeref_from_string("Object<>".to_string()).unwrap_err();
        // get_typeref_from_string("Object<21>".to_string()).unwrap_err();

        Ok(())
    }

    #[test]
    fn test_convert_to_typeref_list() -> Result<()> {
        // Testing converting to TypeRef::List
        let types = Default::default();
        assert_eq!(
            get_typeref_from_string("List<String>".to_string(), &types).unwrap(),
            TypeRef::List(Box::new(TypeRef::String))
        );
        assert_eq!(
            get_typeref_from_string("List<Int>".to_string(), &types).unwrap(),
            TypeRef::List(Box::new(TypeRef::Int))
        );
        assert_eq!(
            get_typeref_from_string("List<Boolean>".to_string(), &types).unwrap(),
            TypeRef::List(Box::new(TypeRef::Boolean))
        );

        // Generate a list of user types to validate use of them in a list
        let mut types: HashMap<_, _> = Default::default();
        types.insert(
            "TestEnum".to_string(),
            TypeRef::Enum("TestEnum".to_string()),
        );
        types.insert(
            "TestObject".to_string(),
            TypeRef::Object("TestObject".to_string()),
        );

        assert_eq!(
            get_typeref_from_string("List<TestEnum>".to_string(), &types).unwrap(),
            TypeRef::List(Box::new(TypeRef::Enum("TestEnum".to_string())))
        );
        assert_eq!(
            get_typeref_from_string("List<TestObject>".to_string(), &types).unwrap(),
            TypeRef::List(Box::new(TypeRef::Object("TestObject".to_string())))
        );

        get_typeref_from_string("list(something)".to_string(), &types).unwrap_err();
        get_typeref_from_string("List()".to_string(), &types).unwrap_err();

        // The commented out lines below represent areas we need better
        // type checking on, but are ignored for now

        // get_typeref_from_string("List".to_string()).unwrap_err();
        // get_typeref_from_string("List<>".to_string()).unwrap_err();
        // get_typeref_from_string("List<21>".to_string()).unwrap_err();

        Ok(())
    }

    #[test]
    fn test_convert_to_typeref_option() -> Result<()> {
        // Testing converting to TypeRef::Option
        let types = Default::default();
        assert_eq!(
            get_typeref_from_string("Option<String>".to_string(), &types).unwrap(),
            TypeRef::Option(Box::new(TypeRef::String))
        );
        assert_eq!(
            get_typeref_from_string("Option<Int>".to_string(), &types).unwrap(),
            TypeRef::Option(Box::new(TypeRef::Int))
        );
        assert_eq!(
            get_typeref_from_string("Option<Boolean>".to_string(), &types).unwrap(),
            TypeRef::Option(Box::new(TypeRef::Boolean))
        );

        // Generate a list of user types to validate use of them as Options
        let mut types = HashMap::new();
        types.insert(
            "TestEnum".to_string(),
            TypeRef::Enum("TestEnum".to_string()),
        );
        types.insert(
            "TestObject".to_string(),
            TypeRef::Object("TestObject".to_string()),
        );
        assert_eq!(
            get_typeref_from_string("Option<TestEnum>".to_string(), &types).unwrap(),
            TypeRef::Option(Box::new(TypeRef::Enum("TestEnum".to_string())))
        );
        assert_eq!(
            get_typeref_from_string("Option<TestObject>".to_string(), &types).unwrap(),
            TypeRef::Option(Box::new(TypeRef::Object("TestObject".to_string())))
        );

        get_typeref_from_string("option(something)".to_string(), &types).unwrap_err();
        get_typeref_from_string("Option(Something)".to_string(), &types).unwrap_err();

        // The commented out lines below represent areas we need better
        // type checking on, but are ignored for now

        // get_typeref_from_string("Option".to_string()).unwrap_err();
        // get_typeref_from_string("Option<>".to_string()).unwrap_err();
        // get_typeref_from_string("Option<21>".to_string()).unwrap_err();

        Ok(())
    }

    #[test]
    fn test_convert_to_typeref_map() -> Result<()> {
        // Testing converting to TypeRef::Map
        let types = Default::default();
        assert_eq!(
            get_typeref_from_string("Map<String, String>".to_string(), &types).unwrap(),
            TypeRef::StringMap(Box::new(TypeRef::String))
        );
        assert_eq!(
            get_typeref_from_string("Map<String, Int>".to_string(), &types).unwrap(),
            TypeRef::StringMap(Box::new(TypeRef::Int))
        );
        assert_eq!(
            get_typeref_from_string("Map<String, Boolean>".to_string(), &types).unwrap(),
            TypeRef::StringMap(Box::new(TypeRef::Boolean))
        );

        // Generate a list of user types to validate use of them in a list
        let mut types = HashMap::new();
        types.insert(
            "TestEnum".to_string(),
            TypeRef::Enum("TestEnum".to_string()),
        );
        types.insert(
            "TestObject".to_string(),
            TypeRef::Object("TestObject".to_string()),
        );
        assert_eq!(
            get_typeref_from_string("Map<String, TestEnum>".to_string(), &types).unwrap(),
            TypeRef::StringMap(Box::new(TypeRef::Enum("TestEnum".to_string())))
        );
        assert_eq!(
            get_typeref_from_string("Map<String, TestObject>".to_string(), &types).unwrap(),
            TypeRef::StringMap(Box::new(TypeRef::Object("TestObject".to_string())))
        );
        assert_eq!(
            get_typeref_from_string("Map<TestEnum, String>".to_string(), &types).unwrap(),
            TypeRef::EnumMap(
                Box::new(TypeRef::Enum("TestEnum".to_string())),
                Box::new(TypeRef::String)
            )
        );
        assert_eq!(
            get_typeref_from_string("Map<TestEnum, TestObject>".to_string(), &types).unwrap(),
            TypeRef::EnumMap(
                Box::new(TypeRef::Enum("TestEnum".to_string())),
                Box::new(TypeRef::Object("TestObject".to_string()))
            )
        );

        get_typeref_from_string("map(something)".to_string(), &Default::default()).unwrap_err();
        get_typeref_from_string("Map(Something)".to_string(), &Default::default()).unwrap_err();

        // The commented out lines below represent areas we need better
        // type checking on, but are ignored for now

        // get_typeref_from_string("Map".to_string()).unwrap_err();
        // get_typeref_from_string("Map<>".to_string()).unwrap_err();
        // get_typeref_from_string("Map<21>".to_string()).unwrap_err();

        Ok(())
    }

    #[test]
    fn test_include_check_can_merge_manifest() -> Result<()> {
        let files = FileLoader::default()?;
        let parser = Parser::new(files, std::env::temp_dir().as_path().into())?;
        let parent_path: FilePath = std::env::temp_dir().as_path().into();
        let child_path = parent_path.join("http://not-needed.com")?;
        let parent = ManifestFrontEnd {
            channels: vec!["alice".to_string(), "bob".to_string()],
            ..Default::default()
        };
        let child = ManifestFrontEnd {
            channels: vec!["alice".to_string(), "bob".to_string()],
            ..Default::default()
        };

        assert!(parser
            .check_can_merge_manifest(&parent_path, &parent, &child_path, &child)
            .is_ok());

        let child = ManifestFrontEnd {
            channels: vec!["eve".to_string()],
            ..Default::default()
        };

        assert!(parser
            .check_can_merge_manifest(&parent_path, &parent, &child_path, &child)
            .is_err());

        Ok(())
    }

    #[test]
    fn test_include_check_can_merge_manifest_with_imports() -> Result<()> {
        let files = FileLoader::default()?;
        let parser = Parser::new(files, std::env::temp_dir().as_path().into())?;
        let parent_path: FilePath = std::env::temp_dir().as_path().into();
        let child_path = parent_path.join("http://child")?;
        let parent = ManifestFrontEnd {
            channels: vec!["alice".to_string(), "bob".to_string()],
            imports: vec![ImportBlock {
                path: "absolute_path".to_string(),
                channel: "one_channel".to_string(),
                features: Default::default(),
            }],
            ..Default::default()
        };
        let child = ManifestFrontEnd {
            channels: vec!["alice".to_string(), "bob".to_string()],
            imports: vec![ImportBlock {
                path: "absolute_path".to_string(),
                channel: "another_channel".to_string(),
                features: Default::default(),
            }],
            ..Default::default()
        };

        let mut map = Default::default();
        let res = parser.check_can_merge_imports(&parent_path, &parent.imports, &mut map);
        assert!(res.is_ok());
        assert_eq!(map.get("absolute_path").unwrap(), "one_channel");

        let err_msg = "Problem with http://child/: File absolute_path is imported with two different channels: one_channel and another_channel";
        let res = parser.check_can_merge_imports(&child_path, &child.imports, &mut map);
        assert!(res.is_err());
        assert_eq!(res.unwrap_err().to_string(), err_msg.to_string());

        let res = parser.check_can_merge_manifest(&parent_path, &parent, &child_path, &child);
        assert!(res.is_err());
        assert_eq!(res.unwrap_err().to_string(), err_msg.to_string());

        Ok(())
    }

    #[test]
    fn test_include_circular_includes() -> Result<()> {
        use crate::util::pkg_dir;
        // snake.yaml includes tail.yaml, which includes snake.yaml
        let path = PathBuf::from(pkg_dir()).join("fixtures/fe/including/circular/snake.yaml");

        let files = FileLoader::default()?;
        let parser = Parser::new(files, path.as_path().into())?;
        let ir = parser.get_intermediate_representation(Some("release"));
        assert!(ir.is_ok());

        Ok(())
    }

    #[test]
    fn test_include_deeply_nested_includes() -> Result<()> {
        use crate::util::pkg_dir;
        // Deeply nested includes, which start at 00-head.yaml, and then recursively includes all the
        // way down to 06-toe.yaml
        let path_buf = PathBuf::from(pkg_dir()).join("fixtures/fe/including/deep/00-head.yaml");

        let files = FileLoader::default()?;
        let parser = Parser::new(files, path_buf.as_path().into())?;

        let ir = parser.get_intermediate_representation(Some("release"))?;
        assert_eq!(ir.feature_defs.len(), 1);

        Ok(())
    }
}
