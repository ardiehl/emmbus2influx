#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <ctype.h>
#include "global.h"
#include "math.h"
#include <endian.h>
#include <assert.h>

#include "log.h"
#include "mbusread.h"
#include "mbus.h"
#ifndef DISABLE_FORMULAS
#include "muParser.h"
#include <readline/readline.h>
#include <readline/history.h>
#endif


int mbus_ping_address(mbus_handle *handle, mbus_frame *reply, int address)
{
    int i, ret = MBUS_RECV_RESULT_ERROR;

    memset((void *)reply, 0, sizeof(mbus_frame));

    for (i = 0; i <= handle->max_search_retry; i++)
    {
        if (verbose)
        {
            printf("%d ", address);
            fflush(stdout);
        }

        if (mbus_send_ping_frame(handle, address, 0) == -1)
        {
            fprintf(stderr,"mbus_ping_address failed. Could not send ping frame to address %d: (%s)\n", address,mbus_error_str());
            return MBUS_RECV_RESULT_ERROR;
        }

        ret = mbus_recv_frame(handle, reply);

        if (ret != MBUS_RECV_RESULT_TIMEOUT)
            return ret;
    }

    return ret;
}


mbus_handle *mb_Serial;

typedef struct meterIPConnection_t meterIPConnection_t;
struct meterIPConnection_t {
	char * hostname;
	char * port;
	mbus_handle *mb;
	int isConnected;
	int retryCount;
	meterIPConnection_t *next;
};

meterIPConnection_t * meterIPconnections;

const char * defPort = "502";

mbus_handle ** mbusTCP_open (char *device, char *port) {
	int res;
	meterIPConnection_t * mc = meterIPconnections;

	if (port == NULL) port = (char *)defPort;

	while (mc) {
		if (strcmp(device,mc->hostname) == 0)
			if (strcmp(port,mc->port) == 0) {
				if (mc->isConnected) {
					VPRINTFN(2,"mbusTCP_open (%s:%s): using exiting connection",device,port);
					return &mc->mb;
				}
				if (mc->retryCount) {
					mc->retryCount--;
					return NULL;
				}
				res = mbus_connect(mc->mb);
				if (res == 0) {
					mc->isConnected = 1;
					VPRINTFN(2,"mbusTCP_open (%s:%s): connected",device,port);
					return &mc->mb;
				}
				VPRINTFN(2,"mbusTCP_open (%s:%s): connect failed retrying later",device,port);
				mc->retryCount = TCP_OPEN_RETRY_DELAY;
				return NULL;
			}
		mc = mc->next;
	}
	if (meterIPconnections == NULL) {
		meterIPconnections = (meterIPConnection_t *)calloc(1,sizeof(meterIPConnection_t));
		mc = meterIPconnections;
	} else {
		mc = meterIPconnections;
		while (mc->next != NULL) mc = mc->next;
		mc->next = (meterIPConnection_t *)calloc(1,sizeof(meterIPConnection_t));
		mc = mc->next;
	}
	mc->hostname = device;
	mc->port = port;
	VPRINTFN(2,"mbusTCP_open (%s:%s): creating new host entry",device,port);
	mc->mb = mbus_context_tcp(device, strtol(port,NULL,10));
	if (!mc->mb) {
		EPRINTFN("mbus_context_tcp (%s:%s) failed",device,port);
		exit(1);
	}
	res = mbus_connect(mc->mb);
	if (res == 0) {
		VPRINTFN(2,"mbusTCP_open (%s:%s): connected",device,port);
		mc->isConnected = 1;
		return &mc->mb;
	}
	VPRINTFN(2,"mbusTCP_open (%s:%s): connect failed retrying later",device,port);
	mc->retryCount = TCP_OPEN_RETRY_DELAY;
	return NULL;
}


void mbusTCP_freeAll() {
	meterIPConnection_t * mc = meterIPconnections;
	meterIPConnection_t * mcWork;
	while (mc) {
		mcWork = mc;
		mc = mc->next;
		if (mcWork->mb) {
            mbus_disconnect(mcWork->mb);
            mbus_context_free(mcWork->mb);
        }
		free(mcWork);
	}
	meterIPconnections = NULL;
}

//*****************************************************************************


/* msleep(): Sleep for the requested number of milliseconds. */
int msleep(long msec)
{
    struct timespec ts;
    int res;

    if (msec < 0)
    {
        errno = EINVAL;
        return -1;
    }

    ts.tv_sec = msec / 1000;
    ts.tv_nsec = (msec % 1000) * 1000000;

    do {
        res = nanosleep(&ts, &ts);
    } while (res && errno == EINTR);

    return res;
}


