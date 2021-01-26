#ifndef __ERROR_HANDLING__H__
#define __ERROR_HANDLING__H__

#include <stdio.h>

/******************************************************************************
 * Error-handling macros
 *****************************************************************************/
typedef enum {
    RC_FAILED = -1,
    RC_OK = 0,
} Retcode;

#define CHECK(cond)                                                  \
    do {                                                             \
        if (!(cond)) {                                               \
            fprintf(stderr, "%s:%d error, failed to check \"%s\"\n", \
                __func__, __LINE__, #cond);                          \
            goto fail;                                               \
        }                                                            \
    } while (0)

#endif //__ERROR_HANDLING__H__
