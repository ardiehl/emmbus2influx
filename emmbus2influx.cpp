/******************************************************************************
emmbus2influx

Read from mbus energy meters
and send the data to influxdb (1.x or 2.x API) and/or via mqtt
******************************************************************************/
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <ctype.h>
#include <errno.h>
#include <termios.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "argparse.h"
#include <math.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <time.h>

#include "log.h"

#include "mqtt_publish.h"

#include "influxdb-post/influxdb-post.h"
#include "meterDef.h"
#include "mbusread.h"
#include "global.h"
#include "parser.h"
#include <mbus.h>
#include "global.h"
#include <endian.h>

#ifndef DISABLE_FORMULAS
#include "muParser.h"
#endif

#include "MQTTClient.h"
#include "cron.h"
#define VER "1.05 Armin Diehl <ad@ardiehl.de> Sep 7,2023, compiled " __DATE__ " " __TIME__
#define ME "emmbus2influx"
#define CONFFILE "emmbus2influx.conf"

#define MQTT_CLIENT_ID ME

#define NUM_RECS_TO_BUFFER_ON_FAILURE 1000

char *configFileName;
char * serDevice;
int serBaudrate = 2400;
int dumpRegisters;
int dryrun;
int queryIntervalSecs = 60 * 60 * 2;	// seconds = 2 hours
char *formulaValMeterName;
int formulaTry;
int scan1;
int scan2;

influx_client_t *iClient;
char *cronExpression;

mqtt_pubT *mClient;

#define MQTT_PREFIX_DEF "ad/house/energy/"


int doTry   = false;

// maximal length of a http line send
#define INFLUX_BUFLEN 2048


#define INFLUX_DEFAULT_MEASUREMENT "energyMeter"
#define INFLUX_DEFAULT_TAGNAME "Meter"
char * influxMeasurement;
char * influxTagName;
int iVerifyPeer = 1;
int influxWriteMult;    // write to influx only on x th query (>=2)
int mqttQOS;
int mqttRetain;
char * mqttprefix;

// Grafana Live
char *ghost;
int gport = 3000;
char *gtoken;
char *gpushid;
int gUseInfluxMeasurement;
int gVerifyPeer = 1;
influx_client_t *gClient;


int syslogTestCallback(argParse_handleT *a, char * arg) {
	VPRINTF(0,"%s : sending testtext via syslog\n\n",ME);
	log_setSyslogTarget(ME);
	VPRINTF(0,"testtext via syslog by %s",ME);
	exit(0);
}


int showVersionCallback(argParse_handleT *a, char * arg) {
	MQTTClient_nameValue* MQTTVersionInfo;
	char *MQTTVersion = NULL;
	int i;

	printf("%s %s\n",ME,VER);
	printf("  libmbus: %s\n",mbus_get_current_version());
#ifndef DISABLE_FORMULAS
	//printf("   muparser: %s\n",mu::ParserVersion.c_str());
#else
	//printf("  muparser: disabled at compile time\n");
#endif
	MQTTVersionInfo = MQTTClient_getVersionInfo();
	i = 0;
	while (MQTTVersionInfo[i].name && MQTTVersion == NULL) {
		if (strcasecmp(MQTTVersionInfo[i].name,"Version") == 0) {
			MQTTVersion = (char *)MQTTVersionInfo[i].value;
			break;
		}
		i++;
	}
	if (MQTTVersion)
	printf("paho mqtt-c: %s\n",MQTTVersion);
	exit(2);
}

// to avoid unused error message for --configfile
int dummyCallback(argParse_handleT *a, char * arg) {
	return 0;
}

#define CONFFILEARG "--configfile="

