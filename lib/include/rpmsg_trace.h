

#ifndef _rpmsg_trace_h
#define _rpmsg_trace_h

#include <stdarg.h>

#if defined __cplusplus
extern "C" {
#endif

#include "rpmsg_config.h"

#define RLTRACE_ENTRY if(RLTRACE_ON && (LOCAL_TRACE)) RL_PRINTF("%s(): entry\n", __PRETTY_FUNCTION__)
#define RLTRACE_EXIT if(RLTRACE_ON & LOCAL_TRACE) RL_PRINTF("%s(): exit @line %d\n", __PRETTY_FUNCTION__, __LINE__)
#define RLTRACE if(RLTRACE_ON & LOCAL_TRACE) RL_PRINTF("%s():%d\n", __PRETTY_FUNCTION__, __LINE__)
#define RLTRACEF(...) if(RLTRACE_ON & LOCAL_TRACE) do { \
    RL_PRINTF("%s():%d: ", __PRETTY_FUNCTION__, __LINE__); \
    RL_PRINTF(__VA_ARGS__); } while (0)

static inline void rl_dump(void* buf, uint16_t len, uint16_t n_per_line)
{
    RL_ASSERT( NULL != buf );
    char* charBuf = (char*)buf;
    uint16_t nPrinted = 0;
    uint16_t nPerLine = n_per_line;
    while( nPrinted < len )
    {
        uint16_t nPrintedThisLine = 0;
        uint16_t remaining = len - nPrinted;
        if( remaining < n_per_line )
        {
            nPerLine = remaining;
        }
        while( nPrintedThisLine < nPerLine )
        {
            RL_PRINTF("%2x ", (uint8_t)charBuf[nPrintedThisLine] );
            nPrintedThisLine++;
            nPrinted++;
        }
        RL_PRINTF("\n");
    }
}

#define L_RLTRACE_ENTRY do { if (LOCAL_TRACE) { RLTRACE_ENTRY; }} while(0)
#define L_RLTRACE_EXIT do { if (LOCAL_TRACE) { RLTRACE_EXIT; }} while(0)
#define L_RLTRACE do { if (LOCAL_TRACE) { RLTRACE; }} while(0)
#define L_RLTRACEF(...) do { if (LOCAL_TRACE) { RLTRACEF( __VA_ARGS__); }} while(0)


#if defined __cplusplus
}
#endif

#endif // _rpmsg_trace_h