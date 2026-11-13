/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

 {#- This file contains macros needed to generate code for the FML.

    It is the natural place to put commonalities between Object and Features,
    and rendering literals for Objects.
-#}

{%- macro render_class(inner) %}
{%- let raw_name = inner.name() %}
{% let class_name = inner.name()|class_name -%}

{{ inner.doc()|comment("") }}
public class {{class_name}}: FMLFeatureInterface {
    private let _variables: Variables
    private let _defaults: Defaults

    private init(variables: Variables = NilVariables.instance, defaults: Defaults) {
        self._variables = variables
        self._defaults = defaults
    }
    {# The struct holds the default values that come from the manifest. They should completely
    specify all values needed for the  feature #}
    struct Defaults {
        {%- for p in inner.props() %}
        {%- let t = p.typ() %}
        let {{p.name()|var_name}}: {{ t|defaults_type_label }}
    {%- endfor %}
    }

    {#- A constructor for application tests to use.  #}

    public convenience init(
        _ _variables: Variables = NilVariables.instance,
        {%- for p in inner.props() %}
        {%- let t = p.typ() %}
        {{p.name()|var_name}}: {{ t|defaults_type_label }} = {{ t|literal(self, p.default(), "") }}{% if !loop.last %},{% endif %}
    {%- endfor %}
    ) {
        self.init(variables: _variables, defaults: Defaults({% for p in inner.props() %}
            {{p.name()|var_name}}: {{p.name()|var_name}}{% if !loop.last %},{% endif %}
        {%- endfor %}))
    }

    enum CodingKeys: String, CodingKey {
    {%- for p in inner.props() %}
    {%- let prop = p.name() %}
        case {{ prop|var_name }} = {{ prop|quoted }}
    {%- endfor %}
    }

    public func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
    {%- for p in inner.props() %}
    {%- let prop = p.name()|var_name %}
        try container.encode({{ prop|to_json(p.typ()) }}, forKey: .{{ prop }})
    {%- endfor %}
    }

{#- The property initializers #}
{# -#}
    {% for p in inner.props() %}
    {%- let prop_swift = p.name()|var_name %}
    {%- let type_swift = p.typ()|type_label %}
    {%- let defaults = format!("_defaults.{}", prop_swift) %}
    {%- let getter = p.typ()|property(p.name(), "self._variables", defaults) %}
    {{ p.doc()|comment("    ") }}
    public lazy var {{ prop_swift }}: {{ type_swift }} = {
        {{ getter }}
    }()
    {% endfor %}
}

{%- endmacro %}}