int parseArgs (int argc, char **argv) {
	int res = 0;
	int i;
	char * dbName = NULL;
	char * serverName = NULL;
	char * userName = NULL;
	char * password = NULL;
	char * bucket = NULL;
	char * org = NULL;
	char * token = NULL;
	int syslog = 0;
	int port = 8086;
	int numQueueEntries = NUM_RECS_TO_BUFFER_ON_FAILURE;
	int influxapi=1;
	argParse_handleT *a;

	influxMeasurement = strdup(INFLUX_DEFAULT_MEASUREMENT);
	influxTagName = strdup(INFLUX_DEFAULT_TAGNAME);



	AP_START(argopt)
		AP_HELP
		AP_OPT_STRVAL_CB    (0,0,"configfile"    ,NULL                   ,"config file name",&dummyCallback)
		AP_OPT_STRVAL       (0,'d',"device"      ,&serDevice             ,"specify serial device name")
		AP_OPT_INTVAL       (1,0 ,"baud"         ,&serBaudrate           ,"baudrate")

		//AP_REQ_STRVAL_CB    (1,'a',"tags"        ,NULL                  ,"specify influxdb tags for each meter separated by ,", &parseTagCallback)
		AP_OPT_STRVAL       (1,'m',"measurement"    ,&influxMeasurement    ,"Influxdb measurement")
		AP_OPT_STRVAL       (1,'g',"tagname"        ,&influxTagName        ,"Influxdb tag name")
		AP_OPT_STRVAL       (1,'s',"server"         ,&serverName           ,"influxdb server name or ip")
		AP_OPT_INTVAL       (1,'o',"port"           ,&port                 ,"influxdb port")
		AP_OPT_STRVAL       (1,'b',"db"             ,&dbName               ,"Influxdb v1 database name")
		AP_OPT_STRVAL       (1,'u',"user"           ,&userName             ,"Influxdb v1 user name")
		AP_OPT_STRVAL       (1,'p',"password"       ,&password             ,"Influxdb v1 password")
		AP_OPT_STRVAL       (1,'B',"bucket"         ,&bucket               ,"Influxdb v2 bucket")
		AP_OPT_STRVAL       (1,'O',"org"            ,&org                  ,"Influxdb v2 org")
		AP_OPT_STRVAL       (0,'T',"token"          ,&token                ,"Influxdb v2 auth api token")
		AP_OPT_INTVAL       (0, 0 ,"influxwritemult",&influxWriteMult      ,"Influx write multiplicator")
		AP_OPT_INTVAL       (1,0  ,"isslverifypeer" ,&iVerifyPeer          ,"Influx SSL certificate verification (0=off)")
		AP_OPT_INTVAL       (1,'c',"cache"          ,&numQueueEntries      ,"#entries for influxdb cache")
		AP_OPT_STRVAL       (1,'M',"mqttserver"     ,&mClient->hostname    ,"mqtt server name or ip")
		AP_OPT_STRVAL       (1,'C',"mqttprefix"     ,&mqttprefix           ,"prefix for mqtt publish")
		AP_OPT_INTVAL       (1,'R',"mqttport"       ,&mClient->port        ,"ip port for mqtt server")
		AP_OPT_INTVAL       (1,'Q',"mqttqos"        ,&mqttQOS              ,"default mqtt QOS, can be changed for meter")
		AP_OPT_INTVAL       (1,'r',"mqttretain"     ,&mqttRetain           ,"default mqtt retain, can be changed for meter")

		AP_OPT_STRVAL       (1,0  ,"ghost"          ,&ghost                ,"grafana server url w/o port, e.g. ws://localost or https://localhost")
		AP_OPT_INTVAL       (1,0  ,"gport"          ,&gport                ,"grafana port")
		AP_OPT_STRVAL       (1,0  ,"gtoken"         ,&gtoken               ,"authorisation api token for Grafana")
		AP_OPT_STRVAL       (1,0  ,"gpushid"        ,&gpushid              ,"push id for Grafana")
		AP_OPT_INTVAL       (1,0  ,"ginfluxmeas"    ,&gUseInfluxMeasurement,"use influx measurement names for grafana as well")
		AP_OPT_INTVAL       (1,0  ,"gsslverifypeer" ,&gVerifyPeer          ,"grafana SSL certificate verification (0=off)")

		AP_OPT_INTVALFO     (0,'v',"verbose"        ,&log_verbosity        ,"increase or set verbose level")
		AP_OPT_INTVAL       (0,'P',"poll"           ,&queryIntervalSecs    ,"poll intervall in seconds")
		AP_OPT_STRVAL       (0, 'H',"cron"          ,&cronExpression       ,"Crontab style expression like Sec Min Hour Day Mon Wday")
		AP_OPT_INTVALF      (0,'y',"syslog"         ,&syslog               ,"log to syslog insead of stderr")
		AP_OPT_INTVALF_CB   (0,'Y',"syslogtest"     ,NULL                  ,"send a testtext to syslog and exit",&syslogTestCallback)
		AP_OPT_INTVALF_CB   (0,'e',"version"        ,NULL                  ,"show version and exit",&showVersionCallback)
		AP_OPT_INTVALF      (0,'D',"dumpregisters"  ,&dumpRegisters        ,"Show registers read from all meters and exit, twice to show received data")
		AP_OPT_INTVALFO     (0,'U',"dryrun"         ,&dryrun               ,"Show what would be written to MQQT/Influx for one query and exit")
		AP_OPT_INTVALF      (0,'t',"try"            ,&doTry                ,"try to connect returns 0 on success")
		AP_OPT_STRVAL       (0, 0 ,"formtryt"       ,&formulaValMeterName  ,"interactive try out formula for register values for a given meter name")
		AP_OPT_INTVALF      (0, 0 ,"formtry"        ,&formulaTry           ,"interactive try out formula (global for formulas in meter definition)")
		AP_OPT_INTVALF      (0,'1',"scan"           ,&scan1                ,"scan for serial mbus devices on primary address")
		AP_OPT_INTVALF      (0,'2',"scan2"          ,&scan2                ,"scan for serial mbus devices on secondary address")
	AP_END;

	// check if we have a configfile argument
	int len = strlen(CONFFILEARG);
	for (i=1;i<argc;i++) {
		if (strncmp(CONFFILEARG,argv[i],len) == 0) {
			configFileName = strdup(argv[i]+len);
			int fh = open(configFileName,O_RDONLY);
			if (fh < 0) {
				EPRINTFN("unable to open config file '%s'",configFileName);
				exit(1);
			}
			close(fh);
			LOGN(1,"using configfile \"%s\"",configFileName);
			break;
		}
	}

	if (configFileName == NULL) configFileName = strdup(CONFFILE);

	a = argParse_init(argopt, configFileName, NULL,
		"The cache will be used in case the influxdb server is down. In\n" \
        "that case data will be send when the server is reachable again.\n");
	res = argParse (a, argc, argv, 0);
	if (res != 0) {
		argParse_free (a);
		return res;
	}

	//mClient->topicPrefix = mqttprefix;

	if (doTry==0 && serverName != NULL) {
		if (org || token || bucket) influxapi++;
		//if (serverName == NULL) { EPRINTF("influx server name not specified\n"); exit(1); }
		if (influxapi == 1) {
			if (!dbName) { EPRINTF("influxdb database name not specified\n"); exit(1); }
		} else {
			if (!org) { EPRINTF("influxdb org not specified\n"); exit(1); }
			if (!bucket) { EPRINTF("influxdb bucket not specified\n"); exit(1); }
			if (!token) { EPRINTF("influxdb token not specified\n"); exit(1); }
			if (dbName) EPRINTF("Warning: database name ignored for influxdb v2 api\n");
			if (userName) EPRINTF("Warning: user name ignored for influxdb v2 api\n");
			if (password) EPRINTF("Warning: password ignored for influxdb v2 api\n");
		}
	}

	if (mClient->hostname) {
		if (!mqttprefix) {
			EPRINTF("mqttprefix required\n"); exit(1);
		}
	}

	if (syslog) log_setSyslogTarget(ME);

	if (doTry == 0) {
		if (serverName) {
			LOG(1,"Influx init: serverName: %s, port %d, dbName: %s, userName: %s, password: %s, org: %s, bucket:%s, numQueueEntries %d\n",serverName, port, dbName, userName, password, org, bucket, numQueueEntries);
			iClient = influxdb_post_init (serverName, port, dbName, userName, password, org, bucket, token, numQueueEntries, iVerifyPeer);
		} else {
			free(dbName);
			free(serverName);
			free(userName);
			free(password);
			free(bucket);
			free(org);
			free(token);
		}
	}

	argParse_free (a);

    return 0;
}


