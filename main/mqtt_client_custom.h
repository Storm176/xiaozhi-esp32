#ifndef CUSTOM_MQTT_CLIENT_H
#define CUSTOM_MQTT_CLIENT_H

#include "esp_mqtt.h" // Using esp_mqtt_client directly
#include <string>
#include <functional>

class CustomMqttClient {
public:
    CustomMqttClient();
    ~CustomMqttClient();

    // Call this after Wi-Fi is connected
    bool Start(const std::string& broker_url, 
               const std::string& client_id, 
               const std::string& username = "", 
               const std::string& password = "");

    void Stop();

    bool Publish(const std::string& topic, const std::string& payload, int qos = 0, bool retain = false);
    bool Subscribe(const std::string& topic, int qos = 0);

    // Callbacks
    std::function<void(const std::string& topic, const std::string& payload)> OnMessageReceived;
    std::function<void(bool connected)> OnConnectionStateChanged;

    void PublishDetailedStatus(); // New method to publish detailed status

private:
    esp_mqtt_client_handle_t client_handle_ = nullptr;
    std::string device_status_topic_; // Example: "xiaozhi/[client_id]/status"
    std::string device_command_topic_; // Example: "xiaozhi/[client_id]/command"
    // Application& app_instance_; // Option 1: Decided to use Application::GetInstance() directly

    static void MqttEventHandler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data);
    void HandleMqttEvent(esp_mqtt_event_handle_t event);

    void PublishDeviceStatus(bool online); // Internal method to publish basic online/offline status
};

#endif // CUSTOM_MQTT_CLIENT_H
