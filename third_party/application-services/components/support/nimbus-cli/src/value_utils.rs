// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use anyhow::Result;
use serde::{Deserialize, Serialize};
use serde_json::{Map, Value};
use std::collections::HashMap;
use std::path::Path;

use crate::NimbusApp;

pub(crate) trait CliUtils {
    fn get_str<'a>(&'a self, key: &str) -> Result<&'a str>;
    fn get_bool(&self, key: &str) -> Result<bool>;
    fn get_array<'a>(&'a self, key: &str) -> Result<&'a Vec<Value>>;
    fn get_mut_array<'a>(&'a mut self, key: &str) -> Result<&'a mut Vec<Value>>;
    fn get_mut_object<'a>(&'a mut self, key: &str) -> Result<&'a mut Value>;
    fn get_object<'a>(&'a self, key: &str) -> Result<&'a Value>;
    fn get_u64(&self, key: &str) -> Result<u64>;

    fn has(&self, key: &str) -> bool;
    fn set<V>(&mut self, key: &str, value: V) -> Result<()>
    where
        V: Serialize;
}

impl CliUtils for Value {
    fn get_str<'a>(&'a self, key: &str) -> Result<&'a str> {
        let v = self
            .get(key)
            .ok_or_else(|| {
                anyhow::Error::msg(format!(
                    "Expected a string with key '{key}' in the JSONObject"
                ))
            })?
            .as_str()
            .ok_or_else(|| anyhow::Error::msg("value is not a string"))?;

        Ok(v)
    }

    fn get_bool(&self, key: &str) -> Result<bool> {
        let v = self
            .get(key)
            .ok_or_else(|| {
                anyhow::Error::msg(format!(
                    "Expected a string with key '{key}' in the JSONObject"
                ))
            })?
            .as_bool()
            .ok_or_else(|| anyhow::Error::msg("value is not a string"))?;

        Ok(v)
    }

    fn get_array<'a>(&'a self, key: &str) -> Result<&'a Vec<Value>> {
        let v = self
            .get(key)
            .ok_or_else(|| {
                anyhow::Error::msg(format!(
                    "Expected an array with key '{key}' in the JSONObject"
                ))
            })?
            .as_array()
            .ok_or_else(|| anyhow::Error::msg("value is not a array"))?;
        Ok(v)
    }

    fn get_mut_array<'a>(&'a mut self, key: &str) -> Result<&'a mut Vec<Value>> {
        let v = self
            .get_mut(key)
            .ok_or_else(|| {
                anyhow::Error::msg(format!(
                    "Expected an array with key '{key}' in the JSONObject"
                ))
            })?
            .as_array_mut()
            .ok_or_else(|| anyhow::Error::msg("value is not a array"))?;
        Ok(v)
    }

    fn get_object<'a>(&'a self, key: &str) -> Result<&'a Value> {
        let v = self.get(key).ok_or_else(|| {
            anyhow::Error::msg(format!(
                "Expected an object with key '{key}' in the JSONObject"
            ))
        })?;
        Ok(v)
    }

    fn get_mut_object<'a>(&'a mut self, key: &str) -> Result<&'a mut Value> {
        let v = self.get_mut(key).ok_or_else(|| {
            anyhow::Error::msg(format!(
                "Expected an object with key '{key}' in the JSONObject"
            ))
        })?;
        Ok(v)
    }

    fn get_u64(&self, key: &str) -> Result<u64> {
        let v = self
            .get(key)
            .ok_or_else(|| {
                anyhow::Error::msg(format!(
                    "Expected an array with key '{key}' in the JSONObject"
                ))
            })?
            .as_u64()
            .ok_or_else(|| anyhow::Error::msg("value is not a array"))?;
        Ok(v)
    }

    fn set<V>(&mut self, key: &str, value: V) -> Result<()>
    where
        V: Serialize,
    {
        let value = serde_json::to_value(value)?;
        match self.as_object_mut() {
            Some(m) => m.insert(key.to_string(), value),
            _ => anyhow::bail!("Can only insert into JSONObjects"),
        };
        Ok(())
    }

    fn has(&self, key: &str) -> bool {
        self.get(key).is_some()
    }
}

pub(crate) fn try_find_experiment(value: &Value, slug: &str) -> Result<Value> {
    let array = try_extract_data_list(value)?;
    let exp = array
        .iter()
        .find(|exp| {
            if let Some(Value::String(s)) = exp.get("slug") {
                slug == s
            } else {
                false
            }
        })
        .ok_or_else(|| anyhow::Error::msg(format!("No experiment with slug {}", slug)))?;

    Ok(exp.clone())
}

pub(crate) fn try_extract_data_list(value: &Value) -> Result<Vec<Value>> {
    assert!(value.is_object());
    Ok(value.get_array("data")?.to_vec())
}

