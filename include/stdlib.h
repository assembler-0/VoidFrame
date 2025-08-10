#ifndef VOIDFRAME_STDLIB_H
#define VOIDFRAME_STDLIB_H

static inline int ABSi(const int x) {
    if (x < 0) {
        return -x;
    }
    return x;
}

static inline double ABSd(const double x) {
    if (x < 0.0) {
        return -x;
    }
    return x;
}

static inline int atoi(const char* str) {
    int result = 0;
    int sign = 1;
    int i = 0;

    // Skip leading whitespace
    while (str[i] == ' ' || str[i] == '\t' || str[i] == '\n' ||
           str[i] == '\r' || str[i] == '\f' || str[i] == '\v') {
        i++;
           }

    // Handle sign
    if (str[i] == '-') {
        sign = -1;
        i++;
    } else if (str[i] == '+') {
        i++;
    }

    // Convert digits
    while (str[i] >= '0' && str[i] <= '9') {
        result = result * 10 + (str[i] - '0');
        i++;
    }

    return result * sign;
}

#endif // VOIDFRAME_STDLIB_H
