/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

use crate::derives::*;

type AtomThinVec = thin_vec::ThinVec<Atom>;

/// Gecko's pseudo-element definition.
///
/// We intentionally double-box legacy ::-moz-tree pseudo-elements to keep the
/// size of PseudoElement (and thus selector components) small.
#[derive(Clone, Debug, Eq, Hash, MallocSizeOf, PartialEq, ToShmem)]
#[repr(u8)]
pub enum PseudoElement {
    % for pseudo in PSEUDOS:
    /// ${pseudo.name}
    % if pseudo.argument:
    ${pseudo.capitalized}(${pseudo.argument}),
    % else:
    ${pseudo.capitalized},
    % endif
    % endfor
    /// ::-webkit-* that we don't recognize
    /// https://github.com/whatwg/compat/issues/103
    UnknownWebkit(Atom),
}

<% EAGER_PSEUDOS = list(filter(lambda p: getattr(p, 'is_eager', False), PSEUDOS)) %>

/// The number of eager pseudo-elements.
pub const EAGER_PSEUDO_COUNT: usize = ${len(EAGER_PSEUDOS)};

/// The number of all pseudo-elements.
pub const PSEUDO_COUNT: usize = ${len(PSEUDOS)};

/// The list of eager pseudos.
pub const EAGER_PSEUDOS: [PseudoElement; EAGER_PSEUDO_COUNT] = [
    % for eager_pseudo in EAGER_PSEUDOS:
    PseudoElement::${eager_pseudo.capitalized},
    % endfor
];

<%def name="pseudo_element_variant(pseudo, arg='..')">\
PseudoElement::${pseudo.capitalized}${"({})".format(arg) if pseudo.argument else ""}\
</%def>

impl PseudoElement {
    /// Whether this pseudo-element is tree pseudo-element.
    #[inline]
    pub fn is_tree_pseudo_element(&self) -> bool {
        matches!(
            *self,
            ${" | ".join(capture(pseudo_element_variant, pseudo) for pseudo in PSEUDOS if pseudo.name.startswith("-moz-tree-"))}
        )
    }

    #[inline]
    fn flags_for_index(i: usize) -> PseudoStyleTypeFlags {
        static FLAGS: [PseudoStyleTypeFlags; PSEUDO_COUNT + 1] = [
        % for pseudo in PSEUDOS:
        PseudoStyleTypeFlags::from_bits_truncate(${' | '.join(f"PseudoStyleTypeFlags::{flag}.bits()" for flag in pseudo.flags())}), // ${pseudo.name}
        % endfor
        // Unknown ::-webkit-* pseudos allow user-action state pseudo-classes.
        PseudoStyleTypeFlags::from_bits_truncate(
            PseudoStyleTypeFlags::IS_PSEUDO_ELEMENT.bits() |
            PseudoStyleTypeFlags::SUPPORTS_USER_ACTION_STATE.bits()
        ),
        ];
        FLAGS[i]
    }

    /// Property flag that properties must have to apply to this pseudo-element, or empty if there
    /// are no restrictions.
    #[inline]
    pub fn property_restriction(&self) -> PropertyFlags {
        static FLAGS: [PropertyFlags; PSEUDO_COUNT + 1] = [
            % for pseudo in PSEUDOS:
            % if pseudo.name == "first-letter":
            PropertyFlags::APPLIES_TO_FIRST_LETTER,
            % elif pseudo.name == "first-line":
            PropertyFlags::APPLIES_TO_FIRST_LINE,
            % elif pseudo.name == "marker":
            PropertyFlags::APPLIES_TO_MARKER,
            % elif pseudo.name == "cue":
            PropertyFlags::APPLIES_TO_CUE,
            % elif pseudo.name == "placeholder":
            PropertyFlags::APPLIES_TO_PLACEHOLDER,
            % elif pseudo.name in ["selection", "highlight", "target-text"]:
            PropertyFlags::APPLIES_TO_HIGHLIGHT,
            % else:
            PropertyFlags::empty(),
            % endif
            % endfor
            // Unknown ::-webkit pseudos have no restrictions.
            PropertyFlags::empty()
        ];
        FLAGS[self.index()]
    }

    /// Gets the flags associated to this pseudo-element or anon box.
    #[inline]
    pub fn flags(&self) -> PseudoStyleTypeFlags {
        Self::flags_for_index(self.index())
    }

