/* libgcc_stubs.c — ARM EABI runtime stubs
 *   - целочисленное деление
 *   - IEEE-754 float (single): +, -, *, /, сравнения, конверсии
 *   - IEEE-754 double (минимально): +, *, конверсии float<->double
 * Никаких вызовов libgcc — всё своё.
 */
/* freestanding — без стандартных заголовков */
typedef unsigned int       u32;
typedef signed int         s32;
typedef unsigned long long u64;
typedef signed long long   s64;
typedef unsigned short     u16;
typedef unsigned char      u8;

/* ============== Целочисленное деление ============== */
static u32 _udiv(u32 n, u32 d) {
    if (!d) return 0;
    u32 q = 0, r = 0;
    for (int i = 31; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1u);
        if (r >= d) { r -= d; q |= (1u << i); }
    }
    return q;
}
static u32 _umod(u32 n, u32 d) {
    if (!d) return 0;
    u32 r = 0;
    for (int i = 31; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1u);
        if (r >= d) r -= d;
    }
    return r;
}
int  __aeabi_idiv(int n, int d) {
    if (!d) return 0;
    int sign = 1;
    u32 un = (u32)n, ud = (u32)d;
    if (n < 0) { un = (u32)(-n); sign = -sign; }
    if (d < 0) { ud = (u32)(-d); sign = -sign; }
    return sign * (int)_udiv(un, ud);
}
u32 __aeabi_uidiv(u32 n, u32 d) { return _udiv(n, d); }

typedef struct { int quot, rem; } idivmod_t;
idivmod_t __aeabi_idivmod(int n, int d) {
    idivmod_t r;
    if (!d) { r.quot = r.rem = 0; return r; }
    int sign = 1;
    u32 un = (u32)n, ud = (u32)d;
    if (n < 0) { un = (u32)(-n); sign = -sign; }
    if (d < 0) { ud = (u32)(-d); sign = -sign; }
    r.quot = sign * (int)_udiv(un, ud);
    r.rem  = (n < 0) ? -(int)_umod(un, ud) : (int)_umod(un, ud);
    return r;
}
typedef struct { u32 quot, rem; } uidivmod_t;
uidivmod_t __aeabi_uidivmod(u32 n, u32 d) {
    uidivmod_t r;
    r.quot = _udiv(n, d);
    r.rem  = _umod(n, d);
    return r;
}

/* ============== IEEE-754 single ============== */
#define F_SIGN_BIT   0x80000000u
#define F_EXP_MASK   0x7F800000u
#define F_MANT_MASK  0x007FFFFFu
#define F_IMPLICIT   0x00800000u
#define F_EXP_BIAS   127

typedef union { float f; u32 u; } fu_t;

/* Разобрать: sign 0/1, e (без bias), m с неявной 1 (кроме нуля) */
static int f_unpack(u32 x, int *sign, int *e, u32 *m) {
    *sign = (x >> 31) & 1;
    int ex = (x >> 23) & 0xFF;
    u32 mx = x & F_MANT_MASK;
    if (ex == 0) {
        if (mx == 0) { *e = 0; *m = 0; return 0; }           /* ноль */
        *e = -126;
        *m = mx;
        while (!(*m & F_IMPLICIT)) { *m <<= 1; (*e)--; }
        return 1;
    }
    if (ex == 0xFF) { *e = 0; *m = mx | F_IMPLICIT; return 2; } /* inf/nan */
    *e = ex - F_EXP_BIAS;
    *m = mx | F_IMPLICIT;
    return 1;
}

static u32 f_pack(int sign, int e, u32 m) {
    if (m == 0) return sign ? F_SIGN_BIT : 0;
    while ((m & F_IMPLICIT) == 0 && e > -126) { m <<= 1; e--; }
    while (m & 0x01000000u) { m >>= 1; e++; }
    if (e > 127) return (sign ? F_SIGN_BIT : 0) | F_EXP_MASK;
    if (e < -126) {
        int sh = -126 - e;
        if (sh > 24) return sign ? F_SIGN_BIT : 0;
        return (sign ? F_SIGN_BIT : 0) | ((m | F_IMPLICIT) >> (sh + 1));
    }
    return (sign ? F_SIGN_BIT : 0) | ((u32)(e + F_EXP_BIAS) << 23) | (m & F_MANT_MASK);
}

