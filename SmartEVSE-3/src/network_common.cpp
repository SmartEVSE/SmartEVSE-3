#if defined(ESP32)

#include <WiFi.h>
#include <vector>
#include "mbedtls/md_internal.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha256.h"
#include "utils.h"
#include "network_common.h"
#include "glcd.h"
#include "esp32.h"
#include <ArduinoJson.h>

#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <Preferences.h>
#include "esp_efuse.h"

#ifndef SENSORBOX_VERSION
#include "esp32.h"
#endif

#if SMARTEVSE_VERSION >=30
#include "OneWire.h"
#endif

#ifndef DEBUG_DISABLED
RemoteDebug Debug;
#endif

#define SNTP_GET_SERVERS_FROM_DHCP 1
#include <esp_sntp.h>

struct tm timeinfo;
bool LocalTimeSet = false;

//mongoose stuff
#include "esp_log.h"
struct mg_mgr mgr;  // Mongoose event manager. Holds all connections
// end of mongoose stuff

String APhostname;
String APpassword = "00000000";

#if MQTT
// MQTT connection info
String MQTTuser;
String MQTTpassword;
String MQTTprefix;
String MQTTHost = "";
uint16_t MQTTPort;
mg_timer *MQTTtimer;
uint8_t lastMqttUpdate = 0;
bool MQTTtls = false;
bool MQTTSmartServer = false;               // Use mqtt.smartevse.nl server, can be set from the LCD menu
bool MQTTSmartServerChanged = false;        // Flag to trigger reconnect from network_loop()
bool WIFImodeChanged = false;               // Flag to trigger handleWIFImode() from network_loop()
String MQTTprivatePassword;                 // mqtt.smartevse.nl pre calculated password (hash of ec_private key)
#endif

// WebSocket LCD image timer and connection tracking
mg_timer *LCDImageTimer = nullptr;
std::vector<mg_connection*> wsLcdConnections;

static void stopLCDImageTimer(struct mg_mgr *manager) {
    if (LCDImageTimer != nullptr && manager != nullptr) {
        mg_timer_free(&manager->timers, LCDImageTimer);
        LCDImageTimer = nullptr;
        _LOG_V("Stopped LCD image timer\n");
    }
}

static bool isTrackedLcdWsConnection(const mg_connection *connection) {
    for (const auto *tracked : wsLcdConnections) {
        if (tracked == connection) return true;
    }
    return false;
}

static void sendWsError(struct mg_connection *c, const char *reason) {
    char buf[96];
    int n = snprintf(buf, sizeof(buf), "{\"error\":\"%s\"}", reason);
    if (n > 0) mg_ws_send(c, buf, (size_t)n, WEBSOCKET_OP_TEXT);
}

mg_connection *HttpListener80, *HttpListener443;

bool shouldReboot = false;

extern void write_settings(void);
extern void StopwebServer(void); //TODO or move over to network.cpp?
extern void StartwebServer(void); //TODO or move over to network.cpp?
extern bool handle_URI(struct mg_connection *c, struct mg_http_message *hm,  webServerRequest* request);
extern uint8_t AutoUpdate;
extern uint16_t firmwareUpdateTimer;

uint32_t serialnr = 0;


// The following data will be updated by eeprom/storage data at powerup:
uint8_t WIFImode = WIFI_MODE;                                               // WiFi Mode (0:Disabled / 1:Enabled / 2:Start Portal)
String TZinfo = "";                                                         // contains POSIX time string
String TZname = "";                                                         // contains timezone name (e.g. Europe/Amsterdam)

char *downloadUrl = NULL;
int downloadProgress = 0;
int downloadSize = 0;

#if MQTT
#if MQTT_ESP == 1
/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, esp_mqtt_event_t *event) {
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        MQTTclient.connected = true;
        SetupMQTTClient();
        break;
    case MQTT_EVENT_DISCONNECTED:
        MQTTclient.connected = false;
        break;
    case MQTT_EVENT_DATA:
        {
        String topic2 = String(event->topic).substring(0,event->topic_len);
        String payload2 = String(event->data).substring(0,event->data_len);
        //_LOG_A("Received MQTT EVENT DATA: topic=%s, payload=%s.\n", topic2.c_str(), payload2.c_str());
        mqtt_receive_callback(topic2, payload2);
        }
        break;
    case MQTT_EVENT_ERROR:
        _LOG_I("MQTT_EVENT_ERROR; Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        break;
    default:
        break;
    }
}


void MQTTclient_t::connect(void) {
    if (MQTTHost == "") return;
    
    // Stop and destroy old client if exists to prevent memory leak
    if (client) {
        esp_mqtt_client_stop(client);
        esp_mqtt_client_destroy(client);
        client = nullptr;
    }
    
    static String ca_cert_str;
    if (MQTTtls) {
        ca_cert_str = readMqttCaCert();
        if (ca_cert_str.length() < 10) {
            ca_cert_str = root_ca_letsencrypt;
            _LOG_A("No CA cert in LittleFS, using LetsEncrypt as default");
        }
    }
    
    static char s_mqtt_url[80];
    snprintf(s_mqtt_url, sizeof(s_mqtt_url), "%s://%s:%i", MQTTtls ? "mqtts" : "mqtt", MQTTHost.c_str(), MQTTPort);
    static String lwtTopic;
    lwtTopic = MQTTprefix + "/connected";
    esp_mqtt_client_config_t mqtt_cfg = { .uri = s_mqtt_url, .client_id=MQTTprefix.c_str(), .username=MQTTuser.c_str(), .password=MQTTpassword.c_str(), .lwt_topic=lwtTopic.c_str(), .lwt_msg="offline", .lwt_qos=0, .lwt_retain=1, .lwt_msg_len=7, .keepalive=15, .buffer_size=512, .out_buffer_size=512 };
    
    if (MQTTtls) {
        mqtt_cfg.cert_pem = ca_cert_str.c_str();
        _LOG_D("Using CA cert (%d bytes).\n", ca_cert_str.length());
    }
    _LOG_A("MQTT connecting to %s as %s\n", MQTTHost.c_str(), MQTTprefix.c_str());

    client = esp_mqtt_client_init(&mqtt_cfg);
    if (!client) {
        _LOG_A("MQTT: esp_mqtt_client_init failed (heap: %u)\n", ESP.getFreeHeap());
        return;
    }
    esp_err_t err = esp_mqtt_client_register_event(client, (esp_mqtt_event_id_t) ESP_EVENT_ANY_ID, (esp_event_handler_t) mqtt_event_handler, NULL);
    if (err != ESP_OK) {
        _LOG_A("MQTT: esp_mqtt_client_register_event failed: %d\n", err);
    }
    // Start now if any network interface is connected (WiFi or Ethernet)
    if (NetworkConnected()) {
        err = esp_mqtt_client_start(client);
        if (err != ESP_OK) {
            _LOG_A("MQTT: esp_mqtt_client_start failed: %d\n", err);
        }
    }
}

void MQTTclient_t::disconnect(void) {
    connected = false;  // Set flag first to prevent event handler from using client
    if (client) {
        esp_mqtt_client_publish(client, (MQTTprefix + "/connected").c_str(), "offline", 7, 0, 1);
        esp_mqtt_client_stop(client);
        vTaskDelay(50 / portTICK_PERIOD_MS);
        esp_mqtt_client_destroy(client);
        client = nullptr;
    }
}
#endif


//wrapper so MQTTClient::Publish works
void MQTTclient_t::publish(const String &topic, const String &payload, bool retained, int qos) {
#if MQTT_ESP == 0
    if (s_conn && connected) {
        struct mg_mqtt_opts opts = default_opts;
        opts.topic = mg_str(topic.c_str());
        opts.message = mg_str(payload.c_str());
        opts.qos = qos;
        opts.retain = retained;
        mg_mqtt_pub(s_conn, &opts);
    }
#else
    if (connected && client)
        esp_mqtt_client_publish(client, topic.c_str(), payload.c_str(), payload.length(), qos, retained);
#endif
}

void MQTTclient_t::publish(const char *topic, const char *payload, size_t payload_len, bool retained, int qos) {
#if MQTT_ESP == 0
    if (s_conn && connected) {
        struct mg_mqtt_opts opts = default_opts;
        opts.topic = mg_str(topic);
        opts.message = mg_str_n(payload, payload_len);
        opts.qos = qos;
        opts.retain = retained;
        mg_mqtt_pub(s_conn, &opts);
    }
#else
    if (connected && client)
        esp_mqtt_client_publish(client, topic, payload, (int)payload_len, qos, retained);
#endif
}

void MQTTclient_t::subscribe(const String &topic, int qos) {
#if MQTT_ESP == 0
    if (s_conn && connected) {
        struct mg_mqtt_opts opts = default_opts;
        opts.topic = mg_str(topic.c_str());
        opts.qos = qos;
        mg_mqtt_sub(s_conn, &opts);
    }
#else
    if (connected && client)
        esp_mqtt_client_subscribe(client, topic.c_str(), qos);
#endif
}


void MQTTclient_t::announce(const String& entity_name, const String& domain, const String& optional_payload) {
    announce(entity_name.c_str(), domain.c_str(), optional_payload.c_str());
}

void MQTTclient_t::announce(const char *entity_name, const char *domain, const char *optional_payload) {
    // Build entity_suffix (entity_name with spaces removed) on the stack.
    char suffix[64];
    { size_t j = 0; const char *s = entity_name;
      while (*s && j + 1 < sizeof(suffix)) { if (*s != ' ') suffix[j++] = *s; s++; }
      suffix[j] = 0; }

    const char *prefix = MQTTprefix.c_str();
    const char *dom = domain;

    // default_entity_id: <domain>.<prefix>_<suffix>, lowercased, '-' -> '_'.
    char did[128];
    int dn = snprintf(did, sizeof(did), "%s.%s_%s", dom, prefix, suffix);
    if (dn > 0) {
        for (int i = 0; i < dn && i < (int)sizeof(did) - 1; i++) {
            char c = did[i];
            if (c >= 'A' && c <= 'Z') did[i] = c + ('a' - 'A');
            else if (c == '-') did[i] = '_';
        }
    }

    char topic[160];
    snprintf(topic, sizeof(topic), "homeassistant/%s/%s-%s/config", dom, prefix, suffix);

#ifndef SENSORBOX_VERSION
    const char *model = "SmartEVSE v3";
#else
    const char *model = "Sensorbox v2";
#endif

    char payload[1024];
    IPAddress ip = WiFi.localIP();
    int n = snprintf(payload, sizeof(payload),
        "{\"name\":\"%s\","
        "\"object_id\":\"%s-%s\","        // Deprecated for HA 2026.4, kept for back-compat.
        "\"default_entity_id\":\"%s\","    // HA 2025.10+: must include domain prefix.
        "\"unique_id\":\"%s-%s\","
        "\"state_topic\":\"%s/%s\","
        "\"availability_topic\":\"%s/connected\","
        "\"device\":{\"model\":\"%s\",\"identifiers\":\"%s\",\"name\":\"%s\","
        "\"manufacturer\":\"Stegen\",\"configuration_url\":\"http://%u.%u.%u.%u\","
        "\"sw_version\":\"%s\"}",
        entity_name,
        prefix, suffix,
        did,
        prefix, suffix,
        prefix, suffix,
        prefix,
        model, prefix, prefix,
        ip[0], ip[1], ip[2], ip[3],
        VERSION);
    if (n < 0 || n >= (int)sizeof(payload)) return;

    // optional_payload always starts with ", " (jsna prefix) and goes inside the
    // top-level object, before the closing '}'. NULL or "" is allowed.
    size_t opt_len = (optional_payload ? strlen(optional_payload) : 0);
    if (n + opt_len + 2 >= sizeof(payload)) return;
    if (opt_len) { memcpy(payload + n, optional_payload, opt_len); n += opt_len; }
    payload[n++] = '}';
    payload[n]   = 0;

    publish(topic, payload, (size_t)n, true, 0);  // Retain + QoS 0
}

MQTTclient_t MQTTclient;

#ifndef SENSORBOX_VERSION
// SmartEVSE server MQTT client implementation
MQTTclientSmartEVSE_t MQTTclientSmartEVSE;
bool MQTTclientSmartEVSE_AppConnected = false;  // Track if app is connected
String MQTTSmartEVSEprefix;                     // Initialized once in connect(), used by all SmartEVSE MQTT functions

