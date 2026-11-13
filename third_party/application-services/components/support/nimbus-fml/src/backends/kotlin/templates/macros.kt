/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

 {#- This file contains macros needed to generate code for the FML.

    It is the natural place to put commonalities between Object and Features,
    and rendering literals for Objects.
-#}

{% macro render_constructor() -%}
private constructor(
   private val _variables: Variables,
   private val _prefs: SharedPreferences? = null,
   private val _defaults: Defaults)
{% endmacro %}

{% macro render_class_body(inner) %}
{%- let prop_name = inner.name()|var_name %}
{%- let raw_name = inner.name() -%}
   {# The data class holds the default values that come from the manifest. They should completely
   specify all values needed for the  feature #}
   private data class Defaults({% for p in inner.props() %}
      {%- let t = p.typ() %}
      val {{p.name()|var_name}}: {{ t|defaults_type_label }}{% if !loop.last %},{% endif %}
   {%- endfor %}
   )

   {#- A constructor for application tests to use.  #}

   constructor(_variables: Variables = NullVariables.instance, _prefs: SharedPreferences? = null, {% for p in inner.props() %}
   {%- let t = p.typ() %}
      {{p.name()|var_name}}: {{ t|defaults_type_label }} = {{ t|literal(self, p.default(), "_variables.context") }}{% if !loop.last %},{% endif %}
   {%- endfor %}
   ) : this(
      _variables = _variables,
      _prefs = _prefs,
      _defaults = Defaults({% for p in inner.props() %}
      {%- let nm = p.name()|var_name %}{{ nm }} = {{ nm }}{% if !loop.last %}, {% endif %}
      {%- endfor %})
   )

   {# The property initializers #}
   {%- for p in inner.props() %}
   {%- let prop_kt = p.name()|var_name %}
   {%- let type_kt = p.typ()|type_label %}
   {%- let defaults = format!("_defaults.{}", prop_kt) %}
   {%- let getter = p.typ()|property(p.name(), "_variables", defaults) %}
  {{ p.doc()|comment("  ") }}
   {%- if !p.has_prefs() %}
   val {{ prop_kt }}: {{ type_kt }} by lazy {
      {{ getter }}
   }
   {%- else %}
   val {{ prop_kt }}: {{ type_kt }}
      get() =
         {%- let prefs = "it" %}
         {%- let key = p.pref_key().unwrap() %}
         {{ prefs() }}?.let {
            if ({{ prefs }}.contains({{ key|quoted }})) {
               try {
                  {{ p.typ()|preference_getter(prefs, key) }}
               } catch (e:  ClassCastException) {
                  // This only gets to here if the app has written the
                  // wrong type to this preference.
                  null
               }
            } else {
               null
            }
         } ?: {{ getter }}
   {%- endif %}
{% endfor %}

   {#- toJSON #}
   override fun toJSONObject(): JSONObject =
      JSONObject(
         mapOf(
            {%- for p in inner.props() %}
            {{ p.name()|quoted }} to {{ p.name()|var_name|to_json(p.typ()) }},
            {%- endfor %}
         )
      )
{% endmacro %}

{% macro prefs() %}this._prefs{% endmacro %}
