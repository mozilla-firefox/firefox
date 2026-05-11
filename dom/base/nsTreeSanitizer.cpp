/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsTreeSanitizer.h"

#include <algorithm>
#include <iterator>

#include "NonCustomCSSPropertyId.h"
#include "mozilla/DeclarationBlock.h"
#include "mozilla/NullPrincipal.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StyleSheetInlines.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentFragment.h"
#include "mozilla/dom/HTMLFormElement.h"
#include "mozilla/dom/HTMLTemplateElement.h"
#include "mozilla/dom/HTMLUnknownElement.h"
#include "mozilla/dom/Link.h"
#include "mozilla/dom/SRIMetadata.h"
#include "mozilla/dom/SanitizerBinding.h"
#include "mozilla/dom/ShadowIncludingTreeIterator.h"
#include "nsAtom.h"
#include "nsAttrName.h"
#include "nsComponentManagerUtils.h"
#include "nsContentUtils.h"
#include "nsHashtablesFwd.h"
#include "nsIParserUtils.h"
#include "nsIScriptError.h"
#include "nsIScriptSecurityManager.h"
#include "nsNameSpaceManager.h"
#include "nsNetUtil.h"
#include "nsQueryObject.h"
#include "nsString.h"
#include "nsTHashtable.h"
#include "nsUnicharInputStream.h"

// Undo some conflicting Windows macro definition.
#undef small

using namespace mozilla;
using namespace mozilla::dom;

//
// Thanks to Mark Pilgrim and Sam Ruby for the initial whitelist
//
const nsStaticAtom* const kElementsHTML[] = {
    // clang-format off
  nsGkAtoms::a,
  nsGkAtoms::abbr,
  nsGkAtoms::acronym,
  nsGkAtoms::address,
  nsGkAtoms::area,
  nsGkAtoms::article,
  nsGkAtoms::aside,
  nsGkAtoms::audio,
  nsGkAtoms::b,
  nsGkAtoms::bdi,
  nsGkAtoms::bdo,
  nsGkAtoms::big,
  nsGkAtoms::blockquote,
  // body checked specially
  nsGkAtoms::br,
  nsGkAtoms::button,
  nsGkAtoms::canvas,
  nsGkAtoms::caption,
  nsGkAtoms::center,
  nsGkAtoms::cite,
  nsGkAtoms::code,
  nsGkAtoms::col,
  nsGkAtoms::colgroup,
  nsGkAtoms::data,
  nsGkAtoms::datalist,
  nsGkAtoms::dd,
  nsGkAtoms::del,
  nsGkAtoms::details,
  nsGkAtoms::dfn,
  nsGkAtoms::dialog,
  nsGkAtoms::dir,
  nsGkAtoms::div,
  nsGkAtoms::dl,
  nsGkAtoms::dt,
  nsGkAtoms::em,
  nsGkAtoms::fieldset,
  nsGkAtoms::figcaption,
  nsGkAtoms::figure,
  nsGkAtoms::font,
  nsGkAtoms::footer,
  nsGkAtoms::form,
  nsGkAtoms::h1,
  nsGkAtoms::h2,
  nsGkAtoms::h3,
  nsGkAtoms::h4,
  nsGkAtoms::h5,
  nsGkAtoms::h6,
  // head checked specially
  nsGkAtoms::header,
  nsGkAtoms::hgroup,
  nsGkAtoms::hr,
  // html checked specially
  nsGkAtoms::i,
  nsGkAtoms::img,
  nsGkAtoms::input,
  nsGkAtoms::ins,
  nsGkAtoms::kbd,
  nsGkAtoms::keygen,
  nsGkAtoms::label,
  nsGkAtoms::legend,
  nsGkAtoms::li,
  nsGkAtoms::link,
  nsGkAtoms::listing,
  nsGkAtoms::main,
  nsGkAtoms::map,
  nsGkAtoms::mark,
  nsGkAtoms::menu,
  nsGkAtoms::meta,
  nsGkAtoms::meter,
  nsGkAtoms::nav,
  nsGkAtoms::nobr,
  nsGkAtoms::noscript,
  nsGkAtoms::ol,
  nsGkAtoms::optgroup,
  nsGkAtoms::option,
  nsGkAtoms::output,
  nsGkAtoms::p,
  nsGkAtoms::picture,
  nsGkAtoms::pre,
  nsGkAtoms::progress,
  nsGkAtoms::q,
  nsGkAtoms::rb,
  nsGkAtoms::rp,
  nsGkAtoms::rt,
  nsGkAtoms::rtc,
  nsGkAtoms::ruby,
  nsGkAtoms::s,
  nsGkAtoms::samp,
  nsGkAtoms::section,
  nsGkAtoms::select,
  nsGkAtoms::small,
  nsGkAtoms::source,
  nsGkAtoms::span,
  nsGkAtoms::strike,
  nsGkAtoms::strong,
  nsGkAtoms::sub,
  nsGkAtoms::summary,
  nsGkAtoms::sup,
  // style checked specially
  nsGkAtoms::table,
  nsGkAtoms::tbody,
  nsGkAtoms::td,
  // template checked and traversed specially
  nsGkAtoms::textarea,
  nsGkAtoms::tfoot,
  nsGkAtoms::th,
  nsGkAtoms::thead,
  nsGkAtoms::time,
  // title checked specially
  nsGkAtoms::tr,
  nsGkAtoms::track,
  nsGkAtoms::tt,
  nsGkAtoms::u,
  nsGkAtoms::ul,
  nsGkAtoms::var,
  nsGkAtoms::video,
  nsGkAtoms::wbr,
  nullptr
    // clang-format on
};

const nsStaticAtom* const kAttributesHTML[] = {
    // clang-format off
  nsGkAtoms::abbr,
  nsGkAtoms::accept,
  nsGkAtoms::acceptcharset,
  nsGkAtoms::accesskey,
  nsGkAtoms::action,
  nsGkAtoms::alt,
  nsGkAtoms::as,
  nsGkAtoms::autocomplete,
  nsGkAtoms::autofocus,
  nsGkAtoms::autoplay,
  nsGkAtoms::axis,
  nsGkAtoms::_char,
  nsGkAtoms::charoff,
  nsGkAtoms::charset,
  nsGkAtoms::checked,
  nsGkAtoms::cite,
  nsGkAtoms::_class,
  nsGkAtoms::cols,
  nsGkAtoms::colspan,
  nsGkAtoms::content,
  nsGkAtoms::contenteditable,
  nsGkAtoms::contextmenu,
  nsGkAtoms::controls,
  nsGkAtoms::coords,
  nsGkAtoms::crossorigin,
  nsGkAtoms::datetime,
  nsGkAtoms::dir,
  nsGkAtoms::disabled,
  nsGkAtoms::draggable,
  nsGkAtoms::enctype,
  nsGkAtoms::face,
  nsGkAtoms::_for,
  nsGkAtoms::frame,
  nsGkAtoms::headers,
  nsGkAtoms::height,
  nsGkAtoms::hidden,
  nsGkAtoms::high,
  nsGkAtoms::href,
  nsGkAtoms::hreflang,
  nsGkAtoms::icon,
  nsGkAtoms::id,
  nsGkAtoms::integrity,
  nsGkAtoms::ismap,
  nsGkAtoms::itemid,
  nsGkAtoms::itemprop,
  nsGkAtoms::itemref,
  nsGkAtoms::itemscope,
  nsGkAtoms::itemtype,
  nsGkAtoms::kind,
  nsGkAtoms::label,
  nsGkAtoms::lang,
  nsGkAtoms::list,
  nsGkAtoms::longdesc,
  nsGkAtoms::loop,
  nsGkAtoms::low,
  nsGkAtoms::max,
  nsGkAtoms::maxlength,
  nsGkAtoms::media,
  nsGkAtoms::method,
  nsGkAtoms::min,
  nsGkAtoms::minlength,
  nsGkAtoms::multiple,
  nsGkAtoms::muted,
  nsGkAtoms::name,
  nsGkAtoms::nohref,
  nsGkAtoms::novalidate,
  nsGkAtoms::nowrap,
  nsGkAtoms::open,
  nsGkAtoms::optimum,
  nsGkAtoms::pattern,
  nsGkAtoms::placeholder,
  nsGkAtoms::playbackrate,
  nsGkAtoms::poster,
  nsGkAtoms::preload,
  nsGkAtoms::prompt,
  nsGkAtoms::pubdate,
  nsGkAtoms::radiogroup,
  nsGkAtoms::readonly,
  nsGkAtoms::rel,
  nsGkAtoms::required,
  nsGkAtoms::rev,
  nsGkAtoms::reversed,
  nsGkAtoms::role,
  nsGkAtoms::rows,
  nsGkAtoms::rowspan,
  nsGkAtoms::rules,
  nsGkAtoms::scoped,
  nsGkAtoms::scope,
  nsGkAtoms::selected,
  nsGkAtoms::shape,
  nsGkAtoms::span,
  nsGkAtoms::spellcheck,
  nsGkAtoms::src,
  nsGkAtoms::srclang,
  nsGkAtoms::start,
  nsGkAtoms::summary,
  nsGkAtoms::tabindex,
  nsGkAtoms::target,
  nsGkAtoms::title,
  nsGkAtoms::type,
  nsGkAtoms::usemap,
  nsGkAtoms::value,
  nsGkAtoms::width,
  nsGkAtoms::wrap,
  nullptr
    // clang-format on
};

