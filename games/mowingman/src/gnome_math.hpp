#pragma once
// JavaScript's Math functions as V8 computes them (v8/src/base/ieee754.cc, ports of
// fdlibm / FreeBSD msun), so the gnome's voice is computed bit for bit as Node computes
// it, natively and in the browser build, whatever the platform's libm. Only the
// functions the gnome's voice calls are here.
namespace mm::js {

double exp(double x);
double expm1(double x);
double tanh(double x);
double sin(double x);
double cos(double x);
double tan(double x);
double pow(double x, double y);
double log2(double x);
// Math.round: the nearest integer, halves rounded up (toward +infinity).
double round(double x);

}  // namespace mm::js
