/* -*- Mode: c; tab-width: 8; c-basic-offset: 4; indent-tabs-mode: t; -*- */
/* Cairo - a vector graphics library with display and print output
 *
 * Copyright © 2010 Mozilla Foundation
 *
 * This library is free software; you can redistribute it and/or
 * modify it either under the terms of the GNU Lesser General Public
 * License version 2.1 as published by the Free Software Foundation
 * (the "LGPL") or, at your option, under the terms of the Mozilla
 * Public License Version 1.1 (the "MPL"). If you do not alter this
 * notice, a recipient may use your version of this file under either
 * the MPL or the LGPL.
 *
 * You should have received a copy of the LGPL along with this library
 * in the file COPYING-LGPL-2.1; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 * You should have received a copy of the MPL along with this library
 * in the file COPYING-MPL-1.1
 *
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY
 * OF ANY KIND, either express or implied. See the LGPL or the MPL for
 * the specific language governing rights and limitations.
 *
 * The Original Code is the cairo graphics library.
 *
 * The Initial Developer of the Original Code is the Mozilla Foundation
 *
 * Contributor(s):
 *	Bas Schouten <bschouten@mozilla.com>
 */

#include "cairoint.h"

#include "cairo-win32-private.h"
#include "cairo-pattern-private.h"
#include "cairo-surface-private.h"
#include "cairo-image-surface-private.h"
#include "cairo-clip-private.h"
#include "cairo-win32-refptr.hpp"
#include "cairo-dwrite-private.hpp"
#include "cairo-truetype-subset-private.h"
#include "cairo-scaled-font-subsets-private.h"
#include "cairo-dwrite.h"

#include <utility>
#include <float.h>

#include <wincodec.h>

/**
 * SECTION:cairo-dwrite-fonts
 * @Title: DWrite Fonts
 * @Short_Description: Font support for Microsoft DirectWrite
 * @See_Also: #cairo_font_face_t
 *
 * The Microsoft DirectWrite font backend is primarily used to render text on
 * Microsoft Windows systems.
 **/

/**
 * CAIRO_HAS_DWRITE_FONT:
 *
 * Defined if the Microsoft DWrite font backend is available.
 * This macro can be used to conditionally compile backend-specific code.
 *
 * Since: 1.18
 **/

typedef HRESULT (WINAPI*D2D1CreateFactoryFunc)(
    D2D1_FACTORY_TYPE factoryType,
    REFIID iid,
    CONST D2D1_FACTORY_OPTIONS *pFactoryOptions,
    void **factory
);

#define CAIRO_INT_STATUS_SUCCESS (cairo_int_status_t)CAIRO_STATUS_SUCCESS

// Forward declarations
cairo_int_status_t
_dwrite_draw_glyphs_to_gdi_surface_d2d(cairo_win32_surface_t *surface,
				       DWRITE_MATRIX *transform,
				       DWRITE_GLYPH_RUN *run,
				       COLORREF color,
				       const RECT &area);

static cairo_int_status_t
_dwrite_draw_glyphs_to_gdi_surface_gdi(cairo_win32_surface_t *surface,
				       DWRITE_MATRIX *transform,
				       DWRITE_GLYPH_RUN *run,
				       COLORREF color,
				       cairo_dwrite_scaled_font_t *scaled_font,
				       const RECT &area);

/**
 * _cairo_dwrite_error:
 * @hr HRESULT code
 * @context: context string to display along with the error
 *
 * Helper function to print a human readable form a HRESULT.
 *
 * Return value: A cairo status code for the error code
 **/
static cairo_int_status_t
_cairo_dwrite_error (HRESULT hr, const char *context)
{
    void *lpMsgBuf;

    if (!FormatMessageW (FORMAT_MESSAGE_ALLOCATE_BUFFER |
                         FORMAT_MESSAGE_IGNORE_INSERTS |
			 FORMAT_MESSAGE_FROM_SYSTEM,
			 NULL,
			 hr,
			 MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT),
			 (LPWSTR) &lpMsgBuf,
			 0, NULL)) {
	fprintf (stderr, "%s: Unknown DWrite error HRESULT=0x%08lx\n", context, (unsigned long)hr);
    } else {
	fprintf (stderr, "%s: %S\n", context, (wchar_t *)lpMsgBuf);
	LocalFree (lpMsgBuf);
    }
    fflush (stderr);

    return (cairo_int_status_t)_cairo_error (CAIRO_STATUS_DWRITE_ERROR);
}

class D2DFactory
{
public:
    static RefPtr<ID2D1Factory> Instance()
    {
	if (!mFactoryInstance) {
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
            HMODULE d2d1 = _cairo_win32_load_library_from_system32 (L"d2d1.dll");
	    D2D1CreateFactoryFunc createD2DFactory = (D2D1CreateFactoryFunc)
                GetProcAddress(d2d1, "D2D1CreateFactory");
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
	    if (createD2DFactory) {
		D2D1_FACTORY_OPTIONS options;
		options.debugLevel = D2D1_DEBUG_LEVEL_NONE;
		createD2DFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
				 __uuidof(ID2D1Factory),
				 &options,
				 (void**)&mFactoryInstance);
	    }
	}
	return mFactoryInstance;
    }

    static RefPtr<IDWriteFactory4> Instance4()
    {
	if (!mFactoryInstance4) {
	    if (Instance()) {
		Instance()->QueryInterface(&mFactoryInstance4);
	    }
	}
	return mFactoryInstance4;
    }

    static RefPtr<ID2D1DCRenderTarget> RenderTarget()
    {
	if (!mRenderTarget) {
	    if (!Instance()) {
		return NULL;
	    }
	    // Create a DC render target.
	    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
		D2D1_RENDER_TARGET_TYPE_DEFAULT,
		D2D1::PixelFormat(
		    DXGI_FORMAT_B8G8R8A8_UNORM,
		    D2D1_ALPHA_MODE_PREMULTIPLIED),
		0,
		0,
		D2D1_RENDER_TARGET_USAGE_NONE,
		D2D1_FEATURE_LEVEL_DEFAULT
		);

	    Instance()->CreateDCRenderTarget(&props, &mRenderTarget);
	}
	return mRenderTarget;
    }

private:
    static RefPtr<ID2D1Factory> mFactoryInstance;
    static RefPtr<IDWriteFactory4> mFactoryInstance4;
    static RefPtr<ID2D1DCRenderTarget> mRenderTarget;
};

class WICImagingFactory
{
public:
    static RefPtr<IWICImagingFactory> Instance()
    {
	if (!mFactoryInstance) {
	    CoInitialize(NULL);
	    CoCreateInstance(CLSID_WICImagingFactory,
			     NULL,
			     CLSCTX_INPROC_SERVER,
			     IID_PPV_ARGS(&mFactoryInstance));
	}
	return mFactoryInstance;
    }
private:
    static RefPtr<IWICImagingFactory> mFactoryInstance;
};


RefPtr<IDWriteFactory> DWriteFactory::mFactoryInstance;
RefPtr<IDWriteFactory1> DWriteFactory::mFactoryInstance1;
RefPtr<IDWriteFactory2> DWriteFactory::mFactoryInstance2;
RefPtr<IDWriteFactory3> DWriteFactory::mFactoryInstance3;
RefPtr<IDWriteFactory4> DWriteFactory::mFactoryInstance4;

RefPtr<IWICImagingFactory> WICImagingFactory::mFactoryInstance;
RefPtr<IDWriteFontCollection> DWriteFactory::mSystemCollection;
RefPtr<IDWriteRenderingParams> DWriteFactory::mDefaultRenderingParams;

RefPtr<ID2D1Factory> D2DFactory::mFactoryInstance;
RefPtr<ID2D1DCRenderTarget> D2DFactory::mRenderTarget;

static RefPtr<IDWriteRenderingParams>
_create_rendering_params(IDWriteRenderingParams     *params,
			 const cairo_font_options_t *options,
			 cairo_antialias_t           antialias)
{
    if (!params)
	params = DWriteFactory::DefaultRenderingParams();
    FLOAT gamma = params->GetGamma();
    FLOAT enhanced_contrast = params->GetEnhancedContrast();
    FLOAT clear_type_level = params->GetClearTypeLevel();
    DWRITE_PIXEL_GEOMETRY pixel_geometry = params->GetPixelGeometry();
    DWRITE_RENDERING_MODE rendering_mode = params->GetRenderingMode();

    cairo_bool_t modified = FALSE;
    switch (antialias) {
    case CAIRO_ANTIALIAS_NONE:
	if (rendering_mode != DWRITE_RENDERING_MODE_ALIASED) {
	    rendering_mode = DWRITE_RENDERING_MODE_ALIASED;
	    modified = TRUE;
	}
	break;
    case CAIRO_ANTIALIAS_FAST:
    case CAIRO_ANTIALIAS_GRAY:
	if (clear_type_level) {
	    clear_type_level = 0;
	    modified = TRUE;
	}
	break;
    default:
	break;
    }
    auto subpixel_order = cairo_font_options_get_subpixel_order (options);
    switch (subpixel_order) {
    case CAIRO_SUBPIXEL_ORDER_RGB:
	if (pixel_geometry != DWRITE_PIXEL_GEOMETRY_RGB) {
	    pixel_geometry = DWRITE_PIXEL_GEOMETRY_RGB;
	    modified = TRUE;
	}
	break;
    case CAIRO_SUBPIXEL_ORDER_BGR:
	if (pixel_geometry != DWRITE_PIXEL_GEOMETRY_BGR) {
	    pixel_geometry = DWRITE_PIXEL_GEOMETRY_BGR;
	    modified = TRUE;
	}
	break;
    default:
	break;
    }	
    if (!modified)
	return params;

    HRESULT hr;
    RefPtr<IDWriteRenderingParams1> params1;
    hr = params->QueryInterface(&params1);
    if (FAILED(hr)) {
	RefPtr<IDWriteRenderingParams> ret;
	DWriteFactory::Instance()->CreateCustomRenderingParams(gamma, enhanced_contrast, clear_type_level, pixel_geometry, rendering_mode, &ret);
	return ret;
    }

    FLOAT grayscaleEnhancedContrast = params1->GetGrayscaleEnhancedContrast();
    RefPtr<IDWriteRenderingParams2> params2;
    hr = params->QueryInterface(&params2);
    if (FAILED(hr)) {
	RefPtr<IDWriteRenderingParams1> ret;
	DWriteFactory::Instance1()->CreateCustomRenderingParams(gamma, enhanced_contrast, grayscaleEnhancedContrast, clear_type_level, pixel_geometry, rendering_mode, &ret);
	return ret;
    }    

    DWRITE_GRID_FIT_MODE gridFitMode = params2->GetGridFitMode();
    RefPtr<IDWriteRenderingParams3> params3;
    hr = params->QueryInterface(&params3);
    if (FAILED(hr)) {
	RefPtr<IDWriteRenderingParams2> ret;
	DWriteFactory::Instance2()->CreateCustomRenderingParams(gamma, enhanced_contrast, grayscaleEnhancedContrast, clear_type_level, pixel_geometry, rendering_mode, gridFitMode, &ret);
	return ret;
    }    
    
    DWRITE_RENDERING_MODE1 rendering_mode1 = params3->GetRenderingMode1();
    if (antialias == CAIRO_ANTIALIAS_NONE)
	rendering_mode1 = DWRITE_RENDERING_MODE1_ALIASED;
    RefPtr<IDWriteRenderingParams3> ret;
    DWriteFactory::Instance3()->CreateCustomRenderingParams(gamma, enhanced_contrast, grayscaleEnhancedContrast, clear_type_level, pixel_geometry, rendering_mode1, gridFitMode, &ret);
    return ret;
}

/* Functions #cairo_font_face_backend_t */
static cairo_status_t
_cairo_dwrite_font_face_create_for_toy (cairo_toy_font_face_t   *toy_face,
					cairo_font_face_t      **font_face);
static cairo_bool_t
_cairo_dwrite_font_face_destroy (void *font_face);

static cairo_status_t
_cairo_dwrite_font_face_scaled_font_create (void			*abstract_face,
					    const cairo_matrix_t	*font_matrix,
					    const cairo_matrix_t	*ctm,
					    const cairo_font_options_t *options,
					    cairo_scaled_font_t **font);

