/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use url::Url;

/// Identification of placement sent and returned from MARS (eg: `mock_spoc_1`)
#[cfg(feature = "stateful")]
#[derive(Clone, Debug, Hash, PartialEq, Eq)]
pub struct PlacementId(String);

#[cfg(feature = "stateful")]
impl PlacementId {
    pub fn new(s: &str) -> PlacementId {
        PlacementId(s.to_string())
    }
    pub fn into_inner(self) -> String {
        self.0
    }
}

#[cfg(feature = "stateful")]
impl AsRef<str> for PlacementId {
    fn as_ref(&self) -> &str {
        &self.0
    }
}

#[cfg(feature = "stateful")]
impl From<String> for PlacementId {
    fn from(value: String) -> Self {
        PlacementId(value)
    }
}

#[cfg(feature = "stateful")]
impl From<PlacementId> for String {
    fn from(value: PlacementId) -> Self {
        value.0
    }
}

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub struct AdImage {
    pub alt_text: Option<String>,
    pub block_key: String,
    pub callbacks: AdCallbacks,
    pub format: String,
    pub image_url: Url,
    pub url: Url,
}

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub struct AdSpoc {
    pub block_key: String,
    pub callbacks: AdCallbacks,
    pub caps: SpocFrequencyCaps,
    pub domain: String,
    pub excerpt: String,
    pub format: String,
    pub image_url: Url,
    pub ranking: SpocRanking,
    pub sponsor: String,
    pub sponsored_by_override: Option<String>,
    pub title: String,
    pub url: Url,
}

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub struct AdTile {
    pub block_key: String,
    pub callbacks: AdCallbacks,
    pub format: String,
    pub image_url: Url,
    pub name: String,
    pub url: Url,
}

/// One ad as MARS sends it, in the shape its `format` calls for.
#[derive(Clone, Debug, PartialEq, Serialize)]
#[serde(untagged)]
pub enum Ad {
    Image(AdImage),
    Spoc(AdSpoc),
    Tile(AdTile),
}

/// The `format` IDs MARS sends.
#[derive(Clone, Copy, Debug, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
enum AdFormat {
    Billboard,
    Leaderboard,
    Rectangle,
    Skyscraper,
    Spoc,
    Tile,
}

impl<'de> Deserialize<'de> for Ad {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: serde::Deserializer<'de>,
    {
        use serde::de::Error;
        let value = <serde_json::Value>::deserialize(deserializer)?;
        let format = value
            .get("format")
            .ok_or_else(|| D::Error::missing_field("format"))?;
        match AdFormat::deserialize(format).map_err(D::Error::custom)? {
            AdFormat::Billboard
            | AdFormat::Leaderboard
            | AdFormat::Rectangle
            | AdFormat::Skyscraper => serde_json::from_value(value).map(Self::Image),
            AdFormat::Spoc => serde_json::from_value(value).map(Self::Spoc),
            AdFormat::Tile => serde_json::from_value(value).map(Self::Tile),
        }
        .map_err(D::Error::custom)
    }
}

impl Ad {
    pub fn callbacks_mut(&mut self) -> &mut AdCallbacks {
        match self {
            Ad::Image(ad) => &mut ad.callbacks,
            Ad::Spoc(ad) => &mut ad.callbacks,
            Ad::Tile(ad) => &mut ad.callbacks,
        }
    }

    pub fn cap_key(&self) -> Option<String> {
        match self {
            Ad::Spoc(ad) => Some(ad.caps.cap_key.clone()),
            Ad::Image(_) | Ad::Tile(_) => None,
        }
    }

    fn unexpected(&self, expected: &'static str) -> UnexpectedAdKind {
        let format = match self {
            Ad::Image(ad) => &ad.format,
            Ad::Spoc(ad) => &ad.format,
            Ad::Tile(ad) => &ad.format,
        };
        UnexpectedAdKind {
            expected,
            format: format.clone(),
        }
    }
}

/// An ad of another kind than the one asked for.
#[derive(Debug, thiserror::Error)]
#[error("Expected {expected}, got an ad with format `{format}`")]
pub struct UnexpectedAdKind {
    expected: &'static str,
    format: String,
}

impl TryFrom<Ad> for AdImage {
    type Error = UnexpectedAdKind;

    fn try_from(ad: Ad) -> Result<Self, Self::Error> {
        match ad {
            Ad::Image(ad) => Ok(ad),
            other => Err(other.unexpected("an image")),
        }
    }
}

impl TryFrom<Ad> for AdSpoc {
    type Error = UnexpectedAdKind;

    fn try_from(ad: Ad) -> Result<Self, Self::Error> {
        match ad {
            Ad::Spoc(ad) => Ok(ad),
            other => Err(other.unexpected("a spoc")),
        }
    }
}

impl TryFrom<Ad> for AdTile {
    type Error = UnexpectedAdKind;

    fn try_from(ad: Ad) -> Result<Self, Self::Error> {
        match ad {
            Ad::Tile(ad) => Ok(ad),
            other => Err(other.unexpected("a tile")),
        }
    }
}

