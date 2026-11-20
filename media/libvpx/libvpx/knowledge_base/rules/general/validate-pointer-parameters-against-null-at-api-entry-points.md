---
name: libvpx/validate-pointer-parameters-against-null-at-api-entry-points
description: When functions or API entry points accept pointer arguments, validate them against NULL or nullptr before dereferencing, string formatting, or downstream passing
rubric: safety
severity: blocking
---

**Rule.** When functions or API entry points accept pointer parameters (such as
output pointers, C-strings `const char *`, context structs, or `FILE *`
handles), validate them against `NULL` or `nullptr` at function entry before any
statements dereference them, pass them to callee functions, or pass them to
formatting functions like `snprintf` or `fprintf`. If a pointer parameter is
null, log a diagnostic or return an appropriate error status (or early return)
immediately rather than proceeding.

**Why.** Dereferencing pointer parameters, passing unvalidated pointers to
downstream functions, or formatting null strings with `%s` in
`snprintf`/`fprintf` leads to null-pointer dereferences, crashes, and undefined
behavior. Validating pointers at function entry ensures graceful error reporting
and memory safety across all execution paths.

## Examples

Bad:

```cpp
// Bad: Dereferencing output pointers in early return branch before NULL check.
void get_layer_resolution(const int width_org, const int height_org,
                          const int num, const int den, int *width_out,
                          int *height_out) {
  if (den == 0 || num == 0) {
    *width_out = width_org;
    *height_out = height_org;
    return;
  }
  if (width_out == NULL || height_out == NULL) return;
  // ...
}

// Bad: Passing unvalidated pointers/strings to formatting or callee functions.
StatusCode SimpleEncode::SetEncodeConfig(const char *name, const char *value) {
  EncodeConfig config;
  snprintf(config.name, ENCODE_CONFIG_BUF_SIZE, "%s", name);
  snprintf(config.value, ENCODE_CONFIG_BUF_SIZE, "%s", value);
  impl_ptr_->encode_config_list.push_back(config);
  return StatusOk;
}
```

Good:

```cpp
// Good: Validate output pointers at function entry before any dereferences.
void get_layer_resolution(const int width_org, const int height_org,
                          const int num, const int den, int *width_out,
                          int *height_out) {
  if (width_out == NULL || height_out == NULL) return;
  if (den == 0 || num == 0) {
    *width_out = width_org;
    *height_out = height_org;
    return;
  }
  // ...
}

// Good: Validate pointers at API entry and log/return error status before use.
StatusCode SimpleEncode::SetEncodeConfig(const char *name, const char *value) {
  if (name == nullptr || value == nullptr) {
    fprintf(stderr, "SetEncodeConfig: null pointer, name %p value %p\n", name,
            value);
    return StatusError;
  }
  EncodeConfig config;
  snprintf(config.name, ENCODE_CONFIG_BUF_SIZE, "%s", name);
  snprintf(config.value, ENCODE_CONFIG_BUF_SIZE, "%s", value);
  impl_ptr_->encode_config_list.push_back(config);
  return StatusOk;
}
```