const cairo_font_face_backend_t _cairo_dwrite_font_face_backend = {
    CAIRO_FONT_TYPE_DWRITE,
    _cairo_dwrite_font_face_create_for_toy,
    _cairo_dwrite_font_face_destroy,
    _cairo_dwrite_font_face_scaled_font_create
};

/* Functions #cairo_scaled_font_backend_t */

static void _cairo_dwrite_scaled_font_fini(void *scaled_font);

static cairo_warn cairo_int_status_t
_cairo_dwrite_scaled_glyph_init(void			     *scaled_font,
				cairo_scaled_glyph_t	     *scaled_glyph,
				cairo_scaled_glyph_info_t     info,
				const cairo_color_t          *foreground_color);

static cairo_int_status_t
_cairo_dwrite_load_truetype_table(void		       *scaled_font,
				  unsigned long         tag,
				  long                  offset,
				  unsigned char        *buffer,
				  unsigned long        *length);

static unsigned long
_cairo_dwrite_ucs4_to_index(void		       *scaled_font,
			    uint32_t                    ucs4);

static cairo_int_status_t
_cairo_dwrite_is_synthetic(void                       *scaled_font,
			   cairo_bool_t               *is_synthetic);

static cairo_bool_t
_cairo_dwrite_has_color_glyphs(void *scaled_font);

static const cairo_scaled_font_backend_t _cairo_dwrite_scaled_font_backend = {
    CAIRO_FONT_TYPE_DWRITE,
    _cairo_dwrite_scaled_font_fini,
    _cairo_dwrite_scaled_glyph_init,
    NULL, /* text_to_glyphs */
    _cairo_dwrite_ucs4_to_index,
    _cairo_dwrite_load_truetype_table,
    NULL, /* index_to_ucs4 */
    _cairo_dwrite_is_synthetic,
    NULL, /* index_to_glyph_name */
    NULL, /* load_type1_data */
    _cairo_dwrite_has_color_glyphs
};

/* Helper conversion functions */


/**
 * _cairo_dwrite_matrix_from_matrix:
 * Get a DirectWrite matrix from a cairo matrix. Note that DirectWrite uses row
 * vectors where cairo uses column vectors. Hence the transposition.
 *
 * \param Cairo matrix
 * \return DirectWrite matrix
 **/
static DWRITE_MATRIX
_cairo_dwrite_matrix_from_matrix(const cairo_matrix_t *matrix)
{
    DWRITE_MATRIX dwmat;
    dwmat.m11 = (FLOAT)matrix->xx;
    dwmat.m12 = (FLOAT)matrix->yx;
    dwmat.m21 = (FLOAT)matrix->xy;
    dwmat.m22 = (FLOAT)matrix->yy;
    dwmat.dx = (FLOAT)matrix->x0;
    dwmat.dy = (FLOAT)matrix->y0;
    return dwmat;
}

/* Helper functions for cairo_dwrite_scaled_glyph_init() */
static cairo_int_status_t
_cairo_dwrite_scaled_font_init_glyph_metrics
    (cairo_dwrite_scaled_font_t *scaled_font, cairo_scaled_glyph_t *scaled_glyph);

static cairo_int_status_t
_cairo_dwrite_scaled_font_init_glyph_color_surface(cairo_dwrite_scaled_font_t *scaled_font,
						   cairo_scaled_glyph_t	      *scaled_glyph,
						   const cairo_color_t        *foreground_color);

static cairo_int_status_t
_cairo_dwrite_scaled_font_init_glyph_surface
    (cairo_dwrite_scaled_font_t *scaled_font, cairo_scaled_glyph_t *scaled_glyph);

static cairo_int_status_t
_cairo_dwrite_scaled_font_init_glyph_path
    (cairo_dwrite_scaled_font_t *scaled_font, cairo_scaled_glyph_t *scaled_glyph);

/* implement the font backend interface */

static cairo_status_t
_cairo_dwrite_font_face_create_for_toy (cairo_toy_font_face_t   *toy_face,
					cairo_font_face_t      **font_face)
{
    WCHAR *face_name;
    int face_name_len;

    if (!DWriteFactory::Instance()) {
	return (cairo_status_t)CAIRO_INT_STATUS_UNSUPPORTED;
    }

    face_name_len = MultiByteToWideChar(CP_UTF8, 0, toy_face->family, -1, NULL, 0);
    face_name = new WCHAR[face_name_len];
    MultiByteToWideChar(CP_UTF8, 0, toy_face->family, -1, face_name, face_name_len);

    RefPtr<IDWriteFontFamily> family = DWriteFactory::FindSystemFontFamily(face_name);
    delete[] face_name;
    if (!family) {
	/* If the family is not found, use the default that should always exist. */
	face_name_len = MultiByteToWideChar(CP_UTF8, 0, CAIRO_FONT_FAMILY_DEFAULT, -1, NULL, 0);
	face_name = new WCHAR[face_name_len];
	MultiByteToWideChar(CP_UTF8, 0, CAIRO_FONT_FAMILY_DEFAULT, -1, face_name, face_name_len);

	family = DWriteFactory::FindSystemFontFamily(face_name);
	delete[] face_name;
	if (!family) {
	    *font_face = (cairo_font_face_t*)&_cairo_font_face_nil;
	    return (cairo_status_t)CAIRO_INT_STATUS_UNSUPPORTED;
	}
    }

    DWRITE_FONT_WEIGHT weight;
    switch (toy_face->weight) {
    case CAIRO_FONT_WEIGHT_BOLD:
	weight = DWRITE_FONT_WEIGHT_BOLD;
	break;
    case CAIRO_FONT_WEIGHT_NORMAL:
    default:
	weight = DWRITE_FONT_WEIGHT_NORMAL;
	break;
    }

    DWRITE_FONT_STYLE style;
    switch (toy_face->slant) {
    case CAIRO_FONT_SLANT_ITALIC:
	style = DWRITE_FONT_STYLE_ITALIC;
	break;
    case CAIRO_FONT_SLANT_OBLIQUE:
	style = DWRITE_FONT_STYLE_OBLIQUE;
	break;
    case CAIRO_FONT_SLANT_NORMAL:
    default:
	style = DWRITE_FONT_STYLE_NORMAL;
	break;
    }

    RefPtr<IDWriteFont> font;
    HRESULT hr = family->GetFirstMatchingFont(weight, DWRITE_FONT_STRETCH_NORMAL, style, &font);
    if (FAILED(hr))
	return (cairo_status_t)_cairo_dwrite_error (hr, "GetFirstMatchingFont failed");

    RefPtr<IDWriteFontFace> dwriteface;
    hr = font->CreateFontFace(&dwriteface);
    if (FAILED(hr))
	return (cairo_status_t)_cairo_dwrite_error (hr, "CreateFontFace failed");

    *font_face = cairo_dwrite_font_face_create_for_dwrite_fontface(dwriteface);
    return CAIRO_STATUS_SUCCESS;
}

static cairo_bool_t
_cairo_dwrite_font_face_destroy (void *font_face)
{
    cairo_dwrite_font_face_t *dwrite_font_face = static_cast<cairo_dwrite_font_face_t*>(font_face);
    if (dwrite_font_face->dwriteface)
        dwrite_font_face->dwriteface->Release();
    if (dwrite_font_face->rendering_params)
	dwrite_font_face->rendering_params->Release();
    return TRUE;
}

static inline unsigned short
read_short(const char *buf)
{
    return be16_to_cpu(*(unsigned short*)buf);
}

void
_cairo_dwrite_glyph_run_from_glyphs(cairo_glyph_t *glyphs,
				    int num_glyphs,
				    cairo_dwrite_scaled_font_t *scaled_font,
				    AutoDWriteGlyphRun *run,
				    cairo_bool_t *transformed)
{
    run->allocate(num_glyphs);

    UINT16 *indices = const_cast<UINT16*>(run->glyphIndices);
    FLOAT *advances = const_cast<FLOAT*>(run->glyphAdvances);
    DWRITE_GLYPH_OFFSET *offsets = const_cast<DWRITE_GLYPH_OFFSET*>(run->glyphOffsets);

    run->bidiLevel = 0;
    run->fontFace = scaled_font->dwriteface;
    run->glyphCount = num_glyphs;
    run->isSideways = FALSE;

    if (scaled_font->mat.xy == 0 && scaled_font->mat.yx == 0 &&
	scaled_font->mat.xx == scaled_font->base.font_matrix.xx &&
	scaled_font->mat.yy == scaled_font->base.font_matrix.yy) {
	// Fast route, don't actually use a transform but just
	// set the correct font size.
	*transformed = 0;

	run->fontEmSize = (FLOAT)scaled_font->base.font_matrix.yy;

	for (int i = 0; i < num_glyphs; i++) {
	    indices[i] = (WORD) glyphs[i].index;

	    offsets[i].ascenderOffset = -(FLOAT)(glyphs[i].y);
	    offsets[i].advanceOffset = (FLOAT)(glyphs[i].x);
	    advances[i] = 0.0;
	}
    } else {
	*transformed = 1;
        // Transforming positions by the inverse matrix, then by the original
        // matrix later may introduce small errors, especially because the
        // D2D matrix is single-precision whereas the cairo one is double.
        // This is a problem when glyph positions were originally at exactly
        // half-pixel locations, which eventually round to whole pixels for
        // GDI rendering - the errors introduced here cause them to round in
        // unpredictable directions, instead of all rounding in a consistent
        // way, leading to poor glyph spacing (bug 675383).
        // To mitigate this, nudge the positions by a tiny amount to try and
        // ensure that after the two transforms, they'll still round in a
        // consistent direction.
        const double EPSILON = 0.0001;
	for (int i = 0; i < num_glyphs; i++) {
	    indices[i] = (WORD) glyphs[i].index;
	    double x = glyphs[i].x + EPSILON;
	    double y = glyphs[i].y;
	    cairo_matrix_transform_point(&scaled_font->mat_inverse, &x, &y);
	    // Since we will multiply by our ctm matrix later for rotation effects
	    // and such, adjust positions by the inverse matrix now. Y-axis is
	    // inverted! Therefor the offset is -y.
	    offsets[i].ascenderOffset = -(FLOAT)y;
	    offsets[i].advanceOffset = (FLOAT)x;
	    advances[i] = 0.0;
	}
	// The font matrix takes care of the scaling if we have a transform,
	// emSize should be 1.
	run->fontEmSize = 1.0f;
    }
}

#define GASP_TAG 0x70736167
#define GASP_DOGRAY 0x2

static cairo_bool_t
do_grayscale(IDWriteFontFace *dwface, unsigned int ppem)
{
    void *tableContext;
    char *tableData;
    UINT32 tableSize;
    BOOL exists;
    dwface->TryGetFontTable(GASP_TAG, (const void**)&tableData, &tableSize, &tableContext, &exists);

    if (exists) {
	if (tableSize < 4) {
	    dwface->ReleaseFontTable(tableContext);
	    return true;
	}
	struct gaspRange {
	    unsigned short maxPPEM; // Stored big-endian
	    unsigned short behavior; // Stored big-endian
	};
	unsigned short numRanges = read_short(tableData + 2);
	if (tableSize < (UINT)4 + numRanges * 4) {
	    dwface->ReleaseFontTable(tableContext);
	    return true;
	}
	gaspRange *ranges = (gaspRange *)(tableData + 4);
	for (int i = 0; i < numRanges; i++) {
	    if (be16_to_cpu(ranges[i].maxPPEM) > ppem) {
		if (!(be16_to_cpu(ranges[i].behavior) & GASP_DOGRAY)) {
		    dwface->ReleaseFontTable(tableContext);
		    return false;
		}
		break;
	    }
	}
	dwface->ReleaseFontTable(tableContext);
    }
    return true;
}

