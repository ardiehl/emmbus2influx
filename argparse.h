#ifndef ARGPARSE_H
#define ARGPARSE_H

#ifdef __cplusplus
extern "C" {
#endif


/*
Argparser that parses short and long options and reads long options
from a configuration file.

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

Dec 20, 2021 Armin:
 initial version
*/

/* macros for options struct creation

 AP_START(structVariableName)
   generates the start of the struct
 AP_END;
   ends the struct
 AP_HELP
   predefined -h --help with callback to argParse_showHelp

 AP_xxx_yyyyyy[_CB]
	 |     |     |
	 |     |     |----- _CB last parameter is a callback function that will be called after the value has been set
	 |     |-----------  INTVAL   an integer argument is required
     |                   INTVALF  flag: each option will increment or -f- decrements
     |                   INTVALFO flag but value can optionally specified
     |                   STRVAL   string value
     |----------------   OPT option is optional
                         REQ required option
  Parameter
    1:  1=show default value (or value from config file) value in help
    2:	short option char or index (>255) (int or char) (0..32 or >255 will not be shown as a short option but can be used in callbacks for long options)
    3:	long option, can be NULL
    4:	Pointer to int or string
		if not NULL int or string will be set. Strings will be malloc'd or free'd if there is a value already
		NULL only makes sense if no callback is specified
	5:	Text that will he shown in help, can be NULL

	example:
	AP_START(options)
		AP_REQ_STRVAL   (1,'h',"hostname",&hostname      ,"hostname to connect to")
		AP_OPT_INTVAL   (1,'p',"port"    ,&port          ,"port to use when connecting to hostname")
		AP_OPT_INTVALFO (0,'v',"verbose" ,&verboseLevel  ,"increment or set verbosity level")
	EP_END;

 */

#if defined(__cplusplus)
 extern "C" {
#endif


#define AP_OPT_INTVAL(shval,short,long,intPtr,help) { shval, OPT_OPT, short, (char *)long, ARG_REQ, ARGTYPE_INT, intPtr, NULL, NULL, (char *)help },
#define AP_REQ_INTVAL(shval,short,long,intPtr,help) { shval, OPT_REQ, short, (char *)long, ARG_REQ, ARGTYPE_INT, intPtr, NULL, NULL, (char *)help },
#define AP_OPT_INTVALF(shval,short,long,intPtr,help) { shval, OPT_OPT, short, (char *)long, ARG_NO, ARGTYPE_INT, intPtr, NULL, NULL, (char *)help },
#define AP_REQ_INTVALF(shval,short,long,intPtr,help) { shval, OPT_REQ, short, (char *)long, ARG_NO, ARGTYPE_INT, intPtr, NULL, NULL, (char *)help },
#define AP_OPT_INTVALFO(shval,short,long,intPtr,help) { shval, OPT_OPT, short, (char *)long, ARG_OPT, ARGTYPE_INT, intPtr, NULL, NULL, (char *)help },
#define AP_REQ_INTVALFO(shval,short,long,intPtr,help) { shval, OPT_REQ, short, (char *)long, ARG_OPT, ARGTYPE_INT, intPtr, NULL, NULL, (char *)help },
#define AP_OPT_STRVAL(shval,short,long,strPtr,help) { shval, OPT_OPT, short, (char *)long, ARG_REQ, ARGTYPE_STR, NULL, strPtr, NULL, (char *)help },
#define AP_REQ_STRVAL(shval,short,long,strPtr,help) { shval, OPT_REQ, short, (char *)long, ARG_REQ, ARGTYPE_STR, NULL, strPtr, NULL, (char *)help },

#define AP_OPT_INTVAL_CB(shval,short,long,intPtr,help,cb) { shval, OPT_OPT, short, (char *)long, ARG_REQ, ARGTYPE_INT, intPtr, NULL, cb, (char *)help },
#define AP_REQ_INTVAL_CB(shval,short,long,intPtr,help,cb) { shval, OPT_REQ, short, (char *)long, ARG_REQ, ARGTYPE_INT, intPtr, NULL, cb, (char *)help },
#define AP_OPT_INTVALF_CB(shval,short,long,intPtr,help,cb) { shval, OPT_OPT, short, (char *)long, ARG_NO, ARGTYPE_INT, intPtr, NULL, cb, (char *)help },
#define AP_REQ_INTVALF_CB(shval,short,long,intPtr,help,cb) { shval, OPT_REQ, short, (char *)long, ARG_NO, ARGTYPE_INT, intPtr, NULL, cb, (char *)help },
#define AP_OPT_INTVALFO_CB(shval,short,long,intPtr,help,cb) { shval, OPT_OPT, short, (char *)long, ARG_OPT, ARGTYPE_INT, intPtr, NULL, cb, (char *)help },
#define AP_REQ_INTVALFO_CB(shval,short,long,intPtr,help,cb) { shval, OPT_REQ, short, (char *)long, ARG_OPT, ARGTYPE_INT, intPtr, NULL, cb, (char *)help },
#define AP_OPT_STRVAL_CB(shval,short,long,strPtr,help,cb) { shval, OPT_OPT, short, (char *)long, ARG_REQ, ARGTYPE_STR, NULL, strPtr, cb, (char *)help },
#define AP_REQ_STRVAL_CB(shval,short,long,strPtr,help,cb) { shval, OPT_REQ, short, (char *)long, ARG_REQ, ARGTYPE_STR, NULL, strPtr, cb, (char *)help },

#define AP_START(STRUCTNAME) struct argParse_optionT STRUCTNAME[] = {
#define AP_END  { 0, OPT_OPT, -1 , NULL              ,ARG_NO,ARGTYPE_NONE,NULL,NULL, NULL              , (char *) NULL }}
#define AP_HELP { 0, OPT_OPT, 'h', (char *)"help"    ,ARG_NO,ARGTYPE_NONE,NULL,NULL, &argParse_showHelp, (char *) "show this help and exit" },
/*
	int showDefValue;
	argParse_optT optReqired;	// required or optional option
	int shortOption;			// short option if > ' ' && < 256 else index (-1 = end of list)
	char *longOption;
	argParse_argT argRequired;	// do we require an argument or is the argument optional ?
	argParse_typeT argType;
	int *intValue;				// pointer to integer, arg only increments or for arg with value
	char **strValue;			// pointer to a string pointer, will be freed before changed
	argParse_callbackT *cb;		// function that will be called
	char *help;					// help text for usage */

/**
 * struct to be used as s single linked list for storing optional args.
 */
typedef struct argParseOptArg {
  char *value;
  struct argParseOptArg *next;
} argParse_optArgT;

/**
 * struct to be used for maintaining argParse state. Needs to be allocated by calling argParse_init
 */
typedef struct {
	int numOptions;
	int numOptionsProcessed;
	int *optionsProcessed;		// array 1 if processed
	char *confFileName;
	int index;					// index in argv
	char *longOption;			// current long option string
	int shortOption;			// current short option char
	int lineNum;				// line number on config file if > 0
	int optArgsCount;			// number of opional arguments (without - or --) found
	argParse_optArgT *optArgs;
	int allowOptArgs;			// are optional args allowed
	int optIndex;				// index of current option
	const struct argParse_optionT *options;
	char *progName;				// without path, do not free
	char *helpTop;
	char *helpBottom;
	int helpCol0Size;
} argParse_handleT;

typedef enum { ARG_NO, ARG_OPT, ARG_REQ } argParse_argT;
typedef enum { ARGTYPE_NONE, ARGTYPE_STR, ARGTYPE_INT } argParse_typeT;
typedef enum { OPT_OPT, OPT_REQ } argParse_optT;

// callback, will be called after setting a value or if no int/str ptr was specified
typedef int argParse_callbackT(argParse_handleT *a, char * arg);


struct argParse_optionT {
	int showDefValue;
	argParse_optT optReqired;	// required or optional option
	int shortOption;			// short option if > ' ' && < 256 else index (-1 = end of list)
	char *longOption;
	argParse_argT argRequired;	// do we require an argument or is the argument optional ?
	argParse_typeT argType;
	int *intValue;				// pointer to integer, arg only increments or for arg with value
	char **strValue;			// pointer to a string pointer, will be freed before changed
	argParse_callbackT *cb;		// function that will be called
	char *help;					// help text for usage
};



/**
 * This function initializes an ArgParseHandle that will be used tio maintain the state of
 * the arg parser.
 * @param options The options array, can be created using the AP_ macros, required parameter.
 * Options need to be specified and are scanned within this function. ArgParser will not change
 * the option structs.
 * @param configFileName if exists, long options will be read from this file before parsing command line args. The file
 * may contain comment lines starting with #, blank lines or lines containing LONGOPT or LONGOPT= (starting at column 1).
 * If a line starts with [, argParse will stop reading the file and continue with command line args.
 * @param helpTop text will be shown at the top of the help output
 * @param helpBottom text will be shown after the options of the help output
 * helpTop and helpBottom are optional for showing help (may be NULL)
 */
argParse_handleT * argParse_init(const struct argParse_optionT *options, const char *confFileName, const char *helpTop, const char *helpBottom);

/**
 * This function free's all used memory by the arg parser as well as the handle itself
  * @param a pointer to an initialized argParseHandle
 */
void argParse_free (argParse_handleT *a);

/**
 * Parse the options as previously set by calling argParse_init
  * @param a: pointer to an initialized argParseHandle
  * @param argc: number of entries in argv
  * @param argv: array of command line options (argv[0] needs to contain the program name)
  * @param allowOptionalArgs: set to 1 if optional args (without - or --) are allowed. on
  *  return a->optArgsCount contains the number of optional arguments. These can by accessed by
  *  calling argParse_getOptArg(index). The returned char* pointer must not be free'd.
 */
int argParse (argParse_handleT *a, int argc, char *argv[], int allowOptionalArgs);

/**
 * Show the help for the given option only (one line)
  * @param a: pointer to an initialized argParseHandle
  * @param option: pointer to one option
 */
void argParse_showHelpOption (argParse_handleT *a, const struct argParse_optionT *option);

/**
 * Show the help. This function is specified as a callback to the -h/--help command
  * @param a: pointer to an initialized argParseHandle
  * @param arg: not used, can be NULL
 */
int argParse_showHelp(argParse_handleT *a, char * arg);

// get optional arg, returns NULL if index out of range
/**
 * Returns a char * to the optional argument with the given index or NULL. Do not free the returned pointer, modifying the string in the returned char * is ok.
  * @param a: pointer to an initialized argParseHandle
  * @param index: index of the arg (>=0, < a->optArgsCount)
 */
char * argParse_getOptArg (argParse_handleT *a, int index);

/**
 * Read a line from the given file. Returns NULL of EOF is reached. The returned char * needs to be free'ed
  * @param file: pointer to an opened text file
 */
char *readLineFromFile(FILE *file);


#if defined(__cplusplus)
 }
#endif

#ifdef __cplusplus
}
#endif


#endif
