#include "wifi_board.h"
#include "audio_codecs/box_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "iot/thing_manager.h"
#include "led/circular_strip.h"
#include "assets/lang_config.h"

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <esp_lcd_gc9a01.h>
#include <wifi_station.h>

#define TAG "XyDevKitV1"

// 声明字体
LV_FONT_DECLARE(font_puhui_16_4);
LV_FONT_DECLARE(font_awesome_16_4);

/* ADC Buttons */
typedef enum {
    BSP_ADC_BUTTON_REC,        // 2.41V
    BSP_ADC_BUTTON_MODE,       // 1.98V
    BSP_ADC_BUTTON_PLAY,       // 1.65V
    BSP_ADC_BUTTON_SET,        // 1.11V
    BSP_ADC_BUTTON_VOL_DOWN,   // 0.82V
    BSP_ADC_BUTTON_VOL_UP,     // 0.38V
    BSP_ADC_BUTTON_NUM
} bsp_adc_button_t;

class XyDevKitV1 : public WifiBoard {
private:
    i2c_master_bus_handle_t codec_i2c_bus_;
    Button boot_button_;
    Button* adc_button_[BSP_ADC_BUTTON_NUM];
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    adc_oneshot_unit_handle_t bsp_adc_handle = NULL;
#endif
    Display* display_;

    // I2C初始化
    void InitializeI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &codec_i2c_bus_));
    }

    // SPI初始化
    void InitializeSpi() {
        ESP_LOGI(TAG, "Initialize SPI bus");
        spi_bus_config_t buscfg = GC9A01_PANEL_BUS_SPI_CONFIG(DISPLAY_SPI_SCK_PIN, DISPLAY_SPI_MOSI_PIN, 
                                    DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t));
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    // GC9A01初始化
    void InitializeGc9a01Display() {
        ESP_LOGI(TAG, "Init GC9A01 display");

        ESP_LOGI(TAG, "Install panel IO");
        esp_lcd_panel_io_handle_t io_handle = NULL;
        esp_lcd_panel_io_spi_config_t io_config = GC9A01_PANEL_IO_SPI_CONFIG(DISPLAY_SPI_CS_PIN, DISPLAY_DC_PIN, NULL, NULL);
        io_config.pclk_hz = DISPLAY_SPI_SCLK_HZ;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &io_handle));
    
        ESP_LOGI(TAG, "Install GC9A01 panel driver");
        esp_lcd_panel_handle_t panel_handle = NULL;
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_SPI_RST_PIN;    // Set to -1 if not use
        panel_config.rgb_endian = LCD_RGB_ENDIAN_BGR;           //LCD_RGB_ENDIAN_RGB;
        panel_config.bits_per_pixel = 16;                       // Implemented by LCD command `3Ah` (16/18)

        ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(io_handle, &panel_config, &panel_handle));
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, true, false));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true)); 

        display_ = new SpiLcdDisplay(io_handle, panel_handle,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
                                    {
                                        .text_font = &font_puhui_16_4,
                                        .icon_font = &font_awesome_16_4,
                                        .emoji_font = font_emoji_64_init(),
                                    });
    }

    void changeVol(int val) {
        auto codec = GetAudioCodec();
        auto volume = codec->output_volume() + val;
        if (volume > 100) {
            volume = 100;
        }
        if (volume < 0) {
            volume = 0;
        }
        codec->SetOutputVolume(volume);
        GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
    }
    
    void TogleState() {
        auto& app = Application::GetInstance();
        if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
            ResetWifiConfiguration();
        }
        app.ToggleChatState();        
    }

    void InitializeButtons() {
        button_adc_config_t adc_cfg;
        adc_cfg.adc_channel = ADC_CHANNEL_5;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)        
        const adc_oneshot_unit_init_cfg_t init_config1 = {
            .unit_id = ADC_UNIT_1,
        };
        adc_oneshot_new_unit(&init_config1, &bsp_adc_handle);
        adc_cfg.adc_handle = &bsp_adc_handle;
#endif
        adc_cfg.button_index = BSP_ADC_BUTTON_REC;
        adc_cfg.min = 2200;
        adc_cfg.max = 2600;
        adc_button_[0] = new AdcButton(adc_cfg);

        adc_cfg.button_index = BSP_ADC_BUTTON_MODE;
        adc_cfg.min = 1800;
        adc_cfg.max = 2100;
        adc_button_[1] = new AdcButton(adc_cfg);

        adc_cfg.button_index = BSP_ADC_BUTTON_PLAY;
        adc_cfg.min = 1500;
        adc_cfg.max = 1800;
        adc_button_[2] = new AdcButton(adc_cfg);

        adc_cfg.button_index = BSP_ADC_BUTTON_SET;
        adc_cfg.min = 1000;
        adc_cfg.max = 1300;
        adc_button_[3] = new AdcButton(adc_cfg);

        adc_cfg.button_index = BSP_ADC_BUTTON_VOL_DOWN;
        adc_cfg.min = 700;
        adc_cfg.max = 1000;
        adc_button_[4] = new AdcButton(adc_cfg);

        adc_cfg.button_index = BSP_ADC_BUTTON_VOL_UP;
        adc_cfg.min = 280;
        adc_cfg.max = 500;
        adc_button_[5] = new AdcButton(adc_cfg);

        auto volume_up_button = adc_button_[BSP_ADC_BUTTON_VOL_UP];
        volume_up_button->OnClick([this]() {changeVol(10);});
        volume_up_button->OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(100);
            GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME);
        });

        auto volume_down_button = adc_button_[BSP_ADC_BUTTON_VOL_DOWN];
        volume_down_button->OnClick([this]() {changeVol(-10);});
        volume_down_button->OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(0);
            GetDisplay()->ShowNotification(Lang::Strings::MUTED);
        });

        auto break_button = adc_button_[BSP_ADC_BUTTON_PLAY];
        break_button->OnClick([this]() {TogleState();});
        boot_button_.OnClick([this]() {TogleState();});
    }

    void InitializeIot() {
        auto& thing_manager = iot::ThingManager::GetInstance();
        thing_manager.AddThing(iot::CreateThing("Speaker"));
        thing_manager.AddThing(iot::CreateThing("Screen"));
        thing_manager.AddThing(iot::CreateThing("Lamp"));
    }

public:
    // 构造函数
    XyDevKitV1() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeI2c();
        InitializeSpi();
        InitializeGc9a01Display();
        InitializeButtons();
        InitializeIot();
        GetBacklight()->SetBrightness(100);
    }

    virtual Led* GetLed() override {
        static CircularStrip led(BUILTIN_LED_GPIO, 12);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
        static BoxAudioCodec audio_codec(
            codec_i2c_bus_, 
            AUDIO_INPUT_SAMPLE_RATE, 
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S0_GPIO_MCLK, 
            AUDIO_I2S0_GPIO_BCLK, 
            AUDIO_I2S0_GPIO_WS, 
            AUDIO_I2S1_GPIO_MCLK, 
            AUDIO_I2S1_GPIO_BCLK, 
            AUDIO_I2S1_GPIO_WS, 
            AUDIO_I2S0_GPIO_DOUT, 
            AUDIO_I2S1_GPIO_DIN,
            AUDIO_CODEC_PA_PIN, 
            AUDIO_CODEC_ES8311_ADDR, 
            AUDIO_CODEC_ES7210_ADDR, 
            AUDIO_INPUT_REFERENCE);
        return &audio_codec;
    }

    // 获取显示屏
    virtual Display* GetDisplay() override {
        return display_;
    }
    
    // 获取背光控制
    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }
};

DECLARE_BOARD(XyDevKitV1);