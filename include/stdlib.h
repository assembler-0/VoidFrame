#ifndef VOIDFRAME_STDLIB_H
#define VOIDFRAME_STDLIB_H

inline int ABSi(const int x) {
    if (x < 0) {
        return -x;
    }
    return x;
}

inline double ABSd(const double x) {
    if (x < 0.0) {
        return -x;
    }
    return x;
}

#endif // VOIDFRAME_STDLIB_H
