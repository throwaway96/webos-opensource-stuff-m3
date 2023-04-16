// (c) Copyright 2013 LG Electronics, Inc.

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <net/route.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <glib.h>

#include "tds.h"

#include "til_handler.h"
#include "logging.h"

#define GPRS_FLAG_ATTACHING 0x1
#define GPRS_FLAG_RECHECK 0x2
#define GPRS_FLAG_ATTACHED_UPDATE 0x4

pri_context *g_pri_context;

enum packet_bearer {
    PACKET_BEARER_NONE =		0,
    PACKET_BEARER_GPRS =		1,
    PACKET_BEARER_EGPRS =		2,
    PACKET_BEARER_UMTS =		3,
    PACKET_BEARER_HSUPA =		4,
    PACKET_BEARER_HSDPA =		5,
    PACKET_BEARER_HSUPA_HSDPA =	6,
    PACKET_BEARER_EPS =			7,
};

struct tds_gprs {
    GSList *contexts;
    tds_bool_t attached;
    tds_bool_t roaming_allowed;
    tds_bool_t powered;
    tds_bool_t suspended;
    int status;
    int flags;
    int bearer;
    unsigned int last_context_id;
    int netreg_status;
    char *imsi;
    struct tds_modem *modem;
};

struct ipv4_settings {
    tds_bool_t static_ip;
    char *ip;
    char *netmask;
    char *gateway;
    char **dns;
    char *proxy;
};

struct ipv6_settings {
    char *ip;
    unsigned char prefix_len;
    char *gateway;
    char **dns;
};

struct context_settings {
    char *interface;
    struct ipv4_settings *ipv4;
    struct ipv6_settings *ipv6;
};

static void gprs_deactivate_next(struct tds_gprs *gprs);

gboolean is_valid_apn(const char *apn)
{
    int i;
    int last_period = 0;

    if (apn[0] == '.' || apn[0] == '\0')
        return FALSE;

    for (i = 0; apn[i] != '\0'; i++) {
        if (g_ascii_isalnum(apn[i]))
            continue;

        if (apn[i] == '-')
            continue;

        if (apn[i] == '.' && (i - last_period) > 1) {
            last_period = i;
            continue;
        }

        return FALSE;
    }

    return TRUE;
}



const char *packet_bearer_to_string(int bearer)
{
    TDS_LOG_DEBUG("TDS");

    switch (bearer) {
    case PACKET_BEARER_NONE:
        return "none";
    case PACKET_BEARER_GPRS:
        return "gprs";
    case PACKET_BEARER_EGPRS:
        return "edge";
    case PACKET_BEARER_UMTS:
        return "umts";
    case PACKET_BEARER_HSUPA:
        return "hsupa";
    case PACKET_BEARER_HSDPA:
        return "hsdpa";
    case PACKET_BEARER_HSUPA_HSDPA:
        return "hspa";
    case PACKET_BEARER_EPS:
        return "lte";
    }
    return "";
}

static const char *gprs_context_default_name(enum tds_gprs_context_type type)
{
    TDS_LOG_DEBUG("TDS");

    switch (type) {
    case TDS_GPRS_CONTEXT_TYPE_ANY:
        return NULL;
    case TDS_GPRS_CONTEXT_TYPE_INTERNET:
        return "Internet";
    case TDS_GPRS_CONTEXT_TYPE_MMS:
        return "MMS";
    case TDS_GPRS_CONTEXT_TYPE_WAP:
        return "WAP";
    case TDS_GPRS_CONTEXT_TYPE_IMS:
        return "IMS";
    }

    return NULL;
}

static const char *gprs_context_type_to_string(
                    enum tds_gprs_context_type type)
{
    TDS_LOG_DEBUG("TDS");

    switch (type) {
    case TDS_GPRS_CONTEXT_TYPE_ANY:
        return NULL;
    case TDS_GPRS_CONTEXT_TYPE_INTERNET:
        return "internet";
    case TDS_GPRS_CONTEXT_TYPE_MMS:
        return "mms";
    case TDS_GPRS_CONTEXT_TYPE_WAP:
        return "wap";
    case TDS_GPRS_CONTEXT_TYPE_IMS:
        return "ims";
    }

    return NULL;
}

static gboolean gprs_context_string_to_type(const char *str,
                    enum tds_gprs_context_type *out)
{
    TDS_LOG_DEBUG("TDS");

    if (g_str_equal(str, "internet")) {
        *out = TDS_GPRS_CONTEXT_TYPE_INTERNET;
        return TRUE;
    } else if (g_str_equal(str, "wap")) {
        *out = TDS_GPRS_CONTEXT_TYPE_WAP;
        return TRUE;
    } else if (g_str_equal(str, "mms")) {
        *out = TDS_GPRS_CONTEXT_TYPE_MMS;
        return TRUE;
    } else if (g_str_equal(str, "ims")) {
        *out = TDS_GPRS_CONTEXT_TYPE_IMS;
        return TRUE;
    }

    return FALSE;
}

static const char *gprs_proto_to_string(enum tds_gprs_proto proto)
{
    TDS_LOG_DEBUG("TDS");

    switch (proto) {
    case TDS_GPRS_PROTO_IP:
        return "ip";
    case TDS_GPRS_PROTO_IPV6:
        return "ipv6";
    case TDS_GPRS_PROTO_IPV4V6:
        return "dual";
    };

    return NULL;
}

static gboolean gprs_proto_from_string(const char *str,
                    enum tds_gprs_proto *proto)
{
    TDS_LOG_DEBUG("TDS");

    if (g_str_equal(str, "ip")) {
        *proto = TDS_GPRS_PROTO_IP;
        return TRUE;
    } else if (g_str_equal(str, "ipv6")) {
        *proto = TDS_GPRS_PROTO_IPV6;
        return TRUE;
    } else if (g_str_equal(str, "dual")) {
        *proto = TDS_GPRS_PROTO_IPV4V6;
        return TRUE;
    }

    return FALSE;
}

static struct pri_context *gprs_context_by_path(struct tds_gprs *gprs,
                        const char *ctx_path)
{
    GSList *l;

    for (l = gprs->contexts; l; l = l->next) {
        struct pri_context *ctx = (struct pri_context *) l->data;

        if (g_str_equal(ctx_path, ctx->path))
            return ctx;
    }

    return NULL;
}

static void context_settings_append_ipv4(struct context_settings *settings,
                        DBusMessageIter *iter)
{
    DBusMessageIter variant;
    DBusMessageIter array;
    char typesig[5];
    char arraysig[6];
    const char *method;

    arraysig[0] = DBUS_TYPE_ARRAY;
    arraysig[1] = typesig[0] = DBUS_DICT_ENTRY_BEGIN_CHAR;
    arraysig[2] = typesig[1] = DBUS_TYPE_STRING;
    arraysig[3] = typesig[2] = DBUS_TYPE_VARIANT;
    arraysig[4] = typesig[3] = DBUS_DICT_ENTRY_END_CHAR;
    arraysig[5] = typesig[4] = '\0';

    dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT,
                        arraysig, &variant);

    dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY,
                        typesig, &array);
    if (settings == NULL || settings->ipv4 == NULL)
        goto done;

    tds_dbus_dict_append(&array, "Interface",
                DBUS_TYPE_STRING, &settings->interface);

    if (settings->ipv4->static_ip == TRUE)
        method = "static";
    else
        method = "dhcp";

    tds_dbus_dict_append(&array, "Method", DBUS_TYPE_STRING, &method);

    if (settings->ipv4->ip)
        tds_dbus_dict_append(&array, "Address", DBUS_TYPE_STRING,
                    &settings->ipv4->ip);

    if (settings->ipv4->netmask)
        tds_dbus_dict_append(&array, "Netmask", DBUS_TYPE_STRING,
                    &settings->ipv4->netmask);

    if (settings->ipv4->gateway)
        tds_dbus_dict_append(&array, "Gateway", DBUS_TYPE_STRING,
                    &settings->ipv4->gateway);

    if (settings->ipv4->dns)
        tds_dbus_dict_append_array(&array, "DomainNameServers",
                        DBUS_TYPE_STRING,
                        &settings->ipv4->dns);