//*****************************************************************************

mbus_handle ** mbusSerial_getmh() {
	return &mb_Serial;
}

int mbusSerial_open (const char *device, int baud) {

	if ((mb_Serial = mbus_context_serial(device)) == NULL) {
        fprintf(stderr,"mbusSerial_open failed: Could not initialize M-Bus context for %s (%s)\n", device, mbus_error_str());
        exit(1);
    }

    if (mbus_connect(mb_Serial) == -1) {		// sets B2400
        fprintf(stderr,"\nFailed to configure serial port %s @ %d baud (%s)\n",device,baud,strerror(errno));
        exit(1);
    }


    if (mbus_serial_set_baudrate(mb_Serial, baud) == -1) {
        fprintf(stderr,"Failed to set baud rate of %d for %s (%s).\n",baud,device,strerror(errno));
        exit(1);
    }

	VPRINTFN(7,"mbusSerial_open: %s opened (B%d)",device,baud);
	return 0;
}


void mbusSerial_close() {
	if (mb_Serial) {
		mbus_disconnect(mb_Serial);
		mbus_context_free(mb_Serial);
		mb_Serial = NULL;
	}
}


void dumpBuffer (const uint16_t *data,int numValues) {
	while (numValues) {
		printf("%04x ",*data);
		data++; numValues--;
	}
	printf("\n");
}

// apply divider/multiplier, if we have an int value, convert it to float
void applyDevider (meterRegisterRead_t *rr) {

	if (rr->registerDef->divider) {
		if (rr->isInt) rr->isInt = 0;
        rr->fvalue = rr->fvalue / rr->registerDef->divider;
	}

	if (rr->registerDef->multiplier) {
		rr->fvalue = rr->fvalue * rr->registerDef->multiplier;
	}

	switch (rr->registerDef->forceType) {
		case force_none:
			break;
		case force_int:
			rr->isInt = 1;
			break;
		case force_float:
			rr->isInt = 0;
			break;
	}
	//printf("%s: isInt: %d\n",rr->registerDef->name,rr->isInt);
}



#ifndef DISABLE_FORMULAS


mu::Parser *parser;         // included all registers from all meters
mu::Parser *currParser;     // used for auto complete (tab) in formula eval

using namespace mu;

static value_type Rnd(value_type v) { return v * std::rand() / (value_type)(RAND_MAX + 1.0); }

// init the global parser and add all variables
mu::Parser * initParser() {
    meterRegisterRead_t *registerRead;
    meter_t *meter = meters;
    char name[255];

    if (parser == NULL) {
        parser = new (mu::Parser);
        parser-> DefineNameChars(MUPARSER_ALLOWED_CHARS);
        parser->DefineFun(_T("rnd"), Rnd, false);     // Add an unoptimizeable function
        // add all variables using their fully qualified name (MeterName.VariableName)
        while (meter) {
            if (meter->disabled == 0) {
                registerRead = meter->registerRead;
                while (registerRead) {
                    strncpy(name,meter->name,sizeof(name)-1);
                    strncat(name,".",sizeof(name)-1);
                    strncat(name,registerRead->registerDef->name,sizeof(name)-1);

                    try {
                        parser->DefineVar(name,&registerRead->fvalue);
                    }
                    catch (mu::Parser::exception_type& e) {
                        EPRINTFN("error adding variable %s (%s)",&name,e.GetMsg().c_str());
                        exit(1);
                    }

                    registerRead = registerRead->next;
                }
            }
            meter = meter->next;
        }
    }
    return parser;
}


// init the global parser and add all local variables
//mu::Parser *parser
mu::Parser * initLocalParser(meter_t *meter) {
    meterRegisterRead_t *registerRead;
    mu::Parser *parser;

    parser = new (mu::Parser);
    parser-> DefineNameChars(MUPARSER_ALLOWED_CHARS);
    parser->DefineFun(_T("rnd"), Rnd, false);     // Add an unoptimizeable function
    // add all local meter variables using their local name
    registerRead = meter->registerRead;
    while (registerRead) {
        try {
            parser->DefineVar(registerRead->registerDef->name,&registerRead->fvalue);
        }
        catch (mu::Parser::exception_type& e) {
            EPRINTFN("error adding variable %s (%s)",&registerRead->registerDef->name,e.GetMsg().c_str());
            exit(1);
        }

        registerRead = registerRead->next;
    }
    return parser;
}


