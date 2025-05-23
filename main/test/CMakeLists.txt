idf_component_register(
    SRCS 
        "test_external_mqtt_thing.cpp"
        "test_custom_mqtt_client.cpp"
        # Add other test files here if any
    INCLUDE_DIRS 
        ../ # To access headers in main component
    REQUIRES 
        unity 
        # Add other required components like esp-idf specific ones if directly used by mocks or tested code
        # For example, if CustomMqttClient uses esp_log directly, you might need 'esp_common'
        # For NVS tests
        nvs_flash
)