done:
    dbus_message_iter_close_container(&variant, &array);

    dbus_message_iter_close_container(iter, &variant);
}

static void context_settings_append_ipv4_dict(struct context_settings *settings,
                        DBusMessageIter *dict)
{
    DBusMessageIter entry;
    const char *key = "Settings";

    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY,
                        NULL, &entry);

    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);

    context_settings_append_ipv4(settings, &entry);

    dbus_message_iter_close_container(dict, &entry);
}

static void context_settings_append_ipv6(struct context_settings *settings,
                        DBusMessageIter *iter)
{
    DBusMessageIter variant;
    DBusMessageIter array;
    char typesig[5];
    char arraysig[6];

    arraysig[0] = DBUS_TYPE_ARRAY;
    arraysig[1] = typesig[0] = DBUS_DICT_ENTRY_BEGIN_CHAR;
    arraysig[2] = typesig[1] = DBUS_TYPE_STRING;
    arraysig[3] = typesig[2] = DBUS_TYPE_VARIANT;
    arraysig[4] = typesig[3] = DBUS_DICT_ENTRY_END_CHAR;
    arraysig[5] = typesig[4] = '\0';

    dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT,
                        arraysig, &variant);

    dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY,
                        typesig, &array);
    if (settings == NULL || settings->ipv6 == NULL)
        goto done;

    tds_dbus_dict_append(&array, "Interface",
                DBUS_TYPE_STRING, &settings->interface);

    if (settings->ipv6->ip)
        tds_dbus_dict_append(&array, "Address", DBUS_TYPE_STRING,
                    &settings->ipv6->ip);

    if (settings->ipv6->prefix_len)
        tds_dbus_dict_append(&array, "PrefixLength", DBUS_TYPE_BYTE,
                    &settings->ipv6->prefix_len);

    if (settings->ipv6->gateway)
        tds_dbus_dict_append(&array, "Gateway", DBUS_TYPE_STRING,
                    &settings->ipv6->gateway);

    if (settings->ipv6->dns)
        tds_dbus_dict_append_array(&array, "DomainNameServers",
                        DBUS_TYPE_STRING,
                        &settings->ipv6->dns);

done:
    dbus_message_iter_close_container(&variant, &array);

    dbus_message_iter_close_container(iter, &variant);
}

static void context_settings_append_ipv6_dict(struct context_settings *settings,
                        DBusMessageIter *dict)
{
    DBusMessageIter entry;
    const char *key = "IPv6.Settings";

    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY,
                        NULL, &entry);

    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);

    context_settings_append_ipv6(settings, &entry);

    dbus_message_iter_close_container(dict, &entry);
}

static void signal_settings(struct pri_context *ctx, const char *prop,
        void (*append)(struct context_settings *, DBusMessageIter *))

{
    TDS_LOG_DEBUG("TDS");

    DBusConnection *conn = tds_dbus_get_connection();
    const char *path = ctx->path;
    DBusMessage *signal;
    DBusMessageIter iter;
    struct context_settings *settings;

    signal = dbus_message_new_signal(path,
                    TDS_CONNECTION_CONTEXT_INTERFACE,
                    "PropertyChanged");

    if (signal == NULL)
        return;

    dbus_message_iter_init_append(signal, &iter);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &prop);

    if (ctx)
        settings = ctx->settings;
    else
        settings = NULL;

    append(settings, &iter);
    g_dbus_send_message(conn, signal);
}

static void pri_context_signal_settings(struct pri_context *ctx,
                    gboolean ipv4, gboolean ipv6)
{
    TDS_LOG_DEBUG("ipv4 = %s, ipv6 = %s", ipv4?"true":"false", ipv6?"true":"false" );

    if (ipv4)
        signal_settings(ctx, "Settings",
                context_settings_append_ipv4);

    if (ipv6)
        signal_settings(ctx, "IPv6.Settings",
                context_settings_append_ipv6);
}

static void append_context_properties(struct pri_context *ctx,
                    DBusMessageIter *dict)
{
    const char *type = gprs_context_type_to_string(ctx->type);
    const char *proto = gprs_proto_to_string(ctx->context.proto);
    const char *name = ctx->name;
    tds_bool_t value;
    const char *strvalue;
    struct context_settings *settings;

    TDS_LOG_DEBUG("TDS");

    tds_dbus_dict_append(dict, "Name", DBUS_TYPE_STRING, &name);

    value = ctx->active;
    tds_dbus_dict_append(dict, "Active", DBUS_TYPE_BOOLEAN, &value);

    tds_dbus_dict_append(dict, "Type", DBUS_TYPE_STRING, &type);

    tds_dbus_dict_append(dict, "Protocol", DBUS_TYPE_STRING, &proto);

    strvalue = ctx->context.apn;
    tds_dbus_dict_append(dict, "AccessPointName", DBUS_TYPE_STRING,
                &strvalue);

    strvalue = ctx->context.username;
    tds_dbus_dict_append(dict, "Username", DBUS_TYPE_STRING,
                &strvalue);

    strvalue = ctx->context.password;
    tds_dbus_dict_append(dict, "Password", DBUS_TYPE_STRING,
                &strvalue);

    if (ctx->type == TDS_GPRS_CONTEXT_TYPE_MMS) {
        strvalue = ctx->message_proxy;
        tds_dbus_dict_append(dict, "MessageProxy",
                    DBUS_TYPE_STRING, &strvalue);

        strvalue = ctx->message_center;
        tds_dbus_dict_append(dict, "MessageCenter",
                    DBUS_TYPE_STRING, &strvalue);
    }

    settings = ctx->settings;

    context_settings_append_ipv4_dict(settings, dict);
    context_settings_append_ipv6_dict(settings, dict);
}

static void pri_activate_callback(const struct tds_error error, void *data)
{
    struct pri_context *ctx = (struct pri_context *) data;
    DBusConnection *conn = tds_dbus_get_connection();
    tds_bool_t value;

    TDS_LOG_DEBUG("TDS");

    if (error.type != TDS_ERROR_TYPE_NO_ERROR) {
        TDS_LOG_DEBUG("TDS_ERROR_TYPE_FAILURE");
        return;
    }

    __tds_dbus_pending_reply(&ctx->pending,
                dbus_message_new_method_return(ctx->pending));

    ctx->settings->interface = g_strdup("ppp0");
    TDS_LOG_DEBUG("ctx->settings->interface = [%s]", ctx->settings->interface);

    ctx->settings->ipv4->static_ip = TRUE;
	ctx->settings->ipv4->ip = "223.190.21.191";
	ctx->settings->ipv4->netmask = "255.255.255.0";
	ctx->settings->ipv4->gateway = "10.64.64.64";

	char *dns[3] = {"125.22.47.102","125.22.47.103",NULL};

	if(ctx->settings->ipv4->dns)
		g_strfreev(ctx->settings->ipv4->dns);

	ctx->settings->ipv4->dns = g_strdupv((char **) dns);

	if (ctx->settings->interface != NULL) {
        pri_context_signal_settings(ctx, ctx->settings->ipv4 != NULL,
                        ctx->settings->ipv6 != NULL);
    }

    value = ctx->active;
    TDS_LOG_DEBUG("ctx->active will be update as [%s]", value?"true":"false");
    tds_dbus_signal_property_changed(conn, ctx->path,
                    TDS_CONNECTION_CONTEXT_INTERFACE,
                    "Active", DBUS_TYPE_BOOLEAN, &value);

}

