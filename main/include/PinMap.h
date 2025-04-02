#ifndef __PIN_MAP_H__
#define __PIN_MAP_H__

// ESP32-WROOM-32D & ESP32-WROOM-32U
// https://www.espressif.com/sites/default/files/documentation/esp32-wroom-32d_esp32-wroom-32u_datasheet_en.pdf
#ifdef CONFIG_IDF_TARGET_ESP32

// Default pinout for Penumbra
// https://github.com/reeltwo/PenumbraShadowMD/blob/main/PenumbraSchematic.pdf

// GPIO next to USB Host port
#define PIN_DOUT13              GPIO_NUM_13 // pin 16 - GPIO13, ADC2_CH4, TOUCH4, RTC_GPIO14, MTCK, HSPID, HS2_DATA3, SD_DATA3, EMAC_RX_ER
#define PIN_DOUT14              GPIO_NUM_14 // pin 13 - GPIO14, ADC2_CH6, TOUCH6, RTC_GPIO16, MTMS, HSPICLK, HS2_CLK, SD_CLK, EMAC_TXD2
#define PIN_DIN35               GPIO_NUM_35 // pin  7 - (INPUT ONLY) GPIO35, ADC1_CH7, RTC_GPIO5
#define PIN_DIN34               GPIO_NUM_34 // pin  6 - (INPUT ONLY) GPIO34, ADC1_CH6, RTC_GPIO4

// UART 0: SERIAL1 
// Shared with Serial/USB Conversion CH340C and USB Console for Bluepad32 ??
// Join solder pad to disable boot messages being sent to TXD0
#define PIN_SERIAL1_RX          GPIO_NUM_3  // pin 34 - GPIO3, U0RXD, CLK_OUT2
#define PIN_SERIAL1_TX          GPIO_NUM_1  // pin 35 - GPIO1, U0TXD, CLK_OUT3, EMAC_RXD2

// UART 1: SERIAL2
// Shared with RS485
// SHD/SD2 (disconnected) pin 17 - GPIO9, SD_DATA2, SPIHD, HS1_DATA2, U1RXD
// SWP/SD3 (disconnected) pin 18 - GPIO10, SD_DATA3, SPIWP, HS1_DATA3, U1TXD
// connected to the integrated SPI flash integrated on the module and are not recommended for other uses.
#define PIN_SERIAL2_RX          GPIO_NUM_33 // pin  9 - GPIO33, XTAL_32K_N (32.768 kHz crystal oscillator output), ADC1_CH5, TOUCH8, RTC_GPIO8
#define PIN_SERIAL2_TX          GPIO_NUM_25 // pin 10 - GPIO25, DAC_1, ADC2_CH8, RTC_GPIO6, EMAC_RXD0

// UART 2: SERIAL3
// ERRATA: Pin 17 is not connected. Serial2 can either be RX or TX but not both.
#define PIN_SERIAL3_RX          GPIO_NUM_16 // pin 27 - GPIO16, HS1_DATA4, U2RXD, EMAC_CLK_OUT
#define PIN_SERIAL3_TX          GPIO_NUM_17 // pin 28 - GPIO17, HS1_DATA5, U2TXD, EMAC_CLK_OUT_180

// UART 3: SERIAL4
#define PIN_SERIAL4_RX          GPIO_NUM_32 // pin  8 - GPIO32, XTAL_32K_P (32.768 kHz crystal oscillator input), ADC1_CH4, TOUCH9, RTC_GPIO9
#define PIN_SERIAL4_TX          GPIO_NUM_4  // pin 26 - GPIO4, ADC2_CH0, TOUCH0, RTC_GPIO10, HSPIHD, HS2_DATA1, SD_DATA1, EMAC_TX_ER