void executeMeterFormulas(int verboseMsg, meter_t * meter) {
    meterFormula_t * mf = meter->meterFormula;
    mu::Parser *parser = NULL;
    if (!mf) return;
    if (verbose || verboseMsg)
		printf("\nexecuteMeterFormulas for \"%s\"\n",meter->name);
    while(mf) {
        if (!parser) parser = initParser();
        try {
            parser->SetExpr(mf->formula);
            mf->fvalue = parser->Eval();
            if (verbose || verboseMsg)
				printf("%s \"%s\" %10.2f\n",mf->name,mf->formula,mf->fvalue);
        }
        catch (mu::Parser::exception_type &e) {
            EPRINTFN("%d.%d error evaluating meter formula (%s)",meter->name,mf->name,e.GetMsg().c_str());
            exit(1);
        }
        mf = mf->next;
    }
}


int executeMeterTypeFormulas(int verboseMsg, meter_t *meter) {
    meterRegisterRead_t *registerRead;

    mu::Parser * parser = NULL;
    registerRead = meter->registerRead;
    while (registerRead) {
        // execute formulas
        if (registerRead->registerDef->formula) {
            if (!parser) parser = initLocalParser(meter);
            try {
                parser->SetExpr(registerRead->registerDef->formula);
                registerRead->fvalue = parser->Eval();
                applyDevider(registerRead);
                VPRINTF(2,"MeterType formula for %s: \"%s\" -> %10.2f\n",meter->name,registerRead->registerDef->formula,registerRead->fvalue);
            }
            catch (mu::Parser::exception_type &e) {
                EPRINTFN("%d.%d error evaluating meter type formula (%s)",meter->name,registerRead->registerDef->name,e.GetMsg().c_str());
            }
        }
		registerRead = registerRead->next;
	}
	if (parser) delete(parser);
    return 0;
}



void listConstants(mu::Parser *parser) {
    mu::valmap_type cmap = parser->GetConst();
    if (cmap.size())
    {
        mu::valmap_type::const_iterator item = cmap.begin();
        for (; item!=cmap.end(); ++item)
            printf("%s %3.2f\n",item->first.c_str(),item->second);
    }
}


void listFunctions(mu::Parser *parser) {
    mu::funmap_type cmap = parser->GetFunDef();
    if (cmap.size())
    {
        mu::funmap_type::const_iterator item = cmap.begin();
        for (; item!=cmap.end(); ++item)
            printf("%s ",item->first.c_str());
    }
    printf("\n");
}


void listOperators(mu::Parser *parser) {
    int i = 0;
    printf("Operators: ");
    const mu::char_type** op = parser->GetOprtDef();
    while (op[i]) {
        printf("%s ",(char *)op[i]);
        i++;
    }
    printf("\n? and : can be used like in c, for example to have an alarm for voltage:\n"\
           " VoltageL1 > 240 ? 1:0 || VoltageL2 > 240 ? 1:0 || VoltageL3 > 240 ? 1:0 ||  Inverter.Voltage > 240 ? 1 : 0\n"\
           "This will return 1 of any of the four voltages are above 240.\n\n");
}


void listVariables(mu::Parser *parser) {
    char *name;
    char *p;
    meter_t *meter;

    // Get the map with the variables
    mu::varmap_type variables = parser->GetVar();
    //cout << "Number: " << (int)variables.size() << "\n";

    // Get the number of variables
    mu::varmap_type::const_iterator item = variables.begin();

    // Query the variables
    for (; item!=variables.end(); ++item)
    {
        name = strdup(item->first.c_str());
        printf("%-40s %12.2f",name,*(double *)item->second);
        p = strchr(name,'.');
        if (p) {
            *p = '\0';
            meter = findMeter(name);
            if (meter) {
                printf("  type: '%s'",meter->meterType->name);
            }
        }
        free(name);
        printf("\n");

    }
}


char **character_name_completion(const char *, int, int);
char *character_name_generator(const char *, int);

const char *evalHelp = "enter formula to show the result, * for showing available variables\n" \
           "*c to list constants, *f to list functions, *o to list operators or *q to quit,\n"\
           "<tab> for autocomplete, <tab><tab> list or ? to show this message\n\n";

