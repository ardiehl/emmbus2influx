#include "cron.h"
#include <stdlib.h>
#include <string.h>
#include "log.h"
#include "assert.h"


cronDef_t *cronTab;

cronDef_t * cron_find(const char * name) {
	cronDef_t * cd = cronTab;

	while (cd) {
		if (cd->name == name) return cd;
		if (name)
			if (cd->name)
				if (strcmp(name,cd->name) == 0) return cd;
		cd = cd->next;
	}
	return NULL;
}


void cron_add(const char *name, const char * cronExpression) {		// name=NULL for default
	cronDef_t * ct,* ct1;
	const char *errStr;

	ct = cron_find(name);
	if (ct) {
		if (name != NULL) {
			EPRINTFN("multiple definitions for cron entry \"%s\"",name);
			exit(1);
		}
		EPRINTFN("Warning: Overwriting previously defined default cron entry (%s) with (%s)",ct->cronExpression,cronExpression);
		free(ct->cronExpression);
		ct->cronExpression = strdup(cronExpression);
		memset(&ct->cronExpr,0,sizeof(ct->cronExpr));
		cron_parse_expr(cronExpression, &ct->cronExpr, &errStr);
		if (errStr) {
			EPRINTFN("Error in cron expression: \"%s\"",cronExpression);
			EPRINTFN(errStr);
			exit (1);
		}
		return;
	}

	ct = (cronDef_t *) calloc(1,sizeof(cronDef_t));
	ct->cronExpression = strdup(cronExpression);
	if (name) ct->name = strdup(name);
	cron_parse_expr(cronExpression, &ct->cronExpr, &errStr);
	if (errStr) {
		EPRINTFN("Error in cron expression: \"%s\"",cronExpression);
		EPRINTFN(errStr);
		exit (1);
	}
	if (! cronTab) {
		cronTab = ct;
		return;
	}
	ct1 = cronTab;
	while (ct1->next) ct1=ct1->next;
	ct1->next = ct;
}


int  cron_is_due(time_t currTime, cronDef_t *cronDef) {
	if (currTime >= cronDef->nextQueryTime) return 1;
	return 0;
}

//void cron_calc_next(cronDef_t *cronDef) {
//	cron_next(&cronDef->cronExpr, time(NULL));
//}

void cron_meter_add(cronDef_t *cronDef, meter_t *meter) {
	cronMember_t * cm;

	//printf("cron_meter_add(%s,%s)\n",cronDef->name,meter->name);
	cm = (cronMember_t *) calloc(1,sizeof(cronMember_t));
	cm->meter = meter;

	if (! cronDef->members) {
		cronDef->members = cm;
		if (cronDef->name) meter->hasSchedule++;	// not for default schedule
		return;
	}
	cronMember_t *cm1 = cronDef->members;
	while (cm1->next) cm1 = cm1->next;
	cm1->next = cm;
	if (cronDef->name) meter->hasSchedule++;	// not for default schedule
}

void cron_meter_add_byName(char * cronName, meter_t *meter) {
	cronDef_t * cronDef = cronTab;
	cronDef_t * cd = NULL;

	//printf("cron_meter_add_byName (%s,%s)\n",cronName,meter->name);

	while (cronDef && (cd == NULL)) {
		if (cronDef->name == cronName) cd = cronDef;	// for default name==NULL
		else
			if (cronName)
				if (cronDef->name)
					if (strcmp(cronName,cronDef->name) == 0) cd = cronDef;
		cronDef = cronDef->next;
	}
	if (cd) {
		cron_meter_add(cd, meter);
		return;
	}
	EPRINTFN("cron name \"%s\" not defined",cronName);
	exit(1);
}