void til_handle_Activate(bool ret, void *data)
{
    TDS_LOG_DEBUG("TDS");

    struct tds_error error;
    struct pri_context *ctx = (struct pri_context *)data;

    if(ret){
        error.type = TDS_ERROR_TYPE_NO_ERROR;
        ctx->active = TRUE;
    }
    else{
        error.type = TDS_ERROR_TYPE_FAILURE;
        ctx->active = FALSE;
    }

    pri_activate_callback(error, ctx);
}
static void pri_deactivate_callback(const struct tds_error error, void *data)
{
    struct pri_context *ctx = (struct pri_context *)data;
    DBusConnection *conn = tds_dbus_get_connection();
    tds_bool_t value;

    TDS_LOG_DEBUG("TDS");

    if (error.type != TDS_ERROR_TYPE_NO_ERROR) {
        TDS_LOG_DEBUG("TDS_ERROR_TYPE_FAILURE");
        return;
    }

    __tds_dbus_pending_reply(&ctx->pending,
                dbus_message_new_method_return(ctx->pending));

    value = ctx->active;

    TDS_LOG_DEBUG("ctx->active will be update as [%s]", value?"true":"false");

    tds_dbus_signal_property_changed(conn, ctx->path,
                    TDS_CONNECTION_CONTEXT_INTERFACE,
                    "Active", DBUS_TYPE_BOOLEAN, &value);
}

void til_handle_Deactivate(bool ret, void *data)
{
    TDS_LOG_DEBUG("TDS");

    struct tds_error error;
    struct pri_context *ctx = (struct pri_context *)data;

    if(ret){
        error.type = TDS_ERROR_TYPE_NO_ERROR;
        ctx->active = FALSE;
    }
    else{
        error.type = TDS_ERROR_TYPE_FAILURE;
        ctx->active = TRUE;
    }

    pri_deactivate_callback(error, ctx);
}

void til_handle_ConnStatus(string &ConnState, void *data)
{
    TDS_LOG_DEBUG("TDS");

    struct tds_error *error;
    struct pri_context *ctx = (struct pri_context *)data;
    DBusConnection *conn = tds_dbus_get_connection();

    tds_bool_t old_active = ctx->active;

    if(ConnState.compare("active")==0){
        ctx->active = TRUE;
    }
    else{
        ctx->active = FALSE;
    }

    if(old_active != ctx->active){
        TDS_LOG_DEBUG("ctx->active will be updated as [%d]", ctx->active );

        tds_dbus_signal_property_changed(conn, ctx->path,
                        TDS_CONNECTION_CONTEXT_INTERFACE,
                        "Active", DBUS_TYPE_BOOLEAN, &ctx->active);
    }
}

void til_handle_OnlineStatus(bool online, void *data)
{
    TDS_LOG_DEBUG("TDS");

    struct pri_context *ctx = (struct pri_context *)data;
    DBusConnection *conn = tds_dbus_get_connection();

    tds_bool_t old_online = ctx->gprs->modem->online;

    ctx->gprs->modem->online = (tds_bool_t) online;

    if(old_online != online){
        TDS_LOG_DEBUG("ctx->gprs->modem->online will be updates as [%d]", ctx->gprs->modem->online );

        tds_dbus_signal_property_changed(conn, ctx->gprs->modem->path,
                        TDS_MODEM_INTERFACE,
                        "Online", DBUS_TYPE_BOOLEAN, &ctx->gprs->modem->online);
    }
}

void til_handle_EmergencyStatus(bool emergency, void *data)
{
    TDS_LOG_DEBUG("TDS");

    struct pri_context *ctx = (struct pri_context *)data;
    DBusConnection *conn = tds_dbus_get_connection();

    tds_bool_t old_emergency = (tds_bool_t) ctx->gprs->modem->emergency;

    ctx->gprs->modem->emergency = (guint) emergency;

    if(old_emergency != (tds_bool_t)emergency){
        TDS_LOG_DEBUG("ctx->gprs->modem->emergency will be updates as [%d]", ctx->gprs->modem->emergency );

        tds_bool_t value = (tds_bool_t) ctx->gprs->modem->emergency;
        tds_dbus_signal_property_changed(conn, ctx->gprs->modem->path,
                        TDS_MODEM_INTERFACE,
                        "Emergency", DBUS_TYPE_BOOLEAN, &value);
    }
}

void til_handle_PowerStatus(bool powered, void *data)
{
    TDS_LOG_DEBUG("TDS");

    struct pri_context *ctx = (struct pri_context *)data;
    DBusConnection *conn = tds_dbus_get_connection();

    tds_bool_t old_powered = ctx->gprs->modem->powered;

    TDS_LOG_DEBUG("old_powered = [%d]", old_powered );

    ctx->gprs->modem->powered = (tds_bool_t) powered;

    if(old_powered != (tds_bool_t)powered){
        TDS_LOG_DEBUG("ctx->gprs->modem->powered will be updates as [%d]", ctx->gprs->modem->powered );
        tds_dbus_signal_property_changed(conn, ctx->gprs->modem->path,
            TDS_MODEM_INTERFACE, "Powered", DBUS_TYPE_BOOLEAN, &ctx->gprs->modem->powered);
    }
}

void til_handle_AttachedStatus(bool attached, void *data)
{
    TDS_LOG_DEBUG("TDS");

    struct pri_context *ctx = (struct pri_context *)data;
    DBusConnection *conn = tds_dbus_get_connection();

    tds_bool_t old_attached = ctx->gprs->attached;

    ctx->gprs->attached = (tds_bool_t) attached;

    if(old_attached != attached){
        TDS_LOG_DEBUG("ctx->gprs->attached will be updates as [%d]", ctx->gprs->attached );

        tds_dbus_signal_property_changed(conn, ctx->gprs->modem->path,
                        TDS_CONNECTION_MANAGER_INTERFACE,
                        "Attached", DBUS_TYPE_BOOLEAN, &ctx->gprs->attached);
    }
}

void til_handle_Bearer(string &DataType, void *data)
{
    TDS_LOG_DEBUG("TDS");

    struct pri_context *ctx = (struct pri_context *)data;
    DBusConnection *conn = tds_dbus_get_connection();

    int old_bearer = ctx->gprs->bearer;

    if(DataType.compare("none")==0)
        ctx->gprs->bearer = 0;
    else if(DataType.compare("gsm")==0)
        ctx->gprs->bearer = 1;
    else if(DataType.compare("edge")==0)
        ctx->gprs->bearer = 2;
    else if(DataType.compare("umts")==0)
        ctx->gprs->bearer = 3;
    else if(DataType.compare("hsupa")==0)
        ctx->gprs->bearer = 4;
    else if(DataType.compare("hsdpa")==0)
        ctx->gprs->bearer = 5;
    else if(DataType.compare("hspa")==0)
        ctx->gprs->bearer = 6;
    else if(DataType.compare("lte")==0)
        ctx->gprs->bearer = 7;
    else
        ctx->gprs->bearer = -1;

    if(old_bearer != ctx->gprs->bearer){
        TDS_LOG_DEBUG("ctx->gprs->bearer will be updates as [%d = %s]", ctx->gprs->bearer, DataType.c_str() );

        tds_dbus_signal_property_changed(conn, ctx->gprs->modem->path,
                        TDS_CONNECTION_MANAGER_INTERFACE,
                        "Bearer", DBUS_TYPE_STRING, &DataType);
    }
}