const nsStaticAtom* const kPresAttributesHTML[] = {
    // clang-format off
  nsGkAtoms::align,
  nsGkAtoms::background,
  nsGkAtoms::bgcolor,
  nsGkAtoms::border,
  nsGkAtoms::cellpadding,
  nsGkAtoms::cellspacing,
  nsGkAtoms::color,
  nsGkAtoms::compact,
  nsGkAtoms::clear,
  nsGkAtoms::hspace,
  nsGkAtoms::noshade,
  nsGkAtoms::pointSize,
  nsGkAtoms::size,
  nsGkAtoms::valign,
  nsGkAtoms::vspace,
  nullptr
    // clang-format on
};

// List of HTML attributes with URLs that the
// browser will fetch. Should be kept in sync with
// https://html.spec.whatwg.org/multipage/indices.html#attributes-3
const nsStaticAtom* const kURLAttributesHTML[] = {
    // clang-format off
  nsGkAtoms::action,
  nsGkAtoms::href,
  nsGkAtoms::src,
  nsGkAtoms::longdesc,
  nsGkAtoms::cite,
  nsGkAtoms::background,
  nsGkAtoms::formaction,
  nsGkAtoms::data,
  nsGkAtoms::ping,
  nsGkAtoms::poster,
  nullptr
    // clang-format on
};

const nsStaticAtom* const kElementsSVG[] = {
    nsGkAtoms::a,                    // a
    nsGkAtoms::circle,               // circle
    nsGkAtoms::clipPath,             // clipPath
    nsGkAtoms::color_profile,        // color-profile
    nsGkAtoms::cursor,               // cursor
    nsGkAtoms::defs,                 // defs
    nsGkAtoms::desc,                 // desc
    nsGkAtoms::ellipse,              // ellipse
    nsGkAtoms::elevation,            // elevation
    nsGkAtoms::erode,                // erode
    nsGkAtoms::ex,                   // ex
    nsGkAtoms::exact,                // exact
    nsGkAtoms::exponent,             // exponent
    nsGkAtoms::feBlend,              // feBlend
    nsGkAtoms::feColorMatrix,        // feColorMatrix
    nsGkAtoms::feComponentTransfer,  // feComponentTransfer
    nsGkAtoms::feComposite,          // feComposite
    nsGkAtoms::feConvolveMatrix,     // feConvolveMatrix
    nsGkAtoms::feDiffuseLighting,    // feDiffuseLighting
    nsGkAtoms::feDisplacementMap,    // feDisplacementMap
    nsGkAtoms::feDistantLight,       // feDistantLight
    nsGkAtoms::feDropShadow,         // feDropShadow
    nsGkAtoms::feFlood,              // feFlood
    nsGkAtoms::feFuncA,              // feFuncA
    nsGkAtoms::feFuncB,              // feFuncB
    nsGkAtoms::feFuncG,              // feFuncG
    nsGkAtoms::feFuncR,              // feFuncR
    nsGkAtoms::feGaussianBlur,       // feGaussianBlur
    nsGkAtoms::feImage,              // feImage
    nsGkAtoms::feMerge,              // feMerge
    nsGkAtoms::feMergeNode,          // feMergeNode
    nsGkAtoms::feMorphology,         // feMorphology
    nsGkAtoms::feOffset,             // feOffset
    nsGkAtoms::fePointLight,         // fePointLight
    nsGkAtoms::feSpecularLighting,   // feSpecularLighting
    nsGkAtoms::feSpotLight,          // feSpotLight
    nsGkAtoms::feTile,               // feTile
    nsGkAtoms::feTurbulence,         // feTurbulence
    nsGkAtoms::filter,               // filter
    nsGkAtoms::font,                 // font
    nsGkAtoms::font_face,            // font-face
    nsGkAtoms::font_face_format,     // font-face-format
    nsGkAtoms::font_face_name,       // font-face-name
    nsGkAtoms::font_face_src,        // font-face-src
    nsGkAtoms::font_face_uri,        // font-face-uri
    nsGkAtoms::foreignObject,        // foreignObject
    nsGkAtoms::g,                    // g
    // glyph
    nsGkAtoms::glyphRef,  // glyphRef
    // hkern
    nsGkAtoms::image,           // image
    nsGkAtoms::line,            // line
    nsGkAtoms::linearGradient,  // linearGradient
    nsGkAtoms::marker,          // marker
    nsGkAtoms::mask,            // mask
    nsGkAtoms::metadata,        // metadata
    nsGkAtoms::missingGlyph,    // missingGlyph
    nsGkAtoms::mpath,           // mpath
    nsGkAtoms::path,            // path
    nsGkAtoms::pattern,         // pattern
    nsGkAtoms::polygon,         // polygon
    nsGkAtoms::polyline,        // polyline
    nsGkAtoms::radialGradient,  // radialGradient
    nsGkAtoms::rect,            // rect
    nsGkAtoms::stop,            // stop
    nsGkAtoms::svg,             // svg
    nsGkAtoms::svgSwitch,       // switch
    nsGkAtoms::symbol,          // symbol
    nsGkAtoms::text,            // text
    nsGkAtoms::textPath,        // textPath
    nsGkAtoms::title,           // title
    nsGkAtoms::tref,            // tref
    nsGkAtoms::tspan,           // tspan
    nsGkAtoms::use,             // use
    nsGkAtoms::view,            // view
    // vkern
    nullptr};

