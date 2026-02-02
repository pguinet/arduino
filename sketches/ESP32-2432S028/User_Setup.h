// User_Setup.h - Configuration TFT_eSPI pour ESP32-2432S028 (Cheap Yellow Display)
//
// Copier ce fichier dans la bibliothèque TFT_eSPI :
//   cp sketches/ESP32-2432S028/User_Setup.h ~/Arduino/libraries/TFT_eSPI/User_Setup.h

#define USER_SETUP_INFO "ESP32-2432S028-CYD"

// Driver ILI9341
#define ILI9341_2_DRIVER

// Résolution
#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// Pins écran LCD (SPI HSPI)
#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST  -1
#define TFT_BL   21

// Utiliser le port HSPI
#define USE_HSPI_PORT

// Fréquences SPI
#define SPI_FREQUENCY       55000000
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY  2500000

// Polices
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT
