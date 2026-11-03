/* wasm glue: the ST7789 panel emulated at the firmware's SPI calls, and the linker symbols the
 * firmware names but the host never uses. The FM-1 register windows (0x10000-0x01F00000) are plain
 * zeroed wasm memory below the program's own data (global base 0x02000000). */
typedef unsigned short uint16_t; typedef unsigned char uint8_t; typedef unsigned int uint32_t;
uint32_t _bss_start[1], _bss_end[1], _data_start[1], _data_end[1], _data_load[1], _pool_start[1], _pool_end[1];
uint32_t _rt_start[1], _rt_end[1], _rt_load[1], _etext[1], _guard0[1], _guard1[1];
uint32_t _sstack_lo[1], _sstack_top[1], _ustack_lo[1], _bb_c1_sstack_top[1], _bb_c1_ustack[1], _bb_c1_ustack_top[1];
void isr_alnk0(void) {}
void isr_timer5(void) {}
void bb_c1_entry(void) {}
static uint16_t panel_fb[240 * 240];
static unsigned panel_cmd, panel_x0, panel_x1 = 239, panel_y0, panel_y1 = 239, panel_x, panel_y;
void host_panel_cmd(unsigned c)
{
    panel_cmd = c;
    if (c == 0x2Cu) { panel_x = panel_x0; panel_y = panel_y0; }
}
void host_panel_data(const void *p, unsigned n)
{
    const uint8_t *b = p;
    unsigned i;
    if (panel_cmd == 0x2Au && n >= 4u) {
        panel_x0 = (unsigned)b[0] << 8 | b[1]; panel_x1 = (unsigned)b[2] << 8 | b[3];
    } else if (panel_cmd == 0x2Bu && n >= 4u) {
        panel_y0 = (unsigned)b[0] << 8 | b[1]; panel_y1 = (unsigned)b[2] << 8 | b[3];
    } else if (panel_cmd == 0x2Cu) {
        for (i = 0; i + 1u < n; i += 2u) {
            if (panel_x < 240u && panel_y < 240u) panel_fb[panel_y * 240u + panel_x] = (uint16_t)(b[i] << 8 | b[i + 1u]);
            if (++panel_x > panel_x1) { panel_x = panel_x0; if (++panel_y > panel_y1) panel_y = panel_y0; }
        }
    }
}
const uint16_t *host_panel_fb(void) { return panel_fb; }