void evalFormula(const char * welcome, const char * prompt) {
    char *line;
    double result;
    //printf(prompt); printf("\n");
    initParser();
    printf(evalHelp);

    rl_attempted_completion_function = character_name_completion;

    while(1) {
        line = readline(prompt);
        if (line) {
            if(strcmp(line,"*exit") == 0 || strcmp(line,"*q") == 0) return;
            if (strcmp(line,"*") == 0) listVariables(currParser);
            else if (strcmp(line,"*c") == 0) listConstants(currParser);
            else if (strcmp(line,"*f") == 0) listFunctions(currParser);
            else if (strcmp(line,"*o") == 0) listOperators(currParser);
            else if (strcmp(line,"?") == 0) printf(evalHelp);
            else {
                try {
                    if (line) {
                        if (strlen(line) > 0) {
                            add_history (line);
                            currParser->SetExpr(line);
                            result = currParser->Eval();
                            printf(" Result: %10.4f\n",result);

                        }
                        free(line); line = NULL;
                    }
                }
                catch (mu::Parser::exception_type& e) {
                    EPRINTFN("error evaluating formula (%s)",e.GetMsg().c_str());
                    free(line); line = NULL;
                }
            }
        } else printf("\n");
    }
}


char **character_name_completion(const char *text, int start, int end)
{
    rl_attempted_completion_over = 1;
    return rl_completion_matches(text, character_name_generator);
}

char *character_name_generator(const char *text, int state)
{
    static int list_index, len, max_index;
    char *name;
    static mu::varmap_type::const_iterator item;


    if (!state) {
        list_index = 0;
        len = strlen(text);
        mu::varmap_type variables = currParser->GetVar();
        item = currParser->GetVar().begin();
        max_index = variables.size();
    }

    while (list_index < max_index) {
        list_index++;
        name = (char *)item->first.c_str();
        item++;
        //printf(" '%s' '%s' %d %d\n",name,text,len,strncmp(name, text, len));
        if (strncmp(name, text, len) == 0) {
            return strdup(name);
        }
    }

    return NULL;
}


#define PROMPT "> "
void testRegCalcFormula(char * meterName) {
    char *prompt;
    meter_t*meter;
    mu::Parser * parser;

    if (meterName) {
        meter = findMeter(meterName);
        if (! meter) {
            printf("Invalid meter name '%s'\n",meterName);
            exit(1);
        }
        if (meter->meterType)
			printf("\nFormula test for register values in meter type '%s'\n",meter->meterType->name);
		else
			printf("\nFormula test for register values in virtual meter '%s'\n",meterName);
        parser = initLocalParser(meter);
        if (meter->meterType) {
			prompt = (char *)malloc(strlen(meter->meterType->name)+3);
			strcpy(prompt,meter->meterType->name); strcat(prompt,":"); strcat(prompt," ");
        } else {
        	prompt = (char *)malloc(strlen(meterName)+3);
			strcpy(prompt,meterName); strcat(prompt,":"); strcat(prompt," ");
        }
    } else {
        printf("\nFormula test for calculating virtual registers in a meter definition\n");
        prompt = (char *)malloc(strlen(PROMPT)+1);
        strcpy(prompt,PROMPT);
        parser = initParser();
    }
    currParser = parser;    // for auto complete n readline

    evalFormula("Formula test for register values",prompt);
    free(prompt);
    delete(parser);
}


void freeFormulaParser() {
	if (parser) delete(parser);
	parser = NULL;
}


#endif