constexpr const nsStaticAtom* const kAttributesSVG[] = {
    // accent-height
    nsGkAtoms::accumulate,          // accumulate
    nsGkAtoms::additive,            // additive
    nsGkAtoms::alignment_baseline,  // alignment-baseline
    // alphabetic
    nsGkAtoms::amplitude,  // amplitude
    // arabic-form
    // ascent
    nsGkAtoms::attributeName,   // attributeName
    nsGkAtoms::attributeType,   // attributeType
    nsGkAtoms::azimuth,         // azimuth
    nsGkAtoms::baseFrequency,   // baseFrequency
    nsGkAtoms::baseline_shift,  // baseline-shift
    // baseProfile
    // bbox
    nsGkAtoms::begin,     // begin
    nsGkAtoms::bias,      // bias
    nsGkAtoms::by,        // by
    nsGkAtoms::calcMode,  // calcMode
    // cap-height
    nsGkAtoms::_class,                       // class
    nsGkAtoms::clip_path,                    // clip-path
    nsGkAtoms::clip_rule,                    // clip-rule
    nsGkAtoms::clipPathUnits,                // clipPathUnits
    nsGkAtoms::color,                        // color
    nsGkAtoms::color_interpolation,          // color-interpolation
    nsGkAtoms::color_interpolation_filters,  // color-interpolation-filters
    nsGkAtoms::cursor,                       // cursor
    nsGkAtoms::cx,                           // cx
    nsGkAtoms::cy,                           // cy
    nsGkAtoms::d,                            // d
    // descent
    nsGkAtoms::diffuseConstant,    // diffuseConstant
    nsGkAtoms::direction,          // direction
    nsGkAtoms::display,            // display
    nsGkAtoms::divisor,            // divisor
    nsGkAtoms::dominant_baseline,  // dominant-baseline
    nsGkAtoms::dur,                // dur
    nsGkAtoms::dx,                 // dx
    nsGkAtoms::dy,                 // dy
    nsGkAtoms::edgeMode,           // edgeMode
    nsGkAtoms::elevation,          // elevation
    // enable-background
    nsGkAtoms::end,            // end
    nsGkAtoms::fill,           // fill
    nsGkAtoms::fill_opacity,   // fill-opacity
    nsGkAtoms::fill_rule,      // fill-rule
    nsGkAtoms::filter,         // filter
    nsGkAtoms::filterUnits,    // filterUnits
    nsGkAtoms::flood_color,    // flood-color
    nsGkAtoms::flood_opacity,  // flood-opacity
    // XXX focusable
    nsGkAtoms::font,              // font
    nsGkAtoms::font_family,       // font-family
    nsGkAtoms::font_size,         // font-size
    nsGkAtoms::font_size_adjust,  // font-size-adjust
    nsGkAtoms::font_stretch,      // font-stretch
    nsGkAtoms::font_style,        // font-style
    nsGkAtoms::font_variant,      // font-variant
    nsGkAtoms::font_weight,       // font-weight
    nsGkAtoms::format,            // format
    nsGkAtoms::from,              // from
    nsGkAtoms::fx,                // fx
    nsGkAtoms::fy,                // fy
    // g1
    // g2
    // glyph-name
    // glyphRef
    // glyph-orientation-horizontal
    // glyph-orientation-vertical
    nsGkAtoms::gradientTransform,  // gradientTransform
    nsGkAtoms::gradientUnits,      // gradientUnits
    nsGkAtoms::height,             // height
    nsGkAtoms::href,
    // horiz-adv-x
    // horiz-origin-x
    // horiz-origin-y
    nsGkAtoms::id,  // id
    // ideographic
    nsGkAtoms::image_rendering,  // image-rendering
    nsGkAtoms::in,               // in
    nsGkAtoms::in2,              // in2
    nsGkAtoms::intercept,        // intercept
    // k
    nsGkAtoms::k1,  // k1
    nsGkAtoms::k2,  // k2
    nsGkAtoms::k3,  // k3
    nsGkAtoms::k4,  // k4
    // kerning
    nsGkAtoms::kernelMatrix,      // kernelMatrix
    nsGkAtoms::kernelUnitLength,  // kernelUnitLength
    nsGkAtoms::keyPoints,         // keyPoints
    nsGkAtoms::keySplines,        // keySplines
    nsGkAtoms::keyTimes,          // keyTimes
    nsGkAtoms::lang,              // lang
    // lengthAdjust
    nsGkAtoms::letter_spacing,     // letter-spacing
    nsGkAtoms::lighting_color,     // lighting-color
    nsGkAtoms::limitingConeAngle,  // limitingConeAngle
    // local
    nsGkAtoms::marker,            // marker
    nsGkAtoms::marker_end,        // marker-end
    nsGkAtoms::marker_mid,        // marker-mid
    nsGkAtoms::marker_start,      // marker-start
    nsGkAtoms::markerHeight,      // markerHeight
    nsGkAtoms::markerUnits,       // markerUnits
    nsGkAtoms::markerWidth,       // markerWidth
    nsGkAtoms::mask,              // mask
    nsGkAtoms::maskContentUnits,  // maskContentUnits
    nsGkAtoms::maskUnits,         // maskUnits
    // mathematical
    nsGkAtoms::max,          // max
    nsGkAtoms::media,        // media
    nsGkAtoms::method,       // method
    nsGkAtoms::min,          // min
    nsGkAtoms::mode,         // mode
    nsGkAtoms::name,         // name
    nsGkAtoms::numOctaves,   // numOctaves
    nsGkAtoms::offset,       // offset
    nsGkAtoms::opacity,      // opacity
    nsGkAtoms::_operator,    // operator
    nsGkAtoms::order,        // order
    nsGkAtoms::orient,       // orient
    nsGkAtoms::orientation,  // orientation
    // origin
    // overline-position
    // overline-thickness
    nsGkAtoms::overflow,  // overflow
    // panose-1
    nsGkAtoms::path,                 // path
    nsGkAtoms::pathLength,           // pathLength
    nsGkAtoms::patternContentUnits,  // patternContentUnits
    nsGkAtoms::patternTransform,     // patternTransform
    nsGkAtoms::patternUnits,         // patternUnits
    nsGkAtoms::pointer_events,       // pointer-events XXX is this safe?
    nsGkAtoms::points,               // points
    nsGkAtoms::pointsAtX,            // pointsAtX
    nsGkAtoms::pointsAtY,            // pointsAtY
    nsGkAtoms::pointsAtZ,            // pointsAtZ
    nsGkAtoms::preserveAlpha,        // preserveAlpha
    nsGkAtoms::preserveAspectRatio,  // preserveAspectRatio
    nsGkAtoms::primitiveUnits,       // primitiveUnits
    nsGkAtoms::r,                    // r
    nsGkAtoms::radius,               // radius
    nsGkAtoms::refX,                 // refX
    nsGkAtoms::refY,                 // refY
    nsGkAtoms::repeatCount,          // repeatCount
    nsGkAtoms::repeatDur,            // repeatDur
    nsGkAtoms::requiredExtensions,   // requiredExtensions
    nsGkAtoms::requiredFeatures,     // requiredFeatures
    nsGkAtoms::restart,              // restart
    nsGkAtoms::result,               // result
    nsGkAtoms::rotate,               // rotate
    nsGkAtoms::rx,                   // rx
    nsGkAtoms::ry,                   // ry
    nsGkAtoms::scale,                // scale
    nsGkAtoms::seed,                 // seed
    nsGkAtoms::shape_rendering,      // shape-rendering
    nsGkAtoms::slope,                // slope
    nsGkAtoms::spacing,              // spacing
    nsGkAtoms::specularConstant,     // specularConstant
    nsGkAtoms::specularExponent,     // specularExponent
    nsGkAtoms::spreadMethod,         // spreadMethod
    nsGkAtoms::startOffset,          // startOffset
    nsGkAtoms::stdDeviation,         // stdDeviation
    // stemh
    // stemv
    nsGkAtoms::stitchTiles,   // stitchTiles
    nsGkAtoms::stop_color,    // stop-color
    nsGkAtoms::stop_opacity,  // stop-opacity
    // strikethrough-position
    // strikethrough-thickness
    nsGkAtoms::string,             // string
    nsGkAtoms::stroke,             // stroke
    nsGkAtoms::stroke_dasharray,   // stroke-dasharray
    nsGkAtoms::stroke_dashoffset,  // stroke-dashoffset
    nsGkAtoms::stroke_linecap,     // stroke-linecap
    nsGkAtoms::stroke_linejoin,    // stroke-linejoin
    nsGkAtoms::stroke_miterlimit,  // stroke-miterlimit
    nsGkAtoms::stroke_opacity,     // stroke-opacity
    nsGkAtoms::stroke_width,       // stroke-width
    nsGkAtoms::surfaceScale,       // surfaceScale
    nsGkAtoms::systemLanguage,     // systemLanguage
    nsGkAtoms::tableValues,        // tableValues
    nsGkAtoms::target,             // target
    nsGkAtoms::targetX,            // targetX
    nsGkAtoms::targetY,            // targetY
    nsGkAtoms::text_anchor,        // text-anchor
    nsGkAtoms::text_decoration,    // text-decoration
    // textLength
    nsGkAtoms::text_rendering,    // text-rendering
    nsGkAtoms::title,             // title
    nsGkAtoms::to,                // to
    nsGkAtoms::transform,         // transform
    nsGkAtoms::transform_origin,  // transform-origin
    nsGkAtoms::type,              // type
    // u1
    // u2
    // underline-position
    // underline-thickness
    // unicode
    nsGkAtoms::unicode_bidi,  // unicode-bidi
    // unicode-range
    // units-per-em
    // v-alphabetic
    // v-hanging
    // v-ideographic
    // v-mathematical
    nsGkAtoms::values,         // values
    nsGkAtoms::vector_effect,  // vector-effect
    // vert-adv-y
    // vert-origin-x
    // vert-origin-y
    nsGkAtoms::viewBox,     // viewBox
    nsGkAtoms::viewTarget,  // viewTarget
    nsGkAtoms::visibility,  // visibility
    nsGkAtoms::width,       // width
    // widths
    nsGkAtoms::word_spacing,  // word-spacing
    nsGkAtoms::writing_mode,  // writing-mode
    nsGkAtoms::x,             // x
    // x-height
    nsGkAtoms::x1,                // x1
    nsGkAtoms::x2,                // x2
    nsGkAtoms::xChannelSelector,  // xChannelSelector
    nsGkAtoms::y,                 // y
    nsGkAtoms::y1,                // y1
    nsGkAtoms::y2,                // y2
    nsGkAtoms::yChannelSelector,  // yChannelSelector
    nsGkAtoms::z,                 // z
    nsGkAtoms::zoomAndPan,        // zoomAndPan
    nullptr};

