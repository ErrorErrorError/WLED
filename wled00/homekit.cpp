#include "wled.h"

#if !defined(WLED_DISABLE_HOMEKIT) && defined(ARDUINO_ARCH_ESP32)

void updateOn(bool on);
void updateBrightness(int brightness);
void updateColor(double hue, double sat);

void homekitInit() {
    if (WLED_CONNECTED) {
        if (homeKitDevice == nullptr) {
            Serial.println("Starting HomeKit");
            homeSpan.setPortNum(8080);
            homeSpan.begin(Category::Lighting, "WLED Light");

            new SpanAccessory();
                new Service::AccessoryInformation();
                    new Characteristic::Name("WLED Light");
                    new Characteristic::Manufacturer("WLED");
                    new Characteristic::SerialNumber("WLED-111");
                    new Characteristic::Model("WLED-Device");
                    new Characteristic::FirmwareRevision("0.1");
                    new Characteristic::Identify();

                new Service::HAPProtocolInformation();
                    new Characteristic::Version("1.1.0");

            uint16_t hsv[3] = {0};
            colorFromRGB(col[0], col[1], col[2], hsv);

            long brightness = map(bri, 0, 255, 0, 100);
            homeKitDevice = new HomeKitDevice(
                brightness > 0,
                brightness,
                hsv,
                updateOn,
                updateBrightness,
                updateColor
            );
        } else {
            homeSpan.restart();
        }
    } else {
        EHK_DEBUGLN("WLED not connected to wifi, will not start Homekit.");
    }
}

void handleHomeKit() {
    if (!WLED_CONNECTED) return;
    if (homeKitDevice != nullptr) {
        homeSpan.poll();
    }
}

// Callbacks

void updateOn(bool on) {
    if (on) {
        if (bri == 0) {
            bri = briLast;
            stateUpdated(CALL_MODE_HOMEKIT);
        }
    } else {
        if (bri > 0) {
            briLast = bri;
            bri = 0;
            stateUpdated(CALL_MODE_HOMEKIT);
        } 
    }
}

void updateBrightness(int brightness) {
        bri = map(brightness, 0, 100, 0, UINT8_MAX);
        stateUpdated(CALL_MODE_HOMEKIT);
}

void updateColor(double hue, double sat) {
    byte rgb[4] {0};
    uint16_t hueMapped = map(hue, 0, 360, 0, UINT16_MAX);
    uint8_t satMapped = map(sat, 0, 100, 0, UINT8_MAX);

    colorHStoRGB(hueMapped, satMapped, rgb);

    uint32_t color = ((rgb[0] << 16) | (rgb[1] << 8) | (rgb[2]));

    strip.setColor(0, color);
    stateUpdated(CALL_MODE_HOMEKIT);
}

#else
 void homekitInit(){}
 void handleHomeKit(){}
#endif
