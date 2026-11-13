/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

mod config;
mod descriptor;
mod inspector;
#[cfg(test)]
mod test_helper;

pub use config::FmlLoaderConfig;
cfg_if::cfg_if! {
    if #[cfg(feature = "uniffi-bindings")] {
    use crate::{editing::{CorrectionCandidate, CursorPosition, CursorSpan}, frontend::DocumentationLink};
    use url::Url;
    use descriptor::FmlFeatureDescriptor;
    use inspector::{FmlEditorError, FmlFeatureExample, FmlFeatureInspector};
    }
}
use serde_json::Value;

use crate::{
    error::{ClientError::JsonMergeError, FMLError, Result},
    intermediate_representation::FeatureManifest,
    parser::Parser,
    util::loaders::{FileLoader, LoaderConfig},
};
use std::collections::HashMap;

use std::sync::Arc;

pub struct MergedJsonWithErrors {
    pub json: String,
    pub errors: Vec<FMLError>,
}

pub struct FmlClient {
    pub(crate) manifest: Arc<FeatureManifest>,
    pub(crate) default_json: serde_json::Map<String, serde_json::Value>,
}

fn get_default_json_for_manifest(manifest: &FeatureManifest) -> Result<JsonObject> {
    if let Value::Object(json) = manifest.default_json() {
        Ok(json)
    } else {
        Err(FMLError::ClientError(JsonMergeError(
            "Manifest default json is not an object".to_string(),
        )))
    }
}

impl FmlClient {
    /// Constructs a new FmlClient object.
    ///
    /// Definitions of the parameters are as follows:
    /// - `manifest_path`: The path (relative to the current working directory) to the fml.yml that should be loaded.
    /// - `channel`: The channel that should be loaded for the manifest.
    pub fn new(manifest_path: String, channel: String) -> Result<Self> {
        Self::new_with_ref(manifest_path, channel, None)
    }

    pub fn new_with_ref(
        manifest_path: String,
        channel: String,
        ref_: Option<String>,
    ) -> Result<Self> {
        let config = Self::create_loader(&manifest_path, ref_.as_deref());
        Self::new_with_config(manifest_path, channel, config)
    }

    pub fn new_with_config(
        manifest_path: String,
        channel: String,
        config: FmlLoaderConfig,
    ) -> Result<Self> {
        let config: LoaderConfig = config.into();
        let files = FileLoader::try_from(&config)?;
        let path = files.file_path(&manifest_path)?;
        let parser: Parser = Parser::new(files, path)?;
        let ir = parser.get_intermediate_representation(Some(&channel))?;
        ir.validate_manifest_with(config.lax_gecko_pref_validation)?;

        Ok(FmlClient {
            default_json: get_default_json_for_manifest(&ir)?,
            manifest: Arc::new(ir),
        })
    }

    #[cfg(test)]
    pub fn new_from_manifest(manifest: FeatureManifest) -> Self {
        manifest.validate_manifest().ok();
        Self {
            default_json: get_default_json_for_manifest(&manifest).ok().unwrap(),
            manifest: Arc::new(manifest),
        }
    }

    fn create_loader(manifest_path: &str, ref_: Option<&str>) -> FmlLoaderConfig {
        let mut refs: HashMap<_, _> = Default::default();
        match (LoaderConfig::repo_and_path(manifest_path), ref_) {
            (Some((repo, _)), Some(ref_)) => refs.insert(repo, ref_.to_string()),
            _ => None,
        };

        FmlLoaderConfig {
            refs,
            ..Default::default()
        }
    }

    /// Validates a supplied list of feature configurations. The valid configurations will be merged into the manifest's
    /// default feature JSON, and invalid configurations will be returned as a list of their respective errors.
    pub fn merge(
        &self,
        feature_configs: HashMap<String, JsonObject>,
    ) -> Result<MergedJsonWithErrors> {
        let mut json = self.default_json.clone();
        let mut errors: Vec<FMLError> = Default::default();
        for (feature_id, value) in feature_configs {
            match self
                .manifest
                .validate_feature_config(&feature_id, serde_json::Value::Object(value))
            {
                Ok(fd) => {
                    json.insert(feature_id, fd.default_json());
                }
                Err(e) => errors.push(e),
            };
        }
        Ok(MergedJsonWithErrors {
            json: serde_json::to_string(&json)?,
            errors,
        })
    }

