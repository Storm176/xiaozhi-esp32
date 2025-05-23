#include "mqtt_client_custom.h"
#include "settings.h" // For NVS configuration
#include "system_info.h" // For unique client ID / MAC address
#include "application.h" // Required for GetDeviceState, GetInstance
#include "wifi_station.h" // Required for GetIpAddress
#include <cJSON.h> // Required for JSON manipulation
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h> // For task delay in reconnect

#define CUSTOM_MQTT_TAG "CustomMqtt"
#define RECONNECT_DELAY_MS 5000

CustomMqttClient::CustomMqttClient() {
    // Constructor
}

CustomMqttClient::~CustomMqttClient() {
    Stop();
}

void CustomMqttClient::MqttEventHandler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
    CustomMqttClient* self = static_cast<CustomMqttClient*>(handler_args);
    if (self) {
        self->HandleMqttEvent(static_cast<esp_mqtt_event_handle_t>(event_data));
    }
}

void CustomMqttClient::HandleMqttEvent(esp_mqtt_event_handle_t event) {
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(CUSTOM_MQTT_TAG, "MQTT_EVENT_CONNECTED");
            if (OnConnectionStateChanged) OnConnectionStateChanged(true);
            // Subscribe to command topic upon connection
            Subscribe(device_command_topic_);
            // Publish detailed status upon connection
            PublishDetailedStatus();
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(CUSTOM_MQTT_TAG, "MQTT_EVENT_DISCONNECTED");
            if (OnConnectionStateChanged) OnConnectionStateChanged(false);
            // Optional: Implement reconnection logic here or manage from outside
            // ESP_LOGI(CUSTOM_MQTT_TAG, "Attempting to reconnect in %dms...", RECONNECT_DELAY_MS);
            // vTaskDelay(pdMS_TO_TICKS(RECONNECT_DELAY_MS));
            // esp_mqtt_client_reconnect(client_handle_); // Or Start() again if needed
            break;
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(CUSTOM_MQTT_TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(CUSTOM_MQTT_TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(CUSTOM_MQTT_TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(CUSTOM_MQTT_TAG, "MQTT_EVENT_DATA");
            ESP_LOGI(CUSTOM_MQTT_TAG, "TOPIC=%.*s", event->topic_len, event->topic);
            ESP_LOGI(CUSTOM_MQTT_TAG, "DATA=%.*s", event->data_len, event->data);
            if (OnMessageReceived) {
                std::string topic(event->topic, event->topic_len);
                std::string payload(event->data, event->data_len);
                OnMessageReceived(topic, payload);
            }
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(CUSTOM_MQTT_TAG, "MQTT_EVENT_ERROR");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGE(CUSTOM_MQTT_TAG, "Last error code reported from esp-tls: 0x%x", event->error_handle->esp_tls_last_esp_err);
                ESP_LOGE(CUSTOM_MQTT_TAG, "Last tls stack error number: 0x%x", event->error_handle->esp_tls_stack_err);
            } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                ESP_LOGE(CUSTOM_MQTT_TAG, "Connection refused error: 0x%x", event->error_handle->connect_return_code);
            } else {
                ESP_LOGW(CUSTOM_MQTT_TAG, "Unknown error type: 0x%x", event->error_handle->error_type);
            }
            break;
        default:
            ESP_LOGI(CUSTOM_MQTT_TAG, "Other event id:%d", event->event_id);
            break;
    }
}

