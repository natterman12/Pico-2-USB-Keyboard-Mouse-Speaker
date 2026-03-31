/*
 * Pico Amp 2 + HID Composite Device
 * USB Audio Speaker + HID Keyboard + Mouse Scroll
 * 
 * Based on picoamp-2 by sctanf/BambooMaster (USB Audio)
 * With HID from pico_composite_device (buttons, rotary, LEDs)
 * 
 * Pins:
 *   GP0, GP1   - I2C (ADS1115)
 *   GP2-GP6    - Hat switch (F1-F5)
 *   GP15       - Space button
 *   GP17       - LED 1 (always on)
 *   GP18       - LED 2 (blinks on button press)
 *   GP19       - I2S LRCLK (clock_pin_base)
 *   GP20       - I2S BCLK (clock_pin_base + 1)
 *   GP21       - I2S DIN (data)
 *   GP22       - I2S MCLK
 *   GP25       - Onboard LED (status)
 */

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"

#include "bsp/board_api.h"
#include "tusb.h"
#include "usb_descriptors.h"

#include "i2s.h"

//--------------------------------------------------------------------
// Pin Definitions
//--------------------------------------------------------------------
// Hat switch buttons
#define HAT_UP          2   // F1
#define HAT_RIGHT       3   // F2
#define HAT_DOWN        4   // F3
#define HAT_LEFT        5   // F4
#define HAT_CENTER      6   // F5

// Other buttons
#define BTN_SPACE       15  // Space key

// LEDs
#define LED_PIN_1       17   // External LED 1 (always on)
#define LED_PIN_2       18   // External LED 2 (blinks on button press)
#define LED_PWM_LEVEL   64   // 25% duty cycle

// I2S pins
#define PIN_I2S_DATA    21
#define PIN_I2S_LRCLK   19
#define PIN_I2S_MCLK    22

// I2C
#define I2C_SDA         0
#define I2C_SCL         1
#define ADS1115_ADDR    0x48

//--------------------------------------------------------------------
// Timing
//--------------------------------------------------------------------
#define DEBOUNCE_MS             100
#define ROTARY_DEBOUNCE_MS      150
#define ROTARY_THRESHOLD        3000    // Increase for less sensitive (was 1200)
#define ROTARY_WRAP_THRESHOLD   15000

//--------------------------------------------------------------------
// Audio Configuration
//--------------------------------------------------------------------
const uint32_t sample_rates[] = {48000};
uint32_t current_sample_rate = 48000;

#define N_SAMPLE_RATES  TU_ARRAY_SIZE(sample_rates)
#define DEFAULT_VOLUME  -6 * 256  // -6 dB

//--------------------------------------------------------------------
// LED Blink Patterns (onboard LED)
//--------------------------------------------------------------------
enum {
    BLINK_STREAMING = 25,
    BLINK_NOT_MOUNTED = 250,
    BLINK_MOUNTED = 1000,
    BLINK_SUSPENDED = 2500,
};

static uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;

//--------------------------------------------------------------------
// Audio Controls
//--------------------------------------------------------------------
int8_t mute[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_RX + 1];
int16_t volume[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_RX + 1];

// Audio buffer (like picoamp-2)
uint8_t spk_buf[CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX];
int spk_data_size = 0;
uint8_t current_resolution = 16;

//--------------------------------------------------------------------
// Button Configuration (from your working code)
//--------------------------------------------------------------------
typedef struct {
    uint8_t gpio;
    uint8_t keycode;
    const char* name;
    bool was_pressed;
    uint32_t last_press_time;
} button_t;

static button_t buttons[] = {
    {BTN_SPACE,   HID_KEY_SPACE, "SPACE",  false, 0},
    {HAT_UP,      HID_KEY_F1,    "UP",     false, 0},
    {HAT_RIGHT,   HID_KEY_F2,    "RIGHT",  false, 0},
    {HAT_DOWN,    HID_KEY_F3,    "DOWN",   false, 0},
    {HAT_LEFT,    HID_KEY_F4,    "LEFT",   false, 0},
    {HAT_CENTER,  HID_KEY_F5,    "CENTER", false, 0},
};
#define NUM_BUTTONS (sizeof(buttons) / sizeof(buttons[0]))

//--------------------------------------------------------------------
// Rotary Encoder State (from your working code)
//--------------------------------------------------------------------
static int16_t last_rotary_value = -32768;
static uint32_t last_rotary_time = 0;

//--------------------------------------------------------------------
// LED State (from your working code)
//--------------------------------------------------------------------
static uint32_t last_led_update = 0;
static bool led2_on = true;

