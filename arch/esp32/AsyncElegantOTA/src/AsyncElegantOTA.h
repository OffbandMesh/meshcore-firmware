#ifndef AsyncElegantOTA_h
#define AsyncElegantOTA_h

#warning AsyncElegantOTA library is deprecated, Please consider moving to newer ElegantOTA library which now comes with an Async Mode. Learn More: https://docs.elegantota.pro/async-mode/ 

#include "Arduino.h"
#include "stdlib_noniso.h"

#if defined(ESP8266)
    #include "ESP8266WiFi.h"
    #include "ESPAsyncTCP.h"
    #include "flash_hal.h"
    #include "FS.h"
#elif defined(ESP32)
    #include "WiFi.h"
    #include "AsyncTCP.h"
    #include "Update.h"
    #include <esp_ota_ops.h>   // Offband #2297: the target slot's size
#endif

#include "Hash.h"
#include "ESPAsyncWebServer.h"
#include "FS.h"

#include "elegantWebpage.h"


class AsyncElegantOtaClass{

    public:
        void
            setID(const char* id),
            begin(AsyncWebServer *server, const char* username = "", const char* password = ""),
            loop(),
            restart();

        // Offband #2297: decides, before anything is written, whether an app
        // upload is too big for the slot it would go into. Gets the request's
        // Content-Length and the target slot's size; returns true to refuse.
        // The project supplies it (src/helpers/PartitionFit.h holds the rule):
        // this library can't include project headers. Unset, uploads behave as
        // they always did.
        typedef bool (*RefuseOversizeFn)(size_t content_length, size_t slot_size);
        void setRefuseOversize(RefuseOversizeFn fn) { _refuseOversize = fn; }

    private:
        AsyncWebServer *_server;
        RefuseOversizeFn _refuseOversize = nullptr;

        String getID();

        String _id = getID();
        String _username = "";
        String _password = "";
        bool _authRequired = false;

};

extern AsyncElegantOtaClass AsyncElegantOTA;

#endif