static void
set_dwrite_axes_from_string (const char             *string,
                             DWRITE_FONT_AXIS_VALUE *dwrite_axes,
                             UINT32                  count)
{
    const char *p = string;

    while (p && *p) {
        const char *start;
        const char *end, *end2;
        DWRITE_FONT_AXIS_TAG tag;
        double value;

        while (_cairo_isspace (*p)) p++;

        start = p;
        end = strchr (p, ',');
        if (end && (end - p < 6))
            goto skip;

        tag = DWRITE_MAKE_FONT_AXIS_TAG (p[0], p[1], p[2], p[3]);

        p += 4;
        while (_cairo_isspace (*p)) p++;
        if (*p == '=') p++;

        if (p - start < 5)
            goto skip;

        value = _cairo_strtod (p, (char **) &end2);

        while (end2 && _cairo_isspace (*end2)) end2++;

        if (end2 && (*end2 != ',' && *end2 != '\0'))
            goto skip;

        for (UINT32 i = 0; i < count; i++) {
            if (dwrite_axes[i].axisTag == tag) {
                dwrite_axes[i].value = value;
                break;
            }
        }

skip:
        p = end ? end + 1 : NULL;
    }
}

static cairo_status_t
_cairo_dwrite_font_face_scaled_font_create (void			*abstract_face,
					    const cairo_matrix_t	*font_matrix,
					    const cairo_matrix_t	*ctm,
					    const cairo_font_options_t  *options,
					    cairo_scaled_font_t **font)
{
    cairo_status_t status;
    cairo_dwrite_font_face_t *font_face = static_cast<cairo_dwrite_font_face_t*>(abstract_face);

    /* Must do malloc and not C++ new, since Cairo frees this. */
    cairo_dwrite_scaled_font_t *dwrite_font = (cairo_dwrite_scaled_font_t*)_cairo_calloc(
	sizeof(cairo_dwrite_scaled_font_t));
    if (unlikely(dwrite_font == NULL))
	return _cairo_error (CAIRO_STATUS_NO_MEMORY);

    *font = reinterpret_cast<cairo_scaled_font_t*>(dwrite_font);
    status = _cairo_scaled_font_init (&dwrite_font->base,
				      &font_face->base,
				      font_matrix,
				      ctm,
				      options,
				      &_cairo_dwrite_scaled_font_backend);
    if (status) {
	free(dwrite_font);
	return status;
    }

    if (options->variations) {
        RefPtr<IDWriteFontFace5> dwriteface5;

        /* Since Windows 10 20348 */
        if (SUCCEEDED (font_face->dwriteface->QueryInterface(&dwriteface5))) {
            RefPtr<IDWriteFontResource> dwritefontresource;

            if (dwriteface5->HasVariations () &&
                SUCCEEDED (dwriteface5->GetFontResource (&dwritefontresource)))
            {
                UINT32 count = MIN (dwriteface5->GetFontAxisValueCount (), 500);
                DWRITE_FONT_AXIS_VALUE *dwrite_axes = new DWRITE_FONT_AXIS_VALUE[count];
                UINT32 variables_count = 0;

                /* Sort variable axes first */
                for (UINT32 i = 0; i < count; i++) {
                    if (dwritefontresource->GetFontAxisAttributes (i) & DWRITE_FONT_AXIS_ATTRIBUTES_VARIABLE) {
                       if (variables_count != i) {
                           DWRITE_FONT_AXIS_VALUE swap_aux = dwrite_axes[variables_count];
                           dwrite_axes[variables_count] = dwrite_axes[i];
                           dwrite_axes[i] = swap_aux;
                       }

                       variables_count++;
                    }
                }

                if (SUCCEEDED (dwriteface5->GetFontAxisValues(dwrite_axes, count))) {
                    RefPtr<IDWriteFontFace5> dwriteface_new5;

                    set_dwrite_axes_from_string (options->variations, dwrite_axes, variables_count);

                    /* Can't use constexpr with mingw-w64 headers */
                    const DWRITE_FONT_SIMULATIONS all_simulations = DWRITE_FONT_SIMULATIONS_BOLD |
                                                                    DWRITE_FONT_SIMULATIONS_OBLIQUE;
                    if (SUCCEEDED (dwritefontresource->CreateFontFace(all_simulations,
                                                                      dwrite_axes,
                                                                      count,
                                                                      &dwriteface_new5)))
                    {
                        dwrite_font->dwriteface = dwriteface_new5.forget().drop();
                    }
                }

                delete[] dwrite_axes;
            }
        }
    }

    if (!dwrite_font->dwriteface) {
        font_face->dwriteface->AddRef ();
        dwrite_font->dwriteface = font_face->dwriteface;
    }

    dwrite_font->mat = dwrite_font->base.ctm;
    cairo_matrix_multiply(&dwrite_font->mat, &dwrite_font->mat, font_matrix);
    dwrite_font->mat_inverse = dwrite_font->mat;
    cairo_matrix_invert (&dwrite_font->mat_inverse);

    cairo_font_extents_t extents;

    DWRITE_FONT_METRICS metrics;
    if (dwrite_font->measuring_mode == DWRITE_MEASURING_MODE_GDI_CLASSIC ||
	dwrite_font->measuring_mode == DWRITE_MEASURING_MODE_GDI_NATURAL) {
	DWRITE_MATRIX transform = _cairo_dwrite_matrix_from_matrix (&dwrite_font->mat);
        dwrite_font->dwriteface->GetGdiCompatibleMetrics(1, 1, &transform, &metrics);
    } else {
        dwrite_font->dwriteface->GetMetrics(&metrics);
    }

    extents.ascent = (FLOAT)metrics.ascent / metrics.designUnitsPerEm;
    extents.descent = (FLOAT)metrics.descent / metrics.designUnitsPerEm;
    extents.height = (FLOAT)(metrics.ascent + metrics.descent + metrics.lineGap) / metrics.designUnitsPerEm;
    extents.max_x_advance = 14.0;
    extents.max_y_advance = 0.0;

    cairo_antialias_t default_quality = CAIRO_ANTIALIAS_SUBPIXEL;

    // The following code detects the system quality at scaled_font creation time,
    // this means that if cleartype settings are changed but the scaled_fonts
    // are re-used, they might not adhere to the new system setting until re-
    // creation.
    switch (cairo_win32_get_system_text_quality()) {
	case CLEARTYPE_QUALITY:
	    default_quality = CAIRO_ANTIALIAS_SUBPIXEL;
	    break;
	case ANTIALIASED_QUALITY:
	    default_quality = CAIRO_ANTIALIAS_GRAY;
	    break;
	case DEFAULT_QUALITY:
	    // _get_system_quality() seems to think aliased is default!
	    default_quality = CAIRO_ANTIALIAS_NONE;
	    break;
    }

    if (default_quality == CAIRO_ANTIALIAS_GRAY) {
        if (!do_grayscale(dwrite_font->dwriteface, (unsigned int)_cairo_round(font_matrix->yy))) {
	    default_quality = CAIRO_ANTIALIAS_NONE;
	}
    }

    if (options->antialias == CAIRO_ANTIALIAS_DEFAULT) {
	dwrite_font->antialias_mode = default_quality;
    } else {
	dwrite_font->antialias_mode = options->antialias;
    }

    dwrite_font->rendering_params = _create_rendering_params(font_face->rendering_params, options, dwrite_font->antialias_mode).forget().drop();
    dwrite_font->measuring_mode = font_face->measuring_mode;

    return _cairo_scaled_font_set_metrics (*font, &extents);
}

/* Implementation #cairo_dwrite_scaled_font_backend_t */
static void
_cairo_dwrite_scaled_font_fini(void *scaled_font)
{
    cairo_dwrite_scaled_font_t *dwrite_font = static_cast<cairo_dwrite_scaled_font_t*>(scaled_font);

    dwrite_font->dwriteface->Release ();

    if (dwrite_font->rendering_params)
	dwrite_font->rendering_params->Release();
}

static cairo_int_status_t
_cairo_dwrite_scaled_glyph_init(void			     *scaled_font,
				cairo_scaled_glyph_t	     *scaled_glyph,
				cairo_scaled_glyph_info_t     info,
				const cairo_color_t          *foreground_color)
{
    cairo_dwrite_scaled_font_t *scaled_dwrite_font = static_cast<cairo_dwrite_scaled_font_t*>(scaled_font);
    cairo_int_status_t status = CAIRO_INT_STATUS_UNSUPPORTED;

    if ((info & CAIRO_SCALED_GLYPH_INFO_METRICS) != 0) {
	status = _cairo_dwrite_scaled_font_init_glyph_metrics (scaled_dwrite_font, scaled_glyph);
	if (status)
	    return status;
    }

    if (info & CAIRO_SCALED_GLYPH_INFO_COLOR_SURFACE) {
	status = _cairo_dwrite_scaled_font_init_glyph_color_surface (scaled_dwrite_font, scaled_glyph, foreground_color);
	if (status)
	    return status;
    }

    if (info & CAIRO_SCALED_GLYPH_INFO_SURFACE) {
	status = _cairo_dwrite_scaled_font_init_glyph_surface (scaled_dwrite_font, scaled_glyph);
	if (status)
	    return status;
    }

    if ((info & CAIRO_SCALED_GLYPH_INFO_PATH) != 0) {
	status = _cairo_dwrite_scaled_font_init_glyph_path (scaled_dwrite_font, scaled_glyph);
	if (status)
	    return status;
    }

    return CAIRO_INT_STATUS_SUCCESS;
}

static unsigned long
_cairo_dwrite_ucs4_to_index(void			     *scaled_font,
			    uint32_t		      ucs4)
{
    cairo_dwrite_scaled_font_t *dwritesf = static_cast<cairo_dwrite_scaled_font_t*>(scaled_font);
    UINT16 index = 0;

    dwritesf->dwriteface->GetGlyphIndicesA(&ucs4, 1, &index);
    return index;
}

/* cairo_dwrite_scaled_glyph_init() helper function bodies */
static cairo_int_status_t
_cairo_dwrite_scaled_font_init_glyph_metrics(cairo_dwrite_scaled_font_t *scaled_font,
					     cairo_scaled_glyph_t *scaled_glyph)
{
    UINT16 charIndex = (UINT16)_cairo_scaled_glyph_index (scaled_glyph);
    cairo_dwrite_font_face_t *font_face = (cairo_dwrite_font_face_t*)scaled_font->base.font_face;
    cairo_text_extents_t extents;

    DWRITE_GLYPH_METRICS metrics;
    DWRITE_FONT_METRICS fontMetrics;
    HRESULT hr;
    if (font_face->measuring_mode == DWRITE_MEASURING_MODE_GDI_CLASSIC ||
	font_face->measuring_mode == DWRITE_MEASURING_MODE_GDI_NATURAL) {
	DWRITE_MATRIX transform = _cairo_dwrite_matrix_from_matrix (&scaled_font->mat);
        scaled_font->dwriteface->GetGdiCompatibleMetrics(1, 1, &transform, &fontMetrics);
	BOOL natural = font_face->measuring_mode == DWRITE_MEASURING_MODE_GDI_NATURAL;
        hr = scaled_font->dwriteface->GetGdiCompatibleGlyphMetrics (1, 1, &transform, natural, &charIndex, 1, &metrics, FALSE);
    } else {
	scaled_font->dwriteface->GetMetrics(&fontMetrics);
        hr = scaled_font->dwriteface->GetDesignGlyphMetrics(&charIndex, 1, &metrics);
    }
    if (FAILED(hr)) {
	return CAIRO_INT_STATUS_UNSUPPORTED;
    }

    // GetGdiCompatibleMetrics may return a glyph metrics that yields a small nagative glyph height.
    INT32 glyph_width = metrics.advanceWidth - metrics.leftSideBearing - metrics.rightSideBearing;
    INT32 glyph_height = metrics.advanceHeight - metrics.topSideBearing - metrics.bottomSideBearing;
    glyph_width = MAX(glyph_width, 0);
    glyph_height = MAX(glyph_height, 0);

    // TODO: Treat swap_xy.
    extents.width = (FLOAT)glyph_width / fontMetrics.designUnitsPerEm;
    extents.height = (FLOAT)glyph_height / fontMetrics.designUnitsPerEm;
    extents.x_advance = (FLOAT)metrics.advanceWidth / fontMetrics.designUnitsPerEm;
    extents.x_bearing = (FLOAT)metrics.leftSideBearing / fontMetrics.designUnitsPerEm;
    extents.y_advance = 0.0;
    extents.y_bearing = (FLOAT)(metrics.topSideBearing - metrics.verticalOriginY) /
	fontMetrics.designUnitsPerEm;

    // We pad the extents here because GetDesignGlyphMetrics returns "ideal" metrics
    // for the glyph outline, without accounting for hinting/gridfitting/antialiasing,
    // and therefore it does not always cover all pixels that will actually be touched.
    if (extents.width > 0 && extents.height > 0) {
	double x = 1, y = 1;
	cairo_matrix_transform_distance (&scaled_font->mat_inverse, &x, &y);
	x = fabs(x);
	y = fabs(y);
	extents.width += x * 2;
	extents.x_bearing -= x;
	extents.height += y * 2;
	extents.y_bearing -= y;
    }

    _cairo_scaled_glyph_set_metrics (scaled_glyph,
				     &scaled_font->base,
				     &extents);
    return CAIRO_INT_STATUS_SUCCESS;
}

