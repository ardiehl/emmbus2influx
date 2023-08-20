#ifndef METERDEF_H_INCLUDED
#define METERDEF_H_INCLUDED

#include <mbus.h>
#include "parser.h"

#define MUPARSER_ALLOWED_CHARS "0123456789_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ."


// !! keep in sync with parserInit

#define T_TYPEFIRST   300
#define TK_FLOAT      300
#define TK_FLOAT_ABCD 301
#define TK_FLOAT_BADC 302
#define TK_FLOAT_CDAB 303
#define TK_INT16      304
#define TK_INT32      305
#define TK_INT48      306
#define TK_INT64      307
#define TK_UINT16     308
#define TK_UINT32     309
#define TK_UINT48     310
#define TK_UINT64     311

// LSW first
#define TK_INT32L     312
#define TK_INT48L     313
#define TK_INT64L     314
#define TK_UINT32L    315
#define TK_UINT48L    316
#define TK_UINT64L    317

#define T_TYPELAST    317
#define TK_UNKNOWN    0xffffff

// integer arg
#define TK_REGOPTIFIRST    500
#define TK_DEC             500
#define TK_DIV             501
#define TK_MUL             502
#define TK_MQTT            504
#define TK_INFLUX          505
#define TK_REGOPTILAST     549


// string arg
#define TK_REGOPTSFIRST 550
#define TK_ARRAY        550
#define TK_FORMULA      551
#define TK_MQTTPREFIX   552

#define TK_REGOPTSLAST  599


#define TK_NAME            600
//#define TK_READ            601
#define TK_TYPE            602
#define TK_ADDRESS         603
#define TK_HOSTNAME        604
#define TK_FORCE           605
#define TK_MEASUREMENT     606
#define TK_PORT            607
//#define TK_SUNSPEC         608
#define TK_ID              609

#define TK_METERS          614
#define TK_DISABLE         615
#define TK_MQTTQOS		   616
#define TK_MQTTRETAIN      617
#define TK_INFLUXWRITEMULT 618
#define TK_IMAX            619
#define TK_IMIN            620
#define TK_IAVG            621
#define TK_MODBUSDEBUG     622
#define TK_DEFAULT         625
#define TK_SCHEDULE        626
#define TK_INAME           627

#define CHAR_TOKENS ",;()={}+-*/&%$"

#define TK_COMMA      1
#define TK_SEMICOLON  2
#define TK_BR_OPEN    3
#define TK_BR_CLOSE   4
#define TK_EQUAL      5


#define TARIF_MAX     4



typedef enum  {force_none = 0, force_int, force_float} typeForce_t;

// used when influxdb data will be written every x queries
typedef enum  {pr_last = 0, pr_max, pr_min, pr_avg} influxMultProcessing_t;

typedef struct meterRegister_t meterRegister_t;
struct meterRegister_t {
	char *name;
	int isFormulaOnly;  // 1 when no modbus read will be performed
	char *formula;      // either for modifying a value read via modbus or for calculating a "virtual" register based on values of other registers
	int recordNumber;
	int type;
	int divider;
	int multiplier;
	meterRegister_t *next;
	typeForce_t forceType;
	char * arrayName;
	int decimals;
	int enableInfluxWrite;
	int enableMqttWrite;
	influxMultProcessing_t influxMultProcessing;
};



typedef struct meterType_t meterType_t;
struct meterType_t {
	char *name;
	int isFormulaOnly;  // 1 when no modbus read will be performed
	meterRegister_t *meterRegisters;
	int numEnabledRegisters_mqtt;
	int numEnabledRegisters_influx;
	int id;

	int mqttQOS;
	int mqttRetain;
	meterType_t *next;
	char *mqttprefix;
	char * influxMeasurement;
	int influxWriteMult;
};


extern meterType_t *meterTypes;


typedef struct meterRegisterRead_t meterRegisterRead_t;
struct meterRegisterRead_t {
	meterRegister_t *registerDef;
	double fvalueInflux;
	double fvalue;       // now always float, will be saved as int to influx/mqtt if defined as integer
	double fvalueInfluxLast;
	//int64_t ivalue;
	int isInt;
	int hasBeenRead;
	meterRegisterRead_t *next;
};


typedef struct meterFormula_t meterFormula_t;
struct meterFormula_t {
    char * name;
    char * formula;
    typeForce_t forceType;
	char * arrayName;
	int decimals;
	int enableInfluxWrite;
	int enableMqttWrite;
    double fvalue;
    double fvalueInflux;
    double fvalueInfluxLast;
    influxMultProcessing_t influxMultProcessing;
    meterFormula_t *next;
};


typedef struct meter_t meter_t;
struct meter_t {
    int disabled;
	meterRegisterRead_t *registerRead;
	meterType_t *meterType;
	int isFormulaOnly;
	uint64_t mbusAddress;
	int mbusId;
	char *name;
	char *iname;
	int meterHasBeenRead;
	int hasSchedule;
	int isDue;
	mbus_handle **mb;	// pointer to a pointer to global RTU handle or a global one for a TCP connection (multiple meters may use the same IP connection)
	int isTCP;
	char *hostname;
	char *port;
	char * influxMeasurement;
	char * influxTagName;
	meterFormula_t * meterFormula;
	int numEnabledRegisters_mqtt;
	int numEnabledRegisters_influx;
	int mqttQOS;
	int mqttRetain;
	char *mqttprefix;
	meter_t *next;
	int influxWriteMult;
	int influxWriteCountdown;
	unsigned int queryTimeNano;
	unsigned int numQueries;	// including errs
	unsigned int numErrs;
};


typedef struct meterList_t meterList_t;
struct meterList_t {
    meter_t *meter;
    meterList_t *next;
};

extern meter_t *meters;

int readMeterDefinitions (const char * configFileName);
void freeMeters();

meter_t *findMeter(char *name);

int parserExpectEqual(parser_t * pa, int tkExpected);

#endif // METERDEF_H_INCLUDED