pub(crate) fn try_find_branches_from_experiment(value: &Value) -> Result<Vec<Value>> {
    Ok(value.get_array("branches")?.to_vec())
}

pub(crate) fn try_find_features_from_branch(value: &Value) -> Result<Vec<Value>> {
    let features = value.get_array("features");
    Ok(if features.is_ok() {
        features?.to_vec()
    } else {
        let feature = value
            .get("feature")
            .expect("Expected a feature or features in a branch");
        vec![feature.clone()]
    })
}

pub(crate) fn try_find_mut_features_from_branch<'a>(
    value: &'a mut Value,
) -> Result<HashMap<String, &'a mut Value>> {
    let mut res = HashMap::new();
    if value.has("features") {
        let features = value.get_mut_array("features")?;
        for f in features {
            res.insert(
                f.get_str("featureId")?.to_string(),
                f.get_mut_object("value")?,
            );
        }
    } else {
        let f: &'a mut Value = value.get_mut_object("feature")?;
        res.insert(
            f.get_str("featureId")?.to_string(),
            f.get_mut_object("value")?,
        );
    }
    Ok(res)
}

pub(crate) trait Patch {
    fn patch(&mut self, patch: &Self) -> bool;
}

impl Patch for Value {
    fn patch(&mut self, patch: &Self) -> bool {
        match (self, patch) {
            (Value::Object(t), Value::Object(p)) => {
                t.patch(p);
            }
            (Value::String(t), Value::String(p)) => t.clone_from(p),
            (Value::Bool(t), Value::Bool(p)) => *t = *p,
            (Value::Number(t), Value::Number(p)) => *t = p.clone(),
            (Value::Array(t), Value::Array(p)) => t.clone_from(p),
            (Value::Null, Value::Null) => (),
            _ => return false,
        };
        true
    }
}

impl Patch for Map<String, Value> {
    fn patch(&mut self, patch: &Self) -> bool {
        for (k, v) in patch {
            match (self.get_mut(k), v) {
                (Some(_), Value::Null) => {
                    self.remove(k);
                }
                (_, Value::Null) => {
                    // If the patch is null, then don't add it to this value.
                }
                (Some(t), p) => {
                    if !t.patch(p) {
                        println!("Warning: the patched key '{k}' has different types: {t} != {p}");
                        self.insert(k.clone(), v.clone());
                    }
                }
                (None, _) => {
                    self.insert(k.clone(), v.clone());
                }
            }
        }
        true
    }
}

fn prepare_recipe(
    recipe: &Value,
    params: &NimbusApp,
    preserve_targeting: bool,
    preserve_bucketing: bool,
) -> Result<Value> {
    let mut recipe = recipe.clone();
    let slug = recipe.get_str("slug")?;
    let app_name = params
        .app_name
        .as_deref()
        .expect("An app name is expected. This is a bug in nimbus-cli");
    if app_name != recipe.get_str("appName")? {
        anyhow::bail!(format!("'{slug}' is not for {app_name} app"));
    }
    recipe.set("channel", &params.channel)?;
    recipe.set("isEnrollmentPaused", false)?;
    if !preserve_targeting {
        recipe.set("targeting", "true")?;
    }
    if !preserve_bucketing {
        let bucketing = recipe.get_mut_object("bucketConfig")?;
        bucketing.set("start", 0)?;
        bucketing.set("count", 10_000)?;
    }
    Ok(recipe)
}

pub(crate) fn prepare_rollout(
    recipe: &Value,
    params: &NimbusApp,
    preserve_targeting: bool,
    preserve_bucketing: bool,
) -> Result<Value> {
    let rollout = prepare_recipe(recipe, params, preserve_targeting, preserve_bucketing)?;
    if !rollout.get_bool("isRollout")? {
        let slug = rollout.get_str("slug")?;
        anyhow::bail!(format!("Recipe '{}' isn't a rollout", slug));
    }
    Ok(rollout)
}

pub(crate) fn prepare_experiment(
    recipe: &Value,
    params: &NimbusApp,
    branch: &str,
    preserve_targeting: bool,
    preserve_bucketing: bool,
) -> Result<Value> {
    let mut experiment = prepare_recipe(recipe, params, preserve_targeting, preserve_bucketing)?;

    if !preserve_bucketing {
        let branches = experiment.get_mut_array("branches")?;
        let mut found = false;
        for b in branches {
            let slug = b.get_str("slug")?;
            let ratio = if slug == branch {
                found = true;
                100
            } else {
                0
            };
            b.set("ratio", ratio)?;
        }
        if !found {
            let slug = experiment.get_str("slug")?;
            anyhow::bail!(format!(
                "No branch called '{}' was found in '{}'",
                branch, slug
            ));
        }
    }
    Ok(experiment)
}

fn is_yaml<P>(file: P) -> bool
where
    P: AsRef<Path>,
{
    let ext = file.as_ref().extension().unwrap_or_default();
    ext == "yaml" || ext == "yml"
}

