/*
 * SPDX-FileCopyrightText: 2022-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* MQTT 5.0 over TLS Example
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_mac.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "esp_log.h"
//#include "esp_crt_bundle.h" TLS REMOVED
#include "mqtt_client.h"
#include "sdkconfig.h"

#define MQTT_BROKER_URI "mqtt://192.168.1.240:1883"
#define MQTT_TOPIC_PUB   "/kitchen/sensor"
#define MQTT_TOPIC_SUB  "/kitchen/control"


static const char *TAG = "ESP32_IOT_DEVICE";

// Loop Posting to the MQTT Broker with following function
void mqtt_publish_loop(void *param)
{
    esp_mqtt_client_handle_t client = (esp_mqtt_client_handle_t)param;

    while (1) {
        //TODO: Set real values from added sensors
        const char *json = "{\"temp\":22.3,\"humidity\":48.0,\"pressure\":1012.8}";

        // 0,1,0 eventually change to 0,0,0?
        esp_mqtt_client_publish(client, MQTT_TOPIC_PUB, json, 0, 1, 0);

        ESP_LOGI(TAG, "Repeated MQTT publish");

        vTaskDelay(pdMS_TO_TICKS(1000));  // every 1 seconds -- Req 
    }

}

 // Event handler registered to receive MQTT events
static void mqtt5_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32, base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    // example message to be posted to broker!!
    //const char *json = "{\"temp\":22.3,\"humidity\":48.0,\"pressure\":1012.8}";

    ESP_LOGD(TAG, "free heap size is %" PRIu32 ", minimum %" PRIu32, esp_get_free_heap_size(), esp_get_minimum_free_heap_size());
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        
        // Subscribe for recieving temp commads
        msg_id = esp_mqtt_client_subscribe(client, MQTT_TOPIC_SUB, 0);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        // Create new free RTOS task to run mqtt publish loop continuosly in background
        xTaskCreate(mqtt_publish_loop, "mqtt_publish_loop", 4096, client, 5, NULL);

        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d, reason code=0x%02x ", event->msg_id, (uint8_t)*event->data);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        esp_mqtt_client_disconnect(client);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        ESP_LOGI(TAG, "TOPIC=%.*s", event->topic_len, event->topic);
        ESP_LOGI(TAG, "DATA=%.*s", event->data_len, event->data);
        // TODO: Parse incoming temp commands for fan settings
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        ESP_LOGI(TAG, "Transport error");
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

// Configures and starts MQTT CLIENT
static void mqtt5_app_start(void)
{
   /*BELOW IS MODIFIED TO CONNECT TO LOCAL MQTT MOSQUITTO BROKER.
   I have commented out Last Will until i discuss with RISHI*/

    const esp_mqtt_client_config_t mqtt5_cfg = {
        // perhaps make this a global def so i can change easier
        .broker = {
        .address.uri = MQTT_BROKER_URI,
    },
        .session = {
            .protocol_ver = MQTT_PROTOCOL_V_5,
            /*.last_will = {
                .topic = "/kitchen/sensor",
                .msg = "i will leave",
                .msg_len = strlen("i will leave"),
                .qos = 1,
                .retain = true,
                
            },*/
        },
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt5_cfg);

    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt5_event_handler, NULL);
    esp_mqtt_client_start(client);
}

void app_main(void)
{

    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */

    // Ill put my switching logic here i imagine
    ESP_ERROR_CHECK(example_connect());

    mqtt5_app_start();
}
