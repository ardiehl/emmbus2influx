/*
 * AD 11 Dec 2021:
 *   added influxdb v2 support
 *   fixed connect to ipv6 target
 * AD 4 Oct 2022:
 *   try to connect to all addresses on fails
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

#define INFLUX_TIMEOUT_SECONDS 5
#define INFLUX_DEQUEUE_AT_ONCE 10

#include "influxdb-post.h"


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

int _format_line(char** buf, va_list ap);
int send_udp_line(influx_client_t* c, char *line, int len);
int _format_line2(char** buf, va_list ap, size_t *_len, size_t used);
int _escaped_append(char** dest, size_t* len, size_t* used, const char* src, const char* escape_seq);

influx_client_t* influxdb_post_init (char* host, int port, char* db, char* user, char* pwd, char * org, char *bucket, char *token, int numQueueEntries) {
    influx_client_t* i;

    i=malloc(sizeof(influx_client_t));
    if(! i) return i;

    memset((void*)i, 0, sizeof(influx_client_t));
    i->host=host;
    if(port==0) i->port=8086; else i->port=port;
    i->db=db;
    i->usr=user;
    i->pwd=pwd;
    i->org=org;
    i->bucket=bucket;
    i->token=token;
    i->maxNumEntriesToQueue=numQueueEntries;
    return i;
}

void influxdb_post_deInit(influx_client_t *c) {
     while (influxdb_deQueue(c) > 0) {};
     if(c->ainfo) {
        freeaddrinfo(c->ainfo);
        c->ainfo=NULL;
     }
     c->hostResolved=0;
}


void influxdb_post_free(influx_client_t *c) {
	if (c) {
		influxdb_post_deInit(c);
		free(c->host);
		free(c->db);
		free(c->usr);
		free(c->pwd);
		free(c->org);
		free(c->bucket);
		free(c->token);
		free(c);
	}
}


int influxdb_send_udp(influx_client_t* c, ...)
{
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

int influxdb_format_line(char **buf, int *len, size_t used, ...)
{
    va_list ap;
    va_start(ap, used);

    used = _format_line2(buf, ap, (size_t *)len, used);
    va_end(ap);
    if(*len < 0)
        return -1;
    else
	return used;
}

int lastNeededBufferSize = 0x100;

int _begin_line(char **buf)
{
    int len = lastNeededBufferSize; //1500;  // ad: increased because format_line2 is buggy when extending the buffer
    if(!(*buf = (char*)malloc(len)))
        return -1;
    return len;
}

int _format_line(char** buf, va_list ap)
{
	size_t len = 0;
	*buf = NULL;
	return _format_line2(buf, ap, &len, 0);
}


int appendToBuf (char **buf, size_t *used, size_t *bufLen, char *src) {
    int srcLen = strlen(src);
    if (*used+srcLen+1 > *bufLen) {
        *bufLen = (*bufLen * 2) + srcLen;
        *buf = (char*)realloc(*buf, *bufLen);
        if (*buf == NULL) {
            LOG(0,"failed to expand buffer to %zu (used: %zu, slen: %d)\n",*bufLen,*used,srcLen);
            return -1;
        }
        if (*bufLen > lastNeededBufferSize) lastNeededBufferSize = *bufLen;
        //printf("appendToBuf, after realloc\n>'%s'<\n",*buf);
    }

    strcpy(*buf + *used,src);
    //printf("buf: %s\n", *buf);
    *used+=srcLen;
    return srcLen;
}

#define MAX_FIELD_LENGTH 255

int last_type;

int _format_line2(char** buf, va_list ap, size_t *_len, size_t used)
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

#define _APPEND(fmter...) \
        do { \
        if (snprintf(&tempStr[0],MAX_FIELD_LENGTH, ##fmter) >= MAX_FIELD_LENGTH) \
            goto FAIL; \
        if (appendToBuf (buf, &used, &len, &tempStr[0]) < 0) \
            goto FAIL; \
        } while(0)


    size_t len = *_len;
    int type = 0;
    uint64_t i = 0;
    double d = 0.0;
    char tempStr[MAX_FIELD_LENGTH+1];

    if (*buf == NULL) {
	    len = _begin_line(buf);
	    used = 0;
	    last_type = 0;
//	} else {
//		last_type = IF_TYPE_FIELD_BOOLEAN; // ad 2.12.2021
	}

    type = va_arg(ap, int);
    while(type != IF_TYPE_ARG_END) {
        if(type >= IF_TYPE_TAG && type <= IF_TYPE_FIELD_BOOLEAN) {
            if(last_type < IF_TYPE_MEAS || last_type > (type == IF_TYPE_TAG ? IF_TYPE_TAG : IF_TYPE_FIELD_BOOLEAN))
                goto FAIL;
            _APPEND("%c", (last_type <= IF_TYPE_TAG && type > IF_TYPE_TAG) ? ' ' : ',');
            if(_escaped_append(buf, &len, &used, va_arg(ap, char*), ",= "))
                return -2;
            _APPEND("=");
        }
        switch(type) {
            case IF_TYPE_MEAS:
                if(last_type)
					_APPEND("\n");
                if(last_type && last_type <= IF_TYPE_TAG)
                    goto FAIL;
                if(_escaped_append(buf, &len, &used, va_arg(ap, char*), ", "))
                    return -3;
                break;
            case IF_TYPE_TAG:
                if(_escaped_append(buf, &len, &used, va_arg(ap, char*), ",= "))
                    return -4;
                break;
            case IF_TYPE_FIELD_STRING:
                _APPEND("\"");
                if(_escaped_append(buf, &len, &used, va_arg(ap, char*), "\""))
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
                if(last_type < IF_TYPE_FIELD_STRING || last_type > IF_TYPE_FIELD_BOOLEAN)
                    goto FAIL;
                i = va_arg(ap, long long);
                _APPEND(" %" PRId64, i);
                break;
            case IF_TYPE_TIMESTAMP_NOW:
                if(last_type < IF_TYPE_FIELD_STRING || last_type > IF_TYPE_FIELD_BOOLEAN)
                    goto FAIL;
                i = influxdb_getTimestamp();
                //LOGN(0,"xxxxx %" PRId64,i);
                _APPEND(" %" PRId64, i);
                break;
            default:
                goto FAIL;
        }
        last_type = type;
        type = va_arg(ap, int);
    }
    //_APPEND("\n");  // AD: removed
#if 0
    if(last_type <= IF_TYPE_TAG)
        goto FAIL;
#endif
    *_len = len;
    return used;
FAIL:
	free(*buf);
	*buf = NULL;
    return -1;
}
#undef _APPEND

int _escaped_append(char** dest, size_t* len, size_t* used, const char* src, const char* escape_seq)
{
    size_t i = 0;

    for(;;) {
        if((i = strcspn(src, escape_seq)) > 0) {
            if(*used + i > *len && !(*dest = (char*)realloc(*dest, (*len) *= 2)))
                return -1;
            strncpy(*dest + *used, src, i);
            *used += i;
            src += i;
        }
        if(*src) {
            if(*used + 2 > *len && !(*dest = (char*)realloc(*dest, (*len) *= 2)))
                return -2;
            (*dest)[(*used)++] = '\\';
            (*dest)[(*used)++] = *src++;
        }
        else
            return 0;
    }
    return 0;
}


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


int post_http_send_line(influx_client_t *c, char *buf, int len)
{
    int sock=0, ret_code = 0;
    struct iovec iv[2];
    int charsReceived = 0;
    struct addrinfo *ai;
    int bytesExpected, bytesWritten;

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
    LOG(2,"httpHeader initialized:\n---------------%s\n---------------\n",(char *)iv[0].iov_base);

    iv[0].iov_len = strlen((char *)iv[0].iov_base);

	LOG(5, "influxdb-c::post_http: iv[1] = '%s'\n", (char *)iv[1].iov_base);


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
		LOG(1,"connect to %s:%d failed (%d - %s)\n",c->host,c->port,errno,strerror(errno));
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
    if (recLen <= 0) { LOG(0,"post_http_send_line: recv returned %d, errno=%d\n",recLen,errno); ret_code = -8; goto END; }

    /*  RFC 2616 defines the Status-Line syntax as shown below:

        Status-Line = HTTP-Version SP Status-Code SP Reason-Phrase CRLF */
    char *p = &recvBuf[0];
    if (strncmp(recvBuf,"HTTP/",5) != 0) { LOG(0,"post_http_send_line: response not starting with HTTP/ (%s)\n",recvBuf); ret_code = -20; goto END; }

    while ((*p != '\0') && (*p != '\n') && (*p != ' ')) { p++; if (*p =='\0') { LOG(0,"post_http_send_line: end of string while searching for start of resonse code in '%s'\n",recvBuf); ret_code = -8; goto END; } }

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


