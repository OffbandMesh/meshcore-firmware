#pragma once

#include "LocationProvider.h"
#include <MicroNMEA.h>
#include <RTClib.h>
#include <helpers/RefCountedDigitalPin.h>
#include <MeshLog.h>  // #411: [GPSPARSE] routed through the serial-capture sink

#ifndef GPS_EN
    #ifdef PIN_GPS_EN
        #define GPS_EN PIN_GPS_EN
    #else
        #define GPS_EN (-1)
    #endif
#endif

#ifndef PIN_GPS_EN_ACTIVE
    #define PIN_GPS_EN_ACTIVE HIGH
#endif

#ifndef GPS_RESET
    #ifdef PIN_GPS_RESET
        #define GPS_RESET PIN_GPS_RESET
    #else
        #define GPS_RESET (-1)
    #endif
#endif

#ifndef GPS_RESET_FORCE
    #ifdef PIN_GPS_RESET_ACTIVE
        #define GPS_RESET_FORCE PIN_GPS_RESET_ACTIVE
    #else
        #define GPS_RESET_FORCE LOW
    #endif
#endif

#ifndef GPS_LOOP_MAX_BYTES
    // #216: max NMEA bytes ingested per loop() (see loop()). Headroom over the
    // 115200 steady-state per-loop arrival while bounding the worst case so the
    // GPS read can never monopolize the loop / starve BLE. Tunable per board.
    #define GPS_LOOP_MAX_BYTES 96
#endif

class MicroNMEALocationProvider : public LocationProvider {
    char _nmeaBuffer[100];
    MicroNMEA nmea;
    mesh::RTCClock* _clock;
protected :
    Stream* _gps_serial;   // Offband (#193): protected so GPS subclasses (e.g. ATGM336H) can reach it
private :
    RefCountedDigitalPin* _peripher_power;
    int8_t _claims = 0;
    int _pin_reset;
    int _pin_en;
    long next_check = 0;
    long time_valid = 0;
    unsigned long _last_time_sync = 0;
    static const unsigned long TIME_SYNC_INTERVAL = 1800000; // Re-sync every 30 minutes

public :
    MicroNMEALocationProvider(Stream& ser, mesh::RTCClock* clock = NULL, int pin_reset = GPS_RESET, int pin_en = GPS_EN,RefCountedDigitalPin* peripher_power=NULL) :
    _gps_serial(&ser), nmea(_nmeaBuffer, sizeof(_nmeaBuffer)), _pin_reset(pin_reset), _pin_en(pin_en), _clock(clock), _peripher_power(peripher_power) {
        if (_pin_reset != -1) {
            pinMode(_pin_reset, OUTPUT);
            digitalWrite(_pin_reset, GPS_RESET_FORCE);
        }
        if (_pin_en != -1) {
            pinMode(_pin_en, OUTPUT);
            digitalWrite(_pin_en, LOW);
        }
    }

    void claim() {
        _claims++;
        if (_claims > 0) {
            if (_peripher_power) _peripher_power->claim();
        }
    }

    void release() {
        if (_claims == 0) return; // avoid negative _claims
        _claims--;
        if (_peripher_power) _peripher_power->release();
    }

    void begin() override {
        claim();
        if (_pin_en != -1) {
            digitalWrite(_pin_en, PIN_GPS_EN_ACTIVE);
        }
        if (_pin_reset != -1) {
            digitalWrite(_pin_reset, !GPS_RESET_FORCE);
        }
    }

    void reset() override {
        if (_pin_reset != -1) {
            digitalWrite(_pin_reset, GPS_RESET_FORCE);
            delay(10);
            digitalWrite(_pin_reset, !GPS_RESET_FORCE);
        }
    }

    void stop() override {
        if (_pin_en != -1) {
            digitalWrite(_pin_en, !PIN_GPS_EN_ACTIVE);
        }
        if (_pin_reset != -1) {
            digitalWrite(_pin_reset, GPS_RESET_FORCE);
        }
        release();
    }

    bool isEnabled() override {
        // directly read the enable pin if present as gps can be
        // activated/deactivated outside of here ...
        if (_pin_en != -1) {
            return digitalRead(_pin_en) == PIN_GPS_EN_ACTIVE;
        } else {
            return true; // no enable so must be active
        }
    }

