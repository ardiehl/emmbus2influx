/*
 * parser.h
 *
 * Copyright 2022 Armin Diehl <ad@ardiehl.de>
 *
 * Simple text file parser used to read config file
 *
 * License: GPL
 *
*/


#ifndef PARSER_H_INCLUDED
#define PARSER_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif


#include <stdio.h>

#define TK_EOF       -10
#define TK_EOL       -11
#define TK_INTVAL    -12
#define TK_FLOATVAL  -13
#define TK_STRVAL    -14
#define TK_IDENT     -15
#define TK_SECTION   -16


typedef struct buf_t buf_t;
struct buf_t {
	int allocSize;
	int len;
	char *buf;
	char *bufEnd;
};


void buf_init(buf_t * b, int allocSize);
void buf_addChar(buf_t * b, char c);
void buf_clear(buf_t * b);

typedef struct parserToken_t parserToken_t;
struct parserToken_t {
	char *name;
	int value;
	parserToken_t *next;
};



typedef struct parser_t parser_t;
struct parser_t {
	char *charTokens;
	parserToken_t * tokens;
	int col;
	char *currPos;
	char *currLine;
	int currLineNo;
	FILE *fh;
	char *fileName;
	int iVal;
	double fVal;
	char *strVal;
	int firstTokenInLine;
};


/**
 parser_getTokenTxt
 @param a pointer to an initialized parser struct
 @param token value
 @return pointer to a string, needs to be freed
 */
char * parserGetTokenTxt(parser_t * pa, int tk);


/**
 * Initialize the parser
 * @param a string of single character tokens. parserGetToken will return these starting with 0
 * @param Tokens, pairs of string and value, the last one have to be NULL
 * @return a pointer to parser_t, to be free'd using parserFree
 */
parser_t * parserInit (const char * charTokens, ...);

void parserFree (parser_t * pa);

/**
 parserBegin
 @param a pointer to an initialized parser struct
 @param name of the file to be read
 @param 1 to skip lines until the first line that starts with [
 @return negative value indicates an error
 */
int parserBegin (parser_t * pa, const char * fileName, int skipToSectionStart);

/**
 peek char
 @param a pointer to an initialized parser struct
 @return current character or less than 0 if no char is available (e.g. TK_EOL, TK_EOF)
 */
int pch (parser_t * pa);

/**
 peek next char
 @param a pointer to an initialized parser struct
 @return next character or less than 0 if no char is available (e.g. TK_EOL, TK_EOF)
 */
int pchnext (parser_t * pa);

/**
 get char
 @param a pointer to an initialized parser struct
 @return current character or less than 0 if no char is available (e.g. TK_EOL, TK_EOF)
 */
int gch (parser_t * pa);


/**
 get token
 @param a pointer to an initialized parser struct
 @return next token and values (iVal,fVal or strVal in pa)
 */
int parserGetToken (parser_t *pa);

/**
 * scans for a section start, blank lines will be skipped
 * @param a pointer to an initialized parser struct
 * @return either TK_SECTION or TK_EOF
 */
int parserExpectSection (parser_t * pa);

/**
 shows the filename, the current position in the file as well as txt and exits
 @param a pointer to an initialized parser struct
 @param error message or format string (without newline)
 @
 */
void parserError(parser_t *pa, const char *format, ...);

/**
 if next token is an integer, return the value otherwise show error and exit
 @param a pointer to an initialized parser struct
 @return integer value
 */
int parserExpectInteger(parser_t * pa);

/**
 if next token is not the given one show error and exit
 @param a pointer to an initialized parser struct
 @return expected token value
 */
int parserExpect(parser_t * pa, int tkExpected);


/**
 if next token is not the given one or EOL show error and exit
 @param a pointer to an initialized parser struct
 @return expected token value
 */
int parserExpectOrEOL(parser_t * pa, int tkExpected);

#ifdef __cplusplus
}
#endif


#endif // PARSER_H_INCLUDED
