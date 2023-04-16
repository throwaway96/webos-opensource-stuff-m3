// (c) Copyright 2013 LG Electronics, Inc.

#ifndef __TDS_GPRS_H
#define __TDS_GPRS_H

#include <stdint.h>
#include <gdbus.h>
#include <string>

#include "tds_types.h"
#include "gprs-context.h"

using namespace std;

struct tds_gprs;
struct context_settings;

#define SETTINGS_STORE "gprs"
#define SETTINGS_GROUP "Settings"
#define MAX_CONTEXT_NAME_LENGTH 127
#define MAX_MESSAGE_PROXY_LENGTH 255
#define MAX_MESSAGE_CENTER_LENGTH 255
#define MAX_CONTEXTS 256
#define SUSPEND_TIMEOUT 8
#define SIGNAL_KILL 9

struct pri_context {
    tds_bool_t active;
    tds_bool_t inuse;
    enum tds_gprs_context_type type;
    char name[MAX_CONTEXT_NAME_LENGTH + 1];
    char message_proxy[MAX_MESSAGE_PROXY_LENGTH + 1];
    char message_center[MAX_MESSAGE_CENTER_LENGTH + 1];
    unsigned int id;
    char *path;
    char *key;
    char *proxy_host;
    uint16_t proxy_port;
    DBusMessage *pending;
    struct tds_gprs_primary_context context;
    struct tds_gprs *gprs;
    struct context_settings *settings;
};

typedef struct _MONITOR_PPPD_INFO
{
	GPid procID;
	guint pppWatchSrcID;
    char *interface;
	char *portOfComm;
	char *speed;
	char *otherParams[10];
    struct ipv4_settings *ipv4;
	gint timeout_id;
	static GMutex mutex;
	struct tds_error tilErr;
	void *tdsData;
	int ipInfoRetryCount;
	bool bPPPStatus;
	bool bPPPKilling;
	bool bPdpActivateInProgress;

} sPppdInfo;

enum
{
	PPP_CONFIG_FAILED = -1,
	PPP_CONFIG_SUCCESS= 0,
};



typedef void (*tds_gprs_status_cb_t)(const struct tds_error *error, int status,
        void *data);

typedef void (*tds_gprs_cb_t)(const struct tds_error *error, void *data);

enum gprs_suspend_cause {
    GPRS_SUSPENDED_DETACHED,
    GPRS_SUSPENDED_SIGNALLING,
    GPRS_SUSPENDED_CALL,
    GPRS_SUSPENDED_NO_COVERAGE,
    GPRS_SUSPENDED_UNKNOWN_CAUSE,
};

enum network_registration_status {
    NETWORK_REGISTRATION_STATUS_NOT_REGISTERED =   0,
    NETWORK_REGISTRATION_STATUS_REGISTERED =       1,
    NETWORK_REGISTRATION_STATUS_SEARCHING =        2,
    NETWORK_REGISTRATION_STATUS_DENIED =           3,
    NETWORK_REGISTRATION_STATUS_UNKNOWN =          4,
    NETWORK_REGISTRATION_STATUS_ROAMING =          5,
};

struct tds_gprs *tds_gprs_create(struct tds_modem *modem, unsigned int vendor);

void tds_gprs_add_context(struct tds_gprs *gprs, struct pri_context *gc);

void til_handle_Activate(bool ret, void *data);
void til_handle_Deactivate(bool ret, void *data);
void til_handle_ConnStatus(string &ConnState, void *data);
void til_handle_OnlineStatus(bool online, void *data);
void til_handle_EmergencyStatus(bool emergency, void *data);
void til_handle_PowerStatus(bool powered, void *data);
void til_handle_AttachedStatus(bool attached, void *data);
void til_handle_Bearer(string &DataType, void *data);
void til_handle_CommonError(bool ret, void *data);
void til_handle_ApnSet(bool ret, void *data);
void til_sends_fake_apn(void *data,char *i_strAPN);

static void
	cb_child_watch( GPid  pid,
					gint  status,
					gpointer *i_pData );

static void
cb_execute(sPppdInfo *data);

static gboolean
cb_timeout( gpointer *i_pData );


#endif /* __TDS_GPRS_H */
