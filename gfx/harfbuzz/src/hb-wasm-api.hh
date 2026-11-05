/*
 * Copyright © 2023  Behdad Esfahbod
 *
 *  This is part of HarfBuzz, a text shaping library.
 *
 * Permission is hereby granted, without written agreement and without
 * license or royalty fees, to use, copy, modify, and distribute this
 * software and its documentation for any purpose, provided that the
 * above copyright notice and the following two paragraphs appear in
 * all copies of this software.
 *
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES
 * ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN
 * IF THE COPYRIGHT HOLDER HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * THE COPYRIGHT HOLDER SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE COPYRIGHT HOLDER HAS NO OBLIGATION TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 */

#ifndef HB_WASM_API_HH
#define HB_WASM_API_HH

#include "hb.hh"

#include <wasm_export.h>

#define HB_WASM_BEGIN_DECLS namespace hb { namespace wasm {
#define HB_WASM_END_DECLS }}

#define HB_WASM_API(ret_t, name) HB_INTERNAL ret_t name
#define HB_WASM_API_COMPOUND(ret_t, name) HB_INTERNAL void name

#define HB_WASM_EXEC_ENV wasm_exec_env_t exec_env,
#define HB_WASM_EXEC_ENV_COMPOUND wasm_exec_env_t exec_env, ptr_t() retptr,

#define ptr_t(type_t) uint32_t
#define ptr_d(type_t, name) uint32_t name##ptr

#include "hb-wasm-api.h"

#undef HB_WASM_BEGIN_DECLS
#undef HB_WASM_END_DECLS


enum {
  hb_wasm_ref_type_none,
  hb_wasm_ref_type_face,
  hb_wasm_ref_type_font,
  hb_wasm_ref_type_buffer,
};

HB_INTERNAL extern hb_user_data_key_t _hb_wasm_ref_type_key;

#define nullref 0

#define HB_REF2OBJ(obj) \
  hb_##obj##_t *obj = nullptr; \
  HB_STMT_START { \
    (void) wasm_externref_ref2obj (obj##ptr, (void **) &obj); \
    /* Check object type. */ \
    /* This works because all our objects have the same hb_object_t layout. */ \
    if (unlikely (hb_##obj##_get_user_data (obj, &_hb_wasm_ref_type_key) != \
		  (void *) hb_wasm_ref_type_##obj)) \
      obj = hb_##obj##_get_empty (); \
  } HB_STMT_END

#define HB_OBJ2REF(obj) \
  uint32_t obj##ref = nullref; \
  HB_STMT_START { \
    hb_##obj##_set_user_data (obj, &_hb_wasm_ref_type_key, \
			      (void *) hb_wasm_ref_type_##obj, \
			      nullptr, false); \
    (void) wasm_externref_obj2ref (module_inst, obj, &obj##ref); \
  } HB_STMT_END

#define HB_RETURN_STRUCT(type, name) \
  type *_name_ptr = nullptr; \
  { \
    if (likely (wasm_runtime_validate_app_addr (module_inst, \
						retptr, sizeof (type)))) \
      _name_ptr = (type *) wasm_runtime_addr_app_to_native (module_inst, retptr); \
    if (unlikely (!_name_ptr)) \
      return; \
  } \
  type &name = *_name_ptr

#define HB_PTR_PARAM(type, name) \
  type *name = nullptr; \
  HB_STMT_START { \
    if (likely (wasm_runtime_validate_app_addr (module_inst, \
						name##ptr, sizeof (type)))) \
      name = (type *) wasm_runtime_addr_app_to_native (module_inst, name##ptr); \
  } HB_STMT_END

#define HB_ARRAY_PARAM(type, name, length) \
  type *name = nullptr; \
  HB_STMT_START { \
    if (likely (!hb_unsigned_mul_overflows (length, sizeof (type)) && \
		wasm_runtime_validate_app_addr (module_inst, \
						name##ptr, length * sizeof (type)))) \
      name = (type *) wasm_runtime_addr_app_to_native (module_inst, name##ptr); \
  } HB_STMT_END

#define HB_ARRAY_APP2NATIVE(type, name, length) \
    ((type *) (!hb_unsigned_mul_overflows (length, sizeof (type)) && \
	       validate_app_addr (name, (length) * sizeof (type)) ? \
	       addr_app_to_native (name) : nullptr))


#endif /* HB_WASM_API_HH */