pub(crate) fn read_from_file<P, T>(file: P) -> Result<T>
where
    P: AsRef<Path>,
    for<'a> T: Deserialize<'a>,
{
    let s = std::fs::read_to_string(&file)?;
    Ok(if is_yaml(&file) {
        serde_yaml::from_str(&s)?
    } else {
        serde_json::from_str(&s)?
    })
}

pub(crate) fn write_to_file_or_print<P, T>(file: Option<P>, contents: &T) -> Result<()>
where
    P: AsRef<Path>,
    T: Serialize,
{
    match file {
        Some(file) => {
            let s = if is_yaml(&file) {
                serde_yaml::to_string(&contents)?
            } else {
                serde_json::to_string_pretty(&contents)?
            };
            std::fs::write(file, s)?;
        }
        _ => println!("{}", serde_json::to_string_pretty(&contents)?),
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use serde_json::json;

    use super::*;

    #[test]
    fn test_find_experiment() -> Result<()> {
        let exp = json!({
            "slug": "a-name",
        });
        let source = json!({ "data": [exp] });

        assert_eq!(try_find_experiment(&source, "a-name")?, exp);

        let source = json!({
            "data": {},
        });
        assert!(try_find_experiment(&source, "a-name").is_err());

        let source = json!({
            "data": [],
        });
        assert!(try_find_experiment(&source, "a-name").is_err());

        Ok(())
    }

    #[test]
    fn test_prepare_experiment() -> Result<()> {
        let src = json!({
            "appName": "an-app",
            "slug": "a-name",
            "branches": [
                {
                    "slug": "another-branch",
                },
                {
                    "slug": "a-branch",
                }
            ],
            "bucketConfig": {
            }
        });

        let params = NimbusApp::new("an-app", "developer");

        assert_eq!(
            json!({
                "appName": "an-app",
                "channel": "developer",
                "slug": "a-name",
                "branches": [
                    {
                        "slug": "another-branch",
                        "ratio": 0,
                    },
                    {
                        "slug": "a-branch",
                        "ratio": 100,
                    }
                ],
                "bucketConfig": {
                    "start": 0,
                    "count": 10_000,
                },
                "isEnrollmentPaused": false,
                "targeting": "true"
            }),
            prepare_experiment(&src, &params, "a-branch", false, false)?
        );

        assert_eq!(
            json!({
                "appName": "an-app",
                "channel": "developer",
                "slug": "a-name",
                "branches": [
                    {
                        "slug": "another-branch",
                    },
                    {
                        "slug": "a-branch",
                    }
                ],
                "bucketConfig": {
                },
                "isEnrollmentPaused": false,
                "targeting": "true"
            }),
            prepare_experiment(&src, &params, "a-branch", false, true)?
        );

        assert_eq!(
            json!({
                "appName": "an-app",
                "channel": "developer",
                "slug": "a-name",
                "branches": [
                    {
                        "slug": "another-branch",
                        "ratio": 0,
                    },
                    {
                        "slug": "a-branch",
                        "ratio": 100,
                    }
                ],
                "bucketConfig": {
                    "start": 0,
                    "count": 10_000,
                },
                "isEnrollmentPaused": false,
            }),
            prepare_experiment(&src, &params, "a-branch", true, false)?
        );

        assert_eq!(
            json!({
                "appName": "an-app",
                "slug": "a-name",
                "channel": "developer",
                "branches": [
                    {
                        "slug": "another-branch",
                    },
                    {
                        "slug": "a-branch",
                    }
                ],
                "bucketConfig": {
                },
                "isEnrollmentPaused": false,
            }),
            prepare_experiment(&src, &params, "a-branch", true, true)?
        );
        Ok(())
    }

    #[test]
    fn test_patch_value() -> Result<()> {
        let mut v1 = json!({
            "string": "string",
            "obj": {
                "string": "string",
                "num": 1,
                "bool": false,
            },
            "num": 1,
            "bool": false,
        });
        let ov1 = json!({
            "string": "patched",
            "obj": {
                "string": "patched",
            },
            "num": 2,
            "bool": true,
        });

        v1.patch(&ov1);

        let expected = json!({
            "string": "patched",
            "obj": {
                "string": "patched",
                "num": 1,
                "bool": false,
            },
            "num": 2,
            "bool": true,
        });

        assert_eq!(&expected, &v1);

        let mut v1 = json!({
            "string": "string",
            "obj": {
                "string": "string",
                "num": 1,
                "bool": false,
            },
            "num": 1,
            "bool": false,
        });
        let ov1 = json!({
            "obj": null,
            "never": null,
        });
        v1.patch(&ov1);
        let expected = json!({
            "string": "string",
            "num": 1,
            "bool": false,
        });

        assert_eq!(&expected, &v1);

        Ok(())
    }
}