constexpr const nsStaticAtom* const kURLAttributesSVG[] = {nsGkAtoms::href,
                                                           nullptr};

static_assert(std::all_of(std::begin(kURLAttributesSVG),
                          std::end(kURLAttributesSVG),
                          [](auto aURLAttributeSVG) {
                            return std::any_of(std::begin(kAttributesSVG),
                                               std::end(kAttributesSVG),
                                               [&](auto aAttributeSVG) {
                                                 return aAttributeSVG ==
                                                        aURLAttributeSVG;
                                               });
                          }));

const nsStaticAtom* const kElementsMathML[] = {
    nsGkAtoms::abs,                  // abs
    nsGkAtoms::_and,                 // and
    nsGkAtoms::annotation,           // annotation
    nsGkAtoms::annotation_xml,       // annotation-xml
    nsGkAtoms::apply,                // apply
    nsGkAtoms::approx,               // approx
    nsGkAtoms::arccos,               // arccos
    nsGkAtoms::arccosh,              // arccosh
    nsGkAtoms::arccot,               // arccot
    nsGkAtoms::arccoth,              // arccoth
    nsGkAtoms::arccsc,               // arccsc
    nsGkAtoms::arccsch,              // arccsch
    nsGkAtoms::arcsec,               // arcsec
    nsGkAtoms::arcsech,              // arcsech
    nsGkAtoms::arcsin,               // arcsin
    nsGkAtoms::arcsinh,              // arcsinh
    nsGkAtoms::arctan,               // arctan
    nsGkAtoms::arctanh,              // arctanh
    nsGkAtoms::arg,                  // arg
    nsGkAtoms::bind,                 // bind
    nsGkAtoms::bvar,                 // bvar
    nsGkAtoms::card,                 // card
    nsGkAtoms::cartesianproduct,     // cartesianproduct
    nsGkAtoms::cbytes,               // cbytes
    nsGkAtoms::ceiling,              // ceiling
    nsGkAtoms::cerror,               // cerror
    nsGkAtoms::ci,                   // ci
    nsGkAtoms::cn,                   // cn
    nsGkAtoms::codomain,             // codomain
    nsGkAtoms::complexes,            // complexes
    nsGkAtoms::compose,              // compose
    nsGkAtoms::condition,            // condition
    nsGkAtoms::conjugate,            // conjugate
    nsGkAtoms::cos,                  // cos
    nsGkAtoms::cosh,                 // cosh
    nsGkAtoms::cot,                  // cot
    nsGkAtoms::coth,                 // coth
    nsGkAtoms::cs,                   // cs
    nsGkAtoms::csc,                  // csc
    nsGkAtoms::csch,                 // csch
    nsGkAtoms::csymbol,              // csymbol
    nsGkAtoms::curl,                 // curl
    nsGkAtoms::declare,              // declare
    nsGkAtoms::degree,               // degree
    nsGkAtoms::determinant,          // determinant
    nsGkAtoms::diff,                 // diff
    nsGkAtoms::divergence,           // divergence
    nsGkAtoms::divide,               // divide
    nsGkAtoms::domain,               // domain
    nsGkAtoms::domainofapplication,  // domainofapplication
    nsGkAtoms::el,                   // el
    nsGkAtoms::emptyset,             // emptyset
    nsGkAtoms::eq,                   // eq
    nsGkAtoms::equivalent,           // equivalent
    nsGkAtoms::eulergamma,           // eulergamma
    nsGkAtoms::exists,               // exists
    nsGkAtoms::exp,                  // exp
    nsGkAtoms::exponentiale,         // exponentiale
    nsGkAtoms::factorial,            // factorial
    nsGkAtoms::factorof,             // factorof
    nsGkAtoms::_false,               // false
    nsGkAtoms::floor,                // floor
    nsGkAtoms::fn,                   // fn
    nsGkAtoms::forall,               // forall
    nsGkAtoms::gcd,                  // gcd
    nsGkAtoms::geq,                  // geq
    nsGkAtoms::grad,                 // grad
    nsGkAtoms::gt,                   // gt
    nsGkAtoms::ident,                // ident
    nsGkAtoms::image,                // image
    nsGkAtoms::imaginary,            // imaginary
    nsGkAtoms::imaginaryi,           // imaginaryi
    nsGkAtoms::implies,              // implies
    nsGkAtoms::in,                   // in
    nsGkAtoms::infinity,             // infinity
    nsGkAtoms::int_,                 // int
    nsGkAtoms::integers,             // integers
    nsGkAtoms::intersect,            // intersect
    nsGkAtoms::interval,             // interval
    nsGkAtoms::inverse,              // inverse
    nsGkAtoms::lambda,               // lambda
    nsGkAtoms::laplacian,            // laplacian
    nsGkAtoms::lcm,                  // lcm
    nsGkAtoms::leq,                  // leq
    nsGkAtoms::limit,                // limit
    nsGkAtoms::list,                 // list
    nsGkAtoms::ln,                   // ln
    nsGkAtoms::log,                  // log
    nsGkAtoms::logbase,              // logbase
    nsGkAtoms::lowlimit,             // lowlimit
    nsGkAtoms::lt,                   // lt
    nsGkAtoms::maction,              // maction
    nsGkAtoms::maligngroup,          // maligngroup
    nsGkAtoms::malignmark,           // malignmark
    nsGkAtoms::math,                 // math
    nsGkAtoms::matrix,               // matrix
    nsGkAtoms::matrixrow,            // matrixrow
    nsGkAtoms::max,                  // max
    nsGkAtoms::mean,                 // mean
    nsGkAtoms::median,               // median
    nsGkAtoms::menclose,             // menclose
    nsGkAtoms::merror,               // merror
    nsGkAtoms::mfrac,                // mfrac
    nsGkAtoms::mglyph,               // mglyph
    nsGkAtoms::mi,                   // mi
    nsGkAtoms::min,                  // min
    nsGkAtoms::minus,                // minus
    nsGkAtoms::mlabeledtr,           // mlabeledtr
    nsGkAtoms::mlongdiv,             // mlongdiv
    nsGkAtoms::mmultiscripts,        // mmultiscripts
    nsGkAtoms::mn,                   // mn
    nsGkAtoms::mo,                   // mo
    nsGkAtoms::mode,                 // mode
    nsGkAtoms::moment,               // moment
    nsGkAtoms::momentabout,          // momentabout
    nsGkAtoms::mover,                // mover
    nsGkAtoms::mpadded,              // mpadded
    nsGkAtoms::mphantom,             // mphantom
    nsGkAtoms::mprescripts,          // mprescripts
    nsGkAtoms::mroot,                // mroot
    nsGkAtoms::mrow,                 // mrow
    nsGkAtoms::ms,                   // ms
    nsGkAtoms::mscarries,            // mscarries
    nsGkAtoms::mscarry,              // mscarry
    nsGkAtoms::msgroup,              // msgroup
    nsGkAtoms::msline,               // msline
    nsGkAtoms::mspace,               // mspace
    nsGkAtoms::msqrt,                // msqrt
    nsGkAtoms::msrow,                // msrow
    nsGkAtoms::mstack,               // mstack
    nsGkAtoms::mstyle,               // mstyle
    nsGkAtoms::msub,                 // msub
    nsGkAtoms::msubsup,              // msubsup
    nsGkAtoms::msup,                 // msup
    nsGkAtoms::mtable,               // mtable
    nsGkAtoms::mtd,                  // mtd
    nsGkAtoms::mtext,                // mtext
    nsGkAtoms::mtr,                  // mtr
    nsGkAtoms::munder,               // munder
    nsGkAtoms::munderover,           // munderover
    nsGkAtoms::naturalnumbers,       // naturalnumbers
    nsGkAtoms::neq,                  // neq
    nsGkAtoms::none,                 // none
    nsGkAtoms::_not,                 // not
    nsGkAtoms::notanumber,           // notanumber
    nsGkAtoms::note,                 // note
    nsGkAtoms::notin,                // notin
    nsGkAtoms::notprsubset,          // notprsubset
    nsGkAtoms::notsubset,            // notsubset
    nsGkAtoms::_or,                  // or
    nsGkAtoms::otherwise,            // otherwise
    nsGkAtoms::outerproduct,         // outerproduct
    nsGkAtoms::partialdiff,          // partialdiff
    nsGkAtoms::pi,                   // pi
    nsGkAtoms::piece,                // piece
    nsGkAtoms::piecewise,            // piecewise
    nsGkAtoms::plus,                 // plus
    nsGkAtoms::power,                // power
    nsGkAtoms::primes,               // primes
    nsGkAtoms::product,              // product
    nsGkAtoms::prsubset,             // prsubset
    nsGkAtoms::quotient,             // quotient
    nsGkAtoms::rationals,            // rationals
    nsGkAtoms::real,                 // real
    nsGkAtoms::reals,                // reals
    nsGkAtoms::reln,                 // reln
    nsGkAtoms::rem,                  // rem
    nsGkAtoms::root,                 // root
    nsGkAtoms::scalarproduct,        // scalarproduct
    nsGkAtoms::sdev,                 // sdev
    nsGkAtoms::sec,                  // sec
    nsGkAtoms::sech,                 // sech
    nsGkAtoms::selector,             // selector
    nsGkAtoms::semantics,            // semantics
    nsGkAtoms::sep,                  // sep
    nsGkAtoms::set,                  // set
    nsGkAtoms::setdiff,              // setdiff
    nsGkAtoms::share,                // share
    nsGkAtoms::sin,                  // sin
    nsGkAtoms::sinh,                 // sinh
    nsGkAtoms::subset,               // subset
    nsGkAtoms::sum,                  // sum
    nsGkAtoms::tan,                  // tan
    nsGkAtoms::tanh,                 // tanh
    nsGkAtoms::tendsto,              // tendsto
    nsGkAtoms::times,                // times
    nsGkAtoms::transpose,            // transpose
    nsGkAtoms::_true,                // true
    nsGkAtoms::union_,               // union
    nsGkAtoms::uplimit,              // uplimit
    nsGkAtoms::variance,             // variance
    nsGkAtoms::vector,               // vector
    nsGkAtoms::vectorproduct,        // vectorproduct
    nsGkAtoms::xor_,                 // xor
    nullptr};

