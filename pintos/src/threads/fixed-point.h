#ifndef FIXED_POINT_H
#define FIXED_POINT_H

#include <stdint.h>

// fixed point real number implementation in 17.14 format
#define F (1 << 14)
typedef int fixedpoint_t;

fixedpoint_t int2fp(int n);

fixedpoint_t fp2int(fixedpoint_t x);

fixedpoint_t fp2int_round(fixedpoint_t x);

fixedpoint_t add_fp_int(fixedpoint_t x, int n);

fixedpoint_t sub_fp_int(fixedpoint_t x, int n);

fixedpoint_t mul_fp_int(fixedpoint_t x, int n);

fixedpoint_t div_fp_int(fixedpoint_t x, int n);

fixedpoint_t mul_fp(fixedpoint_t x, fixedpoint_t y);

fixedpoint_t div_fp(fixedpoint_t x, fixedpoint_t y);

fixedpoint_t add_fp(fixedpoint_t x, fixedpoint_t y);

fixedpoint_t sub_fp(fixedpoint_t x, fixedpoint_t y);

#endif /* FIXED_POINT_H */
