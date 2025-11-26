/*
 * SPDX-FileCopyrightText: 2022-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* MQTT 5.0 over TLS Example, Ethernet Basic Example
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
#include "mqtt_client.h"
#include "sdkconfig.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
//#include "esp_eth_phy_lan8720.h"

// ---- MQTT settings --------
#define MQTT_BROKER_URI "mqtt://192.168.1.240:1883"
#define MQTT_TOPIC_PUB   "/kitchen/sensor"
#define MQTT_TOPIC_SUB  "/kitchen/control"
#define MQTT_QOS_LEVEL 0 // 0 is forget, less overhead --> 1 is at least once, was getting jumpy and not really nessacary here
#define MQTT_RETAIN_FLAG 0

// ---- Ethernet PHY settings (ESP32 Ethernet Kit - LAN8720) ----
#define ETH_PHY_ADDR      1
#define ETH_PHY_POWER_PIN 5   
#define ETH_MDC_PIN       23
#define ETH_MDIO_PIN      18
#define ETH_RMII_CLK_PIN  0

static bool eth_connected = false;
static const char *TAG = "ESP32_IOT_DEVICE";
static const char *ETH_TAG = "ETH_CONNECTION";//Message ETH_TAG

// Function Definitions
static void mqtt5_app_start(void);
static esp_err_t eth_init(esp_eth_handle_t *eth_handle_out);
static void eth_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static void got_ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static void ethernet_start(void);
void mqtt_publish_loop(void *param);
static void mqtt5_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);

void app_main(void)
{

    ESP_LOGI(TAG, "[APP] Startup..");
    //ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    //ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    //esp_log_level_set("*", ESP_LOG_INFO);
    
    ESP_ERROR_CHECK(nvs_flash_init());
    // already called in ethernet start...
    //ESP_ERROR_CHECK(esp_netif_init());
    //ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */

    // Ill put my switching logic here i imagine
    // This Branch is straight up Ethernet functionality tho
    ethernet_start();
    // called in ethernet ip handler
    //mqtt5_app_start();
}


/*-------------ETHERNET FUNCTION SECTION-----------*/

// Ethernet Function Calls
static esp_err_t eth_init(esp_eth_handle_t *eth_handle_out)
{
    if (eth_handle_out == NULL) {
        ESP_LOGE(ETH_TAG, "invalid argument: eth_handle_out cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Init common MAC and PHY configs to default
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();

    // Update PHY config based on board specific configuration
    phy_config.phy_addr = ETH_PHY_ADDR; // common addr
    phy_config.reset_gpio_num = ETH_PHY_POWER_PIN; // no rst pin needed

    // Init vendor specific MAC config to default
    eth_esp32_emac_config_t esp32_emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    // Update vendor specific MAC config based on board configuration
    esp32_emac_config.smi_gpio.mdc_num = ETH_MDC_PIN;
    esp32_emac_config.smi_gpio.mdio_num = ETH_MDIO_PIN;

    esp32_emac_config.interface = EMAC_DATA_INTERFACE_RMII;
    esp32_emac_config.clock_config.rmii.clock_mode = EMAC_CLK_OUT;
    esp32_emac_config.clock_config.rmii.clock_gpio = ETH_RMII_CLK_PIN; // standard

    // Create new ESP32 Ethernet MAC instance
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&esp32_emac_config, &mac_config);
    if (mac == NULL) {
        ESP_LOGE(ETH_TAG, "create MAC instance failed");
        return ESP_FAIL;
    }

    // Create new generic PHY instance
    esp_eth_phy_t *phy = esp_eth_phy_new_generic(&phy_config);
    if (phy == NULL) {
        ESP_LOGE(ETH_TAG, "create PHY instance failed");
        mac->del(mac);
        return ESP_FAIL;
    }

    // Init Ethernet driver to default and install it
    esp_eth_handle_t eth_handle = NULL;
    esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
    if (esp_eth_driver_install(&config, &eth_handle) != ESP_OK) {
        ESP_LOGE(ETH_TAG, "Ethernet driver install failed");
        mac->del(mac);
        phy->del(phy);
        return ESP_FAIL;
    }

    *eth_handle_out = eth_handle;

    return ESP_OK;
}

/** Event handler for Ethernet events */
static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    uint8_t mac_addr[6] = {0};
    /* we can get the ethernet driver handle from event data */
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        eth_connected = true;
        esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
        ESP_LOGI(ETH_TAG, "Ethernet Link Up");
        ESP_LOGI(ETH_TAG, "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x",
                 mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        eth_connected = false;
        ESP_LOGI(ETH_TAG, "Ethernet Link Down");
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(ETH_TAG, "Ethernet Started");
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(ETH_TAG, "Ethernet Stopped");
        break;
    default:
        break;
    }
}

