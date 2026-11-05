/*
 * Copyright © 2022  Red Hat, Inc
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
 *
 * Google Author(s): Matthias Clasen
 */

#ifndef HB_CAIRO_UTILS_H
#define HB_CAIRO_UTILS_H

#include "hb.hh"
#include "hb-cairo.h"
#include "hb-vector.hh"


struct hb_cairo_budget_t
{
  /* Cairo serializes glyph-cache population for a scaled font.  Keeping the
   * owner there lets the draw and paint callbacks share one session. */
  int64_t budget = HB_BUDGET_DEFAULT;
  int64_t budget_remaining = HB_BUDGET_GLYPH;
  unsigned int active = 0;

  void enter ()
  {
    if (!active++)
      recharge ();
  }

  void leave ()
  {
    assert (active);
    active--;
  }

  void recharge ()
  {
    budget_remaining = budget == HB_BUDGET_DEFAULT ?
		       HB_BUDGET_GLYPH : budget;
  }
};

struct hb_cairo_context_t
{
  hb_cairo_context_t (cairo_scaled_font_t *scaled_font_,
		      cairo_t *cr_,
		      hb_map_t *color_cache_,
		      hb_cairo_budget_t *budget_) :
    scaled_font (scaled_font_), cr (cr_), color_cache (color_cache_),
    budget (budget_)
  {
    budget->enter ();
  }

  ~hb_cairo_context_t ()
  {
    budget->leave ();
  }

  bool set_budget (int64_t value)
  {
    budget->budget = value;
    budget->recharge ();
    return true;
  }

  int64_t get_budget () const { return budget->budget; }
  int64_t *get_budget_remaining () { return &budget->budget_remaining; }

  bool spend (unsigned int cost, unsigned int mult = 1)
  { return hb_budget_spend (budget->budget_remaining, cost, mult); }

  /* One emitted path segment.  Also counted, so that the compositing
   * operations that cairo later performs with the path can be charged. */
  bool spend_segment ()
  {
    if (unlikely (!spend (HB_BUDGET_1)))
      return false;
    path_segments++;
    return true;
  }

  /* One compositing operation: a fill, a paint, or a composited group.
   * Cairo rasterizes the path being filled and every clip path in force
   * anew for each of them, walking every segment through the scanlines it
   * crosses; with more than one path it first intersects them, which is
   * quadratic in their segments.  Charge accordingly, so that a font
   * cannot pay for a clip outline once and reuse it for thousands of
   * paints.  WEIGHT is the relative cost of a span of the source being
   * composited.  These counters feed the existing budget_remaining; they
   * are not a separate per-callback limit. */
  bool spend_paint (unsigned int weight = HB_BUDGET_512)
  {
    unsigned paths = clip.paths + !!path_segments;
    unsigned segments = clip.segments + path_segments;
    path_segments = 0;
    return spend (1 + segments, hb_max (weight, paths > 1 ? segments : 0u));
  }

  /* The current path becomes a clip, in force until the matching pop_clip. */
  void push_clip ()
  {
    if (unlikely (!clip_stack.push_or_fail (clip)))
      budget->budget_remaining = -1;
    clip.paths += !!path_segments;
    clip.segments += path_segments;
    path_segments = 0;
  }

  void pop_clip ()
  {
    if (likely (clip_stack.length))
      clip = clip_stack.pop ();
  }

  cairo_scaled_font_t *scaled_font;
  cairo_t *cr;
  hb_map_t *color_cache;
  hb_cairo_budget_t *budget;

  /* Segments of the current path, and of the clip paths in force. */
  unsigned path_segments = 0;
  struct clip_t
  {
    unsigned paths = 0;
    unsigned segments = 0;
  } clip;
  hb_vector_t<clip_t> clip_stack;
};

