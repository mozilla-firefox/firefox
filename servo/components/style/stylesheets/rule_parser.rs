/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Parsing of the stylesheet contents.

use crate::counter_style::{parse_counter_style_body, parse_counter_style_name_definition};
use crate::custom_properties::parse_name as parse_custom_property_name;
use crate::derives::*;
use crate::error_reporting::ContextualParseError;
use crate::font_face::parse_font_face_block;
use crate::media_queries::MediaList;
use crate::parser::{Parse, ParserContext};
use crate::properties::declaration_block::{
    DeclarationParserState, PropertyDeclarationBlock, parse_property_declaration_list,
};
use crate::properties_and_values::rule::{PropertyRuleName, parse_property_block};
use crate::selector_parser::{SelectorImpl, SelectorParser};
use crate::shared_lock::{Locked, SharedRwLock};
use crate::stylesheets::container_rule::{ContainerCondition, ContainerConditions, ContainerRule};
use crate::stylesheets::document_rule::DocumentCondition;
use crate::stylesheets::font_feature_values_rule::{
    FontFeatureValuesBlockType, parse_family_name_list,
};
use crate::stylesheets::import_rule::{ImportLayer, ImportRule, ImportSupportsCondition};
use crate::stylesheets::keyframes_rule::parse_keyframe_list;
use crate::stylesheets::layer_rule::{LayerBlockRule, LayerName, LayerStatementRule};
use crate::stylesheets::scope_rule::{ScopeBounds, ScopeRule};
use crate::stylesheets::supports_rule::SupportsCondition;
use crate::stylesheets::{
    AllowImportRules, AppearanceBaseRule, CorsMode, CssRule, CssRuleType, CssRuleTypes, CssRules,
    CustomMediaCondition, CustomMediaRule, DocumentRule, FontFeatureValuesRule,
    FontPaletteValuesRule, KeyframesRule, MarginRule, MarginRuleType, MediaRule, NamespaceRule,
    NestedDeclarationsRule, PageRule, PageSelectors, PositionTryRule, RulesMutateError,
    StartingStyleRule, StyleRule, StylesheetLoader, SupportsRule, ViewTransitionRule,
};
use crate::url::UrlInitiator;
use crate::values::computed::font::FamilyName;
use crate::values::{CssUrl, CustomIdent, DashedIdent, KeyframesName};
use crate::{Atom, Namespace, Prefix};
use cssparser::{
    AtRuleParser, BasicParseError, BasicParseErrorKind, CowRcStr, DeclarationParser, Parser,
    ParserState, QualifiedRuleParser, RuleBodyItemParser, RuleBodyParser, SourcePosition,
    match_ignore_ascii_case,
};
use selectors::parser::{ParseRelative, SelectorList};
use servo_arc::Arc;
use style_traits::arc_slice::ArcSlice;
use style_traits::{ParseError, StyleParseErrorKind};

/// The information we need particularly to do CSSOM insertRule stuff.
pub struct InsertRuleContext<'a> {
    /// The rule list we're about to insert into.
    pub rule_list: &'a [CssRule],
    /// The index we're about to get inserted at.
    pub index: usize,
    /// The containing rule types of our ancestors.
    pub containing_rule_types: CssRuleTypes,
    /// Rule type determining if and how we parse relative selector syntax.
    pub parse_relative_rule_type: Option<CssRuleType>,
}

impl<'a> InsertRuleContext<'a> {
    /// Returns the max rule state allowable for insertion at a given index in
    /// the rule list.
    pub fn max_rule_state_at_index(&self, index: usize) -> State {
        let rule = match self.rule_list.get(index) {
            Some(rule) => rule,
            None => return State::Body,
        };
        match rule {
            CssRule::Import(..) => State::Imports,
            CssRule::Namespace(..) => State::Namespaces,
            CssRule::LayerStatement(..) => {
                // If there are @import / @namespace after this layer, then
                // we're in the early-layers phase, otherwise we're in the body
                // and everything is fair game.
                let next_non_layer_statement_rule = self.rule_list[index + 1..]
                    .iter()
                    .find(|r| !matches!(*r, CssRule::LayerStatement(..)));
                if let Some(non_layer) = next_non_layer_statement_rule
                    && matches!(*non_layer, CssRule::Import(..) | CssRule::Namespace(..))
                {
                    return State::EarlyLayers;
                }
                State::Body
            },
            _ => State::Body,
        }
    }
}

/// The parser for the top-level rules in a stylesheet.
pub struct TopLevelRuleParser<'a, 'i> {
    /// A reference to the lock we need to use to create rules.
    pub shared_lock: &'a SharedRwLock,
    /// A reference to a stylesheet loader if applicable, for `@import` rules.
    pub loader: Option<&'a dyn StylesheetLoader>,
    /// The top-level parser context.
    pub context: ParserContext<'a>,
    /// The current state of the parser.
    pub state: State,
    /// Whether we have tried to parse was invalid due to being in the wrong
    /// place (e.g. an @import rule was found while in the `Body` state). Reset
    /// to `false` when `take_had_hierarchy_error` is called.
    pub dom_error: Option<RulesMutateError>,
    /// The info we need insert a rule in a list.
    pub insert_rule_context: Option<InsertRuleContext<'a>>,
    /// Whether @import rules will be allowed.
    pub allow_import_rules: AllowImportRules,
    /// Whether to keep declarations into first_declaration_block, rather than turning it into a
    /// nested declarations rule.
    pub wants_first_declaration_block: bool,
    /// The first declaration block, only relevant when wants_first_declaration_block is true.
    pub first_declaration_block: PropertyDeclarationBlock,
    /// Parser state for declaration blocks in either nested rules or style rules.
    pub declaration_parser_state: DeclarationParserState<'i>,
    /// State we keep around only for error reporting purposes. Right now that contains just the
    /// selectors stack for nesting, if any.
    ///
    /// TODO(emilio): This isn't populated properly for `insertRule()` but...
    pub error_reporting_state: Vec<SelectorList<SelectorImpl>>,
    /// The rules we've parsed so far.
    pub rules: Vec<CssRule>,
}