int parseCron (parser_t * pa) {
	int tk;
	char *st;
	char errMsg[255];

	parserExpect(pa,TK_EOL);  // after section
	tk = parserGetToken(pa);
	while (tk != TK_SECTION && tk != TK_EOF) {
		switch(tk) {
			case TK_DEFAULT:
				parserExpectEqual(pa,TK_STRVAL);
				cron_add(NULL,pa->strVal);
				break;
            case TK_STRVAL:
				st = strdup(pa->strVal);
				parserExpectEqual(pa,TK_STRVAL);
				cron_add(st,pa->strVal);
				free(st);
				break;
			case TK_EOL:
				break;
			default:
				strncpy(errMsg,"parseCron: unexpected identifier ",sizeof(errMsg)-1);
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

	return tk;
}


time_t getCurrTime() {
#ifdef CRON_USE_LOCAL_TIME
	time_t t = time(NULL);
    struct tm *lt = localtime(&t);
	return mktime(lt);
#else
	return time(NULL);
#endif
}

char * getFormattedTime(time_t t) {
#if 0
#ifdef CRON_USE_LOCAL_TIME
	struct tm *lt = localtime(&t);
#else
	struct tm *lt = gmtime(&t);
#endif
#endif
	struct tm *lt = gmtime(&t);
	char *buf=(char *)malloc(30);
	snprintf(buf,30,"%02d.%02d.%04d %02d:%02d:%02d",lt->tm_mday,lt->tm_mon,lt->tm_year+1900,lt->tm_hour,lt->tm_min,lt->tm_sec);
	return buf;
}


int cron_queryMeters(int verboseMsg) {
	meter_t * meter = meters;
	cronMember_t * cm;
	time_t currTime = getCurrTime();
	char *timeStr;

	while (meter) {
		meter->meterHasBeenRead = 0;
		meter->isDue = 0;
		meter = meter->next;
	}

	// mark all meters of all due schedules and calculate next query time
	cronDef_t * cd = cronTab;
	while (cd) {
		if (cron_is_due(currTime,cd)) {
			if (cd->members) {
				if (verbose) {
					timeStr = getFormattedTime(time(NULL));
					VPRINTF(1,"%s: Schedule \"%s\" is due: ",timeStr,cd->name ? cd->name : "default");
					free(timeStr);
				}
				cd ->nextQueryTime = cron_next(&cd->cronExpr, currTime);
				cm = cd->members;
				int first=1;
				while (cm) {
					assert (cm->meter != NULL);
					if (! cm->meter->disabled) {
						VPRINTF(1,"%c%s",first?' ':',',cm->meter->name);
						setMeterFvalueInfluxLast (cm->meter);
						cm->meter->isDue++;
					}
					cm = cm->next;
				}
				if (verbose) {
					timeStr = getFormattedTime(cd->nextQueryTime);
					VPRINTF(1,", next query: %s\n",timeStr);
					free(timeStr);
				}
			}
		}
		cd = cd->next;
	}

	// query all due meters
	meter = meters;
	int numMeters = 0;
	while (meter) {
		if (meter->isDue) {
			int res = queryMeter(verboseMsg, meter);
			if (! meter->isTCP) msleep(500);
			if (res == 0) numMeters++;
		}
		meter = meter->next;
	}

#ifndef DISABLE_FORMULAS
	// meter formulas
	meter = meters;
	while (meter) {
		if (! meter->disabled)
			if (meter->meterHasBeenRead) executeMeterFormulas(verboseMsg,meter);
		meter = meter->next;
	}
#endif

    // handle influxWriteMult
    meter = meters;
	while (meter) {
		if (! meter->disabled)
			if (meter->meterHasBeenRead) {
				setMeterFvalueInflux(meter);
				executeInfluxWriteCalc(verboseMsg,meter);
			}
		meter = meter->next;
	}

	return numMeters;
}


void cron_showSchedules() {
	cronDef_t * cd = cronTab;
	printf( "Schedules\n" \
			"Name                Definition\n" \
			"------------------------------------------------------------------------------\n");
	while (cd) {
		printf("%-20s%-30s\n%-20s",cd->name==NULL ? "default" : cd->name,cd->cronExpression,"");

		cronMember_t * cm = cd->members;
		int first = 1;
		while (cm) {
			if (first>0) printf("Members: ");
			printf("%c%s",first ? '[' : ',',cm->meter->name);
			cm = cm->next;
			first--;
		}
		if(first<1) printf("] ");
		char * timeStr = getFormattedTime(cd->nextQueryTime);
		printf("next query: %s\n",timeStr);
		free(timeStr);
		cd = cd->next;
	}
}

// set the default schedule for all meters where no schedule(s) are specified
void cron_setDefault() {
	meter_t * meter = meters;
	time_t currTime = getCurrTime();

	while (meter) {
		if (! meter->hasSchedule) cron_meter_add_byName(NULL,meter);
		meter = meter->next;
	}
	// set next query time
	cronDef_t * cd = cronTab;
	while (cd) {
		cd->nextQueryTime = cron_next(&cd->cronExpr, currTime);
		cd = cd->next;
	}
}
