
#ifndef __FILTERED_EWA_RESIZE_H
#define __FILTERED_EWA_RESIZE_H

#include "avisynth.h"
#include "EWACore.h"

class FilteredEWAResize : public GenericVideoFilter
{
public:
  FilteredEWAResize(PClip _child, int width, int height,
                    double crop_left, double crop_top, double crop_width, double crop_height,
                    EWACore *func, IScriptEnvironment* env);
  ~FilteredEWAResize();

  PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env);

private:
  EWACore *func;
  int src_width, src_height;
  double crop_left, crop_top, crop_width, crop_height;
};

#endif
