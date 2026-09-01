#include "led.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"

#define LED_GPIO          21
#define LED_RESOLUTION_HZ 10000000   // 100 ns per tick RMT

static const char *TAG = "led";

static rmt_channel_handle_t s_chan = NULL;
static rmt_encoder_handle_t s_enc = NULL;

// Master attivo -> verde, non attivo -> rosso.
// Il WS2812 di bordo della S3-Zero usa ordine GRB.
static void led_show(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_chan) return;
    uint8_t data[3] = { g, r, b };
    rmt_transmit(s_chan, s_enc, data, sizeof(data), &(rmt_transmit_config_t){0});
}

void led_set_master(bool on)
{
    if (on) {
        led_show(0, 64, 0);
    } else {
        led_show(64, 0, 0);
    }
    ESP_LOGI(TAG, "led master %s", on ? "verde" : "rosso");
}

void led_init(void)
{
    rmt_tx_channel_config_t cfg = {
        .gpio_num = LED_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = LED_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 1,
    };
    if (rmt_new_tx_channel(&cfg, &s_chan) != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_tx_channel fallito");
        return;
    }

    rmt_bytes_encoder_config_t ec = {
        // '0': high 400 ns, low 800 ns; '1': high 800 ns, low 400 ns.
        .bit0 = { .duration0 = 4, .level0 = 1, .duration1 = 8, .level1 = 0 },
        .bit1 = { .duration0 = 8, .level0 = 1, .duration1 = 4, .level1 = 0 },
        .flags = { .msb_first = 1 },
    };
    if (rmt_new_bytes_encoder(&ec, &s_enc) != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_bytes_encoder fallito");
        return;
    }

    rmt_enable(s_chan);
    led_set_master(false);
}