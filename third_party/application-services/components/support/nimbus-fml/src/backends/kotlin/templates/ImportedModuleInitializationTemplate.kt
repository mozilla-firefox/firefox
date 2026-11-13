{%- import "macros.kt" as kt %}
{%- let variables = "variables" %}
{%- let prefs = "prefs" %}
{%- let context = "variables.context" %}
{%- let class_name = self.inner.about().nimbus_object_name_kt() %}
        {{- class_name }}.features.apply {
            {%- for f in self.inner.features() %}
            {{ f.name()|var_name }}.withInitializer { {{ variables }}: Variables, {{ prefs }}: SharedPreferences? ->
                {{ f.name()|class_name }}(
                    _variables = {{ variables }},
                    _prefs = {{ prefs }}, {%- for p in f.props() %}
                    {{p.name()|var_name}} = {{ p.typ()|literal(self, p.default(), context) }}{% if !loop.last %},{% endif %}
                    {%- endfor %}
                )
            }
            {%- endfor %}
        }