impl<'a, 'i> TopLevelRuleParser<'a, 'i> {
    #[inline]
    fn nested(&mut self) -> &mut NestedRuleParser<'a, 'i> {
        // SAFETY: NestedRuleParser is just a repr(transparent) wrapper over TopLevelRuleParser
        const_assert!(
            std::mem::size_of::<TopLevelRuleParser<'static, 'static>>()
                == std::mem::size_of::<NestedRuleParser<'static, 'static>>()
        );
        const_assert!(
            std::mem::align_of::<TopLevelRuleParser<'static, 'static>>()
                == std::mem::align_of::<NestedRuleParser<'static, 'static>>()
        );
        unsafe { &mut *(self as *mut _ as *mut NestedRuleParser<'a, 'i>) }
    }

    /// Returns the current state of the parser.
    #[inline]
    pub fn state(&self) -> State {
        self.state
    }

    /// If we're in a nested state, this returns whether declarations can be parsed. See
    /// RuleBodyItemParser::parse_declarations().
    #[inline]
    pub fn can_parse_declarations(&self) -> bool {
        // We also have to check for page rules here because we currently don't
        // have a bespoke parser for page rules, and parse them as though they
        // are style rules.
        // Scope rules can have direct declarations, behaving as if `:where(:scope)`.
        // See https://drafts.csswg.org/css-cascade-6/#scoped-declarations
        self.in_specified_rule(
            CssRuleType::Style.bit() | CssRuleType::Page.bit() | CssRuleType::Scope.bit(),
        )
    }

    #[inline]
    fn in_style_rule(&self) -> bool {
        self.context
            .nesting_context
            .rule_types
            .contains(CssRuleType::Style)
    }

    #[inline]
    fn in_page_rule(&self) -> bool {
        self.context
            .nesting_context
            .rule_types
            .contains(CssRuleType::Page)
    }

    #[inline]
    fn in_specified_rule(&self, bits: u32) -> bool {
        let types = CssRuleTypes::from_bits(bits);
        self.context.nesting_context.rule_types.intersects(types)
    }

    #[inline]
    fn in_style_or_page_rule(&self) -> bool {
        self.in_specified_rule(CssRuleType::Style.bit() | CssRuleType::Page.bit())
    }

    /// Checks whether we can parse a rule that would transition us to
    /// `new_state`.
    ///
    /// This is usually a simple branch, but we may need more bookkeeping if
    /// doing `insertRule` from CSSOM.
    fn check_state(&mut self, new_state: State) -> bool {
        if self.state > new_state {
            self.dom_error = Some(RulesMutateError::HierarchyRequest);
            return false;
        }

        let ctx = match self.insert_rule_context {
            Some(ref ctx) => ctx,
            None => return true,
        };

        let max_rule_state = ctx.max_rule_state_at_index(ctx.index);
        if new_state > max_rule_state {
            self.dom_error = Some(RulesMutateError::HierarchyRequest);
            return false;
        }

        // If there's anything that isn't a namespace rule (or import rule, but
        // we checked that already at the beginning), reject with a
        // StateError.
        if new_state == State::Namespaces
            && ctx.rule_list[ctx.index..]
                .iter()
                .any(|r| !matches!(*r, CssRule::Namespace(..)))
        {
            self.dom_error = Some(RulesMutateError::InvalidState);
            return false;
        }

        true
    }
}

/// The current state of the parser.
#[derive(Clone, Copy, Eq, Ord, PartialEq, PartialOrd)]
pub enum State {
    /// We haven't started parsing rules.
    Start = 1,
    /// We're parsing early `@layer` statement rules.
    EarlyLayers = 2,
    /// We're parsing `@import` and early `@layer` statement rules.
    Imports = 3,
    /// We're parsing `@namespace` rules.
    Namespaces = 4,
    /// We're parsing the main body of the stylesheet.
    Body = 5,
}

#[derive(Clone, Debug, MallocSizeOf, ToShmem)]
/// Vendor prefix.
pub enum VendorPrefix {
    /// -moz prefix.
    Moz,
    /// -webkit prefix.
    WebKit,
}

#[allow(missing_docs)]
pub enum AtRuleType {
    FontFace,
    FontFeatureValues,
    FontFeatureValuesBlock,
    FontPaletteValues,
    CounterStyle,
    Media,
    CustomMedia,
    Container,
    Supports,
    Keyframes(Option<VendorPrefix>),
    Page,
    Property,
    Document,
    Import,
    Namespace,
    Layer,
    Scope,
    StartingStyle,
    AppearanceBase,
    PositionTry,
    ViewTransition,
    Margin(MarginRuleType),
}

