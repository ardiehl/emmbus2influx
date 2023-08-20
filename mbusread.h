#ifndef MODBUSREAD_H_INCLUDED
#define MODBUSREAD_H_INCLUDED


#include "meterDef.h"
#include <stdint.h>
#include "mbus.h"

// number of queries
#define TCP_OPEN_RETRY_DELAY 30

#define NANO_PER_SEC 1000000000.0

int mbus_ping_address(mbus_handle *handle, mbus_frame *reply, int address);

int msleep(long msec);

mbus_handle ** mbusSerial_getmh();
int mbusSerial_open (const char *device, int baud);
void mbusSerial_close();

void mbusTCP_freeAll();

void setMeterFvalueInfluxLast (meter_t *meter);
void setMeterFvalueInflux (meter_t * meter);

void executeMeterFormulas(int verboseMsg, meter_t * meter);
void executeInfluxWriteCalc (int verboseMsg, meter_t *meter);

int queryMeter(int verboseMsg, meter_t *meter);
int queryMeters(int verboseMsg);


/**
  testRTUpresent, try to find the first serial connected meter by reading either the first
  defined register or the sunspec header
 @return 0=not found, 1=found
 */
int testSerialpresent();

meter_t * findMeter(char * name);
void testRegCalcFormula(char * meterName);

#ifndef DISABLE_FORMULAS
void freeFormulaParser();
#endif // DISABLE_FORMULAS

int mbus_showDeviceInfo(int verbose, mbus_handle *mb, uint64_t mbusAddress);

#endif // MODBUSREAD_H_INCLUDED

