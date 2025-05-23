#include "external_mqtt_thing.h"
#include <esp_log.h>
#include <cJSON.h> // For creating JSON payloads

#define EXT_MQTT_THING_TAG "ExtMqttThing"

namespace iot {

ExternalMqttThing::ExternalMqttThing(const std::string& name, 
                                     const std::string& description, 
                                     CustomMqttClient* mqtt_client, 
                                     const std::string& command_topic,
                                     const std::string& on_payload,
                                     const std::string& off_payload)
    : Thing(name, description), 
      custom_mqtt_client_(mqtt_client), 
      command_topic_(command_topic),
      current_power_state_(false), // Default to off
      on_payload_(on_payload),
      off_payload_(off_payload) {

    if (!custom_mqtt_client_) {
        ESP_LOGE(EXT_MQTT_THING_TAG, "CustomMqttClient pointer is null for %s", name_.c_str());
    }
    
    // Add a simple readable property for the assumed power state
    properties_.AddBooleanProperty("power", "The current power state (true for on, false for off)",
        [this]() -> bool {
            return current_power_state_;
        }
    );
}

void ExternalMqttThing::SetPower(bool on) {
    if (!custom_mqtt_client_) {
        ESP_LOGE(EXT_MQTT_THING_TAG, "Cannot SetPower for %s, MQTT client not available.", name().c_str());
        return;
    }

    const std::string& payload_to_send = on ? on_payload_ : off_payload_;

    bool published = custom_mqtt_client_->Publish(command_topic_, payload_to_send, 1, false); // QoS 1, No Retain

    if (published) {
        ESP_LOGI(EXT_MQTT_THING_TAG, "Published SetPower (%s) for %s to topic %s with payload: %s", 
                 on ? "ON" : "OFF", name().c_str(), command_topic_.c_str(), payload_to_send.c_str());
        current_power_state_ = on; // Update internal state
        // Note: This is an assumed state. No confirmation from actual device here.
    } else {
        ESP_LOGE(EXT_MQTT_THING_TAG, "Failed to publish SetPower for %s to topic %s", name().c_str(), command_topic_.c_str());
    }
}

void ExternalMqttThing::AddOnOffMethods() {
    methods_.AddMethod("TurnOn", "Turn the device on", ParameterList(), 
        [this](const ParameterList& parameters) {
            SetPower(true);
        }
    );

    methods_.AddMethod("TurnOff", "Turn the device off", ParameterList(),
        [this](const ParameterList& parameters) {
            SetPower(false);
        }
    );

    methods_.AddMethod("SetPowerState", "Set the power state of the device", 
        ParameterList({Parameter("power", "True for on, false for off", kValueTypeBoolean, true)}),
        [this](const ParameterList& parameters) {
            bool power_val = parameters["power"].boolean();
            SetPower(power_val);
        }
    );
}

} // namespace iot