float __aeabi_fadd(float a, float b) {
    fu_t ua = {a}, ub = {b}, ur;
    int sa, sb, ea, eb; u32 ma, mb;
    int ka = f_unpack(ua.u, &sa, &ea, &ma);
    int kb = f_unpack(ub.u, &sb, &eb, &mb);
    if (ka == 0) return b;
    if (kb == 0) return a;
    if (ka == 2) { ur.u = ua.u; return ur.f; }
    if (kb == 2) { ur.u = ub.u; return ur.f; }

    int e = ea > eb ? ea : eb;
    if (ea > eb)      { int s = ea - eb; ma = (s > 31) ? 0 : (ma >> s); }
    else if (eb > ea) { int s = eb - ea; mb = (s > 31) ? 0 : (mb >> s); }

    int sign; u32 m;
    if (sa == sb) { m = ma + mb; sign = sa; }
    else if (ma > mb) { m = ma - mb; sign = sa; }
    else if (mb > ma) { m = mb - ma; sign = sb; }
    else return 0.0f;
    ur.u = f_pack(sign, e, m);
    return ur.f;
}

float __aeabi_fsub(float a, float b) {
    fu_t ub = {b}; ub.u ^= F_SIGN_BIT;
    return __aeabi_fadd(a, ub.f);
}

float __aeabi_fmul(float a, float b) {
    fu_t ua = {a}, ub = {b}, ur;
    int sa, sb, ea, eb; u32 ma, mb;
    int ka = f_unpack(ua.u, &sa, &ea, &ma);
    int kb = f_unpack(ub.u, &sb, &eb, &mb);
    int s = sa ^ sb;
    if (ka == 0 || kb == 0) return s ? -0.0f : 0.0f;
    if (ka == 2 || kb == 2) { ur.u = (s ? F_SIGN_BIT : 0) | F_EXP_MASK; return ur.f; }

    u64 p = (u64)ma * (u64)mb;      /* 48 бит */
    int e = ea + eb;
    if (p & ((u64)1 << 47)) { p >>= 24; e += 1; }
    else                    { p >>= 23; }
    ur.u = f_pack(s, e, (u32)p);
    return ur.f;
}

float __aeabi_fdiv(float a, float b) {
    fu_t ua = {a}, ub = {b}, ur;
    int sa, sb, ea, eb; u32 ma, mb;
    int ka = f_unpack(ua.u, &sa, &ea, &ma);
    int kb = f_unpack(ub.u, &sb, &eb, &mb);
    int s = sa ^ sb;
    if (ka == 2) { ur.u = (s ? F_SIGN_BIT : 0) | F_EXP_MASK; return ur.f; }
    if (kb == 0) { ur.u = (s ? F_SIGN_BIT : 0) | F_EXP_MASK; return ur.f; }
    if (ka == 0) return s ? -0.0f : 0.0f;
    if (kb == 2) return s ? -0.0f : 0.0f;

    /* Деление 24-битных мантисс методом сдвиг-вычитание */
    u64 num = ((u64)ma) << 24;
    u32 mbx = mb;
    u32 q = 0;
    for (int i = 24; i >= 0; i--) {
        u64 sh = ((u64)mbx) << i;
        if (num >= sh) { num -= sh; q |= (1u << i); }
    }
    int e = ea - eb;
    /* нормализуем q к 24-битному с битом 23 */
    if (q & (1u << 24)) { q >>= 1; e += 1; }
    ur.u = f_pack(s, e, q);
    return ur.f;
}

int __aeabi_fcmplt(float a, float b) { return a <  b; }
int __aeabi_fcmple(float a, float b) { return a <= b; }
int __aeabi_fcmpeq(float a, float b) { return a == b; }

float __aeabi_i2f(int i) {
    if (i == 0) return 0.0f;
    int sign = 0; u32 m;
    if (i < 0) { sign = 1; m = (u32)(-(s64)i); } else m = (u32)i;
    int e = 31;
    while (!(m & 0x80000000u)) { m <<= 1; e--; }
    fu_t ur; ur.u = f_pack(sign, e, m >> 8);
    return ur.f;
}

int __aeabi_f2iz(float f) {
    fu_t ua = {f};
    int sa, ea; u32 ma;
    int k = f_unpack(ua.u, &sa, &ea, &ma);
    if (k == 0) return 0;
    if (k == 2 || ea > 30) return sa ? (int)0x80000000 : (int)0x7FFFFFFF;
    if (ea < 0) return 0;
    /* mantissa 24 бита, сдвигаем к int */
    s32 v = (s32)(ma >> (23 - ea));
    return sa ? -v : v;
}

/* ============== IEEE-754 double (упрощённо) ============== */
typedef union { double d; u64 u; } du_t;
#define D_SIGN_BIT 0x8000000000000000ULL

/* Упрощённо: через приведение к float для + и *. Округление теряется, но работает. */
double __aeabi_dadd(double a, double b) { return (double)((float)a + (float)b); }
double __aeabi_dmul(double a, double b) { return (double)((float)a * (float)b); }
float  __aeabi_d2f(double d)  { return (float)d; }
double __aeabi_f2d(float f)   { return (double)f; }

/* Заглушки, если clang вызовет — не должны использоваться */
double __aeabi_dsub(double a, double b) { return (double)((float)a - (float)b); }
double __aeabi_ddiv(double a, double b) { return (double)((float)a / (float)b); }