impl AtRuleType {
    fn from_name(name: &str, context: &ParserContext) -> Option<Self> {
        Some(match_ignore_ascii_case! { name,
            "font-face" => Self::FontFace,
            "font-feature-values" => Self::FontFeatureValues,
            "font-palette-values" if crate::pref!("layout.css.font-palette.enabled", gecko = true) => Self::FontPaletteValues,
            "counter-style" if cfg!(feature = "gecko") => Self::CounterStyle,
            "media" => Self::Media,
            "custom-media" if crate::pref!("layout.css.custom-media.enabled") => Self::CustomMedia,
            "container" if cfg!(feature = "gecko") => Self::Container,
            "supports" => Self::Supports,
            "keyframes" => Self::Keyframes(None),
            "-webkit-keyframes" => Self::Keyframes(Some(VendorPrefix::WebKit)),
            "-moz-keyframes" if cfg!(feature = "gecko") => Self::Keyframes(Some(VendorPrefix::Moz)),
            "page" if cfg!(feature = "gecko") => Self::Page,
            "property" if crate::pref!("layout.css.properties-and-values.enabled") => Self::Property,
            "-moz-document" if cfg!(feature = "gecko") => Self::Document,
            "import" => Self::Import,
            "namespace" => Self::Namespace,
            "layer" => Self::Layer,
            "scope" if crate::pref!("layout.css.at-scope.enabled") => Self::Scope,
            "starting-style" if crate::pref!("layout.css.starting-style-at-rules.enabled") => Self::StartingStyle,
            "appearance-base" if context.chrome_rules_enabled() => Self::AppearanceBase,
            "position-try" if crate::pref!("layout.css.anchor-positioning.enabled", gecko = true) => Self::PositionTry,
            "view-transition" if crate::pref!("dom.viewTransitions.cross-document.enabled") => Self::ViewTransition,
            _ => {
                // The margin at-rules supported within @page.
                if cfg!(feature = "gecko") && crate::pref!("layout.css.margin-rules.enabled")
                    && let Some(rule_type) = MarginRuleType::from_name(name) {
                        return Some(Self::Margin(rule_type));
                    }

                // The font feature value at-rules supported within @font-feature-values.
                if cfg!(feature = "gecko") && FontFeatureValuesBlockType::from_name(name).is_some() {
                    return Some(Self::FontFeatureValuesBlock);
                }

                return None;
            },
        })
    }

    /// Returns whether the given at-rule is supported in any context.
    ///
    /// https://drafts.csswg.org/css-conditional-5/#support-definition-at-rules
    pub fn is_supported(name: &str, context: &ParserContext) -> bool {
        AtRuleType::from_name(name, context).is_some()
    }
}

/// A rule prelude for at-rule with block.
pub enum AtRulePrelude {
    /// A @font-face rule prelude.
    FontFace,
    /// A @font-feature-values rule prelude, with its FamilyName list.
    FontFeatureValues(Vec<FamilyName>),
    /// A @font-palette-values rule prelude, with its identifier.
    FontPaletteValues(DashedIdent),
    /// A @counter-style rule prelude, with its counter style name.
    CounterStyle(CustomIdent),
    /// A @media rule prelude, with its media queries.
    Media(Arc<Locked<MediaList>>),
    /// A @container rule prelude.
    Container(ArcSlice<ContainerCondition>),
    /// An @supports rule, with its conditional
    Supports(SupportsCondition),
    /// A @keyframes rule, with its animation name and vendor prefix if exists.
    Keyframes(KeyframesName, Option<VendorPrefix>),
    /// A @page rule prelude, with its page name if it exists.
    Page(PageSelectors),
    /// A @property rule prelude.
    Property(PropertyRuleName),
    /// A @document rule, with its conditional.
    Document(DocumentCondition),
    /// A @import rule prelude.
    Import(
        CssUrl,
        Arc<Locked<MediaList>>,
        Option<ImportSupportsCondition>,
        ImportLayer,
    ),
    /// A @margin rule prelude.
    Margin(MarginRuleType),
    /// A @namespace rule prelude.
    Namespace(Option<Prefix>, Namespace),
    /// A @layer rule prelude.
    Layer(Vec<LayerName>),
    /// A @scope rule prelude.
    Scope(ScopeBounds),
    /// A @starting-style prelude.
    StartingStyle,
    /// A @appearance-base prelude (UA sheets only).
    AppearanceBase,
    /// A @position-try prelude for Anchor Positioning.
    PositionTry(DashedIdent),
    /// A @custom-media prelude.
    CustomMedia(DashedIdent, CustomMediaCondition),
    /// A @view-transition prelude.
    ViewTransition,
}

