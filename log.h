#ifndef LOG_H_INCLUDED
#define LOG_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif


#if defined(ESP_PLATFORM) || defined(ESP_EMU)
#define ESP
#endif

#ifndef ESP
#include <syslog.h>
#endif

#include <stdio.h>

extern int log_verbosity;
extern int log_syslog;

#define verbose log_verbosity

void log_setVerboseLevel (int verboseLevel);
void log_incVerboseLevel();
void log_setSyslogTarget (const char * progName);
void log_close();
void log_fprintf(FILE *stream, const char *format, ...);
void log_fprintfn(FILE *stream, const char *format, ...);

#define VPRINTF(LEVEL, FORMAT, ...) if (log_verbosity >= LEVEL) log_fprintf(stdout, FORMAT, ##__VA_ARGS__)
#define VPRINTFN(LEVEL, FORMAT, ...) if (log_verbosity >= LEVEL) log_fprintfn(stdout, FORMAT, ##__VA_ARGS__)
#define PRINTF(FORMAT, ...) if (log_verbosity >= 0) log_fprintf(stdout, FORMAT, ##__VA_ARGS__)
#define EPRINTF(FORMAT, ...)  log_fprintf(stderr, FORMAT, ##__VA_ARGS__)
#define EPRINTFN(FORMAT, ...) log_fprintfn(stderr, FORMAT, ##__VA_ARGS__)
//#define LOG(...) PRINTF(__VA_ARGS__)
#define LOG VPRINTF
#define LOGN VPRINTFN

#ifdef __cplusplus
}
#endif


#endif // LOG_H_INCLUDED