    /// Returns the default feature JSON for the loaded FML's selected channel.
    pub fn get_default_json(&self) -> Result<String> {
        Ok(serde_json::to_string(&self.default_json)?)
    }

    /// Returns a list of feature ids that support coenrollment.
    pub fn get_coenrolling_feature_ids(&self) -> Result<Vec<String>> {
        Ok(self.manifest.get_coenrolling_feature_ids())
    }
}

pub(crate) type JsonObject = serde_json::Map<String, serde_json::Value>;

#[cfg(feature = "uniffi-bindings")]
uniffi::include_scaffolding!("fml");

#[cfg(test)]
mod unit_tests {
    use super::*;
    use crate::{
        fixtures::intermediate_representation::get_feature_manifest,
        intermediate_representation::{FeatureDef, ModuleId, PropDef, TypeRef},
    };
    use serde_json::{json, Value};
    use std::collections::{BTreeMap, HashMap};

    fn create_manifest() -> FeatureManifest {
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
                metadata: Default::default(),
                ..Default::default()
            }],
            BTreeMap::new(),
        );

        get_feature_manifest(
            vec![],
            vec![],
            vec![FeatureDef {
                name: "feature".into(),
                props: vec![PropDef::new(
                    "prop_1",
                    &TypeRef::String,
                    &json!("prop_1_value"),
                )],
                metadata: Default::default(),
                allow_coenrollment: true,
                ..Default::default()
            }],
            BTreeMap::from([(ModuleId::Local("test".into()), fm_i)]),
        )
    }

    #[test]
    fn test_get_default_json() -> Result<()> {
        let json_result = get_default_json_for_manifest(&create_manifest())?;

        assert_eq!(
            Value::Object(json_result),
            json!({
                "feature": {
                    "prop_1": "prop_1_value"
                },
                "feature_i": {
                    "prop_i_1": "prop_i_1_value"
                }
            })
        );

        Ok(())
    }

    #[test]
    fn test_validate_and_merge_feature_configs() -> Result<()> {
        let client: FmlClient = create_manifest().into();

        let result = client.merge(HashMap::from_iter([
            (
                "feature".to_string(),
                json!({ "prop_1": "new value" })
                    .as_object()
                    .unwrap()
                    .clone(),
            ),
            (
                "feature_i".to_string(),
                json!({"prop_i_1": 1}).as_object().unwrap().clone(),
            ),
        ]))?;

        assert_eq!(
            serde_json::from_str::<Value>(&result.json)?,
            json!({
                "feature": {
                    "prop_1": "new value"
                },
                "feature_i": {
                    "prop_i_1": "prop_i_1_value"
                }
            })
        );
        assert_eq!(result.errors.len(), 1);
        assert_eq!(
            result.errors[0].to_string(),
            "Validation Error at features/feature_i.prop_i_1: Invalid value 1 for type String"
                .to_string()
        );

        Ok(())
    }

    #[test]
    fn test_get_coenrolling_feature_ids() -> Result<()> {
        let client: FmlClient = create_manifest().into();
        let result = client.get_coenrolling_feature_ids();

        assert_eq!(result.unwrap(), vec!["feature"]);

        Ok(())
    }
}

#[cfg(test)]
mod string_aliases {
    use super::{test_helper::client, *};

    #[test]
    fn test_simple_feature() -> Result<()> {
        let client = client("string-aliases.fml.yaml", "storms")?;
        let inspector = {
            let i = client.get_feature_inspector("my-simple-team".to_string());
            assert!(i.is_some());
            i.unwrap()
        };

        // -> feature my-sports:
        //      player-availability: Map<PlayerName, Boolean> (PlayerName is the set of strings in this list)
        //      captain: Option<PlayerName>
        //      the-team: List<SportName> (SportName is the set of string that are keys in this map)

        // Happy path. This configuration is internally consistent.
        let errors = inspector.get_errors(
            r#"{
                "captain": "Babet",
                "the-team": ["Babet", "Elin", "Isha"]
            }"#
            .to_string(),
        );
        assert_eq!(None, errors);

