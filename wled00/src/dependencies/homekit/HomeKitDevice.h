#pragma once

#include "HomeSpan/HomeSpan.h"

typedef void (*UpdatedPowerCallback)(bool power);
typedef void (*UpdatedLevelCallback)(int level);
typedef void (*UpdatedColorCallback)(double hue, double saturation);

struct HomeKitDevice : Service::LightBulb {
    UpdatedPowerCallback cbPower;
    UpdatedLevelCallback cbLevel;
    UpdatedColorCallback cbColor;

    SpanCharacteristic *power;
    SpanCharacteristic *level;
    SpanCharacteristic *hue;
    SpanCharacteristic *saturation;

    HomeKitDevice(
        bool on, 
        int brightness,
        uint16_t * hsv,
        UpdatedPowerCallback cbP, 
        UpdatedLevelCallback  cbL, 
        UpdatedColorCallback cbC
    ) : Service::LightBulb() {
        power = new Characteristic::On(on);
        level = new Characteristic::Brightness(brightness);
        hue = new Characteristic::Hue(hsv[0]);
        saturation = new Characteristic::Saturation(map(hsv[1], 0, UINT8_MAX, 0, 100));
        cbPower = cbP;
        cbLevel = cbL;
        cbColor = cbC;
    }

    boolean update() {
        if (power->updated()) {
            cbPower(power->getNewVal());
        }

        if (level->updated()) {
            cbLevel(level->getNewVal());
        }

        if (hue->updated() || saturation->updated()) {
            cbColor(hue->getNewVal<double>(), saturation->getNewVal<double>());
        }

        return(true);
    }
};