impl<'a, 'i> AtRuleParser<'i> for TopLevelRuleParser<'a, 'i> {
    type Prelude = AtRulePrelude;
    type AtRule = SourcePosition;
    type Error = StyleParseErrorKind;

    fn parse_prelude(
        &mut self,
        name: CowRcStr<'i>,
        input: &mut Parser<'i>,
    ) -> Result<AtRulePrelude, ParseError> {
        // @charset is removed by rust-cssparser if it’s the first rule in the stylesheet
        // anything left is invalid.
        if name.eq_ignore_ascii_case("charset") {
            self.dom_error = Some(RulesMutateError::HierarchyRequest);
            return Err(ParseError::custom(
                StyleParseErrorKind::UnexpectedCharsetRule,
            ));
        }

        let Some(rule_type) = AtRuleType::from_name(&name, &self.context) else {
            return Err(ParseError::from_basic_kind(
                BasicParseErrorKind::AtRuleInvalid,
            ));
        };

        match rule_type {
            AtRuleType::Import => {
                if !self.check_state(State::Imports) {
                    return Err(ParseError::custom(
                        StyleParseErrorKind::UnexpectedImportRule,
                    ));
                }

                if let AllowImportRules::No = self.allow_import_rules {
                    return Err(ParseError::custom(
                        StyleParseErrorKind::DisallowedImportRule,
                    ));
                }

                // FIXME(emilio): We should always be able to have a loader
                // around! See bug 1533783.
                if self.loader.is_none() {
                    error!("Saw @import rule, but no way to trigger the load");
                    return Err(ParseError::custom(
                        StyleParseErrorKind::UnexpectedImportRule,
                    ));
                }

                let url_string = input.expect_url_or_string()?.as_ref().to_owned();
                let url = CssUrl::new_from_untainted_string(
                    url_string,
                    &self.context,
                    CorsMode::None,
                    UrlInitiator::Css,
                );

                let (layer, supports) =
                    ImportRule::parse_layer_and_supports(input, &mut self.context);

                let media = MediaList::parse(&mut self.context, input);
                let media = Arc::new(self.shared_lock.wrap(media));

                return Ok(AtRulePrelude::Import(url, media, supports, layer));
            },
            AtRuleType::Namespace => {
                if !self.check_state(State::Namespaces) {
                    return Err(ParseError::custom(
                        StyleParseErrorKind::UnexpectedNamespaceRule,
                    ));
                }

                let prefix = input
                    .try_parse(|i| i.expect_ident_cloned())
                    .map(|s| Prefix::from(s.as_ref()))
                    .ok();
                let maybe_namespace = match input.expect_url_or_string() {
                    Ok(url_or_string) => url_or_string,
                    Err(BasicParseError {
                        kind: BasicParseErrorKind::UnexpectedToken,
                    }) => {
                        return Err(ParseError::custom(
                            StyleParseErrorKind::UnexpectedTokenWithinNamespace,
                        ));
                    },
                    Err(e) => return Err(e.into()),
                };
                let url = Namespace::from(maybe_namespace.as_ref());
                return Ok(AtRulePrelude::Namespace(prefix, url));
            },
            AtRuleType::Layer => {
                let state_to_check = if self.state <= State::EarlyLayers {
                    // The real state depends on whether there's a block or not.
                    // We don't know that yet, but the parse_block check deals
                    // with that.
                    State::EarlyLayers
                } else {
                    State::Body
                };
                if !self.check_state(state_to_check) {
                    return Err(ParseError::custom(StyleParseErrorKind::UnspecifiedError));
                }
            },
            _ => {
                // All other rules have blocks, so we do this check early in
                // parse_block instead.
            },
        }

        self.nested().parse_at_rule_prelude(rule_type, input)
    }

    #[inline]
    fn parse_block(
        &mut self,
        prelude: AtRulePrelude,
        start: &ParserState,
        input: &mut Parser<'i>,
    ) -> Result<Self::AtRule, ParseError> {
        if !self.check_state(State::Body) {
            return Err(ParseError::custom(StyleParseErrorKind::UnspecifiedError));
        }
        AtRuleParser::parse_block(self.nested(), prelude, start, input)?;
        self.state = State::Body;
        Ok(start.position())
    }

    #[inline]
    fn rule_without_block(
        &mut self,
        prelude: AtRulePrelude,
        start: &ParserState,
    ) -> Result<Self::AtRule, ()> {
        match prelude {
            AtRulePrelude::Import(url, media, supports, layer) => {
                let loader = self
                    .loader
                    .expect("Expected a stylesheet loader for @import");

                let import_rule = loader.request_stylesheet(
                    url,
                    start.source_location(),
                    self.shared_lock,
                    media,
                    supports,
                    layer,
                );

                self.state = State::Imports;
                self.rules.push(CssRule::Import(import_rule))
            },
            AtRulePrelude::Namespace(prefix, url) => {
                let namespaces = self.context.namespaces.to_mut();
                let prefix = if let Some(prefix) = prefix {
                    namespaces.prefixes.insert(prefix.clone(), url.clone());
                    Some(prefix)
                } else {
                    namespaces.default = Some(url.clone());
                    None
                };

                self.state = State::Namespaces;
                self.rules.push(CssRule::Namespace(Arc::new(NamespaceRule {
                    prefix,
                    url,
                    source_location: start.source_location(),
                })));
            },
            AtRulePrelude::Layer(..) => {
                AtRuleParser::rule_without_block(self.nested(), prelude, start)?;
                if self.state <= State::EarlyLayers {
                    self.state = State::EarlyLayers;
                } else {
                    self.state = State::Body;
                }
            },
            _ => AtRuleParser::rule_without_block(self.nested(), prelude, start)?,
        };

        Ok(start.position())
    }
}

impl<'a, 'i> QualifiedRuleParser<'i> for TopLevelRuleParser<'a, 'i> {
    type Prelude = SelectorList<SelectorImpl>;
    type QualifiedRule = SourcePosition;
    type Error = StyleParseErrorKind;

    #[inline]
    fn parse_prelude(&mut self, input: &mut Parser<'i>) -> Result<Self::Prelude, ParseError> {
        if !self.check_state(State::Body) {
            return Err(ParseError::custom(StyleParseErrorKind::UnspecifiedError));
        }

        QualifiedRuleParser::parse_prelude(self.nested(), input)
    }

    #[inline]
    fn parse_block(
        &mut self,
        prelude: Self::Prelude,
        start: &ParserState,
        input: &mut Parser<'i>,
    ) -> Result<Self::QualifiedRule, ParseError> {
        QualifiedRuleParser::parse_block(self.nested(), prelude, start, input)?;
        self.state = State::Body;
        Ok(start.position())
    }
}

