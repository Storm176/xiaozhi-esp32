#include "unity.h"
#include "iot/things/external_mqtt_thing.h" // Path to your actual header
#include "mqtt_client_custom.h" // For the actual CustomMqttClient, though we mock it
#include <string>
#include <vector>
#include "cJSON.h" // For cJSON_Parse, cJSON_CreateObject etc.

// --- Mock CustomMqttClient ---
// This mock will be used by ExternalMqttThing
class MockPublishInterface { // Interface for testability
public:
    virtual ~MockPublishInterface() = default;
    virtual bool Publish(const std::string& topic, const std::string& payload, int qos = 0, bool retain = false) = 0;
};


// Actual CustomMqttClient needs to be adapted or we use a more complex mock.
// For this test, let's assume CustomMqttClient can be simplified to an interface
// or we use a specific mock class that doesn't need full CustomMqttClient functionality.

// This struct is a simplified mock for the purpose of testing ExternalMqttThing.
// It does not inherit from CustomMqttClient to avoid ESP-IDF dependencies in this specific mock.
// The reinterpret_cast will be used, understanding its risks.
struct MockMqttClientForExternalThing {
    std::string last_topic;
    std::string last_payload;
    int last_qos = 0;
    bool last_retain = false;
    int publish_calls = 0;
    bool connected_state = true; // Assume connected for tests

    // This method name must match what ExternalMqttThing calls (i.e. CustomMqttClient's method name)
    bool Publish(const std::string& topic, const std::string& payload, int qos = 0, bool retain = false) {
        if (!connected_state) return false;
        last_topic = topic;
        last_payload = payload;
        last_qos = qos;
        last_retain = retain;
        publish_calls++;
        return true;
    }
    // Add a dummy IsConnected for ExternalMqttThing if it starts using it.
    bool IsConnected() const { return connected_state; } 
};


// --- Test Cases ---
static MockMqttClientForExternalThing mock_mqtt_client_instance;

// Helper to reset mock before each test
void reset_mock_mqtt_client() {
    mock_mqtt_client_instance = MockMqttClientForExternalThing();
}

static void test_external_thing_turn_on_default_payload() {
    reset_mock_mqtt_client();
    iot::ExternalMqttThing thing("TestLight", "A test light", 
                               reinterpret_cast<CustomMqttClient*>(&mock_mqtt_client_instance), 
                               "test/light/command");
    thing.AddOnOffMethods();
    
    cJSON* cmd = cJSON_CreateObject();
    cJSON_AddStringToObject(cmd, "method", "TurnOn");
    thing.Invoke(cmd); 
    cJSON_Delete(cmd);

    TEST_ASSERT_EQUAL_STRING("test/light/command", mock_mqtt_client_instance.last_topic.c_str());
    TEST_ASSERT_EQUAL_STRING("{\"power\": true}", mock_mqtt_client_instance.last_payload.c_str());
    TEST_ASSERT_EQUAL(1, mock_mqtt_client_instance.publish_calls);
    TEST_ASSERT_TRUE(thing.properties_["power"].boolean()); // Check property after command
}

static void test_external_thing_turn_off_custom_payload() {
    reset_mock_mqtt_client();
    iot::ExternalMqttThing thing("TestFan", "A test fan", 
                               reinterpret_cast<CustomMqttClient*>(&mock_mqtt_client_instance), 
                               "test/fan/command", 
                               "{\"state\":\"POWER_ON\"}", 
                               "{\"state\":\"POWER_OFF\"}");
    thing.AddOnOffMethods();

    cJSON* cmd = cJSON_CreateObject();
    cJSON_AddStringToObject(cmd, "method", "TurnOff");
    thing.Invoke(cmd);
    cJSON_Delete(cmd);

    TEST_ASSERT_EQUAL_STRING("test/fan/command", mock_mqtt_client_instance.last_topic.c_str());
    TEST_ASSERT_EQUAL_STRING("{\"state\":\"POWER_OFF\"}", mock_mqtt_client_instance.last_payload.c_str());
    TEST_ASSERT_FALSE(thing.properties_["power"].boolean());
}

static void test_external_thing_set_power_state_true() {
    reset_mock_mqtt_client();
    iot::ExternalMqttThing thing("TestDevice", "A device", 
                               reinterpret_cast<CustomMqttClient*>(&mock_mqtt_client_instance), 
                               "test/device/command");
    thing.AddOnOffMethods();

    cJSON* cmd = cJSON_CreateObject();
    cJSON_AddStringToObject(cmd, "method", "SetPowerState");
    cJSON* params = cJSON_CreateObject();
    cJSON_AddTrueToObject(params, "power");
    cJSON_AddItemToObject(cmd, "parameters", params);
    thing.Invoke(cmd);
    cJSON_Delete(cmd);
    
    TEST_ASSERT_EQUAL_STRING("test/device/command", mock_mqtt_client_instance.last_topic.c_str());
    TEST_ASSERT_EQUAL_STRING("{\"power\": true}", mock_mqtt_client_instance.last_payload.c_str());
    TEST_ASSERT_TRUE(thing.properties_["power"].boolean());
}


// Main test runner for this file
extern "C" void app_main_test_external_mqtt_thing(); // Ensure C linkage if called from C main
void app_main_test_external_mqtt_thing() {
    UNITY_BEGIN();
    RUN_TEST(test_external_thing_turn_on_default_payload);
    RUN_TEST(test_external_thing_turn_off_custom_payload);
    RUN_TEST(test_external_thing_set_power_state_true);
    UNITY_END();
}

// If this is the main entry point for all tests, it might look like:
// extern "C" void app_main_test_custom_mqtt_client(); // Declare other test main
// 
// extern "C" void app_main() {
//     ESP_LOGI("UnitTest", "Starting All Tests...");
//     app_main_test_external_mqtt_thing();
//     app_main_test_custom_mqtt_client();
//     ESP_LOGI("UnitTest", "All Tests Finished.");
// }
