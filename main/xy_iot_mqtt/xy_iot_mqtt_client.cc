#include "xy_iot_mqtt_client.h"
#include <cstring> // For strlen, strcmp
#include "iot/thing_manager.h" // Adjust path if necessary

const char* XyIotMqttClient::TAG = "XyIotMqttClient";

XyIotMqttClient::XyIotMqttClient() {
    LoadConfig();
}

XyIotMqttClient::~XyIotMqttClient() {
    if (is_started_ && client_handle_) {
        ESP_LOGI(TAG, "Stopping MQTT client in destructor");
        esp_mqtt_client_stop(client_handle_);
        esp_mqtt_client_destroy(client_handle_);
        client_handle_ = nullptr;
    }
}

void XyIotMqttClient::LoadConfig() {
#ifdef CONFIG_XY_IOT_MQTT_ENABLE
    broker_url_ = CONFIG_XY_IOT_MQTT_URL;
    client_id_ = CONFIG_XY_IOT_MQTT_CLIENT_ID;
    username_ = CONFIG_XY_IOT_MQTT_USERNAME;
    password_ = CONFIG_XY_IOT_MQTT_PASSWORD;
    status_topic_ = CONFIG_XY_IOT_MQTT_STATUS_TOPIC;
    control_topic_ = CONFIG_XY_IOT_MQTT_CONTROL_TOPIC;
    command_topic_ = CONFIG_XY_IOT_MQTT_COMMAND_TOPIC;
    is_configured_ = true;
    ESP_LOGI(TAG, "XY IoT MQTT configuration loaded.");
    ESP_LOGI(TAG, "URL: %s, ClientID: %s", broker_url_.c_str(), client_id_.c_str());
    ESP_LOGI(TAG, "Status Topic: %s, Control Topic: %s, Command Topic: %s", status_topic_.c_str(), control_topic_.c_str(), command_topic_.c_str());
#else
    is_configured_ = false;
    ESP_LOGI(TAG, "XY IoT MQTT is disabled in Kconfig.");
#endif
}

bool XyIotMqttClient::Start() {
    if (!is_configured_ || !CONFIG_XY_IOT_MQTT_ENABLE) {
        ESP_LOGW(TAG, "MQTT client not started because it's disabled or not configured.");
        return false;
    }
    if (is_started_) {
        ESP_LOGW(TAG, "MQTT client already started.");
        return true;
    }

    ESP_LOGI(TAG, "Starting MQTT client...");
    esp_mqtt_client_config_t mqtt_cfg = {}; // Use C++ style zero-initialization
    mqtt_cfg.broker.address.uri = broker_url_.c_str();
    mqtt_cfg.credentials.client_id = client_id_.c_str();
    if (!username_.empty()) {
        mqtt_cfg.credentials.username = username_.c_str();
    }
    if (!password_.empty()) {
        mqtt_cfg.credentials.authentication.password = password_.c_str();
    }
    // Example LWT: Publish to status topic "offline"
    std::string lwt_msg = "offline";
    mqtt_cfg.session.last_will.topic = status_topic_.c_str();
    mqtt_cfg.session.last_will.msg = lwt_msg.c_str();
    mqtt_cfg.session.last_will.msg_len = lwt_msg.length();
    mqtt_cfg.session.last_will.qos = 1;
    mqtt_cfg.session.last_will.retain = true;


    client_handle_ = esp_mqtt_client_init(&mqtt_cfg);
    if (!client_handle_) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return false;
    }

    esp_err_t err = esp_mqtt_client_register_event(client_handle_, 
                           ESP_EVENT_ANY_ID, 
                           XyIotMqttClient::MqttEventHandlerShim, 
                           this);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register MQTT event handler: %s", esp_err_to_name(err));
        esp_mqtt_client_destroy(client_handle_);
        client_handle_ = nullptr;
        return false;
    }

    err = esp_mqtt_client_start(client_handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
        esp_mqtt_client_destroy(client_handle_);
        client_handle_ = nullptr;
        return false;
    }

    is_started_ = true;
    ESP_LOGI(TAG, "MQTT client started successfully.");
    return true;
}

void XyIotMqttClient::Stop() {
    if (!is_started_ || !client_handle_) {
        ESP_LOGI(TAG, "MQTT client not started or already stopped.");
        return;
    }
    ESP_LOGI(TAG, "Stopping MQTT client...");
    esp_err_t err = esp_mqtt_client_stop(client_handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop MQTT client: %s", esp_err_to_name(err));
    }
    err = esp_mqtt_client_destroy(client_handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to destroy MQTT client: %s", esp_err_to_name(err));
    }
    client_handle_ = nullptr;
    is_started_ = false;
    ESP_LOGI(TAG, "MQTT client stopped.");
}

void XyIotMqttClient::MqttEventHandlerShim(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
    XyIotMqttClient* p_client = static_cast<XyIotMqttClient*>(handler_args);
    if (p_client) {
        p_client->MqttEventHandler(base, event_id, event_data);
    }
}