#if MQTT_ESP == 1
void mqtt_smartevse_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, esp_mqtt_event_t *event) {
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        // Ignore connection if user disabled the server while connecting
        // Note: Cannot call disconnect() here - we're in MQTT task context and esp_mqtt_client_stop() would deadlock
        if (!MQTTSmartServer) {
            _LOG_I("SmartEVSE MQTT: Connection completed but server is disabled, ignoring.\n");
            break;  // Don't set connected=true, cleanup will happen on next connect() call
        }
        MQTTclientSmartEVSE.connected = true;
        _LOG_A("SmartEVSE MQTT server connected.\n");
        SetupMQTTClientSmartEVSE();
        break;
    case MQTT_EVENT_DISCONNECTED:
        MQTTclientSmartEVSE.connected = false;
        MQTTclientSmartEVSE_AppConnected = false;
        _LOG_I("SmartEVSE MQTT server disconnected.\n");
        break;
    case MQTT_EVENT_DATA:
        {
        String topic = String(event->topic).substring(0, event->topic_len);
        String payload = String(event->data).substring(0, event->data_len);
        _LOG_D("SmartEVSE MQTT received: topic=%s, payload=%s\n", topic.c_str(), payload.c_str());
        // Check if App status changed
        if (topic == MQTTSmartEVSEprefix + "/App/Status") {
            if (payload != "offline") {
                MQTTclientSmartEVSE_AppConnected = true;
                _LOG_I("SmartEVSE App connected, publishing data.\n");
                mqttSmartEVSEPublishData();
            } else {
                MQTTclientSmartEVSE_AppConnected = false;
                _LOG_I("SmartEVSE App disconnected.\n");
            }
        } else if (topic.indexOf("/Set/") >= 0) {
            // Handle Set commands and publish updated data immediately
            mqtt_receive_callback(topic, payload);
            mqttSmartEVSEPublishData();
        } else {
            // Other messages (e.g. subscribed topics) - just process, don't publish
            mqtt_receive_callback(topic, payload);
        }
        }
        break;
    case MQTT_EVENT_ERROR:
        _LOG_I("SmartEVSE MQTT_EVENT_ERROR; Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        break;
    default:
        break;
    }
}

// Centralized cleanup - prevents race conditions by atomically clearing state before stopping client.
// This only stops the client; it deliberately does NOT destroy it. esp_mqtt_client_stop() blocks until
// the client task has fully stopped, so the handle is left in a safe, inert state that connect() can
// cheaply restart later. 
void MQTTclientSmartEVSE_t::cleanup(bool publishOffline) {
    connected = false;
    MQTTclientSmartEVSE_AppConnected = false;
    if (!client) return;
    
    if (publishOffline && MQTTSmartEVSEprefix.length()) {
        esp_mqtt_client_publish(client, (MQTTSmartEVSEprefix + "/connected").c_str(), "offline", 7, 0, 1);
    }
    
    // Note: This must NOT be called from MQTT task context (event handler)
    esp_mqtt_client_stop(client);
}

void MQTTclientSmartEVSE_t::connect(void) {
    if (!MQTTSmartServer || MQTTprivatePassword.length() == 0) {
        if (MQTTSmartServer) _LOG_A("SmartEVSE MQTT: No private key hash available.\n");
        return;
    }

    if (client) {
        // Already initialized from an earlier connect() 
        connected = false;
        _LOG_A("SmartEVSE MQTT restarting existing client as %s\n", MQTTSmartEVSEprefix.c_str());
        esp_err_t err = esp_mqtt_client_start(client);
        if (err != ESP_OK) {
            _LOG_A("SmartEVSE MQTT: esp_mqtt_client_start failed: %d (heap: %u)\n", err, ESP.getFreeHeap());
        }
        return;
    }

    // Initialize shared prefix (used by all SmartEVSE MQTT functions)
    MQTTSmartEVSEprefix = "SmartEVSE-" + String(serialnr);
    
    // Static strings kept alive for esp_mqtt_client
    static String lwtTopic;
    static char s_mqtt_url[] = "mqtts://mqtt.smartevse.nl:8883";
    lwtTopic = MQTTSmartEVSEprefix + "/connected";
    
    esp_mqtt_client_config_t cfg = { 
        .uri = s_mqtt_url, .client_id = MQTTSmartEVSEprefix.c_str(), 
        .username = MQTTSmartEVSEprefix.c_str(), .password = MQTTprivatePassword.c_str(),
        .lwt_topic = lwtTopic.c_str(), .lwt_msg = "offline", .lwt_qos = 0, .lwt_retain = 1, .lwt_msg_len = 7,
        .keepalive = 15, .buffer_size = 512, .out_buffer_size = 512
    };
    cfg.cert_pem = root_ca_letsencrypt;
    
    _LOG_A("SmartEVSE MQTT connecting as %s (heap: %u)\n", MQTTSmartEVSEprefix.c_str(), ESP.getFreeHeap());
    client = esp_mqtt_client_init(&cfg);
    if (!client) {
        _LOG_A("SmartEVSE MQTT: esp_mqtt_client_init failed (heap: %u)\n", ESP.getFreeHeap());
        return;
    }
    esp_err_t err = esp_mqtt_client_register_event(client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, (esp_event_handler_t)mqtt_smartevse_event_handler, NULL);
    if (err != ESP_OK) {
        _LOG_A("SmartEVSE MQTT: esp_mqtt_client_register_event failed: %d\n", err);
    }
    err = esp_mqtt_client_start(client);
    if (err != ESP_OK) {
        _LOG_A("SmartEVSE MQTT: esp_mqtt_client_start failed: %d (heap: %u)\n", err, ESP.getFreeHeap());
    }
}

void MQTTclientSmartEVSE_t::disconnect(void) {
    cleanup(true);  // Publish offline before disconnecting
}

void MQTTclientSmartEVSE_t::publish(const String &topic, const String &payload, bool retained, int qos) {
    if (connected && client)
        esp_mqtt_client_publish(client, topic.c_str(), payload.c_str(), payload.length(), qos, retained);
}

void MQTTclientSmartEVSE_t::subscribe(const String &topic, int qos) {
    if (connected && client)
        esp_mqtt_client_subscribe(client, topic.c_str(), qos);
}
#endif  // SENSORBOX_VERSION

#endif

#endif

//github.com L1
    const char* root_ca_github = R"ROOT_CA(
-----BEGIN CERTIFICATE-----
MIID0zCCArugAwIBAgIQVmcdBOpPmUxvEIFHWdJ1lDANBgkqhkiG9w0BAQwFADB7
MQswCQYDVQQGEwJHQjEbMBkGA1UECAwSR3JlYXRlciBNYW5jaGVzdGVyMRAwDgYD
VQQHDAdTYWxmb3JkMRowGAYDVQQKDBFDb21vZG8gQ0EgTGltaXRlZDEhMB8GA1UE
AwwYQUFBIENlcnRpZmljYXRlIFNlcnZpY2VzMB4XDTE5MDMxMjAwMDAwMFoXDTI4
MTIzMTIzNTk1OVowgYgxCzAJBgNVBAYTAlVTMRMwEQYDVQQIEwpOZXcgSmVyc2V5
MRQwEgYDVQQHEwtKZXJzZXkgQ2l0eTEeMBwGA1UEChMVVGhlIFVTRVJUUlVTVCBO
ZXR3b3JrMS4wLAYDVQQDEyVVU0VSVHJ1c3QgRUNDIENlcnRpZmljYXRpb24gQXV0
aG9yaXR5MHYwEAYHKoZIzj0CAQYFK4EEACIDYgAEGqxUWqn5aCPnetUkb1PGWthL
q8bVttHmc3Gu3ZzWDGH926CJA7gFFOxXzu5dP+Ihs8731Ip54KODfi2X0GHE8Znc
JZFjq38wo7Rw4sehM5zzvy5cU7Ffs30yf4o043l5o4HyMIHvMB8GA1UdIwQYMBaA
FKARCiM+lvEH7OKvKe+CpX/QMKS0MB0GA1UdDgQWBBQ64QmG1M8ZwpZ2dEl23OA1
xmNjmjAOBgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zARBgNVHSAECjAI
MAYGBFUdIAAwQwYDVR0fBDwwOjA4oDagNIYyaHR0cDovL2NybC5jb21vZG9jYS5j
b20vQUFBQ2VydGlmaWNhdGVTZXJ2aWNlcy5jcmwwNAYIKwYBBQUHAQEEKDAmMCQG
CCsGAQUFBzABhhhodHRwOi8vb2NzcC5jb21vZG9jYS5jb20wDQYJKoZIhvcNAQEM
BQADggEBABns652JLCALBIAdGN5CmXKZFjK9Dpx1WywV4ilAbe7/ctvbq5AfjJXy
ij0IckKJUAfiORVsAYfZFhr1wHUrxeZWEQff2Ji8fJ8ZOd+LygBkc7xGEJuTI42+
FsMuCIKchjN0djsoTI0DQoWz4rIjQtUfenVqGtF8qmchxDM6OW1TyaLtYiKou+JV
bJlsQ2uRl9EMC5MCHdK8aXdJ5htN978UeAOwproLtOGFfy/cQjutdAFI3tZs4RmY
CV4Ks2dH/hzg1cEo70qLRDEmBDeNiXQ2Lu+lIg+DdEmSx/cQwgwp+7e9un/jX9Wf
8qn0dNW44bOwgeThpWOjzOoEeJBuv/c=
-----END CERTIFICATE-----
)ROOT_CA";

// Let's Encrypt ISRG Root X1
// valid till 2035
const char* root_ca_letsencrypt = R"ROOT_CA(
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
)ROOT_CA";


// get version nr. of latest release of off github
// input:
// owner_repo format: dingo35/SmartEVSE-3.5
// asset name format: one of firmware.bin, firmware.debug.bin, firmware.signed.bin, firmware.debug.signed.bin
// output:
// version -- null terminated string with latest version of this repo
// downloadUrl -- global pointer to null terminated string with the url where this version can be downloaded
bool getLatestVersion(String owner_repo, String asset_name, char *version) {
    HTTPClient httpClient;
    String useURL = "https://api.github.com/repos/" + owner_repo + "/releases/latest";
    httpClient.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    const char* url = useURL.c_str();
    _LOG_A("Connecting to: %s.\n", url );
    if( String(url).startsWith("https") ) {
        httpClient.begin(url, root_ca_github);
    } else {
        httpClient.begin(url);
    }
    httpClient.addHeader("User-Agent", "SmartEVSE-v3");
    httpClient.addHeader("Accept", "application/vnd.github+json");
    httpClient.addHeader("X-GitHub-Api-Version", "2022-11-28" );
    const char* get_headers[] = { "Content-Length", "Content-type", "Accept-Ranges" };
    httpClient.collectHeaders( get_headers, sizeof(get_headers)/sizeof(const char*) );
    int httpCode = httpClient.GET();  //Make the request

    // only handle 200/301, fail on everything else
    if( httpCode != HTTP_CODE_OK && httpCode != HTTP_CODE_MOVED_PERMANENTLY ) {
        // This error may be a false positive or a consequence of the network being disconnected.
        // Since the network is controlled from outside this class, only significant error messages are reported.
        _LOG_A("Error on HTTP request (httpCode=%i)\n", httpCode);
        httpClient.end();
        return false;
    }
    // The filter: it contains "true" for each value we want to keep
    DynamicJsonDocument  filter(100);
    filter["tag_name"] = true;
    filter["assets"][0]["browser_download_url"] = true;
    filter["assets"][0]["name"] = true;

    // Deserialize the document
    DynamicJsonDocument doc2(1500);
    DeserializationError error = deserializeJson(doc2, httpClient.getStream(), DeserializationOption::Filter(filter));

    if (error) {
        _LOG_A("deserializeJson() failed: %s\n", error.c_str());
        httpClient.end();  // We're done with HTTP - free the resources
        return false;
    }
    const char* tag_name = doc2["tag_name"]; // "v3.6.1"
    if (!tag_name) {
        //no version found
        _LOG_A("ERROR: LatestVersion of repo %s not found.\n", owner_repo.c_str());
        httpClient.end();  // We're done with HTTP - free the resources
        return false;
    }
    else
        //duplicate value so it won't get lost out of scope
        strlcpy(version, tag_name, 32);
        //strlcpy(version, tag_name, sizeof(version));
    _LOG_V("Found latest version:%s.\n", version);

    httpClient.end();  // We're done with HTTP - free the resources
    return true;
/*    for (JsonObject asset : doc2["assets"].as<JsonArray>()) {
        String name = asset["name"] | "";
        if (name == asset_name) {
            const char* asset_browser_download_url = asset["browser_download_url"];
            if (!asset_browser_download_url) {
                // no download url found
                _LOG_A("ERROR: Downloadurl of asset %s in repo %s not found.\n", asset_name.c_str(), owner_repo.c_str());
                httpClient.end();  // We're done with HTTP - free the resources
                return false;
            } else {
                asprintf(&downloadUrl, "%s", asset_browser_download_url);        //will be freed in FirmwareUpdate()
                _LOG_V("Found asset: name=%s, url=%s.\n", name.c_str(), downloadUrl);
                httpClient.end();  // We're done with HTTP - free the resources
                return true;
            }
        }
    }
    _LOG_A("ERROR: could not find asset %s in repo %s at version %s.\n", asset_name.c_str(), owner_repo.c_str(), version);
    httpClient.end();  // We're done with HTTP - free the resources
    return false;*/
}


unsigned char *signature = NULL;
#define SIGNATURE_LENGTH 512

