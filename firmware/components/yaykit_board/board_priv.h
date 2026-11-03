// Inside yaykit_board: what its files share with each other (not for apps).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/** One read of the touch controller's report (registers 0x01..0x06). ESP_ERR_INVALID_STATE without touch. */
esp_err_t board_touch_read(uint8_t report[6]);
/** While the fast reader runs: its latest state, so LVGL need not read the controller again. False otherwise. */
bool board_touch_fast_point(bool *pressed, int32_t *x, int32_t *y);
/** Once, from board_init(): the SW6106's 5 V charge current down to 0.5 A (0x3A bits 5-3, the rest as read), read
 * back and logged as one YK_BOARD_CHARGE line. A failure is logged once and never stops the start. */
void board_charge_limit(void);
