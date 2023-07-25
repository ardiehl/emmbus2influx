#ifndef MQTT_PUBLISH_H_INCLUDED
#define MQTT_PUBLISH_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif


#include "MQTTClient.h"
#include "MQTTClientPersistence.h"



typedef struct {
	char *clientId;
	char *hostname;
	char *topicPrefix;
	int lastBufSize;
	int port;
	char *url;  // will be created in mqtt_pub_connect
	MQTTClient client;
	MQTTProperties pub_props;
	MQTTClient_createOptions createOpts;
	MQTTClient_deliveryToken last_token;
	MQTTClient_connectOptions conn_opts;

} mqtt_pubT;


mqtt_pubT * mqtt_pub_init (const char * hostname, int port, const char *  clientId, const char *topicPrefix);

int mqtt_pub_free(mqtt_pubT *m);

int mqtt_pub_connect (mqtt_pubT *m);

int mqtt_pub (mqtt_pubT *m, char *topic, char *str, int timeoutMs, int qos, int retained);

// printf formatting
int mqtt_pub_strF (mqtt_pubT *m, char *topic, int timeoutMs, int qos, int retained, const char *fmt, ...);

int mqtt_pub_str (mqtt_pubT *m, char *topic, char *str, int isNumeric, int timeoutMs, int qos, int retained);

int mqtt_pub_int (mqtt_pubT *m, char *topic, int value, int timeoutMs, int qos, int retained);

int mqtt_pub_float (mqtt_pubT *m, char *topic, float value, int decimals, int timeoutMs, int qos, int retained);

#ifdef __cplusplus
}
#endif


#endif // MQTT_PUBLISH_H_INCLUDED
