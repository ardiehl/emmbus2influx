#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "meterDef.h"
#include "argparse.h"
#include "log.h"
#include "mbusread.h"
#include <endian.h>
#include "cron.h"

extern int mqttQOS;
extern int mqttRetain;
extern char * mqttprefix;
extern int influxWriteMult;    // write to influx only on x th query (>=2)
extern int modbusDebug;


meterType_t *meterTypes = NULL;
meter_t *meters = NULL;

void freeMeters() {
	meterType_t * mt, *mtNext;
	meterRegister_t *mreg, *mregNext;
	meter_t *m, *mNext;
	meterRegisterRead_t *rr, *rrNext;
	meterFormula_t *mf,*mfNext;

	VPRINTFN(5,"free meter types");
	mt = meterTypes;
	while (mt) {
		mtNext = mt->next;

		VPRINTFN(5," free meter types -> meter registers");
		mreg = mt->meterRegisters;
		while (mreg) {
			mregNext = mreg->next;
			free(mreg->arrayName);
			free(mreg->name);
			free(mreg->formula);
			free(mreg);
			mreg = mregNext;
		}
		free(mt->name);
		free(mt->influxMeasurement);
		free(mt->mqttprefix);

		free(mt);
		mt = mtNext;
	}
	meterTypes = NULL;

	VPRINTFN(5,"free meters");
	m = meters;
	while (m) {
		mNext = m->next;

		VPRINTFN(5," free meters -> free register reads");
		rr = m->registerRead;
		while (rr) {
			rrNext = rr->next;
			free(rr);
			rr = rrNext;
		}

		mf = m->meterFormula;
		while (mf) {
			mfNext = mf->next;
			free(mf->arrayName);
			free(mf->formula);
			free(mf->name);
			free(mf);
			mf = mfNext;
		}


		free(m->name);
		free(m->hostname);
		free(m->port);
		free(m->influxMeasurement);
		free(m->influxTagName);
		free(m->mqttprefix);

		free(m);
		m = mNext;
	}
	meters = NULL;
}

typedef struct {
    int t;
    char *s;
    int numRegisters;
} typeinfo_t;


int parserExpectEqual(parser_t * pa, int tkExpected) {
	parserExpect(pa,TK_EQUAL);
	return parserExpect(pa,tkExpected);
}


#define BUF_INITIAL_SIZE 10

void buf16add (uint16_t val,int count,uint16_t **buf,int *numWords,int *bufSize) {
    int i;
    uint16_t *tgt;
    while (*numWords + count > *bufSize) {
        if (*bufSize == 0) {
            *bufSize = BUF_INITIAL_SIZE;
            *numWords = 0;
        } else
            *bufSize = *bufSize * 2;
        *buf = (uint16_t *)realloc(*buf,*bufSize * sizeof(uint16_t));
    }
    tgt = *buf;
    tgt += *numWords;
    for (i=0;i<count;i++) {
        *tgt = val;
        tgt++;
        (*numWords)++;
    }
    tgt = *buf;
    //for (i=0;i<*numWords;i++) { printf("%04x ",*tgt); tgt++; } printf("\n");
}


void checkAllowedChars (parser_t *pa, char *name) {
    char *s = name;
    int len = strlen(name);
    while (*s) {
        if (! strchr(MUPARSER_ALLOWED_CHARS,*s)) {
            pa->col -= len;
            parserError(pa,"invalid char '%c' 0x%02x in '%s'",*s,*s,name);
        }
        s++;
        len--;
    }
}


meter_t *findMeter(char *name) {
    meter_t *meter = meters;

    while (meter) {
        if (strcmp(name,meter->name) == 0)
            if (meter->disabled == 0) return meter;
        meter = meter->next;
    }
    return meter;
}

meterType_t * findMeterType(const char * name) {
    meterType_t *mt = meterTypes;

	while (mt) {
        if (strcmp(mt->name,name) == 0) return mt;
        mt = mt->next;
    }
    return NULL;
}


meterRegisterRead_t *findMeterRegisterRead (meter_t *meter, char * name) {
    meterRegisterRead_t *mrrd = meter->registerRead;

    if (!meter) return NULL;
    while (mrrd) {
        if (strcmp(name,mrrd->registerDef->name) == 0) return mrrd;
        mrrd = mrrd->next;
    }
    return mrrd;
}