#[repr(transparent)]
#[derive(Deref, DerefMut)]
struct NestedRuleParser<'a, 'i>(TopLevelRuleParser<'a, 'i>);

struct NestedParseResult {
    first_declaration_block: PropertyDeclarationBlock,
    rules: Vec<CssRule>,
}

impl<'a, 'i> NestedRuleParser<'a, 'i> {
    fn parse_at_rule_prelude(
        &mut self,
        rule_type: AtRuleType,
        input: &mut Parser<'i>,
    ) -> Result<AtRulePrelude, ParseError> {
        Ok(match rule_type {
            AtRuleType::Media => {
                let media_queries = MediaList::parse(&mut self.context, input);
                let arc = Arc::new(self.shared_lock.wrap(media_queries));
                AtRulePrelude::Media(arc)
            },
            AtRuleType::Supports => {
                let cond = SupportsCondition::parse(input)?;
                AtRulePrelude::Supports(cond)
            },
            AtRuleType::FontFace => AtRulePrelude::FontFace,
            AtRuleType::Container => {
                let conditions = input.parse_comma_separated(|input| {
                    ContainerCondition::parse(&self.context, input)
                })?;
                // Container rules must have at least one condition.
                debug_assert!(!conditions.is_empty());
                let conditions = ArcSlice::from_iter(conditions.into_iter());
                AtRulePrelude::Container(conditions)
            },
            AtRuleType::Layer => {
                let names = input
                    .try_parse(|input| {
                        input.parse_comma_separated(|input| LayerName::parse(&self.context, input))
                    })
                    .unwrap_or_default();
                AtRulePrelude::Layer(names)
            },
            AtRuleType::FontFeatureValues => {
                let family_names = parse_family_name_list(&self.context, input)?;
                AtRulePrelude::FontFeatureValues(family_names)
            },
            AtRuleType::FontPaletteValues => {
                let name = DashedIdent::parse(&self.context, input)?;
                AtRulePrelude::FontPaletteValues(name)
            },
            AtRuleType::CounterStyle => {
                let name = parse_counter_style_name_definition(input)?;
                AtRulePrelude::CounterStyle(name)
            },
            AtRuleType::Keyframes(prefix) => {
                let name = KeyframesName::parse(&self.context, input)?;
                AtRulePrelude::Keyframes(name, prefix)
            },
            AtRuleType::Page => AtRulePrelude::Page(
                input
                    .try_parse(|i| PageSelectors::parse(&self.context, i))
                    .unwrap_or_default(),
            ),
            AtRuleType::Property => {
                let name = input.expect_ident_cloned()?;
                let name = parse_custom_property_name(&name)
                    .map_err(|_| ParseError::custom(StyleParseErrorKind::UnexpectedIdent))?;
                AtRulePrelude::Property(PropertyRuleName(Atom::from(name)))
            },
            AtRuleType::Document => {
                let cond = DocumentCondition::parse(&self.context, input)?;
                AtRulePrelude::Document(cond)
            },
            AtRuleType::Scope => {
                let bounds = ScopeBounds::parse(&self.context, input, self.parse_relative())?;
                AtRulePrelude::Scope(bounds)
            },
            AtRuleType::StartingStyle => AtRulePrelude::StartingStyle,
            AtRuleType::AppearanceBase => {
                // We allow parsing this in chrome sheets mostly just so that
                // browser_parsable_css.js checks UA sheets properly.
                AtRulePrelude::AppearanceBase
            },
            AtRuleType::PositionTry => {
                let name = DashedIdent::parse(&self.context, input)?;
                AtRulePrelude::PositionTry(name)
            },
            AtRuleType::CustomMedia => {
                let name = DashedIdent::parse(&self.context, input)?;
                let condition = input
                    .try_parse(CustomMediaCondition::parse_keyword)
                    .unwrap_or_else(|_| {
                        CustomMediaCondition::MediaList(Arc::new(
                            self.shared_lock
                                .wrap(MediaList::parse(&mut self.context, input)),
                        ))
                    });
                AtRulePrelude::CustomMedia(name, condition)
            },
            AtRuleType::ViewTransition => AtRulePrelude::ViewTransition,
            AtRuleType::Margin(rule_type) => AtRulePrelude::Margin(rule_type),
            AtRuleType::Import | AtRuleType::Namespace | AtRuleType::FontFeatureValuesBlock => {
                // @import and @namespace are only valid and handled at the
                // top level, and @font-feature-values uses its own parser.
                return Err(ParseError::from_basic_kind(
                    BasicParseErrorKind::AtRuleInvalid,
                ));
            },
        })
    }

    #[inline]
    fn parse_relative(&self) -> ParseRelative {
        self.context.nesting_context.parse_relative
    }