/*
 * Stack-based helper implementing IDWriteGeometrySink.
 * Used to determine the path of the glyphs.
 */

class GeometryRecorder final
    : public IDWriteGeometrySink
{
public:
    GeometryRecorder(cairo_path_fixed_t *aCairoPath, const cairo_matrix_t &matrix)
	: mCairoPath(aCairoPath)
	, mMatrix(matrix) {}

    IFACEMETHOD (QueryInterface)(IID const& iid, OUT void** ppObject) noexcept override
    {
        if (iid == __uuidof (IUnknown) ||
            iid == __uuidof (IDWriteGeometrySink))
        {
            AddRef();
            *ppObject = this;
            return S_OK;
        }

        *ppObject = nullptr;
        return E_NOINTERFACE;
    }

    IFACEMETHOD_(ULONG, AddRef)() noexcept override
    {
	return 1;
    }

    IFACEMETHOD_(ULONG, Release)() noexcept override
    {
	return 1;
    }

    IFACEMETHOD_(void, SetFillMode)(D2D1_FILL_MODE fillMode) noexcept override
    {
	return;
    }

    IFACEMETHOD (Close)() noexcept override
    {
	return S_OK;
    }

    IFACEMETHOD_(void, SetSegmentFlags)(D2D1_PATH_SEGMENT vertexFlags) noexcept override
    {
	return;
    }

    IFACEMETHOD_(void, BeginFigure)(
	D2D1_POINT_2F startPoint,
	D2D1_FIGURE_BEGIN figureBegin) noexcept override
    {
	double x = startPoint.x;
	double y = startPoint.y;
	cairo_matrix_transform_point(&mMatrix, &x, &y);
	mStartPointX = _cairo_fixed_from_double(x);
	mStartPointY = _cairo_fixed_from_double(y);
	cairo_status_t status = _cairo_path_fixed_move_to(mCairoPath,
							  mStartPointX,
							  mStartPointY);
	(void)status; /* squelch warning */
    }

    IFACEMETHOD_(void, EndFigure)(
	D2D1_FIGURE_END figureEnd) noexcept override
    {
	if (figureEnd == D2D1_FIGURE_END_CLOSED) {
	    cairo_status_t status = _cairo_path_fixed_line_to(mCairoPath,
							      mStartPointX,
							      mStartPointY);
	    (void)status; /* squelch warning */
	}
    }

    IFACEMETHOD_(void, AddBeziers)(
	const D2D1_BEZIER_SEGMENT *beziers,
	UINT beziersCount) noexcept override
    {
	for (unsigned int i = 0; i < beziersCount; i++) {
	    double x1 = beziers[i].point1.x;
	    double y1 = beziers[i].point1.y;
	    double x2 = beziers[i].point2.x;
	    double y2 = beziers[i].point2.y;
	    double x3 = beziers[i].point3.x;
	    double y3 = beziers[i].point3.y;
	    cairo_matrix_transform_point(&mMatrix, &x1, &y1);
	    cairo_matrix_transform_point(&mMatrix, &x2, &y2);
	    cairo_matrix_transform_point(&mMatrix, &x3, &y3);
	    cairo_status_t status = _cairo_path_fixed_curve_to(mCairoPath,
							       _cairo_fixed_from_double(x1),
							       _cairo_fixed_from_double(y1),
							       _cairo_fixed_from_double(x2),
							       _cairo_fixed_from_double(y2),
							       _cairo_fixed_from_double(x3),
							       _cairo_fixed_from_double(y3));
	    (void)status; /* squelch warning */
	}
    }

    IFACEMETHOD_(void, AddLines)(
	const D2D1_POINT_2F *points,
	UINT pointsCount) noexcept override
    {
	for (unsigned int i = 0; i < pointsCount; i++) {
	    double x = points[i].x;
	    double y = points[i].y;
	    cairo_matrix_transform_point(&mMatrix, &x, &y);
	    cairo_status_t status = _cairo_path_fixed_line_to(mCairoPath,
							      _cairo_fixed_from_double(x),
							      _cairo_fixed_from_double(y));
	    (void)status; /* squelch warning */
	}
    }

private:
    cairo_path_fixed_t *mCairoPath;
    const cairo_matrix_t &mMatrix;
    cairo_fixed_t mStartPointX;
    cairo_fixed_t mStartPointY;
};

static cairo_int_status_t
_cairo_dwrite_scaled_font_init_glyph_path(cairo_dwrite_scaled_font_t *scaled_font,
					  cairo_scaled_glyph_t *scaled_glyph)
{
    cairo_int_status_t status;
    cairo_path_fixed_t *path;
    path = _cairo_path_fixed_create();
    GeometryRecorder recorder(path, scaled_font->base.scale);

    DWRITE_GLYPH_OFFSET offset;
    offset.advanceOffset = 0;
    offset.ascenderOffset = 0;
    UINT16 glyphId = (UINT16)_cairo_scaled_glyph_index(scaled_glyph);
    FLOAT advance = 0.0;

    HRESULT hr = scaled_font->dwriteface->GetGlyphRunOutline(1,
                                                             &glyphId,
                                                             &advance,
                                                             &offset,
                                                             1,
                                                             FALSE,
                                                             FALSE,
                                                             &recorder);
    if (!SUCCEEDED(hr))
	return _cairo_dwrite_error (hr, "GetGlyphRunOutline failed");

    status = (cairo_int_status_t)_cairo_path_fixed_close_path(path);

    _cairo_scaled_glyph_set_path (scaled_glyph,
				  &scaled_font->base,
				  path);
    return status;
}

static cairo_int_status_t
_cairo_dwrite_scaled_font_init_glyph_color_surface(cairo_dwrite_scaled_font_t *scaled_font,
						   cairo_scaled_glyph_t	      *scaled_glyph,
						   const cairo_color_t        *foreground_color)
{
    int width, height;
    double x1, y1, x2, y2;
    cairo_bool_t uses_foreground_color = FALSE;

    cairo_dwrite_font_face_t *dwrite_font_face = (cairo_dwrite_font_face_t *)scaled_font->base.font_face;
    if (!dwrite_font_face->have_color) {
	scaled_glyph->color_glyph = FALSE;
	scaled_glyph->color_glyph_set = TRUE;
	return CAIRO_INT_STATUS_UNSUPPORTED;
    }

    x1 = _cairo_fixed_integer_floor (scaled_glyph->bbox.p1.x);
    y1 = _cairo_fixed_integer_floor (scaled_glyph->bbox.p1.y);
    x2 = _cairo_fixed_integer_ceil (scaled_glyph->bbox.p2.x);
    y2 = _cairo_fixed_integer_ceil (scaled_glyph->bbox.p2.y);
    width = (int)(x2 - x1);
    height = (int)(y2 - y1);

    DWRITE_GLYPH_RUN run;
    FLOAT advance = 0;
    UINT16 index = (UINT16)_cairo_scaled_glyph_index (scaled_glyph);
    DWRITE_GLYPH_OFFSET offset;
    double x = -x1 + .25 * _cairo_scaled_glyph_xphase (scaled_glyph);
    double y = -y1 + .25 * _cairo_scaled_glyph_yphase (scaled_glyph);
    DWRITE_MATRIX matrix;
    D2D1_POINT_2F origin = {0, 0};
    RefPtr<IDWriteColorGlyphRunEnumerator1> run_enumerator;
    HRESULT hr;

    /*
     * We transform by the inverse transformation here. This will put our glyph
     * locations in the space in which we draw. Which is later transformed by
     * the transformation matrix that we use. This will transform the
     * glyph positions back to where they were before when drawing, but the
     * glyph shapes will be transformed by the transformation matrix.
     */
    cairo_matrix_transform_point(&scaled_font->mat_inverse, &x, &y);
    offset.advanceOffset = (FLOAT)x;
    /* Y-axis is inverted */
    offset.ascenderOffset = -(FLOAT)y;

    run.fontFace = scaled_font->dwriteface;
    run.fontEmSize = 1;
    run.glyphCount = 1;
    run.glyphIndices = &index;
    run.glyphAdvances = &advance;
    run.glyphOffsets = &offset;
    run.isSideways = FALSE;
    run.bidiLevel = 0;

    matrix = _cairo_dwrite_matrix_from_matrix(&scaled_font->mat);

    /* The list of glyph image formats this renderer is prepared to support. */
    DWRITE_GLYPH_IMAGE_FORMATS supported_formats = static_cast<DWRITE_GLYPH_IMAGE_FORMATS>(
        DWRITE_GLYPH_IMAGE_FORMATS_COLR |
        DWRITE_GLYPH_IMAGE_FORMATS_SVG |
        DWRITE_GLYPH_IMAGE_FORMATS_PNG |
        DWRITE_GLYPH_IMAGE_FORMATS_JPEG |
        DWRITE_GLYPH_IMAGE_FORMATS_TIFF |
        DWRITE_GLYPH_IMAGE_FORMATS_PREMULTIPLIED_B8G8R8A8);

    RefPtr<IDWriteFontFace2> fontFace2;
    UINT32 palette_count = 0;
    if (SUCCEEDED(scaled_font->dwriteface->QueryInterface(&fontFace2)))
	palette_count = fontFace2->GetColorPaletteCount();

    UINT32 palette_index = CAIRO_COLOR_PALETTE_DEFAULT;
    if (scaled_font->base.options.palette_index < palette_count)
	palette_index = scaled_font->base.options.palette_index;

    hr = DWriteFactory::Instance4()->TranslateColorGlyphRun(
	origin,
	&run,
	NULL, /* glyphRunDescription */
	supported_formats,
	dwrite_font_face->measuring_mode,
	&matrix,
	palette_index,
	&run_enumerator);

    if (hr == DWRITE_E_NOCOLOR) {
	/* No color glyphs */
	scaled_glyph->color_glyph = FALSE;
	scaled_glyph->color_glyph_set = TRUE;
	return CAIRO_INT_STATUS_UNSUPPORTED;
    }

    if (FAILED(hr))
	return _cairo_dwrite_error (hr, "TranslateColorGlyphRun failed");

    /* We have a color glyph(s). Use Direct2D to render it to a bitmap */
    if (!WICImagingFactory::Instance() || !D2DFactory::Instance())
	return _cairo_dwrite_error (hr, "Instance failed");

    RefPtr<IWICBitmap> bitmap;
    hr = WICImagingFactory::Instance()->CreateBitmap ((UINT)width,
						      (UINT)height,
						      GUID_WICPixelFormat32bppPBGRA,
						      WICBitmapCacheOnLoad,
						      &bitmap);
    if (FAILED(hr))
	return _cairo_dwrite_error (hr, "CreateBitmap failed");

    D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
	D2D1_RENDER_TARGET_TYPE_DEFAULT,
	D2D1::PixelFormat(
	    DXGI_FORMAT_B8G8R8A8_UNORM,
	    D2D1_ALPHA_MODE_PREMULTIPLIED),
	0,
	0,
	D2D1_RENDER_TARGET_USAGE_NONE,
	D2D1_FEATURE_LEVEL_DEFAULT);

    RefPtr<ID2D1RenderTarget> rt;
    hr = D2DFactory::Instance()->CreateWicBitmapRenderTarget (bitmap, properties, &rt);
    if (FAILED(hr))
	return _cairo_dwrite_error (hr, "CreateWicBitmapRenderTarget failed");

    RefPtr<ID2D1DeviceContext4> dc4;
    hr = rt->QueryInterface(&dc4);
    if (FAILED(hr))
	return _cairo_dwrite_error (hr, "QueryInterface(&dc4) failed");

    RefPtr<ID2D1SolidColorBrush> foreground_color_brush;
    dc4->CreateSolidColorBrush(
	D2D1::ColorF(foreground_color->red,
		     foreground_color->green,
		     foreground_color->blue,
		     foreground_color->alpha), &foreground_color_brush);

    RefPtr<ID2D1SolidColorBrush> color_brush;
    dc4->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &color_brush);

    dc4->SetDpi(96, 96); /* 1 unit = 1 pixel */
    rt->SetTransform(D2D1::Matrix3x2F(matrix.m11,
				      matrix.m12,
				      matrix.m21,
				      matrix.m22,
				      matrix.dx,
				      matrix.dy));

    dc4->BeginDraw();
    dc4->Clear(NULL); /* Transparent black */

    while (true) {
	BOOL have_run;
	hr = run_enumerator->MoveNext(&have_run);
	if (FAILED(hr) || !have_run)
	    break;

	DWRITE_COLOR_GLYPH_RUN1_WORKAROUND const* color_run;
	hr = run_enumerator->GetCurrentRun(reinterpret_cast<const DWRITE_COLOR_GLYPH_RUN1**>(&color_run));
	if (FAILED(hr))
	    return _cairo_dwrite_error (hr, "GetCurrentRun failed");

	switch (color_run->glyphImageFormat) {
	    case DWRITE_GLYPH_IMAGE_FORMATS_PNG:
	    case DWRITE_GLYPH_IMAGE_FORMATS_JPEG:
	    case DWRITE_GLYPH_IMAGE_FORMATS_TIFF:
	    case DWRITE_GLYPH_IMAGE_FORMATS_PREMULTIPLIED_B8G8R8A8:
		/* Bitmap glyphs */
		dc4->DrawColorBitmapGlyphRun(color_run->glyphImageFormat,
					     origin,
					     &color_run->glyphRun,
					     dwrite_font_face->measuring_mode,
					     D2D1_COLOR_BITMAP_GLYPH_SNAP_OPTION_DEFAULT);
		break;

	    case DWRITE_GLYPH_IMAGE_FORMATS_SVG:
		/* SVG glyphs */
		dc4->DrawSvgGlyphRun(origin,
				     &color_run->glyphRun,
				     foreground_color_brush,
				     nullptr,
				     palette_index,
				     dwrite_font_face->measuring_mode);
		uses_foreground_color = TRUE;
		break;
	    case DWRITE_GLYPH_IMAGE_FORMATS_TRUETYPE:
	    case DWRITE_GLYPH_IMAGE_FORMATS_CFF:
	    case DWRITE_GLYPH_IMAGE_FORMATS_COLR:
		/* Outline glyphs */
		if (color_run->paletteIndex == 0xFFFF) {
		    D2D1_COLOR_F color = foreground_color_brush->GetColor();
		    color_brush->SetColor(&color);
		    uses_foreground_color = TRUE;
		} else {
		    double red, green, blue, alpha;
		    cairo_status_t status;
		    status = cairo_font_options_get_custom_palette_color (&scaled_font->base.options,
									  color_run->paletteIndex,
									  &red, &blue, &green, &alpha);
		    if (status == CAIRO_STATUS_SUCCESS) {
			color_brush->SetColor(D2D1::ColorF(red, blue, green, alpha));
		    } else {
			color_brush->SetColor(color_run->runColor);
		    }
		}

		dc4->DrawGlyphRun(origin,
				  &color_run->glyphRun,
				  color_run->glyphRunDescription,
				  color_brush,
				  dwrite_font_face->measuring_mode);
	    case DWRITE_GLYPH_IMAGE_FORMATS_NONE:
		break;
	}
    }

    hr = dc4->EndDraw();
    if (FAILED(hr))
	return _cairo_dwrite_error (hr, "EndDraw failed");

    cairo_surface_t *image = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, width, height);
    cairo_status_t status = cairo_surface_status (image);
    if (_cairo_status_is_error (status)) {
        cairo_surface_destroy (image);
        return (cairo_int_status_t) status;
    }

    int stride = cairo_image_surface_get_stride (image);
    WICRect rect = { 0, 0, width, height };
    bitmap->CopyPixels(&rect,
		       stride,
		       height * stride,
		       cairo_image_surface_get_data (image));
    cairo_surface_mark_dirty (image);
    cairo_surface_set_device_offset (image, -x1, -y1);
    _cairo_scaled_glyph_set_color_surface (scaled_glyph,
					   &scaled_font->base,
					   (cairo_image_surface_t *) image,
					   uses_foreground_color ? foreground_color : NULL);
    scaled_glyph->color_glyph = TRUE;
    scaled_glyph->color_glyph_set = TRUE;

    return CAIRO_INT_STATUS_SUCCESS;
}

