/*
argparser that parses short and long options and reads long options
from a configuration file.

for usage, see argparser.h and atest.c

Copyright 2020 Armin Diehl <ad@ardiehl.de>

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

Dec 20, 2021 Armin: initial version
Dec 23, 2021 Armin: fixed memory leak when reading options from config file
Dec 26, 2021 Armin: Check for duplicate options
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "argparse.h"

#define MAX_LINE_LEN 256

argParse_handleT * argParse_init(const struct argParse_optionT *options, const char *confFileName, const char *helpTop, const char *helpBottom) {
	int i,j;
	int size;
	argParse_handleT *a = calloc(1,sizeof(argParse_handleT));
	if (a) {
		if (confFileName) a->confFileName = strdup(confFileName);
		a->options = options;
		if (helpTop) a->helpTop = strdup(helpTop);
		if (helpBottom) a->helpBottom = strdup(helpBottom);

		for (i=0;a->options[i].shortOption > -1; i++) {
			a->numOptions++;
			size = 5;
			if (a->options[i].shortOption) {
				size += 3;
			}
			if (a->options[i].longOption) {
				size += strlen(a->options[i].longOption);
				size += 2;
				if (a->options[i].shortOption) size += 2;
				if (a->options[i].argRequired == ARG_OPT) size +=2;  // []
				if (a->options[i].argRequired == ARG_REQ) size ++;
			}
			if (a->helpCol0Size < size) a->helpCol0Size = size;
		}
		a->optionsProcessed = calloc(a->numOptions,sizeof(int));
	}

	// check for duplicate options
	for (i=0;i<a->numOptions;i++) {
		if (options[i].shortOption > 0)
			for (j=0;j<a->numOptions;j++) {
				if (i != j)
					if (options[i].shortOption == options[j].shortOption) {
						fprintf(stderr,"duplicate short option, entry number %d, (%c %c) (%s %s)\n",i,options[i].shortOption,options[j].shortOption,options[i].longOption,options[j].longOption);
						exit(1);
					}
			}

		if (options[i].longOption)
			for (j=0;j<a->numOptions;j++) {
				if ((i != j) && (options[i].longOption))
					if (strcmp(options[i].longOption, options[j].longOption) == 0) {
						fprintf(stderr,"duplicate long option, entry number %d, (%s)\n",i,options[i].longOption);
						exit(1);
					}
			}
	}

	return a;
}


void argParse_freeOptArgs (argParse_handleT *a){
	argParse_optArgT *optArg;
	argParse_optArgT *i = a->optArgs;
	while (i) {
		optArg = i;
		i = i->next;
		free(optArg->value);
		free(optArg);
	}
	a->optArgs = NULL;
}


void argParse_free (argParse_handleT *a){
	if (!a) return;
	free(a->confFileName);
	free(a->longOption);
	free(a->helpTop);
	free(a->helpBottom);
	free(a->optionsProcessed);
	argParse_freeOptArgs (a);
	free(a);
}


void argParse_showHelpOption (argParse_handleT *a, const struct argParse_optionT *option) {
	char fmt[30];
	char st[100];
	char shortTxt[5];
	int hasShort;

	snprintf(fmt,sizeof(fmt),"%%-%ds %%s",a->helpCol0Size);

	strcpy(st,"  ");
	hasShort = 0;
	if ((option->shortOption > ' ') && (option->shortOption <= 255)) {
		hasShort++;
		shortTxt[2]=0; shortTxt[1]=option->shortOption; shortTxt[0] = '-';
		strcat(st,shortTxt);
	}
	if (option->longOption) {
		if (hasShort) strcat(st,", ");
		strcat(st,"--");
		strcat(st,option->longOption);
		if (option->argRequired == ARG_OPT) strcat(st,"[=]");
		else if (option->argRequired == ARG_REQ) strcat(st,"=");
	}
	printf(fmt,st,option->help);
	if (option->showDefValue) {
		if (option->strValue) {
			if (*option->strValue) printf(" (%s)",*option->strValue);
		} else
		if (option->intValue) printf(" (%d)",*option->intValue);
	}
	printf("\n");
}


int argParse_showHelp(argParse_handleT *a, char * arg) {
	int i;

	if (a->helpTop) printf(a->helpTop);
	printf("Usage: %s [OPTION]...\n",a->progName);

	for (i=0;i<a->numOptions; i++) {
		argParse_showHelpOption(a,&a->options[i]);

	}
	if (a->helpBottom) printf(a->helpBottom);
	exit(1);
}



char *readLineFromFile(FILE *file) {

	if (file == NULL) return NULL;
	int maximumLineLength = 256;
	char *lineBuffer = (char *)malloc(sizeof(char) * maximumLineLength);

	if (lineBuffer == NULL) return NULL;

	int ch = getc(file);
	if (ch == EOF) {
		free(lineBuffer);
		return NULL;
	}
	int count = 0;

	while ((ch != '\n') && (ch != EOF)) {
		if (count == maximumLineLength) {
			maximumLineLength += 256;
			lineBuffer = realloc(lineBuffer, maximumLineLength);
			if (lineBuffer == NULL) return NULL;
        }
		lineBuffer[count] = ch;
		count++;
		ch = getc(file);
	}
	lineBuffer[count] = '\0';
	return lineBuffer;
}

int argParse_addOptArg (argParse_handleT *a, char *value) {
	argParse_optArgT *optArg;
	argParse_optArgT *i;

	//printf("argParse_addOptArg: '%s' %d\n",value,a->optArgsCount);

	optArg = calloc(1,sizeof(argParse_optArgT));
	if (!optArg) return -1;
	optArg->value = strdup(value);
	if (a->optArgs == NULL) {		// first one
		a->optArgs = optArg;
		a->optArgsCount = 1;
		return 0;
	}
	i = a->optArgs;
	while (i->next) i = i->next;
	i->next = optArg;
	a->optArgsCount++;

	return 0;
}


// we are here:
// -v9   --verbose=9
//   |             |
int argParse_handleValue (argParse_handleT *a, char *arg, const struct argParse_optionT *option) {
	char * p;
	int valueSet = 0;
	int ival;
	int rc = 0;

	//printf("argParseHandleValue %c  long:'%s' arg:'%s'\n",a->shortOption, a->longOption, arg);

	if (*arg <= ' ') {
		if (option->argRequired == ARG_NO) {
			if (option->intValue)
				*option->intValue = *option->intValue + 1;
			if (option->cb) rc = option->cb(a,arg);			// optional callback
			a->numOptionsProcessed++;
			a->optionsProcessed[a->optIndex]++;
			return rc;
		}
		if (option->argRequired == ARG_OPT) {
			if (option->intValue)
				*option->intValue = *option->intValue + 1;
			if (option->cb) rc = option->cb(a,arg);
			a->numOptionsProcessed++;
			a->optionsProcessed[a->optIndex]++;
			return rc;
		}
		if (a->longOption) fprintf(stderr,"%s: Option --%s requires an argument\n",a->progName,a->longOption);
		else fprintf(stderr,"%s: Option -%c requires an argument\n",a->progName,a->shortOption);
		return -1;
	}

	p = arg + strlen(arg); p--;
	while ((*p <= ' ') && (*p != 0)) {		// skip noise at end of argument
		*p = 0; p--;
	}
	if (option->argType == ARGTYPE_STR) {
		if(option->strValue) {				// do we have a pointer to the destination string pointer ?
			free(*(option->strValue));
			*option->strValue = strdup(arg);
			//printf("Option: %s, len:%ld\n",arg,strlen(arg));
			valueSet++;
		}
	} else
	if (option->argType == ARGTYPE_INT) {
		p = arg;
		while (*p) {
			if ((*p < '0') || (*p > '9')) {
				if (a->longOption) fprintf(stderr,"%s: Option --%s requires a numeric argument, found '%s'\n",a->progName,a->longOption,arg);
				else fprintf(stderr,"%s: Option -%c requires a numeric argument, found '%s'\n",a->progName,a->shortOption,arg);
				return -1;
			}
			p++;
		}
		ival = strtol (arg,NULL,10);
		//printf ("ival(%s): %d, %d\n",arg,ival,errno);
		if (errno) {
			if (a->longOption) fprintf(stderr,"%s: Option --%s requires an integer argument, got '%s'\n",a->progName,a->longOption,arg);
			else fprintf(stderr,"%s: Option -%c requires an integer argument, got '%s'\n",a->progName,a->shortOption,arg);
			return -1;
		}
		if(option->intValue) {				// do we have a pointer destination int ?
			*option->intValue = ival;
			valueSet++;
		}
	}

	if ((!valueSet) && (!option->cb)) fprintf(stderr,"%s: option %c %s has no target value pointer and no callback and will therefore be ignored\n",a->progName,option->shortOption,option->longOption);

	if (option->cb) rc = option->cb(a,arg);
	a->numOptionsProcessed++;
	a->optionsProcessed[a->optIndex]++;
	return rc;
}

int argParseArg (argParse_handleT *a, char *arg) {
	if (!arg) return -1;
	char *p = arg;
	char *optNameStart;
	int len;
	int hasOpt=0;
	int i;
	int rc;

	if (*p == 0) return 0;
	a->shortOption = -1;
	if (a->longOption) fprintf(stderr,"argParseArg, possible memory leak, a->longOption\n");
	free (a->longOption);
	a->longOption = NULL;
	if (*p == '-') {
		p++;
		if (*p == '-') {	// long option
			p++;
			optNameStart = p; len=0;
			while ((*p > ' ') && (*p != '=')) {
				len++; p++;
			}
			a->longOption = calloc(1,len+1);
			memcpy(a->longOption,optNameStart,len);
			//printf("Long option: '%s'\n",a->longOption);
			if (*p == '=') {
				hasOpt++;
				p++;
			}

			a->optIndex = 0;
			while ((a->options[a->optIndex].shortOption > -1) && (strcmp(a->options[a->optIndex].longOption, a->longOption) != 0)) a->optIndex++;

			if (a->options[a->optIndex].shortOption > -1)
				if (strcmp(a->options[a->optIndex].longOption, a->longOption) == 0) {
					a->shortOption = a->options[a->optIndex].shortOption;
					if ((hasOpt) && (a->options[a->optIndex].argRequired == ARG_NO)) {
						fprintf(stderr,"%s: Option --%s does not require an argument\n",a->progName,a->longOption);
						free(a->longOption); a->longOption = NULL;
						return -1;
					}
					rc = argParse_handleValue (a, p, &a->options[a->optIndex]);
					free(a->longOption); a->longOption = NULL;
					return rc;
				}
			fprintf(stderr,"%s: Invalid option --%s\n",a->progName,a->longOption);
			free(a->longOption); a->longOption = NULL;
			return -1;

		} else {			// short option
			a->shortOption = *p;
			p++;
			a->optIndex = 0;
			while ((a->options[a->optIndex].shortOption > -1) && (a->options[a->optIndex].shortOption != a->shortOption)) a->optIndex++;
			if (a->options[a->optIndex].shortOption == a->shortOption) {
				if ((*p > ' ') && (a->options[a->optIndex].argRequired == ARG_NO)) {
					if ((*p == '-') && (a->options[a->optIndex].intValue)) {
						// for flags allow -f- to decrement value (if command line should override config file flag)
						rc = 0;
						*a->options[a->optIndex].intValue = *a->options[a->optIndex].intValue - 1;
						if (a->options[a->optIndex].cb) rc = a->options[a->optIndex].cb(a,"-");
						a->numOptionsProcessed++;
						a->optionsProcessed[a->optIndex]++;
						return rc;
					}
					fprintf(stderr,"%s: Option -%c does not require an argument\n",a->progName,a->shortOption);
					return -1;
				}
				return argParse_handleValue (a, p, &a->options[a->optIndex]);
			}
			fprintf(stderr,"%s: Invalid option -%c\n",a->progName,a->shortOption);
			return -1;
		}
	}
	// not starting with -
	if (a->lineNum > 0) {		// we are in the config file, accept LONGOPT without -- only
		while ((*p != 0) && (*p <= ' ')) p++;	// skip noise
		if (*p == 0) return 0;					// empty line
		if (*p == '#') return 0;				// comment line

		optNameStart = p; len=0;
		while ((*p > ' ') && (*p != '=')) {
			len++; p++;
		}
		a->longOption = calloc(1,len+1);
		memcpy(a->longOption,optNameStart,len);
		if (*p == '=') p++;

		while ((*p != 0) && (*p <= ' ')) p++;	// skip noise

		a->optIndex = -1;
		for (i=0;i<a->numOptions;i++)
			if (strcmp(a->options[i].longOption, a->longOption) == 0) {
				a->optIndex = i;
				a->shortOption = a->options[i].shortOption;
				break;
			}
		if (a->optIndex >= 0) {
			rc = argParse_handleValue (a, p, &a->options[a->optIndex]);
			free(a->longOption); a->longOption = NULL;
			return rc;
		}
		fprintf(stderr,"%s: Invalid option '%s' in line %d of %s\n",a->progName,a->longOption,a->lineNum,a->confFileName);
		free(a->longOption); a->longOption = NULL;
		return -1;
	}
	// we have an additional argument without -
	if (!a->allowOptArgs) {
		fprintf(stderr,"%s: Invalid argument '%s'\n",a->progName,arg);
		return -1;
	}
	return argParse_addOptArg (a,arg);
}


int argParse (argParse_handleT *a, int argc, char *argv[], int allowOptionalArgs) {
	FILE *fh;
	char *buf;
	int rc;
	int i;

	a->progName = argv[0];
	if (a->progName) {
		if (*a->progName != 0) {
			a->progName += strlen(argv[0]);
			a->progName--;
			while ((a->progName > argv[0]) && (*a->progName != '/')) a->progName--;
			if (*a->progName == '/') a->progName++;
		}
	}

	//printf("argc: %d prog: '%s' %s\n",argc,a->progName,argv[1]);
	if (!a) return -1;
	a->allowOptArgs = allowOptionalArgs;
	a->lineNum = 0;
	a->index = 0;
	if (a->confFileName) {
		if (strlen(a->confFileName) > 0) {
			fh = fopen(a->confFileName,"r");
			if (fh) {
				buf = readLineFromFile(fh);
				while (buf) {
					a->lineNum++;
					if (*buf == '[') {		// stop when [ found so the .conf may have multiple sections
						free(buf); buf=NULL;
					} else {
						rc = argParseArg (a,buf);
						free(buf);
						if (rc != 0) return rc;
						buf = readLineFromFile(fh);
					}
				}
				fclose(fh);
				a->lineNum = 0;
			}
		}
	}
	a->index++;
	while (a->index < argc) {
		rc = argParseArg (a,argv[a->index]);
		if (rc != 0) return rc;
		a->index++;
	}

	// check if all required args have been processed
	int numArgsMissing = 0;
	for (i=0;i<a->numOptions;i++) {
		if (a->options[i].optReqired == OPT_REQ)
			if (!a->optionsProcessed[i]) numArgsMissing++;
	}
	if (numArgsMissing>0) {
		fprintf(stderr,"%s: the following required option%s have not been specified\n",a->progName,numArgsMissing>1 ? "s" : "");
		for (i=0;i<a->numOptions;i++) {
			if (a->options[i].optReqired == OPT_REQ)
				argParse_showHelpOption(a,&a->options[i]);
		}
		return -9;
	}

	return 0;
}

char * argParse_getOptArg (argParse_handleT *a, int index) {
	int idx = 0;
	argParse_optArgT *arg = a->optArgs;

	while (arg) {
		if (idx == index) return arg->value;
		arg = arg->next; idx++;
	}
	return NULL;
}


