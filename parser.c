/*
 * parser.c
 *
 * Copyright 2022 Armin Diehl <ad@ardiehl.de>
 *
 * Simple text file parser used to read config file
 *
 * License: GPL
 *
 */


#include "parser.h"
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <ctype.h>
#include "argparse.h"


void buf_init(buf_t * b, int allocSize) {
	if (allocSize < 1) allocSize = 256;
	b->buf = calloc(1,allocSize);
	if (b->buf == NULL) {
		fprintf(stderr,"malloc failed\n");
		exit(1);
	}
	b->allocSize = allocSize;
	b->len = 0;
	b->bufEnd = b->buf;
}


void buf_addChar(buf_t * b, char c) {
	if (b->len+1 >= b->allocSize) {
		b->allocSize = b->allocSize * 2;
		b->buf = realloc(b->buf,b->allocSize);
		if (b->buf == NULL) {
			fprintf(stderr,"realloc failed\n");
			exit(1);
		}
	}
	//printf("buf_addChar %c len=%d, allocSize: %d (%s)\n",c,b->len,b->allocSize,b->buf);
	*b->bufEnd = c;
	b->bufEnd++;
	*b->bufEnd = 0;
	b->len++;
}


void buf_clear(buf_t * b) {
	if (b->buf) {
		free(b->buf); b->buf = NULL;
		b->allocSize = 0;
		b->len = 0;
		b->bufEnd = NULL;
	}
}


char * parserGetTokenTxt(parser_t * pa, int tk) {
	parserToken_t *tokens = pa->tokens;
	char st[2];

	switch (tk) {
		case TK_EOF:		return strdup("EOF");
		case TK_EOL:		return strdup("EOL");
		case TK_INTVAL:		return strdup("int value");
		case TK_FLOATVAL:	return strdup("float value");
		case TK_STRVAL:		return strdup("string");
		case TK_IDENT:		return strdup("identifier");
		case TK_SECTION:	return strdup("section");
	}

	while (tokens) {
		if (tokens->value == tk) return strdup(tokens->name);
		tokens = tokens->next;
	}

	if (tk > 0 && tk <= strlen(pa->charTokens)) {
		st[0] = *(pa->charTokens + tk - 1);
		st[1] = 0;
		return strdup(st);
	}
	return strdup("unknown");

}


parser_t * parserInit (const char * charTokens, ...) {
	parser_t * pa;
	parserToken_t * lastpt = NULL;
	va_list ap;
	char *token;
	int value;

	pa = calloc(1,sizeof(parser_t));

	va_start(ap, charTokens);
	token = va_arg(ap, char *);
	while (token) {
		value = va_arg(ap, int);
		//printf("Token: %s %d\n",token,value);
		if (pa->tokens == NULL) {
			pa->tokens = calloc(1,sizeof(parserToken_t));
			pa->tokens->name = strdup(token);
			pa->tokens->value = value;
			lastpt = pa->tokens;
		} else {
			lastpt->next = calloc(1,sizeof(parserToken_t));
			lastpt = lastpt->next;
			lastpt->name = strdup(token);
			lastpt->value = value;
		}
		token = va_arg(ap, char *);
	}
	va_end(ap);
	pa->charTokens = strdup(charTokens);
	return pa;

}



void parserFree (parser_t * pa) {
	parserToken_t *pt, *ptNext;

	pt = pa->tokens;
	while (pt) {
		ptNext = pt->next;
		free(pt->name);
		free(pt);
		pt = ptNext;
	}
	free(pa->charTokens);
	free(pa->currLine);
	free(pa->fileName);
	free(pa->strVal);
	free(pa);
}

void parserSkipNoise(parser_t * pa) {
	int c;

	c = pch(pa);
	while (c >= 0 && c <= 32) {
		c = pchnext(pa);
	}
}


int parserGetNextLine(parser_t * pa) {
	free(pa->currLine);
	pa->col = 1;
	pa->currLine = readLineFromFile(pa->fh);
	if (pa->currLine == NULL) {
		fclose(pa->fh); pa->fh = NULL;
		return -1;
	}
	pa->currLineNo++;

	// check for comment
	char *comment = pa->currLine;
	while (*comment && *comment != '#') {
		if (*comment == '"') {				// in a string, ignore until end of string to allow # in strings
			comment++;
			while (*comment && *comment != '"') comment++;
			if (*comment != '"') parserError(pa,"closing quote missing");
		}
		comment++;
	}
	if (*comment == '#') *comment = 0;


	pa->currPos = pa->currLine;
	parserSkipNoise(pa);
	pa->firstTokenInLine = 1;
	//printf("Line %d: '%s'\n",pa->currLineNo,pa->currPos);
	return 0;
}


