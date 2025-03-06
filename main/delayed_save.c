/*
 * ESP32 White Ambiance
 * Written in 2025 by Michal Jirků (wejn)
 *
 * This code is licensed under AGPL version 3.
 */
#include <stdint.h>
#include "delayed_save.h"
#include "light_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#define SUSPEND_TICKS 250 // how many ms to wait until re-trying save
#define SAVE_EVERY 5 * 1000 * 1000 // 5 seconds ± SUSPEND_TICKS, actually
#define TRIGGERED_LAST_AT_LEAST 3 * 1000 * 1000 // 3 seconds ± SUSPEND_TICKS, actually

static const char *TAG = "DELAYED_SAVE";
static TaskHandle_t ds_task_handle;
volatile static bool ds_initialized = false;
volatile static uint32_t onoff_to_save = 0;
volatile static uint32_t level_to_save = 0;
volatile static uint32_t temperature_to_save = 0;
volatile static bool onoff_dirty = false;
volatile static bool level_dirty = false;
volatile static bool temperature_dirty = false;
volatile static int64_t last_triggered = 0;
volatile static int64_t last_saved = 0;
static portMUX_TYPE my_spinlock = portMUX_INITIALIZER_UNLOCKED;

static void delayed_save_task(void *pvParameters) {
    bool save_onoff = false;
    bool save_level = false;
    bool save_temperature = false;
    bool due_to_last_triggered = false;
    bool due_to_last_saved = false;
    ml_flash_vars_t vars[3];
    size_t num_to_save = 0;
    while (true) {
        taskENTER_CRITICAL(&my_spinlock);
        save_onoff = save_level = save_temperature = false;
        if (onoff_dirty || level_dirty || temperature_dirty) {
            due_to_last_triggered = (esp_timer_get_time() - last_triggered) > TRIGGERED_LAST_AT_LEAST;
            due_to_last_saved = (esp_timer_get_time() - last_saved) > SAVE_EVERY;
            if (due_to_last_saved || due_to_last_triggered) {
                last_saved = esp_timer_get_time();
                // Mark the values for saving & reset.
                save_onoff = onoff_dirty;
                save_level = level_dirty;
                save_temperature = temperature_dirty;
                onoff_dirty = level_dirty = temperature_dirty = false;
            }
        }
        taskEXIT_CRITICAL(&my_spinlock);

        // Actual saving -- not in critical section, cos we don't care if we save more recent
        // level or temp (than at the decision-to-save ts).
        if (due_to_last_saved || due_to_last_triggered) {
            ESP_LOGI(TAG, "Saving: dtls: %d, dtlt: %d", due_to_last_saved, due_to_last_triggered);

            num_to_save = 0;
            if (save_onoff) {
                vars[num_to_save].key = MLFV_onoff;
                vars[num_to_save].value = onoff_to_save;
                num_to_save++;
            }
            if (save_level) {
                vars[num_to_save].key = MLFV_level;
                vars[num_to_save].value = level_to_save;
                num_to_save++;
            }
            if (save_temperature) {
                vars[num_to_save].key = MLFV_temp;
                vars[num_to_save].value = temperature_to_save;
                num_to_save++;
            }
            my_light_save_vars_to_flash(vars, num_to_save);

            // Now that we saved, go to sleep until triggered again
            xTaskNotifyWait(0, 0, NULL, portMAX_DELAY);
        } else {
            // Not saved yet → wake up in a bit to try again
            xTaskNotifyWait(0, 0, NULL, pdMS_TO_TICKS(SUSPEND_TICKS));
        }
    }
}

void trigger_delayed_save(delayed_save_type type, uint32_t value) {
    taskENTER_CRITICAL(&my_spinlock);
    switch (type) {
        case DS_onoff:
            onoff_to_save = value;
            onoff_dirty = true;
            break;
        case DS_level:
            level_to_save = value;
            level_dirty = true;
            break;
        case DS_temperature:
            temperature_to_save = value;
            temperature_dirty = true;
            break;
        default:
            ESP_LOGW(TAG, "Delayed save for unknown type: %d", type);
            return;
    }

    last_triggered = esp_timer_get_time();
    taskEXIT_CRITICAL(&my_spinlock);
    // ESP_LOGI(TAG, "Notifying for save type %d with val %lu", type, value);
    xTaskNotifyGive(ds_task_handle);
}

void create_delayed_save_task() {
    if (ds_initialized) {
        ESP_LOGW(TAG, "Attempted to initialize delayed save more than once");
    } else {
        ds_initialized = true;
        onoff_dirty = level_dirty = temperature_dirty = false;
        last_saved = last_triggered = 0;
        xTaskCreate(delayed_save_task, "delayed_save", 4096, NULL, 4, &ds_task_handle);
        ESP_LOGI(TAG, "Created delayed save task");
    }
}