int terminated;


void sigterm_handler(int signum) {
	LOGN(0,"sigterm_handler called, terminating");
	signal(SIGINT, NULL);
	terminated++;
}



void sigusr1_handler(int signum) {
	log_verbosity++;
	LOGN(0,"verbose: %d",log_verbosity);
}

void sigusr2_handler(int signum) {
	if (log_verbosity) log_verbosity--;
	LOGN(0,"verbose: %d",log_verbosity);
}


#define INITIAL_BUFFER_LEN 256

void appendToStr (const char *src, char **dest, int *len, int *bufsize) {
	int srclen;

	if (src == NULL) return;
	if (*src == 0) return;

	srclen = strlen(src);
	if (*len + srclen + 1 > *bufsize) {
		*bufsize *= 2;
		//printf("Realloc to %d, len=%d, srclen: %d %x %x %s\n",*bufsize,*len,srclen,dest,*dest,*dest);
		*dest = (char *)realloc(*dest,*bufsize);
		if (*dest == NULL) { EPRINTF("Out of memory in appendToStr"); exit(1); };
	}
	strcat(*dest,src);
	*len += srclen;
}

#define VALBUFLEN 64
void appendValue (int includeName, meterRegisterRead_t *rr, char **dest, int *len, int *bufsize) {
	char valbuf[VALBUFLEN];
	char format[30];
	char *nameBuf;
	int nameBufSize;
	char *p = &valbuf[0];

	if (rr->isInt) {
#ifdef BUILD_64
		snprintf(valbuf,VALBUFLEN,"%d",(int) rr->fvalue);
#else
		snprintf(valbuf,VALBUFLEN,"%ld",(int) rr->fvalue);
#endif
	} else {
		snprintf(format,sizeof(format),"%%%d.%df",10+rr->registerDef->decimals,rr->registerDef->decimals);
		snprintf(valbuf,VALBUFLEN,format,rr->fvalue);
		while (*p && *p<=32) p++;
	}
	if (includeName) {
		nameBufSize = strlen(rr->registerDef->name)+10;
		nameBuf = (char *)malloc(nameBufSize);
		snprintf(nameBuf,nameBufSize,"\"%s\":",rr->registerDef->name);
		appendToStr(nameBuf,dest,len,bufsize);
		free(nameBuf);
	}

	appendToStr(p,dest,len,bufsize);
}