void til_handle_ApnSet(bool ret, void *data)
{
    TDS_LOG_DEBUG("TDS");

    struct tds_error error;
    struct pri_context *ctx = (struct pri_context *)data;
    DBusConnection *conn = tds_dbus_get_connection();

    if(ret){
        error.type = TDS_ERROR_TYPE_NO_ERROR;
    }
    else{
        error.type = TDS_ERROR_TYPE_FAILURE;
    }

    if (error.type != TDS_ERROR_TYPE_NO_ERROR) {
        TDS_LOG_DEBUG("TDS_ERROR_TYPE_FAILURE");
        return;
    }

    TDS_LOG_DEBUG("ctx->context.apn will be update as [%s]", ctx->context.apn);

	const char *strvalue = ctx->context.apn;

    tds_dbus_signal_property_changed(conn, ctx->path,
                    TDS_CONNECTION_CONTEXT_INTERFACE,
                    "AccessPointName",
                    DBUS_TYPE_STRING, &strvalue);
}

void til_handle_CommonError(bool ret, void *data)
{
    TDS_LOG_DEBUG("TDS");

    struct tds_error error;
    struct pri_context *ctx = (struct pri_context *)data;
    DBusConnection *conn = tds_dbus_get_connection();

    tds_bool_t old_attached = ctx->gprs->attached;

    if(ret){
        error.type = TDS_ERROR_TYPE_NO_ERROR;
    }
    else{
        error.type = TDS_ERROR_TYPE_FAILURE;
    }
}

static DBusMessage *pri_get_properties(DBusConnection *conn,
                    DBusMessage *msg, void *data)
{
    struct pri_context *ctx = (struct pri_context *)data;
    DBusMessage *reply;
    DBusMessageIter iter;
    DBusMessageIter dict;

    TDS_LOG_DEBUG("TDS");

    reply = dbus_message_new_method_return(msg);
    if (reply == NULL)
        return NULL;

    dbus_message_iter_init_append(reply, &iter);

    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
                    TDS_PROPERTIES_ARRAY_SIGNATURE,
                    &dict);
    append_context_properties(ctx, &dict);
    dbus_message_iter_close_container(&iter, &dict);

    return reply;
}


static DBusMessage *pri_set_apn(struct pri_context *ctx, DBusConnection *conn,
                DBusMessage *msg, const char *apn)
{
    TDS_LOG_DEBUG("TDS");

    if (strlen(apn) > TDS_GPRS_MAX_APN_LENGTH)
        return __tds_error_invalid_format(msg);
    if (is_valid_apn(apn) == FALSE)
        return __tds_error_invalid_format(msg);

    til_luna_ApnUpdate(ctx, apn);

    return NULL;
}

static DBusMessage *pri_set_username(struct pri_context *ctx,
                    DBusConnection *conn, DBusMessage *msg,
                    const char *username)
{
    if (strlen(username) > TDS_GPRS_MAX_USERNAME_LENGTH)
        return __tds_error_invalid_format(msg);

    if (g_str_equal(username, ctx->context.username))
        return dbus_message_new_method_return(msg);

    strcpy(ctx->context.username, username);

    g_dbus_send_reply(conn, msg, DBUS_TYPE_INVALID);

    tds_dbus_signal_property_changed(conn, ctx->path,
                    TDS_CONNECTION_CONTEXT_INTERFACE,
                    "Username",
                    DBUS_TYPE_STRING, &username);

    return NULL;
}

static DBusMessage *pri_set_password(struct pri_context *ctx,
                    DBusConnection *conn, DBusMessage *msg,
                    const char *password)
{
    if (strlen(password) > TDS_GPRS_MAX_PASSWORD_LENGTH)
        return __tds_error_invalid_format(msg);

    if (g_str_equal(password, ctx->context.password))
        return dbus_message_new_method_return(msg);

    strcpy(ctx->context.password, password);

    g_dbus_send_reply(conn, msg, DBUS_TYPE_INVALID);

    tds_dbus_signal_property_changed(conn, ctx->path,
                    TDS_CONNECTION_CONTEXT_INTERFACE,
                    "Password",
                    DBUS_TYPE_STRING, &password);

    return NULL;
}

static DBusMessage *pri_set_type(struct pri_context *ctx, DBusConnection *conn,
                    DBusMessage *msg, const char *type)
{
    enum tds_gprs_context_type context_type;

    if (gprs_context_string_to_type(type, &context_type) == FALSE)
        return __tds_error_invalid_format(msg);

    if (ctx->type == context_type)
        return dbus_message_new_method_return(msg);

    ctx->type = context_type;

    g_dbus_send_reply(conn, msg, DBUS_TYPE_INVALID);

    tds_dbus_signal_property_changed(conn, ctx->path,
                    TDS_CONNECTION_CONTEXT_INTERFACE,
                    "Type", DBUS_TYPE_STRING, &type);

    return NULL;
}

static DBusMessage *pri_set_proto(struct pri_context *ctx,
                    DBusConnection *conn,
                    DBusMessage *msg, const char *str)
{
    enum tds_gprs_proto proto;

    if (gprs_proto_from_string(str, &proto) == FALSE)
        return __tds_error_invalid_format(msg);

    if (ctx->context.proto == proto)
        return dbus_message_new_method_return(msg);

    ctx->context.proto = proto;

    g_dbus_send_reply(conn, msg, DBUS_TYPE_INVALID);

    tds_dbus_signal_property_changed(conn, ctx->path,
                    TDS_CONNECTION_CONTEXT_INTERFACE,
                    "Protocol", DBUS_TYPE_STRING, &str);

    return NULL;
}

static DBusMessage *pri_set_name(struct pri_context *ctx, DBusConnection *conn,
                    DBusMessage *msg, const char *name)
{
    if (strlen(name) > MAX_CONTEXT_NAME_LENGTH)
        return __tds_error_invalid_format(msg);

    if (ctx->name && g_str_equal(ctx->name, name))
        return dbus_message_new_method_return(msg);

    strcpy(ctx->name, name);

    g_dbus_send_reply(conn, msg, DBUS_TYPE_INVALID);

    tds_dbus_signal_property_changed(conn, ctx->path,
                    TDS_CONNECTION_CONTEXT_INTERFACE,
                    "Name", DBUS_TYPE_STRING, &name);

    return NULL;
}

static DBusMessage *pri_set_message_proxy(struct pri_context *ctx,
                    DBusConnection *conn,
                    DBusMessage *msg, const char *proxy)
{
    if (strlen(proxy) > MAX_MESSAGE_PROXY_LENGTH)
        return __tds_error_invalid_format(msg);

    if (ctx->message_proxy && g_str_equal(ctx->message_proxy, proxy))
        return dbus_message_new_method_return(msg);

    strcpy(ctx->message_proxy, proxy);

    g_dbus_send_reply(conn, msg, DBUS_TYPE_INVALID);

    tds_dbus_signal_property_changed(conn, ctx->path,
                TDS_CONNECTION_CONTEXT_INTERFACE,
                "MessageProxy", DBUS_TYPE_STRING, &proxy);

    return NULL;
}