    /// If this pseudo is pref-gated, returns the pref value, otherwise false.
    pub fn type_enabled_in_content(ty: PseudoStyleType) -> bool {
        let flags = Self::flags_for_index(ty as usize);
        // Common path, not enabled explicitly in UA sheets (note that chrome implies UA), and not
        // pref-gated.
        if !flags.intersects(PseudoStyleTypeFlags::ENABLED_IN_UA | PseudoStyleTypeFlags::ENABLED_BY_PREF) {
            return true;
        }
        if !flags.intersects(PseudoStyleTypeFlags::ENABLED_BY_PREF) {
            return false;
        }
        match ty {
        % for pseudo in PSEUDOS:
        % if pseudo.pref:
            PseudoStyleType::${pseudo.capitalized} => pref!("${pseudo.pref}"),
        % endif
        % endfor
            _ => false,
        }
    }

    /// Returns the current value of the `enabled_domains_pref` pref for this
    /// pseudo, if it has one. The value is a list of the only domains for
    /// which this pseudo should be enabled, or "*" for "every domain" (see
    /// `nsContentUtils::IsURIInList` for the format).
    pub fn enabled_domains(&self) -> Option<nsstring::nsCString> {
        match *self {
        % for pseudo in PSEUDOS:
        % if pseudo.is_pseudo_element() and pseudo.enabled_domains_pref:
            ${pseudo_element_variant(pseudo)} => Some(pref!("${pseudo.enabled_domains_pref}")),
        % endif
        % endfor
            _ => None,
        }
    }

    /// Construct a pseudo-element from a `PseudoStyleType`.
    #[inline]
    pub fn from_pseudo_type(type_: PseudoStyleType, functional_pseudo_parameter: Option<AtomIdent>) -> Option<Self> {
        match type_ {
            % for pseudo in PSEUDOS:
            % if not pseudo.argument:
            PseudoStyleType::${pseudo.capitalized} => {
                debug_assert!(functional_pseudo_parameter.is_none());
                Some(${pseudo_element_variant(pseudo)})
            },
            % elif pseudo.argument == "AtomThinVec":
            PseudoStyleType::${pseudo.capitalized} => {
                debug_assert!(functional_pseudo_parameter.is_none());
                Some(${pseudo_element_variant(pseudo, "Default::default()")})
            },
            % elif pseudo.argument == "PtNameAndClassSelector":
            PseudoStyleType::${pseudo.capitalized} => functional_pseudo_parameter.map(|p| {
                PseudoElement::${pseudo.capitalized}(PtNameAndClassSelector::from_name(p.0))
            }),
            % else:
            <% assert pseudo.argument == "AtomIdent", f"Unhandled argument type {pseudo.argument}" %>
            PseudoStyleType::${pseudo.capitalized} => {
                functional_pseudo_parameter.map(PseudoElement::${pseudo.capitalized})
            },
            % endif
            % endfor
            _ => None,
        }
    }

    /// Construct a `PseudoStyleType`.
    #[inline]
    pub fn pseudo_type(&self) -> PseudoStyleType {
        // SAFETY: PseudoStyleType has the same variants as PseudoElement
        unsafe { std::mem::transmute::<u8, PseudoStyleType>(self.discriminant()) }
    }

    /// Returns the relevant PseudoStyleType, and an atom as an argument, if any.
    /// FIXME: we probably have to return the arguments of -moz-tree. However, they are multiple
    /// names, so we skip them for now (until we really need them).
    #[inline]
    pub fn pseudo_type_and_argument(&self) -> (PseudoStyleType, Option<&Atom>) {
        let ty = self.pseudo_type();
        let arg = match *self {
            % for pseudo in PSEUDOS:
            % if pseudo.argument == "PtNameAndClassSelector":
            PseudoElement::${pseudo.capitalized}(ref val) => Some(val.name()),
            % elif pseudo.argument == "AtomIdent":
            PseudoElement::${pseudo.capitalized}(ref val) => Some(&val.0),
            % endif
            % endfor
            _ => None,
        };
        (ty, arg)
    }

    /// Get the argument list of a tree pseudo-element.
    #[inline]
    pub fn tree_pseudo_args(&self) -> &[Atom] {
        match *self {
            % for pseudo in PSEUDOS:
            % if pseudo.name.startswith("-moz-tree-"):
            PseudoElement::${pseudo.capitalized}(ref args) => args,
            % endif
            % endfor
            _ => &[],
        }
    }