impl From<AdImage> for Ad {
    fn from(ad: AdImage) -> Self {
        Ad::Image(ad)
    }
}

impl From<AdSpoc> for Ad {
    fn from(ad: AdSpoc) -> Self {
        Ad::Spoc(ad)
    }
}

impl From<AdTile> for Ad {
    fn from(ad: AdTile) -> Self {
        Ad::Tile(ad)
    }
}

#[derive(Clone, Debug, PartialEq, Serialize)]
#[serde(untagged)]
pub enum AdImageOrSpoc {
    Image(AdImage),
    Spoc(AdSpoc),
}

impl TryFrom<Ad> for AdImageOrSpoc {
    type Error = UnexpectedAdKind;

    fn try_from(ad: Ad) -> Result<Self, Self::Error> {
        match ad {
            Ad::Image(ad) => Ok(AdImageOrSpoc::Image(ad)),
            Ad::Spoc(ad) => Ok(AdImageOrSpoc::Spoc(ad)),
            other => Err(other.unexpected("an image or a spoc")),
        }
    }
}

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub struct SpocFrequencyCaps {
    pub cap_key: String,
    pub day: u32,
}

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub struct SpocRanking {
    pub item_score: f64,
    pub personalization_models: Option<HashMap<String, u32>>,
    pub priority: u32,
}

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub struct AdCallbacks {
    pub click: Url,
    pub impression: Url,
    pub report: Option<Url>,
}

#[cfg(test)]
mod tests {
    use crate::{
        ads::{Ad, AdImageOrSpoc},
        test_utils::{
            get_example_happy_image_response, get_example_happy_spoc_response,
            get_example_happy_uatile_response,
        },
    };

    fn first_ad_json(ads: Vec<Ad>) -> serde_json::Value {
        serde_json::to_value(ads.into_iter().next().unwrap()).unwrap()
    }

    #[test]
    fn ad_deserializes_to_the_shape_its_format_calls_for() {
        let image = get_example_happy_image_response()
            .data
            .into_values()
            .next()
            .unwrap();
        let spoc = get_example_happy_spoc_response()
            .data
            .into_values()
            .next()
            .unwrap();
        let tile = get_example_happy_uatile_response()
            .data
            .into_values()
            .next()
            .unwrap();

        let image: Ad = serde_json::from_value(first_ad_json(image)).unwrap();
        assert!(matches!(image, Ad::Image(_)));
        let spoc: Ad = serde_json::from_value(first_ad_json(spoc)).unwrap();
        assert!(matches!(spoc, Ad::Spoc(_)));
        let tile: Ad = serde_json::from_value(first_ad_json(tile)).unwrap();
        assert!(matches!(tile, Ad::Tile(_)));
    }

    #[test]
    fn ad_image_or_spoc_takes_images_and_spocs_but_not_tiles() {
        let image = get_example_happy_image_response()
            .data
            .into_values()
            .next()
            .unwrap();
        let spoc = get_example_happy_spoc_response()
            .data
            .into_values()
            .next()
            .unwrap();
        let tile = get_example_happy_uatile_response()
            .data
            .into_values()
            .next()
            .unwrap();

        let image: Ad = serde_json::from_value(first_ad_json(image)).unwrap();
        assert!(matches!(
            AdImageOrSpoc::try_from(image),
            Ok(AdImageOrSpoc::Image(_))
        ));
        let spoc: Ad = serde_json::from_value(first_ad_json(spoc)).unwrap();
        assert!(matches!(
            AdImageOrSpoc::try_from(spoc),
            Ok(AdImageOrSpoc::Spoc(_))
        ));
        let tile: Ad = serde_json::from_value(first_ad_json(tile)).unwrap();
        assert_eq!(
            AdImageOrSpoc::try_from(tile).unwrap_err().to_string(),
            "Expected an image or a spoc, got an ad with format `tile`"
        );
    }

    #[test]
    fn ad_with_an_iab_format_is_an_image() {
        let image = get_example_happy_image_response()
            .data
            .into_values()
            .next()
            .unwrap();
        let mut image = first_ad_json(image);

        for format in ["billboard", "leaderboard", "rectangle", "skyscraper"] {
            image["format"] = format.into();
            let ad: Ad = serde_json::from_value(image.clone()).unwrap();
            assert!(matches!(ad, Ad::Image(_)), "{format}");
        }
    }

    #[test]
    fn ad_with_an_unknown_or_missing_format_is_rejected() {
        let image = get_example_happy_image_response()
            .data
            .into_values()
            .next()
            .unwrap();
        let mut image = first_ad_json(image);

        image["format"] = "nonsense_format".into();
        let err = serde_json::from_value::<Ad>(image.clone()).unwrap_err();
        assert!(err
            .to_string()
            .starts_with("unknown variant `nonsense_format`"));

        image.as_object_mut().unwrap().remove("format");
        assert!(serde_json::from_value::<Ad>(image).is_err());
    }
}
