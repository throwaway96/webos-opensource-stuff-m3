// (c) Copyright 2013 LG Electronics, Inc.

#include <glib.h>
#include <gdbus.h>
#include <stdio.h>

#include "tds.h"
#include "logging.h"

#define TDS_ERROR_INTERFACE "com.lge.tds.Error"

static DBusConnection *g_connection;

struct error_mapping_entry {
    int error;
    DBusMessage *(*tds_error_func)(DBusMessage *);
};

struct error_mapping_entry cme_errors_mapping[] = {
    { 3,    __tds_error_not_allowed },
    { 4,    __tds_error_not_supported },
    { 16,    __tds_error_incorrect_password },
    { 30,    __tds_error_not_registered },
    { 31,    __tds_error_timed_out },
    { 32,    __tds_error_access_denied },
    { 50,    __tds_error_invalid_args },
};

static void append_variant(DBusMessageIter *iter,
                int type, void *value)
{
    char sig[2];
    DBusMessageIter valueiter;

    sig[0] = type;
    sig[1] = 0;

    dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT,
                        sig, &valueiter);

    dbus_message_iter_append_basic(&valueiter, type, value);

    dbus_message_iter_close_container(iter, &valueiter);
}

void tds_dbus_dict_append(DBusMessageIter *dict,
            const char *key, int type, void *value)
{
    DBusMessageIter keyiter;

    if (type == DBUS_TYPE_STRING) {
        const char *str = *((const char **) value);
        if (str == NULL)
            return;
    }

    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY,
                            NULL, &keyiter);

    dbus_message_iter_append_basic(&keyiter, DBUS_TYPE_STRING, &key);

    append_variant(&keyiter, type, value);

    dbus_message_iter_close_container(dict, &keyiter);
}

static void append_array_variant(DBusMessageIter *iter, int type, void *val)
{
    DBusMessageIter variant, array;
    char typesig[2];
    char arraysig[3];
    const char **str_array = *(const char ***) val;
    int i;

    arraysig[0] = DBUS_TYPE_ARRAY;
    arraysig[1] = typesig[0] = type;
    arraysig[2] = typesig[1] = '\0';

    dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT,
                        arraysig, &variant);

    dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY,
                        typesig, &array);

    for (i = 0; str_array[i]; i++)
        dbus_message_iter_append_basic(&array, type,
                        &(str_array[i]));

    dbus_message_iter_close_container(&variant, &array);

    dbus_message_iter_close_container(iter, &variant);
}

void tds_dbus_dict_append_array(DBusMessageIter *dict, const char *key,
                int type, void *val)
{
    DBusMessageIter entry;

    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY,
                        NULL, &entry);

    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);

    append_array_variant(&entry, type, val);

    dbus_message_iter_close_container(dict, &entry);
}

static void append_dict_variant(DBusMessageIter *iter, int type, void *val)
{
    DBusMessageIter variant, array, entry;
    char typesig[5];
    char arraysig[6];
    const void **val_array = *(const void ***) val;
    int i;

    arraysig[0] = DBUS_TYPE_ARRAY;
    arraysig[1] = typesig[0] = DBUS_DICT_ENTRY_BEGIN_CHAR;
    arraysig[2] = typesig[1] = DBUS_TYPE_STRING;
    arraysig[3] = typesig[2] = type;
    arraysig[4] = typesig[3] = DBUS_DICT_ENTRY_END_CHAR;
    arraysig[5] = typesig[4] = '\0';

    dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT,
                        arraysig, &variant);

    dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY,
                        typesig, &array);

    for (i = 0; val_array[i]; i += 2) {
        dbus_message_iter_open_container(&array, DBUS_TYPE_DICT_ENTRY,
                            NULL, &entry);

        dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING,
                        &(val_array[i + 0]));

        /*
         * D-Bus expects a char** or uint8* depending on the type
         * given. Since we are dealing with an array through a void**
         * (and thus val_array[i] is a pointer) we need to
         * differentiate DBUS_TYPE_STRING from the others. The other
         * option would be the user to pass the exact type to this
         * function, instead of a pointer to it. However in this case
         * a cast from type to void* would be needed, which is not
         * good.
         */
        if (type == DBUS_TYPE_STRING) {
            dbus_message_iter_append_basic(&entry, type,
                            &(val_array[i + 1]));
        } else {
            dbus_message_iter_append_basic(&entry, type,
                            val_array[i + 1]);
        }

        dbus_message_iter_close_container(&array, &entry);
    }

    dbus_message_iter_close_container(&variant, &array);

    dbus_message_iter_close_container(iter, &variant);
}

