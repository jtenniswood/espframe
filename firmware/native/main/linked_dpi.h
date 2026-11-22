#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_lcd_panel_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ESPFRAME_DPI_SEGMENTS 3

// Read after the DMA callback arms the next buffer. A previously displayed
// slot's source pixels can be reused once its replacement is confirmed.
unsigned espframe_dpi_armed_frame(esp_lcd_panel_handle_t panel);
esp_err_t espframe_dpi_submit_frame(esp_lcd_panel_handle_t panel, unsigned index, const uint16_t *owned,
                                   const uint16_t *const *sources, const size_t *lengths,
                                   bool resisted, uint32_t elapsed_us);

typedef struct {
    uint32_t refreshes;
    uint32_t underruns;
    uint32_t submits;
    uint32_t worst_us;
    uint32_t total_us;
    uint32_t painted_bytes;
    uint32_t edge_submits;
    uint32_t edge_total_us;
    uint32_t edge_worst_us;
    uint32_t edge_max_bytes;
} espframe_dpi_report_t;

void espframe_dpi_read_report(espframe_dpi_report_t *report);

#ifdef __cplusplus
}
#endif
