#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void board_init(void);
void board_show_display(void);
int board_set_pixel_clock(unsigned hz);
#ifdef __cplusplus
}
#endif