// SHA-Verify the OTA partition after it's been written
// https://techtutorialsx.com/2018/05/10/esp32-arduino-mbed-tls-using-the-sha-256-algorithm/
// https://github.com/ARMmbed/mbedtls/blob/development/programs/pkey/rsa_verify.c
bool validate_sig( const esp_partition_t* partition, unsigned char *signature, int size )
{
    const char* rsa_key_pub = R"RSA_KEY_PUB(
-----BEGIN PUBLIC KEY-----
MIICIjANBgkqhkiG9w0BAQEFAAOCAg8AMIICCgKCAgEAtjEWhkfKPAUrtX1GueYq
JmDp4qSHBG6ndwikAHvteKgWQABDpwaemZdxh7xVCuEdjEkaecinNOZ0LpSCF3QO
qflnXkvpYVxjdTpKBxo7vP5QEa3I6keJfwpoMzGuT8XOK7id6FHJhtYEXcaufALi
mR/NXT11ikHLtluATymPdoSscMiwry0qX03yIek91lDypBNl5uvD2jxn9smlijfq
9j0lwtpLBWJPU8vsU0uzuj7Qq5pWZFKsjiNWfbvNJXuLsupOazf5sh0yeQzL1CBL
RUsBlYVoChTmSOyvi6kO5vW/6GLOafJF0FTdOQ+Gf3/IB6M1ErSxlqxQhHq0pb7Y
INl7+aFCmlRjyLlMjb8xdtuedlZKv8mLd37AyPAihrq9gV74xq6c7w2y+h9213p8
jgcmo/HvOlGaXEIOVCUu102teOckXjTni2yhEtFISCaWuaIdb5P9e0uBIy1e+Bi6
/7A3aut5MQP07DO99BFETXyFF6EixhTF8fpwVZ5vXeIDvKKEDUGuzAziUEGIZpic
UQ2fmTzIaTBbNlCMeTQFIpZCosM947aGKNBp672wdf996SRwg9E2VWzW2Z1UuwWV
BPVQkHb1Hsy7C9fg5JcLKB9zEfyUH0Tm9Iur1vsuA5++JNl2+T55192wqyF0R9sb
YtSTUJNSiSwqWt1m0FLOJD0CAwEAAQ==
-----END PUBLIC KEY-----
)RSA_KEY_PUB";

    if( !partition ) {
        _LOG_A( "Could not find update partition!.\n");
        return false;
    }
    _LOG_D("Creating mbedtls context.\n");
    mbedtls_pk_context pk;
    mbedtls_md_context_t rsa;
    mbedtls_pk_init( &pk );
    _LOG_D("Parsing public key.\n");

    int ret;
    if( ( ret = mbedtls_pk_parse_public_key( &pk, (const unsigned char*)rsa_key_pub, strlen(rsa_key_pub)+1 ) ) != 0 ) {
        _LOG_A( "Parsing public key failed! mbedtls_pk_parse_public_key %d (%d bytes)\n%s", ret, strlen(rsa_key_pub)+1, rsa_key_pub);
        return false;
    }
    if( !mbedtls_pk_can_do( &pk, MBEDTLS_PK_RSA ) ) {
        _LOG_A( "Public key is not an rsa key -0x%x", -ret );
        return false;
    }
    _LOG_D("Initing mbedtls.\n");
    const mbedtls_md_info_t *mdinfo = mbedtls_md_info_from_type( MBEDTLS_MD_SHA256 );
    mbedtls_md_init( &rsa );
    mbedtls_md_setup( &rsa, mdinfo, 0 );
    mbedtls_md_starts( &rsa );
    int bytestoread = SPI_FLASH_SEC_SIZE;
    int bytesread = 0;
    uint8_t *_buffer = (uint8_t*)malloc(SPI_FLASH_SEC_SIZE);
    if(!_buffer){
        _LOG_A( "malloc failed.\n");
        return false;
    }
    _LOG_D("Parsing content.\n");
    _LOG_V( "Reading partition (%i sectors, sec_size: %i)", size, bytestoread );
    while( bytestoread > 0 ) {
        _LOG_V( "Left: %i (%i)               \r", size, bytestoread );

        if( ESP.partitionRead( partition, bytesread, (uint32_t*)_buffer, bytestoread ) ) {
            mbedtls_md_update( &rsa, (uint8_t*)_buffer, bytestoread );
            bytesread = bytesread + bytestoread;
            size = size - bytestoread;
            if( size <= SPI_FLASH_SEC_SIZE ) {
                bytestoread = size;
            }
        } else {
            _LOG_A( "partitionRead failed!.\n");
            return false;
        }
    }
    free( _buffer );

    unsigned char *hash = (unsigned char*)malloc( mdinfo->size );
    if(!hash){
        _LOG_A( "malloc failed.\n");
        return false;
    }
    mbedtls_md_finish( &rsa, hash );
    ret = mbedtls_pk_verify( &pk, MBEDTLS_MD_SHA256, hash, mdinfo->size, (unsigned char*)signature, SIGNATURE_LENGTH );
    free( hash );
    mbedtls_md_free( &rsa );
    mbedtls_pk_free( &pk );
    if( ret == 0 ) {
        return true;
    }

    // validation failed, overwrite the first few bytes so this partition won't boot!
    log_w( "Validation failed, erasing the invalid partition.\n");
    ESP.partitionEraseRange( partition, 0, ENCRYPTED_BLOCK_SIZE);
    return false;
}


bool forceUpdate(const char* firmwareURL, bool validate) {
    HTTPClient httpClient;
    //WiFiClientSecure _client;
    int partition = U_FLASH;

    httpClient.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    _LOG_A("Connecting to: %s.\n", firmwareURL );
    if( String(firmwareURL).startsWith("https") ) {
        //_client.setCACert(root_ca_github); // OR
        //_client.setInsecure(); //not working for github
        httpClient.begin(firmwareURL, root_ca_github);
    } else {
        httpClient.begin(firmwareURL);
    }
    httpClient.addHeader("User-Agent", "SmartEVSE-v3");
    httpClient.addHeader("Accept", "application/vnd.github+json");
    httpClient.addHeader("X-GitHub-Api-Version", "2022-11-28" );
    const char* get_headers[] = { "Content-Length", "Content-type", "Accept-Ranges" };
    httpClient.collectHeaders( get_headers, sizeof(get_headers)/sizeof(const char*) );

    int updateSize = 0;
    int httpCode = httpClient.GET();
    String contentType;

    if( httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY ) {
        updateSize = httpClient.getSize();
        contentType = httpClient.header( "Content-type" );
        String acceptRange = httpClient.header( "Accept-Ranges" );
        if( acceptRange == "bytes" ) {
            _LOG_V("This server supports resume!\n");
        } else {
            _LOG_V("This server does not support resume!\n");
        }
    } else {
        _LOG_A("ERROR: Server responded with HTTP Status %i.\n", httpCode );
        return false;
    }

    _LOG_D("updateSize : %i, contentType: %s.\n", updateSize, contentType.c_str());
    Stream * stream = httpClient.getStreamPtr();
    if( updateSize<=0 || stream == nullptr ) {
        _LOG_A("HTTP Error.\n");
        return false;
    }

    // some network streams (e.g. Ethernet) can be laggy and need to 'breathe'
    if( ! stream->available() ) {
        uint32_t timeout = millis() + 10000;
        while( ! stream->available() ) {
            if( millis()>timeout ) {
                _LOG_A("Stream timed out.\n");
                return false;
            }
            vTaskDelay(1);
        }
    }

    if( validate ) {
        if( updateSize == UPDATE_SIZE_UNKNOWN || updateSize <= SIGNATURE_LENGTH ) {
            _LOG_A("Malformed signature+fw combo.\n");
            return false;
        }
        updateSize -= SIGNATURE_LENGTH;
    }

    if( !Update.begin(updateSize, partition) ) {
        _LOG_A("ERROR Not enough space to begin OTA, partition size mismatch? Update failed!\n");
        Update.abort();
        return false;
    }

    Update.onProgress( [](uint32_t progress, uint32_t size) {
      _LOG_V("Firmware update progress %i/%i.\n", progress, size);
      //move this data to global var
      downloadProgress = progress;
      downloadSize = size;
      //give background tasks some air
      //vTaskDelay(100 / portTICK_PERIOD_MS);
    });

    // read signature
    if( validate ) {
        signature = (unsigned char *) malloc(SIGNATURE_LENGTH);                       //tried to free in in all exit scenarios, RISK of leakage!!!
        stream->readBytes( signature, SIGNATURE_LENGTH );
    }

    _LOG_I("Begin %s OTA. This may take 2 - 5 mins to complete. Things might be quiet for a while.. Patience!\n", partition==U_FLASH?"Firmware":"Filesystem");

    // Some activity may appear in the Serial monitor during the update (depends on Update.onProgress)
    int written = Update.writeStream(*stream);                                 // although writeStream returns size_t, we don't expect >2Gb

    if ( written == updateSize ) {
        _LOG_D("Written : %d successfully", written);
        updateSize = written; // flatten value to prevent overflow when checking signature
    } else {
        _LOG_A("Written only : %u/%u Premature end of stream?", written, updateSize);
        Update.abort();
        FREE(signature);
        return false;
    }

    if (!Update.end()) {
        _LOG_A("An Update Error Occurred. Error #: %d", Update.getError());
        FREE(signature);
        return false;
    }

    if( validate ) { // check signature
        _LOG_I("Checking partition %d to validate", partition);

        //getPartition( partition ); // updated partition => '_target_partition' pointer
        const esp_partition_t* _target_partition = esp_ota_get_next_update_partition(NULL);

        #define CHECK_SIG_ERROR_PARTITION_NOT_FOUND -1
        #define CHECK_SIG_ERROR_VALIDATION_FAILED   -2

        if( !_target_partition ) {
            _LOG_A("Can't access partition #%d to check signature!", partition);
            FREE(signature);
            return false;
        }

        _LOG_D("Checking signature for partition %d...", partition);

        const esp_partition_t* running_partition = esp_ota_get_running_partition();

        if( partition == U_FLASH ) {
            // /!\ An OTA partition is automatically set as bootable after being successfully
            // flashed by the Update library.
            // Since we want to validate before enabling the partition, we need to cancel that
            // by temporarily reassigning the bootable flag to the running-partition instead
            // of the next-partition.
            esp_ota_set_boot_partition( running_partition );
            // By doing so the ESP will NOT boot any unvalidated partition should a reset occur
            // during signature validation (crash, oom, power failure).
        }

        if( !validate_sig( _target_partition, signature, updateSize ) ) {
            FREE(signature);
            // erase partition
            esp_partition_erase_range( _target_partition, _target_partition->address, _target_partition->size );
            _LOG_A("Signature check failed!.\n");
            return false;
        } else {
            FREE(signature);
            _LOG_D("Signature check successful!.\n");
            if( partition == U_FLASH ) {
                // Set updated partition as bootable now that it's been verified
                esp_ota_set_boot_partition( _target_partition );
            }
        }
    }
    _LOG_D("OTA Update complete!.\n");
    if (Update.isFinished()) {
        _LOG_V("Update succesfully completed at %s partition\n", partition==U_SPIFFS ? "spiffs" : "firmware" );
        return true;
    } else {
        _LOG_A("ERROR: Update not finished! Something went wrong!.\n");
    }
    return false;
}


// put firmware update in separate task so we can feed progress to the html page
void FirmwareUpdate(void *parameter) {
    //_LOG_A("DINGO: url=%s.\n", downloadUrl);
    if (forceUpdate(downloadUrl, 1)) {
#ifndef SENSORBOX_VERSION
        _LOG_A("Firmware update succesfull; rebooting as soon as no EV is charging.\n");
#else
        _LOG_A("Firmware update succesfull; rebooting.\n");
#endif
        downloadProgress = -1;
        shouldReboot = true;
    } else {
        _LOG_A("ERROR: Firmware update failed.\n");
        //_http.end();
        downloadProgress = -2;
    }
    if (downloadUrl) free(downloadUrl);
    vTaskDelete(NULL);                                                        //end this task so it will not take up resources
}

void RunFirmwareUpdate(void) {
    _LOG_V("Starting firmware update from downloadUrl=%s.\n", downloadUrl);
    downloadProgress = 0;                                                       // clear errors, if any
    xTaskCreate(
        FirmwareUpdate, // Function that should be called
        "FirmwareUpdate",// Name of the task (for debugging)
        4096,           // Stack size (bytes)
        NULL,           // Parameter to pass
        3,              // Task priority - medium
        NULL            // Task handle
    );
}


