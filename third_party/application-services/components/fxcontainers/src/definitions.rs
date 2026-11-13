/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::collections::HashMap;

/// Everything a color is, kept on one line per color. Borrowed, so it stays
/// inside the crate: borrowed data cannot cross an FFI boundary.
#[derive(Clone, Copy)]
struct ColorDef {
    name: &'static str,
    code: &'static str,
    code_nova: &'static str,
    gecko_l10n_id: &'static str,
}

/// The colors a container can carry.
///
/// A stored document holds the name, not the variant, and the same caveats as
/// [`ContainerIcon`] apply. Legacy names are not variants: they are resolved to
/// their canonical replacement by [`resolve_color`].
#[derive(Clone, Copy, Debug, PartialEq, Eq, uniffi::Enum)]
pub enum ContainerColor {
    Gray,
    Yellow,
    Orange,
    Red,
    Pink,
    Purple,
    Violet,
    Blue,
    Cyan,
    Green,
}

impl ContainerColor {
    /// Every color, in the order the container editor offers them.
    pub const ALL: &'static [Self] = &[
        Self::Gray,
        Self::Yellow,
        Self::Orange,
        Self::Red,
        Self::Pink,
        Self::Purple,
        Self::Violet,
        Self::Blue,
        Self::Cyan,
        Self::Green,
    ];

    fn def(self) -> ColorDef {
        /// Positional, in the order [`ColorDef`] declares: the Fluent id is
        /// spelled out rather than built from the name, so that Firefox can
        /// rename one without renaming the other.
        macro_rules! def {
            ($name:literal, $code:literal, $code_nova:literal, $gecko_l10n_id:literal) => {
                ColorDef {
                    name: $name,
                    code: $code,
                    code_nova: $code_nova,
                    gecko_l10n_id: $gecko_l10n_id,
                }
            };
        }

        match self {
            Self::Gray => def!("gray", "#7c7c7d", "#949297", "user-context-color-gray"),
            Self::Yellow => def!("yellow", "#ffcb00", "#db820e", "user-context-color-yellow"),
            Self::Orange => def!("orange", "#ff9f00", "#f4682c", "user-context-color-orange"),
            Self::Red => def!("red", "#ff613d", "#ed566e", "user-context-color-red"),
            Self::Pink => def!("pink", "#ff4bda", "#db54bf", "user-context-color-pink"),
            Self::Purple => def!("purple", "#af51f5", "#b864ee", "user-context-color-purple"),
            Self::Violet => def!("violet", "#764edd", "#9871ff", "user-context-color-violet"),
            Self::Blue => def!("blue", "#37adff", "#5a87fd", "user-context-color-blue"),
            Self::Cyan => def!("cyan", "#00c79a", "#10a4ca", "user-context-color-cyan"),
            Self::Green => def!("green", "#51cd00", "#11ae84", "user-context-color-green"),
        }
    }

    /// The name the document stores.
    pub fn name(self) -> &'static str {
        self.def().name
    }

    /// The canonical color a stored document, an enterprise policy or a
    /// WebExtension names, or `None` when the crate has no such color. A legacy
    /// name has to go through [`resolve_color`] first.
    pub fn from_name(name: &str) -> Option<Self> {
        Self::ALL.iter().copied().find(|color| color.name() == name)
    }

    /// `nova` picks the refreshed value over the legacy one. Which to use
    /// depends on a setting the embedder owns, so the caller decides.
    pub fn code(self, nova: bool) -> &'static str {
        let def = self.def();
        if nova {
            def.code_nova
        } else {
            def.code
        }
    }

    /// The Fluent id Firefox Desktop labels the color with. See
    /// [`ContainerIcon::gecko_l10n_id`] on why this is per platform.
    pub fn gecko_l10n_id(self) -> &'static str {
        self.def().gecko_l10n_id
    }
}

/// How a container gets its label.
#[derive(Clone, Debug, PartialEq, Eq, uniffi::Enum)]
pub enum ContainerLabel {
    Name { name: String },
    Personal,
    Work,
    Banking,
    Shopping,
}

