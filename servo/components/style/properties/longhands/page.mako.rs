/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

<%namespace name="helpers" file="/helpers.mako.rs" />
<% from data import PAGE_RULE %>

${helpers.predefined_type(
    "size",
    "PageSize",
    "computed::PageSize::auto()",
    engines="gecko",
    initial_specified_value="specified::PageSize::auto()",
    spec="https://drafts.csswg.org/css-page-3/#page-size-prop",
    boxed=True,
    animation_type="none",
    rule_types_allowed=PAGE_RULE,
    affects="layout",
)}

${helpers.predefined_type(
    "page",
    "PageName",
    "computed::PageName::auto()",
    engines="gecko",
    spec="https://drafts.csswg.org/css-page-3/#using-named-pages",
    animation_type="discrete",
    affects="layout",
)}

${helpers.predefined_type(
    "page-orientation",
    "PageOrientation",
    "computed::PageOrientation::Upright",
    engines="gecko",
    initial_specified_value="specified::PageOrientation::Upright",
    spec="https://drafts.csswg.org/css-page-3/#page-orientation-prop",
    animation_type="none",
    rule_types_allowed=PAGE_RULE,
    affects="layout",
)}
