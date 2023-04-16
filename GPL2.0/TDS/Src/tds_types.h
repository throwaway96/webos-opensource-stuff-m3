// (c) Copyright 2013 LG Electronics, Inc.

#ifndef __TDS_TYPES_H
#define __TDS_TYPES_H

#ifndef    FALSE
#define    FALSE    (0)
#endif

#ifndef    TRUE
#define    TRUE    (!FALSE)
#endif

typedef int        tds_bool_t;

#define TDS_MAX_MCC_LENGTH 3
#define TDS_MAX_MNC_LENGTH 3

typedef void (*tds_destroy_func)(void *data);

enum tds_clir_option {
    TDS_CLIR_OPTION_DEFAULT = 0,
    TDS_CLIR_OPTION_INVOCATION,
    TDS_CLIR_OPTION_SUPPRESSION,
};

enum tds_error_type {
    TDS_ERROR_TYPE_NO_ERROR = 0,
    TDS_ERROR_TYPE_CME,
    TDS_ERROR_TYPE_CMS,
    TDS_ERROR_TYPE_CEER,
    TDS_ERROR_TYPE_SIM,
    TDS_ERROR_TYPE_FAILURE,
};

enum tds_disconnect_reason {
    TDS_DISCONNECT_REASON_UNKNOWN = 0,
    TDS_DISCONNECT_REASON_LOCAL_HANGUP,
    TDS_DISCONNECT_REASON_REMOTE_HANGUP,
    TDS_DISCONNECT_REASON_ERROR,
};

struct tds_error {
    enum tds_error_type type;
    int error;
};


#endif /* __TDS_TYPES_H */
