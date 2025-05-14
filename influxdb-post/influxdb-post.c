/*
 * AD 11 Dec 2021:
 *   added influxdb v2 support
 *   fixed connect to ipv6 target
 * AD 4 Oct 2022:
 *   try to connect to all addresses on fails
 * AD 30.08.2023:
 *   libcurl if not ESP32
 *   Grafana using websockets
 *
 */
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/uio.h>
#include <time.h>
#include "../log.h"
#include <inttypes.h>
#include <assert.h>
#include <time.h>
#include<signal.h>

#define INFLUX_TIMEOUT_SECONDS 5
#define INFLUX_DEQUEUE_AT_ONCE 50

#include "influxdb-post.h"

// TODO: make this a paremeter
#define WEBSOCKETS_PING_SECS 30

/*
  Usage:
    send_udp/post_http(c,
            INFLUX_MEAS("foo"),
            INFLUX_TAG("k", "v"), INFLUX_TAG("k2", "v2"),
            INFLUX_F_STR("s", "string"), INFLUX_F_FLT("f", 28.39, 2),

            INFLUX_MEAS("bar"),
            INFLUX_F_INT("i", 1048576), INFLUX_F_BOL("b", 1),
            INFLUX_TS(1512722735522840439),

            INFLUX_END);

  **NOTICE**: For best performance you should sort tags by key before sending them to the database.
              The sort should match the results from the [Go bytes.Compare function](https://golang.org/pkg/bytes/#Compare).
 */

int _format_line(influx_client_t* c, va_list ap);
int send_udp_line(influx_client_t* c, char *line, int len);
int _format_line2(influx_client_t* c, va_list ap);
int _escaped_append(influx_client_t* c, const char* src, const char* escape_seq);

influx_client_t* influxdb_post_init (char* host, int port, char* db, char* user, char* pwd, char * org, char *bucket, char *token, int numQueueEntries, char *api
#ifdef INFLUXDB_POST_LIBCURL
									, int SSL_VerifyPeer
#endif
) {
    influx_client_t* i;

    i=calloc(1,sizeof(influx_client_t));
    if(! i) return i;

    memset((void*)i, 0, sizeof(influx_client_t));
    if(host) i->host=strdup(host);
    if(port==0) i->port=8086; else i->port=port;
    if (db) i->db=strdup(db);
    if (user) i->usr=strdup(user);
    if(pwd) i->pwd=strdup(pwd);
    if (org) i->org=strdup(org);
    if (bucket) i->bucket=strdup(bucket);
    if (token) i->token=strdup(token);
    if (api) i->apiStr=strdup(api);
    i->maxNumEntriesToQueue=numQueueEntries;
    i->lastNeededBufferSize = INFLUX_INITIAL_BUF_SIZE;
    i->firstConnectionAttempt = 1;
#ifdef INFLUXDB_POST_LIBCURL
	i->ssl_verifypeer = SSL_VerifyPeer;
#endif
    return i;
}

#ifdef INFLUXDB_POST_LIBCURL
influx_client_t* influxdb_post_init_grafana (char* host, int port, char * grafanaPushID, char *token, int SSL_VerifyPeer) {
	if (port == 0) port = 3000;
	if (grafanaPushID == NULL) return NULL;
	if (*grafanaPushID == 0) return NULL;
	influx_client_t* i = influxdb_post_init(host,port,NULL,NULL,NULL,NULL,NULL,token,0,NULL,SSL_VerifyPeer);
	if (i) {
		i->isGrafana++;
		i->grafanaPushID = strdup(grafanaPushID);
	}
	return i;
}
#endif // INFLUXDB_POST_LIBCURL

void influxdb_post_freeBuffer(influx_client_t *c) {
	if (c->influxBuf) {
		free(c->influxBuf);
		c->influxBuf = NULL;
	}
	if (c->influxBufLen > c->lastNeededBufferSize) c->lastNeededBufferSize = c->influxBufLen;
	c->influxBufLen = 0;
	c->influxBufUsed = 0;
}

void influxdb_post_deInit(influx_client_t *c) {
     while (influxdb_deQueue(c) > 0) {};
#ifndef INFLUXDB_POST_LIBCURL
     if(c->ainfo) {
        freeaddrinfo(c->ainfo);
        c->ainfo=NULL;
     }
     c->hostResolved=0;
#endif
}


void influxdb_post_free(influx_client_t *c) {
	if (c) {
		influxdb_post_deInit(c);
		influxdb_post_freeBuffer(c);
		free(c->host);
		free(c->db);
		free(c->usr);
		free(c->pwd);
		free(c->org);
		free(c->bucket);
		free(c->token);
		free(c->grafanaPushID);
#ifdef INFLUXDB_POST_LIBCURL
		free(c->url);
		if (c->ch_headers) curl_slist_free_all(c->ch_headers);
		if (c->ch) curl_easy_cleanup(c->ch);
#endif
		free(c);
	}
}