static DBusMessage *pri_set_message_center(struct pri_context *ctx,
                    DBusConnection *conn,
                    DBusMessage *msg, const char *center)
{
    if (strlen(center) > MAX_MESSAGE_CENTER_LENGTH)
        return __tds_error_invalid_format(msg);

    if (ctx->message_center && g_str_equal(ctx->message_center, center))
        return dbus_message_new_method_return(msg);

    strcpy(ctx->message_center, center);

    g_dbus_send_reply(conn, msg, DBUS_TYPE_INVALID);

    tds_dbus_signal_property_changed(conn, ctx->path,
                TDS_CONNECTION_CONTEXT_INTERFACE,
                "MessageCenter", DBUS_TYPE_STRING, &center);

    return NULL;
}

static DBusMessage *pri_set_property(DBusConnection *conn,
                    DBusMessage *msg, void *data)
{
    struct pri_context *ctx = (struct pri_context *)data;
    DBusMessageIter iter;
    DBusMessageIter var;
    const char *property;
    tds_bool_t value;
    const char *str;

    TDS_LOG_DEBUG("TDS");

    if (!dbus_message_iter_init(msg, &iter))
        return __tds_error_invalid_args(msg);

    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING)
        return __tds_error_invalid_args(msg);

    dbus_message_iter_get_basic(&iter, &property);
    dbus_message_iter_next(&iter);

    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_VARIANT)
        return __tds_error_invalid_args(msg);

    dbus_message_iter_recurse(&iter, &var);

    if (g_str_equal(property, "Active")) {

        if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_BOOLEAN)
            return __tds_error_invalid_args(msg);

        dbus_message_iter_get_basic(&var, &value);

        TDS_LOG_DEBUG("Active ctx->active = %d value = %d", ctx->active, value );

        if (ctx->active == (tds_bool_t) value)
            return dbus_message_new_method_return(msg);

        ctx->pending = dbus_message_ref(msg);
        TDS_LOG_DEBUG("ctx->pending = %p", ctx->pending);

        if (value){
            til_luna_ConnectDataService(ctx);
        }
        else{
            til_luna_DisconnectDataService(ctx);
        }

        return dbus_message_new_method_return(msg);
    }

    if (ctx->active == TRUE)
        return __tds_error_in_use(msg);

    if (!strcmp(property, "AccessPointName")) {
        if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_STRING)
            return __tds_error_invalid_args(msg);

        dbus_message_iter_get_basic(&var, &str);

        pri_set_apn(ctx, conn, msg, str);
        return dbus_message_new_method_return(msg);
    } else if (!strcmp(property, "Type")) {
        if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_STRING)
            return __tds_error_invalid_args(msg);

        dbus_message_iter_get_basic(&var, &str);

        return pri_set_type(ctx, conn, msg, str);
    } else if (!strcmp(property, "Protocol")) {
        if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_STRING)
            return __tds_error_invalid_args(msg);

        dbus_message_iter_get_basic(&var, &str);

        return pri_set_proto(ctx, conn, msg, str);
    } else if (!strcmp(property, "Username")) {
        if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_STRING)
            return __tds_error_invalid_args(msg);

        dbus_message_iter_get_basic(&var, &str);

        return pri_set_username(ctx, conn, msg, str);
    } else if (!strcmp(property, "Password")) {
        if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_STRING)
            return __tds_error_invalid_args(msg);

        dbus_message_iter_get_basic(&var, &str);

        return pri_set_password(ctx, conn, msg, str);
    } else if (!strcmp(property, "Name")) {
        if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_STRING)
            return __tds_error_invalid_args(msg);

        dbus_message_iter_get_basic(&var, &str);

        return pri_set_name(ctx, conn, msg, str);
    }

    if (ctx->type != TDS_GPRS_CONTEXT_TYPE_MMS)
        return __tds_error_invalid_args(msg);

    if (!strcmp(property, "MessageProxy")) {
        if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_STRING)
            return __tds_error_invalid_args(msg);

        dbus_message_iter_get_basic(&var, &str);

        return pri_set_message_proxy(ctx, conn, msg, str);
    } else if (!strcmp(property, "MessageCenter")) {
        if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_STRING)
            return __tds_error_invalid_args(msg);

        dbus_message_iter_get_basic(&var, &str);

        return pri_set_message_center(ctx, conn, msg, str);
    }

    return __tds_error_invalid_args(msg);
}

static struct pri_context *pri_context_create(struct tds_gprs *gprs,
                    const char *name,
                    enum tds_gprs_context_type type)
{
    TDS_LOG_DEBUG("TDS");

    struct pri_context *context = (struct pri_context *)g_try_new0(struct pri_context, 1);

    if (context == NULL)
        return NULL;

    if (name == NULL) {
        name = gprs_context_default_name(type);
        if (name == NULL) {
            g_free(context);
            return NULL;
        }
    }

    context->gprs = gprs;
    strcpy(context->name, name);
    context->type = type;

    return context;
}

static void pri_context_destroy(gpointer userdata)
{
    struct pri_context *ctx = (struct pri_context *) userdata;

    g_free(ctx->settings);
    g_free(ctx->proxy_host);
    g_free(ctx->path);
    g_free(ctx);
}


static gboolean context_dbus_register(struct pri_context *ctx)
{
    TDS_LOG_DEBUG("TDS");

    DBusConnection *conn = tds_dbus_get_connection();
    char path[256];
    const char *basepath;
    basepath = ctx->gprs->modem->path;

    snprintf(path, sizeof(path), "%s/context%u", basepath, ctx->id);

    static GDBusMethodTable context_methods[3];
    memset(&context_methods[0], 0, sizeof(context_methods));

    context_methods[0].name = "GetProperties";
    context_methods[0].in_args = NULL;
    context_methods[0].out_args = GDBUS_ARGS({ "properties", "a{sv}" });
    context_methods[0].function = pri_get_properties;

    context_methods[1].name = "SetProperty";
    context_methods[1].in_args = GDBUS_ARGS({ "property", "s" }, { "value", "v" });
    context_methods[1].out_args = NULL;
    context_methods[1].function = pri_set_property;


    static GDBusSignalTable context_signals[2];
    memset(&context_signals[0], 0, sizeof(context_signals));

    context_signals[0].name = "PropertyChanged";
    context_signals[0].args = (const GDBusArgInfo[]) { { "name", "s" }, { "value", "v" }, { } };

    if (!g_dbus_register_interface(conn, path,
                    TDS_CONNECTION_CONTEXT_INTERFACE,
                    context_methods, context_signals,
                    NULL, ctx, pri_context_destroy)) {
        TDS_LOG_DEBUG("Could not register PrimaryContext %s", path);

        return FALSE;
    }

    ctx->path = g_strdup(path);
    ctx->key = ctx->path + strlen(basepath) + 1;

    return TRUE;
}

static gboolean context_dbus_unregister(struct pri_context *ctx)
{
    TDS_LOG_DEBUG("TDS");

    DBusConnection *conn = tds_dbus_get_connection();
    char path[256];

    strcpy(path, ctx->path);

    return g_dbus_unregister_interface(conn, path,
                    TDS_CONNECTION_CONTEXT_INTERFACE);
}

static gboolean have_active_contexts(struct tds_gprs *gprs)
{
    GSList *l;
    struct pri_context *ctx;

    for (l = gprs->contexts; l; l = l->next) {
        ctx = (struct pri_context *) l->data;

        if (ctx->active == TRUE)
            return TRUE;
    }

    return FALSE;
}