    // https://drafts.csswg.org/css-nesting/#conditionals
    //     In addition to nested style rules, this specification allows nested group rules inside
    //     of style rules: any at-rule whose body contains style rules can be nested inside of a
    //     style rule as well.
    fn at_rule_allowed(&self, prelude: &AtRulePrelude) -> bool {
        match prelude {
            AtRulePrelude::Media(..)
            | AtRulePrelude::Supports(..)
            | AtRulePrelude::Container(..)
            | AtRulePrelude::Document(..)
            | AtRulePrelude::Layer(..)
            | AtRulePrelude::CustomMedia(..)
            | AtRulePrelude::Scope(..)
            | AtRulePrelude::StartingStyle
            | AtRulePrelude::AppearanceBase => true,

            AtRulePrelude::Namespace(..)
            | AtRulePrelude::FontFace
            | AtRulePrelude::FontFeatureValues(..)
            | AtRulePrelude::FontPaletteValues(..)
            | AtRulePrelude::CounterStyle(..)
            | AtRulePrelude::Keyframes(..)
            | AtRulePrelude::Page(..)
            | AtRulePrelude::Property(..)
            | AtRulePrelude::Import(..)
            | AtRulePrelude::PositionTry(..)
            | AtRulePrelude::ViewTransition => !self.in_style_or_page_rule(),
            AtRulePrelude::Margin(..) => self.in_page_rule(),
        }
    }

    fn nest_for_rule<R>(&mut self, rule_type: CssRuleType, cb: impl FnOnce(&mut Self) -> R) -> R {
        let old = self.context.nesting_context.save(rule_type);
        let r = cb(self);
        self.context.nesting_context.restore(old);
        r
    }

    fn parse_nested_rules(
        &mut self,
        input: &mut Parser<'i>,
        rule_type: CssRuleType,
    ) -> Arc<Locked<CssRules>> {
        let rules = self
            .parse_nested(
                input, rule_type, /* wants_first_declaration_block = */ false,
            )
            .rules;
        CssRules::new(rules, self.shared_lock)
    }

    fn parse_nested(
        &mut self,
        input: &mut Parser<'i>,
        rule_type: CssRuleType,
        wants_first_declaration_block: bool,
    ) -> NestedParseResult {
        debug_assert!(
            !self.wants_first_declaration_block,
            "Should've flushed previous declarations"
        );
        self.nest_for_rule(rule_type, |parser| {
            parser.wants_first_declaration_block = wants_first_declaration_block;
            let parse_declarations = parser.parse_declarations();
            let mut rules = std::mem::take(&mut parser.rules);
            let mut first_declaration_block = std::mem::take(&mut parser.first_declaration_block);
            let mut iter = RuleBodyParser::new(input, parser);
            while let Some(result) = iter.next() {
                match result {
                    Ok(()) => {},
                    Err((error, slice, location)) => {
                        if parse_declarations {
                            let top = &mut **iter.parser;
                            top.declaration_parser_state.did_error(
                                &top.context,
                                error,
                                slice,
                                location,
                            );
                        } else {
                            let error = ContextualParseError::InvalidRule(slice, error);
                            iter.parser.context.log_css_error(location, error);
                        }
                    },
                }
            }
            parser.flush_declarations();
            debug_assert!(
                !parser.wants_first_declaration_block,
                "Flushing declarations should take care of this."
            );
            debug_assert!(
                !parser.declaration_parser_state.has_parsed_declarations(),
                "Parsed but didn't consume declarations"
            );
            std::mem::swap(&mut parser.rules, &mut rules);
            std::mem::swap(
                &mut parser.first_declaration_block,
                &mut first_declaration_block,
            );
            NestedParseResult {
                first_declaration_block,
                rules,
            }
        })
    }

    #[inline(never)]
    fn handle_error_reporting_selectors_pre(
        &mut self,
        start: &ParserState,
        selectors: &SelectorList<SelectorImpl>,
    ) {
        use cssparser::ToCss;
        debug_assert!(self.context.error_reporting_enabled());
        self.error_reporting_state.push(selectors.clone());
        'selector_loop: for selector in selectors.slice().iter() {
            let mut current = selector.iter();
            loop {
                let mut found_host = false;
                let mut found_non_host = false;
                for component in &mut current {
                    if component.is_host() {
                        found_host = true;
                    } else {
                        found_non_host = true;
                    }
                    if found_host && found_non_host {
                        self.context.log_css_error(
                            start.source_location(),
                            ContextualParseError::NeverMatchingHostSelector(
                                selector.to_css_string(),
                            ),
                        );
                        continue 'selector_loop;
                    }
                }
                if current.next_sequence().is_none() {
                    break;
                }
            }
        }
    }

    fn handle_error_reporting_selectors_post(&mut self) {
        self.error_reporting_state.pop();
    }

    #[inline]
    fn flush_declarations(&mut self) {
        let parser = &mut **self;
        let wants_first_declaration_block = parser.wants_first_declaration_block;
        parser.wants_first_declaration_block = false;
        parser
            .declaration_parser_state
            .report_errors_if_needed(&parser.context, &parser.error_reporting_state);
        if !parser.declaration_parser_state.has_parsed_declarations() {
            return;
        }
        let source_location = parser.declaration_parser_state.first_declaration_start();
        let declarations = parser.declaration_parser_state.take_declarations();
        if wants_first_declaration_block {
            debug_assert!(parser.first_declaration_block.is_empty(), "How?");
            parser.first_declaration_block = declarations;
        } else {
            let nested_rule = CssRule::NestedDeclarations(Arc::new(parser.shared_lock.wrap(
                NestedDeclarationsRule {
                    block: Arc::new(parser.shared_lock.wrap(declarations)),
                    source_location,
                },
            )));
            parser.rules.push(nested_rule);
        }
    }
}

