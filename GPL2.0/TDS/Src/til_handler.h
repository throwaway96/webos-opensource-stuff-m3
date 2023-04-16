// (c) Copyright 2013 LG Electronics, Inc.

#include <pbnjson.hpp>
#include <lunaservice.h>
#include <glibmm.h>

#define DEFAULT_APN "virtualapn.com"
#define NULL_APN	""

struct pri_context;

struct tds_context {
    const char *apn_set;
    struct pri_context *pre_context;
};

class ApnInfo {
private:
    const Glib::ustring mApnName;

public:
    ApnInfo(const Glib::ustring &apnName);
};

bool til_luna_ConnectDataService(void *ctx_data);

bool til_luna_DisconnectDataService(void *ctx_data);

bool til_luna_ApnUpdate(void *ctx_data, const char *apn_data);

bool til_luna_DataConnectionStatusQuery(void *ctx_data);

bool til_luna_IsTelephonyReady(void *ctx_data);

bool til_luna_NetworkStatusQuery(void *ctx_data);

bool til_luna_ServiceStatusHandler(LSHandle *sh, const char *serviceName, bool connected, void *ctx_data);