    /// Constructs a pseudo-element from a string of text.
    ///
    /// Returns `None` if the pseudo-element is not recognised.
    #[inline]
    pub fn from_slice(name: &str) -> Option<Self> {
        // We don't need to support tree pseudos because functional
        // pseudo-elements needs arguments, and thus should be created
        // via other methods.
        cssparser::ascii_case_insensitive_phf_map! {
            pseudo -> PseudoElement = {
                % for pseudo in PSEUDOS:
                % if not pseudo.argument:
                "${pseudo.name}" => ${pseudo_element_variant(pseudo)},
                % endif
                % endfor
                // Alias some legacy prefixed pseudos to their standardized name at parse time:
                "-moz-selection" => PseudoElement::Selection,
                "-moz-placeholder" => PseudoElement::Placeholder,
                "-moz-list-bullet" => PseudoElement::Marker,
                "-moz-list-number" => PseudoElement::Marker,
            }
        }
        if let Some(p) = pseudo::get(name) {
            return Some(p.clone());
        }
        if starts_with_ignore_ascii_case(name, "-moz-tree-") {
            return PseudoElement::tree_pseudo_element(name, Default::default())
        }
        None
    }

    /// Produces an `UnknownWebkit` pseudo-element for any `-webkit-*` name.
    #[inline]
    pub fn unknown_webkit_from_name(name: &str) -> Option<Self> {
        const WEBKIT_PREFIX: &str = "-webkit-";
        if !starts_with_ignore_ascii_case(name, WEBKIT_PREFIX) {
            return None;
        }
        let part = string_as_ascii_lowercase(&name[WEBKIT_PREFIX.len()..]);
        Some(PseudoElement::UnknownWebkit(part.into()))
    }

    /// Constructs a tree pseudo-element from the given name and arguments.
    /// "name" must start with "-moz-tree-".
    ///
    /// Returns `None` if the pseudo-element is not recognized.
    #[inline]
    pub fn tree_pseudo_element(name: &str, args: thin_vec::ThinVec<Atom>) -> Option<Self> {
        debug_assert!(starts_with_ignore_ascii_case(name, "-moz-tree-"));
        let tree_part = &name[10..];
        % for pseudo in PSEUDOS:
        % if pseudo.name.startswith("-moz-tree-"):
        if tree_part.eq_ignore_ascii_case("${pseudo.name[10:]}") {
            return Some(${pseudo_element_variant(pseudo, "args")});
        }
        % endif
        % endfor
        None
    }

    /// Returns true if this pseudo-element matches the given selector.
    pub fn matches(
        &self,
        pseudo_selector: &PseudoElement,
        element: &super::wrapper::GeckoElement,
    ) -> bool {
        if *self == *pseudo_selector {
            return true;
        }

        if std::mem::discriminant(self) != std::mem::discriminant(pseudo_selector) {
            return false;
        }

        // Check named view transition pseudo-elements.
        self.matches_named_view_transition_pseudo_element(pseudo_selector, element)
    }
}

impl ToCss for PseudoElement {
    fn to_css<W>(&self, dest: &mut W) -> fmt::Result where W: fmt::Write {
        match *self {
            % for pseudo in PSEUDOS:
            % if pseudo.argument == "AtomThinVec":
            ${pseudo_element_variant(pseudo, "ref args")} => {
                dest.write_str("::${pseudo.name}")?;
                let mut iter = args.iter();
                if let Some(first) = iter.next() {
                    dest.write_char('(')?;
                    serialize_atom_identifier(first, dest)?;
                    for item in iter {
                        dest.write_str(", ")?;
                        serialize_atom_identifier(item, dest)?;
                    }
                    dest.write_char(')')?;
                }
                Ok(())
            },
            % elif pseudo.argument:
                PseudoElement::${pseudo.capitalized}(ref arg) => {
                    dest.write_str("::${pseudo.name}(")?;
                    arg.to_css(dest)?;
                    dest.write_char(')')
                }
            % else:
                ${pseudo_element_variant(pseudo)} => dest.write_str("::${pseudo.name}"),
            % endif
            % endfor
            PseudoElement::UnknownWebkit(ref atom) => {
                dest.write_str("::-webkit-")?;
                serialize_atom_identifier(atom, dest)
            },
        }
    }
}