void appendFormulaValue (int includeName, meterFormula_t *mf, char **dest, int *len, int *bufsize) {
	char valbuf[VALBUFLEN];
	char format[30];
	char *nameBuf;
	int nameBufSize;
	char *p = &valbuf[0];


	if (mf->forceType == force_int) {
#ifdef BUILD_64
		snprintf(valbuf,VALBUFLEN,"%d",(int) mf->fvalue);
#else
		snprintf(valbuf,VALBUFLEN,"%ld",(int) mf->fvalue);
#endif
	} else {
		snprintf(format,sizeof(format),"%%%d.%df",10+mf->decimals,mf->decimals);
		snprintf(valbuf,VALBUFLEN,format,mf->fvalue);
		while (*p && *p<=32) p++;
	}
	if (includeName) {
		nameBufSize = strlen(mf->name)+10;
		nameBuf = (char *)malloc(nameBufSize);
		snprintf(nameBuf,nameBufSize,"\"%s\":",mf->name);
		appendToStr(nameBuf,dest,len,bufsize);
		free(nameBuf);
	}

	appendToStr(p,dest,len,bufsize);
}



#define APPEND(SRC) appendToStr(SRC,&buf,&buflen,&bufsize)

int mqttSendData (meter_t * meter,int dryrun) {
	int bufsize = INITIAL_BUFFER_LEN;
	char *buf;
	int buflen = 0;
	int first = 1;
	meterRegisterRead_t *rr = meter->registerRead;
	meterFormula_t *mf = meter->meterFormula;
	char *arrayName;
	char emptyStr = 0;
	int rc = 0;
	int numRegs;

	// check if we have something to write
	if (meter->disabled) return 0;
	if (! meter->numEnabledRegisters_mqtt) return 0;

	buf = (char *)malloc(bufsize);
	if (buf == NULL) return -1;
	*buf=0;

	arrayName = &emptyStr;
	APPEND("{");
	// registers from meter type
	while (rr) {
        if (rr->registerDef->enableMqttWrite) {
			numRegs++;
            if (rr->registerDef->arrayName) {
                if (strcmp(arrayName,rr->registerDef->arrayName) != 0) {		// start new array
                    if (strlen(arrayName)) APPEND("]");						// close previous array
                    arrayName = rr->registerDef->arrayName;
                    if (!first) APPEND(", ");
                    first = 0;
                    APPEND("\""); APPEND(arrayName); APPEND("\":[");			// start new array
                    appendValue(0,rr,&buf,&buflen,&bufsize);
                } else {				// add to array as long as we are in the same array
                    APPEND(", ");
                    appendValue(0,rr,&buf,&buflen,&bufsize);
                }
            } else {		// register not as array
                if (strlen(arrayName)) { APPEND("]"); arrayName = &emptyStr; } // close previous array
                if (!first) APPEND(", ");
                first = 0;
                appendValue(1,rr,&buf,&buflen,&bufsize);
            }
        } else {
			printf("%s: enableMqttWrite=0\n",rr->registerDef->name);
        }
		rr = rr->next;
	}

	// registers from meter specific formulas
	while (mf) {
		if (mf->enableMqttWrite) {
			numRegs++;
            if (mf->arrayName) {
                if (strcmp(arrayName,mf->arrayName) != 0) {					// start new array
                    if (strlen(arrayName)) APPEND("]");						// close previous array
                    arrayName = mf->arrayName;
                    if (!first) APPEND(", ");
                    first = 0;
                    APPEND("\""); APPEND(arrayName); APPEND("\":[");			// start new array
                    appendFormulaValue(0,mf,&buf,&buflen,&bufsize);
                } else {				// add to array as long as we are in the same array
                    APPEND(", ");
                    appendFormulaValue(0,mf,&buf,&buflen,&bufsize);
                }
            } else {		// register not an array
                if (strlen(arrayName)) { APPEND("]"); arrayName = &emptyStr; } // close previous array
                if (!first) APPEND(", ");
                first = 0;
                appendFormulaValue(1,mf,&buf,&buflen,&bufsize);
            }
        }
		mf = mf->next;
	}

	if (strlen(arrayName)) APPEND("]");

	APPEND("}");
	if (dryrun) {
		printf("%s = %s\n",meter->name,buf);
	} else {
		mClient->topicPrefix = meter->mqttprefix;
		rc = mqtt_pub_strF (mClient,meter->name, 0, meter->mqttQOS,meter->mqttRetain, buf);
		if (rc != 0) LOGN(0,"mqtt publish failed with rc: %d",rc);
		mClient->topicPrefix = NULL;
	}

	free(buf);
	return rc;
}