//--------------------------------------------------------------------
// Function Prototypes
//--------------------------------------------------------------------
void led_blinking_task(void);
void hid_task(void);
void hid_key_release_task(void);
void audio_task(void);
void buttons_init(void);
void leds_init(void);
void i2c_init_ads1115(void);
bool ads1115_update(uint16_t *value);
void send_key_tap(uint8_t keycode);
void send_mouse_scroll(int8_t scroll);

//--------------------------------------------------------------------
// Button Initialization (from your working code)
//--------------------------------------------------------------------
void buttons_init(void) {
    for (int i = 0; i < NUM_BUTTONS; i++) {
        gpio_init(buttons[i].gpio);
        gpio_set_dir(buttons[i].gpio, GPIO_IN);
        gpio_pull_up(buttons[i].gpio);
        buttons[i].was_pressed = !gpio_get(buttons[i].gpio);
    }
}

//--------------------------------------------------------------------
// LED Initialization (from your working code)
//--------------------------------------------------------------------
void leds_init(void) {
    // LED 1 - always on
    gpio_set_function(LED_PIN_1, GPIO_FUNC_PWM);
    uint slice1 = pwm_gpio_to_slice_num(LED_PIN_1);
    pwm_config config1 = pwm_get_default_config();
    pwm_config_set_wrap(&config1, 255);
    pwm_init(slice1, &config1, true);
    pwm_set_gpio_level(LED_PIN_1, LED_PWM_LEVEL);
    
    // LED 2 - blinks on button press
    gpio_set_function(LED_PIN_2, GPIO_FUNC_PWM);
    uint slice2 = pwm_gpio_to_slice_num(LED_PIN_2);
    pwm_config config2 = pwm_get_default_config();
    pwm_config_set_wrap(&config2, 255);
    pwm_init(slice2, &config2, true);
    pwm_set_gpio_level(LED_PIN_2, LED_PWM_LEVEL);
}

//--------------------------------------------------------------------
// I2C & ADS1115 - Non-blocking read with faster conversion
//--------------------------------------------------------------------
static uint32_t ads1115_last_start = 0;
static uint8_t ads1115_state = 0;  // 0=idle, 1=waiting for conversion

void i2c_init_ads1115(void) {
    i2c_init(i2c0, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);
}

// Non-blocking ADS1115 read - call frequently from main loop
// Returns true and sets *value when a new reading is available
bool ads1115_update(uint16_t *value) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    
    if (ads1115_state == 0) {
        // Start a new conversion
        // Config: AIN0, FSR=±4.096V, single-shot, 860 SPS (fastest)
        uint8_t config[3] = {0x01, 0xC3, 0xE3};  // 0xE3 = 860 SPS instead of 0x83 = 128 SPS
        i2c_write_blocking(i2c0, ADS1115_ADDR, config, 3, false);
        ads1115_last_start = now;
        ads1115_state = 1;
        return false;
    }
    
    if (ads1115_state == 1) {
        // Wait at least 2ms for conversion at 860 SPS
        if (now - ads1115_last_start < 2) {
            return false;
        }
        
        // Read the result
        uint8_t reg = 0x00;
        i2c_write_blocking(i2c0, ADS1115_ADDR, &reg, 1, true);
        uint8_t data[2];
        i2c_read_blocking(i2c0, ADS1115_ADDR, data, 2, false);
        
        *value = (data[0] << 8) | data[1];
        ads1115_state = 0;  // Ready for next conversion
        return true;
    }
    
    return false;
}

//--------------------------------------------------------------------
// HID Functions - Non-blocking versions
//--------------------------------------------------------------------
static uint32_t key_release_time = 0;
static bool key_pending_release = false;

void send_key_tap(uint8_t keycode) {
    if (!tud_hid_ready() || key_pending_release) return;
    
    uint8_t keycodes[6] = {keycode, 0, 0, 0, 0, 0};
    tud_hid_keyboard_report(REPORT_ID_KEYBOARD, 0, keycodes);
    key_pending_release = true;
    key_release_time = to_ms_since_boot(get_absolute_time()) + 10; // Release after 10ms
}

// Call this from main loop to handle key release
void hid_key_release_task(void) {
    if (key_pending_release && to_ms_since_boot(get_absolute_time()) >= key_release_time) {
        if (tud_hid_ready()) {
            uint8_t no_keys[6] = {0};
            tud_hid_keyboard_report(REPORT_ID_KEYBOARD, 0, no_keys);
            key_pending_release = false;
        }
    }
}

