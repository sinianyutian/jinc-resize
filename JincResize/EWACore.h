
#ifndef __EWACORE_H
#define __EWACORE_H

template<typename T>
inline T clamp(T a, T b, T c)
{
  return a > b ? a : (b > c ? c : b);
}

/*
 * Base Class for EWA Resampler Core
 */
class EWACore
{
public:
  virtual float GetSupport() = 0;
  virtual ~EWACore() {};
  float GetFactor(float dist);
  void InitLutTable();
  void DestroyLutTable();

  float* lut;
  float lut_factor;

protected:
  virtual float factor(float dist) = 0;

private:
};

#endif