void executeInfluxWriteCalc (int verboseMsg, meter_t *meter) {
    meterRegisterRead_t *mrrd;
    meterFormula_t *mf;

    if (!meter->influxWriteMult) return;

    if (meter->influxWriteCountdown == meter->influxWriteMult) {
        meter->influxWriteCountdown--;      // first read, nothing to do
        return;
    }

    if (meter->influxWriteCountdown == -1) {
		// force write on first run after program start
		meter->influxWriteCountdown++;
		return;
    }

    if (meter->influxWriteCountdown == 0)
        meter->influxWriteCountdown = meter->influxWriteMult;

    meter->influxWriteCountdown--;

    mrrd = meter->registerRead;
    while (mrrd) {
        switch (mrrd->registerDef->influxMultProcessing) {
            case pr_last:
                break;
            case pr_max:
                //LOG(9,"max %d fvalue: %10.2f fvalueInflux: %10.2f fvalueInfluxLast: %10.2f ",meter->influxWriteCountdown,mrrd->fvalue,mrrd->fvalueInflux,mrrd->fvalueInfluxLast);
                if (mrrd->fvalueInflux < mrrd->fvalueInfluxLast) mrrd->fvalueInflux = mrrd->fvalueInfluxLast;
                //LOG(9,("-> %10.2f\n",mrrd->fvalueInflux);
                break;
            case pr_min:
                //LOG(9,"min %d fvalue: %10.2f fvalueInflux: %10.2f fvalueInfluxLast: %10.2f ",meter->influxWriteCountdown,mrrd->fvalue,mrrd->fvalueInflux,mrrd->fvalueInfluxLast);
                if (mrrd->fvalueInflux > mrrd->fvalueInfluxLast) mrrd->fvalueInflux = mrrd->fvalueInfluxLast;
                //LOG(9,"-> %10.2f\n",mrrd->fvalueInflux);
                break;
            case pr_avg:
                //LOG(9,"avg %d fvalue: %10.2f fvalueInflux: %10.2f fvalueInfluxLast: %10.2f ",meter->influxWriteCountdown,mrrd->fvalue,mrrd->fvalueInflux,mrrd->fvalueInfluxLast);
                mrrd->fvalueInflux = (mrrd->fvalueInflux + mrrd->fvalueInfluxLast) / 2.0;
                //LOG(9,"-> %10.2f\n",mrrd->fvalueInflux);
                break;
        }
        mrrd = mrrd->next;
    }
    mf = meter->meterFormula;
    while (mf) {
        switch (mf->influxMultProcessing) {
            case pr_last:
                break;
            case pr_max:
                //LOG(9,"max %d fvalue: %10.2f fvalueInflux: %10.2f fvalueInfluxLast: %10.2f ",meter->influxWriteCountdown,mf->fvalue,mf->fvalueInflux,mf->fvalueInfluxLast);
                if (mf->fvalueInflux < mf->fvalueInfluxLast) mf->fvalueInflux = mf->fvalueInfluxLast;
                //LOG(9,"-> %10.2f\n",mf->fvalueInflux);
                break;
            case pr_min:
                //LOG(9,"min %d fvalue: %10.2f fvalueInflux: %10.2f fvalueInfluxLast: %10.2f ",meter->influxWriteCountdown,mf->fvalue,mf->fvalueInflux,mf->fvalueInfluxLast);
                if (mf->fvalueInflux > mf->fvalueInfluxLast) mf->fvalueInflux = mf->fvalueInfluxLast;
                //LOG(9,"-> %10.2f\n",mf->fvalueInflux);
                break;
            case pr_avg:
                //LOG(9,"avg %d fvalue: %10.2f fvalueInflux: %10.2f fvalueInfluxLast: %10.2f ",meter->influxWriteCountdown,mf->fvalue,mf->fvalueInflux,mf->fvalueInfluxLast);
                mf->fvalueInflux = (mf->fvalueInflux + mf->fvalueInfluxLast) / 2.0;
                //LOG(9,"-> %10.2f\n",mf->fvalueInflux);
                break;
        }
        mf = mf->next;
    }
}


void setfvalueInfluxLast () {
    meter_t *meter = meters;
    meterRegisterRead_t *mrrd;
    meterFormula_t *mf;

    while (meter) {
        mrrd = meter->registerRead;
        while (mrrd) {
            mrrd->fvalueInfluxLast = mrrd->fvalueInflux;
            mrrd = mrrd->next;
        }
        mf = meter->meterFormula;
        while (mf) {
            mf->fvalueInfluxLast = mf->fvalueInflux;
            mf = mf->next;
        }
        meter = meter->next;
    }
}


void setfvalueInflux () {
    meter_t *meter = meters;
    meterRegisterRead_t *mrrd;
    meterFormula_t *mf;

    while (meter) {
        mrrd = meter->registerRead;

        while (mrrd) {
            mrrd->fvalueInflux = mrrd->fvalue;
            mrrd = mrrd->next;
        }
        mf = meter->meterFormula;
        while (mf) {
            mf->fvalueInflux = mf->fvalue;
            mf = mf->next;
        }
        meter = meter->next;
    }
}

const char * unknown = "unknown";

