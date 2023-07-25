#include "mqtt_publish.h"
#include "MQTTClient.h"
#include "MQTTClientPersistence.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>


MQTTClient_connectOptions conn_optsDefault = MQTTClient_connectOptions_initializer;
MQTTClient_createOptions createOpsDefault = MQTTClient_createOptions_initializer;
MQTTClient_message pubmsgDefault = MQTTClient_message_initializer;

mqtt_pubT * mqtt_pub_init (const char * hostname, int port, const char *  clientId, const char *topicPrefix) {
	mqtt_pubT * m;
	m = calloc(1,sizeof(mqtt_pubT));
	if (!m) return m;

	if (hostname) m->hostname = strdup(hostname);
	m->port = port;
	if (m->port <= 0) m->port = 1883;
	if (clientId) m->clientId = clientId;
	if (topicPrefix) m-> topicPrefix = strdup(topicPrefix);

	m->createOpts = createOpsDefault;
	m->conn_opts = conn_optsDefault;
	m->conn_opts.keepAliveInterval = 30;
    m->conn_opts.cleansession = 1;
	m->clientId = strdup(clientId);

	return m;
}


int mqtt_pub_free(mqtt_pubT *m) {
	if (m) {
		free(m->clientId);
		free(m->hostname);
		free(m->url);
		free(m->topicPrefix);
		if (m->client) {
			MQTTClient_disconnect(m->client,0);
			MQTTClient_destroy(&m->client);
			m->client = NULL;
		}
		free(m);
	}
	return 0;
}


#define URL_DEFBUFLEN 64

int mqtt_pub_connect (mqtt_pubT *m) {
	int rc;
	int urlBufLen = URL_DEFBUFLEN;
	int urlLen = URL_DEFBUFLEN;

	if (!m) return -1;

	while (urlLen >= urlBufLen) {
		if (m->url) {
			free(m->url);
			urlBufLen = urlBufLen * 2;
		}
		m->url = calloc(1,urlBufLen);
		urlLen = snprintf(m->url,urlBufLen,"tcp://%s:%d",m->hostname,m->port);
	}
	//printf("url: '%s' %d %d\n",m->url,urlBufLen,urlLen);

	if (m->client == NULL) {
		rc = MQTTClient_create(&m->client, m->url, m->clientId, MQTTCLIENT_PERSISTENCE_NONE, NULL);
		if (rc != MQTTCLIENT_SUCCESS) return rc;
		//printf("Client created\n");
	}
	if (!MQTTClient_isConnected(m->client)) {		// connect if not already connected
		rc = MQTTClient_connect(m->client, &m->conn_opts);
		if (rc != MQTTCLIENT_SUCCESS) return rc;
	}
	return 0;
}


int mqtt_pub (mqtt_pubT *m, char *topicIn, char *str, int timeoutMs, int qos, int retained) {
	MQTTClient_message pubmsg = pubmsgDefault;
	int rc;
	char *topic;
	int topicLen;

	topicLen = strlen(topicIn);
	if (m->topicPrefix) topicLen += strlen(m->topicPrefix);
	topic = malloc(topicLen+1);
	*topic = 0;
	if (m->topicPrefix) strcpy(topic,m->topicPrefix);
	strcat(topic,topicIn);

	pubmsg.payload = str;
	pubmsg.payloadlen = strlen(str);
	pubmsg.qos = qos;	// 0=Fire and forget - the message may not be delivered,
	pubmsg.retained = retained;
	rc = MQTTClient_publishMessage(m->client, topic, &pubmsg, &m->last_token);
	if (rc == MQTTCLIENT_DISCONNECTED) {		// try to reconnect
		rc = mqtt_pub_connect(m);
		if (rc != MQTTCLIENT_SUCCESS) {
			free(topic);
			return rc;
		}
		rc = MQTTClient_publishMessage(m->client, topic, &pubmsg, &m->last_token);
	}
	free(topic);
	if (rc != MQTTCLIENT_SUCCESS) return rc;
	if (timeoutMs) rc = MQTTClient_waitForCompletion(m->client, m->last_token, timeoutMs);
	return rc;
}


int mqtt_pub_strF (mqtt_pubT *m, char *topic, int timeoutMs, int qos, int retained, const char *fmt, ...) {
	int n = 0;
	size_t size = 0;
	char *p = NULL;
	va_list ap;
	int rc;

	// Determine required size

	va_start(ap, fmt);
	n = vsnprintf(p, size, fmt, ap);
	va_end(ap);

	if (n < 0) return -1;

	/* One extra byte for '\0' */

	size = (size_t) n + 1;
	p = malloc(size);
	if (p == NULL) return -1;

	va_start(ap, fmt);
	n = vsnprintf(p, size, fmt, ap);
	va_end(ap);

	if (n < 0) {
		free(p);
		return -1;
	}
	rc = mqtt_pub (m, topic, p, timeoutMs, qos, retained);
	free(p);
	return rc;
}



int mqtt_pub_str (mqtt_pubT *m, char *topic, char *str, int isNumeric, int timeoutMs, int qos, int retained) {
	char *buf;
	int bufSizeOk = 0;
	int len;
	int rc;

	if (m->lastBufSize == 0) m->lastBufSize = 128;
	while (bufSizeOk == 0) {
		buf = malloc(m->lastBufSize);
		if (buf == NULL) return -1;
		len = snprintf(buf,m->lastBufSize,"{\"value\": %s%s%s}",isNumeric == 0 ? "" : "\"", str, isNumeric == 0 ? "" : "\"");
		if (len < m->lastBufSize) {
			bufSizeOk++;
		} else {
			free(buf);
			m->lastBufSize += 128;
		}
	}
	rc = mqtt_pub(m,topic,buf,timeoutMs,qos, retained);
	free(buf);
	return rc;
}

int mqtt_pub_int (mqtt_pubT *m, char *topic, int value, int timeoutMs, int qos, int retained) {
	char buf[128];
	snprintf(buf,128,"%d",value);
	return mqtt_pub_str(m,topic,buf,1,timeoutMs,qos, retained);
}

int mqtt_pub_float (mqtt_pubT *m, char *topic, float value, int decimals, int timeoutMs, int qos, int retained) {
	char buf[128];
	char format[10];
	snprintf(format,10,"%%.%df",decimals);

	snprintf(buf,128,format,value);
	return mqtt_pub_str(m,topic,buf,1,timeoutMs,qos, retained);
}