int parserBegin (parser_t * pa, const char * fileName, int skipToSectionStart) {
	int rc;

	free (pa->currLine); pa->currLine = NULL;
	pa->currLineNo = 0;
	pa->col = 1;
	if (fileName == NULL) return -1;
	if (*fileName == 0) return -1;
	pa->fileName = strdup(fileName);
	pa->fh = fopen(fileName,"r");
	if (pa->fh == NULL) return -1;
	if (skipToSectionStart) {
		rc = parserGetNextLine(pa);
		while (rc >= 0) {
			if (pch(pa) == '[') break;
			rc = parserGetNextLine(pa);
		}
		if (rc < 0) {
			fclose(pa->fh); pa->fh = NULL;
			return rc;
		}
	} else {
		rc = parserGetNextLine(pa);
	}

	return rc;
}

// peek char
int pch (parser_t * pa) {
	if (pa == NULL) return TK_EOF;
	if (pa->fh == NULL) return TK_EOF;
	if (pa->currPos == NULL) return TK_EOL;
	if (*pa->currPos == 0) return TK_EOL;

	return *pa->currPos;
}

// get char
int gch (parser_t * pa) {
	int rc = pch(pa);

	if (rc > 0) { pa->currPos++; pa->col++; }
	return rc;
}

// peer next char
int pchnext (parser_t * pa) {
	gch(pa);
	return pch(pa);
}

void skipNoise (parser_t * pa) {
	int c;

	c = pch(pa);
	while (c > 0 && c <= 32) {
		c = pchnext(pa);
	}
}



void parserError(parser_t *pa, const char *format, ...) {
	int col,i;
	int bufSize = 256;
	int requiredBufSize;
	char *buf;
	va_list args;

	buf=calloc(1,bufSize+1);

    va_start(args, format);
	requiredBufSize = vsnprintf(buf,bufSize,format,args);

	if (requiredBufSize > bufSize) {
		buf=realloc(buf,requiredBufSize+2);
		va_start(args, format);
		vsnprintf(buf,requiredBufSize+1,format,args);
		va_end(args);
	}

	if (pa->currLine)
		if (*pa->currLine) {
			fprintf(stderr,"%s\n",pa->currLine);
			col = pa->col; if (col) col--;
			for (i=0;i<col;i++) fprintf(stderr," ");
			fprintf(stderr,"^\n");
			for (i=0;i<col;i++) fprintf(stderr," ");
			fprintf(stderr,"|\n");
		}

	fprintf(stderr,"%s: line %d, column %d - %s\n",pa->fileName,pa->currLineNo,pa->col,buf);
	free(buf);
	exit(1);
}


int hexNibble (char c) {
	if (c >= '0' && c <= '9') return c-'0';
	if (c >='a' && c <= 'f') return c-'a'+10;
	fprintf(stderr,"hexNibble: invalid character %c\n",c);
	exit(1);
}


void parser_expectChar(parser_t *pa, int c) {
	char st[40];
	st[0] = c;
	st[1] = 0;
	switch (c) {
		case TK_EOF:	strcpy(st,"EOF");
						break;
		case TK_EOL:	strcpy(st,"EOL");
						break;

	}
	strcat(st," expected");
	if (gch(pa) != c) {
		parserError(pa,st);
		exit(1);
	}
}



