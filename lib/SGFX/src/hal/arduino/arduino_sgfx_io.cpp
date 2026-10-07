#if defined(SGFX_HAL_ARDUINO_GENERIC) && defined(SGFX_BUS_SPI)

#include <Arduino.h>
#include <SPI.h>
extern "C" {
  #include "sgfx.h"
  #include "sgfx_port.h"
}

/* Required pins via build flags */
#ifndef SGFX_PIN_CS
#  error "SGFX_PIN_CS must be defined"
#endif
#ifndef SGFX_PIN_DC
#  error "SGFX_PIN_DC must be defined"
#endif

#ifndef SGFX_SPI_HZ
#  define SGFX_SPI_HZ 40000000
#endif

#ifndef SGFX_ARDUINO_SPI_INSTANCE
#  define SGFX_ARDUINO_SPI_INSTANCE SPI
#endif

static bool _spi_inited = false;
static inline void _lazy_hw_begin(){
  if (_spi_inited) return;

  /* Configure control pins */
  pinMode(SGFX_PIN_CS, OUTPUT);  digitalWrite(SGFX_PIN_CS, HIGH);
  pinMode(SGFX_PIN_DC, OUTPUT);  digitalWrite(SGFX_PIN_DC, HIGH);

  /* Optional pins */
  #ifdef SGFX_PIN_BL
  if (SGFX_PIN_BL >= 0){ pinMode(SGFX_PIN_BL, OUTPUT); digitalWrite(SGFX_PIN_BL, HIGH); }
  #endif
  #ifdef SGFX_PIN_RST
  if (SGFX_PIN_RST >= 0){
    pinMode(SGFX_PIN_RST, OUTPUT);
    digitalWrite(SGFX_PIN_RST, HIGH); delay(1);
    digitalWrite(SGFX_PIN_RST, LOW);  delay(5);
    digitalWrite(SGFX_PIN_RST, HIGH); delay(5);
  }
  #endif

  /* Start SPI on the requested pins */
  #ifdef SGFX_PIN_SCK
    #ifdef SGFX_PIN_MISO
      #ifdef SGFX_PIN_MOSI
        SGFX_ARDUINO_SPI_INSTANCE.begin(SGFX_PIN_SCK, SGFX_PIN_MISO, SGFX_PIN_MOSI, SGFX_PIN_CS);
      #else
        SGFX_ARDUINO_SPI_INSTANCE.begin(SGFX_PIN_SCK, SGFX_PIN_MISO);
      #endif
    #else
      SGFX_ARDUINO_SPI_INSTANCE.begin(SGFX_PIN_SCK, -1, SGFX_PIN_MOSI, SGFX_PIN_CS);
    #endif
  #else
    SGFX_ARDUINO_SPI_INSTANCE.begin();
  #endif

  _spi_inited = true;
}

extern "C" int sgfx_cmd8(sgfx_device_t*, uint8_t cmd){
  _lazy_hw_begin();
  SGFX_ARDUINO_SPI_INSTANCE.beginTransaction(SPISettings(SGFX_SPI_HZ, MSBFIRST, SPI_MODE0));
  digitalWrite(SGFX_PIN_CS, LOW);
  digitalWrite(SGFX_PIN_DC, LOW);               // command
  SGFX_ARDUINO_SPI_INSTANCE.transfer(cmd);
  digitalWrite(SGFX_PIN_CS, HIGH);
  SGFX_ARDUINO_SPI_INSTANCE.endTransaction();
  return 0;
}

extern "C" int sgfx_cmdn(sgfx_device_t*, uint8_t cmd, const uint8_t* data, size_t n){
  _lazy_hw_begin();
  SGFX_ARDUINO_SPI_INSTANCE.beginTransaction(SPISettings(SGFX_SPI_HZ, MSBFIRST, SPI_MODE0));
  digitalWrite(SGFX_PIN_CS, LOW);
  digitalWrite(SGFX_PIN_DC, LOW);               // command
  SGFX_ARDUINO_SPI_INSTANCE.transfer(cmd);
  if (n && data){
    digitalWrite(SGFX_PIN_DC, HIGH);            // data
    SGFX_ARDUINO_SPI_INSTANCE.transfer((void*)data, n);
  }
  digitalWrite(SGFX_PIN_CS, HIGH);
  SGFX_ARDUINO_SPI_INSTANCE.endTransaction();
  return 0;
}

extern "C" int sgfx_data(sgfx_device_t*, const void* bytes, size_t n){
  _lazy_hw_begin();
  SGFX_ARDUINO_SPI_INSTANCE.beginTransaction(SPISettings(SGFX_SPI_HZ, MSBFIRST, SPI_MODE0));
  digitalWrite(SGFX_PIN_CS, LOW);
  digitalWrite(SGFX_PIN_DC, HIGH);              // data
  SGFX_ARDUINO_SPI_INSTANCE.transfer((void*)bytes, n);
  digitalWrite(SGFX_PIN_CS, HIGH);
  SGFX_ARDUINO_SPI_INSTANCE.endTransaction();
  return 0;
}

extern "C" void sgfx_delay_ms(uint32_t ms){ delay(ms); }

#endif /* SGFX_HAL_ARDUINO_GENERIC && SGFX_BUS_SPI */
