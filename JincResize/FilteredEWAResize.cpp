
#include <math.h>
#include <stdint.h>
#include "FilteredEWAResize.h"

// Features
#define USE_C
#define USE_SSE2
#define USE_SSE3
#define USE_AVX2
#define USE_FMA3

#include "EWAResizer.h"

/************************************************************************/
/* EWACore implementation                                               */
/************************************************************************/

const int LUT_SIZE = 8192;

void EWACore::InitLutTable()
{
  lut = new float[LUT_SIZE];

  float filter_end = GetSupport()*GetSupport();
  lut_factor = ((float) (LUT_SIZE - 16)) / filter_end;

  for (int i = 0; i < LUT_SIZE; i++) {
    lut[i] = factor((float)i / lut_factor);
  }
}

void EWACore::DestroyLutTable()
{
  delete[] lut;
}

inline float EWACore::GetFactor(float dist)
{
  int index = int(dist*lut_factor);

  if (index >= LUT_SIZE)
    return 0;

  return lut[index];
}

/************************************************************************/
/* FilteredEWAResize implementation                                     */
/************************************************************************/

FilteredEWAResize::FilteredEWAResize(PClip _child, int width, int height, double crop_left, double crop_top, double crop_width, double crop_height,
                                     int quant_x, int quant_y, bool version, EWACore *func, IScriptEnvironment* env) :
  GenericVideoFilter(_child),
  func(func),
  crop_left(crop_left), crop_top(crop_top), crop_width(crop_width), crop_height(crop_height),
  stored_coeff_u(nullptr), stored_coeff_v(nullptr), stored_coeff_y(nullptr)
{
  if (version) {
    // Show instruction set it compiles with
    env->ThrowError(
      "[Jinc Resizer] [%d] Compiled Instruction Set: "
# ifdef USE_FMA3
      "FMA3 "
# endif
# ifdef USE_AVX2
      "AVX2 "
# endif
# ifdef USE_SSE3
      "SSE3 "
# endif
# ifdef USE_SSE2
      "SSE2 "
# endif
# ifdef USE_C
      "x86"
# endif
      , get_supported_instruction());
  }

  if (!vi.IsPlanar() || !vi.IsYUV()) {
    env->ThrowError("JincResize: Only planar YUV colorspaces are supported");
  }

  if (vi.width < int(ceil(2*func->GetSupport())) || vi.height < int(ceil(2*func->GetSupport()))) {
    env->ThrowError("JincResize: Source image too small.");
  }

  src_width = vi.width;
  src_height = vi.height;

  vi.width = width;
  vi.height = height;

  func->InitLutTable();

  // Generate resizing core
  stored_coeff_y = new EWAPixelCoeff;
  generate_coeff_table_c(func, stored_coeff_y, quant_x, quant_y, src_width, src_height, vi.width, vi.height, crop_left, crop_top, crop_width, crop_height);

  // Select the EWA Resize core
  resizer_y = GetResizer(stored_coeff_y->filter_size, env);

  if (!vi.IsY8()) {
    int subsample_w = vi.GetPlaneWidthSubsampling(PLANAR_U);
    int subsample_h = vi.GetPlaneHeightSubsampling(PLANAR_U);

    double div_w = 1 << subsample_w;
    double div_h = 1 << subsample_h;

    stored_coeff_u = new EWAPixelCoeff;
    generate_coeff_table_c(func, stored_coeff_u, quant_x, quant_y,
                           src_width >> subsample_w, src_height >> subsample_h, vi.width >> subsample_w, vi.height >> subsample_h,
                           crop_left / div_w, crop_top / div_h, crop_width / div_w, crop_height / div_h);

    stored_coeff_v = new EWAPixelCoeff;
    generate_coeff_table_c(func, stored_coeff_v, quant_x, quant_y,
                           src_width >> subsample_w, src_height >> subsample_h, vi.width >> subsample_w, vi.height >> subsample_h,
                           crop_left / div_w, crop_top / div_h, crop_width / div_w, crop_height / div_h);

    resizer_u = GetResizer(stored_coeff_u->filter_size, env);
    resizer_v = GetResizer(stored_coeff_v->filter_size, env);
  }

}

FilteredEWAResize::~FilteredEWAResize()
{
  func->DestroyLutTable();
  delete func;

  delete_coeff_table(stored_coeff_y);
  delete_coeff_table(stored_coeff_u);
  delete_coeff_table(stored_coeff_v);

  delete stored_coeff_y;
  delete stored_coeff_u;
  delete stored_coeff_v;
}

