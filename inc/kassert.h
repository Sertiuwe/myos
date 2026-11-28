#ifndef KASSERT_H
#define KASSERT_H
#include "kpanic.h"
#define kassert(x) do { \
    if (!(x)) kpanic("assertion failed: %s (%s:%d)", #x, __FILE__, __LINE__); \
} while (0)
#endif