const char * mbus_data_record_type(mbus_data_record *record) {

    if (record) {
        // ignore extension bit
        int vif = (record->drh.vib.vif & MBUS_DIB_VIF_WITHOUT_EXTENSION);
        int vife = (record->drh.vib.vife[0] & MBUS_DIB_VIF_WITHOUT_EXTENSION);

        switch (record->drh.dib.dif & MBUS_DATA_RECORD_DIF_MASK_DATA) {
            case 0x00: // no data
                return "no data";
            case 0x01: // 1 byte integer (8 bit)
                return "int8_t";
            case 0x02: // 2 byte (16 bit)
                // E110 1100  Time Point (date)
                if (vif == 0x6C) return "time16_t";
                return "int8_t";
            case 0x03: // 3 byte integer (24 bit)
                return "int24_t";
            case 0x04: // 4 byte (32 bit)

                // E110 1101  Time Point (date/time)
                // E011 0000  Start (date/time) of tariff
                // E111 0000  Date and time of battery change
                if ( (vif == 0x6D) ||
                    ((record->drh.vib.vif == 0xFD) && (vife == 0x30)) ||
                    ((record->drh.vib.vif == 0xFD) && (vife == 0x70)))
                    return "time32_t";
                return "int32_t";
            case 0x05: // 4 Byte Real (32 bit)
                return "float32_t";
            case 0x06: // 6 byte (48 bit)
                // E110 1101  Time Point (date/time)
                // E011 0000  Start (date/time) of tariff
                // E111 0000  Date and time of battery change
                if ( (vif == 0x6D) ||
                    ((record->drh.vib.vif == 0xFD) && (vife == 0x30)) ||
                    ((record->drh.vib.vif == 0xFD) && (vife == 0x70)))
                    return "time48_t";
                return "int48_t";

            case 0x07: // 8 byte integer (64 bit)
                return "int64_t";

            //case 0x08:

            case 0x09: // 2 digit BCD (8 bit)
                return "bcd8_t";
            case 0x0A: // 4 digit BCD (16 bit)
                return "bcd16_t";
            case 0x0B: // 6 digit BCD (24 bit)
                return "bcd24_t";
            case 0x0C: // 8 digit BCD (32 bit)
                return "bcd32_t";
            case 0x0E: // 12 digit BCD (48 bit)
                return "bcd48_t";
            case 0x0F: // special functions
                return "binary_t";
            case 0x0D: // variable length
                return "string_t";
        }
    }

    return unknown;
}


void setRegisterValue(meter_t * meter,int recordNumber,const char * value, float fvalue) {
    meterRegisterRead_t *rr;
    char *endptr;
    if (meter)
        if (meter->registerRead) {
            rr = meter->registerRead;
            while (rr) {
                if (rr->registerDef->recordNumber == recordNumber) {
                    if (value) rr->fvalue = strtod(value,&endptr); else rr->fvalue = fvalue;
                    if (*endptr != 0) EPRINTFN("%s.%s: unable to convert value '%s' to a number",meter->name,rr->registerDef->name,value);
                    applyDevider (rr);
                    return;
                }
                rr = rr->next;
            }
        }
}


// untested
int process_mbus_data_fixed(meter_t * meter, mbus_data_fixed *data, int verboseMsg) {
    char *medium = NULL;
    char *func = NULL;
    char *unit[2] = { NULL,NULL };
    float value[2];
    int ival,rc;

    medium = strdup(mbus_data_fixed_medium(data));
    func = strdup(mbus_data_fixed_function(data->status));

    if (verboseMsg) printf("%s (status: %d, medium: %s, access: %d)\n",meter->name,data->status,medium,data->tx_cnt);

    unit[0] = strdup (mbus_data_fixed_unit(data->cnt1_type));
    unit[1] = strdup (mbus_data_fixed_unit(data->cnt2_type));

    if ((data->status & MBUS_DATA_FIXED_STATUS_FORMAT_MASK) == MBUS_DATA_FIXED_STATUS_FORMAT_BCD) {
        value[0] = mbus_data_bcd_decode_hex(data->cnt1_val, 4);
        value[1] = mbus_data_bcd_decode_hex(data->cnt2_val, 4);
    } else {
        ival = 0; rc = mbus_data_int_decode(data->cnt1_val, 4, &ival); value[0] = ival;
        if (rc < 0) EPRINTF("%s: process_mbus_data_fixed, mbus_data_int_decode failed for value 1",meter->name);
        ival = 0; rc = mbus_data_int_decode(data->cnt2_val, 4, &ival); value[1] = ival;
        if (rc < 0) EPRINTF("%s: process_mbus_data_fixed, mbus_data_int_decode failed for value 2",meter->name);
    }

    setRegisterValue(meter,0,NULL,value[0]);
    setRegisterValue(meter,1,NULL,value[1]);

    free(medium); free(func); free(unit[0]); free(unit[1]);
    exit(1);
}