        // ----------------------------
        // Donkey cannot be the captain
        // Donkey is not a key in the default) player-availability map.
        let errors = inspector.get_errors(
            r#"{
                "captain": "Donkey",
                "the-team": ["Babet", "Elin", "Isha"]
            }"#
            .to_string(),
        );
        let expected = r#"Invalid value "Donkey" for type PlayerName; did you mean one of "Agnes", "Babet", "Ciarán", "Debi", "Elin", "Fergus", "Gerrit", "Henk", "Isha", "Jocelyn", "Kathleen" or "Lilian"?"#;
        assert!(errors.is_some());
        let errors = errors.unwrap();
        let err = errors.first().unwrap();
        assert_eq!(Some("\"Donkey\""), err.highlight.as_deref());
        assert_eq!(expected, err.message.as_str());

        // -------------------------------------------
        // Donkey cannot play as a member of the-team.
        // Donkey is not a key in the default) player-availability map.
        let errors = inspector.get_errors(
            r#"{
                "captain": "Gerrit",
                "the-team": ["Babet", "Donkey", "Isha"]
            }"#
            .to_string(),
        );
        assert!(errors.is_some());
        let errors = errors.unwrap();
        let err = errors.first().unwrap();
        assert_eq!(Some("\"Donkey\""), err.highlight.as_deref());
        assert_eq!(expected, err.message.as_str());

        // -----------------------------------------------------------
        // Surprise! Donkey is now available!
        // because we added them to the player-availability map.
        let errors = inspector.get_errors(
            r#"{
                "player-availability": {
                    "Donkey": true
                },
                "captain": "Donkey",
                "the-team": ["Babet", "Elin", "Isha"]
            }"#
            .to_string(),
        );
        assert_eq!(None, errors);

        Ok(())
    }

    #[test]
    fn test_objects_in_a_feature() -> Result<()> {
        let client = client("string-aliases.fml.yaml", "cyclones")?;
        let inspector = {
            let i = client.get_feature_inspector("my-sports".to_string());
            assert!(i.is_some());
            i.unwrap()
        };

        // -> feature my-sports:
        //      available-players: List<PlayerName> (PlayerName is the set of strings in this list)
        //      my-favourite-teams: Map<SportName, Team> (SportName is the set of string that are keys in this map)
        // -> class Team:
        //      sport: SportName
        //      players: List<PlayerName>

        // Happy path test.
        // Note that neither KABADDI nor CHESS appeared in the manifest.
        let errors = inspector.get_errors(
            r#"{
                "my-favorite-teams": {
                    "KABADDI": {
                        "sport": "KABADDI",
                        "players": ["Aka", "Hene", "Lino"]
                    },
                    "CHESS": {
                        "sport": "CHESS",
                        "players": ["Mele", "Nona", "Pama"]
                    }
                }
            }"#
            .to_string(),
        );
        assert_eq!(None, errors);

        // ----------------------------------------------------------------
        // Only CHESS is a valid game in this configuration, not CONNECT-4.
        let errors = inspector.get_errors(
            r#"{
                "my-favorite-teams": {
                    "CHESS": {
                        "sport": "CONNECT-4",
                        "players": ["Mele", "Nona", "Pama"]
                    }
                }
            }"#
            .to_string(),
        );
        assert!(errors.is_some());
        let errors = errors.unwrap();
        let err = errors.first().unwrap();
        assert_eq!(Some("\"CONNECT-4\""), err.highlight.as_deref());
        assert_eq!(
            "Invalid value \"CONNECT-4\" for type SportName; did you mean \"CHESS\"?",
            err.message.as_str()
        );

        // ------------------------------------------------------------------
        // Only CHESS is a valid game in this configuration, not the default,
        // which is "MY_DEFAULT"
        let errors = inspector.get_errors(
            r#"{
                "my-favorite-teams": {
                    "CHESS": {
                        "players": ["Mele", "Nona", "Pama"]
                    }
                }
            }"#
            .to_string(),
        );
        assert!(errors.is_some());
        let errors = errors.unwrap();
        let err = errors.first().unwrap();
        assert_eq!(Some("{"), err.highlight.as_deref());
        assert_eq!(
            "A valid value for sport of type SportName is missing",
            err.message.as_str()
        );

        // ----------------------------------------------------
        // Now CONNECT-4 is a valid game, but Donkey can't play
        let errors = inspector.get_errors(
            r#"{
                "my-favorite-teams": {
                    "CONNECT-4": {
                        "sport": "CONNECT-4",
                        "players": ["Nona", "Pama", "Donkey"]
                    }
                }
            }"#
            .to_string(),
        );
        assert!(errors.is_some());
        let errors = errors.unwrap();
        let err = errors.first().unwrap();
        assert_eq!(Some("\"Donkey\""), err.highlight.as_deref());

        // ------------------------------------------------------------------
        // Oh no! Donkey is the only available player, so Aka is highlighted
        // as in error.
        let errors = inspector.get_errors(
            r#"{
                "available-players": ["Donkey"],
                "my-favorite-teams": {
                    "CONNECT-4": {
                        "sport": "CONNECT-4",
                        "players": ["Donkey", "Aka"]
                    }
                }
            }"#
            .to_string(),
        );
        assert!(errors.is_some());
        let errors = errors.unwrap();
        let err = errors.first().unwrap();
        assert_eq!(Some("\"Aka\""), err.highlight.as_deref());

        // ------------------------------------------------------------
        // Surprise! Donkey is the only available player, for all games,
        let errors = inspector.get_errors(
            r#"{
                "available-players": ["Donkey"],
                "my-favorite-teams": {
                    "CONNECT-4": {
                        "sport": "CONNECT-4",
                        "players": ["Donkey", "Donkey", "Donkey"]
                    },
                    "CHESS": {
                        "sport": "CONNECT-4",
                        "players": ["Donkey", "Donkey"]
                    },
                    "GO": {
                        "sport": "CONNECT-4",
                        "players": ["Donkey"]
                    }
                }
            }"#
            .to_string(),
        );
        assert_eq!(None, errors);

        Ok(())
    }

    #[test]
    fn test_deeply_nested_objects_in_a_feature() -> Result<()> {
        let client = client("string-aliases.fml.yaml", "cyclones")?;
        let inspector = {
            let i = client.get_feature_inspector("my-fixture".to_string());
            assert!(i.is_some());
            i.unwrap()
        };

        // -> feature my-fixture:
        //      available-players: List<PlayerName> (PlayerName is the set of strings in this list)
        //      the-sport: SportName (SportName is the set of string containing only this value)
        //      the-match: Match
        // -> class Match:
        //      away: Team
        //      home: Team
        // -> class Team:
        //      sport: SportName
        //      players: List<PlayerName>

        // Happy path test.
        // All the sports match the-sport, and the players are all in the
        // available-players list.
        let errors = inspector.get_errors(
            r#"{
                "the-sport": "Archery",
                "the-match": {
                    "home": {
                        "sport": "Archery",
                        "players": ["Aka", "Hene", "Lino"]
                    },
                    "away": {
                        "sport": "Archery",
                        "players": ["Mele", "Nona", "Pama"]
                    }
                }
            }"#
            .to_string(),
        );
        assert_eq!(None, errors);

        // ----------------------------------------------------------------
        // All the sports need to match, because it's only set by the-sport.
        let errors = inspector.get_errors(
            r#"{
                "the-sport": "Karate",
                "the-match": {
                    "home": {
                        "sport": "Karate",
                        "players": ["Aka", "Hene", "Lino"]
                    },
                    "away": {
                        "sport": "Archery",
                        "players": ["Mele", "Nona", "Pama"]
                    }
                }
            }"#
            .to_string(),
        );
        assert!(errors.is_some());
        let errors = errors.unwrap();
        let err = errors.first().unwrap();
        assert_eq!(Some("\"Archery\""), err.highlight.as_deref());

        Ok(())
    }
}

