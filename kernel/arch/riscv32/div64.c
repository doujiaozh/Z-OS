#include "types.h"

u64 __udivdi3(u64 numerator, u64 denominator) {
    u64 quotient = 0;
    u64 remainder = 0;
    if (denominator == 0) {
        return 0;
    }
    for (int bit = 63; bit >= 0; bit--) {
        remainder <<= 1;
        remainder |= (numerator >> bit) & 1;
        if (remainder >= denominator) {
            remainder -= denominator;
            quotient |= ((u64)1) << bit;
        }
    }
    return quotient;
}

u64 __umoddi3(u64 numerator, u64 denominator) {
    u64 remainder = 0;
    if (denominator == 0) {
        return 0;
    }
    for (int bit = 63; bit >= 0; bit--) {
        remainder <<= 1;
        remainder |= (numerator >> bit) & 1;
        if (remainder >= denominator) {
            remainder -= denominator;
        }
    }
    return remainder;
}
