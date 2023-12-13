/* MQTT Sensor Sender for Home Assistant
   
   Reads a temperature & humidity probe (SHT20, SDA on IO21 and SCL on IO22)
   and sends the data to Home Assistant (or anything else) via MQTT. Hold a 
   button low on IO27 at boot to set the configuration variables. Also sends
   the current battery voltage read from IO34 via a divider network of 
   2 x 1M resistors. Config allows you to calibrate the battery voltage by
   measuring it and entering the real value. Uses a DF Robot Firebeetle 
   ESP32-E board which has the button and battery divider on board, but
   will operate with any ESP32 if properly compiled. It also expects a time
   feed from MQTT on homeassistant/CurrentTime, and uses that to synchronise
   the sensor updates to every 15 minutes (approximately, it will vary a
   little and retransmit attempts will make it late).

   Copyright 2023 Phillip C Dimond

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

*/

#include "main.h"

#define DEBUG 1 // Set to 1 to dump debugging info to the serial port

esp_err_t err;
int retry_num = 0;
char s[80]; // general purpose string input
float temperature = 0.0;
float humidity = 0.0;
float battVolts = 0.0;
bool WiFiGotIP = false;
bool sentMeasurements = false;
int mqttMessagesQueued = 0;
uint64_t timeToDeepSleep = (S_TO_uS(SLEEPTIME));
bool gotTime = false;
int year = 0, month = 0, day = 0, hour = 0, minute = 0, seconds = 0;


static const char *TAG = "MqttHaSensor";

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        if (DEBUG) { printf("Last error %s: 0x%x\r\n", message, error_code); }
    }
}

static void wifi_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_STA_START)
    {
        if (DEBUG) { printf("WIFI CONNECTING....\r\n"); }
    }
    else if (event_id == WIFI_EVENT_STA_CONNECTED)
    {
        if (DEBUG) { printf("WiFi CONNECTED\r\n"); }
    }
    else if (event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (DEBUG) { printf("WiFi lost connection\r\n"); }
        if (retry_num < 5)
        {
            esp_wifi_connect();
            retry_num++;
            if (DEBUG) { printf("Retrying to Connect...\r\n"); }
        }
    }
    else if (event_id == IP_EVENT_STA_GOT_IP)
    {
        WiFiGotIP = true;
        if (DEBUG) { printf("Wifi got IP...\n\n"); }
    }
}

void wifi_connection()
{
    err = nvs_flash_init();
    if (err != ESP_OK) { printf("Error at nvs_flash_init: %d = %s.\r\n", err, esp_err_to_name(err)); }
    err = esp_netif_init();                                                                    // network interdace initialization
    if (err != ESP_OK) { printf("Error at esp_netif_init: %d = %s.\r\n", err, esp_err_to_name(err)); }
    err = esp_event_loop_create_default();                                                     // responsible for handling and dispatching events
    if (err != ESP_OK) { printf("Error at esp_event_loop_create_default: %d = %s.\r\n", err, esp_err_to_name(err)); }
    esp_netif_create_default_wifi_sta();                                                 // sets up necessary data structs for wifi station interface
    wifi_init_config_t wifi_initiation = WIFI_INIT_CONFIG_DEFAULT();                     // sets up wifi wifi_init_config struct with default values
    err = esp_wifi_init(&wifi_initiation);                                               // wifi initialised with dafault wifi_initiation
    if (err != ESP_OK) { printf("Error at esp_wifi_init: %d = %s.\r\n", err, esp_err_to_name(err)); }
    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);  // creating event handler register for wifi
    if (err != ESP_OK) { printf("Error at esp_event_handler_register(WIFI_EVENT: %d = %s.\r\n", err, esp_err_to_name(err)); }
    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL); // creating event handler register for ip event
    if (err != ESP_OK) { printf("Error at esp_event_handler_register(IP_EVENT: %d = %s.\r\n", err, esp_err_to_name(err)); }
    wifi_config_t wifi_configuration = {                                                 // struct wifi_config_t var wifi_configuration
        .sta = {
            // we are sending a const char of ssid and password which we will strcpy in following line so leaving it blank
            .ssid = "",
            .password = ""
        }
    };
    strcpy((char*)wifi_configuration.sta.ssid, config.ssid);    
    strcpy((char*)wifi_configuration.sta.password, config.pass);   
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_configuration);  // setting up configs when event ESP_IF_WIFI_STA
    esp_wifi_start();       // start connection with configurations provided in funtion
    esp_wifi_set_mode(WIFI_MODE_STA);   // station mode selected
    esp_wifi_connect(); // connect with saved ssid and pass
    printf( "wifi_init_softap finished. SSID:%s  password:%s", config.ssid, config.pass);
} 

