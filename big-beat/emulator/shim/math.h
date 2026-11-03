#pragma once
float sinf(float); float cosf(float); float tanf(float); float expf(float); float logf(float); float powf(float, float);
float sqrtf(float); float fabsf(float); float floorf(float); float ceilf(float); float fmodf(float, float); float tanhf(float);
float exp2f(float); float log2f(float); float log10f(float); float atanf(float); float atan2f(float,float); float roundf(float);
float truncf(float); float fminf(float,float); float fmaxf(float,float); float lrintf_f(float); long lrintf(float);
double sin(double); double cos(double); double exp(double); double log(double); double pow(double, double); double sqrt(double);
double fabs(double); double floor(double); double ceil(double); double fmod(double, double); double tanh(double); double tan(double);
double log10(double); double atan(double); double atan2(double,double); double exp2(double); double log2(double); double round(double);
#define M_PI 3.14159265358979323846
#define INFINITY __builtin_inff()
#define NAN __builtin_nanf("")
#define isnan(x) __builtin_isnan(x)
#define isinf(x) __builtin_isinf(x)
#define isfinite(x) __builtin_isfinite(x)
#define HUGE_VALF __builtin_huge_valf()
