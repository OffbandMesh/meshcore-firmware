#include <AsyncElegantOTA.h>

AsyncElegantOtaClass AsyncElegantOTA;

// Offband #2297: a refused upload's state lives on ITS request
// (AsyncWebServerRequest::_tempObject, freed with the request), never on this
// singleton: a second upload in flight must neither clear nor inherit it. Its
// presence also tells the completion handler to answer 413 and NOT restart.
// Without it that handler sees no Update error (the update never began) and
// replies "OK" and reboots.
namespace {
struct OtaRefusal {
    size_t size;
    size_t slot;
};
}

void AsyncElegantOtaClass::setID(const char* id){
    _id = id;
}

void AsyncElegantOtaClass::begin(AsyncWebServer *server, const char* username, const char* password){
    _server = server;

    if(strlen(username) > 0){
        _authRequired = true;
        _username = username;
        _password = password;
    }else{
        _authRequired = false;
        _username = "";
        _password = "";
    }

    _server->on("/update/identity", HTTP_GET, [&](AsyncWebServerRequest *request){
        if(_authRequired){
            if(!request->authenticate(_username.c_str(), _password.c_str())){
                return request->requestAuthentication();
            }
        }
        #if defined(ESP8266)
            request->send(200, "application/json", "{\"id\": \""+_id+"\", \"hardware\": \"ESP8266\"}");
        #elif defined(ESP32)
            request->send(200, "application/json", "{\"id\": \""+_id+"\", \"hardware\": \"ESP32\"}");
        #endif
    });

    _server->on("/update", HTTP_GET, [&](AsyncWebServerRequest *request){
        if(_authRequired){
            if(!request->authenticate(_username.c_str(), _password.c_str())){
                return request->requestAuthentication();
            }
        }
        AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", ELEGANT_HTML, ELEGANT_HTML_SIZE);
        response->addHeader("Content-Encoding", "gzip");
        request->send(response);
    });

    _server->on("/update", HTTP_POST, [&](AsyncWebServerRequest *request) {
        if(_authRequired){
            if(!request->authenticate(_username.c_str(), _password.c_str())){
                return request->requestAuthentication();
            }
        }
        // the request handler is triggered after the upload has finished...
        // create the response, add header, and send response
        if (request->_tempObject != nullptr) {
            // Offband #2297: refused before Update.begin(); nothing was written,
            // so the current image keeps running. No restart.
            const OtaRefusal* r = static_cast<const OtaRefusal*>(request->_tempObject);
            char msg[176];
            snprintf(msg, sizeof msg,
                     "Image too big for this unit's app slot (%u > %u bytes): a USB flash is needed. "
                     "Nothing was written.",
                     (unsigned)r->size, (unsigned)r->slot);
            AsyncWebServerResponse *refused = request->beginResponse(413, "text/plain", msg);
            refused->addHeader("Connection", "close");
            refused->addHeader("Access-Control-Allow-Origin", "*");
            return request->send(refused);
        }
        AsyncWebServerResponse *response = request->beginResponse((Update.hasError())?500:200, "text/plain", (Update.hasError())?"FAIL":"OK");
        response->addHeader("Connection", "close");
        response->addHeader("Access-Control-Allow-Origin", "*");
        request->send(response);
        restart();
    }, [&](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        //Upload handler chunks in data
        if(_authRequired){
            if(!request->authenticate(_username.c_str(), _password.c_str())){
                return request->requestAuthentication();
            }
        }

        if (request->_tempObject != nullptr) {
            return;   // Offband #2297: this request was refused at its first chunk; swallow the rest unwritten
        }

        if (!index) {
            #if defined(ESP32)
            // Offband #2297: refuse an app image that can't fit, BEFORE Update.begin()
            // erases the target slot. On a repeater that slot holds the previous
            // image, the one a rollback returns to.
            if (_refuseOversize != nullptr && filename != "filesystem") {
                const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
                const size_t length = request->contentLength();
                if (next != nullptr && _refuseOversize(length, next->size)) {
                    OtaRefusal* r = static_cast<OtaRefusal*>(malloc(sizeof(OtaRefusal)));
                    if (r == nullptr) {
                        // Can't record the refusal: answer now rather than fall
                        // through to Update.begin() and erase the slot.
                        return request->send(413, "text/plain", "Image too big for this unit's app slot: a USB flash is needed. Nothing was written.");
                    }
                    r->size = length;
                    r->slot = next->size;
                    request->_tempObject = r;
                    Serial.printf("[ota] refused: request %u bytes, slot %u bytes; nothing written\n",
                                  (unsigned)length, (unsigned)next->size);
                    return;
                }
            }
            #endif
            if(!request->hasParam("MD5", true)) {
                return request->send(400, "text/plain", "MD5 parameter missing");
            }

            if(!Update.setMD5(request->getParam("MD5", true)->value().c_str())) {
                return request->send(400, "text/plain", "MD5 parameter invalid");
            }

            #if defined(ESP8266)
                int cmd = (filename == "filesystem") ? U_FS : U_FLASH;
                Update.runAsync(true);
                size_t fsSize = ((size_t) &_FS_end - (size_t) &_FS_start);
                uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
                if (!Update.begin((cmd == U_FS)?fsSize:maxSketchSpace, cmd)){ // Start with max available size
            #elif defined(ESP32)
                int cmd = (filename == "filesystem") ? U_SPIFFS : U_FLASH;
                if (!Update.begin(UPDATE_SIZE_UNKNOWN, cmd)) { // Start with max available size
            #endif
                Update.printError(Serial);
                return request->send(400, "text/plain", "OTA could not begin");
            }
        }

        // Write chunked data to the free sketch space
        if(len){
            if (Update.write(data, len) != len) {
                return request->send(400, "text/plain", "OTA could not begin");
            }
        }
            
        if (final) { // if the final flag is set then this is the last frame of data
            if (!Update.end(true)) { //true to set the size to the current progress
                Update.printError(Serial);
                return request->send(400, "text/plain", "Could not end OTA");
            }
        }else{
            return;
        }
    });
}

// deprecated, keeping for backward compatibility
void AsyncElegantOtaClass::loop() {
}

void AsyncElegantOtaClass::restart() {
    yield();
    delay(1000);
    yield();
    ESP.restart();
}

String AsyncElegantOtaClass::getID(){
    String id = "";
    #if defined(ESP8266)
        id = String(ESP.getChipId());
    #elif defined(ESP32)
        id = String((uint32_t)ESP.getEfuseMac(), HEX);
    #endif
    id.toUpperCase();
    return id;
}
