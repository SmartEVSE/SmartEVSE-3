/*
;    Project: Smart EVSE v3
;
;
; Permission is hereby granted, free of charge, to any person obtaining a copy
; of this software and associated documentation files (the "Software"), to deal
; in the Software without restriction, including without limitation the rights
; to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
; copies of the Software, and to permit persons to whom the Software is
; furnished to do so, subject to the following conditions:
;
; The above copyright notice and this permission notice shall be included in
; all copies or substantial portions of the Software.
;
; THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
; IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
; FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
; AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
; LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
; OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
; THE SOFTWARE.
 */

#ifndef __EVSE_NETWORK

#define __EVSE_NETWORK

#include "mongoose.h"

#ifndef MQTT
#define MQTT 1  // Uncomment or set to 0 to disable MQTT support in code
#endif

#ifndef MQTT_ESP
#if MQTT
#define MQTT_ESP 1   //set to 0 to use mongoose MQTT
                     //set to 1 to use ESP MQTT
                     //mongoose uses less resources but sends malformed MQTT packages sometimes
#endif
#endif

#if MQTT_ESP
#define MQTT 1          // make sure that if MQTT_ESP is set, that MQTT is set too
#include "mqtt_client.h"
#endif

#define FREE(x) free(x); x = NULL;

#if MQTT
// MQTT connection info
extern String MQTTuser;
extern String MQTTpassword;
extern String MQTTprefix;
extern String MQTTHost;
extern uint16_t MQTTPort;
extern uint8_t lastMqttUpdate;

class MQTTclient_t {
#if MQTT_ESP == 0
private:
    struct mg_mqtt_opts default_opts;
#endif
public:
#if MQTT_ESP == 0
    //constructor
    MQTTclient_t () {
        memset(&default_opts, 0, sizeof(default_opts));
        default_opts.qos = 0;
        default_opts.retain = false;
    }
#else
    void connect(void);
#endif
    void publish(const String &topic, const int32_t &payload, bool retained, int qos) { publish(topic, String(payload), retained, qos); };
    void publish(const String &topic, const String &payload, bool retained, int qos);
    void subscribe(const String &topic, int qos);
    bool connected;
#if MQTT_ESP == 0
    struct mg_connection *s_conn;
#else
    esp_mqtt_client_handle_t client;
#endif
};

extern MQTTclient_t MQTTclient;
extern void SetupMQTTClient();
extern void mqtt_receive_callback(const String topic, const String payload);
#endif //MQTT

// wrapper so hasParam and getParam still work
class webServerRequest {
private:
    struct mg_http_message *hm_internal;
    String _value;
    char temp[64];

public:
    void setMessage(struct mg_http_message *hm);
    bool hasParam(const char *param);
    webServerRequest* getParam(const char *param); // Return pointer to self
    const String& value(); // Return the string value
};

extern bool shouldReboot;
extern void network_loop(void);
extern String APhostname;
extern webServerRequest* request;
extern struct mg_mgr mgr;
extern String TZinfo;
extern uint8_t WIFImode;
extern char *downloadUrl;
extern uint32_t serialnr;
extern void RunFirmwareUpdate(void);
extern void WiFiSetup(void);
extern void handleWIFImode(void *s = &Serial);
extern bool getLatestVersion(String owner_repo, String asset_name, char *version);

#define FW_DOWNLOAD_PATH "http://smartevse-3.s3.eu-west-2.amazonaws.com"

#define OWNER_FACT "SmartEVSE"
#define REPO_FACT "SmartEVSE-3"
#define OWNER_COMM "rob040"
#define REPO_COMM "SmartEVSE-3"


#endif
