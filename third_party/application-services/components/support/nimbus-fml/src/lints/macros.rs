// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/// Register the lints for a module.
macro_rules! define_lints {
    ($($name:ident: $category:ident = $description:literal, $help:literal;)*) => {
        $(
            pub static $name: $crate::lints::LintInfo = $crate::lints::LintInfo {
                name: stringify!($name),
                description: $description,
                help: $help,
                category: $crate::lints::LintCategory::$category,
            };
        )*

        pub static LINTS: &[&$crate::lints::LintInfo] = &[$(&$name),*];
    };
}

/// Register all the available lint categories.
macro_rules! define_categories {
    ($($(#[$doc:meta])* $variant:ident = $name:literal => $linter:expr;)*) => {
        /// The category of a lint reported by `nimbus-fml lint`.
        ///
        /// Lints are grouped into categories.
        #[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, Serialize)]
        pub enum LintCategory {
            $(
                $(#[$doc])*
                #[serde(rename = $name)]
                $variant,
            )*
        }

        impl LintCategory {
            pub fn as_str(&self) -> &'static str {
                match self {
                    $(Self::$variant => $name,)*
                }
            }
        }

        /// Every module of lints, in `--list` order.
        static LINTERS: &[&dyn Linter] = &[$(&$linter),*];
    };
}

pub(crate) use define_categories;
pub(crate) use define_lints;