int influxAppendData (influx_client_t* c, meter_t *meter, uint64_t timestamp) {
	meterRegisterRead_t *rr;
	meterFormula_t *mf;
	int regCount = 0;
	int rc;

	// use the global measurement or the one from the meter (if defined)
	char * measurement = influxMeasurement;
	if (meter->influxMeasurement) measurement = meter->influxMeasurement;

	char * tagname = influxTagName;
	if (meter->influxTagName) tagname = meter->influxTagName;

	// check if we have something to write
	if (meter->disabled) {
		VPRINTFN(2,"%s; influx: meter is disabled");
		return 0;
	}
	if (! meter->numEnabledRegisters_influx) {
		VPRINTFN(2,"%s; influx: no enabled registers",meter->name);
		return 0;
	}

	rc = influxdb_format_line(c, INFLUX_MEAS(measurement), INFLUX_TAG(tagname, meter->iname ? meter->iname : meter->name),INFLUX_END);
	if (rc < 0) { EPRINTFN("influxdb_format_line failed, INFLUX_MEAS"); exit(1); }

    rr = meter->registerRead;
	while (rr) {
        if (rr->registerDef->enableInfluxWrite) {
            if (rr->isInt || rr->registerDef->forceType == force_int) {
                rc= influxdb_format_line(c, INFLUX_F_INT(rr->registerDef->name, (int)rr->fvalueInflux), INFLUX_END);
                if (rc < 0) { EPRINTFN("influxdb_format_line failed, INFLUX_F_INT"); exit(1); }
                regCount++;
            } else {
                rc = influxdb_format_line(c, INFLUX_F_FLT(rr->registerDef->name, rr->fvalueInflux, rr->registerDef->decimals), INFLUX_END);
                if (rc < 0) { EPRINTFN("influxdb_format_line failed, INFLUX_F_FLT"); exit(1); }
                regCount++;
            }
        }
        regCount++;
		rr = rr->next;
	}

	// meter specific register formulas
	mf = meter->meterFormula;
	while (mf) {
		if (mf->forceType == force_int) {
			rc = influxdb_format_line(c, INFLUX_F_INT(mf->name, (int)mf->fvalueInflux) ,INFLUX_END);
			if (rc < 0) { EPRINTFN("influxdb_format_line failed, INFLUX_F_INT"); exit(1); }
		} else {
			rc = influxdb_format_line(c, INFLUX_F_FLT(mf->name, mf->fvalueInflux, mf->decimals), INFLUX_END);
			if (rc < 0) { EPRINTFN("influxdb_format_line failed, INFLUX_F_FLT"); exit(1); }
		}
		mf = mf->next;
	}
	rc = influxdb_format_line(c, INFLUX_TS(timestamp), INFLUX_END);
	if (rc < 0) { EPRINTFN("influxdb_format_line failed, INFLUX_TS"); exit(1); }

	if (regCount) LOGN(0,"%s: posted %d lines to influx measurement %s",meter->name,regCount,measurement);
	return regCount;
}



#define CHANNEL_MAX_LEN 140
int grafanaAppendData (influx_client_t* c, meter_t *meter, uint64_t timestamp) {
	meterRegisterRead_t *rr;
	meterFormula_t *mf;
	int regCount = 0;
	int rc;
	char channelName[255];

	// check if we have something to write
	if (meter->disabled) {
		VPRINTFN(2,"%s; grafana: meter is disabled");
		return 0;
	}
	if (! meter->numEnabledRegisters_grafana) {
		VPRINTFN(2,"%s; grafana: no enabled registers",meter->name);
		return 0;
	}
	VPRINTFN(2,"%s: grafanaAppendData",meter->name);

	memset(channelName,0,sizeof(channelName));
	if (gUseInfluxMeasurement) {
		if (meter->influxMeasurement) strncpy(channelName,meter->influxMeasurement,CHANNEL_MAX_LEN);
		else strncpy(channelName,influxMeasurement,CHANNEL_MAX_LEN);
		if (strlen(channelName)) strncat(channelName,"/",CHANNEL_MAX_LEN);
	}
	strncat(channelName,meter->gname?meter->gname:meter->name,CHANNEL_MAX_LEN);

	rc = influxdb_format_line(c, INFLUX_MEAS(channelName), INFLUX_END);
	if (rc < 0) { EPRINTFN("influxdb_format_line failed, INFLUX_MEAS (grafana)"); exit(1); }

    rr = meter->registerRead;
	while (rr) {
        if (rr->registerDef->enableGrafanaWrite) {
            if (rr->isInt || rr->registerDef->forceType == force_int) {
                rc = influxdb_format_line(c, INFLUX_F_INT(rr->registerDef->name, (int)rr->fvalueInflux), INFLUX_END);
                if (rc< 0) { EPRINTFN("influxdb_format_line failed, INFLUX_F_INT"); exit(1); }
            } else {
                rc = influxdb_format_line(c, INFLUX_F_FLT(rr->registerDef->name, rr->fvalueInflux, rr->registerDef->decimals), INFLUX_END);
                if (rc < 0) { EPRINTFN("influxdb_format_line failed, INFLUX_F_FLT"); exit(1); }
            }
        } //else printf("%s %s: disabled for Grafana\n",meter->name,rr->registerDef->name);
        regCount++;
		rr = rr->next;
	}

	// meter specific register formulas
	mf = meter->meterFormula;
	while (mf) {
		if (mf->enableGrafanaWrite) {
			if (mf->forceType == force_int) {
				rc = influxdb_format_line(c, INFLUX_F_INT(mf->name, (int)mf->fvalueInflux) ,INFLUX_END);
				if (rc < 0) { EPRINTFN("influxdb_format_line failed, INFLUX_F_INT"); exit(1); }
				regCount++;
			} else {
				rc = influxdb_format_line(c, INFLUX_F_FLT(mf->name, mf->fvalueInflux, mf->decimals), INFLUX_END);
				if (rc < 0) { EPRINTFN("influxdb_format_line failed, INFLUX_F_FLT"); exit(1); }
				regCount++;
			}
		} //else printf("%s, meter formula field %s disabled for Grafana\n",meter->name,mf->name);
		mf = mf->next;
	}
	rc = influxdb_format_line(c, INFLUX_TS(timestamp), INFLUX_END);
	if (rc < 0) { EPRINTFN("influxdb_format_line failed, INFLUX_TS"); exit(1); }

	if (regCount) meter->numGrafanaWrites++;
	return regCount;
}



