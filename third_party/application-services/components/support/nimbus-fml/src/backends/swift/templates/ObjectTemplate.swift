{%- import "macros.swift" as swift %}
{%- let inner = self.inner() %}
{{ swift::render_class(inner) -}}
{%- let class_name = inner.name()|class_name %}

public extension {{class_name}} {
    func _mergeWith(_ defaults: {{class_name}}?) -> {{class_name}} {
        guard let defaults = defaults else {
            return self
        }
        return {{class_name}}(variables: self._variables, prefs: self._prefs, defaults: defaults._defaults)
    }

    static func create(_ variables: Variables?) -> {{class_name}} {
        return {{class_name}}(variables ?? NilVariables.instance)
    }

    static func mergeWith(_ overrides: {{class_name}}, _ defaults: {{class_name}}) -> {{class_name}} {
        return overrides._mergeWith(defaults)
    }
}
