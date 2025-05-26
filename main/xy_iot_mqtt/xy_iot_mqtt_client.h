#ifndef XY_IOT_MQTT_CLIENT_H
#define XY_IOT_MQTT_CLIENT_H

#include <string>
#include <functional>
#include "esp_mqtt_client.h" // ESP-IDF MQTT client
#include "esp_log.h"       // For ESP_LOG logging
#include "iot/thing_manager.h" // Adjust path if necessary based on actual location

// Forward declaration for sdkconfig.h defines
#ifdef __cplusplus
extern "C" {
#endif
#include "sdkconfig.h"
#ifdef __cplusplus
}
#endif

// Callback function type for incoming commands from the IoT platform
using XyIotCommandCallback = std::function<void(const std::string& topic, const std::string& payload)>;

class XyIotMqttClient {
public:
    XyIotMqttClient();
    ~XyIotMqttClient();

    bool Start();
    void Stop();

    bool Publish(const std::string& topic, const std::string& payload, int qos = 0, bool retain = false);
    // Convenience methods
    bool PublishStatus(const std::string& payload, bool retain = false);
    bool PublishControlCommand(const std::string& command_payload, bool retain = false); // Assuming control_topic is fixed or constructed internally

    void RegisterCommandCallback(XyIotCommandCallback callback);

private:
    static void MqttEventHandlerShim(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data);
    void MqttEventHandler(esp_event_base_t base, int32_t event_id, void* event_data);
    void LoadConfig();
    void SendCurrentDeviceStatus();

    esp_mqtt_client_handle_t client_handle_ = nullptr;
    
    std::string broker_url_;
    std::string client_id_;
    std::string username_;
    std::string password_;
    std::string status_topic_;
    std::string control_topic_;
    std::string command_topic_; // Topic to subscribe to for commands

    XyIotCommandCallback command_callback_ = nullptr;
    bool is_configured_ = false; // To check if config was loaded
    bool is_started_ = false;    // To track if Start() was successfully called

    static const char* TAG; 
};

#endif // XY_IOT_MQTT_CLIENT_H
