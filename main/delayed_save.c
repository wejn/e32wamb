/*
 * ESP32 White Ambiance
 * Copyright © 2025 Michal Jirků (wejn)
 *
 * This code is licensed under GPL version 3.
 */
#include <stdint.h>
#include "delayed_save.h"
#include "light_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#define SUSPEND_MS 250 // how many ms to wait until re-trying save
#define SAVE_EVERY 5 * 1000 * 1000 // 5 seconds ± SUSPEND_MS, actually
#define TRIGGERED_LAST_AT_LEAST 3 * 1000 * 1000 // 3 seconds ± SUSPEND_MS, actually

static const char *TAG = "DELAYED_SAVE";
static TaskHandle_t ds_task_handle;
volatile static bool ds_initialized = false;
volatile static bool onoff_dirty = false;
volatile static bool level_dirty = false;
volatile static bool temperature_dirty = false;
volatile static int64_t last_triggered = 0;
volatile static int64_t next_save_at = 0;
static portMUX_TYPE my_spinlock = portMUX_INITIALIZER_UNLOCKED;

static void delayed_save_task(void *pvParameters) {
    bool save_onoff = false;
    bool save_level = false;
    bool save_temperature = false;
    bool due_to_last_triggered = false; // saving due to last triggered too much in the past
    bool due_to_last_saved = false; // saving due to last saved too much in the past
    lc_flash_var_t vars[3];
    size_t num_to_save = 0;
    while (true) {
        taskENTER_CRITICAL(&my_spinlock);
        save_onoff = save_level = save_temperature = false;
        if (onoff_dirty || level_dirty || temperature_dirty) {
            due_to_last_triggered = (esp_timer_get_time() - last_triggered) > TRIGGERED_LAST_AT_LEAST;
            due_to_last_saved = next_save_at < esp_timer_get_time();
            if (due_to_last_saved || due_to_last_triggered) {
                next_save_at = esp_timer_get_time() + SAVE_EVERY;
                // Mark the values for saving & reset.
                save_onoff = onoff_dirty;
                save_level = level_dirty;
                save_temperature = temperature_dirty;
                onoff_dirty = level_dirty = temperature_dirty = false;
            }
        }
        taskEXIT_CRITICAL(&my_spinlock);

        // Actual saving -- not in critical section, cos we don't care if we save more recent
        // level or temperature (than at the decision-to-save ts).
        if (due_to_last_saved || due_to_last_triggered) {
            ESP_LOGI(TAG, "Saving: dtls: %d, dtlt: %d", due_to_last_saved, due_to_last_triggered);

            num_to_save = 0;
            if (save_onoff) {
                vars[num_to_save] = LCFV_onoff;
                num_to_save++;
            }
            if (save_level) {
                vars[num_to_save] = LCFV_level;
                num_to_save++;
            }
            if (save_temperature) {
                vars[num_to_save] = LCFV_temperature;
                num_to_save++;
            }
            light_config_persist_vars(vars, num_to_save);

            // Now that we saved, go to sleep until triggered again
            xTaskNotifyWait(0, 0, NULL, portMAX_DELAY);
        } else {
            // Not saved yet → wake up in a bit to try again
            xTaskNotifyWait(0, 0, NULL, pdMS_TO_TICKS(SUSPEND_MS));
        }
    }
}

void trigger_delayed_save(delayed_save_type type) {
    if (!ds_initialized) {
        ESP_LOGE(TAG, "Delayed save of %d triggered without initialization, skip.", type);
        return;
    }

    taskENTER_CRITICAL(&my_spinlock);
    switch (type) {
        case DS_onoff:
            onoff_dirty = true;
            break;
        case DS_level:
            level_dirty = true;
            break;
        case DS_temperature:
            temperature_dirty = true;
            break;
        default:
            ESP_LOGW(TAG, "Delayed save for unknown type: %d", type);
            return;
    }

    last_triggered = esp_timer_get_time(); // last_triggered = now. (important for the next_save_at)
    if (next_save_at < last_triggered - SAVE_EVERY) {
        next_save_at = last_triggered + SAVE_EVERY;
    }
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
        next_save_at = last_triggered = 0;
        xTaskCreate(delayed_save_task, "delayed_save", 4096, NULL, 4, &ds_task_handle);
        ESP_LOGI(TAG, "Created delayed save task");
    }
}
