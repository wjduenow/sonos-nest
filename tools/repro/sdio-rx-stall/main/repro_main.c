/*
 * Minimal reproducer for espressif/esp-hosted-mcu#243.
 *
 * ESP32-P4 host + ESP32-C6 co-processor, SDIO streaming RX mode. When the host cannot grow its
 * SDIO RX buffer — an INTERNAL|DMA allocation, from a pool that is small on the P4 — the driver
 * logs
 *
 *     W (…) H_SDIO_DRV: RX buffer alloc failed (len=…); dropping read
 *
 * exactly once, and never receives from the co-processor again. Host->co-processor traffic keeps
 * flowing, so the symptom upstairs is only RPC timeouts.
 *
 * WHAT THIS PROGRAM DOES
 *   1. Joins Wi-Fi through the co-processor.
 *   2. Occupies DMA-capable internal memory until the largest free block is just under the size
 *      of a full streaming read, leaving REPRO_TARGET_DMA_FREE_KB. On a real product this state
 *      arrives on its own, from whatever else wanted internal RAM; here it is deliberate so the
 *      fault reproduces in seconds rather than hours.
 *   3. Listens on REPRO_TCP_PORT and sinks everything sent to it (send_bulk.py).
 *   4. Prints one line a second: bytes received, rate, and the DMA-capable pool. When the byte
 *      count stops moving while the sender is still connected, it prints STALL DETECTED.
 *
 * WHAT TO LOOK FOR
 *   healthy : rx= grows every second, rate is a few Mbit/s.
 *   the bug : one "RX buffer alloc failed" line, then rx= frozen forever, then STALL DETECTED.
 *             Only resetting the co-processor recovers it.
 *
 * The knobs that matter are in sdkconfig.defaults: streaming RX mode, MEMPOOL_PREFER_SPIRAM
 * disabled (the default), and the co-processor's SDIO Tx queue size, which sets how large a
 * single read can be.
 */
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#define REPRO_TCP_PORT              5001
#define REPRO_TARGET_DMA_FREE_KB    CONFIG_REPRO_TARGET_DMA_FREE_KB
#define REPRO_STALL_SECONDS         10
#define REPRO_HOG_CHUNK             1024

static const char *TAG = "repro";

static volatile uint64_t s_rx_bytes;      /* written by the sink task, read by the reporter */
static volatile int64_t  s_last_rx_us;
static volatile bool     s_peer_connected;

/* ---- the DMA-capable internal pool -------------------------------------------------------- */
/* This is the pool the SDIO RX buffer comes from. Note it is NOT what heap_caps_get_*_size(
 * MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL) reports, which is why ordinary heap metrics never
 * predicted this failure on our product. */
static size_t dma_free(void)    { return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA); }
static size_t dma_largest(void) { return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA); }

