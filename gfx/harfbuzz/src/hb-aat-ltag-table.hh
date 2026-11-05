/*
 * Copyright © 2018  Ebrahim Byagowi
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

#ifndef HB_AAT_LTAG_TABLE_HH
#define HB_AAT_LTAG_TABLE_HH

#include "hb-open-type.hh"

/*
 * ltag -- Language Tag
 * https://developer.apple.com/fonts/TrueType-Reference-Manual/RM06/Chap6ltag.html
 */
#define HB_AAT_TAG_ltag HB_TAG('l','t','a','g')


namespace AAT {

using namespace OT;


struct FTStringRange
{
  friend struct ltag;

  bool sanitize (hb_sanitize_context_t *c, const void *base) const
  {
    TRACE_SANITIZE (this);
    return_trace (c->check_struct (this) &&
		  hb_barrier () &&
		  (base+tag).sanitize (c, length));
  }

  protected:
  NNOffset16To<UnsizedArrayOf<HBUINT8>>
		tag;		/* Offset from the start of the table to
				 * the beginning of the string */
  HBUINT16	length;		/* String length (in bytes) */
  public:
  DEFINE_SIZE_STATIC (4);
};

struct ltag
{
  static constexpr hb_tag_t tableTag = HB_AAT_TAG_ltag;

  hb_language_t get_language (unsigned int i) const
  {
    const FTStringRange &range = tagRanges[i];
    return hb_language_from_string ((const char *) (this+range.tag).arrayZ,
				    range.length);
  }

  /* Equivalent to hb_language_matches (get_language (i), specific), but
   * without interning the font's tag: hb_language_from_string() appends
   * every new string to a process-wide, linearly-searched, never-freed list,
   * so a hostile font with many tags and many feature records referencing
   * them could make that quadratic. */
  bool matches_language (unsigned int i, hb_language_t specific) const
  {
    const char *s = hb_language_to_string (specific);
    if (!s) return false;

    const FTStringRange &range = tagRanges[i];
    const HBUINT8 *tag = (this+range.tag).arrayZ;
    /* Canonicalize the same way hb_language_from_string() does: at most 63
     * bytes, lowercase, '_' to '-', stop at the first other non-alphanumeric. */
    unsigned len = hb_min ((unsigned) range.length, 63u);
    unsigned l = 0;
    for (; l < len; l++)
    {
      unsigned char c = TOLOWER (tag[l]);
      if (c == '_') c = '-';
      if (!ISALNUM (c) && c != '-') break;
      if (c != (unsigned char) s[l]) return false;
    }
    return l && (s[l] == '\0' || s[l] == '-');
  }

  bool sanitize (hb_sanitize_context_t *c) const
  {
    TRACE_SANITIZE (this);
    return_trace (likely (c->check_struct (this) &&
			  hb_barrier () &&
			  version >= 1 &&
			  tagRanges.sanitize (c, this)));
  }

  protected:
  HBUINT32	version;	/* Table version; currently 1 */
  HBUINT32	flags;		/* Table flags; currently none defined */
  Array32Of<FTStringRange>
		tagRanges;	/* Range for each tag's string */
  public:
  DEFINE_SIZE_ARRAY (12, tagRanges);
};

} /* namespace AAT */


#endif /* HB_AAT_LTAG_TABLE_HH */