#[cfg(test)]
mod error_messages {
    use crate::client::test_helper::client;

    use super::*;

    #[test]
    fn test_string_aliases() -> Result<()> {
        let client = client("string-aliases.fml.yaml", "cyclones")?;
        let inspector = {
            let i = client.get_feature_inspector("my-coverall-team".to_string());
            assert!(i.is_some());
            i.unwrap()
        };

        // An invalid boolean value of string alias type
        let error = {
            let errors = inspector.get_errors(
                r#"{
                    "players": ["George", "Mildred"],
                    "top-player": true
                }"#
                .to_string(),
            );
            assert!(errors.is_some());
            errors.unwrap().remove(0)
        };
        assert_eq!(
            error.message.as_str(),
            "Invalid value true for type PlayerName; did you mean \"George\" or \"Mildred\"?"
        );

        // An invalid string value of string alias type
        let error = {
            let errors = inspector.get_errors(
                r#"{
                    "players": ["George", "Mildred"],
                    "top-player": "Donkey"
                }"#
                .to_string(),
            );
            assert!(errors.is_some());
            errors.unwrap().remove(0)
        };
        assert_eq!(
            error.message.as_str(),
            "Invalid value \"Donkey\" for type PlayerName; did you mean \"George\" or \"Mildred\"?"
        );

        // An invalid key of string alias type should not suggest all values for the string-alias, just the unused ones.
        let error = {
            let errors = inspector.get_errors(
                r#"{
                    "players": ["George", "Mildred"],
                    "availability": {
                        "George": true,
                        "Donkey": true
                    }
                }"#
                .to_string(),
            );
            assert!(errors.is_some());
            errors.unwrap().remove(0)
        };
        assert_eq!(
            error.message.as_str(),
            "Invalid key \"Donkey\" for type PlayerName; did you mean \"Mildred\"?"
        );

        Ok(())
    }

    #[test]
    fn test_invalid_properties() -> Result<()> {
        let client = client("enums.fml.yaml", "release")?;
        let inspector = {
            let i = client.get_feature_inspector("my-coverall-feature".to_string());
            assert!(i.is_some());
            i.unwrap()
        };

        // An invalid property
        let error = {
            let errors = inspector.get_errors(
                r#"{
                    "invalid-property": true
                }"#
                .to_string(),
            );
            assert!(errors.is_some());
            errors.unwrap().remove(0)
        };
        assert_eq!(
            error.message.as_str(),
            "Invalid property \"invalid-property\"; did you mean one of \"list\", \"map\", \"optional\" or \"scalar\"?"
        );

        // An invalid property, with a suggestion missing out the ones already in use.
        let error = {
            let errors = inspector.get_errors(
                r#"{
                    "invalid-property": true,
                    "optional": null
                }"#
                .to_string(),
            );
            assert!(errors.is_some());
            errors.unwrap().remove(0)
        };
        assert_eq!(
            error.message.as_str(),
            "Invalid property \"invalid-property\"; did you mean one of \"list\", \"map\" or \"scalar\"?"
        );
        Ok(())
    }

    #[test]
    fn test_enums() -> Result<()> {
        let client = client("enums.fml.yaml", "release")?;
        let inspector = {
            let i = client.get_feature_inspector("my-coverall-feature".to_string());
            assert!(i.is_some());
            i.unwrap()
        };

        // An invalid boolean value of enum type
        let error = {
            let errors = inspector.get_errors(
                r#"{
                    "scalar": true
                }"#
                .to_string(),
            );
            assert!(errors.is_some());
            errors.unwrap().remove(0)
        };
        assert_eq!(
            error.message.as_str(),
            "Invalid value true for type ViewPosition; did you mean one of \"bottom\", \"middle\" or \"top\"?"
        );

        // An invalid string value of enum type
        let error = {
            let errors = inspector.get_errors(
                r#"{
                    "scalar": "invalid-value"
                }"#
                .to_string(),
            );
            assert!(errors.is_some());
            errors.unwrap().remove(0)
        };
        assert_eq!(
            error.message.as_str(),
            "Invalid value \"invalid-value\" for type ViewPosition; did you mean one of \"bottom\", \"middle\" or \"top\"?"
        );

        // An invalid key of enum type should not suggest all values for the string-alias, just the unused ones.
        let error = {
            let errors = inspector.get_errors(
                r#"{
                    "map": {
                        "top": true,
                        "invalid-key": true
                    }
                }"#
                .to_string(),
            );
            assert!(errors.is_some());
            errors.unwrap().remove(0)
        };
        assert_eq!(
            error.message.as_str(),
            "Invalid key \"invalid-key\" for type ViewPosition; did you mean \"bottom\" or \"middle\"?"
        );

        Ok(())
    }
}
