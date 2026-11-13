{%- import "macros.swift" as swift %}
{%- let inner = self.inner() %}
{%- let class_name = inner.name()|class_name -%}
{{ swift::render_class(inner) }}

{%- if inner.has_prefs() %}

extension {{ class_name }}: FMLFeatureInterface {
    public func isModified() -> Bool {
        guard let prefs = {{ swift::prefs() }} else {
            return false
        }
        let keys = [
            {%- for p in inner.props() %}
            {%- if p.has_prefs() %}
            {{ p.pref_key().unwrap()|quoted }},
            {%- endif %}
            {%- endfor %}
        ]
        if let _ = keys.first(where: { prefs.object(forKey: $0) != nil }) {
            return true
        }
        return false
    }
}
{%- else %}
extension {{ class_name }}: FMLFeatureInterface {}
{%- endif %}