impl ContainerLabel {
    /// The Fluent id Firefox Desktop labels the container with, or `None` for a
    /// container that carries its own name. See
    /// [`ContainerIcon::gecko_l10n_id`] on why this is per platform.
    pub fn gecko_l10n_id(&self) -> Option<&'static str> {
        match self {
            Self::Name { .. } => None,
            Self::Personal => Some("user-context-personal2"),
            Self::Work => Some("user-context-work2"),
            Self::Banking => Some("user-context-banking2"),
            Self::Shopping => Some("user-context-shopping2"),
        }
    }
}

/// The icons a container can carry. The set is fixed, so a container cannot end
/// up with an icon the embedder has no artwork for.
///
/// A stored document holds the name, not the variant: one written by a newer
/// Firefox can carry an icon this crate does not know, which reads back as
/// `None` and is left in the document untouched.
#[derive(Clone, Copy, Debug, PartialEq, Eq, uniffi::Enum)]
pub enum ContainerIcon {
    Fingerprint,
    Briefcase,
    Dollar,
    Cart,
    Vacation,
    Gift,
    Food,
    Fruit,
    Pet,
    Tree,
    Chill,
    Circle,
    Fence,
}

impl ContainerIcon {
    /// Every icon, in the order the container editor offers them. The first is
    /// the one a new container starts with.
    pub const ALL: &'static [Self] = &[
        Self::Fingerprint,
        Self::Briefcase,
        Self::Dollar,
        Self::Cart,
        Self::Vacation,
        Self::Gift,
        Self::Food,
        Self::Fruit,
        Self::Pet,
        Self::Tree,
        Self::Chill,
        Self::Circle,
        Self::Fence,
    ];

    /// The name the document stores.
    pub fn name(self) -> &'static str {
        match self {
            Self::Fingerprint => "fingerprint",
            Self::Briefcase => "briefcase",
            Self::Dollar => "dollar",
            Self::Cart => "cart",
            Self::Vacation => "vacation",
            Self::Gift => "gift",
            Self::Food => "food",
            Self::Fruit => "fruit",
            Self::Pet => "pet",
            Self::Tree => "tree",
            Self::Chill => "chill",
            Self::Circle => "circle",
            Self::Fence => "fence",
        }
    }

    /// The icon a stored document, an enterprise policy or a WebExtension
    /// names, or `None` when the crate has no such icon.
    pub fn from_name(name: &str) -> Option<Self> {
        Self::ALL.iter().copied().find(|icon| icon.name() == name)
    }

    /// The Fluent id Firefox Desktop labels the icon with, so that the embedder
    /// does not keep a table of its own. Naming schemes do not carry across
    /// platforms, so each one that needs it gets its own accessor.
    pub fn gecko_l10n_id(self) -> &'static str {
        match self {
            Self::Fingerprint => "user-context-icon-fingerprint",
            Self::Briefcase => "user-context-icon-briefcase",
            Self::Dollar => "user-context-icon-dollar",
            Self::Cart => "user-context-icon-cart",
            Self::Vacation => "user-context-icon-vacation",
            Self::Gift => "user-context-icon-gift",
            Self::Food => "user-context-icon-food",
            Self::Fruit => "user-context-icon-fruit",
            Self::Pet => "user-context-icon-pet",
            Self::Tree => "user-context-icon-tree",
            Self::Chill => "user-context-icon-chill",
            Self::Circle => "user-context-icon-circle",
            Self::Fence => "user-context-icon-fence",
        }
    }
}

/// Legacy color names, accepted at the WebExtension API boundary and rewritten
/// to their canonical replacement by the 5 -> 6 migration.
const ALIASES: &[(&str, &str)] = &[("turquoise", "cyan"), ("toolbar", "gray")];

#[uniffi::export]
pub fn container_colors() -> Vec<ContainerColor> {
    ContainerColor::ALL.to_vec()
}

#[uniffi::export]
pub fn container_color_aliases() -> HashMap<String, String> {
    ALIASES
        .iter()
        .map(|(legacy, canonical)| (legacy.to_string(), canonical.to_string()))
        .collect()
}

#[uniffi::export]
pub fn resolve_color(name: &str) -> String {
    ALIASES
        .iter()
        .find(|(alias, _)| *alias == name)
        .map(|(_, canonical)| (*canonical).to_string())
        .unwrap_or_else(|| name.to_string())
}

