// Board variant — ELECROW CrowPanel Advance 7" ESP32-P4 (DHE04107D).
//
// Copied from the framework's stock `esp32p4` variant with exactly ONE line changed:
// BOARD_SDIO_ESP_HOSTED_RESET, 54 -> 32.
//
// WHY THIS FILE EXISTS. Arduino ignores the ESP-Hosted pins in sdkconfig. esp32-hal-hosted.c seeds
// sdio_pin_config from these BOARD_SDIO_ESP_HOSTED_* macros, then overwrites the Kconfig values
// before esp_hosted_sdio_set_config():
//
//     conf.pin_reset.pin = sdio_pin_config.pin_reset;   // clobbers CONFIG_..._RESET_SLAVE=32
//
// The stock esp32p4 variant describes Espressif's Function EV Board. Its CLK/CMD/D0/D1
// (18/19/14/15) match the CrowPanel exactly — which is why Wi-Fi worked at all, and why this took
// so long to find — but its reset pin is 54. With 54 the C6 is NEVER reset, so a warm P4 reset
// leaves it running a stale ESP-Hosted session: the card re-enumerates, the first register read
// times out (sdmmc_send_cmd 0x107), and esp_hosted deliberately reboots the host — forever, since
// the reboot cannot reset the C6. Only physically removing power recovered it.
//
// GPIO32 is net C6_EN (IC1.EN, 10k pull-up, active high) on Elecrow's own Eagle schematic.
//
// Doing it here rather than calling WiFi.setPins() from app code means no future code path has to
// remember to call it before the first Wi-Fi access (setPins is refused once ESP-Hosted has
// initialised, and the failure is silent unless you check its return).
//
// !! PCB REVISION !! The data lines below are for 7" **V1.0**. On V1.1/V1.2 they are REVERSED:
// d0=17, d1=16, d2=15, d3=14. The reset pin stays 32 on every revision. The revision is printed
// on the top silkscreen.

#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>
#include "soc/soc_caps.h"

// BOOT_MODE 35
// BOOT_MODE2 36 pullup

static const uint8_t TX = 37;
static const uint8_t RX = 38;

static const uint8_t SDA = 7;
static const uint8_t SCL = 8;

// GPIO 36 and below: no extra on-chip LDO needed for the IO bank.
// GPIO 39-48: LDO VO4 (channel 4). GPIO 37-38 (UART) are outside that VO4 range.
static const uint8_t SS = 26;
static const uint8_t MOSI = 32;
static const uint8_t MISO = 33;
static const uint8_t SCK = 36;

static const uint8_t A0 = 16;
static const uint8_t A1 = 17;
static const uint8_t A2 = 18;
static const uint8_t A3 = 19;
static const uint8_t A4 = 20;
static const uint8_t A5 = 21;
static const uint8_t A6 = 22;
static const uint8_t A7 = 23;
static const uint8_t A8 = 49;
static const uint8_t A9 = 50;
static const uint8_t A10 = 51;
static const uint8_t A11 = 52;
static const uint8_t A12 = 53;
static const uint8_t A13 = 54;

static const uint8_t T0 = 2;
static const uint8_t T1 = 3;
static const uint8_t T2 = 4;
static const uint8_t T3 = 5;
static const uint8_t T4 = 6;
static const uint8_t T5 = 7;
static const uint8_t T6 = 8;
static const uint8_t T7 = 9;
static const uint8_t T8 = 10;
static const uint8_t T9 = 11;
static const uint8_t T10 = 12;
static const uint8_t T11 = 13;
static const uint8_t T12 = 14;
static const uint8_t T13 = 15;

/* ESP32-P4 EV Function board specific definitions */
//ETH
#define ETH_PHY_TYPE    ETH_PHY_TLK110
#define ETH_PHY_ADDR    1
#define ETH_PHY_MDC     31
#define ETH_PHY_MDIO    52
#define ETH_PHY_POWER   51
#define ETH_RMII_TX_EN  49
#define ETH_RMII_TX0    34
#define ETH_RMII_TX1    35
#define ETH_RMII_RX0    29
#define ETH_RMII_RX1_EN 30
#define ETH_RMII_CRS_DV 28
#define ETH_RMII_CLK    50
#define ETH_CLK_MODE    EMAC_CLK_EXT_IN

//SDMMC
#define BOARD_HAS_SDMMC
#define BOARD_SDMMC_SLOT           0
#define BOARD_SDMMC_POWER_CHANNEL  4
#define BOARD_SDMMC_POWER_PIN      45
#define BOARD_SDMMC_POWER_ON_LEVEL LOW

// On-chip GP LDO: periman enables VO4 when a GPIO in the range is used (see esp32-hal-ldo.c).
#define BOARD_PERIMAN_IO_LDO_AUTO        1
#define BOARD_PERIMAN_IO_LDO0_CHANNEL    4   // LDO_VO4 on ESP32-P4
#define BOARD_PERIMAN_IO_LDO0_GPIO_MIN   39  // Function EV: GPIO 39-48 on VO4
#define BOARD_PERIMAN_IO_LDO0_GPIO_MAX   48
#define BOARD_PERIMAN_IO_LDO0_VOLTAGE_MV 3300

//WIFI - ESP32C6
#define BOARD_HAS_SDIO_ESP_HOSTED
#define BOARD_SDIO_ESP_HOSTED_CLK   18
#define BOARD_SDIO_ESP_HOSTED_CMD   19
#define BOARD_SDIO_ESP_HOSTED_D0    14
#define BOARD_SDIO_ESP_HOSTED_D1    15
#define BOARD_SDIO_ESP_HOSTED_D2    16
#define BOARD_SDIO_ESP_HOSTED_D3    17
#define BOARD_SDIO_ESP_HOSTED_RESET 32  // CrowPanel: net C6_EN (stock EV board = 54)

#endif /* Pins_Arduino_h */