void XyIotMqttClient::MqttEventHandler(esp_event_base_t base, int32_t event_id, void* event_data) {
    esp_mqtt_event_handle_t event = static_cast<esp_mqtt_event_handle_t>(event_data);
    // esp_mqtt_client_handle_t client = event->client; // Can be used if needed

    switch (static_cast<esp_mqtt_event_id_t>(event_id)) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            SendCurrentDeviceStatus(); // Report full device status
            if (!command_topic_.empty() && command_callback_) {
                // It's generally better to subscribe after ensuring the connection is fully established
                // and initial status is sent.
                int msg_id = esp_mqtt_client_subscribe(client_handle_, command_topic_.c_str(), 0);
                ESP_LOGI(TAG, "Subscribed to command topic '%s', msg_id=%d", command_topic_.c_str(), msg_id);
            }
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
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            ESP_LOGI(TAG, "TOPIC=%.*s", event->topic_len, event->topic);
            ESP_LOGI(TAG, "DATA=%.*s", event->data_len, event->data);
            if (command_callback_) {
                std::string topic_str(event->topic, event->topic_len);
                std::string payload_str(event->data, event->data_len);
                command_callback_(topic_str, payload_str);
            }
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
            if (event->error_handle) {
                ESP_LOGE(TAG, "Last error code: 0x%x", event->error_handle->error_type);
                ESP_LOGE(TAG, "Last error esp_tls_stack_err: 0x%x", event->error_handle->esp_tls_stack_err);
                ESP_LOGE(TAG, "Last error esp_tls_cert_verify_flags: 0x%x", event->error_handle->esp_tls_cert_verify_flags);
                ESP_LOGE(TAG, "Last error esp_transport_sock_errno: 0x%x", event->error_handle->esp_transport_sock_errno);
            }
            break;
        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
    }
}

bool XyIotMqttClient::Publish(const std::string& topic, const std::string& payload, int qos, bool retain) {
    if (!is_started_ || !client_handle_) {
        ESP_LOGE(TAG, "MQTT client not started, cannot publish.");
        return false;
    }
    if (topic.empty()) {
        ESP_LOGE(TAG, "Cannot publish to an empty topic.");
        return false;
    }
    // Check if client is actually connected. Note: esp_mqtt_client_is_connected() is not available.
    // Publishing to a disconnected client will result in failure or message queuing if configured.
    // The ESP-IDF client handles queuing internally up to a limit if offline_queuing is enabled.
    // For simplicity, we'll just attempt to publish.

    int msg_id = esp_mqtt_client_publish(client_handle_, topic.c_str(), payload.c_str(), payload.length(), qos, retain ? 1 : 0);
    if (msg_id == -1) {
        ESP_LOGE(TAG, "Failed to publish message to topic '%s'", topic.c_str());
        return false;
    }
    ESP_LOGI(TAG, "Message published to topic '%s', msg_id=%d", topic.c_str(), msg_id);
    return true;
}

bool XyIotMqttClient::PublishStatus(const std::string& payload, bool retain) {
    if (status_topic_.empty()) {
        ESP_LOGW(TAG, "Status topic is not configured, cannot publish status.");
        return false;
    }
    return Publish(status_topic_, payload, 1, retain); // QoS 1 for status, often good to retain last status
}

bool XyIotMqttClient::PublishControlCommand(const std::string& command_payload, bool retain) {
     if (control_topic_.empty()) {
        ESP_LOGW(TAG, "Control topic is not configured, cannot publish control command.");
        return false;
    }
    return Publish(control_topic_, command_payload, 1, retain); // QoS 1 for commands
}

void XyIotMqttClient::RegisterCommandCallback(XyIotCommandCallback callback) {
    command_callback_ = callback;
    ESP_LOGI(TAG, "Command callback registered.");
    // If already connected and command_topic is set, try to subscribe.
    // This might be better handled by re-subscribing on every MQTT_EVENT_CONNECTED
    // to ensure subscription is active after reconnections. (Done in MqttEventHandler)
}

void XyIotMqttClient::SendCurrentDeviceStatus() {
    if (!is_started_ || !client_handle_) { // Ensure client is supposed to be running
        ESP_LOGW(TAG, "MQTT client not active, cannot send current device status.");
        return;
    }
    if (status_topic_.empty()) {
        ESP_LOGW(TAG, "Status topic is not configured. Cannot send initial device status.");
        return;
    }

    std::string states_json;
    // The 'false' argument to GetStatesJson means get all states, not just deltas.
    bool success = iot::ThingManager::GetInstance().GetStatesJson(states_json, false); 

    if (success && !states_json.empty() && states_json != "[]") { // also check for non-empty array
        ESP_LOGI(TAG, "Publishing current device status to '%s': %s", status_topic_.c_str(), states_json.c_str());
        // Using the generic Publish method. QoS 1 and Retain true are good defaults for status.
        Publish(status_topic_, states_json, 1, true); 
    } else if (success && (states_json.empty() || states_json == "[]")) {
        ESP_LOGI(TAG, "No device states to publish or ThingManager returned empty JSON array. Publishing basic online status.");
        Publish(status_topic_, "{\"status\":\"online\", \"details\":\"no_thing_states\"}", 1, true);
    } else { // success is false
        ESP_LOGE(TAG, "Failed to get device states from ThingManager. Publishing basic online status.");
        Publish(status_topic_, "{\"status\":\"online\", \"details\":\"error_getting_states\"}", 1, true);
    }
}