const nsStaticAtom* const kAttributesMathML[] = {
    nsGkAtoms::accent,                // accent
    nsGkAtoms::accentunder,           // accentunder
    nsGkAtoms::actiontype,            // actiontype
    nsGkAtoms::align,                 // align
    nsGkAtoms::alignmentscope,        // alignmentscope
    nsGkAtoms::alt,                   // alt
    nsGkAtoms::altimg,                // altimg
    nsGkAtoms::altimg_height,         // altimg-height
    nsGkAtoms::altimg_valign,         // altimg-valign
    nsGkAtoms::altimg_width,          // altimg-width
    nsGkAtoms::background,            // background
    nsGkAtoms::base,                  // base
    nsGkAtoms::bevelled,              // bevelled
    nsGkAtoms::cd,                    // cd
    nsGkAtoms::cdgroup,               // cdgroup
    nsGkAtoms::charalign,             // charalign
    nsGkAtoms::close,                 // close
    nsGkAtoms::closure,               // closure
    nsGkAtoms::color,                 // color
    nsGkAtoms::columnalign,           // columnalign
    nsGkAtoms::columnalignment,       // columnalignment
    nsGkAtoms::columnlines,           // columnlines
    nsGkAtoms::columnspacing,         // columnspacing
    nsGkAtoms::columnspan,            // columnspan
    nsGkAtoms::columnwidth,           // columnwidth
    nsGkAtoms::crossout,              // crossout
    nsGkAtoms::decimalpoint,          // decimalpoint
    nsGkAtoms::definitionURL,         // definitionURL
    nsGkAtoms::denomalign,            // denomalign
    nsGkAtoms::depth,                 // depth
    nsGkAtoms::dir,                   // dir
    nsGkAtoms::display,               // display
    nsGkAtoms::displaystyle,          // displaystyle
    nsGkAtoms::edge,                  // edge
    nsGkAtoms::encoding,              // encoding
    nsGkAtoms::equalcolumns,          // equalcolumns
    nsGkAtoms::equalrows,             // equalrows
    nsGkAtoms::fence,                 // fence
    nsGkAtoms::fontfamily,            // fontfamily
    nsGkAtoms::fontsize,              // fontsize
    nsGkAtoms::fontstyle,             // fontstyle
    nsGkAtoms::fontweight,            // fontweight
    nsGkAtoms::form,                  // form
    nsGkAtoms::frame,                 // frame
    nsGkAtoms::framespacing,          // framespacing
    nsGkAtoms::groupalign,            // groupalign
    nsGkAtoms::height,                // height
    nsGkAtoms::href,                  // href
    nsGkAtoms::id,                    // id
    nsGkAtoms::indentalign,           // indentalign
    nsGkAtoms::indentalignfirst,      // indentalignfirst
    nsGkAtoms::indentalignlast,       // indentalignlast
    nsGkAtoms::indentshift,           // indentshift
    nsGkAtoms::indentshiftfirst,      // indentshiftfirst
    nsGkAtoms::indenttarget,          // indenttarget
    nsGkAtoms::index,                 // index
    nsGkAtoms::integer,               // integer
    nsGkAtoms::largeop,               // largeop
    nsGkAtoms::length,                // length
    nsGkAtoms::linebreak,             // linebreak
    nsGkAtoms::linebreakmultchar,     // linebreakmultchar
    nsGkAtoms::linebreakstyle,        // linebreakstyle
    nsGkAtoms::linethickness,         // linethickness
    nsGkAtoms::location,              // location
    nsGkAtoms::longdivstyle,          // longdivstyle
    nsGkAtoms::lquote,                // lquote
    nsGkAtoms::lspace,                // lspace
    nsGkAtoms::ltr,                   // ltr
    nsGkAtoms::mathbackground,        // mathbackground
    nsGkAtoms::mathcolor,             // mathcolor
    nsGkAtoms::mathsize,              // mathsize
    nsGkAtoms::mathvariant,           // mathvariant
    nsGkAtoms::maxsize,               // maxsize
    nsGkAtoms::minlabelspacing,       // minlabelspacing
    nsGkAtoms::minsize,               // minsize
    nsGkAtoms::movablelimits,         // movablelimits
    nsGkAtoms::msgroup,               // msgroup
    nsGkAtoms::name,                  // name
    nsGkAtoms::newline,               // newline
    nsGkAtoms::notation,              // notation
    nsGkAtoms::numalign,              // numalign
    nsGkAtoms::number,                // number
    nsGkAtoms::open,                  // open
    nsGkAtoms::order,                 // order
    nsGkAtoms::other,                 // other
    nsGkAtoms::overflow,              // overflow
    nsGkAtoms::position,              // position
    nsGkAtoms::role,                  // role
    nsGkAtoms::rowalign,              // rowalign
    nsGkAtoms::rowlines,              // rowlines
    nsGkAtoms::rowspacing,            // rowspacing
    nsGkAtoms::rowspan,               // rowspan
    nsGkAtoms::rquote,                // rquote
    nsGkAtoms::rspace,                // rspace
    nsGkAtoms::schemaLocation,        // schemaLocation
    nsGkAtoms::scriptlevel,           // scriptlevel
    nsGkAtoms::scriptminsize,         // scriptminsize
    nsGkAtoms::scriptsize,            // scriptsize
    nsGkAtoms::scriptsizemultiplier,  // scriptsizemultiplier
    nsGkAtoms::selection,             // selection
    nsGkAtoms::separator,             // separator
    nsGkAtoms::separators,            // separators
    nsGkAtoms::shift,                 // shift
    nsGkAtoms::side,                  // side
    nsGkAtoms::src,                   // src
    nsGkAtoms::stackalign,            // stackalign
    nsGkAtoms::stretchy,              // stretchy
    nsGkAtoms::subscriptshift,        // subscriptshift
    nsGkAtoms::superscriptshift,      // superscriptshift
    nsGkAtoms::symmetric,             // symmetric
    nsGkAtoms::type,                  // type
    nsGkAtoms::voffset,               // voffset
    nsGkAtoms::width,                 // width
    nsGkAtoms::xref,                  // xref
    nullptr};

const nsStaticAtom* const kURLAttributesMathML[] = {
    // clang-format off
  nsGkAtoms::href,
  nsGkAtoms::src,
  nsGkAtoms::cdgroup,
  nsGkAtoms::altimg,
  nsGkAtoms::definitionURL,
  nullptr
    // clang-format on
};