void setTimeZone(void * parameter) {
    HTTPClient httpClient;
    //we use lambda function because normal function collides with HTTPClient class
    auto onErrorCloseTask = [&httpClient]() {
        _LOG_A("Could not detect timezone, set it to CEST and retry next reboot.\n");
        setenv("TZ","CET-1CEST,M3.5.0,M10.5.0/3",1);                            // CEST tzinfo string
        tzset();
        httpClient.end();
        vTaskDelete(NULL);                                                      //end this task so it will not take up resources
    };

    // Check if browser timezone was saved during WiFi setup
    if (TZname == "") {
        _LOG_A("No browser timezone available.\n");
        onErrorCloseTask();
    }
    _LOG_A("Using browser timezone: %s\n", TZname.c_str());

    // takes TZname (format: Europe/Berlin) , gets TZ_INFO (posix string, format: CET-1CEST,M3.5.0,M10.5.0/3) and sets and stores timezonestring accordingly
    //httpClient.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    WiFiClient * stream = httpClient.getStreamPtr();
    String l;
    char *URL;
    asprintf(&URL, "%s/zones.csv", FW_DOWNLOAD_PATH); //will be freed
    httpClient.begin(URL);
    int httpCode = httpClient.GET();  //Make the request

    // only handle 200/301, fail on everything else
    if( httpCode != HTTP_CODE_OK && httpCode != HTTP_CODE_MOVED_PERMANENTLY ) {
        _LOG_A("Error on zones.csv HTTP request (httpCode=%i)\n", httpCode);
        FREE(URL);
        onErrorCloseTask();
    }

    stream = httpClient.getStreamPtr();
    while(httpClient.connected() && stream->available()) {
        l = stream->readStringUntil('\n');
        if (l.indexOf(TZname) > 0) {
            int from = l.indexOf("\",\"") + 3;
            TZinfo = l.substring(from, l.length() - 1);
            _LOG_A("Detected Timezone info: TZname = %s, tz_info=%s.\n", TZname.c_str(), TZinfo.c_str());
            setenv("TZ",TZinfo.c_str(),1);
            tzset();
            if (preferences.begin("settings", false) ) {
                preferences.putString("TimezoneInfo", TZinfo);
                preferences.end();
            }
            break;
        }
    }
    if (TZinfo == "") {
        _LOG_A("Could not find TZname %s in zones.csv.\n", TZname.c_str());
        FREE(URL);
        onErrorCloseTask();
    }
    httpClient.end();
    FREE(URL);
    vTaskDelete(NULL);                                                          //end this task so it will not take up resources
}

#ifndef SENSORBOX_VERSION
std::array<mDNSServiceEntry, 8> mDNSServices = {};
HTTPClient* homeWizardHttpClient=nullptr;
bool homeWizardHttpClientInitialized = false;
static bool mdnsDiscoveryInProgress = false;            // True when async mDNS task is running
static bool mdnsDiscoveryHasRun = false;                // True after mDNS discovery has been executed for the current network state
static unsigned long lastMdnsQueryTime = 0;             // Last time mDNS query was attempted
static const unsigned long MDNS_RETRY_INTERVAL = 30000; // Retry mDNS discovery every 30 seconds if not found

struct MdnsServiceQuery {
    const char *service;
    const char *protocol;
};

static constexpr MdnsServiceQuery mdnsServiceQueries[] = {
    {"hwenergy", "tcp"}, // HomeWizard Energy meters use this mDNS service
    // Add more entries here when we support other meter brands or service types.
    // Each brand can advertise a different mDNS service/protocol pair.
    // For each entry, a separate mDNS discovery will be performed and cached in mDNSServices.
};

/**
 * @brief Add one discovered HomeWizard service to the cached mDNS table.
 */
static bool appendDiscoveredService(const String &hostname, uint16_t port, const String &ip, uint8_t &serviceCount) {
    if (serviceCount >= mDNSServices.size()) {
        return false;
    }

    const String fullHostname = hostname + ".local" + (port != 80 ? ":" + String(port) : "");
    mDNSServices[serviceCount].ServiceType = getmDNSServiceType(hostname);
    mDNSServices[serviceCount].HostName = fullHostname;
    serviceCount++;
    return true;
}

/**
 * @brief Clear the cached mDNS discovery table.
 */
void clearmDNSServices() {
    for (auto &service : mDNSServices) {
        service.ServiceType = 0;
        service.HostName = "";
    }
    mdnsDiscoveryHasRun = false;
    lastMdnsQueryTime = 0;
}

bool isMDNSDiscoveryInProgress(void) {
    return mdnsDiscoveryInProgress;
}

/**
 * @brief Count cached services matching a specific HomeWizard service type.
 */
uint8_t getmDNSServiceCount(int type) {
    uint8_t count = 0;
    for (const auto &service : mDNSServices) {
        if (!service.HostName.isEmpty() && service.ServiceType == type) {
            count++;
        }
    }
    return count;
}
/**
 * @brief Count all cached mDNS services.
 */
uint8_t getmDNSServiceCount() {
    uint8_t count = 0;
    for (const auto &service : mDNSServices) {
        if (!service.HostName.isEmpty()) {
            count++;
        }
    }
    return count;
}

/**
 * @brief Return a cached service by type, optional hostname pattern, and zero-based index.
 */
const mDNSServiceEntry *getmDNSServiceByIndex(int type, const String &hostnamePattern, uint8_t index, bool strict) {
    uint8_t currentIndex = 0;
    for (const auto &service : mDNSServices) {
        const bool patternMatches = hostnamePattern.isEmpty() || service.HostName.indexOf(hostnamePattern) >= 0;
        if (service.ServiceType != 0 &&
            (type == 0 || service.ServiceType == type) &&
            (!strict || patternMatches)) {
            if (currentIndex == index) {
                return &service;
            }
            currentIndex++;
        }
    }
    return nullptr;
}

/**
 * @brief Build the compact name shown on the LCD for a discovered network meter.
 *
 * The helper trims the raw hostname into the shortest useful label that fits the LCD
 * So the UI can show something readable instead of the full network name.
 */
void compileServiceName(int type, const char *hostname, char *output, size_t outputSize) {
    if (output == nullptr || outputSize == 0) {
        return;
    }

    output[0] = '\0';
    if (hostname == nullptr || hostname[0] == '\0') {
        return;
    }

    // Keep the formatting per meter type here so each service can define how its
    // hostname should be shortened for the LCD without affecting the others.
    switch (type) {
        case EM_HOMEWIZARD: {
            const char *end = strrchr(hostname, '.');
            if (end == nullptr || end <= hostname) {
                end = hostname + strlen(hostname);
            }
            const char *start = (size_t)(end - hostname) <= 6 ? hostname : end - 6;
            const size_t length = (size_t)(end - start);
            if (length >= outputSize) {
                memcpy(output, start, outputSize - 1);
                output[outputSize - 1] = '\0';
            } else {
                memcpy(output, start, length);
                output[length] = '\0';
            }
            return;
        }
        default:
            strlcpy(output, "Unknown", outputSize);
            return;
    }
}

/**
 * @brief Map a discovered hostname to the corresponding meter service type.
 */
int getmDNSServiceType(const String &hostname) {
    struct ServiceTypeMap {
        const char *prefix;
        const int type;
    };
    static const ServiceTypeMap serviceTypes[] = {
        {"p1meter-", EM_HOMEWIZARD},
        {"kwhmeter-", EM_HOMEWIZARD},
         // Add more mappings for other brands/types here if needed
    };

    for (const auto &entry : serviceTypes) {
        if (hostname.startsWith(entry.prefix)) {
            return entry.type;
        }
    }

    return 0;
}

/**
 * @brief Count cached services compatible with the selected meter type.
 */
uint8_t getCompatiblemDNSServiceCount(uint8_t meterType) {
    if (meterType == 0) {
        return 0;
    }

    uint8_t count = 0;
    for (const auto &service : mDNSServices) {
        if (!service.HostName.isEmpty() && service.ServiceType == meterType) {
            count++;
        }
    }
    return count;
}

/**
 * @brief Return the zero-based compatible service for a meter type.
 */
const mDNSServiceEntry *getCompatiblemDNSServiceByIndex(uint8_t meterType, uint8_t index) {
    if (meterType == 0) {
        return nullptr;
    }

    uint8_t currentIndex = 0;
    for (const auto &service : mDNSServices) {
        if (!service.HostName.isEmpty() && service.ServiceType == meterType) {
            if (currentIndex == index) {
                return &service;
            }
            currentIndex++;
        }
    }
    return nullptr;
}


/**
 * @brief FreeRTOS task that performs mDNS discovery in the background.
 * 
 * This task runs the blocking mDNS query without blocking the main loop.
 */
void mdnsDiscoveryTask(void* parameter) {
    _LOG_A("mDNS discovery task started\n");

    bool serviceListReset = false;
    uint8_t serviceCount = 0;
    bool anyServicesFound = false;

    struct DiscoveredService {
        String hostname;
        uint16_t port;
        String ip;
    };

    for (const auto &query : mdnsServiceQueries) {
        // Search for services defined in the compile-time query list.
        // https://api-documentation.homewizard.com/docs/discovery/
        const int n = MDNS.queryService(query.service, query.protocol);
        if (n < 0) {
            _LOG_A("MDNS query failed for %s.%s.\n", query.service, query.protocol);
            continue;
        }
        if (n == 0) {
            _LOG_A("No MDNS services found for %s.%s.\n", query.service, query.protocol);
            continue;
        }

        std::vector<DiscoveredService> services;
        services.reserve(n);
        for (int i = 0; i < n; i++) {
            services.push_back({MDNS.hostname(i), MDNS.port(i), MDNS.IP(i).toString()});
        }

        std::sort(services.begin(), services.end(), [](const DiscoveredService &left, const DiscoveredService &right) {
            return left.hostname < right.hostname;
        });

        if (!serviceListReset) {
            clearmDNSServices();
            serviceCount = 0;
            serviceListReset = true;
        }

        for (const auto &service : services) {
            _LOG_A("Discovered mDNS service: %s.local (%s:%d)\n", service.hostname.c_str(), service.ip.c_str(), service.port);
            anyServicesFound = true;
            appendDiscoveredService(service.hostname, service.port, service.ip, serviceCount);
        }
    }

    if (!anyServicesFound) {
        _LOG_A("No matching mDNS services found.\n");
    }

    mdnsDiscoveryHasRun = true;
    mdnsDiscoveryInProgress = false;
    _LOG_A("mDNS discovery task completed\n");
    vTaskDelete(NULL);
}

/**
 * @brief Starts async mDNS discovery for networked meters.
 *
 * This function uses mDNS to search for services advertising "_hwenergy._tcp" on the local network.
 * This function spawns a background task to perform the blocking mDNS query,
 * so the main loop remains responsive. 
 *
 */
void discoverNetworkMeters() {
    // If discovery is already in progress, don't start another
    if (mdnsDiscoveryInProgress) {
        return;
    }

    // Rate limit discovery attempts
    unsigned long now = millis();
    if (lastMdnsQueryTime != 0 && (now - lastMdnsQueryTime) < MDNS_RETRY_INTERVAL) {
        // Still in cooldown period, skip mDNS query
        return;
    }
    lastMdnsQueryTime = now;
    
    // Start async mDNS discovery task
    mdnsDiscoveryInProgress = true;
    _LOG_A("Starting async mDNS discovery (next retry in %lu seconds)...\n", MDNS_RETRY_INTERVAL / 1000);
    
    // Create task with 4KB stack, priority 1 (low), running on any core
    BaseType_t result = xTaskCreate(
        mdnsDiscoveryTask,      // Task function
        "mDNS_Disc",            // Task name
        4096,                   // Stack size (bytes)
        NULL,                   // Parameters
        1,                      // Priority (low)
        NULL                    // Task handle (not needed)
    );
    
    if (result != pdPASS) {
        _LOG_A("Failed to create mDNS discovery task!\n");
        mdnsDiscoveryInProgress = false;
    }
    
    return;
}

/**
 * @brief Retrieves active current values from a HomeWizard V1 API.
 *
 * This function sends an HTTP GET request to the specified URL to fetch the active current data
 * in JSON format, parses the JSON response, and retrieves specific fields for current.
 *
 * @return A pair containing:
 *     - A int flag indicating: 0: failure, 1: single phase current, 3: 3 phase current
 *     - An array of 6 values representing the active current in deci-amps for L1, L2, L3, total, import, and export
 */
