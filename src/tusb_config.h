/*
 * TinyUSB Configuration - picoamp-2 + HID
 * Based on BambooMaster's picoamp-2
 */

#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "usb_descriptors.h"

//--------------------------------------------------------------------+
// Board Specific Configuration
//--------------------------------------------------------------------+

#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT      0
#endif

#ifndef BOARD_TUD_MAX_SPEED
#define BOARD_TUD_MAX_SPEED   OPT_MODE_DEFAULT_SPEED
#endif

//--------------------------------------------------------------------
// Common Configuration
//--------------------------------------------------------------------

#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS           OPT_OS_NONE
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG        0
#endif

#define CFG_TUD_ENABLED       1
#define CFG_TUD_MAX_SPEED     BOARD_TUD_MAX_SPEED

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN        __attribute__ ((aligned(4)))
#endif

//--------------------------------------------------------------------
// DEVICE CONFIGURATION
//--------------------------------------------------------------------

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE    64
#endif

//------------- CLASS -------------//
#define CFG_TUD_CDC               0
#define CFG_TUD_MSC               0
#define CFG_TUD_HID               1
#define CFG_TUD_MIDI              0
#define CFG_TUD_AUDIO             1
#define CFG_TUD_VENDOR            0

//--------------------------------------------------------------------
// HID CLASS CONFIGURATION
//--------------------------------------------------------------------
#define CFG_TUD_HID_EP_BUFSIZE    64

//--------------------------------------------------------------------
// AUDIO CLASS DRIVER CONFIGURATION
//--------------------------------------------------------------------

#define CFG_TUD_AUDIO_FUNC_1_DESC_LEN                                TUD_AUDIO_HEADSET_STEREO_DESC_LEN

#define CFG_TUD_AUDIO_FUNC_1_N_FORMATS                               3

#define CFG_TUD_AUDIO_FUNC_1_MAX_SAMPLE_RATE                         48000
#define CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX                           0
#define CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_RX                           2

// 16bit in 16bit slots
#define CFG_TUD_AUDIO_FUNC_1_FORMAT_1_N_BYTES_PER_SAMPLE_RX          2
#define CFG_TUD_AUDIO_FUNC_1_FORMAT_1_RESOLUTION_RX                  16

// 24bit in 24bit slots
#define CFG_TUD_AUDIO_FUNC_1_FORMAT_2_N_BYTES_PER_SAMPLE_RX          3
#define CFG_TUD_AUDIO_FUNC_1_FORMAT_2_RESOLUTION_RX                  24

// 32bit in 32bit slots
#define CFG_TUD_AUDIO_FUNC_1_FORMAT_3_N_BYTES_PER_SAMPLE_RX          4
#define CFG_TUD_AUDIO_FUNC_1_FORMAT_3_RESOLUTION_RX                  32

#define CFG_TUD_AUDIO_ENABLE_EP_OUT               1

#define CFG_TUD_AUDIO_FUNC_1_FORMAT_1_EP_SZ_OUT   TUD_AUDIO_EP_SIZE(CFG_TUD_AUDIO_FUNC_1_MAX_SAMPLE_RATE, CFG_TUD_AUDIO_FUNC_1_FORMAT_1_N_BYTES_PER_SAMPLE_RX, CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_RX)
#define CFG_TUD_AUDIO_FUNC_1_FORMAT_2_EP_SZ_OUT   TUD_AUDIO_EP_SIZE(CFG_TUD_AUDIO_FUNC_1_MAX_SAMPLE_RATE, CFG_TUD_AUDIO_FUNC_1_FORMAT_2_N_BYTES_PER_SAMPLE_RX, CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_RX)
#define CFG_TUD_AUDIO_FUNC_1_FORMAT_3_EP_SZ_OUT   TUD_AUDIO_EP_SIZE(CFG_TUD_AUDIO_FUNC_1_MAX_SAMPLE_RATE, CFG_TUD_AUDIO_FUNC_1_FORMAT_3_N_BYTES_PER_SAMPLE_RX, CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_RX)

#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ     CFG_TUD_AUDIO_FUNC_1_FORMAT_3_EP_SZ_OUT*2
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX        CFG_TUD_AUDIO_FUNC_1_FORMAT_3_EP_SZ_OUT

#define CFG_TUD_AUDIO_FUNC_1_N_AS_INT             2

#define CFG_TUD_AUDIO_FUNC_1_CTRL_BUF_SZ        64

#define CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP    1

#ifdef __cplusplus
}
#endif

#endif