void tds_dbus_dict_append_dict(DBusMessageIter *dict, const char *key,
                int type, void *val)
{
    DBusMessageIter entry;

    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY,
                        NULL, &entry);

    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);

    append_dict_variant(&entry, type, val);

    dbus_message_iter_close_container(dict, &entry);
}

int tds_dbus_signal_property_changed(DBusConnection *conn,
                    const char *path,
                    const char *interface,
                    const char *name,
                    int type, void *value)
{
    DBusMessage *signal;
    DBusMessageIter iter;

    signal = dbus_message_new_signal(path, interface, "PropertyChanged");
    if (signal == NULL) {
        TDS_LOG_DEBUG("Unable to allocate new %s.PropertyChanged signal",
                interface);
        return -1;
    }

    dbus_message_iter_init_append(signal, &iter);

    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &name);

    append_variant(&iter, type, value);

    return g_dbus_send_message(conn, signal);
}

int tds_dbus_signal_array_property_changed(DBusConnection *conn,
                        const char *path,
                        const char *interface,
                        const char *name,
                        int type, void *value)

{
    DBusMessage *signal;
    DBusMessageIter iter;

    signal = dbus_message_new_signal(path, interface, "PropertyChanged");
    if (signal == NULL) {
        TDS_LOG_DEBUG("Unable to allocate new %s.PropertyChanged signal",
                interface);
        return -1;
    }

    dbus_message_iter_init_append(signal, &iter);

    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &name);

    append_array_variant(&iter, type, value);

    return g_dbus_send_message(conn, signal);
}

int tds_dbus_signal_dict_property_changed(DBusConnection *conn,
                        const char *path,
                        const char *interface,
                        const char *name,
                        int type, void *value)

{
    DBusMessage *signal;
    DBusMessageIter iter;

    signal = dbus_message_new_signal(path, interface, "PropertyChanged");
    if (signal == NULL) {
        TDS_LOG_DEBUG("Unable to allocate new %s.PropertyChanged signal",
                interface);
        return -1;
    }

    dbus_message_iter_init_append(signal, &iter);

    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &name);

    append_dict_variant(&iter, type, value);

    return g_dbus_send_message(conn, signal);
}

DBusMessage *__tds_error_invalid_args(DBusMessage *msg)
{
    return g_dbus_create_error(msg, TDS_ERROR_INTERFACE
                    ".InvalidArguments",
                    "Invalid arguments in method call");
}

DBusMessage *__tds_error_invalid_format(DBusMessage *msg)
{
    return g_dbus_create_error(msg, TDS_ERROR_INTERFACE
                    ".InvalidFormat",
                    "Argument format is not recognized");
}

DBusMessage *__tds_error_not_implemented(DBusMessage *msg)
{
    return g_dbus_create_error(msg, TDS_ERROR_INTERFACE
                    ".NotImplemented",
                    "Implementation not provided");
}

DBusMessage *__tds_error_failed(DBusMessage *msg)
{
    return g_dbus_create_error(msg, TDS_ERROR_INTERFACE ".Failed",
                    "Operation failed");
}

DBusMessage *__tds_error_busy(DBusMessage *msg)
{
    return g_dbus_create_error(msg, TDS_ERROR_INTERFACE ".InProgress",
                    "Operation already in progress");
}

DBusMessage *__tds_error_not_found(DBusMessage *msg)
{
    return g_dbus_create_error(msg, TDS_ERROR_INTERFACE ".NotFound",
            "Object is not found or not valid for this operation");
}

DBusMessage *__tds_error_not_active(DBusMessage *msg)
{
    return g_dbus_create_error(msg, TDS_ERROR_INTERFACE ".NotActive",
            "Operation is not active or in progress");
}

DBusMessage *__tds_error_not_supported(DBusMessage *msg)
{
    return g_dbus_create_error(msg, TDS_ERROR_INTERFACE
                    ".NotSupported",
                    "Operation is not supported by the"
                    " network / modem");
}

DBusMessage *__tds_error_not_available(DBusMessage *msg)
{
    return g_dbus_create_error(msg, TDS_ERROR_INTERFACE
                    ".NotAvailable",
                    "Operation currently not available");
}

DBusMessage *__tds_error_timed_out(DBusMessage *msg)
{
    return g_dbus_create_error(msg, TDS_ERROR_INTERFACE ".Timedout",
            "Operation failure due to timeout");
}

DBusMessage *__tds_error_sim_not_ready(DBusMessage *msg)
{
    return g_dbus_create_error(msg, TDS_ERROR_INTERFACE ".SimNotReady",
            "SIM is not ready or not inserted");
}

DBusMessage *__tds_error_in_use(DBusMessage *msg)
{
    return g_dbus_create_error(msg, TDS_ERROR_INTERFACE ".InUse",
            "The resource is currently in use");
}

