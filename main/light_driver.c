/*
 * ESP32 White Ambiance
 * Copyright © 2025 Michal Jirků (wejn)
 *
 * This code is licensed under GPL version 3.
 */
#include "esp_check.h"
#include "global_config.h"
#include "light_config.h"
#include "light_driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "data_tables.h"
#include "esp_timer.h"

static const char *TAG = "LIGHT_DRIVER";
static TaskHandle_t ld_task_handle;
volatile static bool ld_initialized = false;

static portMUX_TYPE ld_fade_spinlock = portMUX_INITIALIZER_UNLOCKED; // spinlock governing these:
volatile static uint8_t ld_channels_fading = 0; // bitmap for when given channel fade is active
volatile static bool ld_ledc_fade_active = false; // whether there's active fade

static portMUX_TYPE ld_update_spinlock = portMUX_INITIALIZER_UNLOCKED; // spinlock governing these:
volatile static bool light_config_updated = false; // when onoff, color, or level updated
volatile static ld_effect_type desired_effect = LD_Effect_None; // other than None overrides light config fully

#define COLOR_TABLE_SIZE COLOR_MAX_TEMPERATURE - COLOR_MIN_TEMPERATURE + 1
static double color_normal[COLOR_TABLE_SIZE] = COLOR_DATA_NORMAL;
static double color_cold[COLOR_TABLE_SIZE] = COLOR_DATA_COLD;
static double color_warm[COLOR_TABLE_SIZE] = COLOR_DATA_HOT;
static double brightness_normal[256] = BRIGHTNESS_DATA_NORMAL;
static double brightness_cold[256] = BRIGHTNESS_DATA_COLD;
static double brightness_warm[256] = BRIGHTNESS_DATA_HOT;

#define MY_SPD_MODE LEDC_LOW_SPEED_MODE
#define MY_DUTY_RES LEDC_TIMER_13_BIT
#define MAX_DUTY ((1 << MY_DUTY_RES) - 1)
#define MAX_CHANNELS 5 // this better be set right, or the fading won't work

static IRAM_ATTR bool cb_fade_end(const ledc_cb_param_t *param, void *user_arg) {
    BaseType_t taskAwoken = pdFALSE;
    uint8_t prev;

    if (param->event == LEDC_FADE_END_EVT && param->speed_mode == MY_SPD_MODE) {
        taskENTER_CRITICAL_ISR(&ld_fade_spinlock);
        prev = ld_channels_fading;
        ld_channels_fading &= ~(1 << param->channel); // clear this channel
        if (prev && ! ld_channels_fading) {
            ld_ledc_fade_active = false;
            vTaskNotifyGiveFromISR(ld_task_handle, &taskAwoken);
        }
        taskEXIT_CRITICAL_ISR(&ld_fade_spinlock);
    }

    return (taskAwoken == pdTRUE);
}

#define FADE(chan, duty, time) do { \
    ledc_set_fade_time_and_start(MY_SPD_MODE, chan, (duty), (time), LEDC_FADE_NO_WAIT); \
} while(0)

static void fade_to(bool onoff, uint8_t level, uint16_t temperature, uint16_t time) {
    // Mark all channels active
    taskENTER_CRITICAL(&ld_fade_spinlock);
    ld_ledc_fade_active = true;
    ld_channels_fading = (1 << MAX_CHANNELS) - 1;
    taskEXIT_CRITICAL(&ld_fade_spinlock);

    // Kick off the fading
    if (! onoff) {
        ESP_LOGI(TAG, "Set to off, t: %u", time);
        FADE(LEDC_CHANNEL_0, 0, time);
        FADE(LEDC_CHANNEL_1, 0, time);
        FADE(LEDC_CHANNEL_2, 0, time);
        FADE(LEDC_CHANNEL_3, 0, time); // XXX: unused
        FADE(LEDC_CHANNEL_4, 0, time); // XXX: unused
    } else {
        uint8_t new_level = level;
        uint16_t new_temp = temperature;
        if (light_config->level_options&2) {
#define MAX_LEVEL 254
#define MIN_LEVEL 1
            uint16_t min_temp = light_config->couple_min_temperature;
            uint16_t max_temp = temperature;
            // My reading of ZCLv8 is that when coupled, it is:
            new_temp = max_temp - ((new_level - MIN_LEVEL) * (max_temp - min_temp)) / (MAX_LEVEL - MIN_LEVEL);
        }
        double normal = color_normal[new_temp - COLOR_MIN_TEMPERATURE] * brightness_normal[new_level];
        double cold = color_cold[new_temp - COLOR_MIN_TEMPERATURE] * brightness_cold[new_level];
        double warm = color_warm[new_temp - COLOR_MIN_TEMPERATURE] * brightness_warm[new_level];
        ESP_LOGI(TAG, "Set to %.04f, %.04f, %.04f (o/l/t: [%d, %d, %d], t: %u)",
                normal, cold, warm, onoff, new_level, new_temp, time);
        FADE(LEDC_CHANNEL_0, MAX_DUTY * normal, time);
        FADE(LEDC_CHANNEL_1, MAX_DUTY * cold, time);
        FADE(LEDC_CHANNEL_2, MAX_DUTY * warm, time);
        FADE(LEDC_CHANNEL_3, 0, time); // XXX: unused
        FADE(LEDC_CHANNEL_4, 0, time); // XXX: unused
    }
}

