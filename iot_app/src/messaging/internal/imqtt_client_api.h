#pragma once

#include <cstddef>

struct mosquitto;
struct mosquitto_message;
struct mqtt5__property;
typedef struct mqtt5__property mosquitto_property;

namespace iot {
namespace messaging {
namespace internal {

using MqttConnectedCallback    = void (*)(struct mosquitto *, void *, int, int, const mosquitto_property *);
using MqttDisconnectedCallback = void (*)(struct mosquitto *, void *, int, const mosquitto_property *);
using MqttMessageCallback      = void (*)(struct mosquitto *, void *, const struct mosquitto_message *,
                                     const mosquitto_property *);

/*
 * Internal boundary around the libmosquitto calls used by the MQTT receiver.
 *
 * The normal implementation forwards every operation to libmosquitto. Unit
 * tests replace it with an in-memory implementation, so receiver behaviour can
 * be checked without a broker or network connection. Application code should
 * use MqttApplicationReceiver.
 */
class IMqttClientApi {
public:
  virtual ~IMqttClientApi() = default;

  virtual int               initializeLibrary()                                                                    = 0;
  virtual int               cleanupLibrary()                                                                       = 0;
  virtual struct mosquitto *createClient(const char *clientId, void *userData)                                     = 0;
  virtual void              destroyClient(struct mosquitto *mqttClient)                                            = 0;
  virtual int               selectMqtt5(struct mosquitto *mqttClient)                                              = 0;
  virtual int               setReconnectDelay(struct mosquitto *mqttClient)                                        = 0;
  virtual int         setCredentials(struct mosquitto *mqttClient, const char *username, const char *password)     = 0;
  virtual void        setConnectedCallback(struct mosquitto *mqttClient, MqttConnectedCallback callback)           = 0;
  virtual void        setDisconnectedCallback(struct mosquitto *mqttClient, MqttDisconnectedCallback callback)     = 0;
  virtual void        setMessageCallback(struct mosquitto *mqttClient, MqttMessageCallback callback)               = 0;
  virtual int         connectAsync(struct mosquitto *mqttClient, const char *host, int port, int keepAliveSeconds) = 0;
  virtual int         startNetworkLoop(struct mosquitto *mqttClient)                                               = 0;
  virtual int         disconnect(struct mosquitto *mqttClient)                                                     = 0;
  virtual int         stopNetworkLoop(struct mosquitto *mqttClient)                                                = 0;
  virtual int         subscribe(struct mosquitto *mqttClient, const char *topic)                                   = 0;
  virtual int         addJsonContentType(mosquitto_property **mqttProperties)                                      = 0;
  virtual int         publish(struct mosquitto *mqttClient, const char *topic, const void *payload, int payloadSize,
                              const mosquitto_property *mqttProperties)                                            = 0;
  virtual void        freeProperties(mosquitto_property **mqttProperties)                                          = 0;
  virtual const char *errorText(int errorCode) const                                                               = 0;
  virtual const char *reasonText(int reasonCode) const                                                             = 0;
};

/*
 * Returns the shared libmosquitto implementation used by main.cpp when it
 * creates MqttApplicationReceiver. The concrete implementation stays in the
 * MQTT source file.
 */
IMqttClientApi &mqttClientApi();

} // namespace internal
} // namespace messaging
} // namespace iot
