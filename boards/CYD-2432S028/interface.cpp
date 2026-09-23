#include "core/bus_HAL.h"
#include "core/powerSave.h"
#include "core/utils.h"
#include <Arduino.h>
#include <interface.h>

#if defined(HAS_CAPACITIVE_TOUCH)
#if defined(TOUCH_GT911_I2C)
#include "TouchDrvGT911.hpp"
TouchDrvGT911 touch;
struct TouchPointPro {
    int16_t x = 0;
    int16_t y = 0;
};
#else
#include "CYD28_TouchscreenC.h"
#define CYD28_DISPLAY_HOR_RES_MAX 240
#define CYD28_DISPLAY_VER_RES_MAX 320
CYD28_TouchC touch(CYD28_DISPLAY_HOR_RES_MAX, CYD28_DISPLAY_VER_RES_MAX);
#endif
#elif defined(USE_TFT_eSPI_TOUCH)
#define XPT2046_CS TOUCH_CS
#else
#include "CYD28_TouchscreenR.h"
#define CYD28_DISPLAY_HOR_RES_MAX 320
#define CYD28_DISPLAY_VER_RES_MAX 240
CYD28_TouchR touch(CYD28_DISPLAY_HOR_RES_MAX, CYD28_DISPLAY_VER_RES_MAX);
#if defined(TOUCH_XPT2046_SPI)
#define XPT2046_CS XPT2046_SPI_CONFIG_CS_GPIO_NUM
#else
#define XPT2046_CS 33
#endif
#endif

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
SPIClass touchSPI;

#if defined(USE_TFT_eSPI_TOUCH)
// The frame the touch targets were drawn in. convertRawXY() never revisits /calData's swap and
// invert flags, so the frame has to be read from the file rather than assumed.
static uint8_t touchCalRotation = ROTATION;
#endif

void _setup_gpio() {
#ifndef HAS_CAPACITIVE_TOUCH // Capacitive Touchscreen uses I2C to communicate
    pinMode(XPT2046_CS, OUTPUT);
    digitalWrite(XPT2046_CS, HIGH);
#endif

#if defined(HAS_CAPACITIVE_TOUCH)
    setSysI2CBus(&Wire1);
#if defined(TOUCH_GT911_I2C)
    bruceConfigPins.sys_i2c.sda = (gpio_num_t)SYS_I2C_SDA;
    bruceConfigPins.sys_i2c.scl = (gpio_num_t)SYS_I2C_SCL;
#else
    bruceConfigPins.sys_i2c.sda = (gpio_num_t)CYD28_TouchC_SDA;
    bruceConfigPins.sys_i2c.scl = (gpio_num_t)CYD28_TouchC_SCL;
#endif
#endif

#if defined(TOUCH_GT911_I2C)
    pinMode(BOARD_TOUCH_INT, INPUT);
    touch.setPins(-1, BOARD_TOUCH_INT);
    if (!touch.begin(Wire1, GT911_SLAVE_ADDRESS_L, SYS_I2C_SDA, SYS_I2C_SCL)) {
        Serial.println("Failed to find GT911 - check your wiring!");
    }
#else
#if !defined(USE_TFT_eSPI_TOUCH) // Use libraries
    if (!touch.begin()) {
        Serial.println("Touch IC not Started");
        log_i("Touch IC not Started");
    } else log_i("Touch IC Started");
#endif
#endif

    bruceConfig.colorInverted = 0;
}

