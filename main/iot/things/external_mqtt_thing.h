#ifndef EXTERNAL_MQTT_THING_H
#define EXTERNAL_MQTT_THING_H

#include "iot/thing.h"
#include "mqtt_client_custom.h" // Forward declaration might be better if CustomMqttClient.h includes this. For now, direct include.
#include <string>

// Forward declare CustomMqttClient if it causes circular dependency issues
// class CustomMqttClient; 

namespace iot {

class ExternalMqttThing : public Thing {
public:
    ExternalMqttThing(const std::string& name, 
                      const std::string& description, 
                      CustomMqttClient* mqtt_client, 
                      const std::string& command_topic,
                      const std::string& on_payload = "{\"power\": true}", // Default payload
                      const std::string& off_payload = "{\"power\": false}"); // Default payload

    // Example method to add specific controls like TurnOn, TurnOff, SetBrightness
    void AddOnOffMethods(); 
    // void AddBrightnessMethod(); // Example for later

private:
    CustomMqttClient* custom_mqtt_client_;
    std::string command_topic_;
    bool current_power_state_; // Simple internal state for demo
    std::string on_payload_;
    std::string off_payload_;

    void SetPower(bool on);
};

} // namespace iot

#endif // EXTERNAL_MQTT_THING_H