static void release_active_contexts(struct tds_gprs *gprs)
{
    GSList *l;
    struct pri_context *ctx;

    for (l = gprs->contexts; l; l = l->next) {
        struct tds_gprs_context *gc;

        ctx = (struct pri_context *) l->data;

        if (ctx->active == FALSE)
            continue;

        TDS_LOG_DEBUG("Contexts %s is deactivating", ctx->path);
        ctx->active = false;
    }
}

static void gprs_attached_update(struct tds_gprs *gprs)
{
    TDS_LOG_DEBUG("TDS");

    DBusConnection *conn = tds_dbus_get_connection();
    const char *path;
    tds_bool_t attached;
    tds_bool_t value;

    attached = gprs->status == NETWORK_REGISTRATION_STATUS_REGISTERED
            || gprs->status == NETWORK_REGISTRATION_STATUS_ROAMING;

    if (attached == gprs->attached)
        return;

    if (attached == FALSE) {
        release_active_contexts(gprs);
        gprs->bearer = -1;
    } else if (have_active_contexts(gprs) == TRUE) {
        gprs->flags |= GPRS_FLAG_ATTACHED_UPDATE;
        return;
    }

    gprs->attached = attached;

    path = g_strdup(gprs->modem->path);
    value = attached;
    tds_dbus_signal_property_changed(conn, path,
                TDS_CONNECTION_MANAGER_INTERFACE,
                "Attached", DBUS_TYPE_BOOLEAN, &value);
}

static DBusMessage *gprs_get_properties(DBusConnection *conn,
                    DBusMessage *msg, void *data)
{
    TDS_LOG_DEBUG("TDS");
    struct tds_gprs *gprs = (struct tds_gprs *)data;
    DBusMessage *reply;
    DBusMessageIter iter;
    DBusMessageIter dict;
    tds_bool_t value;

    reply = dbus_message_new_method_return(msg);
    if (reply == NULL)
        return NULL;

    dbus_message_iter_init_append(reply, &iter);

    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
                    TDS_PROPERTIES_ARRAY_SIGNATURE,
                    &dict);

    value = gprs->attached;
    tds_dbus_dict_append(&dict, "Attached", DBUS_TYPE_BOOLEAN, &value);

    if (gprs->bearer != -1) {
        const char *bearer = packet_bearer_to_string(gprs->bearer);

        tds_dbus_dict_append(&dict, "Bearer",
                    DBUS_TYPE_STRING, &bearer);
    }

    value = gprs->roaming_allowed;
    tds_dbus_dict_append(&dict, "RoamingAllowed",
                DBUS_TYPE_BOOLEAN, &value);

    value = gprs->powered;
    tds_dbus_dict_append(&dict, "Powered", DBUS_TYPE_BOOLEAN, &value);

    if (gprs->attached) {
        value = gprs->suspended;
        tds_dbus_dict_append(&dict, "Suspended",
                DBUS_TYPE_BOOLEAN, &value);
    }

    dbus_message_iter_close_container(&iter, &dict);

    return reply;
}

static DBusMessage *gprs_set_property(DBusConnection *conn,
                    DBusMessage *msg, void *data)
{
    TDS_LOG_DEBUG("TDS");
    struct tds_gprs *gprs = (struct tds_gprs *)data;
    DBusMessageIter iter;
    DBusMessageIter var;
    const char *property;
    tds_bool_t value;
    const char *path;

    if (!dbus_message_iter_init(msg, &iter))
        return __tds_error_invalid_args(msg);

    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING)
        return __tds_error_invalid_args(msg);

    dbus_message_iter_get_basic(&iter, &property);
    dbus_message_iter_next(&iter);

    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_VARIANT)
        return __tds_error_invalid_args(msg);

    dbus_message_iter_recurse(&iter, &var);

    if (!strcmp(property, "RoamingAllowed")) {
        if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_BOOLEAN)
            return __tds_error_invalid_args(msg);

        dbus_message_iter_get_basic(&var, &value);

        if (gprs->roaming_allowed == (tds_bool_t) value)
            return dbus_message_new_method_return(msg);

        gprs->roaming_allowed = value;

    } else if (!strcmp(property, "Powered")) {

        if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_BOOLEAN)
            return __tds_error_invalid_args(msg);

        dbus_message_iter_get_basic(&var, &value);

        if (gprs->powered == (tds_bool_t) value)
            return dbus_message_new_method_return(msg);

        gprs->powered = value;

    } else {
        return __tds_error_invalid_args(msg);
    }

    path = g_strdup(gprs->modem->path);
    tds_dbus_signal_property_changed(conn, path,
                    TDS_CONNECTION_MANAGER_INTERFACE,
                    property, DBUS_TYPE_BOOLEAN, &value);

    return dbus_message_new_method_return(msg);
}

static struct pri_context *add_context(struct tds_gprs *gprs,
                    const char *name,
                    enum tds_gprs_context_type type)
{
    struct pri_context *context;

    context = pri_context_create(gprs, name, type);

    tds_gprs_add_context(gprs, context);

    context->id = (gprs->last_context_id)++;

    TDS_LOG_DEBUG("gprs %x", gprs );

    if (!context_dbus_register(context)) {
        TDS_LOG_DEBUG("Unable to register primary context");
        return NULL;
    }

    gprs->contexts = g_slist_append(gprs->contexts, context);

    return context;
}

static DBusMessage *gprs_add_context(DBusConnection *conn,
                    DBusMessage *msg, void *data)
{
    TDS_LOG_DEBUG("TDS");
    struct tds_gprs *gprs = (struct tds_gprs *)data;
    struct pri_context *context;
    const char *typestr;
    const char *name;
    const char *path;
    enum tds_gprs_context_type type;
    DBusMessage *signal;

    if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &typestr,
                    DBUS_TYPE_INVALID))
        return __tds_error_invalid_args(msg);

    if (gprs_context_string_to_type(typestr, &type) == FALSE)
        return __tds_error_invalid_format(msg);

    name = gprs_context_default_name(type);
    if (name == NULL)
        name = typestr;

    context = add_context(gprs, name, type);
    if (context == NULL)
        return __tds_error_failed(msg);

    path = context->path;

    g_dbus_send_reply(conn, msg, DBUS_TYPE_OBJECT_PATH, &path,
                    DBUS_TYPE_INVALID);

    path = g_strdup(gprs->modem->path);
    signal = dbus_message_new_signal(path,
                    TDS_CONNECTION_MANAGER_INTERFACE,
                    "ContextAdded");

    if (signal) {
        DBusMessageIter iter;
        DBusMessageIter dict;

        dbus_message_iter_init_append(signal, &iter);

        path = context->path;
        dbus_message_iter_append_basic(&iter, DBUS_TYPE_OBJECT_PATH,
                        &path);

        dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
                    TDS_PROPERTIES_ARRAY_SIGNATURE,
                    &dict);
        append_context_properties(context, &dict);
        dbus_message_iter_close_container(&iter, &dict);

        g_dbus_send_message(conn, signal);
    }

    return NULL;
}