time_t currTime() {
#ifdef CRON_USE_LOCAL_TIME
	time_t t = time(NULL);
    struct tm *lt = localtime(&t);
	return mktime(lt);
#else
	return time(NULL);
#endif
}



void traceCallback(enum MQTTCLIENT_TRACE_LEVELS level, char *message) {
	printf(message); printf("\n");
}



//------------------------------------------------------------------------------
// Iterate over all address masks according to the M-Bus probe algorithm.
//------------------------------------------------------------------------------
int mbus_scan_2nd_address_range(mbus_handle * handle, int pos, char *addr_mask)
{
    int i, i_start=0, i_end=0, probe_ret;
    uint64_t mbusAddress;
    char *mask, matching_mask[17];

    if (handle == NULL || addr_mask == NULL)
    {
        EPRINTF("%s: Invalid handle or address mask.\n", __PRETTY_FUNCTION__);
        return -1;
    }

    if (strlen(addr_mask) != 16)
    {
        EPRINTF("%s: Illegal address mask [%s]. Not 16 characters long.\n", __PRETTY_FUNCTION__, addr_mask);
        return -1;
    }

    if (pos < 0 || pos >= 16)
    {
        return 0;
    }

    if ((mask = strdup(addr_mask)) == NULL)
    {
        EPRINTF("%s: Failed to allocate local copy of the address mask.\n", __PRETTY_FUNCTION__);
        return -1;
    }

    if (mask[pos] == 'f' || mask[pos] == 'F')
    {
        // mask[pos] is a wildcard -> enumerate all 0..9 at this position
        i_start = 0;
        i_end   = 9;
    }
    else
    {
        if (pos < 15)
        {
            // mask[pos] is not a wildcard -> don't iterate, recursively check pos+1
            mbus_scan_2nd_address_range(handle, pos+1, mask);
        }
        else
        {
            // .. except if we're at the last pos (==15) and this isn't a wildcard we still need to send the probe
            i_start = (int)(mask[pos] - '0');
            i_end   = (int)(mask[pos] - '0');
        }
    }

    // skip the scanning if we're returning from the (pos < 15) case above
    if (mask[pos] == 'f' || mask[pos] == 'F' || pos == 15)
    {
        for (i = i_start; i <= i_end; i++)
        {
            mask[pos] = '0'+i;

            if (handle->scan_progress)
                handle->scan_progress(handle,mask);

            probe_ret = mbus_probe_secondary_address(handle, mask, matching_mask);

            if (probe_ret == MBUS_PROBE_SINGLE)
            {
                if (!handle->found_event)
                {
                    printf("Found a device on address %s [using address mask %s]", matching_mask, mask);
                    mbusAddress = strtol(matching_mask,NULL,16);
                    mbus_showDeviceInfo(1, handle, mbusAddress);
                    printf("\n");
                }
            }
            else if (probe_ret == MBUS_PROBE_COLLISION)
            {
                // collision, more than one device matching, restrict the search mask further
                mbus_scan_2nd_address_range(handle, pos+1, mask);
            }
            else if (probe_ret == MBUS_PROBE_NOTHING)
            {
                 // nothing... move on to next address mask
            }
            else // MBUS_PROBE_ERROR
            {
                EPRINTF("%s: Failed to probe secondary address [%s].\n", __PRETTY_FUNCTION__, mask);
                return -1;
            }
        }
    }

    free(mask);
    return 0;
}