std::pair<int8_t, std::array<std::int32_t, 6> > getDataFromHomeWizard(const char *hostname) {
    _LOG_A("Invocation\n");
    if (hostname == nullptr || hostname[0] == '\0') {
        _LOG_A("No hostname provided.\n");
        return {false, {0, 0, 0, 0, 0, 0}};
    }
    char url[64];
    snprintf(url, sizeof(url), "http://%s/api/v1/data", hostname);

    _LOG_A("Connect to URL %s\n", url);


    if (!homeWizardHttpClientInitialized) {
        homeWizardHttpClient = new HTTPClient();
        homeWizardHttpClient->setTimeout(1500);
        homeWizardHttpClient->setReuse(true);  // Persistent TCP across polls.
        homeWizardHttpClient->addHeader("User-Agent", "SmartEVSE-v3");
        homeWizardHttpClient->addHeader("Accept", "application/json");
        homeWizardHttpClientInitialized = true;
    }

    homeWizardHttpClient->begin(url);

    // Handle HTTP errors or timeout.
    const int httpCode = homeWizardHttpClient->GET();
    if (httpCode != HTTP_CODE_OK) {
        _LOG_A("Error on HTTP request (httpCode=%i), url=%s.\n", httpCode, url);
        homeWizardHttpClient->end(); // Always cleanup
        delete homeWizardHttpClient;
        homeWizardHttpClient = nullptr;
        homeWizardHttpClientInitialized = false;
        return {false, {0, 0, 0, 0, 0, 0}};
    }

    // Get the response stream
    WiFiClient *stream = homeWizardHttpClient->getStreamPtr();

    const char* currentKeys[] = {"active_current_l1_a", "active_current_l2_a", "active_current_l3_a","active_current_a"};
    const char* powerKeys[] = { "active_power_l1_w", "active_power_l2_w", "active_power_l3_w","active_power_w"};
    const char* totalsKeys[] = {"total_power_import_kwh", "total_power_export_kwh"};

    // Filter is constant; build it once and reuse forever.
    static StaticJsonDocument<256> filter;
    static bool filterInit = false;
    if (!filterInit) {
        for (const auto* key : currentKeys) filter[key] = true;
        for (const auto* key : powerKeys)   filter[key] = true;
        for (const auto* key : totalsKeys)  filter[key] = true;
        filterInit = true;
    }

    // Stack-allocated JSON document for the parsed response.
    StaticJsonDocument<512> doc;
    const DeserializationError error = deserializeJson(doc, *stream, DeserializationOption::Filter(filter));
    homeWizardHttpClient->end();

    // Handle JSON parsing errors.
    if (error) {
        _LOG_A("JSON deserialization failed: %s\n", error.c_str());
        return {false, {0, 0, 0, 0, 0, 0}};
    }

    uint8_t phases = 0;
    // Verify all required keys exist.
    for (const auto* key : currentKeys) {
        if (doc.containsKey(key))
            phases++;
    }

    if (!phases) {
        // Early return on missing data.
        _LOG_A("Required JSON fields 'active_current_a' not found\n");
        return {phases, {0, 0, 0, 0, 0, 0}};
    }

    std::array<int32_t, 6> evdata{};
    _LOG_A("Reading %u-phase data\n", phases);

    // Determine grid direction based on power: negative indicates feed-in, positive indicates usage.
    auto getCorrection = [&doc](const char* powerKey) -> int8_t {
        return doc[powerKey].as<int>() < 0 ? -1 : 1;
    };

    if (phases == 1) {
        // Single phase case: use 'active_current_a' and 'active_power_w' for correction
        int16_t rawCurrent = doc[currentKeys[3]].as<float>() * 10;
        int8_t correction = getCorrection(powerKeys[3]);
        evdata[0] = std::abs(rawCurrent) * correction;
    }
    else{
        // Process all three phases.
        for (size_t i = 0; i < 3; ++i) {
            int16_t rawCurrent = doc[currentKeys[i]].as<float>() * 10;
            evdata[i] = std::abs(rawCurrent) * getCorrection(powerKeys[i]);
        }
    }
    evdata[3] = doc[totalsKeys[0]].as<float>() * 1000; // total import in Wh
    evdata[4] = doc[totalsKeys[1]].as<float>() * 1000; // total export in Wh
    evdata[5] = doc[powerKeys[3]].as<float>() * 1; // total power in Watts

return {phases, evdata};
}
#endif

void webServerRequest::setMessage(struct mg_http_message *hm) {
    hm_internal = hm;
}

bool webServerRequest::hasParam(const char *param) {
    return (mg_http_get_var(&hm_internal->query, param, temp, sizeof(temp)) >= 0);
}

webServerRequest* webServerRequest::getParam(const char *param) {
    _value = ""; // Clear previous value
    if (mg_http_get_var(&hm_internal->query, param, temp, sizeof(temp)) >= 0) {
        _value = temp;
    }
    return this; // Return pointer to self
}

const String& webServerRequest::value() {
    return _value; // Return the string value
}
//end of wrapper

struct mg_str empty = mg_str_n("", 0UL);

#if MQTT && MQTT_ESP == 0
char s_mqtt_url[80];
//TODO perhaps integrate multiple fn callback functions?
static void fn_mqtt(struct mg_connection *c, int ev, void *ev_data) {
    if (ev == MG_EV_OPEN) {
        _LOG_V("%lu CREATED\n", c->id);
        // c->is_hexdumping = 1;
    } else if (ev == MG_EV_ERROR) {
        // On error, log error message
        _LOG_A("%lu ERROR %s\n", c->id, (char *) ev_data);
    } else if (ev == MG_EV_CONNECT) {
        // If target URL is SSL/TLS, command client connection to use TLS
        if (mg_url_is_ssl(s_mqtt_url)) {
            struct mg_tls_opts opts = {.ca = empty, .cert = empty, .key = empty, .name = mg_url_host(s_mqtt_url), .skip_verification = 0};
            //struct mg_tls_opts opts = {.ca = empty};
            mg_tls_init(c, &opts);
        }
    } else if (ev == MG_EV_MQTT_OPEN) {
        // MQTT connect is successful
        _LOG_V("%lu CONNECTED to %s\n", c->id, s_mqtt_url);
        MQTTclient.connected = true;
        SetupMQTTClient();
    } else if (ev == MG_EV_MQTT_MSG) {
        // When we get echo response, print it
        struct mg_mqtt_message *mm = (struct mg_mqtt_message *) ev_data;
        _LOG_V("%lu RECEIVED %.*s <- %.*s\n", c->id, (int) mm->data.len, mm->data.buf, (int) mm->topic.len, mm->topic.buf);
        //somehow topic is not null terminated
        String topic2 = String(mm->topic.buf).substring(0,mm->topic.len);
        mqtt_receive_callback(topic2, mm->data.buf);
    } else if (ev == MG_EV_CLOSE) {
        _LOG_V("%lu CLOSED\n", c->id);
        MQTTclient.connected = false;
        MQTTclient.s_conn = NULL;  // Mark that we're closed
    }
}

// Timer function - recreate client connection if it is closed
static void timer_fn(void *arg) {
    struct mg_mgr *mgr = (struct mg_mgr *) arg;
    struct mg_mqtt_opts opts;
    memset(&opts, 0, sizeof(opts));
    opts.clean = false;
    // set will topic
    char willTopic[96];
    snprintf(willTopic, sizeof(willTopic), "%s/connected", MQTTprefix.c_str());
    opts.topic = mg_str(willTopic);
    opts.message = mg_str("offline");
    opts.retain = true;
    opts.keepalive = 15;                                                          // so we will timeout after 15s
    opts.version = 4;
    opts.client_id=mg_str(MQTTprefix.c_str());
    opts.user=mg_str(MQTTuser.c_str());
    opts.pass=mg_str(MQTTpassword.c_str());

    //prepare MQTT url
    //mqtt[s]://[username][:password]@host.domain[:port]
    snprintf(s_mqtt_url, sizeof(s_mqtt_url), "mqtt://%s:%i", MQTTHost.c_str(), MQTTPort);

    if (MQTTclient.s_conn == NULL) MQTTclient.s_conn = mg_mqtt_connect(mgr, s_mqtt_url, &opts, fn_mqtt, NULL);
}
#endif

// Timer function - sends LCD image to all connected websocket clients
static void lcd_image_timer_fn(void *arg) {
    struct mg_mgr *mgr = (struct mg_mgr *) arg;

    if (wsLcdConnections.empty()) {
        stopLCDImageTimer(mgr);
        return;
    }

    // First remove stale/closing sockets.
    for (size_t i = wsLcdConnections.size(); i > 0; --i) {
        const size_t idx = i - 1;
        mg_connection *c = wsLcdConnections[idx];
        if (c == nullptr || c->is_closing) {
            wsLcdConnections.erase(wsLcdConnections.begin() + idx);
        }
    }

    if (wsLcdConnections.empty()) {
        stopLCDImageTimer(mgr);
        return;
    }

    // Generate BMP image from LCD buffer (into a static buffer; no heap activity).
    size_t bmpSize = 0;
    const uint8_t *bmpImage = createImageFromGLCDBuffer(bmpSize);

    // Send to all connected websocket clients
    for (auto *c : wsLcdConnections) {
        mg_ws_send(c, bmpImage, bmpSize, WEBSOCKET_OP_BINARY);
    }
}

// Handle button command received via WebSocket
// Expected JSON format: {"button":"left|middle|right", "state":0|1}
static void handleButtonCommand(struct mg_connection *c, const char* data, size_t len) {
    if (!LCDPasswordOK) {
        _LOG_W("Rejected WebSocket button command: PIN not verified\n");
        sendWsError(c, "unauthorized");
        return;
    }

    DynamicJsonDocument doc(128);
    DeserializationError error = deserializeJson(doc, data, len);

    if (error) {
        _LOG_W("Failed to parse button command JSON: %s\n", error.c_str());
        sendWsError(c, "invalid_json");
        return;
    }

    if (!doc.containsKey("button") || !doc.containsKey("state")) {
        _LOG_W("Button command missing 'button' or 'state' field\n");
        sendWsError(c, "missing_fields");
        return;
    }

    if (!doc["button"].is<const char*>()) {
        _LOG_W("Button command has invalid 'button' type\n");
        sendWsError(c, "invalid_button");
        return;
    }
    const char *btnName = doc["button"].as<const char*>();
    if (btnName == nullptr || btnName[0] == '\0') {
        _LOG_W("Button command has empty 'button' value\n");
        sendWsError(c, "invalid_button");
        return;
    }

    if (!(doc["state"].is<int>() || doc["state"].is<bool>())) {
        _LOG_W("Button command has invalid 'state' type\n");
        sendWsError(c, "invalid_state");
        return;
    }
    const int state = doc["state"].as<int>();
    if (state != 0 && state != 1) {
        _LOG_W("Button command has invalid 'state' value: %d\n", state);
        sendWsError(c, "invalid_state");
        return;
    }
    const bool btnDown = state == 1;

    // Button state bitmasks
    static constexpr uint8_t RIGHT_MASK = 0b100;
    static constexpr uint8_t MIDDLE_MASK = 0b010;
    static constexpr uint8_t LEFT_MASK = 0b001;
    static constexpr uint8_t ALL_BUTTONS_UP = 0b111;

    uint8_t mask = 0;
    if (strcmp(btnName, "right") == 0) {
        mask = RIGHT_MASK;
    } else if (strcmp(btnName, "middle") == 0) {
        mask = MIDDLE_MASK;
    } else if (strcmp(btnName, "left") == 0) {
        mask = LEFT_MASK;
    } else {
        _LOG_W("Unknown button name: %s\n", btnName);
        sendWsError(c, "unknown_button");
        return;
    }

    // Update button state with mutex protection
    xSemaphoreTake(buttonMutex, portMAX_DELAY);
    if (btnDown) {
        ButtonStateOverride = ALL_BUTTONS_UP & ~mask;
    } else {
        ButtonStateOverride = ALL_BUTTONS_UP | mask;
    }
    LastBtnOverrideTime = millis();
    xSemaphoreGive(buttonMutex);

    _LOG_V("WebSocket button command: %s = %s\n", btnName, btnDown ? "down" : "up");

    // Send acknowledgment back to client
    char ack[64];
    int n = snprintf(ack, sizeof(ack), "{\"button\":{\"%s\":\"%s\"}}", btnName, btnDown ? "down" : "up");
    if (n > 0) mg_ws_send(c, ack, (size_t)n, WEBSOCKET_OP_TEXT);

    // Schedule LCD image update after a short delay to allow button processing
    // The timer will fire ~100ms after button press, giving the device time to update the LCD
    if (LCDImageTimer != nullptr) {
        LCDImageTimer->expire = mg_millis() + 100;  // Update in 100ms
    }
}