// Helper for OS versions up to Windows 8
static cairo_int_status_t
init_glyph_surface_fallback_a8 (cairo_dwrite_scaled_font_t  *scaled_font,
                                cairo_scaled_glyph_t        *scaled_glyph,
                                int                          width,
                                int                          height,
                                double                       x1,
                                double                       y1,
                                DWRITE_MATRIX               *matrix,
                                DWRITE_GLYPH_RUN            *run)
{
    RefPtr<IWICBitmap> bitmap;
    HRESULT hr;

    hr = WICImagingFactory::Instance()->CreateBitmap ((UINT)width,
                                                      (UINT)height,
                                                      GUID_WICPixelFormat8bppAlpha,
                                                      WICBitmapCacheOnLoad,
                                                      &bitmap);
    if (FAILED(hr))
        return _cairo_dwrite_error (hr, "CreateBitmap failed");

    D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(
            DXGI_FORMAT_A8_UNORM,
            D2D1_ALPHA_MODE_PREMULTIPLIED),
        0,
        0,
        D2D1_RENDER_TARGET_USAGE_NONE,
        D2D1_FEATURE_LEVEL_DEFAULT);

    RefPtr<ID2D1RenderTarget> rt;
    hr = D2DFactory::Instance()->CreateWicBitmapRenderTarget (bitmap, properties, &rt);
    if (FAILED(hr))
        return _cairo_dwrite_error (hr, "CreateWicBitmapRenderTarget failed");

    RefPtr<ID2D1SolidColorBrush> brush;
    hr = rt->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black, 1.0), &brush);

    rt->BeginDraw();

    rt->SetTransform(*(D2D1_MATRIX_3X2_F*)matrix);

    rt->DrawGlyphRun({0, 0}, run, brush, scaled_font->measuring_mode);

    hr = rt->EndDraw();
    if (FAILED(hr))
        return _cairo_dwrite_error (hr, "EndDraw failed");

    // TODO: rt->Flush()?

    cairo_surface_t *surface = cairo_image_surface_create (CAIRO_FORMAT_A8, width, height);
    if (cairo_surface_status (surface))
        return CAIRO_INT_STATUS_UNSUPPORTED;

    int stride = cairo_image_surface_get_stride (surface);
    WICRect rect = { 0, 0, width, height };
    bitmap->CopyPixels(&rect,
                       stride,
                       height * stride,
                       cairo_image_surface_get_data (surface));
    cairo_surface_mark_dirty (surface);
    cairo_surface_set_device_offset (surface, -x1, -y1);
    _cairo_scaled_glyph_set_surface (scaled_glyph,
                                     &scaled_font->base,
                                     (cairo_image_surface_t*)surface);

    return CAIRO_INT_STATUS_SUCCESS;
}

