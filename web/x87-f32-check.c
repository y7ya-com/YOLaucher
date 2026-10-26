// x87 single-precision rounding: softfloat (Boxedwine's path) vs the native cast, round-to-nearest, non-NaN inputs
// Checks boxedwine-x87-fast.patch. Build against Boxedwine's own softfloat (SF = <Boxedwine>/lib/softfloat/source):
//   cc -O2 -I$SF/include -I$SF -I$SF/8086-SSE -o x87-f32-check x87-f32-check.c $SF/*.c $SF/8086-SSE/*.c -w && ./x87-f32-check
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "platform.h"
#include "softfloat.h"
static uint64_t s = 0x9E3779B97F4A7C15ull;
static uint64_t rnd(void) { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
static int check(double d) {
  if (d != d) return 0;
  float64_sf in; memcpy(&in.v, &d, 8);
  softfloat_roundingMode = softfloat_round_near_even;
  uint32_t a = extF80_to_f32(f64_to_extF80(in)).v;
  float f = (float)d; uint32_t b; memcpy(&b, &f, 4);
  if (a != b) { printf("MISMATCH %a soft=%08x native=%08x\n", d, a, b); return 1; }
  return 0;
}
int main(void) {
  int bad = 0; long n = 0;
  double edge[] = {0.0, -0.0, 1.0, -1.0, 3.4028234663852886e38, 3.4028235677973366e38, 3.402823567797337e38, 1e39, -1e39,
    1.1754943508222875e-38, 1.401298464324817e-45, 7.006492321624085e-46, 7.006492321624086e-46, 2.1019476964872256e-45,
    1e-50, -1e-50, INFINITY, -INFINITY, 0.1, 1.0/3, 16777217.0, 16777219.0};
  for (unsigned i = 0; i < sizeof edge / sizeof *edge; i++, n++) bad += check(edge[i]);
  for (long i = 0; i < 10000000; i++, n++) { uint64_t b = rnd(); double d; memcpy(&d, &b, 8); bad += check(d); }   // any bit pattern
  for (long i = 0; i < 10000000; i++, n++) {                                                                          // near-float range, halfway-ish cases
    float f; uint32_t fb = (uint32_t)rnd(); memcpy(&f, &fb, 4); if (f != f) continue;
    double d = (double)f + (double)f * ldexp((double)(int64_t)(rnd() >> 40) - 8388608.0, -48); bad += check(d); }
  printf("%ld inputs, %d mismatches\n", n, bad); return bad != 0;
}