int parserGetToken (parser_t *pa) {
	int c;
	char *p;
	buf_t buf;
	int isFloat = 0;
	char ch;
	int rc;
	parserToken_t * tokens;

	//pa->iVal = 0;
	//pa->fVal = 0;
	//if (pa->strVal) { free(pa->strVal); pa->strVal = NULL; }

	if (pa->fh == NULL) return TK_EOF;

	skipNoise(pa);
	c = pch(pa);
	if (c < 0) {
		if (c == TK_EOL) parserGetNextLine(pa);
		return c;
	}

	if (pa->charTokens == NULL) {
		fprintf(stderr,"parser: char tokens not initialized\n");
		exit(1);
	}

	p = strchr(pa->charTokens,c);
	if (p) {
		gch(pa);
		return p - pa->charTokens + 1;	// found single char token, return the index
	}

	buf_init(&buf,256);

	if (pa->firstTokenInLine) {			// section ?
		if (c == '[') {
			c = pchnext(pa);
			while (c > 0 && c != ']') {
				buf_addChar(&buf,c);
				c = pchnext(pa);
			}
			parser_expectChar(pa,']');
			skipNoise(pa);
			parser_expectChar(pa,TK_EOL);
			free(pa->strVal);
			pa->strVal = buf.buf;
			return TK_SECTION;
		}
	}

	if (c >= '0' && c <= '9') {			// number, hex/decimal integer/float
		if (c == '0') {
			c = pchnext(pa);
			if (c == 'x') {				// hex starting with 0x
				c = pchnext(pa);
				ch = tolower(c);
				pa->iVal = 0;
				while ( c > 0 && ((ch >= 'a' && ch <= 'f') || (ch >= '0' && ch <= '9'))) {
					pa->iVal = (pa->iVal << 4) | hexNibble(ch);
					c = pchnext(pa);
					ch = tolower(c);
				}
				buf_clear(&buf);
				//printf("hexint: %d (0x%08x)\n",pa->iVal,pa->iVal);
				return TK_INTVAL;
			} else {
				buf_addChar(&buf,'0');
			}
		}
		// float or integer
		while((c >= '0' && c <= '9') || c == '.') {
			buf_addChar(&buf,c);
			if (c == '.') isFloat++;
			c = pchnext(pa);
		}
		if (isFloat) {
			if (isFloat > 1) parserError(pa,"invalid float number");
			rc = sscanf(buf.buf,"%lf",&pa->fVal);
			if (rc != 1) parserError(pa,"invalid float number");
			buf_clear(&buf);
			//printf("float: %1.2f\n",pa->fVal);
			return TK_FLOATVAL;
		}
		rc = sscanf(buf.buf,"%d",&pa->iVal);
		if (rc != 1) parserError(pa,"invalid integer number");
		buf_clear(&buf);
		//printf("int: %d (0x%08x)\n",pa->iVal,pa->iVal);
		return TK_INTVAL;
	}

	if (c == '"') {			// string
		c = pchnext(pa);
		while(c > 0 && c != '"') {
			buf_addChar(&buf,c);
			c = pchnext(pa);
		}
		parser_expectChar(pa,'"');
		free(pa->strVal);
		pa->strVal = buf.buf;
		buf.buf = NULL;
		//printf("string '%s'\n",pa->strVal);
		return TK_STRVAL;
	}



	// token or name
	while(c > ' ') {
		if (strchr(pa->charTokens,c)) break;
		buf_addChar(&buf,c);
		c = pchnext(pa);
	}

	//printf("Token or name: '%s'\n",buf.buf);
	tokens = pa->tokens;
	while (tokens) {
		if (strcmp(buf.buf,tokens->name) == 0) {
			buf_clear(&buf);
			//printf("Token '%s', %d\n",tokens->name,tokens->value);
			return tokens->value;
		}
		tokens = tokens->next;
	}

	//printf("Identifier: '%s'\n",buf.buf);
	free(pa->strVal);
	pa->strVal = buf.buf;
	return TK_IDENT;
}


int parserExpectSection (parser_t * pa) {
	int tk;

	tk = parserGetToken(pa);
	while (tk == TK_EOL) {
		tk = parserGetToken(pa);
	}
	if (tk == TK_EOF) return tk;
	if (tk != TK_SECTION) parserError(pa,"section expected");
	return tk;
}

int parserExpect(parser_t * pa, int tkExpected) {
	char st[255];
	int tk = parserGetToken(pa);
	if (tk != tkExpected) {
		strcpy(st,parserGetTokenTxt(pa, tkExpected));
		strcat(st," expected, got ");
		strcat(st,parserGetTokenTxt(pa,tk));
		if (tkExpected == TK_STRVAL && tk == TK_IDENT) strcat(st,", did you forget \" ?");
		parserError(pa,st);
	}
	return tk;
}


int parserExpectOrEOL(parser_t * pa, int tkExpected) {
	char st[255];
	int tk = parserGetToken(pa);
	if (tk != tkExpected && tk != TK_EOL) {
		strcpy(st,parserGetTokenTxt(pa, tkExpected));
		strcat(st," or EOL expected, got ");
		strcat(st,parserGetTokenTxt(pa,tk));
		parserError(pa,st);
	}
	return tk;
}






int parserExpectInteger(parser_t * pa) {
	parserExpect(pa,TK_INTVAL);
	return pa->iVal;
};