void send_mouse_scroll(int8_t scroll) {
    if (!tud_hid_ready()) return;
    uint8_t report[4] = {0, 0, 0, (uint8_t)scroll};
    tud_hid_report(REPORT_ID_MOUSE, report, sizeof(report));
}

//--------------------------------------------------------------------
// Main
//--------------------------------------------------------------------
int main(void) {
    // Initialize I2S before other peripherals (picoamp-2 requirement)
    i2s_mclk_set_config(pio0, 0, dma_claim_unused_channel(true), false, CLOCK_MODE_LOW_JITTER_OC, MODE_I2S);
    
    board_init();
    
    // Initialize I2S with pin configuration (before tud_init, like picoamp-2)
    i2s_mclk_set_pin(PIN_I2S_DATA, PIN_I2S_LRCLK, PIN_I2S_MCLK);
    i2s_mclk_init(current_sample_rate);
    
    // Initialize TinyUSB
    tud_init(BOARD_TUD_RHPORT);
    
    if (board_init_after_tusb) {
        board_init_after_tusb();
    }
    
    // Set default mute and volume
    for (int i = 0; i < CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_RX + 1; i++) {
        mute[i] = false;
        volume[i] = DEFAULT_VOLUME;
    }
    
    // Initialize buttons and LEDs (after USB init)
    buttons_init();
    leds_init();
    
    // Initialize I2C for ADS1115
    i2c_init_ads1115();
    
    // ADS1115 will start automatically on first ads1115_update() call

    while (1) {
        tud_task();
        audio_task();
        hid_key_release_task();
        led_blinking_task();
        hid_task();
    }

    return 0;
}

//--------------------------------------------------------------------
// HID Task - Buttons and Rotary (non-blocking version)
//--------------------------------------------------------------------
void hid_task(void) {
    if (!tud_mounted()) return;
    
    uint32_t now = to_ms_since_boot(get_absolute_time());
    int pressed_count = 0;
    
    // Check all buttons
    for (int i = 0; i < NUM_BUTTONS; i++) {
        bool is_pressed = !gpio_get(buttons[i].gpio);
        if (is_pressed) pressed_count++;
        
        if (is_pressed && !buttons[i].was_pressed) {
            if (now - buttons[i].last_press_time > DEBOUNCE_MS) {
                send_key_tap(buttons[i].keycode);
                buttons[i].last_press_time = now;
            }
        }
        buttons[i].was_pressed = is_pressed;
    }
    
    // LED2 blink on button press
    if (now - last_led_update > 100) {
        if (pressed_count > 0) {
            led2_on = !led2_on;
            pwm_set_gpio_level(LED_PIN_2, led2_on ? LED_PWM_LEVEL : 0);
        } else {
            pwm_set_gpio_level(LED_PIN_2, LED_PWM_LEVEL);
            led2_on = true;
        }
        last_led_update = now;
    }
    
    // Rotary encoder -> mouse scroll (non-blocking, continuous polling)
    uint16_t current_value_u;
    if (ads1115_update(&current_value_u)) {
        int16_t current_value = (int16_t)current_value_u;
        
        // Only process if we have a valid last value
        if (last_rotary_value != -32768) {
            int32_t diff = (int32_t)current_value - (int32_t)last_rotary_value;
            int32_t abs_diff = diff < 0 ? -diff : diff;
            
            // Check if enough time has passed since last scroll event
            if (now - last_rotary_time > ROTARY_DEBOUNCE_MS) {
                bool trigger = false;
                bool scroll_down = false;
                
                // Handle wrap-around
                if (abs_diff > ROTARY_WRAP_THRESHOLD) {
                    scroll_down = (diff < 0);
                    trigger = true;
                } else if (abs_diff > ROTARY_THRESHOLD) {
                    scroll_down = (diff > 0);
                    trigger = true;
                }
                
                if (trigger) {
                    send_mouse_scroll(scroll_down ? -1 : 1);
                    last_rotary_time = now;
                    last_rotary_value = current_value;
                }
            }
        } else {
            // First reading - just store it
            last_rotary_value = current_value;
        }
    }
}