StaticAutoPtr<StaticAtomSet> nsTreeSanitizer::sElementsHTML;
StaticAutoPtr<StaticAtomSet> nsTreeSanitizer::sAttributesHTML;
StaticAutoPtr<StaticAtomSet> nsTreeSanitizer::sPresAttributesHTML;
StaticAutoPtr<StaticAtomSet> nsTreeSanitizer::sElementsSVG;
StaticAutoPtr<StaticAtomSet> nsTreeSanitizer::sAttributesSVG;
StaticAutoPtr<StaticAtomSet> nsTreeSanitizer::sElementsMathML;
StaticAutoPtr<StaticAtomSet> nsTreeSanitizer::sAttributesMathML;
StaticRefPtr<nsIPrincipal> nsTreeSanitizer::sNullPrincipal;

nsTreeSanitizer::nsTreeSanitizer(uint32_t aFlags)
    : mAllowStyles(aFlags & nsIParserUtils::SanitizerAllowStyle),
      mAllowComments(aFlags & nsIParserUtils::SanitizerAllowComments),
      mDropNonCSSPresentation(aFlags &
                              nsIParserUtils::SanitizerDropNonCSSPresentation),
      mDropForms(aFlags & nsIParserUtils::SanitizerDropForms),
      mCidEmbedsOnly(aFlags & nsIParserUtils::SanitizerCidEmbedsOnly),
      mDropMedia(aFlags & nsIParserUtils::SanitizerDropMedia),
      mFullDocument(false),
      mLogRemovals(aFlags & nsIParserUtils::SanitizerLogRemovals) {
  if (mCidEmbedsOnly) {
    // Sanitizing styles for external references is not supported.
    mAllowStyles = false;
  }

  if (!sElementsHTML) {
    // Initialize lazily to avoid having to initialize at all if the user
    // doesn't paste HTML or load feeds.
    InitializeStatics();
  }
}

bool nsTreeSanitizer::MustFlatten(int32_t aNamespace, nsAtom* aLocal) {
  if (aNamespace == kNameSpaceID_XHTML) {
    if (mDropNonCSSPresentation &&
        (nsGkAtoms::font == aLocal || nsGkAtoms::center == aLocal)) {
      return true;
    }
    if (mDropForms &&
        (nsGkAtoms::form == aLocal || nsGkAtoms::input == aLocal ||
         nsGkAtoms::option == aLocal || nsGkAtoms::optgroup == aLocal)) {
      return true;
    }
    if (mFullDocument &&
        (nsGkAtoms::title == aLocal || nsGkAtoms::html == aLocal ||
         nsGkAtoms::head == aLocal || nsGkAtoms::body == aLocal)) {
      return false;
    }
    if (nsGkAtoms::_template == aLocal) {
      return false;
    }
    return !sElementsHTML->Contains(aLocal);
  }
  if (aNamespace == kNameSpaceID_SVG) {
    if (mCidEmbedsOnly || mDropMedia) {
      // Sanitizing CSS-based URL references inside SVG presentational
      // attributes is not supported, so flattening for cid: embed case.
      return true;
    }
    return !sElementsSVG->Contains(aLocal);
  }
  if (aNamespace == kNameSpaceID_MathML) {
    return !sElementsMathML->Contains(aLocal);
  }
  return true;
}

bool nsTreeSanitizer::IsURL(const nsStaticAtom* const* aURLs,
                            nsAtom* aLocalName) {
  const nsStaticAtom* atom;
  while ((atom = *aURLs)) {
    if (atom == aLocalName) {
      return true;
    }
    ++aURLs;
  }
  return false;
}

bool nsTreeSanitizer::MustPrune(int32_t aNamespace, nsAtom* aLocal,
                                mozilla::dom::Element* aElement) {
  // To avoid attacks where a MathML script becomes something that gets
  // serialized in a way that it parses back as an HTML script, let's just
  // drop elements with the local name 'script' regardless of namespace.
  if (nsGkAtoms::script == aLocal) {
    return true;
  }
  if (aNamespace == kNameSpaceID_XHTML) {
    if (nsGkAtoms::title == aLocal && !mFullDocument) {
      // emulate the quirks of the old parser
      return true;
    }
    if (mDropForms &&
        (nsGkAtoms::select == aLocal || nsGkAtoms::button == aLocal ||
         nsGkAtoms::datalist == aLocal)) {
      return true;
    }
    if (mDropMedia &&
        (nsGkAtoms::img == aLocal || nsGkAtoms::video == aLocal ||
         nsGkAtoms::audio == aLocal || nsGkAtoms::source == aLocal)) {
      return true;
    }
    if (nsGkAtoms::meta == aLocal &&
        (aElement->HasAttr(nsGkAtoms::charset) ||
         aElement->HasAttr(nsGkAtoms::httpEquiv))) {
      // Throw away charset declarations even if they also have microdata
      // which they can't validly have.
      return true;
    }
    if (((!mFullDocument && nsGkAtoms::meta == aLocal) ||
         nsGkAtoms::link == aLocal) &&
        !(aElement->HasAttr(nsGkAtoms::itemprop) ||
          aElement->HasAttr(nsGkAtoms::itemscope))) {
      // emulate old behavior for non-Microdata <meta> and <link> presumably
      // in <head>. <meta> and <link> are whitelisted in order to avoid
      // corrupting Microdata when they appear in <body>. Note that
      // SanitizeAttributes() will remove the rel attribute from <link> and
      // the name attribute from <meta>.
      return true;
    }
  }
  if (mAllowStyles) {
    return nsGkAtoms::style == aLocal && !(aNamespace == kNameSpaceID_XHTML ||
                                           aNamespace == kNameSpaceID_SVG);
  }
  if (nsGkAtoms::style == aLocal) {
    return true;
  }
  return false;
}

/**
 * Parses a style sheet and reserializes it with unsafe styles removed.
 *
 * @param aOriginal the original style sheet source
 * @param aSanitized the reserialization without dangerous CSS.
 * @param aDocument the document the style sheet belongs to
 * @param aBaseURI the base URI to use
 * @param aSanitizationKind the kind of style sanitization to use.
 */
static void SanitizeStyleSheet(const nsAString& aOriginal,
                               nsAString& aSanitized, Document* aDocument,
                               nsIURI* aBaseURI,
                               StyleSanitizationKind aSanitizationKind) {
  aSanitized.Truncate();

  NS_ConvertUTF16toUTF8 style(aOriginal);
  nsIReferrerInfo* referrer =
      aDocument->ReferrerInfoForInternalCSSAndSVGResources();
  auto extraData =
      MakeRefPtr<URLExtraData>(aBaseURI, referrer, aDocument->NodePrincipal());
  RefPtr<StyleStylesheetContents> contents =
      Servo_StyleSheet_FromUTF8Bytes(
          /* loader = */ nullptr,
          /* stylesheet = */ nullptr,
          /* load_data = */ nullptr, &style,
          css::SheetParsingMode::eAuthorSheetFeatures, extraData.get(),
          aDocument->GetCompatibilityMode(),
          /* reusable_sheets = */ nullptr, StyleAllowImportRules::Yes,
          aSanitizationKind, &aSanitized)
          .Consume();
}

bool nsTreeSanitizer::SanitizeInlineStyle(
    Element* aElement, StyleSanitizationKind aSanitizationKind) {
  MOZ_ASSERT(aElement);
  MOZ_ASSERT(aElement->IsHTMLElement(nsGkAtoms::style) ||
             aElement->IsSVGElement(nsGkAtoms::style));

  nsAutoString styleText;
  nsContentUtils::GetNodeTextContent(aElement, false, styleText);

  nsAutoString sanitizedStyle;
  SanitizeStyleSheet(styleText, sanitizedStyle, aElement->OwnerDoc(),
                     aElement->GetBaseURI(), aSanitizationKind);
  RemoveAllAttributesFromDescendants(aElement);
  nsContentUtils::SetNodeTextContent(aElement, sanitizedStyle, true);

  return sanitizedStyle.Length() != styleText.Length();
}

void nsTreeSanitizer::RemoveConditionalCSSFromSubtree(nsINode* aRoot) {
  AutoTArray<RefPtr<nsINode>, 10> nodesToSanitize;
  for (nsINode* node : ShadowIncludingTreeIterator(*aRoot)) {
    if (node->IsHTMLElement(nsGkAtoms::style) ||
        node->IsSVGElement(nsGkAtoms::style)) {
      nodesToSanitize.AppendElement(node);
    }
  }
  for (nsINode* node : nodesToSanitize) {
    SanitizeInlineStyle(node->AsElement(),
                        StyleSanitizationKind::NoConditionalRules);
  }
}