// GPIO next to antenna
// If not using PCA9685 Controller
#define PIN_OUTPUT_ENABLE       GPIO_NUM_27 // pin 12 - GPIO27, ADC2_CH7, TOUCH7, RTC_GPIO17, EMAC_RX_DV
#define PIN_SCL                 GPIO_NUM_22 // pin 36 - GPIO22, VSPIWP, U0RTS, EMAC_TXD1
#define PIN_SDA                 GPIO_NUM_21 // pin 33 - GPIO21, VSPIHD, EMAC_TX_EN

// GPIO next to RS485
#define PIN_RS485_RTS           GPIO_NUM_26 // pin 11 - GPIO26, DAC_2, ADC2_CH9, RTC_GPIO7, EMAC_RXD1

#else
// Assign pins for Other ESP32
// TODO:

#endif

// Assign pins for C1-10P

#define PIN_SABERTOOTH_TX       PIN_SERIAL3_RX

#define PIN_MP3TRIGGER_RX       PIN_SCL
#define PIN_MP3TRIGGER_TX       PIN_SDA

#define PIN_MAESTRO_DOME_RX     PIN_DOUT13
#define PIN_MAESTRO_DOME_TX     PIN_DOUT14

#define PIN_MAESTRO_BODY_RX     PIN_SERIAL4_RX
#define PIN_MAESTRO_BODY_TX     PIN_SERIAL4_TX

#define PIN_OPENMV_RX           PIN_SERIAL2_RX
#define PIN_OPENMV_TX           PIN_SERIAL2_TX

#define PIN_LED_FRONT           PIN_OUTPUT_ENABLE
#define PIN_LED_BACK            PIN_OUTPUT_ENABLE

#define PIN_DOME_POTENTIOMETER  PIN_DIN34

// Map pins to UART ports
#define UART_SABERTOOTH         sabertoothSerial
#define UART_MAESTRO_DOME       maestroDomeSerial
#define UART_MAESTRO_BODY       maestroBodySerial
#define UART_MP3TRIGGER         mp3TriggerSerial
#define UART_OPENMV             Serial2

// Macros for initalizing the Serial Ports
#define UART_INITIALIZE_WAIT(uart)      if (!uart) { while (1) { Console.println("Invalid Serial pin configuration, check config"); delay (1000);}} 

#define UART_MAESTRO_DOME_INIT(baud)    { UART_MAESTRO_DOME.begin(baud, SWSERIAL_8N1, PIN_MAESTRO_DOME_RX, PIN_MAESTRO_DOME_TX, false); UART_INITIALIZE_WAIT(UART_MAESTRO_DOME); }
#define UART_MAESTRO_BODY_INIT(baud)    { UART_MAESTRO_BODY.begin(baud, SWSERIAL_8N1, PIN_MAESTRO_BODY_RX, PIN_MAESTRO_BODY_TX, false); UART_INITIALIZE_WAIT(UART_MAESTRO_BODY); }
#define UART_SABERTOOTH_INIT(baud)      { UART_SABERTOOTH.begin(baud, SWSERIAL_8N1, NOT_A_PIN, PIN_SABERTOOTH_TX, false); UART_INITIALIZE_WAIT(UART_SABERTOOTH); }
#define UART_MP3TRIGGER_INIT(baud)      { UART_MP3TRIGGER.begin(baud, SWSERIAL_8N1, PIN_MP3TRIGGER_RX, PIN_MP3TRIGGER_TX, false); delay(1500); UART_INITIALIZE_WAIT(UART_MP3TRIGGER); }
#define UART_OPENMV_INIT(baud)          { UART_OPENMV.begin(baud, SERIAL_8N1, PIN_OPENMV_RX, PIN_OPENMV_TX); UART_INITIALIZE_WAIT(UART_OPENMV); }


#include <SoftwareSerial.h>
EspSoftwareSerial::UART UART_SABERTOOTH;
EspSoftwareSerial::UART UART_MAESTRO_BODY;
EspSoftwareSerial::UART UART_MAESTRO_DOME;
EspSoftwareSerial::UART UART_MP3TRIGGER;

#endif