static inline cairo_operator_t
_hb_paint_composite_mode_to_cairo (hb_paint_composite_mode_t mode)
{
  switch (mode)
    {
    case HB_PAINT_COMPOSITE_MODE_CLEAR: return CAIRO_OPERATOR_CLEAR;
    case HB_PAINT_COMPOSITE_MODE_SRC: return CAIRO_OPERATOR_SOURCE;
    case HB_PAINT_COMPOSITE_MODE_DEST: return CAIRO_OPERATOR_DEST;
    case HB_PAINT_COMPOSITE_MODE_SRC_OVER: return CAIRO_OPERATOR_OVER;
    case HB_PAINT_COMPOSITE_MODE_DEST_OVER: return CAIRO_OPERATOR_DEST_OVER;
    case HB_PAINT_COMPOSITE_MODE_SRC_IN: return CAIRO_OPERATOR_IN;
    case HB_PAINT_COMPOSITE_MODE_DEST_IN: return CAIRO_OPERATOR_DEST_IN;
    case HB_PAINT_COMPOSITE_MODE_SRC_OUT: return CAIRO_OPERATOR_OUT;
    case HB_PAINT_COMPOSITE_MODE_DEST_OUT: return CAIRO_OPERATOR_DEST_OUT;
    case HB_PAINT_COMPOSITE_MODE_SRC_ATOP: return CAIRO_OPERATOR_ATOP;
    case HB_PAINT_COMPOSITE_MODE_DEST_ATOP: return CAIRO_OPERATOR_DEST_ATOP;
    case HB_PAINT_COMPOSITE_MODE_XOR: return CAIRO_OPERATOR_XOR;
    case HB_PAINT_COMPOSITE_MODE_PLUS: return CAIRO_OPERATOR_ADD;
    case HB_PAINT_COMPOSITE_MODE_SCREEN: return CAIRO_OPERATOR_SCREEN;
    case HB_PAINT_COMPOSITE_MODE_OVERLAY: return CAIRO_OPERATOR_OVERLAY;
    case HB_PAINT_COMPOSITE_MODE_DARKEN: return CAIRO_OPERATOR_DARKEN;
    case HB_PAINT_COMPOSITE_MODE_LIGHTEN: return CAIRO_OPERATOR_LIGHTEN;
    case HB_PAINT_COMPOSITE_MODE_COLOR_DODGE: return CAIRO_OPERATOR_COLOR_DODGE;
    case HB_PAINT_COMPOSITE_MODE_COLOR_BURN: return CAIRO_OPERATOR_COLOR_BURN;
    case HB_PAINT_COMPOSITE_MODE_HARD_LIGHT: return CAIRO_OPERATOR_HARD_LIGHT;
    case HB_PAINT_COMPOSITE_MODE_SOFT_LIGHT: return CAIRO_OPERATOR_SOFT_LIGHT;
    case HB_PAINT_COMPOSITE_MODE_DIFFERENCE: return CAIRO_OPERATOR_DIFFERENCE;
    case HB_PAINT_COMPOSITE_MODE_EXCLUSION: return CAIRO_OPERATOR_EXCLUSION;
    case HB_PAINT_COMPOSITE_MODE_MULTIPLY: return CAIRO_OPERATOR_MULTIPLY;
    case HB_PAINT_COMPOSITE_MODE_HSL_HUE: return CAIRO_OPERATOR_HSL_HUE;
    case HB_PAINT_COMPOSITE_MODE_HSL_SATURATION: return CAIRO_OPERATOR_HSL_SATURATION;
    case HB_PAINT_COMPOSITE_MODE_HSL_COLOR: return CAIRO_OPERATOR_HSL_COLOR;
    case HB_PAINT_COMPOSITE_MODE_HSL_LUMINOSITY: return CAIRO_OPERATOR_HSL_LUMINOSITY;
    default: return CAIRO_OPERATOR_CLEAR;
    }
}

HB_INTERNAL void
_hb_cairo_set_source_color (hb_cairo_context_t *c,
			    hb_bool_t use_foreground,
			    hb_color_t color);

HB_INTERNAL hb_bool_t
_hb_cairo_paint_glyph_image (hb_cairo_context_t *c,
			     hb_blob_t *blob,
			     unsigned width,
			     unsigned height,
			     hb_tag_t format,
			     float slant,
			     hb_glyph_extents_t *extents);

HB_INTERNAL void
_hb_cairo_paint_linear_gradient (hb_cairo_context_t *c,
				 hb_color_line_t *color_line,
				 float x0, float y0,
				 float x1, float y1,
				 float x2, float y2);

HB_INTERNAL void
_hb_cairo_paint_radial_gradient (hb_cairo_context_t *c,
				 hb_color_line_t *color_line,
				 float x0, float y0, float r0,
				 float x1, float y1, float r1);

HB_INTERNAL void
_hb_cairo_paint_sweep_gradient (hb_cairo_context_t *c,
				hb_color_line_t *color_line,
				float x0, float y0,
				float start_angle, float end_angle);


#endif /* HB_CAIRO_UTILS_H */
