/**
 * @file gps_tracker.h
 * @author Rennan Cockles (https://github.com/rennancockles)
 * @brief GPS tracker
 * @version 0.1
 * @date 2024-11-20
 */

#ifndef __GPS_TRACKER_H__
#define __GPS_TRACKER_H__

#include <TinyGPS++.h>
#include <globals.h>

class GPSTracker {
public:
    /////////////////////////////////////////////////////////////////////////////////////
    // Constructor
    /////////////////////////////////////////////////////////////////////////////////////
    GPSTracker();
    ~GPSTracker();

    /////////////////////////////////////////////////////////////////////////////////////
    // Life Cycle
    /////////////////////////////////////////////////////////////////////////////////////
    void setup();
    void loop();

private:
    bool date_time_updated = false;
    bool initial_position_set = false;
    double cur_lat;
    double cur_lng;
    double distance = 0;
    String filename = "";
    TinyGPSPlus gps;
    // gps.satellites is GGA field 7, the count used in the fix, so it stays 0 until there is one.
    // Satellites in view comes from GSV and is what shows the antenna working while still searching.
    TinyGPSCustom satsInView = TinyGPSCustom(gps, "GPGSV", 3);
    char rawSample[25] = {0};
    uint8_t rawIdx = 0;
    HardwareSerial GPSserial = HardwareSerial(2);
    int gpsCoordCount = 0;
    bool rxPinReleased = false;
    bool consoleDetached = false;

    /////////////////////////////////////////////////////////////////////////////////////
    // Setup
    /////////////////////////////////////////////////////////////////////////////////////
    bool begin_gps(void);
    void end(void);
    void releasePins(void);
    void restorePins(void);

    /////////////////////////////////////////////////////////////////////////////////////
    // Display functions
    /////////////////////////////////////////////////////////////////////////////////////
    void display_banner(void);
    void dump_gps_data(void);

    /////////////////////////////////////////////////////////////////////////////////////
    // Operations
    /////////////////////////////////////////////////////////////////////////////////////
    void set_position(void);
    void add_coord(void);
    void add_initial_file_data(File file);
    void add_final_file_data(void);
    void create_filename(void);
};

#endif // GPS_TRACKER_H
