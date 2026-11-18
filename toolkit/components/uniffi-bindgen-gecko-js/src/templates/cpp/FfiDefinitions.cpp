/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

extern "C" {
  {%- for lib in root.libraries() %}
{{ lib.ifdef_start() }}
  {%- for def in lib.ffi_definitions %}
  {%- match def %}
  {%- when FfiDefinition::RustFunction(func) %}
  {{ func.return_type.type_name() }} {{ func.name.0 }}({{ func.arg_types()|join(", ") }});
  {%- when FfiDefinition::FunctionType(func) %}
  typedef {{ func.return_type.type_name() }} (*{{ func.name.0 }})({{ func.arg_types()|join(", ") }});
  {%- when FfiDefinition::Struct(ffi_struct) %}
  struct {{ ffi_struct.name.0 }} {
    {%- for field in ffi_struct.fields %}
    {{ field.ty.type_name }} {{ field.name }};
    {%- endfor %}
  };
  {%- endmatch %}
  {%- endfor %}
{{ lib.ifdef_end() }}
  {%- endfor %}
}