int parseMeterType (parser_t * pa) {
	int tk;
	meterType_t *meterType;
	meterRegister_t *meterRegister;
	char errMsg[100];
	int enableInfluxWrite = 1;
	int enableMqttWrite = 1;

	meterType = (meterType_t *)calloc(1,sizeof(meterType_t));
	meterType->mqttprefix = strdup(mqttprefix);
	meterType->influxWriteMult = influxWriteMult;
	parserExpect(pa,TK_EOL);  // after section

	tk = parserGetToken(pa);
	//printf("tk2: %d, %s\n",tk,parserGetTokenTxt(pa,tk));
	while (tk != TK_SECTION && tk != TK_EOF) {
		switch(tk) {
            case TK_INFLUXWRITEMULT:
                parserExpectEqual(pa,TK_INTVAL);
				meterType->influxWriteMult = pa->iVal;
				if (pa->iVal != 0)
                    if (pa->iVal < 2) parserError(pa,"influxwritemult: 0 or >=2 expected");
				break;
			case TK_MEASUREMENT:
				if (meterType->influxMeasurement) parserError(pa,"duplicate measurement");
				parserExpectEqual(pa,TK_STRVAL);
				free(meterType->influxMeasurement);
				meterType->influxMeasurement = strdup(pa->strVal);
				break;
			case TK_MQTTPREFIX:
				parserExpectEqual(pa,TK_STRVAL);
				free(meterType->mqttprefix);
				meterType->mqttprefix=strdup(pa->strVal);
				break;
			case TK_MQTTQOS:
				parserExpectEqual(pa,TK_INTVAL);
				meterType->mqttQOS = pa->iVal;
				break;
			case TK_MQTTRETAIN:
				parserExpectEqual(pa,TK_INTVAL);
				meterType->mqttRetain = pa->iVal;
				break;
			case TK_MQTT:
				parserExpectEqual(pa,TK_INTVAL);
				enableMqttWrite = pa->iVal;
				break;
			case TK_INFLUX:
				parserExpectEqual(pa,TK_INTVAL);
				enableInfluxWrite = pa->iVal;
				break;
			case TK_EOL:
				break;
			case TK_NAME:
				if (meterType->name) parserError(pa,"duplicate meter type name specifications");
				// TODO: Check for multiple
				parserExpectEqual(pa,TK_STRVAL);
				meterType->name = strdup(pa->strVal);
				checkAllowedChars (pa,meterType->name);
				if (findMeterType(meterType->name)) parserError(pa,"multiple definitions for meter type '%s'",meterType->name);
				VPRINTFN(4,"parseMeterType %s",pa->strVal);
				break;
			case TK_STRVAL:
				meterRegister = meterType->meterRegisters;
				while (meterRegister) {
					if (strcmp(meterRegister->name,pa->strVal) == 0) parserError(pa,"duplicate register name %s",pa->strVal);
					meterRegister = meterRegister->next;
				}
				meterRegister = (meterRegister_t *)calloc(1,sizeof(meterRegister_t));
				meterRegister->enableInfluxWrite = enableInfluxWrite;
                meterRegister->enableMqttWrite = enableMqttWrite;
				meterRegister->type = TK_INT16;
				meterRegister->name = strdup(pa->strVal);
				checkAllowedChars (pa,meterRegister->name);
				parserExpect(pa,TK_EQUAL);
				tk = parserGetToken(pa);
				switch (tk) {
                    case TK_INTVAL:
                        meterRegister->recordNumber = pa->iVal;
                        break;
#ifndef DISABLE_FORMULAS
                    case TK_STRVAL:
                        meterRegister->formula = strdup(pa->strVal);
                        meterRegister->isFormulaOnly = 1;
                        break;
#endif
                    default:
#ifndef DISABLE_FORMULAS
                        parserError(pa,"integer (mbus record number) or string for formula expected");
#else
                        parserError(pa,"integer expected");
#endif
				}


				// more optional parameters may follow separated by comma, e.g. arr="bla", div=10
				tk = parserGetToken(pa);

				while (tk == TK_COMMA) {
					tk = parserGetToken(pa);

					if (tk >= T_TYPEFIRST && tk <= T_TYPELAST) {
							//printf("tk: %s\n",parserGetTokenTxt(pa,tk));
						meterRegister->type = tk;
					} else
					if (tk >= TK_REGOPTSFIRST && tk <= TK_REGOPTSLAST) {
						parserExpectEqual(pa,TK_STRVAL);
						switch (tk) {
							case TK_ARRAY:
								meterRegister->arrayName = strdup(pa->strVal);
								break;
                            case TK_FORMULA:
#ifndef DISABLE_FORMULAS
                                if (meterRegister->formula) parserError(pa,"parseMeterType: formula already specified");
                                meterRegister->formula = strdup(pa->strVal);
#else
                                EPRINTFN("Warning: compiled without muparser, formulas will be ignored");
#endif
                                break;
							default:
								parserError(pa,"parseMeterType: opt with str %d (%s) not yet supported",tk,pa->strVal);
						}
					} else
					if (tk >= TK_REGOPTIFIRST && tk <= TK_REGOPTILAST) {
						parserExpectEqual(pa,TK_INTVAL);
						switch (tk) {
							case TK_DEC:
								meterRegister->decimals = pa->iVal;
								//printf("%s %s %d\n",meterType->name,meterRegister->name,meterRegister->decimals);
								break;
							case TK_DIV:
								meterRegister->divider = pa->iVal;
								break;
							case TK_MUL:
								meterRegister->multiplier = pa->iVal;
								break;
                            case TK_MQTT:
                                meterRegister->enableMqttWrite = pa->iVal;
                                break;
                            case TK_INFLUX:
                                meterRegister->enableInfluxWrite = pa->iVal;
                                break;
							default:
								parserError(pa,"parseMeterType: opt with int %d (%s) not yet supported",tk,pa->strVal);
						}
					} else
					if (tk == TK_FORCE) {
						parserExpect(pa,TK_EQUAL);
						tk = parserGetToken(pa);
						if (tk != TK_INT16 && tk != TK_FLOAT) parserError(pa,"identifier (int or float) expected");
						if (tk == TK_INT16) meterRegister->forceType = force_int;
						else meterRegister->forceType = force_float;
					} else {
                        switch (tk) {
                            case TK_IMAX:
                                if (meterRegister->influxMultProcessing != pr_last) parserError(pa,"imax,imin or iavg already specified");
                                meterRegister->influxMultProcessing = pr_max;
                                break;
                            case TK_IMIN:
                                if (meterRegister->influxMultProcessing != pr_last) parserError(pa,"imax,imin or iavg already specified");
                                meterRegister->influxMultProcessing = pr_min;
                                break;
                            case TK_IAVG:
                                if (meterRegister->influxMultProcessing != pr_last) parserError(pa,"imax,imin or iavg already specified");
                                meterRegister->influxMultProcessing = pr_avg;
                                break;
                            default:
                                parserError(pa,"identifier for option expected");
                        }

                    }

					tk = parserExpectOrEOL(pa,TK_COMMA);
					//printf("tknext: %s\n",parserGetTokenTxt(pa,tk));
				}
				// add the meter register to the meter type
				if (meterType->meterRegisters) {
					meterRegister_t * mr = meterType->meterRegisters;
					while (mr->next) mr=mr->next;
					mr->next = meterRegister;
				} else meterType->meterRegisters = meterRegister;

				// update enabled register count
				if (meterRegister->enableInfluxWrite) meterType->numEnabledRegisters_influx++;
				if (meterRegister->enableMqttWrite) meterType->numEnabledRegisters_mqtt++;
				break;

			default:
				snprintf(errMsg,sizeof(errMsg),"unexpected input, expected identifier or string but got %s",parserGetTokenTxt(pa,tk));
				parserError(pa,errMsg);
		}
		//printf("tend: %s\n",parserGetTokenTxt(pa,tk));
		if (tk != TK_EOL) {
			tk = parserGetToken(pa);
			if (tk != TK_EOL) parserError(pa,"EOL or , expected");
		}
		tk = parserGetToken(pa);
	}
	if (!meterType->name) parserError(pa,"meter type definition without name");
	if (!meterType->meterRegisters) parserError(pa,"\"%s\": meter type definition without registers",meterType->name);

	meterType->isFormulaOnly = 1;
        meterType->numEnabledRegisters_influx = 0;
        meterRegister = meterType->meterRegisters;
        while (meterRegister) {
                        if (! meterRegister->isFormulaOnly) meterType->isFormulaOnly = 0;
                        meterType->numEnabledRegisters_influx += meterRegister->enableInfluxWrite;
                        meterRegister = meterRegister->next;
        }

	// store meter type
	if (meterTypes) {
		meterType_t * mt = meterTypes;
		while (mt->next) mt = mt->next;
		mt->next = meterType;
	} else meterTypes = meterType;

	return tk;
}