template <size_t Len>
static bool UTF16StringStartsWith(const char16_t* aStr, uint32_t aLength,
                                  const char16_t (&aNeedle)[Len]) {
  MOZ_ASSERT(aNeedle[Len - 1] == '\0',
             "needle should be a UTF-16 encoded string literal");

  if (aLength < Len - 1) {
    return false;
  }
  for (size_t i = 0; i < Len - 1; i++) {
    if (aStr[i] != aNeedle[i]) {
      return false;
    }
  }
  return true;
}

void nsTreeSanitizer::SanitizeAttributes(mozilla::dom::Element* aElement,
                                         AllowedAttributes aAllowed) {
  int32_t ac = (int)aElement->GetAttrCount();

  for (int32_t i = ac - 1; i >= 0; --i) {
    const nsAttrName* attrName = aElement->GetAttrNameAt(i);
    int32_t attrNs = attrName->NamespaceID();
    RefPtr<nsAtom> attrLocal = attrName->LocalName();

    if (kNameSpaceID_None == attrNs) {
      if (aAllowed.mStyle && nsGkAtoms::style == attrLocal) {
        continue;
      }
      if (aAllowed.mDangerousSrc && nsGkAtoms::src == attrLocal) {
        continue;
      }
      if (IsURL(aAllowed.mURLs, attrLocal)) {
        bool fragmentOnly = aElement->IsSVGElement(nsGkAtoms::use);
        if (SanitizeURL(aElement, attrNs, attrLocal, fragmentOnly)) {
          // in case the attribute removal shuffled the attribute order, start
          // the loop again.
          --ac;
          i = ac;  // i will be decremented immediately thanks to the for loop
          continue;
        }
        // else fall through to see if there's another reason to drop this
        // attribute (in particular if the attribute is background="" on an
        // HTML element)
      }
      if (!mDropNonCSSPresentation &&
          (aAllowed.mNames == sAttributesHTML) &&  // element is HTML
          sPresAttributesHTML->Contains(attrLocal)) {
        continue;
      }
      if (aAllowed.mNames->Contains(attrLocal) &&
          !((attrLocal == nsGkAtoms::rel &&
             aElement->IsHTMLElement(nsGkAtoms::link)) ||
            (!mFullDocument && attrLocal == nsGkAtoms::name &&
             aElement->IsHTMLElement(nsGkAtoms::meta)))) {
        // name="" and rel="" are whitelisted, but treat them as blacklisted
        // for <meta name> (fragment case) and <link rel> (all cases) to avoid
        // document-wide metadata or styling overrides with non-conforming
        // <meta name itemprop> or
        // <link rel itemprop>
        continue;
      }
      const char16_t* localStr = attrLocal->GetUTF16String();
      uint32_t localLen = attrLocal->GetLength();
      // Allow underscore to cater to the MCE editor library.
      // Allow data-* on SVG and MathML, too, as a forward-compat measure.
      // Allow aria-* on all for simplicity.
      if (UTF16StringStartsWith(localStr, localLen, u"_") ||
          UTF16StringStartsWith(localStr, localLen, u"data-") ||
          UTF16StringStartsWith(localStr, localLen, u"aria-")) {
        continue;
      }
      // else not allowed
    } else if (kNameSpaceID_XML == attrNs) {
      if (nsGkAtoms::lang == attrLocal || nsGkAtoms::space == attrLocal) {
        continue;
      }
      // else not allowed
    } else if (aAllowed.mXLink && kNameSpaceID_XLink == attrNs) {
      if (nsGkAtoms::href == attrLocal) {
        bool fragmentOnly = aElement->IsSVGElement(nsGkAtoms::use);
        if (SanitizeURL(aElement, attrNs, attrLocal, fragmentOnly)) {
          // in case the attribute removal shuffled the attribute order, start
          // the loop again.
          --ac;
          i = ac;  // i will be decremented immediately thanks to the for loop
        }
        continue;
      }
      if (nsGkAtoms::type == attrLocal || nsGkAtoms::title == attrLocal ||
          nsGkAtoms::show == attrLocal || nsGkAtoms::actuate == attrLocal) {
        continue;
      }
      // else not allowed
    }
    aElement->UnsetAttr(attrNs, attrLocal, false);
    if (mLogRemovals) {
      LogMessage("Removed unsafe attribute.", aElement->OwnerDoc(), aElement,
                 attrLocal);
    }
    // in case the attribute removal shuffled the attribute order, start the
    // loop again.
    --ac;
    i = ac;  // i will be decremented immediately thanks to the for loop
  }

  // If we've got HTML audio or video, add the controls attribute, because
  // otherwise the content is unplayable with scripts removed.
  if (aElement->IsAnyOfHTMLElements(nsGkAtoms::video, nsGkAtoms::audio)) {
    aElement->SetAttr(kNameSpaceID_None, nsGkAtoms::controls, u""_ns, false);
  }
}

bool nsTreeSanitizer::SanitizeURL(mozilla::dom::Element* aElement,
                                  int32_t aNamespace, nsAtom* aLocalName,
                                  bool aFragmentsOnly) {
  nsAutoString value;
  aElement->GetAttr(aNamespace, aLocalName, value);

  // Get value and remove mandatory quotes
  static const char* kWhitespace = "\n\r\t\b";
  const nsAString& v = nsContentUtils::TrimCharsInSet(kWhitespace, value);
  // Fragment-only url cannot be harmful.
  if (!v.IsEmpty() && v.First() == u'#') {
    return false;
  }
  // if we allow only same-document fragment URLs, stop and remove here
  if (aFragmentsOnly) {
    aElement->UnsetAttr(aNamespace, aLocalName, false);
    if (mLogRemovals) {
      LogMessage("Removed unsafe URI from element attribute.",
                 aElement->OwnerDoc(), aElement, aLocalName);
    }
    return true;
  }

  nsIScriptSecurityManager* secMan = nsContentUtils::GetSecurityManager();
  uint32_t flags = nsIScriptSecurityManager::DISALLOW_INHERIT_PRINCIPAL;

  nsCOMPtr<nsIURI> attrURI;
  nsresult rv =
      NS_NewURI(getter_AddRefs(attrURI), v, nullptr, aElement->GetBaseURI());
  if (NS_SUCCEEDED(rv)) {
    if (mCidEmbedsOnly && kNameSpaceID_None == aNamespace) {
      if (nsGkAtoms::src == aLocalName || nsGkAtoms::background == aLocalName) {
        // comm-central uses a hack that makes nsIURIs created with cid: specs
        // actually have an about:blank spec. Therefore, nsIURI facilities are
        // useless for cid: when comm-central code is participating.
        if (!(v.Length() > 4 && (v[0] == 'c' || v[0] == 'C') &&
              (v[1] == 'i' || v[1] == 'I') && (v[2] == 'd' || v[2] == 'D') &&
              v[3] == ':')) {
          rv = NS_ERROR_FAILURE;
        }
      } else if (nsGkAtoms::cdgroup == aLocalName ||
                 nsGkAtoms::altimg == aLocalName ||
                 nsGkAtoms::definitionURL == aLocalName) {
        // Gecko doesn't fetch these now and shouldn't in the future, but
        // in case someone goofs with these in the future, let's drop them.
        rv = NS_ERROR_FAILURE;
      } else {
        rv = secMan->CheckLoadURIWithPrincipal(sNullPrincipal, attrURI, flags,
                                               0);
      }
    } else {
      rv = secMan->CheckLoadURIWithPrincipal(sNullPrincipal, attrURI, flags, 0);
    }
  }
  if (NS_FAILED(rv)) {
    aElement->UnsetAttr(aNamespace, aLocalName, false);
    if (mLogRemovals) {
      LogMessage("Removed unsafe URI from element attribute.",
                 aElement->OwnerDoc(), aElement, aLocalName);
    }
    return true;
  }
  return false;
}

void nsTreeSanitizer::Sanitize(DocumentFragment* aFragment) {
  // If you want to relax these preconditions, be sure to check the code in
  // here that notifies / does not notify or that fires mutation events if
  // in tree.
  MOZ_ASSERT(!aFragment->IsInUncomposedDoc(), "The fragment is in doc?");

  mFullDocument = false;
  SanitizeChildren(aFragment);
}

void nsTreeSanitizer::Sanitize(Document* aDocument) {
  // If you want to relax these preconditions, be sure to check the code in
  // here that notifies / does not notify or that fires mutation events if
  // in tree.
#ifdef DEBUG
  MOZ_ASSERT(!aDocument->GetContainer(), "The document is in a shell.");
  RefPtr<mozilla::dom::Element> root = aDocument->GetRootElement();
  MOZ_ASSERT(root->IsHTMLElement(nsGkAtoms::html), "Not HTML root.");
#endif

  mFullDocument = true;
  SanitizeChildren(aDocument);
}