static cairo_int_status_t
_cairo_dwrite_scaled_font_init_glyph_surface (cairo_dwrite_scaled_font_t *scaled_font,
                                              cairo_scaled_glyph_t       *scaled_glyph)
{
    HRESULT hr;

    double x1 = _cairo_fixed_integer_floor (scaled_glyph->bbox.p1.x);
    double y1 = _cairo_fixed_integer_floor (scaled_glyph->bbox.p1.y);
    double x2 = _cairo_fixed_integer_ceil (scaled_glyph->bbox.p2.x);
    double y2 = _cairo_fixed_integer_ceil (scaled_glyph->bbox.p2.y);
    int width = (int)(x2 - x1);
    int height = (int)(y2 - y1);

    if (width <= 0)
        width = 1;
    if (height <= 0)
        height = 1;

    FLOAT advance = 0;
    UINT16 index = (UINT16)_cairo_scaled_glyph_index (scaled_glyph);
    DWRITE_GLYPH_OFFSET offset;
    double x = -x1 + .25 * _cairo_scaled_glyph_xphase (scaled_glyph);
    double y = -y1 + .25 * _cairo_scaled_glyph_yphase (scaled_glyph);

    /* We transform by the inverse transformation here. This will put our glyph
     * locations in the space in which we draw. Which is later transformed by
     * the transformation matrix that we use. This will transform the
     * glyph positions back to where they were before when drawing, but the
     * glyph shapes will be transformed by the transformation matrix. */
    cairo_matrix_transform_point(&scaled_font->mat_inverse, &x, &y);
    offset.advanceOffset = (FLOAT)x;
    offset.ascenderOffset = -(FLOAT)y; /* Y axis is inverted */

    DWRITE_MATRIX matrix = _cairo_dwrite_matrix_from_matrix(&scaled_font->mat);

    DWRITE_GLYPH_RUN run;
    run.glyphCount = 1;
    run.glyphAdvances = &advance;
    run.fontFace = scaled_font->dwriteface;
    run.fontEmSize = 1.0f;
    run.bidiLevel = 0;
    run.glyphIndices = &index;
    run.isSideways = FALSE;
    run.glyphOffsets = &offset;

    // Reduce the many Cairo antialias values to the
    // three we actually care about: NONE, GRAY, RGB
    enum {
        ANTIALIAS_NONE,
        ANTIALIAS_GRAY,
        ANTIALIAS_CLEARTYPE,
    } antialias = ANTIALIAS_CLEARTYPE;

    switch (scaled_font->antialias_mode) {
        case CAIRO_ANTIALIAS_NONE:
            antialias = ANTIALIAS_NONE;
            break;
        case CAIRO_ANTIALIAS_FAST:
        case CAIRO_ANTIALIAS_GRAY:
            antialias = ANTIALIAS_GRAY;
            break;
        case CAIRO_ANTIALIAS_DEFAULT:
        case CAIRO_ANTIALIAS_GOOD:
        case CAIRO_ANTIALIAS_BEST:
        case CAIRO_ANTIALIAS_SUBPIXEL:
            antialias = ANTIALIAS_CLEARTYPE;
            break;
    }

    // Set DWrite rendering options

    DWRITE_RENDERING_MODE rendering_mode;
    DWRITE_RENDERING_MODE1 rendering_mode1;
    DWRITE_TEXT_ANTIALIAS_MODE text_antialias_mode;
    DWRITE_TEXTURE_TYPE texture_type;

    switch (antialias) {
        case ANTIALIAS_NONE:
            rendering_mode1 = DWRITE_RENDERING_MODE1_ALIASED;
            rendering_mode = DWRITE_RENDERING_MODE_ALIASED;
            text_antialias_mode = DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE;
            texture_type = DWRITE_TEXTURE_ALIASED_1x1;
            break;
        case ANTIALIAS_GRAY:
            rendering_mode1 = DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC;
            rendering_mode = DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC;
            text_antialias_mode = DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE;
            texture_type = DWRITE_TEXTURE_ALIASED_1x1;
            break;
        case ANTIALIAS_CLEARTYPE:
            rendering_mode1 = DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC;
            rendering_mode = DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC;
            text_antialias_mode = DWRITE_TEXT_ANTIALIAS_MODE_CLEARTYPE;
            texture_type = DWRITE_TEXTURE_CLEARTYPE_3x1;
            break;
    }

    DWRITE_GRID_FIT_MODE grid_fit_mode = DWRITE_GRID_FIT_MODE_DEFAULT;

    switch (cairo_font_options_get_hint_style (&scaled_font->base.options)) {
        case CAIRO_HINT_STYLE_DEFAULT:
            grid_fit_mode = DWRITE_GRID_FIT_MODE_DEFAULT;
            break;
        case CAIRO_HINT_STYLE_NONE:
            grid_fit_mode = DWRITE_GRID_FIT_MODE_DISABLED;
            break;
        case CAIRO_HINT_STYLE_SLIGHT:
        case CAIRO_HINT_STYLE_MEDIUM:
        case CAIRO_HINT_STYLE_FULL:
            grid_fit_mode = DWRITE_GRID_FIT_MODE_ENABLED;
            break;
    }

    cairo_subpixel_order_t subpixel_order;
    subpixel_order = cairo_font_options_get_subpixel_order (&scaled_font->base.options);

    bool subpixel_order_is_vertical = false;
    if (antialias == ANTIALIAS_CLEARTYPE) {
        switch (subpixel_order) {
            case CAIRO_SUBPIXEL_ORDER_DEFAULT:
            case CAIRO_SUBPIXEL_ORDER_RGB:
            case CAIRO_SUBPIXEL_ORDER_BGR:
                break;
            case CAIRO_SUBPIXEL_ORDER_VRGB:
            case CAIRO_SUBPIXEL_ORDER_VBGR:
                subpixel_order_is_vertical = true;
                break;
        }
    }

    if (subpixel_order_is_vertical) {
        // DirectWrite does not support vertical pixel geometries.
        // As a workaround, apply a simmetry which swaps x and y
        // coordinates, then re-swap while copying the back into
        // the image surface

        // swap the two rows
        std::swap (matrix.m11, matrix.m21);
        std::swap (matrix.m12, matrix.m22);
    }

    RefPtr<IDWriteGlyphRunAnalysis> dwrite_glyph_run_analysis;
    if (DWriteFactory::Instance3()) {
        hr = DWriteFactory::Instance3()->CreateGlyphRunAnalysis(&run,
                                                                &matrix,
                                                                rendering_mode1,
                                                                scaled_font->measuring_mode,
                                                                grid_fit_mode,
                                                                text_antialias_mode,
                                                                0, // baselineOriginX,
                                                                0, // baselineOriginY,
                                                                &dwrite_glyph_run_analysis);
    }
    else if (DWriteFactory::Instance2()) {
        hr = DWriteFactory::Instance2()->CreateGlyphRunAnalysis(&run,
                                                                &matrix,
                                                                rendering_mode,
                                                                scaled_font->measuring_mode,
                                                                grid_fit_mode,
                                                                text_antialias_mode,
                                                                0, // baselineOriginX,
                                                                0, // baselineOriginY,
                                                                &dwrite_glyph_run_analysis);
    }
    else {
        if (antialias == ANTIALIAS_GRAY) {
            // IDWriteGlyphRunAnalysis supports gray-scale antialiasing only when
            // created from IDWriteFactory2 or later. If we have IDWriteFactory
            // only, fallback to rendering with Direct2D on A8 targets.
            return init_glyph_surface_fallback_a8 (scaled_font, scaled_glyph,
                                                   width, height, x1, y1, &matrix, &run);
        }

        hr = DWriteFactory::Instance()->CreateGlyphRunAnalysis(&run, 1,
                                                               &matrix,
                                                               rendering_mode,
                                                               scaled_font->measuring_mode,
                                                               0, // baselineOriginX,
                                                               0, // baselineOriginY,
                                                               &dwrite_glyph_run_analysis);
    }
    if (FAILED(hr))
       return CAIRO_INT_STATUS_UNSUPPORTED;

    cairo_format_t surface_format = antialias == ANTIALIAS_NONE ? CAIRO_FORMAT_A1 :
                                    antialias == ANTIALIAS_GRAY ? CAIRO_FORMAT_A8 :
                                    CAIRO_FORMAT_ARGB32;

    cairo_surface_t *surface = cairo_image_surface_create (surface_format, width, height);
    if (cairo_surface_status (surface))
        return CAIRO_INT_STATUS_UNSUPPORTED;

    unsigned char *surface_data = cairo_image_surface_get_data (surface);
    int surface_stride = cairo_image_surface_get_stride (surface);

    UINT32 dwrite_data_size;
    BYTE *dwrite_data;
    RECT dwrite_rect = {
        0,       // left
        0,       // top
        width,   // right
        height   // bottom
    };

    if (subpixel_order_is_vertical) {
        std::swap (dwrite_rect.right,
                   dwrite_rect.bottom);
    }

    // Whether IDWriteGlyphRunAnalysis::CreateAlphaTexture() can render directly
    // on the cairo image surface (because the pixel formats match) or a separate
    // buffer is needed
    bool render_is_direct;

    switch (antialias) {
        case ANTIALIAS_NONE:
        {
            dwrite_data_size = width * height; //TODO: check overflow

            dwrite_data = (BYTE*) _cairo_malloc (dwrite_data_size);
            if (!dwrite_data)
                return CAIRO_INT_STATUS_UNSUPPORTED;

            render_is_direct = false;
            break;
        }
        case ANTIALIAS_GRAY:
        {
            // The image surface may have a stride that's bigger than width-
            // account for that by passing stride as width to DWrite. Note:
            // stride is a byte-size, but here pixel-size is exactly 1 byte.
            dwrite_rect.right = cairo_image_surface_get_stride (surface);

            dwrite_data_size = dwrite_rect.right * height; //TODO: check overflow
            dwrite_data = static_cast<BYTE*>(surface_data);

            render_is_direct = true;
            break;
        }
        case ANTIALIAS_CLEARTYPE:
        {
            dwrite_data_size = 3 * width * height; //TODO: check overflow

            dwrite_data = (BYTE*) _cairo_malloc (dwrite_data_size);
            if (!dwrite_data)
                return CAIRO_INT_STATUS_UNSUPPORTED;

            render_is_direct = false;
            break;
        }
    }

    hr = dwrite_glyph_run_analysis->CreateAlphaTexture(texture_type, &dwrite_rect, dwrite_data, dwrite_data_size);
    if (FAILED (hr))
        return CAIRO_INT_STATUS_UNSUPPORTED;

    // Most of the code here was copied and adapted from cairoft-font.c
    switch (antialias) {
        case ANTIALIAS_NONE:
        {
            unsigned char *src = static_cast<unsigned char*>(dwrite_data);
            unsigned char *dst = surface_data;

            for (int i = 0; i < height; i++) {
                unsigned char *d = dst;

                for (int j = 0; j < width / 8; j++) {
                    *d = (src[0] ? (1 << 0) : 0) +
                           (src[1] ? (1 << 1) : 0) +
                           (src[2] ? (1 << 2) : 0) +
                           (src[3] ? (1 << 3) : 0) +
                           (src[4] ? (1 << 4) : 0) +
                           (src[5] ? (1 << 5) : 0) +
                           (src[6] ? (1 << 6) : 0) +
                           (src[7] ? (1 << 7) : 0);

                    d++;
                    src += 8;
                }

                if (width % 8 != 0) {
                    *d = 0;

                    for (int k = 0; k < width % 8; k++) {
                        *d += (src[k] ? (1 << k) : 0);
                    }

                    d++;
                    src += (width % 8);
                }

                dst += surface_stride;
            }
            break;
        }
        case ANTIALIAS_GRAY:
        {
           // Nothing to do
           break;
        }
        case ANTIALIAS_CLEARTYPE:
        {
            unsigned char *src = static_cast<unsigned char*>(dwrite_data);
            unsigned char *dst = surface_data;

            // The alpha channel is unused for component-alpha blending.
            // Here we set the alpha channel anyway so that things work
            // even in case of normal blending (but one likely gets some
            // color fringing)

            switch (subpixel_order) {
                case CAIRO_SUBPIXEL_ORDER_DEFAULT:
                case CAIRO_SUBPIXEL_ORDER_RGB:
                {
                    for (int i = 0; i < height; i++) {
                        UINT32 *d = reinterpret_cast<UINT32*>(dst);

                        for (int j = 0; j < width; j++) {
                            // CAIRO_FORMAT_ARGB32 is BGRA on little-endian
                            *d = (src[2] << 0) +
                                 (src[1] << 8) +
                                 (src[0] << 16) +
                                 (src[1] << 24); // unused

                            d++;
                            src += 3;
                        }

                        dst += surface_stride;
                    }
                }
                break;
                case CAIRO_SUBPIXEL_ORDER_BGR:
                {
                    for (int i = 0; i < height; i++) {
                        UINT32 *d = reinterpret_cast<UINT32*>(dst);

                        for (int j = 0; j < width; j++) {
                            *d = (src[0] << 0) +
                                 (src[1] << 8) +
                                 (src[2] << 16) +
                                 (src[1] << 24); // unused

                            d++;
                            src += 3;
                        }

                        dst += surface_stride;
                    }
                }
                break;
                case CAIRO_SUBPIXEL_ORDER_VRGB:
                {
                    size_t p;

                    for (int i = 0; i < height; i++) {
                        UINT32 *d = reinterpret_cast<UINT32*>(dst);

                        p = i * 3;
                        for (int j = 0; j < width; j++) {
                            *d = (src[p + 2] << 0) +
                                 (src[p + 1] << 8) +
                                 (src[p + 0] << 16) +
                                 (src[p + 1] << 24); // unused

                            d++;
                            p += 3 * height;
                        }

                        dst += surface_stride;
                    }
                }
                break;
                case CAIRO_SUBPIXEL_ORDER_VBGR:
                {
                    size_t p;

                    for (int i = 0; i < height; i++) {
                        UINT32 *d = reinterpret_cast<UINT32*>(dst);

                        p = i * 3;
                        for (int j = 0; j < width; j++) {
                            *d = (src[p + 0] << 0) +
                                 (src[p + 1] << 8) +
                                 (src[p + 2] << 16) +
                                 (src[p + 1] << 24); // unused

                            d++;
                            p += 3 * height;
                        }

                        dst += surface_stride;
                    }
                }
                break;
            }

            break;
        }
    }

    if (!render_is_direct) {
        free (dwrite_data);
    }

    cairo_surface_set_device_offset (surface, -x1, -y1);
    _cairo_scaled_glyph_set_surface (scaled_glyph,
                                     &scaled_font->base,
                                     (cairo_image_surface_t*)surface);

    return CAIRO_INT_STATUS_SUCCESS;
}

static cairo_int_status_t
_cairo_dwrite_load_truetype_table(void                 *scaled_font,
				  unsigned long         tag,
				  long                  offset,
				  unsigned char        *buffer,
				  unsigned long        *length)
{
    cairo_dwrite_scaled_font_t *dwritesf = static_cast<cairo_dwrite_scaled_font_t*>(scaled_font);

    const void *data;
    UINT32 size;
    void *tableContext;
    BOOL exists;
    HRESULT hr;
    hr = dwritesf->dwriteface->TryGetFontTable (be32_to_cpu (tag),
                                                &data,
                                                &size,
                                                &tableContext,
                                                &exists);
    if (FAILED(hr))
	return _cairo_dwrite_error (hr, "TryGetFontTable failed");

    if (!exists) {
	return CAIRO_INT_STATUS_UNSUPPORTED;
    }

    if (buffer && *length && (UINT32)offset < size) {
        size = MIN(size - (UINT32)offset, *length);
        memcpy(buffer, (const char*)data + offset, size);
    }
    *length = size;

    if (tableContext) {
        dwritesf->dwriteface->ReleaseFontTable(tableContext);
    }
    return (cairo_int_status_t)CAIRO_STATUS_SUCCESS;
}