    void syncTime() override { nmea.clear(); LocationProvider::syncTime(); }
    long getLatitude() override { return nmea.getLatitude(); }
    long getLongitude() override { return nmea.getLongitude(); }
    long getAltitude() override { 
        long alt = 0;
        nmea.getAltitude(alt);
        return alt;
    }
    long satellitesCount() override { return nmea.getNumSatellites(); }
    bool isValid() override { return nmea.isValid(); }

    long getTimestamp() override {
        // #216: the calendar date (y/mo/d) only arrives once RMC carries it; until
        // then MicroNMEA's date fields are 0 and DateTime(0,0,0,...) wraps to a
        // garbage NEGATIVE epoch (date2days(0,0,0) underflows). Report 0 = "no GPS
        // time yet" so callers (serial [GPS] line, 0xC1 query, client) can tell
        // "acquiring" from a real fix instead of seeing wrapped garbage.
        if (nmea.getYear() == 0) return 0;
        DateTime dt(nmea.getYear(), nmea.getMonth(),nmea.getDay(),nmea.getHour(),nmea.getMinute(),nmea.getSecond());
        return dt.unixtime();
    }

    mesh::RTCClock* getClock() override { return _clock; }  // #152: expose clock for provider hand-off

    void sendSentence(const char *sentence) override {
        nmea.sendSentence(*_gps_serial, sentence);
    }

    void loop() override {

        // #216: BOUND the per-loop GPS ingestion. The upstream unbounded
        // `while (available())` drain monopolizes the main loop at high baud --
        // an M100 @115200 (multi-constellation, 10Hz) delivers ~11.5 KB/s, and
        // draining it every loop starves BLE servicing (recv_queue overflow ->
        // slog/wedge; observed on the companion, never on a 9600 L76K). Cap the
        // bytes processed per loop so the loop ALWAYS yields to BLE; the UART
        // buffer holds the rest, and any lost backlog is fine -- we only need a
        // periodic fix, not every sentence.
        int _gps_budget = GPS_LOOP_MAX_BYTES;
        while (_gps_budget-- > 0 && _gps_serial->available()) {
            char c = _gps_serial->read();
            #ifdef GPS_NMEA_DEBUG
            Serial.print(c);
            #endif
            nmea.process(c);
        }

        if (!isValid()) time_valid = 0;

        if (millis() > next_check) {
            next_check = millis() + 1000;
            // Re-enable time sync periodically when GPS has valid fix
            if (!_time_sync_needed && _clock != NULL && (millis() - _last_time_sync) > TIME_SYNC_INTERVAL) {
                _time_sync_needed = true;
            }
            if (_time_sync_needed && time_valid > 2) {
                if (_clock != NULL) {
                    // #216: time_valid tracks the POSITION fix, which can lead the
                    // date. Only sync the RTC once getTimestamp() returns a real
                    // (date-backed) epoch -- otherwise we'd set the clock to 1970.
                    long ts = getTimestamp();
                    if (ts > 0) {
                        _clock->setCurrentTime(ts);
                        _time_sync_needed = false;
                        _last_time_sync = millis();
                    }
                }
            }
            if (isValid()) {
                time_valid ++;
            }
        }

#ifdef GPS_PARSE_DEBUG
        // #216 diagnostic: show exactly what MicroNMEA PARSED (vs the raw stream),
        // once/sec, to localize the bad-time bug -- does the parser capture the RMC
        // date or not? Low-volume so it doesn't drop under setTxTimeoutMs(0).
        static long _gpsparse_t = 0;
        if (millis() - _gpsparse_t >= 1000) {
            _gpsparse_t = millis();
            // #411: route through the sink (captured + mirrored where serial is free).
            mesh_log_line(MLOG_DEBUG,
                "[GPSPARSE] valid=%d y=%d mo=%d d=%d h=%d mi=%d s=%d ts=%ld\n",
                nmea.isValid() ? 1 : 0, (int)nmea.getYear(), (int)nmea.getMonth(),
                (int)nmea.getDay(), (int)nmea.getHour(), (int)nmea.getMinute(),
                (int)nmea.getSecond(), (long)getTimestamp());
        }
#endif
    }
};