/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    char topic[80];
    char payload[1024];
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;

    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");

        // Subscribe to the time feed
        msg_id = esp_mqtt_client_subscribe(client, "homeassistant/CurrentTime", 0);
        ESP_LOGI(TAG, "Subscribe sent for time feed, msg_id=%d", msg_id);

        // Subscribe to the command feed
        sprintf(s, "homeassistant/number/%s/command", config.Name);
        msg_id = esp_mqtt_client_subscribe(client, s, 0);
        ESP_LOGI(TAG, "Subscribe sent for the command feed, msg_id=%d", msg_id);

        // Send the relay configuration
        sprintf(topic, "homeassistant/number/%s/config",config.Name);
        sprintf(payload, "{\"unique_id\": \"T_%s\", \
            \"device\": {\"identifiers\": [\"%s\"], \"name\": \"%s\"}, \
            \"availability\": {\"topic\": \"homeassistant/number/%s/availability\", \"payload_available\": \"online\", \"payload_not_available\": \"offline\"}, \
            \"min\":0, \"max\":15, \"retain\":true, \
            \"command_topic\": \"homeassistant/number/%s/command\"}"
            ,config.UID, config.DeviceID, config.Name, config.Name, config.Name);
        msg_id = esp_mqtt_client_publish(client, topic, payload, 0, 1, 1); // Temp sensor config, set the retain flag on the message
        mqttMessagesQueued++;
        ESP_LOGI(TAG, "Published Envoy Relay config message successfully, msg_id=%d", msg_id);

        // Send an online message
        sprintf(topic, "homeassistant/number/%s/availability", config.Name);
        sprintf(payload, "online");
        msg_id = esp_mqtt_client_publish(client, topic, payload, 0, 1, 1); // Temp sensor config, set the retain flag on the message
        mqttMessagesQueued++;
        ESP_LOGI(TAG, "Published Envoy Relay online message successfully, msg_id=%d", msg_id);

        /*

        sprintf(topic, "homeassistant/sensor/%sHumidity/config", config.Name);
        sprintf(payload, "{\"device_class\": \"humidity\", \"state_topic\": \"homeassistant/sensor/%s/state\", \
            \"unit_of_measurement\": \"%%\", \"value_template\": \"{{ value_json.humidity}}\",\"unique_id\": \"H_%s\", \
            \"device\": {\"identifiers\": [\"%s\"], \"name\": \"%s\" } }", config.Name, config.UID, config.DeviceID, config.Name);
        msg_id = esp_mqtt_client_publish(client, topic, payload, 0, 1, 1); // Humidity sensor config, set the retain flag on the message
        mqttMessagesQueued++;
        ESP_LOGI(TAG, "Published humidity config message successfully, msg_id=%d", msg_id);

        sprintf(topic,"homeassistant/sensor/%sVoltage/config", config.Name);
        sprintf(payload, "{\"device_class\": \"voltage\", \"state_topic\": \"homeassistant/sensor/%s/state\", \
            \"unit_of_measurement\": \"V\", \"value_template\": \"{{ value_json.voltage}}\",\"unique_id\": \"B_%s\", \
            \"device\": {\"identifiers\": [\"%s\"], \"name\": \"%s\" } }", config.Name, config.UID, config.DeviceID, config.Name);
        msg_id = esp_mqtt_client_publish(client, topic, payload, 0, 1, 1); // Humidity sensor config, set the retain flag on the message
        mqttMessagesQueued++;
        ESP_LOGI(TAG, "Published temperature config message successfully, msg_id=%d", msg_id);

        // Publish the current values
        sprintf(topic, "homeassistant/sensor/%s/state", config.Name);
        sprintf(payload, "{ \"temperature\": %.1f, \"humidity\": %.1f, \"voltage\": %.2f }", temperature, humidity, battVolts);
        msg_id = esp_mqtt_client_publish(client, topic, payload, 0, 1, 0); // Humidity sensor config, donm't retain
        mqttMessagesQueued++;
        ESP_LOGI(TAG, "Published sensor state message successfully, msg_id=%d", msg_id);

        sentMeasurements = true;
        */
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        mqttMessagesQueued--;
        break;
    case MQTT_EVENT_DATA:
        //ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        strncpy(s, event->topic, event->topic_len);
        s[event->topic_len] = '\0';
        printf("Received an event - topic was %s\r\n", s);
        if (strcmp(s, "homeassistant/CurrentTime") == 0) {
            // Process the time
            printf("Got the time from %s, as %.*s.\r\n", s, event->data_len, event->data);
            gotTime = true;
            strncpy(s, event->data, event->data_len);
            s[event->data_len] = 0;
            sscanf(s, "%d.%d.%d %d:%d:%d", &year, &month, &day, &hour, &minute, &seconds);        
        } else if (strstr(s, "command") != NULL) {
            strncpy(s, event->data, event->data_len);
            s[event->data_len] = 0;
            printf("Received command %s\r\n", s);
        }
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