static cairo_int_status_t
_cairo_dwrite_is_synthetic(void                      *scaled_font,
			   cairo_bool_t              *is_synthetic)
{
    cairo_dwrite_scaled_font_t *dwritesf = static_cast<cairo_dwrite_scaled_font_t*>(scaled_font);
    HRESULT hr;
    cairo_int_status_t status;

    if (dwritesf->dwriteface->GetSimulations() != DWRITE_FONT_SIMULATIONS_NONE) {
	*is_synthetic = FALSE;
	return CAIRO_INT_STATUS_SUCCESS;
    }

    RefPtr<IDWriteFontFace5> fontFace5;
    if (FAILED(dwritesf->dwriteface->QueryInterface(&fontFace5))) {
	/* If IDWriteFontFace5 is not available, assume this version of
	 * DirectWrite does not support variations.
	 */
	*is_synthetic = FALSE;
	return CAIRO_INT_STATUS_SUCCESS;
    }

    if (!fontFace5->HasVariations()) {
	*is_synthetic = FALSE;
	return CAIRO_INT_STATUS_SUCCESS;
    }

    RefPtr<IDWriteFontResource> fontResource;
    hr = fontFace5->GetFontResource(&fontResource);
    if (FAILED(hr))
	return _cairo_dwrite_error (hr, "GetFontResource failed");

    UINT32 axis_count = fontResource->GetFontAxisCount();
    DWRITE_FONT_AXIS_VALUE *axis_defaults = new DWRITE_FONT_AXIS_VALUE[axis_count];
    DWRITE_FONT_AXIS_VALUE *axis_values = new DWRITE_FONT_AXIS_VALUE[axis_count];

    hr = fontResource->GetDefaultFontAxisValues(axis_defaults, axis_count);
    if (FAILED(hr)) {
	status = _cairo_dwrite_error (hr, "GetDefaultFontAxisValues failed");
	goto cleanup;
    }

    hr = fontFace5->GetFontAxisValues(axis_values, axis_count);
    if (FAILED(hr)) {
	status = _cairo_dwrite_error (hr, "GetFontAxisValues failed");
	goto cleanup;
    }

    /* The DirectWrite documentation does not state if the tags of the returned
     * defaults and values arrays are in the same order. So assume they are not.
     */
    *is_synthetic = FALSE;
    status = CAIRO_INT_STATUS_SUCCESS;
    for (UINT32 i = 0; i< axis_count; i++) {
	for (UINT32 j = 0; j < axis_count; j++) {
	    if (axis_values[i].axisTag == axis_defaults[j].axisTag) {
		if (axis_values[i].value != axis_defaults[j].value) {
		    *is_synthetic = TRUE;
		    goto cleanup;
		}
		break;
	    }
	}
    }

  cleanup:
    delete[] axis_defaults;
    delete[] axis_values;
    
    return status;
}

static cairo_bool_t
_cairo_dwrite_has_color_glyphs(void *scaled_font)
{
    cairo_dwrite_scaled_font_t *dwritesf = static_cast<cairo_dwrite_scaled_font_t*>(scaled_font);

    return ((cairo_dwrite_font_face_t *)dwritesf->base.font_face)->have_color;
}

/**
 * cairo_dwrite_font_face_create_for_dwrite_fontface:
 * @dwrite_font_face: A pointer to an #IDWriteFontFace specifying the
 * DWrite font to use.
 *
 * Creates a new font for the DWrite font backend based on a
 * DWrite font face. This font can then be used with
 * cairo_set_font_face() or cairo_scaled_font_create().
 *
 * Here is an example of how this function might be used:
 * <informalexample><programlisting><![CDATA[
 * #include <cairo-dwrite.h>
 * #include <dwrite.h>
 *
 * IDWriteFactory* dWriteFactory = NULL;
 * HRESULT hr = DWriteCreateFactory(
 *     DWRITE_FACTORY_TYPE_SHARED,
 *     __uuidof(IDWriteFactory),
 *    reinterpret_cast<IUnknown**>(&dWriteFactory));
 *
 * IDWriteFontCollection *systemCollection;
 * hr = dWriteFactory->GetSystemFontCollection(&systemCollection);
 *
 * UINT32 idx;
 * BOOL found;
 * systemCollection->FindFamilyName(L"Segoe UI Emoji", &idx, &found);
 *
 * IDWriteFontFamily *family;
 * systemCollection->GetFontFamily(idx, &family);
 *
 * IDWriteFont *dwritefont;
 * DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
 * DWRITE_FONT_STYLE style = DWRITE_FONT_STYLE_NORMAL;
 * hr = family->GetFirstMatchingFont(weight, DWRITE_FONT_STRETCH_NORMAL, style, &dwritefont);
 *
 * IDWriteFontFace *dwriteface;
 * hr = dwritefont->CreateFontFace(&dwriteface);
 *
 * cairo_font_face_t *face;
 * face = cairo_dwrite_font_face_create_for_dwrite_fontface(dwriteface);
 * cairo_set_font_face(cr, face);
 * cairo_set_font_size(cr, 70);
 * cairo_move_to(cr, 100, 100);
 * cairo_show_text(cr, "😃");
 * ]]></programlisting></informalexample>
 *
 * Note: When printing a DWrite font to a
 * #CAIRO_SURFACE_TYPE_WIN32_PRINTING surface, the printing surface
 * will substitute each DWrite font with a Win32 font created from the same
 * underlying font file. If the matching font file can not be found,
 * the #CAIRO_SURFACE_TYPE_WIN32_PRINTING surface will convert each
 * glyph to a filled path. If a DWrite font was not created from a system
 * font, it is recommended that the font used to create the DWrite
 * font be made available to GDI to avoid the undesirable fallback
 * to emitting paths. This can be achieved using the GDI font loading functions
 * such as AddFontMemResourceEx().
 *
 * Return value: a newly created #cairo_font_face_t. Free with
 *  cairo_font_face_destroy() when you are done using it.
 *
 * Since: 1.18
 **/
cairo_font_face_t *
cairo_dwrite_font_face_create_for_dwrite_fontface (IDWriteFontFace *dwrite_font_face)
{
    IDWriteFontFace *dwriteface = static_cast<IDWriteFontFace*>(dwrite_font_face);
    // Must do malloc and not C++ new, since Cairo frees this.
    cairo_dwrite_font_face_t *face = (cairo_dwrite_font_face_t *)_cairo_malloc(sizeof(cairo_dwrite_font_face_t));
    if (unlikely (face == NULL)) {
	_cairo_error_throw (CAIRO_STATUS_NO_MEMORY);
	return (cairo_font_face_t*)&_cairo_font_face_nil;
    }

    dwriteface->AddRef();
    face->dwriteface = dwriteface;
    face->have_color = false;
    face->rendering_params = NULL;
    face->measuring_mode = DWRITE_MEASURING_MODE_NATURAL;

    /* Ensure IDWriteFactory4 is available before enabling color fonts */
    if (DWriteFactory::Instance4()) {
	RefPtr<IDWriteFontFace2> fontFace2;
	if (SUCCEEDED(dwriteface->QueryInterface(&fontFace2))) {
	    if (fontFace2->IsColorFont())
		face->have_color = true;
	}
    }

    cairo_font_face_t *font_face;
    font_face = (cairo_font_face_t*)face;
    _cairo_font_face_init (&((cairo_dwrite_font_face_t*)font_face)->base, &_cairo_dwrite_font_face_backend);

    return font_face;
}

/**
 * cairo_dwrite_font_face_get_rendering_params:
 * @font_face: The #cairo_dwrite_font_face_t object to query
 *
 * Gets the #IDWriteRenderingParams object of @font_face.
 *
 * Return value: the #IDWriteRenderingParams object or %NULL if none.
 *
 * Since: 1.18
 **/
IDWriteRenderingParams *
cairo_dwrite_font_face_get_rendering_params (cairo_font_face_t *font_face)
{
    cairo_dwrite_font_face_t *dwface = reinterpret_cast<cairo_dwrite_font_face_t *>(font_face);
    return dwface->rendering_params;
}

/**
 * cairo_dwrite_font_face_set_rendering_params:
 * @font_face: The #cairo_dwrite_font_face_t object to modify
 * @params: The #IDWriteRenderingParams object
 *
 * Sets the #IDWriteRenderingParams object to @font_face.
 * This #IDWriteRenderingParams is used to render glyphs if default values of font options are used.
 * If non-defalut values of font options are specified when creating a #cairo_scaled_font_t,
 * cairo creates a new #IDWriteRenderingParams object for the #cairo_scaled_font_t object by overwriting the corresponding parameters.
 *
 * Since: 1.18
 **/
void
cairo_dwrite_font_face_set_rendering_params (cairo_font_face_t *font_face, IDWriteRenderingParams *params)
{
    cairo_dwrite_font_face_t *dwface = reinterpret_cast<cairo_dwrite_font_face_t *>(font_face);
    if (dwface->rendering_params)
	dwface->rendering_params->Release();
    dwface->rendering_params = params;
    if (dwface->rendering_params)
	dwface->rendering_params->AddRef();
}

/**
 * cairo_dwrite_font_face_get_measuring_mode:
 * @font_face: The #cairo_dwrite_font_face_t object to query
 *
 * Gets the #DWRITE_MEASURING_MODE enum of @font_face.
 *
 * Return value: The #DWRITE_MEASURING_MODE enum of @font_face.
 *
 * Since: 1.18
 **/
DWRITE_MEASURING_MODE
cairo_dwrite_font_face_get_measuring_mode (cairo_font_face_t *font_face)
{
    cairo_dwrite_font_face_t *dwface = reinterpret_cast<cairo_dwrite_font_face_t *>(font_face);
    return dwface->measuring_mode;
}

/**
 * cairo_dwrite_font_face_set_measuring_mode:
 * @font_face: The #cairo_dwrite_font_face_t object to modify
 * @mode: The #DWRITE_MEASURING_MODE enum.
 *
 * Sets the #DWRITE_MEASURING_MODE enum to @font_face.
 * 
 * Since: 1.18
 **/
void
cairo_dwrite_font_face_set_measuring_mode (cairo_font_face_t *font_face, DWRITE_MEASURING_MODE mode)
{
    cairo_dwrite_font_face_t *dwface = reinterpret_cast<cairo_dwrite_font_face_t *>(font_face);
    dwface->measuring_mode = mode;
}

static cairo_int_status_t
_dwrite_draw_glyphs_to_gdi_surface_gdi(cairo_win32_surface_t *surface,
				       DWRITE_MATRIX *transform,
				       DWRITE_GLYPH_RUN *run,
				       COLORREF color,
				       cairo_dwrite_scaled_font_t *scaled_font,
				       const RECT &area)
{
    RefPtr<IDWriteGdiInterop> gdiInterop;
    DWriteFactory::Instance()->GetGdiInterop(&gdiInterop);
    RefPtr<IDWriteBitmapRenderTarget> rt;
    HRESULT hr;

    hr = gdiInterop->CreateBitmapRenderTarget(surface->dc,
					      area.right - area.left,
					      area.bottom - area.top,
					      &rt);

    if (FAILED(hr)) {
	if (hr == E_OUTOFMEMORY) {
	    return (cairo_int_status_t)CAIRO_STATUS_NO_MEMORY;
	} else {
	    return CAIRO_INT_STATUS_UNSUPPORTED;
	}
    }

    /*
     * We set the number of pixels per DIP to 1.0. This is because we always want
     * to draw in device pixels, and not device independent pixels. On high DPI
     * systems this value will be higher than 1.0 and automatically upscale
     * fonts, we don't want this since we do our own upscaling for various reasons.
     */
    rt->SetPixelsPerDip(1.0);

    float x = 0, y = 0;
    if (transform) {
	DWRITE_MATRIX matrix = *transform;
	matrix.dx -= area.left;
	matrix.dy -= area.top;
	rt->SetCurrentTransform(&matrix);
    } else {
	x = (float) -area.left;
	y = (float) -area.top;
    }
    BitBlt(rt->GetMemoryDC(),
	   0, 0,
	   area.right - area.left, area.bottom - area.top,
	   surface->dc,
	   area.left, area.top,
	   SRCCOPY | NOMIRRORBITMAP);
    rt->DrawGlyphRun(x, y, scaled_font->measuring_mode, run, scaled_font->rendering_params, color);
    BitBlt(surface->dc,
	   area.left, area.top,
	   area.right - area.left, area.bottom - area.top,
	   rt->GetMemoryDC(),
	   0, 0,
	   SRCCOPY | NOMIRRORBITMAP);
    return CAIRO_INT_STATUS_SUCCESS;
}