void nsTreeSanitizer::SanitizeChildren(nsINode* aRoot) {
  nsIContent* node = aRoot->GetFirstChild();
  while (node) {
    if (node->IsElement()) {
      mozilla::dom::Element* elt = node->AsElement();
      mozilla::dom::NodeInfo* nodeInfo = node->NodeInfo();
      nsAtom* localName = nodeInfo->NameAtom();
      int32_t ns = nodeInfo->NamespaceID();

      if (MustPrune(ns, localName, elt)) {
        if (mLogRemovals) {
          LogMessage("Removing unsafe node.", elt->OwnerDoc(), elt);
        }
        RemoveAllAttributes(elt);
        nsIContent* descendant = node;
        while ((descendant = descendant->GetNextNode(node))) {
          if (descendant->IsElement()) {
            RemoveAllAttributes(descendant->AsElement());
          }
        }
        nsIContent* next = node->GetNextNonChildNode(aRoot);
        node->Remove();
        node = next;
        continue;
      }
      if (auto* templateEl = HTMLTemplateElement::FromNode(elt)) {
        // traverse into the DocFragment content attribute of template elements
        bool wasFullDocument = mFullDocument;
        mFullDocument = false;
        RefPtr<DocumentFragment> frag = templateEl->Content();
        SanitizeChildren(frag);
        mFullDocument = wasFullDocument;
      }
      if (nsGkAtoms::style == localName) {
        // If styles aren't allowed, style elements got pruned above. Even
        // if styles are allowed, non-HTML, non-SVG style elements got pruned
        // above.
        NS_ASSERTION(ns == kNameSpaceID_XHTML || ns == kNameSpaceID_SVG,
                     "Should have only HTML or SVG here!");
        if (SanitizeInlineStyle(elt, StyleSanitizationKind::Standard) &&
            mLogRemovals) {
          LogMessage("Removed some rules and/or properties from stylesheet.",
                     aRoot->OwnerDoc());
        }

        AllowedAttributes allowed;
        allowed.mStyle = mAllowStyles;
        if (ns == kNameSpaceID_XHTML) {
          allowed.mNames = sAttributesHTML;
          allowed.mURLs = kURLAttributesHTML;
        } else {
          allowed.mNames = sAttributesSVG;
          allowed.mURLs = kURLAttributesSVG;
          allowed.mXLink = true;
        }
        SanitizeAttributes(elt, allowed);
        node = node->GetNextNonChildNode(aRoot);
        continue;
      }
      if (MustFlatten(ns, localName)) {
        if (mLogRemovals) {
          LogMessage("Flattening unsafe node (descendants are preserved).",
                     elt->OwnerDoc(), elt);
        }
        RemoveAllAttributes(elt);
        nsCOMPtr<nsIContent> next = node->GetNextNode(aRoot);
        nsCOMPtr<nsIContent> parent = node->GetParent();
        nsCOMPtr<nsIContent> child;  // Must keep the child alive during move
        ErrorResult rv;
        while ((child = node->GetFirstChild())) {
          nsCOMPtr<nsINode> refNode = node;
          parent->InsertBeforeInternal(
              *child, refNode, MutationEffectOnScript::KeepTrustWorthiness, rv);
          if (rv.Failed()) {
            break;
          }
        }
        node->Remove();
        node = next;
        continue;
      }
      NS_ASSERTION(ns == kNameSpaceID_XHTML || ns == kNameSpaceID_SVG ||
                       ns == kNameSpaceID_MathML,
                   "Should have only HTML, MathML or SVG here!");
      AllowedAttributes allowed;
      if (ns == kNameSpaceID_XHTML) {
        allowed.mNames = sAttributesHTML;
        allowed.mURLs = kURLAttributesHTML;
        allowed.mStyle = mAllowStyles;
        allowed.mDangerousSrc = nsGkAtoms::img == localName && !mCidEmbedsOnly;
        SanitizeAttributes(elt, allowed);
      } else if (ns == kNameSpaceID_SVG) {
        allowed.mNames = sAttributesSVG;
        allowed.mURLs = kURLAttributesSVG;
        allowed.mXLink = true;
        allowed.mStyle = mAllowStyles;
        SanitizeAttributes(elt, allowed);
      } else {
        allowed.mNames = sAttributesMathML;
        allowed.mURLs = kURLAttributesMathML;
        allowed.mXLink = true;
        SanitizeAttributes(elt, allowed);
      }
      node = node->GetNextNode(aRoot);
      continue;
    }
    NS_ASSERTION(!node->GetFirstChild(), "How come non-element node had kids?");
    nsIContent* next = node->GetNextNonChildNode(aRoot);
    if (!mAllowComments && node->IsComment()) {
      node->Remove();
    }
    node = next;
  }
}

void nsTreeSanitizer::RemoveAllAttributes(Element* aElement) {
  const nsAttrName* attrName;
  while (aElement->GetAttrNameAt(0, &attrName)) {
    int32_t attrNs = attrName->NamespaceID();
    RefPtr<nsAtom> attrLocal = attrName->LocalName();
    aElement->UnsetAttr(attrNs, attrLocal, false);
  }
}

void nsTreeSanitizer::RemoveAllAttributesFromDescendants(
    mozilla::dom::Element* aElement) {
  nsIContent* node = aElement->GetFirstChild();
  while (node) {
    if (node->IsElement()) {
      mozilla::dom::Element* elt = node->AsElement();
      RemoveAllAttributes(elt);
    }
    node = node->GetNextNode(aElement);
  }
}

void nsTreeSanitizer::LogMessage(const char* aMessage, Document* aDoc,
                                 Element* aElement, nsAtom* aAttr) {
  if (mLogRemovals) {
    nsAutoString msg;
    msg.AssignASCII(aMessage);
    if (aElement) {
      msg.Append(u" Element: "_ns + aElement->LocalName() + u"."_ns);
    }
    if (aAttr) {
      msg.Append(u" Attribute: "_ns + nsDependentAtomString(aAttr) + u"."_ns);
    }

    nsContentUtils::ReportToConsoleNonLocalized(
        msg, nsIScriptError::warningFlag, "DOM"_ns, aDoc);
  }
}

void nsTreeSanitizer::InitializeStatics() {
  MOZ_ASSERT(!sElementsHTML, "Initializing a second time.");

  sElementsHTML = new StaticAtomSet(std::size(kElementsHTML));
  for (uint32_t i = 0; kElementsHTML[i]; i++) {
    sElementsHTML->Insert(kElementsHTML[i]);
  }

  sAttributesHTML = new StaticAtomSet(std::size(kAttributesHTML));
  for (uint32_t i = 0; kAttributesHTML[i]; i++) {
    sAttributesHTML->Insert(kAttributesHTML[i]);
  }

  sPresAttributesHTML = new StaticAtomSet(std::size(kPresAttributesHTML));
  for (uint32_t i = 0; kPresAttributesHTML[i]; i++) {
    sPresAttributesHTML->Insert(kPresAttributesHTML[i]);
  }

  sElementsSVG = new StaticAtomSet(std::size(kElementsSVG));
  for (uint32_t i = 0; kElementsSVG[i]; i++) {
    sElementsSVG->Insert(kElementsSVG[i]);
  }

  sAttributesSVG = new StaticAtomSet(std::size(kAttributesSVG));
  for (uint32_t i = 0; kAttributesSVG[i]; i++) {
    sAttributesSVG->Insert(kAttributesSVG[i]);
  }

  sElementsMathML = new StaticAtomSet(std::size(kElementsMathML));
  for (uint32_t i = 0; kElementsMathML[i]; i++) {
    sElementsMathML->Insert(kElementsMathML[i]);
  }

  sAttributesMathML = new StaticAtomSet(std::size(kAttributesMathML));
  for (uint32_t i = 0; kAttributesMathML[i]; i++) {
    sAttributesMathML->Insert(kAttributesMathML[i]);
  }

  sNullPrincipal = NullPrincipal::CreateWithoutOriginAttributes();
}

void nsTreeSanitizer::ReleaseStatics() {
  sElementsHTML = nullptr;
  sAttributesHTML = nullptr;
  sPresAttributesHTML = nullptr;
  sElementsSVG = nullptr;
  sAttributesSVG = nullptr;
  sElementsMathML = nullptr;
  sAttributesMathML = nullptr;
  sNullPrincipal = nullptr;
}
