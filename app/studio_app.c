#include "myos_app.h"
#include "astudio.h"

MYOS_APP_META("studio", 16384);

void _start(void) {
    char buf[64];
    as_printf("[studio] hello from astudio, tid=%d\n", as_tid());
    as_printf("[studio] strlen(\"astudio\")=%u strcmp=%d\n",
              (unsigned)as_strlen("astudio"), as_strcmp("abc", "abd"));
    as_printf("[studio] sqrt(123456789)=%d clamp=%d\n",
              as_sqrt_i(123456789), as_clamp(99, 0, 10));
    as_srand(42);
    as_printf("[studio] rand: %u %u %u\n",
              (unsigned)as_rand_range(1, 6), (unsigned)as_rand_range(1, 6), (unsigned)as_rand_range(1, 6));
    as_format_uptime(buf, sizeof(buf), as_uptime_ms());
    as_printf("[studio] uptime=%s\n", buf);
    const uint8_t demo[16] = { 0x41,0x53,0x54,0x55,0x44,0x49,0x4f,0x21,
                               0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07 };
    as_hexdump(demo, 16);
    as_exit(3);
    for (;;) { }
}