static void gprs_deactivate_for_remove(const struct tds_error *error,
                        void *data, DBusMessage *msg)
{
    struct pri_context *ctx = (struct pri_context *)data;
    struct tds_gprs *gprs = ctx->gprs;
    DBusConnection *conn = tds_dbus_get_connection();
    char *path;
    const char *modempath;
    tds_bool_t value;

    value = FALSE;

    tds_dbus_signal_property_changed(conn, ctx->path,
                    TDS_CONNECTION_CONTEXT_INTERFACE,
                    "Active", DBUS_TYPE_BOOLEAN, &value);

    path = g_strdup(ctx->path);

    context_dbus_unregister(ctx);
    gprs->contexts = g_slist_remove(gprs->contexts, ctx);

    __tds_dbus_pending_reply(&msg,
                dbus_message_new_method_return(msg));

    modempath = g_strdup(gprs->modem->path);

    g_dbus_emit_signal(conn, modempath, TDS_CONNECTION_MANAGER_INTERFACE,
                "ContextRemoved", DBUS_TYPE_OBJECT_PATH, &path,
                DBUS_TYPE_INVALID);

    g_free(path);
}

static DBusMessage *gprs_remove_context(DBusConnection *conn,
                    DBusMessage *msg, void *data)
{
    TDS_LOG_DEBUG("TDS");
    struct tds_gprs *gprs = (struct tds_gprs *)data;
    struct pri_context *ctx;
    const char *path;
    const char *atompath;

    if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_OBJECT_PATH, &path,
                    DBUS_TYPE_INVALID))
        return __tds_error_invalid_args(msg);

    if (path[0] == '\0')
        return __tds_error_invalid_format(msg);

    ctx = gprs_context_by_path(gprs, path);
    if (ctx == NULL)
        return __tds_error_not_found(msg);

    if (ctx->active) {
        ctx->active = false;

        struct tds_error err;
        err.type = TDS_ERROR_TYPE_NO_ERROR;

        gprs_deactivate_for_remove(&err, ctx, msg);
        return NULL;
    }

    TDS_LOG_DEBUG("Unregistering context: %s", ctx->path);
    context_dbus_unregister(ctx);
    gprs->contexts = g_slist_remove(gprs->contexts, ctx);

    g_dbus_send_reply(conn, msg, DBUS_TYPE_INVALID);

    atompath = g_strdup(gprs->modem->path);
    g_dbus_emit_signal(conn, atompath, TDS_CONNECTION_MANAGER_INTERFACE,
                "ContextRemoved", DBUS_TYPE_OBJECT_PATH, &path,
                DBUS_TYPE_INVALID);

    return NULL;
}

static void gprs_deactivate_next(struct tds_gprs *gprs)
{
    struct pri_context *ctx;
    DBusConnection *conn;
    dbus_bool_t value;
    GSList *l;

    TDS_LOG_DEBUG("path : %s %x", gprs->modem->path, gprs );

    for (l = gprs->contexts; l; l = l->next) {
        ctx = (struct pri_context *) l->data;

        if (ctx->active == FALSE)
            continue;

        TDS_LOG_DEBUG("context : %s %x", ctx->path, ctx );
        value = ctx->active = FALSE;

        conn = tds_dbus_get_connection();

        tds_dbus_signal_property_changed(conn, ctx->path,
                        TDS_CONNECTION_CONTEXT_INTERFACE,
                        "Active", DBUS_TYPE_BOOLEAN, &value);

        return;
    }
}

static DBusMessage *gprs_deactivate_all(DBusConnection *conn,
                    DBusMessage *msg, void *data)
{
    TDS_LOG_DEBUG("TDS");
    struct tds_gprs *gprs = (struct tds_gprs *)data;
    GSList *l;

    if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_INVALID))
        return __tds_error_invalid_args(msg);

    gprs_deactivate_next(gprs);

    return dbus_message_new_method_return(msg);
}

static DBusMessage *gprs_get_contexts(DBusConnection *conn,
                    DBusMessage *msg, void *data)
{
    TDS_LOG_DEBUG("TDS");
    struct tds_gprs *gprs = (struct tds_gprs *)data;
    DBusMessage *reply;
    DBusMessageIter iter;
    DBusMessageIter array;
    DBusMessageIter entry, dict;
    const char *path;
    GSList *l;
    struct pri_context *ctx;

    reply = dbus_message_new_method_return(msg);
    if (reply == NULL)
        return NULL;

    dbus_message_iter_init_append(reply, &iter);

    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
                    DBUS_STRUCT_BEGIN_CHAR_AS_STRING
                    DBUS_TYPE_OBJECT_PATH_AS_STRING
                    DBUS_TYPE_ARRAY_AS_STRING
                    DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
                    DBUS_TYPE_STRING_AS_STRING
                    DBUS_TYPE_VARIANT_AS_STRING
                    DBUS_DICT_ENTRY_END_CHAR_AS_STRING
                    DBUS_STRUCT_END_CHAR_AS_STRING,
                    &array);

    for (l = gprs->contexts; l; l = l->next) {
        ctx = (struct pri_context *)(l->data);

        path = ctx->path;

        dbus_message_iter_open_container(&array, DBUS_TYPE_STRUCT,
                            NULL, &entry);
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_OBJECT_PATH,
                        &path);
        dbus_message_iter_open_container(&entry, DBUS_TYPE_ARRAY,
                    TDS_PROPERTIES_ARRAY_SIGNATURE,
                    &dict);

        append_context_properties(ctx, &dict);
        dbus_message_iter_close_container(&entry, &dict);
        dbus_message_iter_close_container(&array, &entry);
    }

    dbus_message_iter_close_container(&iter, &array);

    return reply;
}

void tds_gprs_add_context(struct tds_gprs *gprs, struct pri_context *pri_context)
{
    pri_context->gprs = gprs;

    TDS_LOG_DEBUG("TDS");

    struct context_settings *settings = (struct context_settings *)g_try_new0(struct context_settings, 1);
    if (settings == NULL)
        return;

    settings->interface = g_strdup("ppp0");

    TDS_LOG_DEBUG("TDS");

    struct ipv4_settings *ipv4 = (struct ipv4_settings *)g_try_new0(struct ipv4_settings, 1);
    if (ipv4 == NULL)
        return;

    TDS_LOG_DEBUG("TDS");

    ipv4->static_ip = TRUE;

    ipv4->ip = g_strdup("0.0.0.0");
    ipv4->netmask = g_strdup("255.255.255.0");
    ipv4->gateway = g_strdup("0.0.0.0");
    ipv4->proxy = g_strdup("");

    ipv4->dns = (char **)g_try_new0(char *, 3);
    ipv4->dns[0] = g_strdup("8.8.8.8");
    ipv4->dns[1] = g_strdup("8.8.4.4");
    ipv4->dns[2] = NULL;

    settings->ipv4 = ipv4;

    struct ipv6_settings *ipv6 = (struct ipv6_settings *)g_try_new0(struct ipv6_settings, 1);
    if (ipv6 == NULL)
        return;
    settings->ipv6 = ipv6;

    pri_context->settings = settings;
}

void tds_gprs_bearer_notify(struct tds_gprs *gprs, int bearer)
{
    DBusConnection *conn = tds_dbus_get_connection();
    const char *path;
    const char *value;

    if (gprs->bearer == bearer)
        return;

    gprs->bearer = bearer;
    path = "/lgemodem";
    value = packet_bearer_to_string(bearer);
    tds_dbus_signal_property_changed(conn, path,
                    TDS_CONNECTION_CONTEXT_INTERFACE,
                    "Bearer", DBUS_TYPE_STRING, &value);
}

void tds_gprs_context_set_interface(struct pri_context *pri_context,
                    const char *interface)
{
    struct context_settings *settings = (struct context_settings *)pri_context->settings;

    g_free(settings->interface);
    settings->interface = g_strdup(interface);
}

