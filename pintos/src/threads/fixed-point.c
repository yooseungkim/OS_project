#include "threads/fixed-point.h"

fixedpoint_t int2fp(int n) {
    return n * F;
}

fixedpoint_t fp2int(fixedpoint_t x) {
    return x / F;
}

fixedpoint_t fp2int_round(fixedpoint_t x) {
    if (x >=0) return (x + F/2) / F;
    else return (x - F/2) / F;
}

fixedpoint_t add_fp_int(fixedpoint_t x, int n) {
    return x + n * F;
}

fixedpoint_t sub_fp_int(fixedpoint_t x, int n) {
    return x - n * F;
}

fixedpoint_t mul_fp_int(fixedpoint_t x, int n) {
    return x * n;
}

fixedpoint_t div_fp_int(fixedpoint_t x, int n) {
    return x / n;
}

fixedpoint_t mul_fp(fixedpoint_t x, fixedpoint_t y) {
    return ((int64_t)x * y) / F;
}

fixedpoint_t div_fp(fixedpoint_t x, fixedpoint_t y) {
    return ((int64_t)x * F) / y;
}

fixedpoint_t add_fp(fixedpoint_t x, fixedpoint_t y) {
    return x + y;
}

fixedpoint_t sub_fp(fixedpoint_t x, fixedpoint_t y) {
    return x - y;
}