int parseMeter (parser_t * pa) {
	int tk;
	meter_t *meter;
	char errMsg[255];
	meterFormula_t * meterFormula;  // calculated meter specific registers
	int typeDefined = 0;
	int typeConflict = 0;

	meter = (meter_t *)calloc(1,sizeof(meter_t));
	parserExpect(pa,TK_EOL);  // after section
	meter->influxWriteMult = influxWriteMult;
	meter->mbusAddress = -1;
	meter->mbusId = -1;

	tk = parserGetToken(pa);
	//printf("tk2: %d, %s\n",tk,parserGetTokenTxt(pa,tk));
	while (tk != TK_SECTION && tk != TK_EOF) {
		switch(tk) {
			case TK_SCHEDULE:
				parserExpectEqual(pa,TK_STRVAL);
				cron_meter_add_byName(pa->strVal,meter);
				while (tk == TK_COMMA) {
					tk = parserGetToken(pa);
					parserExpect(pa,TK_STRVAL);
					cron_meter_add_byName(pa->strVal,meter);
					tk = parserGetToken(pa);
				}
				break;
            case TK_PORT:
                parserExpectEqual(pa,TK_STRVAL);
				meter->port=strdup(pa->strVal);
				break;
            case TK_ID:
				parserExpectEqual(pa,TK_INTVAL);
				meter->mbusId = pa->iVal;
				break;
/*
            case TK_MODBUSDEBUG:
                parserExpectEqual(pa,TK_INTVAL);
				meter->modbusDebug = pa->iVal;
				break;
*/
            case TK_INFLUXWRITEMULT:
                //if (! typeDefined) parserError(pa,"has to be defined after type=");
                typeConflict++;
                parserExpectEqual(pa,TK_INTVAL);
				meter->influxWriteMult = pa->iVal;
				if (pa->iVal != 0)
                    if (pa->iVal < 2) parserError(pa,"influxwritemult: 0 or >=2 expected");
				break;
			case TK_MQTTPREFIX:
				//if (! typeDefined) parserError(pa,"has to be defined after type=");
				typeConflict++;
				parserExpectEqual(pa,TK_STRVAL);
				meter->mqttprefix=strdup(pa->strVal);
				break;
			case TK_MQTTQOS:
				//if (! typeDefined) parserError(pa,"has to be defined after type=");
				typeConflict++;
				parserExpectEqual(pa,TK_INTVAL);
				meter->mqttQOS = pa->iVal;
				break;
			case TK_MQTTRETAIN:
				//if (! typeDefined) parserError(pa,"has to be defined after type=");
				typeConflict++;
				parserExpectEqual(pa,TK_INTVAL);
				meter->mqttRetain = pa->iVal;
				break;
			case TK_EOL:
				break;
            case TK_DISABLE:
                parserExpectEqual(pa,TK_INTVAL);
                meter->disabled = pa->iVal;
                break;
			case TK_TYPE:
                if (typeConflict) parserError(pa,"type= needs to be specified before options that could be defined in type as well (e.g. influxwritemult, mqttprefix or measurement");
				if (meter->meterType) parserError(pa,"%s: duplicate meter type",meter->name);
				parserExpectEqual(pa,TK_STRVAL);
				meter->meterType = findMeterType(pa->strVal);
				if (!meter->meterType) parserError(pa,"undefined meter type ('%s')",pa->strVal);
				meter->numEnabledRegisters_mqtt = meter->meterType->numEnabledRegisters_mqtt;
				meter->numEnabledRegisters_influx += meter->meterType->numEnabledRegisters_influx;
				if (meter->meterType->mqttprefix) {
					free(meter->mqttprefix);
					meter->mqttprefix = strdup(meter->meterType->mqttprefix);
				}
				meter->mqttQOS = meter->meterType->mqttQOS;
				meter->mqttRetain = meter->meterType->mqttRetain;
				meter->influxWriteMult = meter->meterType->influxWriteMult;
				if (meter->meterType->influxMeasurement) {
					free(meter->influxMeasurement);
					meter->influxMeasurement = strdup(meter->meterType->influxMeasurement);
				}
				typeDefined++;
				break;
			case TK_NAME:
				if (meter->name) parserError(pa,"%s: duplicate meter name in meter definition",meter->name);
				parserExpectEqual(pa,TK_STRVAL);
				meter->name = strdup(pa->strVal);
				if (findMeter(meter->name))
					parserError(pa,"a meter with the name \"%s\" is already defined",pa->strVal);
				checkAllowedChars (pa,meter->name);
				VPRINTFN(4,"parseMeter %s",pa->strVal);
				break;
			case TK_INAME:
				if (meter->iname) parserError(pa,"%s: duplicate meter iname in meter definition",meter->name);
				parserExpectEqual(pa,TK_STRVAL);
				meter->iname = strdup(pa->strVal);
				checkAllowedChars (pa,meter->iname);
				break;
			case TK_HOSTNAME:
				if (meter->hostname) parserError(pa,"duplicate hostname");
				parserExpectEqual(pa,TK_STRVAL);
				meter->hostname = strdup(pa->strVal);
				meter->isTCP = 1;
				break;
			case TK_MEASUREMENT:
				typeConflict++;
				parserExpectEqual(pa,TK_STRVAL);
				free(meter->influxMeasurement);
				meter->influxMeasurement = strdup(pa->strVal);
				break;
			case TK_ADDRESS:
				parserExpectEqual(pa,TK_INTVAL);
				meter->mbusAddress = pa->iVal;
				break;
            case TK_STRVAL:
				meterFormula = meter->meterFormula;
				while (meterFormula) {
					if (strcmp(meterFormula->name,pa->strVal) == 0) parserError(pa,"duplicate formula register name %s",pa->strVal);
					meterFormula = meterFormula->next;
				}
				meterFormula = (meterFormula_t *)calloc(1,sizeof(meterFormula_t));
				meterFormula->enableInfluxWrite = 1;
                meterFormula->enableMqttWrite = 1;

				meterFormula->name = strdup(pa->strVal);
				checkAllowedChars (pa,meterFormula->name);
				parserExpect(pa,TK_EQUAL);
				tk = parserGetToken(pa);
				switch (tk) {
                    case TK_STRVAL:
                        meterFormula->formula = strdup(pa->strVal);
                        break;

                    default:
                        parserError(pa,"string for formula expected");
				}

				// more optional parameters may follow separated by comma, e.g. arr="bla", dec=2
				tk = parserGetToken(pa);

				while (tk == TK_COMMA) {
					tk = parserGetToken(pa);

					if (tk >= TK_REGOPTSFIRST && tk <= TK_REGOPTSLAST) {
						parserExpectEqual(pa,TK_STRVAL);
						switch (tk) {
							case TK_ARRAY:
								meterFormula->arrayName = strdup(pa->strVal);
								break;
							default:
								parserError(pa,"parseMeter: opt with str %d (%s) not yet supported",tk,pa->strVal);
						}
					} else
					if (tk >= TK_REGOPTIFIRST && tk <= TK_REGOPTILAST) {
						parserExpectEqual(pa,TK_INTVAL);
						switch (tk) {
							case TK_DEC:
								meterFormula->decimals = pa->iVal;
								break;
                            case TK_MQTT:
                                meterFormula->enableMqttWrite = pa->iVal;
                                break;
                            case TK_INFLUX:
                                meterFormula->enableInfluxWrite = pa->iVal;
                                break;
							default:
								parserError(pa,"parseMeter: opt with int %d (%s) not yet supported",tk,pa->strVal);
						}
					} else
					if (tk == TK_FORCE) {
						parserExpect(pa,TK_EQUAL);
						tk = parserGetToken(pa);
						if (tk != TK_INT16 && tk != TK_FLOAT) parserError(pa,"identifier (int or float) expected");
						if (tk == TK_INT16) meterFormula->forceType = force_int;
						else meterFormula->forceType = force_float;
					} else {
                        switch (tk) {
                            case TK_IMAX:
                                if (meterFormula->influxMultProcessing != pr_last) parserError(pa,"imax,imin or iavg already specified");
                                meterFormula->influxMultProcessing = pr_max;
                                break;
                            case TK_IMIN:
                                if (meterFormula->influxMultProcessing != pr_last) parserError(pa,"imax,imin or iavg already specified");
                                meterFormula->influxMultProcessing = pr_min;
                                break;
                            case TK_IAVG:
                                if (meterFormula->influxMultProcessing != pr_last) parserError(pa,"imax,imin or iavg already specified");
                                meterFormula->influxMultProcessing = pr_avg;
                                break;
                            default: parserError(pa,"identifier for option expected");
                        }
					}

					tk = parserExpectOrEOL(pa,TK_COMMA);
				}
				// add the formula to the meter
				if (meter->meterFormula) {
					meterFormula_t * mf = meter->meterFormula;
					while (mf->next) mf=mf->next;
					mf->next = meterFormula;
				} else meter->meterFormula = meterFormula;

				// update enabled register count in meter
				if (meterFormula->enableInfluxWrite) meter->numEnabledRegisters_influx++;
				if (meterFormula->enableMqttWrite) meter->numEnabledRegisters_mqtt++;

				break;


			default:
				strncpy(errMsg,"parseMeter: unexpected identifier ",sizeof(errMsg)-1);
				if (tk != TK_IDENT) strncat(errMsg,parserGetTokenTxt(pa,tk),sizeof(errMsg)-1);
				else strncat(errMsg,pa->strVal,sizeof(errMsg)-1);
				parserError (pa,errMsg);
		}
		if (tk != TK_EOL) {
			tk = parserGetToken(pa);
			if (tk != TK_EOL) parserError(pa,"EOL or , expected");
		}
		tk = parserGetToken(pa);
	}

	if (meter->meterType)
		if (meter->mqttprefix == NULL)
			if (meter->meterType->mqttprefix) meter->mqttprefix = strdup(meter->meterType->mqttprefix);
	if (meter->mqttprefix == NULL)
		if (mqttprefix) meter->mqttprefix = strdup(mqttprefix);

	if (!meter->name) parserError(pa,"Meter name not specified");
	if (!meter->meterType)
        if (!meter->meterFormula) parserError(pa,"%s: No meter formula registers and no meter type specified, either one or both need to be specified",meter->name);
	if (meter->mbusAddress < 0 && meter->mbusId < 0 && meter->meterType) parserError(pa,"%s: No mbus address or id specified",meter->name);

	// add meter
	if (meters) {
		meter_t * mt = meters;
		while (mt->next) mt = mt->next;
		mt->next = meter;
	} else meters = meter;

	// copy the registers to read from the meter type to the meter

	if (meter->meterType) {		// for a virtual meter with formulas only
		meterRegister_t *registerDef = meter->meterType->meterRegisters;
		meterRegisterRead_t * mrrd = meter->registerRead;
		while (registerDef) {
			if (!mrrd) {
				meter->registerRead = (meterRegisterRead_t *)calloc(1,sizeof(meterRegisterRead_t));
				mrrd = meter->registerRead;
			} else {
				mrrd->next = (meterRegisterRead_t *)calloc(1,sizeof(meterRegisterRead_t));
				mrrd = mrrd->next;
			}
			mrrd->registerDef = registerDef;
			if (registerDef->decimals > 0 || registerDef->forceType == force_float)
				mrrd->isInt = 0;
			registerDef = registerDef->next;
		}
	}
	if (meter->influxWriteMult) meter->influxWriteCountdown = -1; // meter->influxWriteMult;

	if (meter->meterType)
		meter->isFormulaOnly = meter->meterType->isFormulaOnly;
	else
		meter->isFormulaOnly = 1;

	meterFormula = meter->meterFormula;
	while (meterFormula) {
		if (meterFormula->enableInfluxWrite) meter->numEnabledRegisters_influx++;
		meterFormula = meterFormula->next;
	}

	return tk;
}


