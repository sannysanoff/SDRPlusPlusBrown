#pragma once
#include <ctime>
#include <string>

class SondeFullData {
public:
    SondeFullData() { init(); }
    void init() {
        serial = "";
        seq = 0;
        time = 0;
        burstkill = 0;
        lat = lon = alt = 0.0f;
        spd = hdg = climb = 0.0f;
        temp = rh = dewpt = pressure = 0.0f;
        calibrated = false;
        calib_percent = 0.0f;
        auxData = "";
    }

    std::string serial;         /* Serial number */
    int seq;                    /* Frame sequence number */
    time_t time;                /* Onboard time */
    int burstkill;              /* Time to shutdown, -1 if inactive */
    float lat, lon, alt;        /* Latitude (degrees), longitude (degrees), altitude (meters) */
    float spd, hdg, climb;      /* Speed (m/s), heading (degrees), climb (m/s) */
    float temp, rh;             /* Temperature (degrees C), relative humidity (%) */
    float dewpt, pressure;      /* Dew point (degrees C), pressure (hPa) */
    bool calibrated;            /* Whether all the calibration data has been received */
    float calib_percent;        /* Calibration status (0-100) */
    std::string auxData;        /* Auxiliary freeform data */
};