// HTML web form for entering WIFI credentials in AP setup portal
static const char *html_form = R"EOF(
<!DOCTYPE html><html><head>
<title>WiFi Setup</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>body{font-family:Arial;margin:0;padding:10px;display:flex;justify-content:center}
form{width:90%;max-width:300px}
h2{font-size:20px;text-align:center;margin:10px 0}
label{display:block;margin:5px 0}
input[type=text],input[type=password]{width:100%;padding:8px;font-size:14px;border:1px solid #ccc;box-sizing:border-box}
input[type=submit]{width:100%;padding:8px;font-size:14px;background:#4CAF50;color:#fff;border:0;cursor:pointer}
input[type=submit]:hover{background:#45a049}
@media (max-width:600px){form{width:95%}}</style>
<script>function togglePassword(){var x=document.getElementById('password');x.type=x.type==='password'?'text':'password'}
window.onload=function(){document.getElementById('tz').value=Intl.DateTimeFormat().resolvedOptions().timeZone}</script>
</head>
<body><form action="/save" method="POST">
<h2>WiFi Setup</h2>
)EOF"
#ifdef SENSORBOX_VERSION
"<small>Sensorbox only connects to 2.4 GHz networks.</small>"
#else
"<small>SmartEVSE only connects to 2.4 GHz networks.</small>"
#endif
R"EOF(
<label>SSID:</label>
<input type="text" name="ssid" required minlength="1" maxlength="32" pattern="[ -~]{1,32}" title="SSID must be 1-32 printable characters">
<label>Password:</label>
<input type="password" name="password" id="password" required minlength="8" maxlength="63" pattern="[ -~]{8,63}" title="Password must be 8-63 printable characters">
<label><input type="checkbox" onclick="togglePassword()">Show Password</label>
<input type="hidden" name="tz" id="tz">
<input type="submit" value="Save">
</form></body></html>
)EOF";


// Maximum concurrent HTTP connections to prevent socket exhaustion.
// The ESP32 lwIP stack only has CONFIG_LWIP_MAX_SOCKETS (16) sockets total,
// shared by EVERYTHING (both HTTP listeners, both MQTT clients, mDNS, SNTP,
// and the RemoteDebug telnet listener/client).
#define MAX_HTTP_CONNECTIONS 4
#define WS_CONNECTION_RESERVE 1

// Count only accepted inbound server connections.
// (This does not count listeners, outbound client connections,
// or connections already closing, so the connection limit reflects actual
// in-use HTTP/WebSocket server slots more accurately).
static int countConnections(struct mg_mgr *mgr) {
  int n = 0;
  for (struct mg_connection *t = mgr->conns; t != NULL; t = t->next) {
    if (t->is_accepted && !t->is_client && !t->is_listening && !t->is_closing) n++;
  }
  return n;
}

// Connection event handler function
// indenting lower level two spaces to stay compatible with old StartWebServer
// We use the same event handler function for HTTP and HTTPS connections
// fn_data is NULL for plain HTTP, and non-NULL for HTTPS
static void fn_http_server(struct mg_connection *c, int ev, void *ev_data) {
  if (ev == MG_EV_ACCEPT) {
    // Limit concurrent connections to prevent socket exhaustion
    int nconns = countConnections(c->mgr);
    if (nconns > (MAX_HTTP_CONNECTIONS + WS_CONNECTION_RESERVE)) {
      _LOG_W("Too many connections (%d), rejecting new connection\n", nconns);
      c->is_closing = 1;  // Immediately close the connection
      return;
    }
    // Initialize TLS for HTTPS connections (fn_data != NULL)
    if (c->fn_data != NULL) {
    struct mg_tls_opts opts = { .ca = empty, .cert = mg_unpacked("/data/cert.pem"), .key = mg_unpacked("/data/key.pem"), .name = empty, .skip_verification = 0};
    mg_tls_init(c, &opts);
    }
  } else if (ev == MG_EV_CLOSE) {
    if (c == HttpListener80) {
        _LOG_A("Free HTTP port 80");
        HttpListener80 = nullptr;
    }
    if (c == HttpListener443) {
        _LOG_A("Free HTTP port 443");
        HttpListener443 = nullptr;
    }
    // Remove websocket connection from tracking list
    for (auto it = wsLcdConnections.begin(); it != wsLcdConnections.end(); ++it) {
        if (*it == c) {
            wsLcdConnections.erase(it);
            _LOG_V("Removed websocket LCD connection, remaining: %d\n", wsLcdConnections.size());

            // Stop timer if no more connections
            if (wsLcdConnections.empty()) stopLCDImageTimer(c->mgr);
            break;
        }
    }
    if (wsLcdConnections.empty()) stopLCDImageTimer(c->mgr);
  } else if (ev == MG_EV_WS_OPEN) {
    // Websocket connection opened - check if it's for /ws/lcd endpoint
    struct mg_http_message *hm = (struct mg_http_message *) ev_data;
    if (mg_match(hm->uri, mg_str("/ws/lcd"), NULL)) {
        wsLcdConnections.push_back(c);
        _LOG_V("New websocket LCD connection, total: %d\n", wsLcdConnections.size());

        // Start timer if this is the first connection
        if (wsLcdConnections.size() == 1 && LCDImageTimer == nullptr) {
            LCDImageTimer = mg_timer_add(&mgr, 1000, MG_TIMER_REPEAT | MG_TIMER_RUN_NOW, lcd_image_timer_fn, &mgr);
            _LOG_V("Started LCD image timer\n");
        }
    }
  } else if (ev == MG_EV_WS_MSG) {
    // Websocket message received - handle button commands
    struct mg_ws_message *wm = (struct mg_ws_message *) ev_data;
    if (!isTrackedLcdWsConnection(c)) return;

    // Check if this is a text message (button commands are JSON text)
    if ((wm->flags & 0x0f) == WEBSOCKET_OP_TEXT) {
        handleButtonCommand(c, (const char*)wm->data.buf, wm->data.len);
    }
    // Binary messages are ignored (only server sends binary BMP images)
  } else if (ev == MG_EV_HTTP_MSG) {  // New HTTP request received
    struct mg_http_message *hm = (struct mg_http_message *) ev_data;            // Parsed HTTP request

    // Check for websocket upgrade request for LCD image stream
    if (mg_match(hm->uri, mg_str("/ws/lcd"), NULL)) {
        mg_ws_upgrade(c, hm, NULL);  // Upgrade HTTP to WebSocket
        return;  // Don't process as regular HTTP
    }

    const int nconns = countConnections(c->mgr);
    if (nconns > MAX_HTTP_CONNECTIONS) {
        mg_http_reply(c, 503, "Connection: close\r\nContent-Type: text/plain\r\n",
                      "Server busy, retry shortly");
        c->is_draining = 1;
        return;
    }

    static webServerRequest requestObj;  // Static to avoid heap allocation on every request
    webServerRequest* request = &requestObj;
    request->setMessage(hm);
//make mongoose 7.14 compatible with 7.13
#define mg_http_match_uri(X,Y) mg_match(X->uri, mg_str(Y), NULL)
    // In portal mode, only allow the portal page, /save and /erasesettings
    if (WIFImode == 2 &&
        !mg_match(hm->uri, mg_str("/"),              NULL) &&
        !mg_match(hm->uri, mg_str("/save"),          NULL) &&
        !mg_match(hm->uri, mg_str("/erasesettings"), NULL)) {
        mg_http_reply(c, 403, "Content-Type: text/plain\r\n", "Not available in portal mode");
        return;
    }
    // handles URI and response, returns true if handled, false if not
    if (!handle_URI(c, hm, request)) {
        if (mg_match(hm->uri, mg_str("/erasesettings"), NULL)) {
            if ( preferences.begin("settings", false) ) {         // our own settings
              preferences.clear();
              preferences.end();
            }
            if (preferences.begin("nvs.net80211", false) ) {      // WiFi settings used by ESP
              preferences.clear();
              preferences.end();       
            }
#ifndef SENSORBOX_VERSION
            DeleteAllRFID();                                      // All RFID UIDs
#endif            
            shouldReboot = true;
            mg_http_reply(c, 200, "Content-Type: text/plain\r\n", "Erasing settings, rebooting");
        } else if (mg_http_match_uri(hm, "/") && WIFImode == 2) { // serve AP page to fill in WIFI credentials
            mg_http_reply(c, 200, "Content-Type: text/html\r\n", "%s", html_form);
        // save WiFi credentials, make sure we are still in WiFiPortal mode    
        } else if (mg_http_match_uri(hm, "/save") && WIFImode == 2) {
            char ssid[33], password[64], tz[64];
            bool has_ssid = mg_http_get_var(&hm->body, "ssid", ssid, sizeof(ssid)) > 0;
            bool has_pass = mg_http_get_var(&hm->body, "password", password, sizeof(password)) > 0;
            mg_http_get_var(&hm->body, "tz", tz, sizeof(tz));  // Timezone
            if (has_ssid && has_pass) {
                // Store timezone name if provided (will be converted to TZ_INFO on next boot)
                if (tz[0]) {
                    TZname = tz;
                    if (preferences.begin("settings", false)) {
                        preferences.putString("TZname", TZname);
                        preferences.end();
                    }
                    _LOG_A("Browser timezone saved: %s\n", tz);
                }
                mg_http_reply(c, 200, "Content-Type: text/html\r\n",
                    "<!DOCTYPE html><html><head><title>Saved</title>"
                    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
                    "<style>body{font-family:Arial;padding:20px;text-align:center}</style></head>"
                    "<body><h2>Saved!</h2><p>Connecting to <b>%s</b></p>"
                    "<p>Access at: <b>http://%s.local</b></p><p>Rebooting...</p></body></html>",
                    ssid, APhostname.c_str());
                _LOG_A("Connecting to wifi network.\n");
                WiFi.begin(ssid, password);                         // Configure Wifi with credentials
                WIFImode = 1;                                       // we are already connected so don't call handleWIFImode
                write_settings();
                shouldReboot = true;                                // Allow the webserver to send the reply back before rebooting
            } else {
              mg_http_reply(c, 400, "", "Missing SSID or password");
            }
        } else if (mg_http_match_uri(hm, "/autoupdate")) {
            char owner[40];
            char buf[8];
            int debug;
            mg_http_get_var(&hm->query, "owner", owner, sizeof(owner));
            mg_http_get_var(&hm->query, "debug", buf, sizeof(buf));
            debug = strtol(buf, NULL, 0);
            if (!memcmp(owner, OWNER_FACT, sizeof(OWNER_FACT)) || (!memcmp(owner, OWNER_COMM, sizeof(OWNER_COMM)))) {
#ifdef SENSORBOX_VERSION
                asprintf(&downloadUrl, "%s/%s_sensorboxv2_firmware.%ssigned.bin", FW_DOWNLOAD_PATH, owner, debug ? "debug.": ""); //will be freed in FirmwareUpdate() ; format: http://s3.com/dingo35_sensorboxv2_firmware.debug.signed.bin
#else
                asprintf(&downloadUrl, "%s/%s_firmware.%ssigned.bin", FW_DOWNLOAD_PATH, owner, debug ? "debug.": ""); //will be freed in FirmwareUpdate() ; format: http://s3.com/dingo35_firmware.debug.signed.bin

#endif
                RunFirmwareUpdate();
            }                                                                       // after the first call we just report progress
            DynamicJsonDocument doc(64); // https://arduinojson.org/v6/assistant/
            doc["progress"] = downloadProgress;
            doc["size"] = downloadSize;
            String json;
            serializeJson(doc, json);
            mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s\n", json.c_str());    // Yes. Respond JSON
        } else if (mg_http_match_uri(hm, "/update")) {
            //modified version of mg_http_upload
            char buf[20] = "0", file[40];
            size_t max_size = 0x1B0000;                                             //from partition_custom.csv
            long res = 0, offset, size;
            mg_http_get_var(&hm->query, "offset", buf, sizeof(buf));
            mg_http_get_var(&hm->query, "file", file, sizeof(file));
            offset = strtol(buf, NULL, 0);
            buf[0] = '0';
            mg_http_get_var(&hm->query, "size", buf, sizeof(buf));
            size = strtol(buf, NULL, 0);
            if (hm->body.len == 0) {
              struct mg_http_serve_opts opts = {.root_dir = "/data", .ssi_pattern = NULL, .extra_headers = NULL, .mime_types = NULL, .page404 = NULL, .fs = &mg_fs_packed };
              mg_http_serve_file(c, hm, "/data/update2.html", &opts);
            } else if (file[0] == '\0') {
              mg_http_reply(c, 400, "", "file required");
              res = -1;
            } else if (offset < 0) {
              mg_http_reply(c, 400, "", "offset required");
              res = -3;
            } else if ((size_t) offset + hm->body.len > max_size) {
              mg_http_reply(c, 400, "", "over max size of %lu", (unsigned long) max_size);
              res = -4;
            } else if (size <= 0) {
              mg_http_reply(c, 400, "", "size required");
              res = -5;
            } else {
                if (!memcmp(file,"firmware.bin", sizeof("firmware.bin")) || !memcmp(file,"firmware.debug.bin", sizeof("firmware.debug.bin"))) {
                    if(!offset) {
                        _LOG_A("Update Start: %s\n", file);
                        if(!Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000), U_FLASH) {
                            _LOG_A("ERROR: Update has error:%s.\n", Update.errorString());
                            Update.printError(Serial);
                        }
                    }
                    if(!Update.hasError()) {
                        if(Update.write((uint8_t*) hm->body.buf, hm->body.len) != hm->body.len) {
                            _LOG_A("ERROR: Update has error:%s.\n", Update.errorString());
                            Update.printError(Serial);
                        } else {
                            _LOG_A("bytes written %lu\r", offset + hm->body.len);
                        }
                    }
                    if (offset + hm->body.len >= size) {                                           //EOF
                        if(Update.end(true)) {
                            _LOG_A("\nUpdate Success\n");
                            delay(1000);
                            ESP.restart();
                        } else {
                            _LOG_A("ERROR: Update has error:%s.\n", Update.errorString());
                            Update.printError(Serial);
                        }
                    }
                } else //end of firmware.bin
                if (!memcmp(file,"firmware.signed.bin", sizeof("firmware.signed.bin")) || !memcmp(file,"firmware.debug.signed.bin", sizeof("firmware.debug.signed.bin"))) {
    #define dump(X)   for (int i= 0; i< SIGNATURE_LENGTH; i++) _LOG_A_NO_FUNC("%02x", X[i]); _LOG_A_NO_FUNC(".\n");
                    if(!offset) {
                        _LOG_A("Update Start: %s\n", file);
                        signature = (unsigned char *) malloc(SIGNATURE_LENGTH);                       //tried to free in in all exit scenarios, RISK of leakage!!!
                        memcpy(signature, hm->body.buf, SIGNATURE_LENGTH);          //signature is prepended to firmware.bin
                        hm->body.buf = hm->body.buf + SIGNATURE_LENGTH;
                        hm->body.len = hm->body.len - SIGNATURE_LENGTH;
                        _LOG_A("Firmware signature:");
                        dump(signature);
                        if(!Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000), U_FLASH) {
                            _LOG_A("ERROR: Update has error:%s.\n", Update.errorString());
                            Update.printError(Serial);
                        }
                    }
                    if(!Update.hasError()) {
                        if(Update.write((uint8_t*) hm->body.buf, hm->body.len) != hm->body.len) {
                            _LOG_A("ERROR: Update has error:%s.\n", Update.errorString());
                            Update.printError(Serial);
                            FREE(signature);
                        } else {
                            _LOG_A("bytes written %lu\r", offset + hm->body.len);
                        }
                    }
                    if (offset + hm->body.len >= size) {                                           //EOF
                        //esp_err_t err;
                        const esp_partition_t* target_partition = esp_ota_get_next_update_partition(NULL);              // the newly updated partition
                        if (!target_partition) {
                            _LOG_A("ERROR: Can't access firmware partition to check signature!");
                            mg_http_reply(c, 400, "", "firmware.signed.bin update failed!");
                        }
                        const esp_partition_t* running_partition = esp_ota_get_running_partition();
                        _LOG_V("Running off of partition %s, trying to update partition %s.\n", running_partition->label, target_partition->label);
                        esp_ota_set_boot_partition( running_partition );            // make sure we have not switched boot partitions
    
                        bool verification_result = false;
                        if(Update.end(true)) {
                            verification_result = validate_sig( target_partition, signature, size - SIGNATURE_LENGTH);
                            FREE(signature);
                            if (verification_result) {
                                _LOG_A("Signature is valid!\n");
                                esp_ota_set_boot_partition( target_partition );
                                _LOG_A("\nUpdate Success\n");
                                shouldReboot = true;
                                //ESP.restart(); does not finish the call to fn_http_server, so the last POST of apps.js gets no response....
                                //which results in a "verify failed" message on the /update screen AFTER the reboot :-)
                            }
                        }
                        if (!verification_result) {
                            _LOG_A("Update failed! ERROR:%s.\n", Update.errorString());
                            Update.printError(Serial);
                            //Update.abort(); //not sure this does anything in this stage
                            //Update.rollBack();
                            _LOG_V("Running off of partition %s, erasing partition %s.\n", running_partition->label, target_partition->label);
                            esp_partition_erase_range( target_partition, target_partition->address, target_partition->size );
                            esp_ota_set_boot_partition( running_partition );
                            mg_http_reply(c, 400, "", "firmware.signed.bin update failed!");
                        }
                        FREE(signature);
                    }
                } else //end of firmware.signed.bin
#if SMARTEVSE_VERSION >=30
                if (!memcmp(file,"rfid.txt", sizeof("rfid.txt"))) {
                    if (offset != 0) {
                        mg_http_reply(c, 400, "", "rfid.txt too big, only 100 rfid's allowed!");
                    }
                    else {
                        //we are overwriting all stored RFID's with the ones uploaded
                        DeleteAllRFID();
                        res = offset + hm->body.len;
                        unsigned int RFID_UID[8] = {1, 0, 0, 0, 0, 0, 0, 0};
                        char RFIDtxtstring[20];                                     // 17 characters + NULL terminator
                        int r, pos = 0;
                        int beginpos = 0;
                        while (pos <= hm->body.len) {
                            char c;
                            c = *(hm->body.buf + pos);
                            //_LOG_A_NO_FUNC("%c", c);
                            if (c == '\n' || pos == hm->body.len) {
                                strncpy(RFIDtxtstring, hm->body.buf + beginpos, 19);         // in case of DOS the 0x0D is stripped off here
                                RFIDtxtstring[19] = '\0';
                                r = sscanf(RFIDtxtstring,"%02x%02x%02x%02x%02x%02x%02x", &RFID_UID[0], &RFID_UID[1], &RFID_UID[2], &RFID_UID[3], &RFID_UID[4], &RFID_UID[5], &RFID_UID[6]);
                                RFID_UID[7]=crc8((unsigned char *) RFID_UID,7);
                                if (r == 7) {
                                    _LOG_A("Store RFID_UID %02x%02x%02x%02x%02x%02x%02x, crc=%02x.\n", RFID_UID[0], RFID_UID[1], RFID_UID[2], RFID_UID[3], RFID_UID[4], RFID_UID[5], RFID_UID[6], RFID_UID[7]);
                                    LoadandStoreRFID(RFID_UID);
                                } else {
                                    strncpy(RFIDtxtstring, hm->body.buf + beginpos, 17);         // in case of DOS the 0x0D is stripped off here
                                    RFIDtxtstring[17] = '\0';
                                    RFID_UID[0] = 0x01;
                                    r = sscanf(RFIDtxtstring,"%02x%02x%02x%02x%02x%02x", &RFID_UID[1], &RFID_UID[2], &RFID_UID[3], &RFID_UID[4], &RFID_UID[5], &RFID_UID[6]);
                                    RFID_UID[7]=crc8((unsigned char *) RFID_UID,7);
                                    if (r == 6) {
                                        _LOG_A("Store RFID_UID %02x%02x%02x%02x%02x%02x, crc=%02x.\n", RFID_UID[1], RFID_UID[2], RFID_UID[3], RFID_UID[4], RFID_UID[5], RFID_UID[6], RFID_UID[7]);
                                        LoadandStoreRFID(RFID_UID);
                                    }
                                }
                                beginpos = pos + 1;
                            }
                            pos++;
                        }
                    }
                } else //end of rfid.txt
                    mg_http_reply(c, 400, "", "only allowed to flash firmware.bin, firmware.debug.bin, firmware.signed.bin, firmware.debug.signed.bin or rfid.txt");
#else
                    mg_http_reply(c, 400, "", "only allowed to flash firmware.bin, firmware.debug.bin, firmware.signed.bin, firmware.debug.signed.bin");
#endif
                mg_http_reply(c, 200, "", "%ld", res);
            }
        } else if (mg_http_match_uri(hm, "/reboot")) {
            shouldReboot = true;
#ifndef SMARTEVSE_VERSION //sensorbox
            mg_http_reply(c, 200, "", "Rebooting after 5s...");
#else
            if (State == STATE_C) {
                mg_http_reply(c, 202, "", "Reboot scheduled: Device will reboot 5 seconds after the EV stops charging...");
            } else {
                mg_http_reply(c, 200, "", "Device will reboot in 5 seconds...");
            }
#endif
        } else if (mg_http_match_uri(hm, "/settings") && !memcmp("POST", hm->method.buf, hm->method.len)) {
            DynamicJsonDocument doc(64);
#if MQTT
            if (request->hasParam("mqtt_update") && request->getParam("mqtt_update")->value().toInt() == 1) {

                if(request->hasParam("mqtt_host")) {
                    MQTTHost = request->getParam("mqtt_host")->value();
                    doc["mqtt_host"] = MQTTHost;
                }

                if(request->hasParam("mqtt_port")) {
                    MQTTPort = request->getParam("mqtt_port")->value().toInt();
                    if (MQTTPort == 0) MQTTPort = 1883;
                    doc["mqtt_port"] = MQTTPort;
                }

                if(request->hasParam("mqtt_topic_prefix")) {
                    MQTTprefix = request->getParam("mqtt_topic_prefix")->value();
                    if (!MQTTprefix || MQTTprefix == "") {
                        MQTTprefix = APhostname;
                    }
                    doc["mqtt_topic_prefix"] = MQTTprefix;
                }

                if(request->hasParam("mqtt_username")) {
                    MQTTuser = request->getParam("mqtt_username")->value();
                    if (!MQTTuser || MQTTuser == "") {
                        MQTTuser.clear();
                    }
                    doc["mqtt_username"] = MQTTuser;
                }

                if(request->hasParam("mqtt_password")) {
                    MQTTpassword = request->getParam("mqtt_password")->value();
                    if (!MQTTpassword || MQTTpassword == "") {
                        MQTTpassword.clear();
                    }
                    doc["mqtt_password_set"] = (MQTTpassword != "");
                }

                if (request->hasParam("mqtt_tls")) {
                    MQTTtls = request->getParam("mqtt_tls")->value() == "1";
                    doc["mqtt_tls"] = MQTTtls;
                }

                if(request->hasParam("mqtt_ca_cert")) {
                    String cert = request->getParam("mqtt_ca_cert")->value();
                    writeMqttCaCert(cert);                      // Save to LittleFS
                    doc["mqtt_ca_cert_set"] = !cert.isEmpty();
                }

                // disconnect mqtt so it will automatically reconnect with then new params
                MQTTclient.disconnect();
#if MQTT_ESP == 1
                MQTTclient.connect();
#endif

                if (preferences.begin("settings", false) ) {
                    preferences.putString("MQTTpassword", MQTTpassword);
                    preferences.putString("MQTTuser", MQTTuser);
                    preferences.putString("MQTTprefix", MQTTprefix);
                    preferences.putString("MQTTHost", MQTTHost);
                    preferences.putUShort("MQTTPort", MQTTPort);
                    preferences.putBool("MQTTtls", MQTTtls);
                    preferences.end();
                }
            }
#endif
            String json;
            serializeJson(doc, json);
            mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s\r\n", json.c_str());    // Yes. Respond JSON
        } else if (mg_http_match_uri(hm, "/mqtt_ca_cert") && !memcmp("GET", hm->method.buf, hm->method.len)) {
            String cert = readMqttCaCert();
            mg_http_reply(c, 200, "Content-Type: text/plain\r\n", "%s\r\n", cert.c_str());
        } else {                                                                    // if everything else fails, serve static page
            // Cache ".webp" or ".ico" image files for one year without revalidation or server checks.
            if (mg_match(hm->uri, mg_str("#.webp"), NULL) ||
                mg_match(hm->uri, mg_str("#.ico"), NULL)) {
                struct mg_http_serve_opts opts = {
                    .root_dir = "/data", .ssi_pattern = NULL,
                    .extra_headers = "Cache-Control: public, max-age=31536000\r\n",
                    .mime_types = NULL, .page404 = NULL, .fs = &mg_fs_packed
                };
                mg_http_serve_dir(c, hm, &opts);
            } else {
                struct mg_http_serve_opts opts = {.root_dir = "/data", .ssi_pattern = NULL, .extra_headers = NULL, .mime_types = NULL, .page404 = NULL, .fs = &mg_fs_packed };
                //opts.fs = NULL;
                mg_http_serve_dir(c, hm, &opts);
            }
        }
    } // handle_URI
    // request is static, no delete needed
  } //HTTP request received
}