int readMeterDefinitions (const char * configFileName) {
	meter_t *meter;
	int rc;
	int tk;
	parser_t *pa;

	pa = parserInit(CHAR_TOKENS,
		"floatabcd"       ,TK_FLOAT_ABCD,
		"floatbadc"       ,TK_FLOAT_BADC,
		"float"           ,TK_FLOAT,
		"floatdcba"       ,TK_FLOAT_CDAB,
		"int"             ,TK_INT16,
		"int16"           ,TK_INT16,
		"int32"           ,TK_INT32,
		"int48"           ,TK_INT48,
		"int64"           ,TK_INT64,
		"uint"            ,TK_UINT16,
		"uint16"          ,TK_UINT16,
		"uint32"          ,TK_UINT32,
		"uint48"          ,TK_UINT48,
		"uint64"          ,TK_UINT64,

		"int32l"          ,TK_INT32L,
		"int48l"          ,TK_INT48L,
		"int64l"          ,TK_INT64L,
		"uint32l"         ,TK_UINT32L,
		"uint48l"         ,TK_UINT48L,
		"uint64l"         ,TK_UINT64L,

		"dec"             ,TK_DEC,
		"div"             ,TK_DIV,
		"mul"             ,TK_MUL,
		"arr"             ,TK_ARRAY,
		"array"           ,TK_ARRAY,
		"force"           ,TK_FORCE,
		"name"            ,TK_NAME,
		"type"            ,TK_TYPE,
		"address"         ,TK_ADDRESS,
		"hostname"        ,TK_HOSTNAME,
		"measurement"     ,TK_MEASUREMENT,
		"port"            ,TK_PORT,
		"id"              ,TK_ID,
		"formula"         ,TK_FORMULA,
		"meters"          ,TK_METERS,
		"disabled"        ,TK_DISABLE,
		"mqtt"            ,TK_MQTT,
        "influx"          ,TK_INFLUX,
        "mqttqos"	      ,TK_MQTTQOS,
		"mqttretain"      ,TK_MQTTRETAIN,
		"mqttprefix"      ,TK_MQTTPREFIX,
		"influxwritemult" ,TK_INFLUXWRITEMULT,
		"imax"            ,TK_IMAX,
		"imin"            ,TK_IMIN,
		"iavg"            ,TK_IAVG,
		//"modbusdebug"     ,TK_MODBUSDEBUG,
		"default"         ,TK_DEFAULT,
		"schedule"        ,TK_SCHEDULE,
		"iname"           ,TK_INAME,
		NULL);
	rc = parserBegin (pa, configFileName, 1);
	if (rc != 0) {
		fprintf(stderr,"parserBegin (\"%s\")failed\n",configFileName);
		exit(1);
	}

	tk = parserExpectSection(pa);
	while (tk != TK_EOF) {
		if (strcasecmp(pa->strVal,"MeterType") == 0)
			tk = parseMeterType(pa);
		else if (strcasecmp(pa->strVal,"Meter") == 0)
			tk = parseMeter(pa);
		else if (strcasecmp(pa->strVal,"Schedule") == 0)
			tk = parseCron(pa);

		else
			parserError(pa,"unknown section type %s",pa->strVal);
	}
	parserFree(pa);

	meter = meters;
	if (!meter) {
		EPRINTFN("no meters defined in %s",configFileName);
		exit(1);
	}

	// set the modbus RTU handle in the meter or open an IP connection to the meter
	while (meter) {
		if (meter->isTCP) {
			//meter->mb = modbus_new_tcp_pi(meter->hostname, const char *service);
			//do the open when querying the meter to be able to retry of temporary not available
		} else {	// modbus RTU
			if ((meter->mbusAddress > 0 || meter->mbusId > -1) && meter->disabled == 0) {
				meter->mb = mbusSerial_getmh();
				if (*meter->mb == NULL) {
					EPRINTFN("%s: serial mbus not yet opened or no serial device specified",meter->name);
					exit(1);
				}
			}
		}
		meter = meter->next;
	}

	return 0;

}