/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
void _post_setup_gpio() {
#if defined(USE_TFT_eSPI_TOUCH)
    pinMode(TOUCH_CS, OUTPUT);
    uint16_t calData[5];
    File caldata = LittleFS.open("/calData", "r");
    bool needsCalibration = !caldata;

    if (caldata) {
        Serial.print("\ntft Calibration data: ");
        for (int i = 0; i < 5; i++) {
            String line = caldata.readStringUntil('\n');
            calData[i] = line.toInt();
            Serial.printf("%d, ", calData[i]);
        }
        // Sixth line, absent in files written before the frame was recorded.
        String rotLine = caldata.readStringUntil('\n');
        rotLine.trim();
        if (rotLine.length() == 1 && rotLine[0] >= '0' && rotLine[0] <= '3') {
            touchCalRotation = rotLine[0] - '0';
        } else {
#if defined(TOUCH_ROTATION_FROM_CALIBRATION)
            // The frame cannot be inferred: builds of this board have written five-line files at
            // rotations 2, 0 and 1. A wrong frame rotates every touch, so recalibrate instead.
            Serial.printf("calibration frame missing or bad ('%s') - recalibrating\n", rotLine.c_str());
            needsCalibration = true;
#else
            if (rotLine.length()) Serial.printf("bad calibration rotation '%s' - ", rotLine.c_str());
            touchCalRotation = ROTATION;
#endif
        }
        if (!needsCalibration) Serial.printf("(calibrated at rotation %d)\n", touchCalRotation);
        caldata.close();
#if defined(TOUCH_ROTATION_FROM_CALIBRATION)
        if (needsCalibration) LittleFS.remove("/calData");
#endif
    }

    if (needsCalibration) {
        tft.setRotation(ROTATION);
        tft.calibrateTouch(calData, TFT_WHITE, TFT_BLACK, 10);
        touchCalRotation = ROTATION;

        caldata = LittleFS.open("/calData", "w");
        if (caldata) {
            caldata.printf(
                "%d\n%d\n%d\n%d\n%d\n%d\n",
                calData[0],
                calData[1],
                calData[2],
                calData[3],
                calData[4],
                touchCalRotation
            );
            caldata.close();
        }
    }

    tft.setTouch(calData);

    // calibrateTouch() draws its targets in ROTATION, but begin_tft() already applied the stored
    // rotation. Put both back or the panel and the layout disagree.
    if (tft.getRotation() != bruceConfigPins.rotation) {
        tft.setRotation(bruceConfigPins.rotation);
        tftWidth = tft.width();
#ifdef HAS_TOUCH
        tftHeight = tft.height() - 20;
#else
        tftHeight = tft.height();
#endif
    }
#endif

    // Brightness control must be initialized after tft in this case @Pirata
    pinMode(TFT_BL, OUTPUT);
    ledcAttach(TFT_BL, TFT_BRIGHT_FREQ, TFT_BRIGHT_Bits);
    ledcWrite(TFT_BL, 255);

    // Force sync color inversion to prevent bruceConf.json from overriding
    // the value set in _setup_gpio(). For CYD variants with TFT_INVERSION_ON,
    // the init() sequence sends INVON; we send INVOFF here to ensure normal colors.
#ifdef TFT_INVERSION_ON
    bruceConfig.colorInverted = 0;
    tft.invertDisplay(0);
#else
    bruceConfig.colorInverted = 1;
    tft.invertDisplay(1);
#endif
}