/** Event handler for IP_EVENT_ETH_GOT_IP */
static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    static bool mqtt_started = false;

    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    ESP_LOGI(ETH_TAG, "Ethernet Got IP Address");
    ESP_LOGI(ETH_TAG, "~~~~~~~~~~~");
    ESP_LOGI(ETH_TAG, "ETHIP:" IPSTR, IP2STR(&ip_info->ip));
    ESP_LOGI(ETH_TAG, "ETHMASK:" IPSTR, IP2STR(&ip_info->netmask));
    ESP_LOGI(ETH_TAG, "ETHGW:" IPSTR, IP2STR(&ip_info->gw));
    ESP_LOGI(ETH_TAG, "~~~~~~~~~~~");

    // Added for eth functionality
    if (!mqtt_started) {
        mqtt5_app_start();
        mqtt_started = true;
    }
}

/** Ethernet Startup Function Call */
static void ethernet_start(void)
{
    // Initialize Ethernet driver
    // Create Handle to store Ethernet Driver
    esp_eth_handle_t eth_handle;
    // Initialize Ethernet hardware with program halting on failure
    ESP_ERROR_CHECK(eth_init(&eth_handle));

    // Turn on netowrking section of chip
    // Initialize TCP/IP network interface aka the esp-netif (should be called only once in application)
    ESP_ERROR_CHECK(esp_netif_init());
    // Create default event loop thats running in background
    // Listens for Eth link up, Eth link Down, IP assigned
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create instance of esp-netif for Ethernet (Currently left to standard)
    // Use ESP_NETIF_DEFAULT_ETH when just one Ethernet interface is used and you don't need to modify
    // default esp-netif configuration parameters.
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    // Create Eth network interface
    esp_netif_t *eth_netif = esp_netif_new(&cfg);
    // Create layer between Eth driver and TCP/IP stack
    esp_eth_netif_glue_handle_t eth_netif_glue = esp_eth_new_netif_glue(eth_handle);
    // Attach Ethernet driver to TCP/IP stack - Connect Hardware (MAC/PHYSICAL to ETH Driver to Netif Glue to TCP/IP Stack)
    ESP_ERROR_CHECK(esp_netif_attach(eth_netif, eth_netif_glue));

    // Register user defined event handlers - Whenever anything happens with Ethernet call Eth Event Handler
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
    // When IP address gotten, call got ip event handler
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL));

    // Start Ethernet driver state machine (Link negotiations, cable detection, DHCP runs)
    ESP_ERROR_CHECK(esp_eth_start(eth_handle));
}


/*-------------MQTT FUNCTION SECTION--------------*/

// Loop Posting to the MQTT Broker with following function
void mqtt_publish_loop(void *param)
{
    esp_mqtt_client_handle_t client = (esp_mqtt_client_handle_t)param;

    while (1) {
        //TODO: Set real values from added sensors
        const char *json = "{\"temp\":22.3,\"humidity\":48.0,\"pressure\":1012.8}";

        // 0,1,0 eventually change to 0,0,0?
        esp_mqtt_client_publish(client, MQTT_TOPIC_PUB, json, 0, MQTT_QOS_LEVEL, MQTT_RETAIN_FLAG);

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
