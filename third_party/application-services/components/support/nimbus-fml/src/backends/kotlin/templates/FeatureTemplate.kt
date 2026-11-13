{%- import "macros.kt" as kt %}
{%- let inner = self.inner() %}

{{ inner.doc()|comment("") }}
public class {{ inner.name()|class_name }}  {{ kt::render_constructor() }} : FMLFeatureInterface {
    {{ kt::render_class_body(inner) }}
}