#[uniffi::export]
pub fn container_icons() -> Vec<ContainerIcon> {
    ContainerIcon::ALL.to_vec()
}

#[uniffi::export]
pub fn icon_from_name(name: &str) -> Option<ContainerIcon> {
    ContainerIcon::from_name(name)
}

#[uniffi::export]
pub fn icon_name(icon: ContainerIcon) -> String {
    icon.name().to_string()
}

#[uniffi::export]
pub fn icon_gecko_l10n_id(icon: ContainerIcon) -> String {
    icon.gecko_l10n_id().to_string()
}

#[uniffi::export]
pub fn label_gecko_l10n_id(label: ContainerLabel) -> Option<String> {
    label.gecko_l10n_id().map(str::to_string)
}

#[uniffi::export]
pub fn color_from_name(name: &str) -> Option<ContainerColor> {
    ContainerColor::from_name(name)
}

#[uniffi::export]
pub fn color_name(color: ContainerColor) -> String {
    color.name().to_string()
}

#[uniffi::export]
pub fn color_code(color: ContainerColor, nova: bool) -> String {
    color.code(nova).to_string()
}

#[uniffi::export]
pub fn color_gecko_l10n_id(color: ContainerColor) -> String {
    color.gecko_l10n_id().to_string()
}

#[cfg(test)]
mod tests {
    use std::collections::HashSet;

    use super::*;

    #[test]
    fn every_icon_is_named_and_labelled_on_its_own() {
        let names: HashSet<_> = ContainerIcon::ALL.iter().map(|icon| icon.name()).collect();
        let l10n_ids: HashSet<_> = ContainerIcon::ALL
            .iter()
            .map(|icon| icon.gecko_l10n_id())
            .collect();

        assert_eq!(names.len(), ContainerIcon::ALL.len());
        assert_eq!(l10n_ids.len(), ContainerIcon::ALL.len());
        assert!(ContainerIcon::ALL
            .iter()
            .all(|icon| ContainerIcon::from_name(icon.name()) == Some(*icon)));
    }

    #[test]
    fn every_color_is_named_and_labelled_on_its_own() {
        let all = ContainerColor::ALL;
        let names: HashSet<_> = all.iter().map(|color| color.name()).collect();
        let l10n_ids: HashSet<_> = all.iter().map(|color| color.gecko_l10n_id()).collect();
        let codes: HashSet<_> = all
            .iter()
            .flat_map(|color| [color.code(false), color.code(true)])
            .collect();

        assert_eq!(names.len(), all.len());
        assert_eq!(l10n_ids.len(), all.len());
        assert_eq!(codes.len(), all.len() * 2, "legacy and refreshed differ");
        assert!(all
            .iter()
            .all(|color| ContainerColor::from_name(color.name()) == Some(*color)));
    }

    /// A legacy name is not a variant: it resolves to its replacement, and a
    /// name that is neither is left alone for the document to keep.
    #[test]
    fn a_legacy_color_resolves_to_its_replacement() {
        assert_eq!(resolve_color("turquoise"), "cyan");
        assert_eq!(resolve_color("toolbar"), "gray");
        assert_eq!(resolve_color("blue"), "blue");
        assert_eq!(resolve_color("chartreuse"), "chartreuse");

        assert_eq!(ContainerColor::from_name("turquoise"), None);
        assert_eq!(
            ContainerColor::from_name(&resolve_color("turquoise")),
            Some(ContainerColor::Cyan)
        );
    }

    #[test]
    fn every_default_label_has_its_own_fluent_id() {
        let defaults = [
            ContainerLabel::Personal,
            ContainerLabel::Work,
            ContainerLabel::Banking,
            ContainerLabel::Shopping,
        ];
        let l10n_ids: HashSet<_> = defaults
            .iter()
            .map(|label| label.gecko_l10n_id().expect("a default is localized"))
            .collect();

        assert_eq!(l10n_ids.len(), defaults.len());
        assert_eq!(
            ContainerLabel::Name {
                name: "Mine".into()
            }
            .gecko_l10n_id(),
            None,
            "a name the user chose is not localized"
        );
    }
}