int main(int argc, char *argv[]) {
	int rc,i;
	meter_t *meter;
	uint64_t influxTimestamp;
	struct timespec timeStart, timeEnd;
	int isFirstQuery = 1;  // takes longer due to init and/or getting sunspec id's
	double queryTime;
	int numMeters;


	//printf("byte_order: %d\n",__BYTE_ORDER);
#if 0
	if (LIBMODBUS_VERSION_MAJOR != libmodbus_version_major || LIBMODBUS_VERSION_MINOR != libmodbus_version_minor /*|| LIBMODBUS_VERSION_MICRO != libmodbus_version_micro */) {
		EPRINTFN("%s: compiled with libmodbus %d.%d.%d but the version loaded is %d.%d.%d, recompile required",argv[0],LIBMODBUS_VERSION_MAJOR,LIBMODBUS_VERSION_MINOR,LIBMODBUS_VERSION_MICRO,libmodbus_version_major,libmodbus_version_minor,libmodbus_version_micro);
		exit(1);
	}
#endif
	mqttprefix = strdup(MQTT_PREFIX_DEF);

	mClient = mqtt_pub_init (NULL, 0, (char *)MQTT_CLIENT_ID, NULL);

	if (parseArgs(argc,argv) != 0) exit(1);

// for valgrind leak check
#if 0
	printf("read definitions\n");
	readMeterDefinitions (CONFFILE);
	printf("free meters\n");
	freeMeters();
	free(mqttprefix);
	if (mClient) mqtt_pub_free(mClient);
	influxdb_post_free(iClient);
	exit(1);
#endif

	if (!cronExpression) {	// if we have poll seconds specified, build a cron expression
		cronExpression = (char *)malloc(30);
		snprintf(cronExpression,30,"*/%d * * * * *",queryIntervalSecs);
	}
	// add default schedule
	cron_add(NULL,cronExpression);

	// open serial port, needed by readMeterDefinitions if we have serial mbus connections
	if (serDevice)	// not needed if we only have TCP connections
		if (mbusSerial_open (serDevice,serBaudrate) != 0) {
			EPRINTF("%s: unable to open serial port at %s\n",argv[0],serDevice);
			exit(3);
		}

	if (scan1) {
		if (! serDevice) {
			EPRINTFN("scanserial: no serial port specified");
			exit(1);
		}
		mbus_handle ** mb = mbusSerial_getmh();

		printf("scanning for mbus serial devices at primary addresses\n");
		for (i=1;i<=MBUS_MAX_PRIMARY_SLAVES;i++) {
			printf("\r%-3d ",i); fflush(stdout);
			mbus_frame reply;
			rc = mbus_ping_address(*mb, &reply, i);

			if (rc == MBUS_RECV_RESULT_INVALID) {
				/* check for more data (collision) */
				mbus_purge_frames(*mb);
				printf("Collision at address %d\n", i);
				continue;
			}

			if (mbus_frame_type(&reply) == MBUS_FRAME_TYPE_ACK) {
				/* check for more data (collision) */
				if (mbus_purge_frames(*mb)) {
					printf("Collision at address %d\n", i);
					continue;
				}
				printf("Found a M-Bus device");
				mbus_showDeviceInfo(0,*mb,i);
				printf("\n");
			}
		}
		exit(0);
	}

	if (scan2) {
		if (! serDevice) {
			EPRINTFN("scanserial: no serial port specified");
			exit(1);
		}
		mbus_handle ** mb = mbusSerial_getmh();
		mbus_scan_2nd_address_range(*mb, 0, (char *)"FFFFFFFFFFFFFFFF");
		exit(0);
	}

	readMeterDefinitions (configFileName);
	cron_setDefault();
	if (verbose) cron_showSchedules();

	if (dumpRegisters) {
		printf("Querying all meters\n" \
			   "===================\n");
		rc = queryMeters(dumpRegisters);
		exit(1);
	}

	if (formulaValMeterName) {
        if (findMeter(formulaValMeterName) == NULL) {
            EPRINTFN("Invalid meter name '%s'",formulaValMeterName);
            exit(1);
        }
	    printf("Querying all meters before formula test\n" \
			   "=======================================\n");
		rc = queryMeters(dumpRegisters);
		testRegCalcFormula(formulaValMeterName);
        exit(1);
	}

	if (formulaTry) {
	    printf("Querying all meters before formula test\n" \
			   "=======================================\n");
		rc = queryMeters(dumpRegisters);
		testRegCalcFormula(NULL);
        exit(1);
	}

	if (sizeof(time_t) <= 4) {
		LOGN(0,"Warning: TimeT is less than 64 bit, this may fail after year 2038, recompile with newer kernel and glibc to avoid this");
	}

	if (!iClient) LOGN(0,"no influxdb host specified, influx sender disabled");

	if (ghost && gtoken && gpushid) {
		gClient = influxdb_post_init_grafana (ghost, gport, gpushid, gtoken, gVerifyPeer);
	} else
		LOGN(0,"no grafana host,token and pushid specified, grafana sender disabled");

	if (!mClient->hostname) {
		mqtt_pub_free(mClient);
		mClient = NULL;
		LOGN(0,"no mqtt host specified, mqtt sender disabled");
		if (!iClient) {
			EPRINTFN("No mqtt host and no influxdb host specified, specify one or both");
			exit(1);
		}
	} else {
		rc = mqtt_pub_connect (mClient);
		if (rc != 0) LOGN(0,"mqtt_pub_connect returned %d, will retry later",rc);
	}

	if (doTry) {
	    if (!serDevice) {
	        EPRINTFN("try: no serial port defined");
            exit(1);
	    }
	    if (testSerialpresent()) {
            VPRINTFN(1,"found meter on %s",serDevice);
            exit(0);
	    }
	    VPRINTFN(1,"no meter found on %s",serDevice);
	    exit(1);
	}

	if (verbose > 2) {
		meter = meters;
		printf("Name                            TCP schedules f mqtt influx\n");
		printf("------------------------------------------------------------\n");
		while(meter) {
			printf("%-30s %4d %9d %1d %4d %6d\n",meter->name,meter->isTCP,meter->hasSchedule,meter->isFormulaOnly,meter->numEnabledRegisters_mqtt,meter->numEnabledRegisters_influx);
			meter = meter->next;
		}
		printf("\n");
	}


	// term handler for ^c and SIGTERM send by systemd
	//signal(SIGKILL, sigterm_handler);
	signal(SIGTERM, sigterm_handler);
	signal(SIGINT, sigterm_handler);

	signal(SIGUSR1, sigusr2_handler);	// used for verbose level inc/dec via kill command
	signal(SIGUSR2, sigusr1_handler);

	LOGN(0,"mainloop started (%s %s)",ME,VER);


	// do an initial query but do not write to influx / mqtt (init / init sunspec)
	clock_gettime(CLOCK_REALTIME,&timeStart);
	rc = queryMeters(verbose);
	clock_gettime(CLOCK_REALTIME,&timeEnd);
	queryTime = (double)(timeEnd.tv_sec + timeEnd.tv_nsec / NANO_PER_SEC)-(double)(timeStart.tv_sec + timeStart.tv_nsec / NANO_PER_SEC);
	if (dryrun || verbose>0)
		printf("Initial query took %4.2f seconds\n",queryTime);
	if (rc <= 0) {
		terminated++;
		EPRINTFN("Initial query: no meters could be queried");
		msleep(5000);
		terminated++;
	}

	int loopCount = 0;
	while (!terminated) {
		mqtt_pub_yield (mClient); // for mqtt ping, seeps for 100ms if no mqqt specified
		if (gClient) influxdb_post_http(gClient);	// for websocket ping
		clock_gettime(CLOCK_REALTIME,&timeStart);
		if (isFirstQuery) rc = 1;
		else rc = cron_queryMeters(verbose);
		if (rc > 0) {
			clock_gettime(CLOCK_REALTIME,&timeEnd);
			loopCount++;
			//if (dryrun) printf("- %d -----------------------------------------------------------------------\n",loopCount);
			queryTime = (double)(timeEnd.tv_sec + timeEnd.tv_nsec / NANO_PER_SEC)-(double)(timeStart.tv_sec + timeStart.tv_nsec / NANO_PER_SEC);
			if (dryrun || verbose>0)
				printf("Query %d took %4.2f seconds\n",loopCount,queryTime);

			if (iClient) {		// influx
				influxdb_post_freeBuffer(iClient);
				influxTimestamp = influxdb_getTimestamp();
				meter = meters;
				while(meter) {
					if(meter->meterHasBeenRead && (meter->influxWriteCountdown == 0)) {
						influxAppendData (iClient, meter, influxTimestamp);
						meter->influxWriteCountdown = meter->influxWriteMult;
					}
					meter = meter->next;
				}
				if (dryrun) {
					if (iClient->influxBuf) {
						printf("\nDryrun: would send to influxdb:\n%s\n",iClient->influxBuf);
						influxdb_post_freeBuffer(iClient);
					}
				} else {
					if (iClient->influxBuf) {
						rc = influxdb_post_http_line(iClient);
						if (rc != 0) {
							LOGN(0,"Error: influxdb_post_http_line failed with rc %d",rc);
						}
					}
				}
			}

			if (gClient) {		// grafana
				influxdb_post_freeBuffer(gClient);
				influxTimestamp = influxdb_getTimestamp();
				numMeters = 0;
				meter = meters;
				while(meter) {
					if(!meter->disabled) {
						grafanaAppendData (gClient, meter, influxTimestamp);
						numMeters++;
					}
					meter = meter->next;
				}
				//grafanaAppendStat (gClient, influxTimestamp);
				if (dryrun) {
					if (gClient->influxBufLen) {
						printf("\nDryrun: would send to grafana:\n%s\n",gClient->influxBuf);
						influxdb_post_freeBuffer(gClient);
					} else printf("nothing to be posted to Grafana\n");
				} else {
					if (gClient->influxBufLen) {
						rc = influxdb_post_http_line(gClient);
						if (rc != 0) {
							EPRINTFN("Error: influxdb_post_http_line to grafana failed with rc %d",rc);
						} else {
							VPRINTFN(1,"%d lines posted to grafana",numMeters);
						}
					} else {
						VPRINTFN(2,"nothing to send to grafana");
					}
				}
			}

			if (mClient) {		// mqtt
				if (dryrun) printf("Dryrun: would send to mqtt:\n");
				meter = meters;
				while(meter) {
					//if (meter->meterHasBeenRead)
					mqttSendData (meter,dryrun);
					//else if (dryrun) printf(" %s: has not been read ",meter->name);
					meter = meter->next;
				}
				if (dryrun) printf("\n");
			}
			if (isFirstQuery) isFirstQuery--;

			if (dryrun) {
				dryrun--;
				if (!dryrun) terminated++;
			}
		}
	}


#ifndef DISABLE_FORMULAS
	freeFormulaParser();
#endif // DISABLE_FORMULAS

	mbusTCP_freeAll();

	if (mClient) mqtt_pub_free(mClient);
	influxdb_post_free(iClient);

	free(configFileName);
	free(mqttprefix);

	free(influxMeasurement);
	free(influxTagName);
	free(serDevice);
	freeMeters();
	free (cronExpression);

	LOGN(0,"terminated");

	return 0;
}