// turns out getLocalTime only checks if the current year > 2016, and if so, decides NTP must have synced;
// this callback function actually checks if we are synced!
// NOTE: This callback is called EVERY time SNTP syncs (every 3 hours), not just the first time!
void timeSyncCallback(struct timeval *tv)
{
    // WARNING: Do NOT add \n to this log message! This callback runs in lwIP SNTP task context.
    // Adding \n causes RemoteDebug to immediately flush the buffer via TelnetClient.print(),
    // which is a blocking TCP send. This deadlocks because lwIP is waiting for this callback
    // to return while the TCP send needs lwIP to process packets.
    _LOG_A("Synced clock to NTP server!");
#if MQTT && MQTT_ESP && SMARTEVSE_VERSION 
    // Start SmartEVSE MQTT connection after time is synced (TLS requires correct time for certificate validation)
    // Only connect on first sync - subsequent syncs should not restart the MQTT connection!
    if (!LocalTimeSet) {
        MQTTclientSmartEVSE.connect();
    }
#endif
    LocalTimeSet = true;
}

// Returns true if any network interface (WiFi or Ethernet) has an IP address
bool NetworkConnected(void) {
#if SMARTEVSE_VERSION >= 30 && SMARTEVSE_VERSION < 40
    if (EthHasIP) return true;
#endif
    if (!WiFi.isConnected()) return false;
    return WiFi.localIP() != IPAddress((uint32_t)0);
}

static bool servicesStarted = false;

// Start network services (HTTP, MQTT, mDNS, SNTP, RemoteDebug).
// Safe to call multiple times — the HTTP listeners are (re)tried every call
// (in case an earlier bind failed, e.g. transient socket exhaustion), while
// the one-time-only steps below are still guarded by servicesStarted.
static void startNetworkServices(void) {
    mg_log_set(MG_LL_NONE);

    // Start HTTP listeners (bind to 0.0.0.0 — works on all interfaces)
    if (!HttpListener80) {
        HttpListener80 = mg_http_listen(&mgr, "http://0.0.0.0:80", fn_http_server, NULL);
        if (!HttpListener80) _LOG_A("ERROR: Failed to start HTTP listener on port 80\n");
    }
    if (!HttpListener443) {
        HttpListener443 = mg_http_listen(&mgr, "http://0.0.0.0:443", fn_http_server, (void *)1);
        if (!HttpListener443) _LOG_A("ERROR: Failed to start HTTP listener on port 443\n");
    }
    if (HttpListener80 || HttpListener443) _LOG_A("HTTP server started\n");

    if (servicesStarted) return;
    servicesStarted = true;

#if MQTT
#if MQTT_ESP == 0
    if (!MQTTtimer) {
        MQTTtimer = mg_timer_add(&mgr, 3000, MG_TIMER_REPEAT | MG_TIMER_RUN_NOW, timer_fn, &mgr);
    }
#else
    if (MQTTHost != "" && MQTTclient.client)
        esp_mqtt_client_start(MQTTclient.client);
#ifdef SMARTEVSE_VERSION
    if (MQTTSmartServer && MQTTclientSmartEVSE.client)
        esp_mqtt_client_start(MQTTclientSmartEVSE.client);
#endif
#endif
#endif //MQTT

#if DBG == 1
    Debug.begin(APhostname, 23, 1);
    Debug.showColors(true);
#endif
}

