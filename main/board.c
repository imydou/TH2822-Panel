// Board pin/timing facts: Waveshare ESP32-S3-Touch-LCD-4.3 official schematic and 08_lvgl_Porting.
// Official example initialization is CC0-1.0, Espressif Systems 2022.
#include "board.h"
#include "display_geometry.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "sdkconfig.h"
static esp_lcd_panel_handle_t active_panel;
#if CONFIG_PANEL_ROTATE_180
static void (*original_flush)(lv_disp_drv_t *, const lv_area_t *, lv_color_t *);
static void rotated_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *pixels) {
    // full_refresh renders the complete off-screen buffer on every flush.
    assert(area->x1 == 0 && area->y1 == 0 && area->x2 == 799 && area->y2 == 479);
    panel_rotate_180((uint16_t *)pixels, 800 * 480);
    original_flush(drv, area, pixels);
}
#endif
int board_set_pixel_clock(unsigned hz) {
    if (hz != 12000000 && hz != 16000000 && hz != 21000000)
        return ESP_ERR_INVALID_ARG;
    esp_err_t err = esp_lcd_rgb_panel_set_pclk(active_panel, hz);
    ESP_LOGI("board", "PCLK request=%u Hz, nominal frame=%.2f Hz (820x500), result=%s", hz,
             hz / 410000.0, esp_err_to_name(err));
    return err;
}
static void expander(uint8_t value) {
    ESP_ERROR_CHECK(i2c_master_write_to_device(I2C_NUM_0, 0x38, &value, 1, pdMS_TO_TICKS(100)));
}
void board_show_display(void) {
    expander(0x1e);
    ESP_LOGI("board", "Backlight on after initial UI rendering");
}
static void touch_diagnostic(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y,
                             uint16_t *strength, uint8_t *count, uint8_t max_count) {
#if CONFIG_PANEL_ROTATE_180
    for (unsigned i = 0; i < *count; ++i) {
        x[i] = panel_mirror_coordinate(x[i], 800);
        y[i] = panel_mirror_coordinate(y[i], 480);
    }
#endif
    static unsigned samples = 0;
    if (*count && (++samples % 12 == 1)) {
        ESP_LOGI("touch", "GT911 coordinate x=%u y=%u points=%u IRQ=%d", x[0], y[0], *count,
                 gpio_get_level(GPIO_NUM_4));
    }
}
void board_init(void) {
    i2c_config_t i2c = {.mode = I2C_MODE_MASTER,
                        .sda_io_num = 8,
                        .scl_io_num = 9,
                        .sda_pullup_en = true,
                        .scl_pullup_en = true,
                        .master.clk_speed = 400000};
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &i2c));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0));
    uint8_t mode = 1;
    ESP_ERROR_CHECK(i2c_master_write_to_device(I2C_NUM_0, 0x24, &mode, 1, pdMS_TO_TICKS(100)));
    // EXIO5 stays LOW throughout reset and backlight changes (USB, not CAN).
    // EXIO4 SD deselected; EXIO3 LCD reset high; EXIO2 backlight low initially.
    expander(0x18);
    gpio_set_direction(GPIO_NUM_4, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_4, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    expander(0x1a);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_direction(GPIO_NUM_4, GPIO_MODE_INPUT);
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_rgb_panel_config_t cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {.pclk_hz = 21000000,
                    .h_res = 800,
                    .v_res = 480,
                    .hsync_pulse_width = 4,
                    .hsync_back_porch = 8,
                    .hsync_front_porch = 8,
                    .vsync_pulse_width = 4,
                    .vsync_back_porch = 8,
                    .vsync_front_porch = 8,
                    .flags.pclk_active_neg = 1},
        .data_width = 16,
        .bits_per_pixel = 16,
        .num_fbs = 2,
        .bounce_buffer_size_px = 800 * 20,
        .sram_trans_align = 4,
        .psram_trans_align = 64,
        .hsync_gpio_num = 46,
        .vsync_gpio_num = 3,
        .de_gpio_num = 5,
        .pclk_gpio_num = 7,
        .disp_gpio_num = -1,
        .data_gpio_nums = {14, 38, 18, 17, 10, 39, 0, 45, 48, 47, 21, 1, 2, 42, 41, 40},
        .flags.fb_in_psram = 1};
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&cfg, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    active_panel = panel;
    esp_lcd_panel_io_handle_t touch_io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    ESP_ERROR_CHECK(
        esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)I2C_NUM_0, &io_cfg, &touch_io));
    esp_lcd_touch_handle_t touch = NULL;
    esp_lcd_touch_config_t touch_cfg = {.x_max = 800,
                                        .y_max = 480,
                                        .rst_gpio_num = -1,
                                        .int_gpio_num = -1,
                                        .process_coordinates = touch_diagnostic};
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(touch_io, &touch_cfg, &touch));
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    // Keep drawing on the RGB interrupt core to avoid concurrent PSRAM cache traffic.
    port_cfg.task_affinity = xPortGetCoreID();
    ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));
    ESP_LOGI("board", "RGB ISR and LVGL drawing share CPU%d", port_cfg.task_affinity);
    lvgl_port_display_cfg_t display_cfg = {
        .panel_handle = panel,
        .buffer_size = 800 * 480,
        .double_buffer = true,
        .hres = 800,
        .vres = 480,
        .flags = {.buff_spiram = true, .sw_rotate = false, .full_refresh = true}};
    lvgl_port_display_rgb_cfg_t rgb = {.flags = {.bb_mode = true, .avoid_tearing = true}};
    lv_disp_t *display = lvgl_port_add_disp_rgb(&display_cfg, &rgb);
    assert(display);
    lvgl_port_touch_cfg_t input = {.disp = display, .handle = touch};
    assert(lvgl_port_add_touch(&input));
    if (lvgl_port_lock(1000)) {
#if CONFIG_PANEL_ROTATE_180
        original_flush = display->driver->flush_cb;
        display->driver->flush_cb = rotated_flush;
#endif
        lvgl_port_unlock();
    }
    // Keep backlight off until the application has rendered its first frame.
    ESP_LOGI("board",
             "READY 800x480 RGB GT911 poll input; EXIO5=0 USB; UART0 43/44; PCLK requested=21MHz / "
             "nominal51.22Hz / PSRAM free=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}
