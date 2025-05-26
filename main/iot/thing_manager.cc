#include "thing_manager.h"
#include "application.h" // Assuming Application::GetInstance() is available and public
#include "xy_iot_mqtt/xy_iot_mqtt_client.h" // For XyIotMqttClient
#include "cJSON.h"       // For cJSON_PrintUnformatted and cJSON_GetObjectItem
#include <esp_log.h>     // For ESP_LOGI, ESP_LOGE, ESP_LOGW
#include "sdkconfig.h"   // For CONFIG_XY_IOT_MQTT_ENABLE


#define TAG "ThingManager"

namespace iot {

void ThingManager::AddThing(Thing* thing) {
    things_.push_back(thing);
}

std::string ThingManager::GetDescriptorsJson() {
    std::string json_str = "[";
    for (auto& thing : things_) {
        json_str += thing->GetDescriptorJson() + ",";
    }
    if (json_str.back() == ',') {
        json_str.pop_back();
    }
    json_str += "]";
    return json_str;
}

bool ThingManager::GetStatesJson(std::string& json, bool delta) {
    if (!delta) {
        last_states_.clear();
    }
    bool changed = false;
    json = "[";
    // 枚举thing，获取每个thing的state，如果发生变化，则更新，保存到last_states_
    // 如果delta为true，则只返回变化的部分
    for (auto& thing : things_) {
        std::string state = thing->GetStateJson();
        if (delta) {
            // 如果delta为true，则只返回变化的部分
            auto it = last_states_.find(thing->name());
            if (it != last_states_.end() && it->second == state) {
                continue;
            }
            changed = true;
            last_states_[thing->name()] = state;
        }
        json += state + ",";
    }
    if (json.back() == ',') {
        json.pop_back();
    }
    json += "]";
    return changed;
}

void ThingManager::Invoke(const cJSON* command) {
    auto name = cJSON_GetObjectItem(command, "name");
    std::string thing_name_str = "";
    bool thing_found_and_invoked = false;

    if (name && name->valuestring) {
        thing_name_str = name->valuestring;
    }

    for (auto& thing : things_) {
        if (thing->name() == thing_name_str) {
            thing->Invoke(command);
            thing_found_and_invoked = true;
            break; 
        }
    }

    if (!thing_found_and_invoked) {
        ESP_LOGW(TAG, "Thing with name '%s' not found for invocation.", thing_name_str.c_str());
        return;
    }

#if CONFIG_XY_IOT_MQTT_ENABLE
    // Attempt to get the Application instance and then the MQTT client
    Application& app = Application::GetInstance(); 
    XyIotMqttClient* iot_mqtt_client = app.GetXyIotMqttClient();

    if (iot_mqtt_client) {
        char* command_str = cJSON_PrintUnformatted(command);
        if (command_str) {
            ESP_LOGI(TAG, "Forwarding command for thing '%s' to XY IoT MQTT platform: %s", thing_name_str.c_str(), command_str);
            
            // Use the PublishControlCommand which publishes to a pre-configured control topic.
            // The payload is the JSON string of the command.
            bool published = iot_mqtt_client->PublishControlCommand(command_str);
            if (published) {
                ESP_LOGI(TAG, "Command successfully published to XY IoT MQTT platform.");
            } else {
                ESP_LOGE(TAG, "Failed to publish command to XY IoT MQTT platform.");
            }
            free(command_str); // Free the string allocated by cJSON_PrintUnformatted
        } else {
            ESP_LOGE(TAG, "Failed to print cJSON command to string for MQTT forwarding.");
        }
    } else {
        ESP_LOGW(TAG, "XY IoT MQTT client instance not available for command forwarding (app.GetXyIotMqttClient() returned null or XY_IOT_MQTT_ENABLE is false at client level).");
    }
#else
    // ESP_LOGD(TAG, "XY IoT MQTT forwarding is disabled via Kconfig."); // Optional debug log
#endif
}

} // namespace iot