int addToQueue (influx_client_t* c, char * line) {
    struct influx_dataRow_t *t;
    struct influx_dataRow_t *t2;

    if (c->numEntriesQueued < c->maxNumEntriesToQueue) {
        t = malloc(sizeof(*t));
        if (! t) return -1;
        t->next=NULL;
        t->postData=line;
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
            res = post_http_send_line(c, c->firstEntry->postData, strlen(c->firstEntry->postData));
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
    char *line = NULL;
    int ret_code = 0, len = 0;

    va_start(ap, c);
    len = _format_line((char**)&line, ap);
    va_end(ap);
    if(len < 0)
        return -1;

    ret_code = post_http_send_line(c, line, len);
    if (ret_code != 0) {
        if (addToQueue(c,line)<0) {
            free(line);
        }
    } else {
        free(line);
        influxdb_deQueue(c);
    }
    return ret_code;
}

// line will be free'd or added to queue if influxdb server is unavailable
int influxdb_post_http_line(influx_client_t* c, char * line)
{
    int ret_code = 0, len = strlen(line);

    ret_code = post_http_send_line(c, line, len);
    //printf("rc from post_http_send_line: %d\n",ret_code);
    if (ret_code != 0) {
        if (addToQueue(c,line)<0) {
            free(line);     // queue fill, must ignore this one
        }
    } else {
        free(line);
        influxdb_deQueue(c);
    }
    return ret_code;
}


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


uint64_t influxdb_getTimestamp()  {
int res;
struct timespec tp;

    res = clock_gettime(CLOCK_REALTIME, &tp);
    if (res != 0) { LOGN(0, "clock_gettime failed"); return 0; }
    //printf("sec:%ld nsec:%ld\n",tp.tv_sec,tp.tv_nsec);
    return (int64_t)(tp.tv_sec) * (int64_t)1000000000 + (int64_t)(tp.tv_nsec);
}