// Configure DNS, SNTP and mDNS when an interface gets an IP.
// Can be called from both WiFi and Ethernet got-IP events.
void onGotIP(const char *dns_ip) {
    clearmDNSServices();

    // Load DHCP DNS into mongoose
    static char dns4url[] = "udp://123.123.123.123:53";
    if (dns_ip && strlen(dns_ip) > 0) {
        snprintf(dns4url, sizeof(dns4url), "udp://%s:53", dns_ip);
        mgr.dns4.url = dns4url;
    }

    // Configure SNTP (safe to call again)
    if (!esp_sntp_enabled()) {
        esp_sntp_setservername(1, "europe.pool.ntp.org");
        sntp_set_time_sync_notification_cb(timeSyncCallback);
        esp_sntp_init();
    }

    if (TZinfo == "") {
        xTaskCreate(setTimeZone, "setTimeZone", 4096, NULL, 1, NULL);
    }

    // Start mDNS
    if (!MDNS.begin(APhostname.c_str())) {
        _LOG_A("Error setting up MDNS responder!\n");
    } else {
        _LOG_A("mDNS responder started. http://%s.local\n", APhostname.c_str());
        MDNS.addService("http", "tcp", 80);
    }

    startNetworkServices();
}

void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
        case WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP:
#if LOG_LEVEL >= 1
            _LOG_A("Connected to AP: %s Local IP: %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
#else
            Serial.printf("Connected to AP: %s Local IP: %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
#endif            
            onGotIP(WiFi.dnsIP().toString().c_str());
            break;
        case WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED:
            _LOG_A("Connected or reconnected to WiFi\n");
            break;
        case WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        case WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_LOST_IP:
            if (WIFImode == 1) {
#if MQTT
                //mg_timer_free(&mgr);
#endif
                WiFi.reconnect();                                               // recommended reconnection strategy by ESP-IDF manual
            }
            break;
        // for some reason this is not necessary in the SmartEVSEv3 code, but it is for Sensorbox v2:
        case ARDUINO_EVENT_SC_GOT_SSID_PSWD:
        {
            _LOG_A("Got SSID and password.\n");

            uint8_t ssid[33] = { 0 };
            uint8_t password[65] = { 0 };
            memcpy(ssid, info.sc_got_ssid_pswd.ssid, sizeof(info.sc_got_ssid_pswd.ssid));
            memcpy(password, info.sc_got_ssid_pswd.password, sizeof(info.sc_got_ssid_pswd.password));
            WiFi.begin((char*)ssid, (char *)password);
        }
        break;
        default: 
            _LOG_A("WiFi Event: %d (not handled!)\n", event);
        break;                                                         // prevent compiler warnings
  }
}


void handleWIFImode() {
#if SMARTEVSE_VERSION >= 30 && SMARTEVSE_VERSION < 40
    // Ethernet takes priority: when cable is connected, disable WiFi
    if (EthConnected) {
        if (WiFi.getMode() != WIFI_OFF) {
            _LOG_A("Ethernet connected, stopping WiFi..\n");
            WiFi.softAPdisconnect(true);
            WiFi.disconnect(true);
        }
        return;
    }
#endif

    if (WIFImode == 2 && WiFi.getMode() != WIFI_AP_STA) {
        _LOG_A("Start Portal...\n");

#ifndef SENSORBOX_VERSION
        // Start WiFi as AP
        WiFi.softAP("SmartEVSE-config", APpassword);
#else
        APpassword = "12345678";
        WiFi.softAP("Sensorbox-config", APpassword);
#endif
        IPAddress IP = WiFi.softAPIP();

        if (!HttpListener80) {
            HttpListener80 = mg_http_listen(&mgr, "http://0.0.0.0:80", fn_http_server, NULL);  // Setup listener
        }
        if (!HttpListener443) {
            HttpListener443 = mg_http_listen(&mgr, "http://0.0.0.0:443", fn_http_server, (void *) 1);  // Setup listener
        }
        _LOG_A("HTTP server started\n");
    }

    if (WIFImode == 1 && !WiFi.isConnected()) {
        _LOG_A("Starting WiFi..\n");
        WiFi.mode(WIFI_STA);
        WiFi.begin();
    }    

    if (WIFImode == 0 && WiFi.getMode() != WIFI_OFF) {
        _LOG_A("Stopping WiFi..\n");
        WiFi.softAPdisconnect(true);
        WiFi.disconnect(true);
    }    
}

// Compute SHA256 hash of raw 32-byte EC private key
// Returns first 32 hex chars of hash
String getEcPrivateKeyHashRaw(const unsigned char* key) {
    unsigned char hash[32];
    mbedtls_sha256(key, 32, hash, 0);
    
    String result;
    result.reserve(32);
    for (int i = 0; i < 16; i++) {
        char hex[3];
        snprintf(hex, sizeof(hex), "%02x", hash[i]);
        result += hex;
    }
    return result;
}

// Compute SHA256 hash of raw 32-byte EC private key from PEM string
// Returns first 32 hex chars of hash, or empty string on error
String getEcPrivateKeyHash(const String& pem) {
    int start = pem.indexOf("-----BEGIN EC PRIVATE KEY-----");
    if (start < 0) return "";
    start += 31;  // Skip header
    
    // Extract base64 content, stripping all whitespace
    unsigned char b64[128];
    int b64len = 0;
    for (int i = start; i < (int)pem.length() && b64len < 124; i++) {
        char c = pem[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || 
            (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=')
            b64[b64len++] = c;
    }
    
    // Decode base64 to DER
    unsigned char der[96];
    size_t olen;
    if (mbedtls_base64_decode(der, sizeof(der), &olen, b64, b64len) != 0) return "";
    
    // Raw 32-byte key is at offset 7 in DER (after: 30 len 02 01 01 04 20)
    return getEcPrivateKeyHashRaw(der + 7);
}

// Setup Wifi 
void WiFiSetup(void) {
    // We might need some sort of authentication in the future.
    // SmartEVSE v3 have programmed ECDSA-256 keys stored in nvs
    // Get serial number, hwversion, and private key from NVS or efuses (in priority order)
    uint16_t hwversion = 0;
    // Hardware version 01xx = SmartEVSE
    // xx01 = v3.0 first batch
    // xx02 = v3.0 second batch
    // xx03 = v3.1 (ESP32-mini)
    if (preferences.begin("KeyStorage", true)) {                                // true = readonly
        hwversion = preferences.getUShort("hwversion");
        serialnr = preferences.getUInt("serialnr");
        String ec_private = preferences.getString("ec_private");
        String ec_public = preferences.getString("ec_public");
        preferences.end();

        if (ec_private.length() > 0) {
            MQTTprivatePassword = getEcPrivateKeyHash(ec_private);
        }
        _LOG_D("NVS: hwversion=%04x serialnr=%u\n", hwversion, serialnr);
    }

    // Try efuses for any missing values
    if (!serialnr || !hwversion || MQTTprivatePassword.length() == 0) {
        uint8_t efuse_privatekey[32];
        uint8_t efuse_hwversion[2];
        uint8_t efuse_serialnr[3];
        esp_efuse_read_block(EFUSE_BLK1, efuse_privatekey, 0, 32*8);
        esp_efuse_read_block(EFUSE_BLK3, efuse_hwversion, 56, 16);
        esp_efuse_read_block(EFUSE_BLK3, efuse_serialnr, 72, 24);

        if (!serialnr) {
            serialnr = efuse_serialnr[0] + (efuse_serialnr[1] << 8) + (efuse_serialnr[2] << 16);
        }
        if (!hwversion) {
            hwversion = efuse_hwversion[0] + (efuse_hwversion[1] << 8);
        }
        if (MQTTprivatePassword.length() == 0) {
            // Check if efuse has a non-zero private key
            bool hasKey = false;
            for (uint8_t i = 0; i < 32 && !hasKey; i++) hasKey = (efuse_privatekey[i] != 0);
            if (hasKey) {
                MQTTprivatePassword = getEcPrivateKeyHashRaw(efuse_privatekey);
                _LOG_D("Using efuse private key\n");
            }
        }
    }

    // Fallback to MAC address if no serial number found
    if (!serialnr) {
        serialnr = MacId() & 0xffff;
        _LOG_A("No serialnr programmed, using MAC: %u\n", serialnr);
    }
    
    _LOG_A("hwversion=%04x serialnr=%u mqtt_pwd=%s\n", hwversion, serialnr, MQTTprivatePassword.c_str());

#ifndef SENSORBOX_VERSION
    APhostname = "SmartEVSE-" + String(serialnr);
#else
    APhostname = "Sensorbox-" + String(serialnr);
#endif
    WiFi.setHostname(APhostname.c_str());

    // set random AP password. Used when SetupWifi is active
    uint8_t i, c;
    for (i=0; i<8 ;i++) {
        c = random(16) + '0';
        if (c > '9') c += 'a'-'9'-1;
        APpassword[i] = c;
    }

    mg_mgr_init(&mgr);

    WiFi.setAutoReconnect(true);                                                // Required for Arduino 3
    //WiFi.persistent(true);
    WiFi.onEvent(onWifiEvent);

    if (preferences.begin("settings", false) ) {
        TZinfo = preferences.getString("TimezoneInfo","");
        TZname = preferences.getString("TZname","");
        if (TZinfo != "") {
            setenv("TZ",TZinfo.c_str(),1);
            tzset();
        }
#if MQTT
        MQTTpassword = preferences.getString("MQTTpassword");
        MQTTuser = preferences.getString("MQTTuser");
#ifdef SENSORBOX_VERSION
        MQTTprefix = preferences.getString("MQTTprefix", "Sensorbox/" + String(serialnr));
#else
        MQTTprefix = preferences.getString("MQTTprefix", "SmartEVSE/" + String(serialnr));
#endif
        MQTTHost = preferences.getString("MQTTHost", "");
        MQTTPort = preferences.getUShort("MQTTPort", 1883);
        MQTTtls = preferences.getBool("MQTTtls", false);
#endif //MQTT
        preferences.end();
    }

    // Briefly enable WiFi to create _arduino_event_group, which
    // WiFi.hostByName() DNS callbacks require even on Ethernet-only setups.
    WiFi.mode(WIFI_STA);

    handleWIFImode();                                                           //go into the mode that was saved in nonvolatile memory

#if MQTT && MQTT_ESP
    MQTTclient.connect();
#endif

}


// called by loop() in the main program
void network_loop() {
    static unsigned long lastCheck_net = 0;

    // Handle deferred WiFi mode changes (triggered by Ethernet events on the
    // sys_evt task, which has too little stack for WiFi.softAP / WiFi.begin)
    if (WIFImodeChanged) {
        WIFImodeChanged = false;
        handleWIFImode();
    }

#if MQTT && MQTT_ESP && SMARTEVSE_VERSION
    // Handle SmartEVSE MQTT server setting change (set by LCD menu)
    // This runs in main loop context where MQTT operations are safe
    if (MQTTSmartServerChanged) {
        MQTTSmartServerChanged = false;
        MQTTclientSmartEVSE.disconnect();
        if (MQTTSmartServer) MQTTclientSmartEVSE.connect();
    }
#endif

    if (millis() - lastCheck_net >= 1000) {
        lastCheck_net = millis();
        //this block is for non-time critical stuff that needs to run approx 1 / second
        time_t now;
        time(&now);                     // get seconds since Epoch
        localtime_r(&now, &timeinfo);   // convert seconds to localtime
        if (!LocalTimeSet && (WIFImode == 1 || EthHasIP)) {
            _LOG_A("Time not synced with NTP yet.\n");
        }
        _LOG_D("free heap: %u largest free block: %u\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    }

    mg_mgr_poll(&mgr, 100);                                                     // TODO increase this parameter to up to 1000 to make loop() less greedy

    if (NetworkConnected() && !mdnsDiscoveryHasRun &&
            (MainsMeter.Type == EM_HOMEWIZARD ||
             EVMeter.Type == EM_HOMEWIZARD ||
             CircuitMeter.Type == EM_HOMEWIZARD)) {
        discoverNetworkMeters();
    }

#ifndef DEBUG_DISABLED
    // Remote debug over WiFi
    Debug.handle();
#endif
}
#endif