//--------------------------------------------------------------------
// Audio Task - Pass audio data to I2S (let i2s_enqueue handle conversion)
//--------------------------------------------------------------------
void audio_task(void) {
    if (spk_data_size) {
        // Check mute
        bool is_muted = mute[0] || (mute[1] && mute[2]);
        
        if (is_muted) {
            // Send silence
            static uint8_t silence[CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX] = {0};
            i2s_enqueue(silence, spk_data_size, current_resolution);
        } else {
            // Pass raw data directly - i2s_enqueue handles format conversion
            i2s_enqueue(spk_buf, spk_data_size, current_resolution);
        }
        
        // Feedback (from picoamp-2)
        static uint32_t last_fb_ms = 0;
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        if (now_ms > last_fb_ms) {
            int32_t level = i2s_get_buf_us();
            uint32_t min_feedback = (current_sample_rate / 1000 - 1) << 16;
            uint32_t max_feedback = (current_sample_rate / 1000 + 1) << 16;
            uint32_t feedback_range = 2 << 16;
            
            int32_t feedback = I2S_TARGET_LEVEL_MAX_US - level;
            feedback *= feedback_range;
            feedback /= (I2S_TARGET_LEVEL_MAX_US - I2S_TARGET_LEVEL_MIN_US);
            feedback += min_feedback;
            
            if (feedback < (int32_t)min_feedback) feedback = min_feedback;
            else if (feedback > (int32_t)max_feedback) feedback = max_feedback;
            
            tud_audio_fb_set(feedback);
            last_fb_ms = now_ms;
        }
        
        spk_data_size = 0;
    }
}

//--------------------------------------------------------------------
// LED Blinking Task (onboard LED for USB status)
//--------------------------------------------------------------------
void led_blinking_task(void) {
    static uint32_t start_ms = 0;
    static bool led_state = false;

    if (to_ms_since_boot(get_absolute_time()) - start_ms < blink_interval_ms) return;
    start_ms += blink_interval_ms;

    board_led_write(led_state);
    led_state = !led_state;
}

//--------------------------------------------------------------------
// TinyUSB Device Callbacks
//--------------------------------------------------------------------
void tud_mount_cb(void) {
    blink_interval_ms = BLINK_MOUNTED;
}

void tud_umount_cb(void) {
    blink_interval_ms = BLINK_NOT_MOUNTED;
}

void tud_suspend_cb(bool remote_wakeup_en) {
    (void)remote_wakeup_en;
    blink_interval_ms = BLINK_SUSPENDED;
}

void tud_resume_cb(void) {
    blink_interval_ms = tud_mounted() ? BLINK_MOUNTED : BLINK_NOT_MOUNTED;
}

//--------------------------------------------------------------------
// HID Callbacks
//--------------------------------------------------------------------
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                                hid_report_type_t report_type, uint8_t* buffer,
                                uint16_t reqlen) {
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type, uint8_t const* buffer,
                           uint16_t bufsize) {
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}

//--------------------------------------------------------------------
// Audio Callbacks (from picoamp-2)
//--------------------------------------------------------------------
bool tud_audio_set_itf_close_EP_cb(uint8_t rhport, tusb_control_request_t const* p_request) {
    (void)rhport;
    (void)p_request;
    return true;
}

bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const* p_request) {
    (void)rhport;
    uint8_t const itf = tu_u16_low(tu_le16toh(p_request->wIndex));
    uint8_t const alt = tu_u16_low(tu_le16toh(p_request->wValue));

    if (itf == ITF_NUM_AUDIO_STREAMING_SPK) {
        if (alt != 0) {
            blink_interval_ms = BLINK_STREAMING;
        } else {
            blink_interval_ms = BLINK_MOUNTED;
        }
    }
    return true;
}

bool tud_audio_rx_done_pre_read_cb(uint8_t rhport, uint16_t n_bytes_received, uint8_t func_id, uint8_t ep_out, uint8_t cur_alt_setting) {
    (void)rhport;
    (void)func_id;
    (void)ep_out;
    
    // Store current resolution based on alt setting
    if (cur_alt_setting == 1) current_resolution = 16;
    else if (cur_alt_setting == 2) current_resolution = 24;
    else if (cur_alt_setting == 3) current_resolution = 32;
    
    // Read data into buffer (like picoamp-2)
    spk_data_size = tud_audio_read(spk_buf, n_bytes_received);
    return true;
}

bool tud_audio_tx_done_pre_load_cb(uint8_t rhport, uint8_t itf, uint8_t ep_in, uint8_t cur_alt_setting) {
    (void)rhport;
    (void)itf;
    (void)ep_in;
    (void)cur_alt_setting;
    return true;
}

bool tud_audio_set_req_ep_cb(uint8_t rhport, tusb_control_request_t const* p_request, uint8_t* pBuff) {
    (void)rhport;
    (void)p_request;
    (void)pBuff;
    return true;
}

bool tud_audio_get_req_ep_cb(uint8_t rhport, tusb_control_request_t const* p_request) {
    (void)rhport;
    (void)p_request;
    return true;
}