/*********************************************************************
** Function: setBrightness
** location: settings.cpp
** set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval) {
    int dutyCycle;
    if (brightval == 100) dutyCycle = 255;
    else if (brightval == 75) dutyCycle = 130;
    else if (brightval == 50) dutyCycle = 70;
    else if (brightval == 25) dutyCycle = 20;
    else if (brightval == 0) dutyCycle = 0;
    else dutyCycle = ((brightval * 255) / 100);

    // log_i("dutyCycle for bright 0-255: %d", dutyCycle);
    ledcWrite(TFT_BL, dutyCycle);
}

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    static long d_tmp = 0;
    if (millis() - d_tmp > 200 || LongPress) {
        // I know R3CK.. I Should NOT nest if statements..
        // but it is needed to not keep SPI bus used without need, it save resources
#if defined(USE_TFT_eSPI_TOUCH)
        TouchPoint t;
        checkPowerSaveTime();
        bool _IH_touched = tft.getTouch(&t.x, &t.y);
        if (_IH_touched) {
            NextPress = false;
            PrevPress = false;
            UpPress = false;
            DownPress = false;
            SelPress = false;
            EscPress = false;
            AnyKeyPress = false;
            NextPagePress = false;
            PrevPagePress = false;
            touchPoint.pressed = false;
            _IH_touched = false;
#elif defined(TOUCH_GT911_I2C)
        static unsigned long tm = millis();
        TouchPointPro t;
        uint8_t touched = 0;
        uint8_t rot = 5;

        if (rot != bruceConfigPins.rotation) {
            if (bruceConfigPins.rotation == 1) {
                touch.setMaxCoordinates(TFT_HEIGHT, TFT_WIDTH);
                touch.setSwapXY(true);
                touch.setMirrorXY(false, true);
            }
            if (bruceConfigPins.rotation == 3) {
                touch.setMaxCoordinates(TFT_HEIGHT, TFT_WIDTH);
                touch.setSwapXY(true);
                touch.setMirrorXY(true, false);
            }
            if (bruceConfigPins.rotation == 0) {
                touch.setMaxCoordinates(TFT_WIDTH, TFT_HEIGHT);
                touch.setSwapXY(false);
                touch.setMirrorXY(false, false);
            }
            if (bruceConfigPins.rotation == 2) {
                touch.setMaxCoordinates(TFT_WIDTH, TFT_HEIGHT);
                touch.setSwapXY(false);
                touch.setMirrorXY(true, true);
            }
            rot = bruceConfigPins.rotation;
        }
        // Track touch state to prevent double events on press/release
        static bool lastTouchState = false;
        static unsigned long lastTouchTime = 0;

        touched = touch.getPoint(&t.x, &t.y);
        bool currentTouchState = touched > 0;

        // Only process new touch presses with debouncing
        if (currentTouchState && !lastTouchState && (millis() - lastTouchTime) > 100) {
            // This is a genuine new touch press
            lastTouchTime = millis();
        } else if (!currentTouchState || lastTouchState) {
            // Touch release or continuing touch - ignore
            touched = 0;
        }
        lastTouchState = currentTouchState;
        if (((millis() - tm) > 190 || LongPress) && touched) {
            tm = millis();
#else
        if (touch.touched()) {
            auto t = touch.getPointScaled();
#endif
#if defined(USE_TFT_eSPI_TOUCH) && defined(TOUCH_ROTATION_FROM_CALIBRATION)
            // convertRawXY() only rescales, it never re-maps axes. Undo that, turn by the quarter
            // turns to touchCalRotation, land it in the current frame. In int and clamped: t.x is
            // uint16_t and the old transforms underflowed. Direction follows ST7796_Rotation.h.
            {
                const int w = tft.width(), h = tft.height();
                int cw = (touchCalRotation & 0b01) ? TFT_HEIGHT : TFT_WIDTH;
                int ch = (touchCalRotation & 0b01) ? TFT_WIDTH : TFT_HEIGHT;
                // convertRawXY() multiplies by the display dimension and divides by the span,
                // so the inverse divides by the full dimension too. Integer truncation then costs
                // the last pixel only (319*480/320 = 478), so special-case it. Rescaling by
                // (cw-1)/(w-1) instead would move 159 interior coordinates on the shrinking axis.
                int px = (t.x >= w - 1) ? cw - 1 : (int)t.x * cw / w;
                int py = (t.y >= h - 1) ? ch - 1 : (int)t.y * ch / h;
                for (int i = (touchCalRotation - bruceConfigPins.rotation) & 0b11; i > 0; i--) {
                    int nx = ch - 1 - py;
                    py = px;
                    px = nx;
                    int swap = cw;
                    cw = ch;
                    ch = swap;
                }
                t.x = constrain(px, 0, w - 1);
                t.y = constrain(py, 0, h - 1);
            }
#elif !defined(TOUCH_GT911_I2C)
            // Serial.printf("\nRAW: Touch Pressed on x=%d, y=%d",t.x, t.y);
            if (bruceConfigPins.rotation == 3) {
                t.y = (tftHeight + 20) - t.y;
                t.x = tftWidth - t.x;
            }
            if (bruceConfigPins.rotation == 0) {
                int tmp = t.x;
                t.x = tftWidth - t.y;
                t.y = tmp;
            }
            if (bruceConfigPins.rotation == 2) {
                int tmp = t.x;
                t.x = t.y;
                t.y = (tftHeight + 20) - tmp;
            }
#endif
            // Serial.printf("\nROT: Touch Pressed on x=%d, y=%d\n", t.x, t.y);

            if (!wakeUpScreen()) AnyKeyPress = true;
            else goto END;

            // Touch point global variable
            touchPoint.x = t.x;
            touchPoint.y = t.y;
            touchPoint.pressed = true;
            touchHeatMap(touchPoint);
        END:
            d_tmp = millis();
        }
    }
}

/*********************************************************************
** Function: powerOff
** location: mykeyboard.cpp
** Turns off the device (or try to)
**********************************************************************/
void powerOff() {
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, LOW);
    esp_deep_sleep_start();
}

/*********************************************************************
** Function: checkReboot
** location: mykeyboard.cpp
** Btn logic to turn off the device (name is odd btw)
**********************************************************************/
void checkReboot() {}