#define STOP_FADE(chan) ledc_fade_stop(MY_SPD_MODE, chan)
static void stop_fading() {
    STOP_FADE(LEDC_CHANNEL_0);
    STOP_FADE(LEDC_CHANNEL_1);
    STOP_FADE(LEDC_CHANNEL_2);
    STOP_FADE(LEDC_CHANNEL_3);
    STOP_FADE(LEDC_CHANNEL_4);
}

typedef struct {
    bool valid;
    bool abortable;
    const bool *onoff;
    const uint8_t *level;
    const uint16_t *temperature;
    const uint16_t time;
} effect_frame;

static bool on = true;
static bool off = false;
static uint8_t min_level = 1;
static uint8_t max_level = 254;
static uint8_t mid_level = (MAX_LEVEL - MIN_LEVEL) / 2;
#define LAST_FRAME {false, false /* dontcare */, NULL, NULL, NULL, 0}
static effect_frame Effect_Blink_FromOff[] = {
    {true, false, &on, &max_level, NULL, 250},
    {true, false, &off, &max_level, NULL, 250},
    LAST_FRAME,
};
static effect_frame Effect_Blink_FromOn[] = {
    {true, false, &off, &max_level, NULL, 250},
    {true, false, &on, &max_level, NULL, 250},
    LAST_FRAME,
};

static effect_frame Effect_Breathe[] = {
    {true, false, &on, &min_level, NULL, 500},
    {true, true, &on, &max_level, NULL, 500},
    LAST_FRAME,
};

static effect_frame Effect_ChannelChange[] = {
    {true, false, &on, &max_level, NULL, 500},
    {true, false, &on, &min_level, NULL, 500},
    {true, true, &on, &min_level, NULL, 7000},
    LAST_FRAME,
};

static effect_frame Effect_DelayedOff2[] = {
    {true, false, &on, &mid_level, NULL, 800}, // XXX: should be 50% down, not to 50%
    {true, false, &off, &min_level, NULL, 12000},
    LAST_FRAME,
};

static effect_frame Effect_DyingLight0[] = {
    {true, false, &on, &max_level, NULL, 500}, // XXX: should be 20% up, not to 100%
    {true, false, &on, &mid_level, NULL, 500},
    {true, false, &off, &min_level, NULL, 500},
    LAST_FRAME,
};


#define ACTIVATE_EFFECT(_reps, what) do { \
    ESP_LOGD(TAG, "Activating effect: %s with %d reps", #what, _reps); \
    current_effect = (what); \
    frame_no = 0; \
    reps = _reps; \
    want_effect = LD_Effect_None; \
} while (0)

#define RESET_EFFECTS() do { \
    current_effect = NULL; \
    abort_effect = false; \
    frame_start = 0; \
    frame_duration = 0; \
} while (0)

