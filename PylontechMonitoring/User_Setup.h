#define ILI9341_2_DRIVER

// CYD panel (ESP32-2432S028R): boots with inverted colors unless the inversion
// command is sent. If the background still looks white, try TFT_INVERSION_OFF.
#define TFT_INVERSION_ON

#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// Display SPI pins (ESP32-2432S028R)
//
// USE_HSPI_PORT is mandatory: the CYD SD card is wired to the VSPI default pins
// (MOSI=23, MISO=19, SCLK=18, CS=5), and SD.begin() drives VSPI. TFT_eSPI without
// this define would also claim VSPI and call spi.begin(14,12,13,-1), and the last
// spi.begin() wins the GPIO matrix for the whole bus — so the display and the SD
// card would fight over the same peripheral. USE_HSPI_PORT puts TFT_eSPI on SPI2
// (HSPI/FSPI), leaving SPI3 (VSPI) exclusively for the SD card. Both peripherals
// route through the GPIO matrix independently, so no mutex is needed.
#define USE_HSPI_PORT

#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST  -1   // The display reset is tied to the ESP32 EN pin

// Backlight
#define TFT_BL            21    // Change to 27 if your board is CYD2USB (USB-C)
#define TFT_BACKLIGHT_ON  HIGH

// IMPORTANT — XPT2046 touch (ESP32-2432S028R):
// On this board the touch does NOT share the display SPI bus; it uses its own
// bit-banged bus from the sketch (py_display.cpp): CLK=25, MOSI=32, MISO=39,
// CS=33, IRQ=36. TOUCH_CS only reserves the pin in TFT_eSPI; the actual read is
// done by PyDisplay on those pins (guided calibration on first boot if the touch
// is not calibrated in NVS).

// Fonts (the ones your sketch uses)
#define LOAD_GLCD
#define LOAD_FONT2
#define SMOOTH_FONT

// SPI speeds
#define SPI_FREQUENCY        40000000
#define SPI_READ_FREQUENCY   20000000
#define SPI_TOUCH_FREQUENCY  2500000

// For when you want to use the touch (XPT2046)
#define TOUCH_CS 33