bool tud_audio_set_req_itf_cb(uint8_t rhport, tusb_control_request_t const* p_request, uint8_t* pBuff) {
    (void)rhport;
    (void)p_request;
    (void)pBuff;
    return true;
}

bool tud_audio_get_req_itf_cb(uint8_t rhport, tusb_control_request_t const* p_request) {
    (void)rhport;
    (void)p_request;
    return true;
}

bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const* p_request, uint8_t* pBuff) {
    (void)rhport;

    audio_control_request_t const* request = (audio_control_request_t const*)p_request;

    if (request->bEntityID == UAC2_ENTITY_SPK_FEATURE_UNIT) {
        if (request->bControlSelector == AUDIO_FU_CTRL_MUTE) {
            audio_control_cur_1_t const* data = (audio_control_cur_1_t const*)pBuff;
            mute[request->bChannelNumber] = data->bCur;
            return true;
        }
        if (request->bControlSelector == AUDIO_FU_CTRL_VOLUME) {
            audio_control_cur_2_t const* data = (audio_control_cur_2_t const*)pBuff;
            volume[request->bChannelNumber] = data->bCur;
            return true;
        }
    }

    if (request->bEntityID == UAC2_ENTITY_CLOCK) {
        if (request->bControlSelector == AUDIO_CS_CTRL_SAM_FREQ) {
            TU_VERIFY(request->wLength == sizeof(audio_control_cur_4_t));
            current_sample_rate = (uint32_t)((audio_control_cur_4_t const*)pBuff)->bCur;
            i2s_mclk_change_clock(current_sample_rate);
            return true;
        }
    }

    return false;
}

bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const* p_request) {
    (void)rhport;

    audio_control_request_t const* request = (audio_control_request_t const*)p_request;

    if (request->bEntityID == UAC2_ENTITY_CLOCK) {
        if (request->bControlSelector == AUDIO_CS_CTRL_SAM_FREQ) {
            if (request->bRequest == AUDIO_CS_REQ_CUR) {
                audio_control_cur_4_t curf = {(int32_t)tu_htole32(current_sample_rate)};
                return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &curf, sizeof(curf));
            }
            if (request->bRequest == AUDIO_CS_REQ_RANGE) {
                audio_control_range_4_n_t(N_SAMPLE_RATES) rangef = {
                    .wNumSubRanges = tu_htole16(N_SAMPLE_RATES)
                };
                for (int i = 0; i < N_SAMPLE_RATES; i++) {
                    rangef.subrange[i].bMin = (int32_t)sample_rates[i];
                    rangef.subrange[i].bMax = (int32_t)sample_rates[i];
                    rangef.subrange[i].bRes = 0;
                }
                return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &rangef, sizeof(rangef));
            }
        }
        if (request->bControlSelector == AUDIO_CS_CTRL_CLK_VALID) {
            audio_control_cur_1_t cur_valid = {.bCur = 1};
            return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &cur_valid, sizeof(cur_valid));
        }
    }

    if (request->bEntityID == UAC2_ENTITY_SPK_FEATURE_UNIT) {
        if (request->bControlSelector == AUDIO_FU_CTRL_MUTE) {
            audio_control_cur_1_t cur_mute = {.bCur = mute[request->bChannelNumber]};
            return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &cur_mute, sizeof(cur_mute));
        }
        if (request->bControlSelector == AUDIO_FU_CTRL_VOLUME) {
            if (request->bRequest == AUDIO_CS_REQ_CUR) {
                audio_control_cur_2_t cur_vol = {.bCur = tu_htole16(volume[request->bChannelNumber])};
                return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &cur_vol, sizeof(cur_vol));
            }
            if (request->bRequest == AUDIO_CS_REQ_RANGE) {
                audio_control_range_2_n_t(1) range_vol = {
                    .wNumSubRanges = tu_htole16(1),
                    .subrange[0] = {.bMin = tu_htole16(-90 * 256), tu_htole16(0), tu_htole16(1)}
                };
                return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &range_vol, sizeof(range_vol));
            }
        }
    }

    return false;
}

void tud_audio_feedback_params_cb(uint8_t func_id, uint8_t alt_itf, audio_feedback_params_t* feedback_param) {
    (void)func_id;
    (void)alt_itf;

    feedback_param->method = AUDIO_FEEDBACK_METHOD_FREQUENCY_FLOAT;
    feedback_param->sample_freq = current_sample_rate;
}

TU_ATTR_FAST_FUNC void tud_audio_feedback_interval_isr(uint8_t func_id, uint32_t frame_number, uint8_t interval_shift) {
    (void)func_id;
    (void)frame_number;
    (void)interval_shift;
    // Feedback is handled in audio_task()
}