#if 0
int influxdb_send_udp(influx_client_t* c, ...) {
    int ret = 0, len;
    va_list ap;
    char* line = NULL;

    va_start(ap, c);
    len = _format_line(&line, ap);
    va_end(ap);
    if(len < 0)
        return -1;

    ret = send_udp_line(c, line, len);

    free(line);
    return ret;
}
#endif // 0

int influxdb_format_line(influx_client_t* c, ...) {
    va_list ap;
    va_start(ap, c);

    int used = _format_line2(c, ap);
    va_end(ap);
    if(c->influxBufLen < 0)
        return -1;
    else
	return used;
}



int _begin_line(influx_client_t* c) {
	if (c->influxBuf) {
		free(c->influxBuf);
		c->influxBuf = NULL;
		c->influxBufLen = 0;
		c->influxBufUsed = 0;
	}
    int len = c->lastNeededBufferSize;
	c->influxBuf = malloc(len);
    if(!(c->influxBuf)) return -1;
	c->influxBufLen = len;
	c->influxBufUsed = 0;
	*c->influxBuf = 0;
    return len;
}

int _format_line(influx_client_t* c, va_list ap) {
	return _format_line2(c, ap);
}


int appendToBuf2 (char **buf, size_t *used, size_t *bufLen, char *src) {
    int srcLen = strlen(src);
    if (*used+srcLen+1 > *bufLen) {
        *bufLen = (*bufLen * 2) + srcLen;
        *buf = (char*)realloc(*buf, *bufLen);
        if (*buf == NULL) {
            LOGN(0,"failed to expand buffer to %zu (used: %zu, slen: %d)\n",*bufLen,*used,srcLen);
            return -1;
        }
        //printf("appendToBuf, after realloc\n>'%s'<\n",*buf);
    }

    strcpy(*buf + *used,src);
    //printf("buf: %s\n", *buf);
    *used+=srcLen;
    return srcLen;
}

int appendToBuf (influx_client_t* c, char * src) {
	return appendToBuf2 (&c->influxBuf, &c->influxBufUsed, &c->influxBufLen, src);
}

#define MAX_FIELD_LENGTH 255