int process_mbus_data_variable(meter_t * meter, mbus_data_variable *data, int verboseMsg) {
    mbus_data_record *record;
    int i,tariff;
    char *unit = NULL;
    char *func = NULL;
    char *value = NULL;
    char *medium = NULL;
    char *manufacturer = NULL;

    medium = strdup(mbus_data_variable_medium_lookup(data->header.medium));
    manufacturer = strdup(mbus_decode_manufacturer(data->header.manufacturer[0], data->header.manufacturer[1]));

    if (verboseMsg) printf("%s (manufacturer: %s, version: %d, status: %d, medium: %s, access: %d)\n",meter->name,manufacturer,data->header.version,data->header.status,medium,data->header.access_no);

    for (record = data->record, i = 0; record; record = (mbus_data_record *)record->next, i++) {
        free(unit); unit = NULL;
        free(func); func = NULL;
        free(value); value = NULL;
        tariff = 0;
        if (record->drh.dib.dif == MBUS_DIB_DIF_MANUFACTURER_SPECIFIC) { // MBUS_DIB_DIF_VENDOR_SPECIFIC
            LOGN(3,"%s: MBUS_DIB_DIF_VENDOR_SPECIFIC, record id %d ignored",meter->name,i);
        }
        else if (record->drh.dib.dif == MBUS_DIB_DIF_MORE_RECORDS_FOLLOW) {
            LOGN(2,"%s: MBUS_DIB_DIF_MORE_RECORDS_FOLLOW not yet implemented",meter->name);
        } else {
            func = strdup(mbus_data_record_function(record));
            unit = strdup(mbus_vib_unit_lookup(&(record->drh.vib)));
            tariff = mbus_data_record_tariff(record);
        }
        value = strdup(mbus_data_record_value(record));
        if (verboseMsg) printf(" Record %d, value: %s, type: %s, unit: %s, function: %s, tariff: %d\n",i,value,mbus_data_record_type(record),unit,func,tariff);
        setRegisterValue(meter,i,value,0);
    }
    free(unit); free(func); free(value); free(medium); free(manufacturer);
    return 0;
}


int queryMeter(int verboseMsg, meter_t *meter) {
	meterRegisterRead_t *meterRegisterRead;
	int res;
	char format[50];
	mbus_frame reply;
    mbus_frame_data reply_data;


	if (meter->disabled) return 0;
	if (!meter->hostname)
		if (meter->mbusAddress == -1 && meter->mbusId == -1) return 0;		// virtual meter with formulas only

	if (verboseMsg) {
        if (verboseMsg > 1) printf("\n");
		if (meter->hostname) printf("Query \"%s\" @ TCP %s:%s, Mbus address %d\n",meter->name,meter->hostname,meter->port == NULL ? "502" : meter->port,meter->mbusAddress);
		else printf("Query \"%s\" @ mbus serial address %d\n",meter->name,meter->mbusAddress);
	} else
		VPRINTFN(2,"%s: queryMeter Mbus address %d",meter->name,meter->mbusAddress);

	// tcp open retry
	if (meter->isTCP) {
		meter->mb = mbusTCP_open (meter->hostname,meter->port);	// get it from the pool or create/open if not already in list of connections
		if(meter->mb == NULL) {
			EPRINTFN("%s: connect to %s:%d failed, will retry later",meter->name,meter->hostname,meter->port);
			return -555;
		}
	} else msleep(50);


	// reset read flags
	meter->meterHasBeenRead = 0;

	meterRegisterRead = meter->registerRead;
	while (meterRegisterRead) {
		meterRegisterRead->hasBeenRead = 0;
		meterRegisterRead->fvalue = 0;
		meterRegisterRead = meterRegisterRead->next;
	}

	assert (meter->mb != NULL);
	assert (*meter->mb != NULL);

	if (mbus_send_request_frame(*meter->mb, meter->mbusAddress) == -1) {
        EPRINTFN("Failed to send M-Bus request frame for meter %s @ address %d",meter->name,meter->mbusAddress);
        return -1;
    }

    if (mbus_recv_frame(*meter->mb, &reply) != MBUS_RECV_RESULT_OK) {
        EPRINTFN("Failed to receive M-Bus response frame for meter %s @ address %d",meter->name,meter->mbusAddress);
        return -1;
    }

    if (mbus_frame_data_parse(&reply, &reply_data) == -1) {
        EPRINTFN("M-bus data parse error on meter %s @ %d: %s",meter->name,meter->mbusAddress,mbus_error_str());
        return -1;
    }

   	if (reply.type == MBUS_DATA_TYPE_ERROR) {
		EPRINTFN("mbus_frame_data_parse returned MBUS_DATA_TYPE_ERROR, meter %s @ %d: %s",meter->name,meter->mbusAddress);
        return -1;
	}

    // process

    if (reply_data.type == MBUS_DATA_TYPE_FIXED) {
        res = process_mbus_data_fixed(meter, &(reply_data.data_fix),verboseMsg);
        if (!res) return res;
	} else
	if (reply_data.type == MBUS_DATA_TYPE_VARIABLE) {
        res = process_mbus_data_variable(meter, &(reply_data.data_var),verboseMsg);
        if (!res) return res;
	} else {
		EPRINTFN("unknown data type in returned data, meter %s @ %d: %s",meter->name,meter->mbusAddress);
        return -1;
	}

    if (reply_data.data_var.record)
        mbus_data_record_free(reply_data.data_var.record); // free's up the whole list

#ifndef DISABLE_FORMULAS
	// local formulas
	executeMeterTypeFormulas(verboseMsg,meter);
#endif

	if (verboseMsg > 1) {
		meterRegisterRead = meter->registerRead;
		while (meterRegisterRead) {
			if (meterRegisterRead->isInt) {
#ifdef BUILD_32
				printf(" %-20s %10lld\n",meterRegisterRead->registerDef->name,meterRegisterRead->ivalue);
#else
				printf(" %-20s %10d\n",meterRegisterRead->registerDef->name,(int)meterRegisterRead->fvalue);
#endif
			} else {
				if (meterRegisterRead->registerDef->decimals == 0)
					strcpy(format," %-20s %10.0f\n");
				else
					sprintf(format," %%-20s %%%d.%df\n",11+meterRegisterRead->registerDef->decimals,meterRegisterRead->registerDef->decimals);
				printf(format,meterRegisterRead->registerDef->name,meterRegisterRead->fvalue);
			}
			meterRegisterRead = meterRegisterRead->next;
		}
	}

	// mark complete
	meter->meterHasBeenRead = 1;
	return 0;
}