static void light_driver_task(void *pvParameters) {
    bool updated = false;
    effect_frame *current_effect = NULL;
    uint8_t frame_no = 0;
    uint8_t reps = 1;
    bool abort_effect = false;
    ld_effect_type want_effect = LD_Effect_None;
    uint64_t frame_start = 0;
    int16_t frame_duration = 0;

    xTaskNotifyWait(0, 0, NULL, portMAX_DELAY); // block immediately ;)
    while (true) {
        if (! *light_config_initialized) {
            ESP_LOGW(TAG, "The light_config not initialized yet, skip");
        } else {
            taskENTER_CRITICAL(&ld_update_spinlock);
            updated |= light_config_updated;
            light_config_updated = false;
            if (desired_effect != LD_Effect_None) {
                want_effect = desired_effect;
            }
            desired_effect = LD_Effect_None;
            taskEXIT_CRITICAL(&ld_update_spinlock);

            // do we have another effect to process?
            switch (want_effect) {
                case LD_Effect_None: // no new instruction, maybe continue current effect
                    if (ld_ledc_fade_active) { // previous fade still running → sleep
                        ESP_LOGD(TAG, "Fade still running, sleep");
                    } else { // fade ended, get new frame or update
                        ESP_LOGD(TAG, "No fade active...");
                        if (current_effect) {
                            if (frame_start > 0) {
                                uint16_t sofar = (esp_timer_get_time() - frame_start) / 1000;
                                if (sofar < frame_duration - 10) { // 10 because who cares about 10ms
                                    ESP_LOGD(TAG, "Waiting %u-ms to fill up the wait time...", frame_duration - sofar);
                                    xTaskNotifyWait(0, 0, NULL, pdMS_TO_TICKS(frame_duration - sofar));
                                    continue;
                                }
                            }
                            ESP_LOGD(TAG, "We have effect to run...");
                            if (current_effect[frame_no].valid) {
                                ESP_LOGD(TAG, "Starting frame %d, reps: %d, time: %d...", frame_no, reps, current_effect[frame_no].time);
                                frame_start = esp_timer_get_time();
                                frame_duration = current_effect[frame_no].time;
                                fade_to(
                                        current_effect[frame_no].onoff ? *current_effect[frame_no].onoff : light_config->onoff,
                                        current_effect[frame_no].level ? *current_effect[frame_no].level : light_config->level,
                                        current_effect[frame_no].temperature ? *current_effect[frame_no].temperature : light_config->temperature,
                                        current_effect[frame_no].time);
                                if (abort_effect && current_effect[frame_no].abortable) {
                                    ESP_LOGD(TAG, "Aborting now.");
                                    RESET_EFFECTS();
                                } else {
                                    frame_no++;
                                }
                            } else { // current frame not valid → possibly end
                                assert(frame_no > 0); // we can't have zeroth frame invalid
                                reps--;
                                if (reps > 0) { // more reps → go to frame 0 and start
                                    ESP_LOGD(TAG, "Next rep...");
                                    frame_no = 0;
                                    continue; // do it again, Sam
                                } else { // no more reps → end of animation
                                    ESP_LOGD(TAG, "End of animation...");
                                    RESET_EFFECTS();
                                    fade_to(light_config->onoff, light_config->level, light_config->temperature, 100);
                                }
                            }
                            break; // processed, get out.
                        } else { // no effects
                            if (updated) {
                                ESP_LOGD(TAG, "Running update...");
                                updated = false;
                                fade_to(light_config->onoff, light_config->level, light_config->temperature, 100);
                            } else {
                                ESP_LOGD(TAG, "No update, no effects. No-op");
                            }
                        }
                    }
                    break;
                case LD_Effect_Blink: // identify: flash once
                    ACTIVATE_EFFECT(1, light_config->onoff ? Effect_Blink_FromOn : Effect_Blink_FromOff);
                    continue;
                case LD_Effect_Breathe: // identify: on/off over 1s, repeated 15x
                    ACTIVATE_EFFECT(15, Effect_Breathe);
                    continue;
                case LD_Effect_Okay: // identify: flash twice
                    ACTIVATE_EFFECT(2, light_config->onoff ? Effect_Blink_FromOn : Effect_Blink_FromOff);
                    continue;
                case LD_Effect_ChannelChange: // identify: max brightness 0.5s, then min brightness for 7.5s
                    ACTIVATE_EFFECT(1, Effect_ChannelChange);
                    continue;
                case LD_Effect_Finish:
                    ESP_LOGD(TAG, "Triggering effect finish");
                    abort_effect = true;
                    want_effect = LD_Effect_None; // clear it (processed)
                    break;
                case LD_Effect_Stop:
                    ESP_LOGD(TAG, "Triggering effect stop");
                    RESET_EFFECTS();
                    stop_fading(); // we might be in the middle of one; don't wait
                    fade_to(light_config->onoff, light_config->level, light_config->temperature, 100);
                    want_effect = LD_Effect_None; // clear it (processed)
                    break;
                case LD_Effect_DelayedOff0: // fade to off in 0.8s
                    ESP_LOGD(TAG, "Triggering effect DelayedOff0");
                    RESET_EFFECTS();
                    fade_to(false, light_config->level, light_config->temperature, 800);
                    want_effect = LD_Effect_None; // clear it (processed)
                    break;
                case LD_Effect_DelayedOff1: // no fade (??)
                    ESP_LOGD(TAG, "Triggering effect DelayedOff1");
                    RESET_EFFECTS();
                    fade_to(false, light_config->level, light_config->temperature, 1);
                    want_effect = LD_Effect_None; // clear it (processed)
                    break;
                case LD_Effect_DelayedOff2: // off with effect: 50% dim down in 0.8s, then fade to off in 12s
                    ESP_LOGD(TAG, "Triggering effect DelayedOff2");
                    ACTIVATE_EFFECT(1, Effect_DelayedOff2);
                    continue;
                case LD_Effect_DyingLight0: // off with effect: 20% dim up in 0.5s, then fade to off in 1s
                    ESP_LOGD(TAG, "Triggering effect DyingLight0");
                    ACTIVATE_EFFECT(1, Effect_DyingLight0);
                    continue;
            }
        }
        xTaskNotifyWait(0, 0, NULL, portMAX_DELAY);
    }
}

