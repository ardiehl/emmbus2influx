#ifndef GLOBAL_H_INCLUDED
#define GLOBAL_H_INCLUDED

#include <limits.h>

#if ( __WORDSIZE == 64 )
#define BUILD_64
#endif

#if ( __WORDSIZE == 32 )
#define BUILD_32
#endif

#if ( __WORDSIZE == 16 )
#define BUILD_16
#endif

#if ( __WORDSIZE == 8 )
#define BUILD_8
#endif


#endif // GLOBAL_H_INCLUDED