impl<'a, 'i> AtRuleParser<'i> for NestedRuleParser<'a, 'i> {
    type Prelude = AtRulePrelude;
    type AtRule = ();
    type Error = StyleParseErrorKind;

    fn parse_prelude(
        &mut self,
        name: CowRcStr<'i>,
        input: &mut Parser<'i>,
    ) -> Result<Self::Prelude, ParseError> {
        let Some(rule_type) = AtRuleType::from_name(&name, &self.context) else {
            return Err(ParseError::from_basic_kind(
                BasicParseErrorKind::AtRuleInvalid,
            ));
        };

        self.parse_at_rule_prelude(rule_type, input)
    }

    fn parse_block(
        &mut self,
        prelude: AtRulePrelude,
        start: &ParserState,
        input: &mut Parser<'i>,
    ) -> Result<(), ParseError> {
        if !self.at_rule_allowed(&prelude) {
            self.dom_error = Some(RulesMutateError::HierarchyRequest);
            return Err(ParseError::from_basic_kind(
                BasicParseErrorKind::AtRuleInvalid,
            ));
        }
        let source_location = start.source_location();
        self.flush_declarations();
        let rule = match prelude {
            AtRulePrelude::FontFace => self.nest_for_rule(CssRuleType::FontFace, |p| {
                CssRule::FontFace(Arc::new(p.shared_lock.wrap(parse_font_face_block(
                    &p.context,
                    input,
                    source_location,
                ))))
            }),
            AtRulePrelude::FontFeatureValues(family_names) => {
                self.nest_for_rule(CssRuleType::FontFeatureValues, |p| {
                    CssRule::FontFeatureValues(Arc::new(FontFeatureValuesRule::parse(
                        &p.context,
                        input,
                        family_names,
                        source_location,
                    )))
                })
            },
            AtRulePrelude::FontPaletteValues(name) => {
                self.nest_for_rule(CssRuleType::FontPaletteValues, |p| {
                    CssRule::FontPaletteValues(Arc::new(FontPaletteValuesRule::parse(
                        &p.context,
                        input,
                        name,
                        source_location,
                    )))
                })
            },
            AtRulePrelude::CounterStyle(name) => {
                let body = self.nest_for_rule(CssRuleType::CounterStyle, |p| {
                    parse_counter_style_body(name, &p.context, input, source_location)
                })?;
                CssRule::CounterStyle(Arc::new(self.shared_lock.wrap(body)))
            },
            AtRulePrelude::Media(media_queries) => CssRule::Media(Arc::new(MediaRule {
                media_queries,
                rules: self.parse_nested_rules(input, CssRuleType::Media),
                source_location,
            })),
            AtRulePrelude::Supports(condition) => {
                let enabled =
                    self.nest_for_rule(CssRuleType::Style, |p| condition.eval(&mut p.context));
                CssRule::Supports(Arc::new(SupportsRule {
                    condition,
                    rules: self.parse_nested_rules(input, CssRuleType::Supports),
                    enabled,
                    source_location,
                }))
            },
            AtRulePrelude::Keyframes(name, vendor_prefix) => {
                self.nest_for_rule(CssRuleType::Keyframe, |p| {
                    let top = &mut **p;
                    CssRule::Keyframes(Arc::new(top.shared_lock.wrap(KeyframesRule {
                        name,
                        keyframes: parse_keyframe_list(&mut top.context, input, top.shared_lock),
                        vendor_prefix,
                        source_location,
                    })))
                })
            },
            AtRulePrelude::Page(selectors) => {
                let page_rule = if !crate::pref!("layout.css.margin-rules.enabled") {
                    let declarations = self.nest_for_rule(CssRuleType::Page, |p| {
                        parse_property_declaration_list(&mut p.context, input, &[])
                    });
                    PageRule {
                        selectors,
                        rules: CssRules::new(vec![], self.shared_lock),
                        block: Arc::new(self.shared_lock.wrap(declarations)),
                        source_location,
                    }
                } else {
                    let result = self.parse_nested(input, CssRuleType::Page, true);
                    PageRule {
                        selectors,
                        rules: CssRules::new(result.rules, self.shared_lock),
                        block: Arc::new(self.shared_lock.wrap(result.first_declaration_block)),
                        source_location,
                    }
                };
                CssRule::Page(Arc::new(self.shared_lock.wrap(page_rule)))
            },
            AtRulePrelude::Property(name) => self.nest_for_rule(CssRuleType::Property, |p| {
                let rule_data = parse_property_block(&p.context, input, name, source_location)?;
                Ok::<CssRule, ParseError>(CssRule::Property(Arc::new(rule_data)))
            })?,
            AtRulePrelude::Document(condition) => {
                if !cfg!(feature = "gecko") {
                    unreachable!()
                }
                CssRule::Document(Arc::new(DocumentRule {
                    condition,
                    rules: self.parse_nested_rules(input, CssRuleType::Document),
                    source_location,
                }))
            },
            AtRulePrelude::Container(conditions) => {
                let source_location = start.source_location();
                CssRule::Container(Arc::new(ContainerRule {
                    conditions: ContainerConditions(conditions),
                    rules: self.parse_nested_rules(input, CssRuleType::Container),
                    source_location,
                }))
            },
            AtRulePrelude::Layer(names) => {
                let name = match names.len() {
                    0 | 1 => names.into_iter().next(),
                    _ => {
                        return Err(ParseError::from_basic_kind(
                            BasicParseErrorKind::AtRuleBodyInvalid,
                        ));
                    },
                };
                CssRule::LayerBlock(Arc::new(LayerBlockRule {
                    name,
                    rules: self.parse_nested_rules(input, CssRuleType::LayerBlock),
                    source_location,
                }))
            },
            AtRulePrelude::Margin(rule_type) => {
                let declarations = self.nest_for_rule(CssRuleType::Margin, |p| {
                    parse_property_declaration_list(&mut p.context, input, &[])
                });
                CssRule::Margin(Arc::new(MarginRule {
                    rule_type,
                    block: Arc::new(self.shared_lock.wrap(declarations)),
                    source_location,
                }))
            },
            AtRulePrelude::CustomMedia(..)
            | AtRulePrelude::Import(..)
            | AtRulePrelude::Namespace(..) => {
                // These rules don't have blocks.
                return Err(ParseError::unexpected_token());
            },
            AtRulePrelude::Scope(bounds) => CssRule::Scope(Arc::new(ScopeRule {
                bounds,
                rules: self.parse_nested_rules(input, CssRuleType::Scope),
                source_location,
            })),
            AtRulePrelude::StartingStyle => CssRule::StartingStyle(Arc::new(StartingStyleRule {
                rules: self.parse_nested_rules(input, CssRuleType::StartingStyle),
                source_location,
            })),
            AtRulePrelude::AppearanceBase => {
                CssRule::AppearanceBase(Arc::new(AppearanceBaseRule {
                    rules: self.parse_nested_rules(input, CssRuleType::AppearanceBase),
                    source_location,
                }))
            },
            AtRulePrelude::PositionTry(name) => {
                let declarations = self.nest_for_rule(CssRuleType::PositionTry, |p| {
                    parse_property_declaration_list(&mut p.context, input, &[])
                });
                CssRule::PositionTry(Arc::new(self.shared_lock.wrap(PositionTryRule {
                    name,
                    block: Arc::new(self.shared_lock.wrap(declarations)),
                    source_location,
                })))
            },
            AtRulePrelude::ViewTransition => self.nest_for_rule(CssRuleType::ViewTransition, |p| {
                CssRule::ViewTransition(Arc::new(ViewTransitionRule::parse(
                    &p.context,
                    input,
                    source_location,
                )))
            }),
        };
        self.rules.push(rule);
        Ok(())
    }

    #[inline]
    fn rule_without_block(
        &mut self,
        prelude: AtRulePrelude,
        start: &ParserState,
    ) -> Result<(), ()> {
        if self.in_style_rule() {
            return Err(());
        }
        let source_location = start.source_location();
        let rule = match prelude {
            AtRulePrelude::CustomMedia(name, condition) => {
                CssRule::CustomMedia(Arc::new(CustomMediaRule {
                    name,
                    condition,
                    source_location,
                }))
            },
            AtRulePrelude::Layer(names) => {
                if names.is_empty() {
                    return Err(());
                }
                CssRule::LayerStatement(Arc::new(LayerStatementRule {
                    names,
                    source_location,
                }))
            },
            _ => return Err(()),
        };
        self.flush_declarations();
        self.rules.push(rule);
        Ok(())
    }
}