esp_err_t light_driver_update() {
    if (!ld_initialized) {
        ESP_LOGE(TAG, "Update triggered without initialization, skip");
        return ESP_ERR_NOT_SUPPORTED;
    } else {
        taskENTER_CRITICAL(&ld_update_spinlock);
        light_config_updated = true;
        taskEXIT_CRITICAL(&ld_update_spinlock);
        xTaskNotifyGive(ld_task_handle);
    }

    return ESP_OK;
}

esp_err_t light_driver_trigger_effect(const ld_effect_type effect) {
    if (!ld_initialized) {
        ESP_LOGE(TAG, "Effect triggered without initialization, skip");
        return ESP_ERR_NOT_SUPPORTED;
    } else {
        taskENTER_CRITICAL(&ld_update_spinlock);
        desired_effect = effect;
        taskEXIT_CRITICAL(&ld_update_spinlock);
        xTaskNotifyGive(ld_task_handle);
    }

    return ESP_OK;
}

#define CONFIG_CHAN(PIN, NUM) do { \
    ledc_channel_config_t chan_##NUM = { \
        .speed_mode = MY_SPD_MODE, \
        .channel = LEDC_CHANNEL_##NUM, \
        .timer_sel = timer, \
        .intr_type = LEDC_INTR_FADE_END, \
        .gpio_num = PIN, \
        .duty = 0, \
        .hpoint = 0, /* ?? */ \
        .flags.output_invert = 0, \
    }; \
    ret = ledc_channel_config(&chan_##NUM); \
    if (ret != ESP_OK) { \
        ESP_LOGE(TAG, "can't config ledc chan %d: %s, abort", NUM, esp_err_to_name(ret)); \
        return ret; \
    } \
    ret = ledc_cb_register(MY_SPD_MODE, LEDC_CHANNEL_##NUM, &ledc_callbacks, NULL); \
    if (ret != ESP_OK) { \
        ESP_LOGE(TAG, "can't register ledc fade cb for chan %d: %s, abort", NUM, esp_err_to_name(ret)); \
        return ret; \
    } \
} while(0)

esp_err_t light_driver_initialize() {
    esp_err_t ret = ESP_OK;

    if (ld_initialized) {
        ESP_LOGW(TAG, "Attempted to initialize light driver more than once");
    } else {
        gpio_config_t io_conf = {
            .intr_type = GPIO_INTR_DISABLE,
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = 1ULL<<MY_LIGHT_PWM_CH0_GPIO | 1ULL<<MY_LIGHT_PWM_CH1_GPIO | 1ULL<<MY_LIGHT_PWM_CH2_GPIO |
                1ULL<<MY_LIGHT_PWM_CH3_GPIO | 1ULL<<MY_LIGHT_PWM_CH4_GPIO,
            .pull_down_en = 1,
            .pull_up_en = 0,
        };
        ret = gpio_config(&io_conf);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "gpio_config failed with: %s", esp_err_to_name(ret));
        }

        ledc_timer_t timer = LEDC_TIMER_0;

        ledc_timer_config_t ledc_timer = {
            .speed_mode = MY_SPD_MODE,
            .timer_num = timer,
            .duty_resolution = MY_DUTY_RES,
            .freq_hz = 5000, // FIXME: Maybe 1k like Hue?
            .clk_cfg = LEDC_AUTO_CLK,
        };
        ret = ledc_timer_config(&ledc_timer);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "can't config ledc: %s, abort", esp_err_to_name(ret));
            return ret;
        }

        ret = ledc_fade_func_install(ESP_INTR_FLAG_LEVEL3); // higher prio → buttery smooth?
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "can't install fade func: %s, abort", esp_err_to_name(ret));
            return ret;
        }

        ledc_cbs_t ledc_callbacks = {
            .fade_cb = cb_fade_end,
        };

        CONFIG_CHAN(MY_LIGHT_PWM_CH0_GPIO, 0);
        CONFIG_CHAN(MY_LIGHT_PWM_CH1_GPIO, 1);
        CONFIG_CHAN(MY_LIGHT_PWM_CH2_GPIO, 2);
        CONFIG_CHAN(MY_LIGHT_PWM_CH3_GPIO, 3);
        CONFIG_CHAN(MY_LIGHT_PWM_CH4_GPIO, 4);

        ld_initialized = true;
        xTaskCreate(light_driver_task, "light_driver", 4096, NULL, 4, &ld_task_handle);
        ESP_LOGI(TAG, "Initialized");
    }

    return ret;
}
