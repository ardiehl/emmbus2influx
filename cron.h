/*
cron
a list of cron expressions and meters to be queried at that time

2023 Armin Diehl <ad@ardiehl.de>

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#ifndef CRON_H_INCLUDED
#define CRON_H_INCLUDED

#include "meterDef.h"
#include "ccronexpr.h"
#include <time.h>
#include "parser.h"
#include "mbusread.h"

typedef struct cronMember_t cronMember_t;
struct cronMember_t {
	meter_t * meter;
	cronMember_t * next;
};


typedef struct cronDef_t cronDef_t;
struct cronDef_t {
	char *name;			// NULL for default
	cronDef_t *next;
	cronMember_t * members;
	char * cronExpression;
	cron_expr cronExpr;
	time_t nextQueryTime;
};

time_t getCurrTime();
void cron_add(const char *name, const char * cronExpression);		// name=NULL for default
int  cron_is_due(time_t currTime, cronDef_t *cronDef);
void cron_calc_next(cronDef_t *cronDef);
void cron_meter_add(cronDef_t *cronDef, meter_t *meter);
void cron_meter_add_byName(char * cronName, meter_t *meter);
int parseCron (parser_t * pa);
void cron_showSchedules();

/**
 * Query all meters that are due and calculate the next query time for all defined schedules
 * @param Verbose if > 1
  * @return the number of meters queried successful
 */
int cron_queryMeters(int verboseMsg);

/**
 * Set the default schedule for all meters where no schedules are defined
 */
void cron_setDefault();

#endif // CRON_H_INCLUDED
