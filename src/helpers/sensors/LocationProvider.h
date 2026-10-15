#pragma once

#include "Mesh.h"


class LocationProvider {
protected:
    bool _time_sync_needed = true;

public:
    virtual void syncTime() { _time_sync_needed = true; }
    virtual bool waitingTimeSync() { return _time_sync_needed; }
    virtual long getLatitude() = 0;
    virtual long getLongitude() = 0;
    virtual long getAltitude() = 0;
    // #1456: the receiver's fix mode, 1 = none, 2 = 2D, 3 = 3D; 0 when the
    // provider can't tell (its fixes then show as 2D).
    virtual unsigned char fixMode() { return 0; }
    virtual long satellitesCount() = 0;
    virtual bool isValid() = 0;
    virtual long getTimestamp() = 0;
    // #152: the RTC clock this provider keeps in sync (nullptr if none). Lets a
    // manager hand the same clock to a different active provider (UART -> I2C GPS).
    virtual mesh::RTCClock* getClock() { return nullptr; }
    virtual void sendSentence(const char * sentence);
    virtual void reset() = 0;
    virtual void begin() = 0;
    virtual void stop() = 0;
    virtual void loop() = 0;
    virtual bool isEnabled() = 0;
};
