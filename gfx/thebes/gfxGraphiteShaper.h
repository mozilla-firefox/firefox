/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_GRAPHITESHAPER_H
#define GFX_GRAPHITESHAPER_H

#include "ThebesRLBoxTypes.h"
#include "gfxFont.h"
#include "nsTHashSet.h"

struct gr_face;
struct gr_font;
struct gr_segment;

class gfxGraphiteShaper : public gfxFontShaper {
 public:
  explicit gfxGraphiteShaper(gfxFont* aFont);
  virtual ~gfxGraphiteShaper();

  bool ShapeText(const void* aText, TextEncoding aEncoding, uint32_t aOffset,
                 uint32_t aLength, const ShapingParams& aParams,
                 gfxShapedText* aShapedText) override;

  static void Shutdown();

 protected:
  nsresult SetGlyphsFromSegment(gfxShapedText* aShapedText, uint32_t aOffset,
                                uint32_t aLength, const char16_t* aText,
                                tainted_opaque_gr<char16_t*> t_aText,
                                tainted_opaque_gr<gr_segment*> aSegment,
                                RoundingFlags aRounding);

  // Graphite is run in a rlbox sandbox. Callback GrGetAdvance must be
  // explicitly permitted. Since the sandbox is owned in gfxFontEntry class,
  // gfxFontEntry needs access to the protected callback.
  friend class gfxFontEntryCallbacks;
  static tainted_opaque_gr<float> GrGetAdvance(
      rlbox_sandbox_gr& sandbox, tainted_opaque_gr<const void*> appFontHandle,
      tainted_opaque_gr<uint16_t> glyphid);

  tainted_opaque_gr<gr_face*>
      mGrFace;  // owned by the font entry; shaper must call
                // gfxFontEntry::ReleaseGrFace when finished with it
  tainted_opaque_gr<gr_font*> mGrFont;  // owned by the shaper itself

  // All libGraphite functionality is sandboxed. This is the sandbox instance.
  rlbox_sandbox_gr* mSandbox;

  // Holds the handle to the permitted callback into Firefox for the sandboxed
  // libGraphite
  sandbox_callback_gr<float (*)(const void*, uint16_t)>* mCallback;

  struct CallbackData {
    // mFont is a pointer to the font that owns this shaper, so it will
    // remain valid throughout our lifetime
    gfxFont* MOZ_NON_OWNING_REF mFont;
  };

  CallbackData mCallbackData;
  static thread_local CallbackData* tl_GrGetAdvanceData;

  bool mFallbackToSmallCaps;  // special fallback for the petite-caps case

  // Convert HTML 'lang' (BCP47) to Graphite language code
  static uint32_t GetGraphiteTagForLang(const nsCString& aLang);
  static nsTHashSet<uint32_t>* sLanguageTags;
};

#endif /* GFX_GRAPHITESHAPER_H */
