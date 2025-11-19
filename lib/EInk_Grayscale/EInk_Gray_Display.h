// EInk Grayscale Display definitions

#ifndef _EINK_GRAY_H_
#define _EINK_GRAY_H_

#include <Arduino.h>
#include <SPI.h>

// Color definitions
#define GxEPD_BLACK 0x0000
#define GxEPD_DARKGREY 0x7BEF  /* 128, 128, 128 */
#define GxEPD_LIGHTGREY 0xC618 /* 192, 192, 192 */
#define GxEPD_WHITE 0xFFFF

namespace EInk_Gray {
enum Panel {
  GDEQ0426T82  // 4.26" display (only panel we support)
};
}

#endif
