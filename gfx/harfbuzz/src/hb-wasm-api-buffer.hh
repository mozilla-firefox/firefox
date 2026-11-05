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

#ifndef HB_WASM_API_BUFFER_HH
#define HB_WASM_API_BUFFER_HH

#include "hb-wasm-api.hh"

#include "hb-buffer.hh"

namespace hb {
namespace wasm {

static_assert (sizeof (glyph_info_t) == sizeof (hb_glyph_info_t), "");
static_assert (sizeof (glyph_position_t) == sizeof (hb_glyph_position_t), "");

HB_WASM_API (bool_t, buffer_contents_realloc) (HB_WASM_EXEC_ENV
					       ptr_d(buffer_contents_t, contents),
					       uint32_t size)
{
  HB_PTR_PARAM (buffer_contents_t, contents);
  if (unlikely (!contents))
    return false;

  if (size <= contents->length)
    return true;

  unsigned bytes;
  if (hb_unsigned_mul_overflows (size, sizeof (glyph_info_t), &bytes))
    return false;

  glyph_info_t *info = HB_ARRAY_APP2NATIVE (glyph_info_t, contents->info, contents->length);
  glyph_position_t *pos = HB_ARRAY_APP2NATIVE (glyph_position_t, contents->pos, contents->length);

  if (unlikely (!info || !pos))
    return false;

  glyph_info_t *new_info = nullptr;
  uint32_t new_inforef = module_malloc (bytes, (void **) &new_info);
  glyph_position_t *new_pos = nullptr;
  uint32_t new_posref = module_malloc (bytes, (void **) &new_pos);

  unsigned old_bytes = contents->length * sizeof (glyph_info_t);
  if (likely (new_inforef))
  {
    hb_memcpy (new_info, info, old_bytes);
    module_free (contents->info);
    contents->info = new_inforef;
  }
  if (likely (new_posref))
  {
    hb_memcpy (new_pos, pos, old_bytes);
    module_free (contents->pos);
    contents->pos = new_posref;
  }

  if (likely (new_info && new_pos))
  {
    contents->length = size;
    return true;
  }

  return false;
}

HB_WASM_API (void, buffer_contents_free) (HB_WASM_EXEC_ENV
					  ptr_d(buffer_contents_t, contents))
{
  HB_PTR_PARAM (buffer_contents_t, contents);
  if (unlikely (!contents))
    return;

  module_free (contents->info);
  module_free (contents->pos);

  contents->info = nullref;
  contents->pos = nullref;
  contents->length = 0;
}

HB_WASM_API (bool_t, buffer_copy_contents) (HB_WASM_EXEC_ENV
					    ptr_d(buffer_t, buffer),
					    ptr_d(buffer_contents_t, contents))
{
  HB_REF2OBJ (buffer);
  HB_PTR_PARAM (buffer_contents_t, contents);
  if (unlikely (!contents))
    return false;

  if (buffer->have_output)
    buffer->sync ();
  if (!buffer->have_positions)
    buffer->clear_positions ();

  unsigned length = buffer->len;

  if (length <= contents->length)
  {
    glyph_info_t *info = HB_ARRAY_APP2NATIVE (glyph_info_t, contents->info, length);
    glyph_position_t *pos = HB_ARRAY_APP2NATIVE (glyph_position_t, contents->pos, length);

    if (unlikely (!info || !pos))
    {
      contents->length = 0;
      return false;
    }

    unsigned bytes = length * sizeof (hb_glyph_info_t);
    hb_memcpy (info, buffer->info, bytes);
    hb_memcpy (pos, buffer->pos, bytes);

    return true;
  }

  module_free (contents->info);
  module_free (contents->pos);

  contents->length = length;
  unsigned bytes = length * sizeof (hb_glyph_info_t);
  contents->info = wasm_runtime_module_dup_data (module_inst, (const char *) buffer->info, bytes);
  contents->pos = wasm_runtime_module_dup_data (module_inst, (const char *) buffer->pos, bytes);

  if (length && (!contents->info || !contents->pos))
  {
    contents->length = 0;
    return false;
  }

  return true;
}

HB_WASM_API (bool_t, buffer_set_contents) (HB_WASM_EXEC_ENV
					   ptr_d(buffer_t, buffer),
					   ptr_d(const buffer_contents_t, contents))
{
  HB_REF2OBJ (buffer);
  HB_PTR_PARAM (buffer_contents_t, contents);
  if (unlikely (!contents))
    return false;

  unsigned length = contents->length;
  unsigned bytes;
  if (unlikely (hb_unsigned_mul_overflows (length, sizeof (buffer->info[0]), &bytes)))
    return false;

  glyph_info_t *info = (glyph_info_t *) (validate_app_addr (contents->info, bytes) ? addr_app_to_native (contents->info) : nullptr);
  glyph_position_t *pos = (glyph_position_t *) (validate_app_addr (contents->pos, bytes) ? addr_app_to_native (contents->pos) : nullptr);
  if (unlikely (!info || !pos))
    return false;

  if (unlikely (!buffer->resize (length)))
    return false;

  if (!buffer->have_positions)
    buffer->clear_positions (); /* This is wasteful. */

  hb_memcpy (buffer->info, info, bytes);
  hb_memcpy (buffer->pos, pos, bytes);
  buffer->len = length;

  return true;
}

HB_WASM_API (direction_t, buffer_get_direction) (HB_WASM_EXEC_ENV
						 ptr_d(buffer_t, buffer))
{
  HB_REF2OBJ (buffer);

  return (direction_t) hb_buffer_get_direction (buffer);
}

HB_WASM_API (script_t, buffer_get_script) (HB_WASM_EXEC_ENV
					   ptr_d(buffer_t, buffer))
{
  HB_REF2OBJ (buffer);

  return hb_script_to_iso15924_tag (hb_buffer_get_script (buffer));
}

HB_WASM_API (void, buffer_reverse) (HB_WASM_EXEC_ENV
				    ptr_d(buffer_t, buffer))
{
  HB_REF2OBJ (buffer);

  hb_buffer_reverse (buffer);
}

HB_WASM_API (void, buffer_reverse_clusters) (HB_WASM_EXEC_ENV
					     ptr_d(buffer_t, buffer))
{
  HB_REF2OBJ (buffer);

  hb_buffer_reverse_clusters (buffer);
}

}}

#endif /* HB_WASM_API_BUFFER_HH */
