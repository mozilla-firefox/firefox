{%- import "macros.kt" as kt %}
{%- let inner = self.inner() %}

{{ inner.doc()|comment("") }}
public class {{ inner.name()|class_name }}  {{ kt::render_constructor() }} : FMLFeatureInterface {
    {{ kt::render_class_body(inner) }}

    {%- if inner.has_prefs() %}
    override fun isModified(): Boolean =
        {{ kt::prefs() }}?.let { prefs ->
            listOf(
            {%- for p in inner.props() %}
            {%- if p.has_prefs() %}
                {{ p.pref_key().unwrap()|quoted }},
            {%- endif %}
            {%- if p.has_gecko_prefs() %}
                {{ p.gecko_pref().unwrap().pref()|quoted }},
            {%- endif %}
            {%- endfor %}
            ).any { prefs.contains(it) }
        } ?: false
    {%- endif %}
}