void tds_gprs_context_set_ipv4_address(struct pri_context *pri_context,
                        const char *address,
                        tds_bool_t static_ip)
{
    struct context_settings *settings = (struct context_settings *) pri_context->settings;

    if (settings->ipv4 == NULL)
        return;

    g_free(settings->ipv4->ip);
    settings->ipv4->ip = g_strdup(address);
    settings->ipv4->static_ip = static_ip;
}

void tds_gprs_context_set_ipv4_netmask(struct pri_context *pri_context,
                        const char *netmask)
{
    struct context_settings *settings = (struct context_settings *) pri_context->settings;

    if (settings->ipv4 == NULL)
        return;

    g_free(settings->ipv4->netmask);
    settings->ipv4->netmask = g_strdup(netmask);
}

void tds_gprs_context_set_ipv4_gateway(struct pri_context *pri_context,
                        const char *gateway)
{
    struct context_settings *settings = (struct context_settings *)pri_context->settings;

    if (settings->ipv4 == NULL)
        return;

    g_free(settings->ipv4->gateway);
    settings->ipv4->gateway = g_strdup(gateway);
}

void tds_gprs_context_set_ipv4_dns_servers(struct pri_context *pri_context,
                        const char **dns)
{
    struct context_settings *settings = (struct context_settings *) pri_context->settings;

    if (settings->ipv4 == NULL)
        return;

    g_strfreev(settings->ipv4->dns);
    settings->ipv4->dns = g_strdupv((char **) dns);
}

void tds_gprs_context_set_ipv6_address(struct pri_context *pri_context,
                        const char *address)
{
    struct context_settings *settings = (struct context_settings *) pri_context->settings;

    if (settings->ipv6 == NULL)
        return;

    g_free(settings->ipv6->ip);
    settings->ipv6->ip = g_strdup(address);
}

void tds_gprs_context_set_ipv6_prefix_length(struct pri_context *pri_context,
                        unsigned char length)
{
    struct context_settings *settings = (struct context_settings *) pri_context->settings;

    if (settings->ipv6 == NULL)
        return;

    settings->ipv6->prefix_len = length;
}

void tds_gprs_context_set_ipv6_gateway(struct pri_context *pri_context,
                        const char *gateway)
{
    struct context_settings *settings = (struct context_settings *) pri_context->settings;

    if (settings->ipv6 == NULL)
        return;

    g_free(settings->ipv6->gateway);
    settings->ipv6->gateway = g_strdup(gateway);
}

void tds_gprs_context_set_ipv6_dns_servers(struct pri_context *pri_context,
                        const char **dns)
{
    struct context_settings *settings = (struct context_settings *) pri_context->settings;

    if (settings->ipv6 == NULL)
        return;

    g_strfreev(settings->ipv6->dns);
    settings->ipv6->dns = g_strdupv((char **) dns);
}

static void free_contexts(struct tds_gprs *gprs)
{
    GSList *l;

    g_free(gprs->imsi);
    gprs->imsi = NULL;

    for (l = gprs->contexts; l; l = l->next) {
        struct pri_context *context = (struct pri_context *) l->data;

        context_dbus_unregister(context);
    }

    g_slist_free(gprs->contexts);
}

static void gprs_unregister(struct tds_atom *atom)
{
    DBusConnection *conn = tds_dbus_get_connection();
    const char *path = "/lgemodem";

    g_dbus_unregister_interface(conn, path,
                    TDS_CONNECTION_MANAGER_INTERFACE);
}

static void gprs_remove(struct tds_atom *atom)
{
}

struct tds_gprs *tds_gprs_create(struct tds_modem *modem,
                    unsigned int vendor)
{
    struct tds_gprs *gprs;
    GSList *l;

    gprs = (struct tds_gprs *) g_try_new0(struct tds_gprs, 1);
    if (gprs == NULL)
        return NULL;

    gprs->modem = modem;
    gprs->status = NETWORK_REGISTRATION_STATUS_UNKNOWN;
    gprs->netreg_status = NETWORK_REGISTRATION_STATUS_UNKNOWN;
    gprs->powered = false;
    gprs->last_context_id=0;

    modem->gprs = gprs;

    return gprs;
}


static void tds_gprs_finish_register(struct tds_gprs *gprs)
{
    DBusConnection *conn = tds_dbus_get_connection();
    const char *path = g_strdup(gprs->modem->path);

    static GDBusMethodTable manager_methods[7];
    memset(&manager_methods[0], 0, sizeof(manager_methods));

    manager_methods[0].name = "GetProperties";
    manager_methods[0].in_args = NULL;
    manager_methods[0].out_args = GDBUS_ARGS({ "properties", "a{sv}" });
    manager_methods[0].function = gprs_get_properties;

    manager_methods[1].name = "SetProperty";
    manager_methods[1].in_args = GDBUS_ARGS({ "property", "s" }, { "value", "v" });
    manager_methods[1].out_args = NULL;
    manager_methods[1].function = gprs_set_property;

    manager_methods[2].name = "AddContext";
    manager_methods[2].in_args = GDBUS_ARGS({ "type", "s" });
    manager_methods[2].out_args = GDBUS_ARGS({ "path", "o" });
    manager_methods[2].function = gprs_add_context;

    manager_methods[3].name = "RemoveContext";
    manager_methods[3].in_args = GDBUS_ARGS({ "path", "o" });
    manager_methods[3].out_args = NULL;
    manager_methods[3].function = gprs_remove_context;

    manager_methods[4].name = "DeactivateAll";
    manager_methods[4].in_args = NULL;
    manager_methods[4].out_args = NULL;
    manager_methods[4].function = gprs_deactivate_all;

    manager_methods[5].name = "GetContexts";
    manager_methods[5].in_args = NULL;
    manager_methods[5].out_args = GDBUS_ARGS({ "contexts_with_properties", "a(oa{sv})" });
    manager_methods[5].function = gprs_get_contexts;

    static GDBusSignalTable manager_signals[4];
    memset(&manager_signals[0], 0, sizeof(manager_signals));

    manager_signals[0].name = "PropertyChanged";
    manager_signals[0].args = (const GDBusArgInfo[]) { { "name", "s" }, { "value", "v" }, { } };
    manager_signals[1].name = "ContextAdded";

    manager_signals[1].args = (const GDBusArgInfo[]) { { "path", "o" }, { "properties", "v" }, { } };
    manager_signals[2].name = "ContextRemoved";
    manager_signals[2].args = (const GDBusArgInfo[]) { { "path", "o" }, { } };

    if (!g_dbus_register_interface(conn, path,
                    TDS_CONNECTION_MANAGER_INTERFACE,
                    manager_methods, manager_signals, NULL,
                    gprs, NULL)) {
        TDS_LOG_DEBUG("Could not create %s interface",
                TDS_CONNECTION_MANAGER_INTERFACE);

        free_contexts(gprs);
        return;
    }

    TDS_LOG_DEBUG("TDS");
}

struct pri_context *__tds_gprs_manager_init(struct tds_modem* modem)
{
    TDS_LOG_DEBUG("TDS");

    struct tds_gprs *gprs = tds_gprs_create(modem, 01);
    tds_gprs_finish_register(gprs);

    struct pri_context *ConnContext;
    const char *name;
    name = gprs_context_default_name(TDS_GPRS_CONTEXT_TYPE_INTERNET);
    ConnContext = add_context(gprs, name, TDS_GPRS_CONTEXT_TYPE_INTERNET);

    TDS_LOG_DEBUG("(ConnectionContext path) pri_context->path=[%s], (Modem path) pri_context->tds_gprs->tds_modem->path=[%s]",
        ConnContext->path, ConnContext->gprs->modem->path );

    return ConnContext;
}