int queryMeters(int verboseMsg) {
	meter_t *meter;
	int res;
	int numMeters = 0;

	setfvalueInfluxLast ();   // set last value for all meter registers
	//  query
	meter = meters;
	while (meter) {
		if (! meter->isTCP) msleep(100);
		res = queryMeter(verboseMsg,meter);
		if (res != 0) {
			EPRINTFN("%s: query failed",meter->name);
		} else {
			numMeters++;
		}
		meter = meter->next;
	}
#ifndef DISABLE_FORMULAS
	// meter formulas
	meter = meters;
	while (meter) {
		if (! meter->disabled) executeMeterFormulas(verboseMsg,meter);
		//if (res != 0) EPRINTFN("%s: execute formulas failed",meter->name);
		meter = meter->next;
	}
#endif
    // handle influxWriteMult
    setfvalueInflux();  // set for all meters after formulas
    meter = meters;
	while (meter) {
		if (! meter->disabled) executeInfluxWriteCalc(verboseMsg,meter);
		meter = meter->next;
	}
	return numMeters;
}


// test if we can find the first defined serial mbus meter, returns 1 if meter found
int testSerialpresent() {
    meter_t *meter = meters;
	int res;
	mbus_frame reply;
	mbus_handle ** mb = mbusSerial_getmh();


	while (meter) {
        if (!meter->isTCP && (meter->mbusAddress != -1 || meter->mbusId != -1)) { // serial and no virtual meter with formulas only
            // todo: by Id
            res = mbus_ping_address(*mb, &reply, meter->mbusAddress);
			if (res == MBUS_RECV_RESULT_INVALID) {
				/* check for more data (collision) */
				mbus_purge_frames(*mb);
				LOGN(1,"Collision at address %d\n", meter->mbusAddress);
				return 0;
			}

			if (mbus_frame_type(&reply) == MBUS_FRAME_TYPE_ACK) {
				/* check for more data (collision) */
				if (mbus_purge_frames(*mb)) {
					LOGN(1,"Collision at address %d", meter->mbusAddress);
					return 0;
				}
				LOGN(1,"Found a M-Bus device at address %d", meter->mbusAddress);
			}
        }
        meter = meter->next;
    }
    EPRINTFN("try: unable to try because of no serial mbus meters defined");
    return 0;
}