PVideoFrame __stdcall FilteredEWAResize::GetFrame(int n, IScriptEnvironment* env)
{
  PVideoFrame src = child->GetFrame(n, env);
  PVideoFrame dst = env->NewVideoFrame(vi);
    
  resizer_y(stored_coeff_y,
    dst->GetWritePtr(), src->GetReadPtr(), dst->GetPitch(), src->GetPitch(),
    src_width, src_height, vi.width, vi.height,
    crop_left, crop_top, crop_width, crop_height
    );
      

  if (!vi.IsY8()) {
    int subsample_w = vi.GetPlaneWidthSubsampling(PLANAR_U);
    int subsample_h = vi.GetPlaneHeightSubsampling(PLANAR_U);

    double div_w = 1 << subsample_w;
    double div_h = 1 << subsample_h;
      
    resizer_u(stored_coeff_u,
      dst->GetWritePtr(PLANAR_U), src->GetReadPtr(PLANAR_U), dst->GetPitch(PLANAR_U), src->GetPitch(PLANAR_U),
      src_width >> subsample_w, src_height >> subsample_h, vi.width >> subsample_w, vi.height >> subsample_h,
      crop_left / div_w, crop_top / div_h, crop_width / div_w, crop_height / div_h
      );

    resizer_v(stored_coeff_v,
      dst->GetWritePtr(PLANAR_V), src->GetReadPtr(PLANAR_V), dst->GetPitch(PLANAR_V), src->GetPitch(PLANAR_V),
      src_width >> subsample_w, src_height >> subsample_h, vi.width >> subsample_w, vi.height >> subsample_h,
      crop_left / div_w, crop_top / div_h, crop_width / div_w, crop_height / div_h
      );
  }

  return dst;
}


EWAResizeCore FilteredEWAResize::GetResizer(int filter_size, IScriptEnvironment* env)
{
  int CPU = get_supported_instruction();

#if defined(USE_FMA3) && defined(USE_FMA3)
  if ((CPU & EWARESIZE_AVX2) && (CPU & EWARESIZE_FMA3)) {
    switch (filter_size) {

#define size(n)  \
    case n: return resize_plane_avx<n, EWARESIZE_AVX2 | EWARESIZE_FMA3>; break;

      size(3); size(4); size(5);
      size(6); size(7); size(8); size(9); size(10);
      size(11); size(12); size(13); size(14); size(15);
      size(16); size(17); size(18); size(19); size(20);

#undef size

    default:
      env->ThrowError("JincResize: Internal error; filter size '%d' is not supported.", filter_size);
    }
  }
#endif

#ifdef USE_AVX2
  if (CPU & EWARESIZE_AVX2) {
    switch (filter_size) {

#define size(n)  \
    case n: return resize_plane_avx<n, EWARESIZE_AVX2>; break;

      size(3); size(4); size(5);
      size(6); size(7); size(8); size(9); size(10);
      size(11); size(12); size(13); size(14); size(15);
      size(16); size(17); size(18); size(19); size(20);

#undef size

    default:
      env->ThrowError("JincResize: Internal error; filter size '%d' is not supported.", filter_size);
    }
  }
#endif

#ifdef USE_SSE3
  if (CPU & EWARESIZE_SSE3) {
    switch (filter_size) {

#define size(n)  \
    case n: return resize_plane_sse<n, EWARESIZE_SSE3>; break;

      size(3); size(4); size(5);
      size(6); size(7); size(8); size(9); size(10);
      size(11); size(12); size(13); size(14); size(15);
      size(16); size(17); size(18); size(19); size(20);

#undef size

    default:
      env->ThrowError("JincResize: Internal error; filter size '%d' is not supported.", filter_size);
    }
  }
#endif

#ifdef USE_SSE2
  if (CPU & EWARESIZE_SSE2) {
    switch (filter_size) {

#define size(n)  \
    case n: return resize_plane_sse<n, EWARESIZE_SSE2>; break;

      size(3); size(4); size(5);
      size(6); size(7); size(8); size(9); size(10);
      size(11); size(12); size(13); size(14); size(15);
      size(16); size(17); size(18); size(19); size(20);

#undef size

    default:
      env->ThrowError("JincResize: Internal error; filter size '%d' is not supported.", filter_size);
    }
  }
#endif

#ifdef USE_C
  return resize_plane_c;
#else
  env->ThrowError("JincResize: No supported instruction set found. Supported: "
# ifdef USE_AVX2
                  "AVX2, "
# endif
# ifdef USE_SSE3
                  "SSE3, "
# endif
# ifdef USE_SSE2
                  "SSE2, "
# endif
# ifdef USE_C
                  "x86"
# else
                  " and no other"
# endif
                  );
#endif
}

#if !defined(USE_AVX2) && !defined(USE_SSE3) && !defined(USE_SSE2) && !defined(USE_C)
# error Why are you compling this plugin without any supported instruction?
#endif