DBusMessage *__tds_error_not_attached(DBusMessage *msg)
{
    return g_dbus_create_error(msg, TDS_ERROR_INTERFACE ".NotAttached",
            "GPRS is not attached");
}

DBusMessage *__tds_error_attach_in_progress(DBusMessage *msg)
{
    return g_dbus_create_error(msg,
                TDS_ERROR_INTERFACE ".AttachInProgress",
                "GPRS Attach is in progress");
}

DBusMessage *__tds_error_not_registered(DBusMessage *msg)
{
    return g_dbus_create_error(msg,
                TDS_ERROR_INTERFACE ".NotRegistered",
                "Modem is not registered to the network");
}

DBusMessage *__tds_error_canceled(DBusMessage *msg)
{
    return g_dbus_create_error(msg, TDS_ERROR_INTERFACE ".Canceled",
                    "Operation has been canceled");
}

DBusMessage *__tds_error_access_denied(DBusMessage *msg)
{
    return g_dbus_create_error(msg, TDS_ERROR_INTERFACE ".AccessDenied",
                    "Operation not permitted");
}

DBusMessage *__tds_error_emergency_active(DBusMessage *msg)
{
    return g_dbus_create_error(msg,
                TDS_ERROR_INTERFACE ".EmergencyActive",
                "Emergency mode active");
}

DBusMessage *__tds_error_incorrect_password(DBusMessage *msg)
{
    return g_dbus_create_error(msg,
                TDS_ERROR_INTERFACE ".IncorrectPassword",
                "Password is incorrect");
}

DBusMessage *__tds_error_not_allowed(DBusMessage *msg)
{
    return g_dbus_create_error(msg, TDS_ERROR_INTERFACE ".NotAllowed",
                    "Operation is not allowed");
}

DBusMessage *__tds_error_not_recognized(DBusMessage *msg)
{
    return g_dbus_create_error(msg, TDS_ERROR_INTERFACE ".NotRecognized",
                    "String not recognized as USSD/SS");
}

DBusMessage *__tds_error_from_error(const struct tds_error *error,
                        DBusMessage *msg)
{
    struct error_mapping_entry *e;
    int maxentries;
    int i;

    switch (error->type) {
    case TDS_ERROR_TYPE_CME:
        e = cme_errors_mapping;
        maxentries = sizeof(cme_errors_mapping) /
                    sizeof(struct error_mapping_entry);
        for (i = 0; i < maxentries; i++)
            if (e[i].error == error->error)
                return e[i].tds_error_func(msg);
        break;
    case TDS_ERROR_TYPE_CMS:
        return __tds_error_failed(msg);
    case TDS_ERROR_TYPE_CEER:
        return __tds_error_failed(msg);
    default:
        return __tds_error_failed(msg);
    }

    return __tds_error_failed(msg);
}

void __tds_dbus_pending_reply(DBusMessage **msg, DBusMessage *reply)
{
    TDS_LOG_DEBUG("TDS");

    DBusConnection *conn = tds_dbus_get_connection();

    g_dbus_send_message(conn, reply);

    dbus_message_unref(*msg);
    *msg = NULL;
}

gboolean __tds_dbus_valid_object_path(const char *path)
{
    unsigned int i;
    char c = '\0';

    if (path == NULL)
        return FALSE;

    if (path[0] == '\0')
        return FALSE;

    if (path[0] && !path[1] && path[0] == '/')
        return TRUE;

    if (path[0] != '/')
        return FALSE;

    for (i = 0; path[i]; i++) {
        if (path[i] == '/' && c == '/')
            return FALSE;

        c = path[i];

        if (path[i] >= 'a' && path[i] <= 'z')
            continue;

        if (path[i] >= 'A' && path[i] <= 'Z')
            continue;

        if (path[i] >= '0' && path[i] <= '9')
            continue;

        if (path[i] == '_' || path[i] == '/')
            continue;

        return FALSE;
    }

    if (path[i-1] == '/')
        return FALSE;

    return TRUE;
}

DBusConnection *tds_dbus_get_connection(void)
{
    return g_connection;
}

static void dbus_gsm_set_connection(DBusConnection *conn)
{
    if (conn && g_connection != NULL)
        TDS_LOG_DEBUG("Setting a connection when it is not NULL");

    g_connection = conn;
}

int __tds_dbus_init(DBusConnection *conn)
{
    dbus_gsm_set_connection(conn);

    return 0;
}

void __tds_dbus_cleanup(void)
{
    DBusConnection *conn = tds_dbus_get_connection();

    if (conn == NULL || !dbus_connection_get_is_connected(conn))
        return;

    dbus_gsm_set_connection(NULL);
}

