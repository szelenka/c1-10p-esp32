#ifndef __PIN_MAP_H__
#define __PIN_MAP_H__

#define PENUMBRA_BOARD
#ifdef PENUMBRA_BOARD

// #define USE_USB
// Default pinout for Penumbra
// ref: 
// https://github.com/reeltwo/PenumbraShadowMD/blob/main/PenumbraSchematic.pdf
// https://www.espressif.com/sites/default/files/documentation/esp32-wroom-32d_esp32-wroom-32u_datasheet_en.pdf
#ifndef SDA
#define SDA                     21
#endif
#ifndef SLC
#define SLC                     22
#endif
#ifndef RX
#define RX                      3
#endif
#ifndef TX
#define TX                      1
#endif

#define DIN1_PIN                34 // GPI34
#define DIN2_PIN                35 // GPIO35

#define DOUT1_PIN               14 // GPIO14
#define DOUT2_PIN               13 // ADC1_CH5

#define RXD0_PIN                RX // U0RXD
#define TXD0_PIN                TX // U0TXD
#define RXD1_PIN                33
#define TXD1_PIN                25
#define RXD2_PIN                16 // U2RXD
#define TXD2_PIN                17 // U2TXD
#define RXD3_PIN                32
#define TXD3_PIN                4

#define OUTPUT_ENABLE_PIN       27
#define RS485_RTS_PIN           26

// Assign pins for C1-10P

#define PIN_SABERTOOTH_TX       DIN1_PIN

#define PIN_MP3TRIGGER_RX       RXD1_PIN
#define PIN_MP3TRIGGER_TX       TXD1_PIN

#define PIN_MAESTRO_BODY_RX     RXD2_PIN
#define PIN_MAESTRO_BODY_TX     TXD2_PIN

#define PIN_MAESTRO_DOME_RX     RXD3_PIN
#define PIN_MAESTRO_DOME_TX     TXD3_PIN

#define PIN_OPENMV_RX           RXD0_PIN
#define PIN_OPENMV_TX           TXD0_PIN

#define PIN_LED_FRONT           DIN2_PIN
#define PIN_LED_BACK            DOUT1_PIN

#define PIN_DOME_POTENTIOMETER  DOUT2_PIN

#else
// Assign pins for Generic ESP32
// TODO:

#endif

#define TANK_DRIVE_ID           129
#define DOME_DRIVE_ID           128
#endif