static void mqtt_app_start(void)
{
    char lwTopic[100];
    sprintf(lwTopic, "homeassistant/number/%s/availability", config.Name);
    const char* lwMessage = "offline\0";
    esp_mqtt_client_config_t mqtt_cfg = {
        .network = {
            .reconnect_timeout_ms = 250, // Reconnect MQTT broker after this many ms
        },
        .broker.address.uri = config.mqttBrokerUrl,
        .credentials = { 
            .username = config.mqttUsername, 
            .authentication = { 
                .password = config.mqttPassword
            }, 
        },
        .session = {
            .message_retransmit_timeout = 250,  // ms transmission retry
            .protocol_ver = MQTT_PROTOCOL_V_3_1_1,
            .keepalive = 30, // 30 second keepalive timeout
            .last_will = {
                .topic = lwTopic,
                .msg = (const char*)lwMessage,
                .msg_len = strlen(lwMessage),
                .qos = 1,
                .retain = 1
            }
        },
    };
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

void app_main(void)
{
    bool configMode = false;

    // GPIO setup
    gpio_set_direction(BUTTON_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_PIN, GPIO_PULLUP_ONLY);

    if (gpio_get_level(BUTTON_PIN) == 0) {
        printf("Button was pushed.\r\n");
        configMode = true;
    }

    // Initialise the SPIFFS system
    esp_vfs_spiffs_conf_t spiffs_conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true};
    
    err = esp_vfs_spiffs_register(&spiffs_conf);
    if (err != ESP_OK)
    {
        printf("SPIFFS Mount Failed: %s\r\n", esp_err_to_name(err));
        printf("Reformatting the SPIFFs partition, please restart.");
        return;
    }

    // Load the configuration from the file system
    bool configLoad = LoadConfiguration();
    if (configLoad == false || config.configOK == false) 
    {
        if (configLoad == false)
        {
            if (DEBUG)
            {
                printf("Loading the configuration failed. Please enter the configuration details.\r\n");
            }
        }
        else if (config.configOK == false)
        {
            if (DEBUG)
            {
                printf("The stored configuration is marked as invalid. Please enter the configuration details.\r\n");
            }
        }
        UserConfigEntry();
    }
    else if (DEBUG)
    {
        printf("Loaded config: configOK: %d, Name: %s, Device ID: %s\r\n", config.configOK, config.Name, config.DeviceID);
        printf("               UID: %s, battVCalFactor: %fV\r\n", config.UID, config.battVCalFactor);
        printf("               WiFi SSID: %s, WiFi Password: %s\r\n", config.ssid, config.pass);
        printf("               MQTT URL: %s, Username: %s, Password: %s\r\n", config.mqttBrokerUrl, config.mqttUsername, config.mqttPassword);
    }
    
    // If we're in config mode, ask if the user wants to change the config
    if (configMode) {
        printf("\r\nDo you want to change the configuration (y/n)? ");
        char c = 'n';
        if (getLineInput(s, 1) > 0) { c = s[0]; }
        printf("\r\n");
        if (c == 'y' || c == 'Y') { UserConfigEntry(); }
    }

    // Start WiFi, wait for WiFi to connect and get IP
    wifi_connection();
    int loops = 0;
    while (loops < 10000 && !WiFiGotIP) {
        vTaskDelay(2000 / portTICK_PERIOD_MS); // Wait 10 millseconds
    }

    // Start mqtt
    mqtt_app_start();

    // Loop forever, processing MQTT events.
    while(true) {
        vTaskDelay(2000 / portTICK_PERIOD_MS * 100); // Sleep for 1 second
    }

/*
    // Wait for all message transmission and reception to finish, or timeout
    bool timedOut = false;
    printf("Waiting for MQTT transmission to complete.\r\n");
    int64_t st = esp_timer_get_time();
    while (!timedOut && (!sentMeasurements  || !gotTime || mqttMessagesQueued > 0 )) {
        vTaskDelay(100 / portTICK_PERIOD_MS); 
        if (esp_timer_get_time() - st > S_TO_uS(5)) { 
            printf("Timed out waiting for mqtt transmission to complete. sentMeasurements=%d, gotTime=%d, mqttMessagesQueued=%d\r\n",
                sentMeasurements, gotTime, mqttMessagesQueued);
            timedOut = true;
        }
    }

    // Prepare sleep time calculation if we didn't timeout on transmission
    if (!timedOut || config.retries >= 5) {
        uint64_t timePastQuarterHour = S_TO_uS((uint64_t)(minute * 60 + seconds));   // in microseconds
        uint64_t quarterHour = S_TO_uS((uint64_t)(15 * 60));            // 15 minutes in microseconds
        while (timePastQuarterHour > quarterHour) { timePastQuarterHour -= quarterHour; } // get this to the num. secs since last quarter hour
        timeToDeepSleep = (quarterHour - timePastQuarterHour); // want to sleep to the next 15 minute time
        // add a little hysteresis if close to 15 mins as the timer will sometimes undershoot if we just add 15 minutes, so we
        // wake up just before then sleep for a couple of seconds and wake & send again. Battery waste!
        if (timeToDeepSleep < S_TO_uS(60)) { timeToDeepSleep += S_TO_uS(960); } 

        if (DEBUG) {
            if (config.retries < 5) { printf("Got everything and sent everything. Preparing to sleep.\r\n"); }
            else {printf("We've tries to send stuff after restarting 5 times, giving up. Preparing to sleep.\r\n");}
            printf("Time value was %d minutes and %d seconds past the hour.\r\n", minute, seconds);
            printf("Will deep sleep for %lld seconds.\r\n", uS_TO_S(timeToDeepSleep));
            config.retries = 0;
        }
    } else {        
        timeToDeepSleep = (S_TO_uS(5)); // deep sleep for 5 seconds and try again
        config.retries++;
        if (DEBUG) { printf("We timed out trying to send messages so we'll only sleep for 5 seconds. This will be attempt #%d.\r\n", config.retries + 1); }
    }

    // All done, save config then unmount partition and disable SPIFFS
    SaveConfiguration();
    err = esp_vfs_spiffs_unregister(spiffs_conf.partition_label);
    if ( err != ESP_OK) {
        printf("SPIFFS deregistration: Error %d = %s.\r\n", err, esp_err_to_name(err));
    }
    printf("SPIFFS unmounted.\r\n");

    // Go to sleep
    if (DEBUG) { printf("Sleeping for %lld seconds.\r\n", uS_TO_S(timeToDeepSleep)); }
    if (esp_sleep_enable_timer_wakeup(timeToDeepSleep) != ESP_OK)
    {
        while (true)
        {
            printf ("Error setting sleep time. Value must be out of range.\r\n");
        }
    } 
    esp_deep_sleep_start();

    // If we got here the deep sleep function failed
    while (true)
    {
        printf("Deep sleep initiation failed!\r\n");
        vTaskDelay(250 / portTICK_PERIOD_MS); // 250ms sleep
    } 
*/
}