impl<'a, 'i> QualifiedRuleParser<'i> for NestedRuleParser<'a, 'i> {
    type Prelude = SelectorList<SelectorImpl>;
    type QualifiedRule = ();
    type Error = StyleParseErrorKind;

    fn parse_prelude(&mut self, input: &mut Parser<'i>) -> Result<Self::Prelude, ParseError> {
        let selector_parser = SelectorParser {
            stylesheet_origin: self.context.stylesheet_origin,
            namespaces: &self.context.namespaces,
            url_data: self.context.url_data,
            for_supports_rule: false,
        };
        SelectorList::parse(&selector_parser, input, self.parse_relative())
    }

    fn parse_block(
        &mut self,
        selectors: Self::Prelude,
        start: &ParserState,
        input: &mut Parser<'i>,
    ) -> Result<(), ParseError> {
        let source_location = start.source_location();
        let reporting_errors = self.context.error_reporting_enabled();
        if reporting_errors {
            self.handle_error_reporting_selectors_pre(start, &selectors);
        }
        self.flush_declarations();
        let result = self.parse_nested(input, CssRuleType::Style, true);
        if reporting_errors {
            self.handle_error_reporting_selectors_post();
        }
        let block = Arc::new(self.shared_lock.wrap(result.first_declaration_block));
        let top = &mut **self;
        top.rules
            .push(CssRule::Style(Arc::new(top.shared_lock.wrap(StyleRule {
                selectors,
                block,
                rules: if result.rules.is_empty() {
                    None
                } else {
                    Some(CssRules::new(result.rules, top.shared_lock))
                },
                source_location,
            }))));
        Ok(())
    }
}

impl<'a, 'i> DeclarationParser<'i> for NestedRuleParser<'a, 'i> {
    type Declaration = ();
    type Error = StyleParseErrorKind;
    fn parse_value(
        &mut self,
        name: CowRcStr<'i>,
        input: &mut Parser<'i>,
        declaration_start: &ParserState,
    ) -> Result<(), ParseError> {
        let top = &mut **self;
        top.declaration_parser_state
            .parse_value(&mut top.context, name, input, declaration_start)
    }
}

impl<'a, 'i> RuleBodyItemParser<'i, (), StyleParseErrorKind> for NestedRuleParser<'a, 'i> {
    fn parse_qualified(&self) -> bool {
        true
    }

    /// If nesting is disabled, we can't get there for a non-style-rule. If it's enabled, we parse
    /// raw declarations there.
    fn parse_declarations(&self) -> bool {
        self.can_parse_declarations()
    }
}
