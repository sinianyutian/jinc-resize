
#include <math.h>
#include "FilteredEWAResize.h"

FilteredEWAResize::FilteredEWAResize(PClip _child, int width, int height, EWACore *func, IScriptEnvironment* env) :
  GenericVideoFilter(_child),
  func( func )
{
  if (!vi.IsPlanar() || !vi.IsYUV()) {
    env->ThrowError("JincResize: planar YUV data only!");
  }

  if (width < vi.width || height < vi.height) {
    env->ThrowError("JincResize: java.lang.NotImplementedException");
  }

  src_width = vi.width;
  src_height = vi.height;

  vi.width = width;
  vi.height = height;
}

FilteredEWAResize::~FilteredEWAResize()
{
  delete func;
}

PVideoFrame __stdcall FilteredEWAResize::GetFrame(int n, IScriptEnvironment* env)
{
  PVideoFrame src = child->GetFrame(n, env);
  PVideoFrame dst = env->NewVideoFrame(vi);

  try{
    ResizePlane(dst->GetWritePtr(), src->GetReadPtr(), dst->GetPitch(), src->GetPitch());
  } catch (int d) {
    env->ThrowError("Error! %d", d);
  }

  return dst;
}

void FilteredEWAResize::ResizePlane(BYTE* dst, const BYTE* src, int dst_pitch, int src_pitch)
{
  double filter_support = func->GetSupport();
  int filter_size = ceil(filter_support * 2.0);

  double start_x = (src_width - vi.width) / (vi.width*2);
  double start_y = (src_height - vi.height) / (vi.height*2);

  double x_step = src_width / vi.width;
  double y_step = src_height / vi.height;

  double ypos = start_y;
  for (int y = 0; y < vi.height; y++) {
    double xpos = start_x;

    for (int x = 0; x < vi.width; x++) {

      int window_end_x = int(xpos + filter_support);
      int window_end_y = int(ypos + filter_support);

      if (window_end_x >= src_width)
        window_end_x = src_width-1;

      if (window_end_y >= src_height)
        window_end_y = src_height-1;

      int window_begin_x = window_end_x - filter_size + 1;
      int window_begin_y = window_end_y - filter_size + 1;

      if (window_begin_x < 0)
        window_begin_x = 0;

      if (window_begin_y < 0)
        window_begin_y = 0;

      float result = 0.0;
      float divider = 0.0;

      double current_x = xpos < 0 ? 0 : (xpos > (src_width-1) ? (src_width-1) : xpos);
      double current_y = ypos < 0 ? 0 : (ypos > (src_height-1) ? (src_height-1) : ypos);

      int window_y = window_begin_y;
      for (int lx = 0; lx < filter_size; lx++) {
        int window_x = window_begin_x;
        for (int ly = 0; ly < filter_size; ly++) {
          double dx = (current_x-window_x)*(current_x-window_x);
          double dy = (current_y-window_y)*(current_y-window_y);

          double dist_sqr = dx + dy;

          float src_data = (src+window_y*src_pitch)[window_x];
          float factor = func->GetFactor(dist_sqr);
          result += src_data * factor;
          divider += factor;

          window_x++;
        }

        window_y++;
      }
      
      dst[x] = min(255, max(0, int((result/divider)+0.5)));

      xpos += x_step;
    }

    dst += dst_pitch;
    ypos += y_step;
  }
}