bool CustomMqttClient::Start() {
    if (client_handle_) {
        ESP_LOGW(CUSTOM_MQTT_TAG, "Client already started. Call Stop() first.");
        return false;
    }

    Settings settings("custom_mqtt");
    std::string broker_uri = settings.GetString("uri");
    std::string client_id_nvs = settings.GetString("cid");
    std::string username_nvs = settings.GetString("user");
    std::string password_nvs = settings.GetString("pass");

    if (broker_uri.empty()) {
        ESP_LOGE(CUSTOM_MQTT_TAG, "MQTT broker URI not found in NVS (key: custom_mqtt_uri). Cannot start client.");
        return false;
    }

    std::string client_id_to_use = client_id_nvs;
    if (client_id_to_use.empty()) {
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        char mac_str[18];
        sprintf(mac_str, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        client_id_to_use = "xiaozhi-" + std::string(mac_str);
        ESP_LOGI(CUSTOM_MQTT_TAG, "MQTT client ID not found in NVS or empty, generated: %s", client_id_to_use.c_str());
    }
    
    // Define topics based on client_id_to_use
    device_status_topic_ = "xiaozhi/" + client_id_to_use + "/status";
    device_command_topic_ = "xiaozhi/" + client_id_to_use + "/command";

    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.broker.address.uri = broker_uri.c_str();
    if (!username_nvs.empty()) {
        mqtt_cfg.credentials.username = username_nvs.c_str();
    }
    if (!password_nvs.empty()) {
        mqtt_cfg.credentials.authentication.password = password_nvs.c_str();
    }
    mqtt_cfg.credentials.client_id = client_id_to_use.c_str();
    // Note: For ESP-IDF v5 and later, event_handle is deprecated.
    // Use esp_mqtt_client_register_event instead.
    // mqtt_cfg.event_handle = MqttEventHandler; // For older IDF
    // mqtt_cfg.user_context = this; // For older IDF

    // Need to store these strings in a way that their c_str() pointers remain valid
    // for the lifetime of mqtt_cfg. Using members of the class for this.
    // However, esp_mqtt_client_init makes copies of these.
    // For safety, ensure the std::string objects (broker_uri, username_nvs, password_nvs, client_id_to_use)
    // are in scope or their data copied by esp_mqtt_client_init.
    // ESP-IDF's esp_mqtt_client_init copies the necessary fields from the config structure.

    client_handle_ = esp_mqtt_client_init(&mqtt_cfg);
    if (!client_handle_) {
        ESP_LOGE(CUSTOM_MQTT_TAG, "Failed to initialize MQTT client");
        return false;
    }

    esp_err_t err = esp_mqtt_client_register_event(client_handle_, 
                                       (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, 
                                       MqttEventHandler, 
                                       this);
    if (err != ESP_OK) {
        ESP_LOGE(CUSTOM_MQTT_TAG, "Failed to register MQTT event handler: %s", esp_err_to_name(err));
        esp_mqtt_client_destroy(client_handle_);
        client_handle_ = nullptr;
        return false;
    }
    
    // Set LWT (Last Will and Testament) message
    std::string lwt_payload = "{\"online\": false, \"reason\": \"connection_lost\"}";
    err = esp_mqtt_client_set_lwt(client_handle_, device_status_topic_.c_str(), lwt_payload.c_str(), 0, 0, true);
    if (err != ESP_OK) {
        ESP_LOGW(CUSTOM_MQTT_TAG, "Failed to set LWT: %s", esp_err_to_name(err));
    }


    err = esp_mqtt_client_start(client_handle_);
    if (err != ESP_OK) {
        ESP_LOGE(CUSTOM_MQTT_TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
        esp_mqtt_client_destroy(client_handle_);
        client_handle_ = nullptr;
        return false;
    }

    ESP_LOGI(CUSTOM_MQTT_TAG, "MQTT client starting for broker: %s, client_id: %s", broker_uri.c_str(), client_id_to_use.c_str());
    err = esp_mqtt_client_start(client_handle_);
    if (err != ESP_OK) {
        ESP_LOGE(CUSTOM_MQTT_TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
        esp_mqtt_client_destroy(client_handle_);
        client_handle_ = nullptr;
        return false;
    }
    ESP_LOGI(CUSTOM_MQTT_TAG, "MQTT client started successfully.");
    return true;
}

void CustomMqttClient::Stop() {
    if (client_handle_) {
        PublishDeviceStatus(false); // Publish offline status before disconnecting
        esp_mqtt_client_stop(client_handle_);
        esp_mqtt_client_destroy(client_handle_);
        client_handle_ = nullptr;
        ESP_LOGI(CUSTOM_MQTT_TAG, "MQTT client stopped.");
    }
}

bool CustomMqttClient::Publish(const std::string& topic, const std::string& payload, int qos, bool retain) {
    if (!client_handle_ ) {
        ESP_LOGE(CUSTOM_MQTT_TAG, "Client not started or disconnected.");
        return false;
    }
    int msg_id = esp_mqtt_client_publish(client_handle_, topic.c_str(), payload.c_str(), payload.length(), qos, retain);
    if (msg_id == -1) {
        ESP_LOGE(CUSTOM_MQTT_TAG, "Failed to publish message to topic %s", topic.c_str());
        return false;
    }
    ESP_LOGI(CUSTOM_MQTT_TAG, "Published to %s, msg_id=%d", topic.c_str(), msg_id);
    return true;
}

bool CustomMqttClient::Subscribe(const std::string& topic, int qos) {
    if (!client_handle_) {
        ESP_LOGE(CUSTOM_MQTT_TAG, "Client not started.");
        return false;
    }
    int msg_id = esp_mqtt_client_subscribe(client_handle_, topic.c_str(), qos);
    if (msg_id == -1) {
        ESP_LOGE(CUSTOM_MQTT_TAG, "Failed to subscribe to topic %s", topic.c_str());
        return false;
    }
    ESP_LOGI(CUSTOM_MQTT_TAG, "Subscribed to %s, msg_id=%d", topic.c_str(), msg_id);
    return true;
}

void CustomMqttClient::PublishDeviceStatus(bool online) {
    if (!client_handle_) return;

    // Example status payload
    std::string status_payload = "{\"online\": " + std::string(online ? "true" : "false");
    // Potentially add more info: IP, XiaoZhi state (idle, listening, speaking)
    // This would require access to Application::GetInstance().GetDeviceState() or similar
    // For now, just online status.
    status_payload += "}"; 

    Publish(device_status_topic_, status_payload, 1, true); // QoS 1, Retain true
}

void CustomMqttClient::PublishDetailedStatus() {
    if (!client_handle_) { // Check if client is initialized
        ESP_LOGW(CUSTOM_MQTT_TAG, "Client not initialized, cannot publish detailed status.");
        return;
    }
    // Note: Actual connection state is asynchronous. This function is typically called 
    // after MQTT_EVENT_CONNECTED or in response to a command when connected.

    Application& app = Application::GetInstance();
    DeviceState current_app_state = app.GetDeviceState();
    std::string state_str = "unknown";
    // Mapping DeviceState enum to string, ensure application.h has these states
    // For this example, using a simplified mapping based on common states.
    // (kDeviceStateThinking might not exist, adjust as per actual application.h)
    switch (current_app_state) {
        case kDeviceStateIdle: state_str = "idle"; break;
        case kDeviceStateListening: state_str = "listening"; break;
        case kDeviceStateSpeaking: state_str = "speaking"; break;
        // case kDeviceStateThinking: state_str = "thinking"; break; 
        case kDeviceStateStarting: state_str = "starting"; break;
        case kDeviceStateWifiConfiguring: state_str = "configuring"; break;
        case kDeviceStateConnecting: state_str = "connecting"; break;
        case kDeviceStateUpgrading: state_str = "upgrading"; break;
        case kDeviceStateActivating: state_str = "activating"; break;
        case kDeviceStateFatalError: state_str = "fatal_error"; break;
        default: state_str = "unknown_app_state_" + std::to_string(current_app_state); break;
    }

    std::string ip_addr = WifiStation::GetInstance().IsConnected() ? WifiStation::GetInstance().GetIpAddress() : "N/A";

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(CUSTOM_MQTT_TAG, "Failed to create cJSON object for detailed status.");
        return;
    }
    cJSON_AddTrueToObject(root, "online");
    cJSON_AddStringToObject(root, "xiaozhi_state", state_str.c_str());
    cJSON_AddStringToObject(root, "ip_address", ip_addr.c_str());
    cJSON_AddStringToObject(root, "firmware_version", app.GetOta().GetCurrentVersion().c_str()); // Example: Get firmware version
    // Consider adding: SSID, RSSI, MAC, volume, brightness, etc.
    // cJSON_AddStringToObject(root, "ssid", WifiStation::GetInstance().GetSsid().c_str());
    // cJSON_AddNumberToObject(root, "rssi", WifiStation::GetInstance().GetRssi());
    // uint8_t mac[6];
    // esp_read_mac(mac, ESP_MAC_WIFI_STA);
    // char mac_str[18];
    // sprintf(mac_str, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    // cJSON_AddStringToObject(root, "mac_address", mac_str);


    char *json_payload = cJSON_PrintUnformatted(root);
    if (json_payload) {
        Publish(device_status_topic_, std::string(json_payload), 1, true); // QoS 1, Retain true
        ESP_LOGI(CUSTOM_MQTT_TAG, "Published detailed status to %s: %s", device_status_topic_.c_str(), json_payload);
        cJSON_free(json_payload);
    } else {
        ESP_LOGE(CUSTOM_MQTT_TAG, "Failed to print cJSON payload for detailed status.");
    }
    cJSON_Delete(root);
}