cairo_int_status_t
_dwrite_draw_glyphs_to_gdi_surface_d2d(cairo_win32_surface_t *surface,
				       DWRITE_MATRIX *transform,
				       DWRITE_GLYPH_RUN *run,
				       COLORREF color,
				       const RECT &area)
{
    HRESULT hr;

    RefPtr<ID2D1DCRenderTarget> rt = D2DFactory::RenderTarget();

    // XXX don't we need to set RenderingParams on this RenderTarget?

    hr = rt->BindDC(surface->dc, &area);
    if (FAILED(hr))
	return CAIRO_INT_STATUS_UNSUPPORTED;

    // D2D uses 0x00RRGGBB not 0x00BBGGRR like COLORREF.
    color = (color & 0xFF) << 16 |
	(color & 0xFF00) |
	(color & 0xFF0000) >> 16;
    RefPtr<ID2D1SolidColorBrush> brush;
    hr = rt->CreateSolidColorBrush(D2D1::ColorF(color, 1.0), &brush);
    if (FAILED(hr))
	return CAIRO_INT_STATUS_UNSUPPORTED;

    if (transform) {
	rt->SetTransform(D2D1::Matrix3x2F(transform->m11,
					  transform->m12,
					  transform->m21,
					  transform->m22,
					  transform->dx,
					  transform->dy));
    }
    rt->BeginDraw();
    rt->DrawGlyphRun(D2D1::Point2F(0, 0), run, brush);
    hr = rt->EndDraw();
    if (transform) {
	rt->SetTransform(D2D1::Matrix3x2F::Identity());
    }
    if (FAILED(hr))
	return CAIRO_INT_STATUS_UNSUPPORTED;

    return CAIRO_INT_STATUS_SUCCESS;
}

/* Surface helper function */
cairo_int_status_t
_cairo_dwrite_show_glyphs_on_surface(void			*surface,
				    cairo_operator_t	 op,
				    const cairo_pattern_t	*source,
				    cairo_glyph_t		*glyphs,
				    int			 num_glyphs,
				    cairo_scaled_font_t	*scaled_font,
				    cairo_clip_t	*clip)
{
    // TODO: Check font & surface for types.
    cairo_dwrite_scaled_font_t *dwritesf = reinterpret_cast<cairo_dwrite_scaled_font_t*>(scaled_font);
    cairo_win32_surface_t *dst = reinterpret_cast<cairo_win32_surface_t*>(surface);
    cairo_int_status_t status;

    /* We can only handle dwrite fonts */
    if (cairo_scaled_font_get_type (scaled_font) != CAIRO_FONT_TYPE_DWRITE)
	return CAIRO_INT_STATUS_UNSUPPORTED;

    /* We can only handle opaque solid color sources */
    if (!_cairo_pattern_is_opaque_solid(source))
	return CAIRO_INT_STATUS_UNSUPPORTED;

    /* We can only handle operator SOURCE or OVER with the destination
     * having no alpha */
    if (op != CAIRO_OPERATOR_SOURCE && op != CAIRO_OPERATOR_OVER)
	return CAIRO_INT_STATUS_UNSUPPORTED;

    /* It is vital that dx values for dxy_buf are calculated from the delta of
     * _logical_ x coordinates (not user x coordinates) or else the sum of all
     * previous dx values may start to diverge from the current glyph's x
     * coordinate due to accumulated rounding error. As a result strings could
     * be painted shorter or longer than expected. */

    AutoDWriteGlyphRun run;
    run.allocate(num_glyphs);

    BOOL transform = FALSE;
    _cairo_dwrite_glyph_run_from_glyphs(glyphs, num_glyphs, dwritesf, &run, &transform);

    cairo_solid_pattern_t *solid_pattern = (cairo_solid_pattern_t *)source;
    COLORREF color = RGB(((int)solid_pattern->color.red_short) >> 8,
		((int)solid_pattern->color.green_short) >> 8,
		((int)solid_pattern->color.blue_short) >> 8);

    DWRITE_MATRIX matrix = _cairo_dwrite_matrix_from_matrix(&dwritesf->mat);

    DWRITE_MATRIX *mat;
    if (transform) {
	mat = &matrix;
    } else {
	mat = NULL;
    }

    RefPtr<IDWriteGlyphRunAnalysis> runAnalysis;
    HRESULT hr = DWriteFactory::Instance()->
	CreateGlyphRunAnalysis(&run, 1, mat,
			       DWRITE_RENDERING_MODE_ALIASED,
			       dwritesf->measuring_mode,
			       0, // baselineOriginX,
			       0, // baselineOriginY,
			       &runAnalysis);
    if (FAILED(hr))
	return CAIRO_INT_STATUS_UNSUPPORTED;
    RECT fontArea;
    hr = runAnalysis->GetAlphaTextureBounds(DWRITE_TEXTURE_ALIASED_1x1, &fontArea);
    if (FAILED(hr))
	return CAIRO_INT_STATUS_UNSUPPORTED;
    InflateRect(&fontArea, 1, 1);
    /* Needed to calculate bounding box for efficient blitting */
    RECT copyArea, dstArea = { 0, 0, dst->extents.width, dst->extents.height };
    IntersectRect(&copyArea, &fontArea, &dstArea);

#ifdef CAIRO_TRY_D2D_TO_GDI
    status = _dwrite_draw_glyphs_to_gdi_surface_d2d(dst,
						    mat,
						    &run,
						    color,
						    copyArea);

    if (status == (cairo_status_t)CAIRO_INT_STATUS_UNSUPPORTED) {
#endif
	status = _dwrite_draw_glyphs_to_gdi_surface_gdi(dst,
							mat,
							&run,
							color,
							dwritesf,
							copyArea);

#ifdef CAIRO_TRY_D2D_TO_GDI
    }
#endif

    return status;
}

/* Check if a specific font table in a DWrite font and a scaled font is identical */
static cairo_int_status_t
compare_font_tables (cairo_dwrite_font_face_t *dwface,
		     cairo_scaled_font_t      *scaled_font,
		     unsigned long             tag,
		     cairo_bool_t             *match)
{
    unsigned long size;
    cairo_int_status_t status;
    unsigned char *buffer = NULL;
    const void *dw_data;
    UINT32 dw_size;
    void *dw_tableContext = NULL;
    BOOL dw_exists = FALSE;
    HRESULT hr;

    hr = dwface->dwriteface->TryGetFontTable(be32_to_cpu (tag),
                                             &dw_data,
                                             &dw_size,
                                             &dw_tableContext,
                                             &dw_exists);
    if (FAILED(hr))
	return _cairo_dwrite_error (hr, "TryGetFontTable failed");

    if (!dw_exists) {
	*match = FALSE;
	status = CAIRO_INT_STATUS_SUCCESS;
	goto cleanup;
    }

    status = scaled_font->backend->load_truetype_table (scaled_font, tag, 0, NULL, &size);
    if (unlikely(status))
	goto cleanup;

    if (size != dw_size) {
	*match = FALSE;
	status = CAIRO_INT_STATUS_SUCCESS;
	goto cleanup;
    }

    buffer = (unsigned char *) _cairo_malloc (size);
    if (unlikely (buffer == NULL)) {
	status = (cairo_int_status_t) _cairo_error (CAIRO_STATUS_NO_MEMORY);
	goto cleanup;
    }

    status = scaled_font->backend->load_truetype_table (scaled_font, tag, 0, buffer, &size);
    if (unlikely(status))
	goto cleanup;

    *match = memcmp (dw_data, buffer, size) == 0;
    status = CAIRO_INT_STATUS_SUCCESS;

cleanup:
    free (buffer);
    if (dw_tableContext)
        dwface->dwriteface->ReleaseFontTable(dw_tableContext);

    return status;
}

/* Check if a DWrite font and a scaled font areis identical
 *
 * DWrite does not allow accessing the entire font data using tag=0 so we compare
 * two of the font tables:
 * - 'name' table
 * - 'head' table since this contains the checksum for the entire font
 */
static cairo_int_status_t
font_tables_match (cairo_dwrite_font_face_t *dwface,
		   cairo_scaled_font_t      *scaled_font,
		   cairo_bool_t             *match)
{
    cairo_int_status_t status;

    status = compare_font_tables (dwface, scaled_font, TT_TAG_name, match);
    if (unlikely(status))
	return status;

    if (!*match)
	return CAIRO_INT_STATUS_SUCCESS;

    status = compare_font_tables (dwface, scaled_font, TT_TAG_head, match);
    if (unlikely(status))
	return status;

    return CAIRO_INT_STATUS_SUCCESS;
}

/*
 * Helper for _cairo_win32_printing_surface_show_glyphs to create a win32 equivalent
 * of a dwrite scaled_font so that we can print using ExtTextOut instead of drawing
 * paths or blitting glyph bitmaps.
 */
cairo_int_status_t
_cairo_dwrite_scaled_font_create_win32_scaled_font (cairo_scaled_font_t *scaled_font,
                                                    cairo_scaled_font_t **new_font)
{
    if (cairo_scaled_font_get_type (scaled_font) != CAIRO_FONT_TYPE_DWRITE) {
        return CAIRO_INT_STATUS_UNSUPPORTED;
    }

    cairo_font_face_t *face = cairo_scaled_font_get_font_face (scaled_font);
    if (cairo_font_face_status (face) == CAIRO_STATUS_SUCCESS &&
	cairo_font_face_get_type (face) == CAIRO_FONT_TYPE_TOY)
    {
	face = ((cairo_toy_font_face_t *)face)->impl_face;
    }

    if (face == NULL || cairo_font_face_get_type (face) != CAIRO_FONT_TYPE_DWRITE) {
        return CAIRO_INT_STATUS_UNSUPPORTED;
    }

    cairo_dwrite_font_face_t *dwface = reinterpret_cast<cairo_dwrite_font_face_t*>(face);

    RefPtr<IDWriteGdiInterop> gdiInterop;
    DWriteFactory::Instance()->GetGdiInterop(&gdiInterop);
    if (!gdiInterop) {
        return CAIRO_INT_STATUS_UNSUPPORTED;
    }

    /* TODO: use scaled font? */

    LOGFONTW logfont;
    if (FAILED(gdiInterop->ConvertFontFaceToLOGFONT (dwface->dwriteface, &logfont))) {
	return CAIRO_INT_STATUS_UNSUPPORTED;
    }

    /* DWrite must have been using an outline font, so we want GDI to use the same,
     * even if there's also a bitmap face available
     */
    logfont.lfOutPrecision = OUT_OUTLINE_PRECIS;

    cairo_font_face_t *win32_face = cairo_win32_font_face_create_for_logfontw (&logfont);
    if (cairo_font_face_status (win32_face)) {
	cairo_font_face_destroy (win32_face);
        return CAIRO_INT_STATUS_UNSUPPORTED;
    }

    cairo_matrix_t font_matrix;
    cairo_scaled_font_get_font_matrix (scaled_font, &font_matrix);

    cairo_matrix_t ctm;
    cairo_scaled_font_get_ctm (scaled_font, &ctm);

    cairo_font_options_t options;
    _cairo_font_options_init_default (&options);
    cairo_scaled_font_get_font_options (scaled_font, &options);

    cairo_scaled_font_t *font = cairo_scaled_font_create (win32_face,
			                                  &font_matrix,
			                                  &ctm,
			                                  &options);
    cairo_font_face_destroy (win32_face);

    if (cairo_scaled_font_status(font)) {
	cairo_scaled_font_destroy (font);
        return CAIRO_INT_STATUS_UNSUPPORTED;
    }

    cairo_bool_t match;
    cairo_int_status_t status;
    status = font_tables_match (dwface, font, &match);
    if (status) {
	cairo_scaled_font_destroy (font);
	return status;
    }

    /* If the font tables aren't equal, then GDI may have failed to
     * find the right font and substituted a different font.
     */
    if (!match) {
#if 0
	char *ps_name;
	char *font_name;
	status = _cairo_truetype_read_font_name (scaled_font, &ps_name, &font_name);
	printf("dwrite fontname: %s PS name: %s\n", font_name, ps_name);
	free (font_name);
	free (ps_name);
	status = _cairo_truetype_read_font_name (font, &ps_name, &font_name);
	printf("win32  fontname: %s PS name: %s\n", font_name, ps_name);
	free (font_name);
	free (ps_name);
#endif
	cairo_scaled_font_destroy (font);
        return CAIRO_INT_STATUS_UNSUPPORTED;
    }

    *new_font = font;
    return CAIRO_INT_STATUS_SUCCESS;
}
