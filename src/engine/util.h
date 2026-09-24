#ifndef ENGINE_UTIL_H
#define ENGINE_UTIL_H

// Number of elements of an array, as an int (menu and table counts). Passing
// a pointer does not compile: the second term's array size is then -1.
#define ARRAY_LEN(a) \
    ((int)(sizeof(a) / sizeof((a)[0]) + \
           0 * sizeof(char[1 - 2 * __builtin_types_compatible_p(__typeof__(a), __typeof__(&(a)[0]))])))

#endif