int _format_line2(influx_client_t* c, va_list ap)
{
#if 0
#define _APPEND(fmter...) \
    for(;;) {\
        if((written = snprintf(*buf + used, len - used, ##fmter)) < 0)\
            goto FAIL;\
        if(used + written > len && !(*buf = (char*)realloc(*buf, len *= 2)))\
            return -1;\
        else {\
            used += written;\
            break;\
        }\
    }
#endif // 0

#define _APPEND(fmter...) {\
        if (snprintf(&tempStr[0],MAX_FIELD_LENGTH, ##fmter) >= MAX_FIELD_LENGTH) goto FAIL; \
        if (appendToBuf (c, &tempStr[0]) < 0) goto FAIL; \
		}

//    size_t len = *_len;
    int type = 0;
    uint64_t i = 0;
    double d = 0.0;
    char tempStr[MAX_FIELD_LENGTH+1];

    if (c->influxBuf == NULL) {
	    _begin_line(c);
	    //used = 0;
	    c->last_type = 0;
//	} else {
//		last_type = IF_TYPE_FIELD_BOOLEAN; // ad 2.12.2021
	}

    type = va_arg(ap, int);
    while(type != IF_TYPE_ARG_END) {
        if(type >= IF_TYPE_TAG && type <= IF_TYPE_FIELD_BOOLEAN) {
            if(c->last_type < IF_TYPE_MEAS || c->last_type > (type == IF_TYPE_TAG ? IF_TYPE_TAG : IF_TYPE_FIELD_BOOLEAN))
                goto FAIL;
            _APPEND("%c", (c->last_type <= IF_TYPE_TAG && type > IF_TYPE_TAG) ? ' ' : ',');
            if(_escaped_append(c, va_arg(ap, char*), ",= ")) return -2;
            _APPEND("=");
        }
        switch(type) {
            case IF_TYPE_MEAS:
                if(c->last_type) _APPEND("\n");
                if(c->last_type && c->last_type <= IF_TYPE_TAG)
                    goto FAIL;
                if(_escaped_append(c, va_arg(ap, char*), ", "))
                    return -3;
                break;
            case IF_TYPE_TAG:
                if(_escaped_append(c, va_arg(ap, char*), ",= "))
                    return -4;
                break;
            case IF_TYPE_FIELD_STRING:
                _APPEND("\"");
                if(_escaped_append(c, va_arg(ap, char*), "\""))
                    return -5;
                _APPEND("\"");
                break;
            case IF_TYPE_FIELD_FLOAT:
                d = va_arg(ap, double);
                i = va_arg(ap, int);
                _APPEND("%.*lf", (int)i, d);
                break;
            case IF_TYPE_FIELD_INTEGER:
                i = va_arg(ap, long long);
                _APPEND("%" PRId64 "i", i);    //"%lldi", i);
                break;
            case IF_TYPE_FIELD_BOOLEAN:
                i = va_arg(ap, int);
                _APPEND("%c", i ? 't' : 'f');
                break;
            case IF_TYPE_TIMESTAMP:
                if(c->last_type < IF_TYPE_FIELD_STRING || c->last_type > IF_TYPE_FIELD_BOOLEAN)
                    goto FAIL;
                i = va_arg(ap, long long);
                _APPEND(" %" PRId64, i);
                break;
            case IF_TYPE_TIMESTAMP_NOW:
                if(c->last_type < IF_TYPE_FIELD_STRING || c->last_type > IF_TYPE_FIELD_BOOLEAN)
                    goto FAIL;
                i = influxdb_getTimestamp();
                _APPEND(" %" PRId64, i);
                break;
            default:
                goto FAIL;
        }
        c->last_type = type;
        type = va_arg(ap, int);
    }
    //_APPEND("\n");  // AD: removed
#if 0
    if(c->last_type <= IF_TYPE_TAG)
        goto FAIL;
#endif
    //*_len = len;
    return 0;
FAIL:
	influxdb_post_freeBuffer(c);
    return -1;
}
#undef _APPEND

int _escaped_append(influx_client_t * c, const char* src, const char* escape_seq)
{
    size_t i = 0;

    for(;;) {
        if((i = strcspn(src, escape_seq)) > 0) {
            if(c->influxBufUsed + i > c->influxBufLen && !(c->influxBuf = (char*)realloc(c->influxBuf, (c->influxBufLen) *= 2)))
                return -1;
            strncpy(c->influxBuf + c->influxBufUsed, src, i);
            c->influxBufUsed += i;
            src += i;
        }
        if(*src) {
            if(c->influxBufUsed + 2 > c->influxBufLen && !(c->influxBuf = (char*)realloc(c->influxBuf, (c->influxBufLen) *= 2)))
                return -2;
            (c->influxBuf)[(c->influxBufUsed)++] = '\\';
            (c->influxBuf)[(c->influxBufUsed)++] = *src++;
        }
        else
            return 0;
    }
    return 0;
}

#ifndef INFLUXDB_POST_LIBCURL
int resolvHostname (influx_client_t *c) {
    int res;
    char service[30];

    sprintf(&service[0],"%d",c->port);
    if (c->ainfo) {
        freeaddrinfo(c->ainfo);
        c->ainfo=NULL;
    }
    c->hostResolved=0;
    res = getaddrinfo(c->host, (const char *)&service, NULL /*&hints*/, &c->ainfo);
    if (res != 0) {
        LOGN(0,"unable to resolve host %s:%s",c->host,service);
        return -1;
    }
    c->hostResolved=1;
    return 0;
}
#endif // INFLUXDB_POST_LIBCURL


#ifndef INFLUXDB_POST_LIBCURL
int post_http_send_line(influx_client_t *c, char *buf, int len)
{
    int sock=0, ret_code = 0;
    struct iovec iv[2];
    int charsReceived = 0;
    struct addrinfo *ai;
    int bytesExpected, bytesWritten;

    if (len <= 0) return 0;

    if(! c->hostResolved) {
        if (resolvHostname (c) != 0)
            return -4;
    }

    iv[1].iov_base = buf;
    iv[1].iov_len = len;
    iv[0].iov_base = NULL;

    if (c->org) {
		// v2 api
		char *httpHeaderFormat="POST /api/v2/write?org=%s&bucket=%s HTTP/1.1\r\nHost: %s\r\nContent-Length: %zd\r\nAuthorization: Token %s\r\n\r\n";
		int neededHeaderSize = strlen(httpHeaderFormat);
		neededHeaderSize+=strlen(c->org);
		neededHeaderSize+=strlen(c->bucket);
		neededHeaderSize+=strlen(c->token);
		iv[0].iov_base=malloc(neededHeaderSize + 5);  // a litte bit to much due to escape chars, 5=content length
		if (iv[0].iov_base==NULL) return -2;
		sprintf((char *)iv[0].iov_base, httpHeaderFormat,c->org, c->bucket, c->host, iv[1].iov_len, c->token);

    } else {
		char *httpHeaderFormat="POST /write?db=%s%s%s%s%s HTTP/1.1\r\nHost: %s\r\nContent-Length: %zd\r\n\r\n";
		int neededHeaderSize = strlen(httpHeaderFormat);
		if (c->usr) neededHeaderSize+=strlen(c->usr)+3;
		if (c->pwd) neededHeaderSize+=strlen(c->pwd)+3;
		if (! c->db) {
			LOGN(0,"post_http_send_line: no database name specified");
			return -5;
		}
		neededHeaderSize+=strlen(c->db);
		neededHeaderSize+=strlen(c->host);
		iv[0].iov_base=malloc(neededHeaderSize + 5);  // a litte bit to much due to escape chars, 5=content length
		if (iv[0].iov_base==NULL) return -2;
		sprintf((char *)iv[0].iov_base, httpHeaderFormat,
            c->db, c->usr ? "&u=" : "",c->usr ? c->usr : "", c->pwd ? "&p=" : "", c->pwd ? c->pwd : "", c->host, iv[1].iov_len);
	}
    LOGN(2,"httpHeader initialized:\n---------------\n%s\n---------------\n",(char *)iv[0].iov_base);

    iv[0].iov_len = strlen((char *)iv[0].iov_base);

	LOGN(5, "influxdb-c::post_http: iv[1] = '%s'\n", (char *)iv[1].iov_base);


	ai = c->ainfo;
	int connected = 0;

	while (ai && connected==0) {
		if((sock = socket(ai->ai_family, SOCK_STREAM,ai->ai_protocol)) < 0) {
			//LOG(1,"socket to %s:%d failed (%d - %s)\n",c->host,c->port,errno,strerror(errno));
			ai = ai->ai_next;
		} else {
			if (connect(sock, ai->ai_addr, ai->ai_addrlen) < 0) {
				ret_code = -6;
				ai = ai->ai_next;
				close(sock);
			} else {
				connected++;
			}
		}
	}

	if (!connected) {
		c->hostResolved=0;      // make sure we resolv the host name the next time, ip may have been changed
		//free(iv[1].iov_base); iv[1].iov_base = NULL;
		//free(iv[0].iov_base); iv[0].iov_base = NULL;
		LOGN(1,"connect to %s:%d failed (%d - %s)\n",c->host,c->port,errno,strerror(errno));
		goto END;
	}

#ifndef WINDOWS
    // LINUX
    struct timeval tv;
    tv.tv_sec = INFLUX_TIMEOUT_SECONDS;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
#else
// WINDOWS
    DWORD timeout = INFLUX_TIMEOUT_SECONDS * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof timeout);
#endif

    bytesExpected = (int)(iv[0].iov_len + iv[1].iov_len);
    bytesWritten = writev(sock, iv, 2);
    if(bytesWritten < bytesExpected) {
        LOGN(0,"influx writev returned %d, expected at least %d, errno: %d %s",bytesWritten,bytesExpected,errno,strerror(errno));
        ret_code = -7;
        goto END;
    }
    iv[0].iov_len = len;

#define RECV_BUF_LEN 1024
    char recvBuf[RECV_BUF_LEN];
    memset(&recvBuf,0,RECV_BUF_LEN);
    int recLen = recv(sock,&recvBuf,RECV_BUF_LEN,0);
    //LOGN(0,"recLen:%d",recLen);
    if (recLen <= 0) { LOGN(0,"post_http_send_line: recv returned %d, errno=%d\n",recLen,errno); ret_code = -8; goto END; }

    /*  RFC 2616 defines the Status-Line syntax as shown below:

        Status-Line = HTTP-Version SP Status-Code SP Reason-Phrase CRLF */
    char *p = &recvBuf[0];
    if (strncmp(recvBuf,"HTTP/",5) != 0) { LOGN(0,"post_http_send_line: response not starting with HTTP/ (%s)\n",recvBuf); ret_code = -20; goto END; }

    while ((*p != '\0') && (*p != '\n') && (*p != ' ')) { p++; if (*p =='\0') { LOGN(0,"post_http_send_line: end of string while searching for start of resonse code in '%s'\n",recvBuf); ret_code = -8; goto END; } }

    if (*p != ' ') { ret_code = -9; goto END; }
    p++; ret_code = 0;
    // get respnse code
    while ((*p >= '0') && (*p <= '9') ) {
        ret_code = ret_code * 10 + (*p - '0');
        p++;
    }

    if ((ret_code / 100 != 2) && (ret_code != 0)) {
        LOGN(0,"post_http_send_line: received unexpected statusCode %d (%s)\n",ret_code,recvBuf);
        LOGN(0, "influxdb-c::post_http: iv[0] = '%s'\n", (char *)iv[0].iov_base);
        LOGN(0, "influxdb-c::post_http: iv[1] = '%s'\n", (char *)iv[1].iov_base);
    } else
    if (log_verbosity>1) {
        LOGN(1,"post_http_send_line: statusCode: %d, line: %s\n",ret_code,buf);
    } else
		if (ret_code != 204) LOGN(0,"post_http_send_line: statusCode: %d\n",ret_code);

    //goto END;

END:
    close(sock);
    free(iv[0].iov_base);
    //free(iv[1].iov_base);
    if(ret_code < 0) LOGN(1,"post_http_send_line: ret: %d, charsReceived: %d",ret_code,charsReceived);
    return ret_code / 100 == 2 ? 0 : ret_code;
}

#else



int protoPrefixLen[] = {4,5,2,3,0,0};
const char * protocolsStr[] = {"http","https","ws","wss",NULL,NULL};

const char *getTransportProtoStr(transport_proto_t t) {
	if (t < 0 || t > proto_unknown) return NULL;
	return protocolsStr[t];
}

transport_proto_t getTransportProto (const char *url) {
	if (!url) return proto_unknown;
	char *c = strstr((char *)url,"://");
	if (!c) return proto_unknown;
	size_t len = c - url;
	for (int i=0;i<proto_unknown;i++) {
		if(protocolsStr[i])
			if (len == strlen(protocolsStr[i])) {
				if (strncasecmp(url,protocolsStr[i],len) == 0) return (transport_proto_t)i;
			}
	}

	return (proto_none);
}

int transportIsWebsocket (const char *url) {
	transport_proto_t t = getTransportProto (url);
	return ((t == proto_ws) || (t == proto_wss));
}


transport_proto_t changeTransportProto (char **url, transport_proto_t t) {
	int newSize;

	if (t<0 || t>proto_unknown) return proto_unknown;
	if (! protocolsStr[t]) return proto_unknown;

	transport_proto_t oldT = getTransportProto(*url);
	if (t != oldT) {
		int oldSize = strlen(*url);
		newSize = oldSize - protoPrefixLen[oldT] + protoPrefixLen[t] + 1;
		if (oldT == proto_none) newSize += 4;		// ://
		if (oldT == proto_unknown) newSize += 8;	// could be https://
		char * newUrl = (char *)malloc(newSize+1);
		strcpy(newUrl,protocolsStr[t]);
		strcat(newUrl,"://");
		char *c = strstr((char *)*url,"://");
		if (c) c+=3; else c = *url;
		strcat(newUrl,c);
		free(*url);
		*url = newUrl;
	}
	return t;
}


static int curlDebugCallback(CURL *handle, curl_infotype type, char *data, size_t size,void *clientp) {

  (void)handle; /* prevent compiler warning */
  (void)clientp;

  if (verbose <3) return 0;
  switch(type) {
	case CURLINFO_TEXT:
		//fputs("== Info: ", stderr);	fwrite(data, size, 1, stderr);
		EPRINTF("== Info: %s",data);
	default: /* in case a new one is introduced to shock us */
		return 0;
  }
}


int post_http_send_line(influx_client_t *c, char *buf, int len, int showSendErr) {
	int res;
	long response_code;

	assert(c != NULL);

	if (!c->ch) {
		char * strbuf;
		c->ch = curl_easy_init();
		assert(c->ch != NULL);
		if (! c->ssl_verifypeer) {
			res = curl_easy_setopt(c->ch, CURLOPT_SSL_VERIFYPEER, 0);
			if (res) EPRINTFN("curl_easy_setopt(CURLOPT_SSL_VERIFYPEER) for '%s' failed with %d (%s)",c->host,res,curl_easy_strerror(res));
			res = curl_easy_setopt(c->ch, CURLOPT_SSL_VERIFYHOST, 0);
			if (res) EPRINTFN("curl_easy_setopt(CURLOPT_SSL_VERIFYPEER) for '%s' failed with %d (%s)",c->host,res,curl_easy_strerror(res));
		}
		//printf("CURLOPT_SSL_VERIFYPEER, %d, rc: %d\n",c->ssl_verifypeer,res);

		curl_easy_setopt(c->ch, CURLOPT_DEBUGFUNCTION, curlDebugCallback);
		// add http:// if needed
		if (getTransportProto (c->host) == proto_none) changeTransportProto (&c->host, proto_http);

		if (!c->url) {
			if (verbose > 3) curl_easy_setopt(c->ch, CURLOPT_VERBOSE, 1L);

			if (c->isGrafana) {
				// v2 api
				char *urlFormat="%s:%d/api/live/push/%s";
				int urlSize = strlen(urlFormat);
				urlSize+=strlen(c->host);
				urlSize+=strlen(c->grafanaPushID);
				urlSize+=8;
				c->url = malloc(urlSize);
				if (c->url==NULL) return -2;
				//printf("pushid: %s %d\n",c->grafanaPushID,c->port?c->port:3000);
				sprintf(c->url, (char *)urlFormat, c->host, c->port?c->port:3000, c->grafanaPushID);

				char *authFormat = "Authorization: Bearer %s";
				int authSize = strlen(authFormat)-2;
				authSize+=strlen(c->token);
				authSize++;
				strbuf = (char *)malloc(authSize);
				sprintf(strbuf,authFormat,c->token);
				c->ch_headers = curl_slist_append(NULL,strbuf); free(strbuf);
				curl_slist_append(c->ch_headers,"Content-Type: text/plain; charset=utf-8");
				curl_easy_setopt(c->ch, CURLOPT_HTTPHEADER, c->ch_headers);
				if (transportIsWebsocket(c->url)) {
					res = curl_easy_setopt(c->ch, CURLOPT_URL, c->url);
					if (res) {
						EPRINTFN("curl_easy_setopt(CURLOPT_URL,\"%s\") failed with %d (%s)",c->url,res,curl_easy_strerror(res));
						curl_slist_free_all(c->ch);
						curl_easy_cleanup(c->ch); c->ch = NULL;
						return res;
					};
					res = curl_easy_setopt(c->ch, CURLOPT_CONNECT_ONLY, 2);
					if (res) {
						EPRINTFN("curl_easy_setopt(CURLOPT_CONNECT_ONLY) for '%s' failed with %d (%s)",c->url,res,curl_easy_strerror(res));
						curl_slist_free_all(c->ch);
						curl_easy_cleanup(c->ch); c->ch = NULL;
						return res;
					}

					res = curl_easy_perform(c->ch);
					if (res == CURLE_HTTP_RETURNED_ERROR) {
						int res2 = curl_easy_getinfo(c->ch, CURLINFO_RESPONSE_CODE, &response_code);
						if (!res2) res = response_code;
						VPRINTFN(0,"Connecting to grafana at %s failed, rc: %d",c->url,res);
					}
					if (res && c->firstConnectionAttempt) {
						c->firstConnectionAttempt--;
						EPRINTFN("Initial connection to grafana at '%s' failed with %d (%s), trying http instead",c->url,res,curl_easy_strerror(res));
						if (getTransportProto (c->url) == proto_wss)
							changeTransportProto (&c->host, proto_https);
						else changeTransportProto (&c->host, proto_http);
						free(c->url); c->url = NULL;
						curl_slist_free_all(c->ch);
						curl_easy_cleanup(c->ch); c->ch = NULL;
						return post_http_send_line(c,buf,len,showSendErr);
					} else {
						c->isWebsocket = 1;
						c->firstConnectionAttempt = 0;
						if (res) {
							EPRINTFN("curl_easy_perform to '%s' returned %d (%s)",c->url,res,curl_easy_strerror(res));
							return res;
						}
					}
					VPRINTFN(0,"Connected to grafana at %s",c->url);
				}
			}

			if (!c->isGrafana && c->apiStr) {
				char *urlFormat="%s:%d%s";
				int urlSize = strlen(urlFormat);
				urlSize+=strlen(c->host);
				urlSize+=strlen(c->apiStr);
				urlSize+=8;
				c->url = malloc(urlSize);
				if (c->url==NULL) return -2;
				sprintf(c->url, (char *)urlFormat, c->host, c->port?c->port:8086, c->apiStr);
				c->ch_headers = curl_slist_append(NULL,"Content-Type: text/plain; charset=utf-8");
				curl_easy_setopt(c->ch, CURLOPT_HTTPHEADER, c->ch_headers);
				PRINTFN("Using influxdb writer at %s",c->url);

			} else
			if (!c->isGrafana && c->org) {
				// v2 api
				char *urlFormat="%s/api/v2/write?org=%s&bucket=%s";
				int urlSize = strlen(urlFormat);
				urlSize+=strlen(c->host);
				urlSize+=strlen(c->org);
				urlSize+=strlen(c->bucket);
				c->url = malloc(urlSize);
				if (c->url==NULL) return -2;
				sprintf((char *)c->url, urlFormat,c->host,c->org, c->bucket);

				char *authFormat = "Authorization: Token %s";
				int authSize = strlen(authFormat)-2;
				authSize+=strlen(c->token);
				authSize++;
				strbuf = (char *)malloc(authSize);
				sprintf(strbuf,authFormat,c->token);
				c->ch_headers = curl_slist_append(NULL,strbuf); free(strbuf);
				curl_slist_append(c->ch_headers,"Content-Type: text/plain; charset=utf-8");
				//curl_slist_append(c->ch_headers,"Accept: application/json");
				curl_easy_setopt(c->ch, CURLOPT_HTTPHEADER, c->ch_headers);
				PRINTFN("Using influxdb2 at %s",c->url);
			} else
			if (!c->isGrafana) {
				char *urlFormat="%s/write?db=%s%s%s%s%s";
				int urlSize = strlen(urlFormat);
				urlSize+=strlen(c->host);
				if (c->usr) urlSize+=strlen(c->usr)+3;
				if (c->pwd) urlSize+=strlen(c->pwd)+3;
				if (! c->db) {
					LOGN(0,"influxdb1: no database name specified");
					return -5;
				}
				urlSize+=strlen(c->db);
				urlSize+=strlen(c->host);
				c->url=(char *)malloc(urlSize);
				if (c->url==NULL) return -2;
				sprintf(c->url, urlFormat,
					c->host, c->db, c->usr ? "&u=" : "",c->usr ? c->usr : "", c->pwd ? "&p=" : "", c->pwd ? c->pwd : "");
				PRINTF("Using influxdb1 at %s",c->url);
			}
		}
		curl_easy_setopt(c->ch, CURLOPT_URL, c->url);
		curl_easy_setopt(c->ch, CURLOPT_PORT, c->port);
	}

	if (c->isWebsocket) {
		size_t sent = 0;
		size_t rlen;
		const struct curl_ws_frame *meta;

		// this is to answer ping handled by libcurl
		curl_ws_recv(c->ch, NULL, 0, &rlen, &meta);
		//if (res) LOGN(8,"curl_ws_recv 1 (CURLWS_PING) to \"%s\" failed with %d (%s), closing connection",c->url,res,curl_easy_strerror(res));

		if (!len) return 0;		// only to answer ping from server

		while (len) {
			curl_easy_setopt(c->ch,CURLOPT_VERBOSE, 1);
			res = curl_ws_send(c->ch, buf, len, &sent, 0, CURLWS_TEXT);
			curl_easy_setopt(c->ch,CURLOPT_VERBOSE, 0);
			if (res) {
				if (showSendErr) EPRINTFN("curl_ws_send to \"%s\" failed with %d (%s), closing connection",c->url,res,curl_easy_strerror(res));
				//curl_slist_free_all(c->ch);  // sigsegv sometimes with curl 8.4.0 ??
				//EPRINTFN("curl_slist_free_all: done");
				curl_easy_cleanup(c->ch);
				VPRINTFN(4,"curl_easy_cleanup: done");
				c->ch = NULL;	// reconnect next time
				EPRINTFN("curl_ws_send to \"%s\" closed connection",c->url);
				free(c->url); c->url = NULL;
				//kill(getpid(),SIGINT);	// terminate for debug
				return -1;
			}
			len -= sent;
			buf += sent;
		}
		return 0;
	} else {
		if (len <= 0) return 0;
		/* Set size of the POST data */
		curl_easy_setopt(c->ch, CURLOPT_POSTFIELDSIZE, len);

		//printf("Buf:'%s'\n",buf);
		/* Pass in a pointer of data - libcurl will not copy */
		curl_easy_setopt(c->ch, CURLOPT_POSTFIELDS, buf);

		/* Perform the request, res will get the return code */
		res = curl_easy_perform(c->ch);
		/* Check for errors */
		if(res != CURLE_OK && res != CURLE_HTTP_RETURNED_ERROR) {
			if (showSendErr) EPRINTFN("Posting %s data returned %d (%s)",c->isGrafana?"grafana":"influx",res,curl_easy_strerror(res));
			//curl_slist_free_all(c->ch);	// this will result in a segfault
			curl_easy_cleanup(c->ch);
			c->ch = NULL;	// reconnect next time
			free(c->url); c->url = NULL;
			return res;
		}


		res = curl_easy_getinfo(c->ch, CURLINFO_RESPONSE_CODE, &response_code);
		if(res != CURLE_OK) {
			EPRINTFN("curl_easy_getinfo returned %d (%s)",res,curl_easy_strerror(res));
			//curl_slist_free_all(c->ch);
			curl_easy_cleanup(c->ch);
			c->ch = NULL;	// reconnect next time
			free(c->url); c->url = NULL;
			return res;
		}
		LOGN(4,"Post to influxdb, status: %d",response_code);
		return response_code / 100 == 2 ? 0 : response_code;
	}
}

#endif // INFLUXDB_POST_LIBCURL


int addToQueue (influx_client_t* c) {
    struct influx_dataRow_t *t;
    struct influx_dataRow_t *t2;

    if (c->numEntriesQueued < c->maxNumEntriesToQueue) {
        t = malloc(sizeof(*t));
        if (! t) return -1;
        t->next=NULL;
        t->postData=c->influxBuf;
        c->influxBuf=NULL;
        c->influxBufLen=0;
        c->influxBufUsed=0;
        if (! c->firstEntry) {
            c->firstEntry=t;
        } else {
            t2=c->firstEntry;
            while (t2->next) t2=t2->next;
            t2->next=t;
        }
        if (c->numEntriesQueued==0) {
            LOGN(0,"Beginning queueing of records due to failures posting to influxdb host (max: %d)",c->maxNumEntriesToQueue);
        } else
            LOGN(1,"Due to failure sending data, record has been queued as #%d",c->numEntriesQueued);
        c->numEntriesQueued++;
        return 0;
    } else {
        LOGN((c->maxNumEntriesToQueue>0),"Failed sending data and will not queue (numEntriesQueued(%d) < maxNumEntriesToQueue(%d), record lost", c->numEntriesQueued, c->maxNumEntriesToQueue);
        return -2;
    }
}


int influxdb_deQueue(influx_client_t *c) {
    int numDequeued=0;
    int res;
    struct influx_dataRow_t *t;

    if (c->numEntriesQueued) {
        LOGN(0,"beginning dequeing, %d remaining",c->numEntriesQueued);
        do {
            //if (c->firstEntry) {}
            res = post_http_send_line(c, c->firstEntry->postData, strlen(c->firstEntry->postData),1);
            if (res == 0) {
                t = c->firstEntry;
                c->firstEntry = t->next;
                free(t->postData);
                free(t);
                numDequeued++; c->numEntriesQueued--;
                //LOGN(0,"dequeue first: success, remaining: %d",c->numEntriesQueued);
            } else {
                LOGN(0,"dequeue: post_http_send_line failed with %d",res);
                return -1;
            }
        } while((numDequeued < INFLUX_DEQUEUE_AT_ONCE) && (res == 0) && (c->numEntriesQueued));
        if (numDequeued>0) {
            char s[20];
            if (c->numEntriesQueued) sprintf(s,"%d left",c->numEntriesQueued);
            else strcpy(s,"=all");
            LOGN(0,"%d entr%s (%s) dequeued and successfully posted to influxdb host",numDequeued, numDequeued > 1 ? "ies" : "y", s);
        }
    }
    return numDequeued;
}


int influxdb_post_http(influx_client_t* c, ...)
{
    va_list ap;
    int ret_code = 0, len = 0;

    va_start(ap, c);
    len = _format_line(c, ap);
    va_end(ap);
    if(len < 0) {
		post_http_send_line(c, NULL, 0, 1);	// for ws ping
        return 0;
    }

    ret_code = post_http_send_line(c, c->influxBuf, len,0);	// do not show send errors
    if (ret_code != 0 && ret_code < 400)
	ret_code = post_http_send_line(c, c->influxBuf, len,1);	// show send errors here

    if (ret_code != 0 && ret_code < 400) {
		addToQueue(c);
		c->influxBuf=NULL;
    }
    else {
        influxdb_deQueue(c);
    }
    influxdb_post_freeBuffer(c);
    return ret_code;
}

// line will be free'd or added to queue if influxdb server is unavailable
int influxdb_post_http_line(influx_client_t* c)
{
    int ret_code = 0, len = strlen(c->influxBuf);

    ret_code = post_http_send_line(c, c->influxBuf, len, 0);	// dont show send errors
    if (ret_code != 0 && (ret_code < 200 || ret_code >= 500))
	ret_code = post_http_send_line(c, c->influxBuf, len, 1);	// retry and show send errors
    //printf("rc from post_http_send_line: %d\n",ret_code);
    if (ret_code != 0 && (ret_code < 200 || ret_code >= 500)) {
        if (addToQueue(c)<0) {
            influxdb_post_freeBuffer(c);     // queue full, must ignore this one
        } else {
			c->influxBuf = NULL;			// pointer moved to queue so no free is needed in influxdb_post_freeBuffer
			influxdb_post_freeBuffer(c);
        }
    } else {
        influxdb_post_freeBuffer(c);
        influxdb_deQueue(c);
    }
    return ret_code;
}

#if 0
int send_udp_line(influx_client_t* c, char *line, int len)
{
    int sock = 0, ret = 0;
    struct sockaddr_in addr;

    addr.sin_family = AF_INET;
    addr.sin_port = htons(c->port);
    if((addr.sin_addr.s_addr = inet_addr(c->host)) == INADDR_NONE) {
        ret = -2;
        goto END;
    }

    if((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        ret = -3;
        goto END;
    }

    if(sendto(sock, line, len, 0, (struct sockaddr *)&addr, sizeof(addr)) < len) {
        //fprintf(stderr,"sendto failed\n");
        ret = -4;
    }

END:
    close(sock);
    return ret;
}
#endif // 0

uint64_t influxdb_getTimestamp()  {
int res;
struct timespec tp;

    res = clock_gettime(CLOCK_REALTIME, &tp);
    if (res != 0) { LOGN(0, "clock_gettime failed"); return 0; }
    //printf("sec:%ld nsec:%ld\n",tp.tv_sec,tp.tv_nsec);
    return (int64_t)(tp.tv_sec) * (int64_t)1000000000 + (int64_t)(tp.tv_nsec);
}