/* Hold the blocks so they are never freed; the point is to keep the pool small for the whole run. */
static void hog_dma_memory(size_t leave_bytes)
{
    size_t taken = 0, blocks = 0;
    ESP_LOGI(TAG, "DMA-internal before: free %u, largest %u", (unsigned)dma_free(), (unsigned)dma_largest());
    while (dma_free() > leave_bytes) {
        size_t want = dma_free() - leave_bytes;
        if (want > REPRO_HOG_CHUNK) want = REPRO_HOG_CHUNK;
        void *p = heap_caps_malloc(want, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        if (!p) break;
        taken += want;
        blocks++;
    }
    ESP_LOGI(TAG, "held %u B in %u blocks; DMA-internal now: free %u, largest %u",
             (unsigned)taken, (unsigned)blocks, (unsigned)dma_free(), (unsigned)dma_largest());
    ESP_LOGW(TAG, "a full streaming read is (co-processor SDIO Tx queue size) * 1536 B — with the"
                  " default 20 that is 30720 B, which no longer fits");
}

/* ---- TCP sink ------------------------------------------------------------------------------ */
static void sink_task(void *arg)
{
    int listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (listen_fd < 0) { ESP_LOGE(TAG, "socket: errno %d", errno); vTaskDelete(NULL); }
    int yes = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(REPRO_TCP_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(listen_fd, 1) != 0) {
        ESP_LOGE(TAG, "bind/listen: errno %d", errno);
        close(listen_fd);
        vTaskDelete(NULL);
    }
    ESP_LOGI(TAG, "sink listening on port %d — run: python3 send_bulk.py <this ip>", REPRO_TCP_PORT);

    static char buf[4096];   /* static: this task's own buffer must not add to the pressure */
    for (;;) {
        struct sockaddr_in peer;
        socklen_t plen = sizeof(peer);
        int fd = accept(listen_fd, (struct sockaddr *)&peer, &plen);
        if (fd < 0) { vTaskDelay(pdMS_TO_TICKS(200)); continue; }
        ESP_LOGI(TAG, "sender connected from %s", inet_ntoa(peer.sin_addr));
        s_peer_connected = true;
        s_last_rx_us = esp_timer_get_time();
        for (;;) {
            int n = recv(fd, buf, sizeof(buf), 0);
            if (n > 0) { s_rx_bytes += (uint64_t)n; s_last_rx_us = esp_timer_get_time(); continue; }
            if (n == 0) ESP_LOGW(TAG, "sender closed the connection");
            else        ESP_LOGW(TAG, "recv: errno %d", errno);
            break;
        }
        s_peer_connected = false;
        close(fd);
    }
}

/* ---- one line a second, and the verdict ---------------------------------------------------- */
static void report_task(void *arg)
{
    uint64_t last = 0;
    bool announced = false;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        const uint64_t now_bytes = s_rx_bytes;
        const uint64_t delta = now_bytes - last;
        last = now_bytes;
        ESP_LOGI(TAG, "rx=%llu B (+%llu B/s = %.2f Mbit/s)  dma-internal: free %u largest %u",
                 (unsigned long long)now_bytes, (unsigned long long)delta, (double)delta * 8 / 1e6,
                 (unsigned)dma_free(), (unsigned)dma_largest());

        if (!s_peer_connected) { announced = false; continue; }
        const int64_t idle_us = esp_timer_get_time() - s_last_rx_us;
        if (idle_us > (int64_t)REPRO_STALL_SECONDS * 1000000 && !announced) {
            announced = true;
            ESP_LOGE(TAG, "STALL DETECTED — no bytes for %d s while the sender is still connected.",
                     REPRO_STALL_SECONDS);
            ESP_LOGE(TAG, "Look above for: H_SDIO_DRV: RX buffer alloc failed (len=...); dropping read");
            ESP_LOGE(TAG, "Host->co-processor still works (the sender's TCP ACKs stop, its socket"
                          " buffer fills, and only a co-processor reset recovers RX).");
        }
    }
}

/* ---- Wi-Fi through the co-processor -------------------------------------------------------- */
static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START)              esp_wifi_connect();
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)  esp_wifi_connect();
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "got ip " IPSTR " — sink on port %d", IP2STR(&e->ip_info.ip), REPRO_TCP_PORT);
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_wifi, NULL));

    wifi_config_t wc = { 0 };
    strncpy((char *)wc.sta.ssid, CONFIG_REPRO_WIFI_SSID, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, CONFIG_REPRO_WIFI_PASSWORD, sizeof(wc.sta.password) - 1);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* After the transport is up, so the driver's own buffers are allocated first and this leaves
     * the pool exactly where a long-running application would. */
    vTaskDelay(pdMS_TO_TICKS(5000));
    hog_dma_memory((size_t)REPRO_TARGET_DMA_FREE_KB * 1024);

    xTaskCreate(sink_task,   "sink",   4096, NULL, 5, NULL);
    xTaskCreate(report_task, "report", 4096, NULL, 4, NULL);
}
