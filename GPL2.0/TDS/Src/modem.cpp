// (c) Copyright 2013 LG Electronics, Inc.

#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <glib.h>
#include <gdbus.h>

#include "tds.h"
#include "logging.h"


static GSList *g_modem_list = NULL;

static int next_modem_id = 0;

const char *tds_modem_get_path(struct tds_modem *modem)
{
    if (modem)
        return modem->path;

    return NULL;
}

static int set_modem_property(struct tds_modem *modem, const char *name,
                enum property_type type, const void *value)
{
    struct modem_property *property;

    TDS_LOG_DEBUG("modem %p property %s", modem, name);

    if (type != PROPERTY_TYPE_STRING &&
            type != PROPERTY_TYPE_INTEGER)
        return -EINVAL;

    property = g_try_new0(struct modem_property, 1);
    if (property == NULL)
        return -ENOMEM;

    property->type = type;

    switch (type) {
    case PROPERTY_TYPE_STRING:
        property->value = g_strdup((const char *) value);
        break;
    case PROPERTY_TYPE_INTEGER:
        property->value = g_memdup(value, sizeof(int));
        break;
    case PROPERTY_TYPE_BOOLEAN:
        property->value = g_memdup(value, sizeof(tds_bool_t));
        break;
    default:
        break;
    }

    g_hash_table_replace(modem->properties, g_strdup(name), property);

    return 0;
}

static gboolean get_modem_property(struct tds_modem *modem, const char *name,
                    enum property_type type,
                    void *value)
{
    struct modem_property *property;

    TDS_LOG_DEBUG("modem %p property %s", modem, name);

    property = (struct modem_property *) g_hash_table_lookup(modem->properties, name);

    if (property == NULL)
        return FALSE;

    if (property->type != type)
        return FALSE;

    switch (property->type) {
    case PROPERTY_TYPE_STRING:
        *((const char **) value) = (char *)property->value;
        return TRUE;
    case PROPERTY_TYPE_INTEGER:
        memcpy(value, property->value, sizeof(int));
        return TRUE;
    case PROPERTY_TYPE_BOOLEAN:
        memcpy(value, property->value, sizeof(tds_bool_t));
        return TRUE;
    default:
        return FALSE;
    }
}

int tds_modem_set_string(struct tds_modem *modem,
                const char *key, const char *value)
{
    return set_modem_property(modem, key, PROPERTY_TYPE_STRING, value);
}

int tds_modem_set_integer(struct tds_modem *modem,
                const char *key, int value)
{
    return set_modem_property(modem, key, PROPERTY_TYPE_INTEGER, &value);
}

int tds_modem_set_boolean(struct tds_modem *modem,
                const char *key, tds_bool_t value)
{
    return set_modem_property(modem, key, PROPERTY_TYPE_BOOLEAN, &value);
}

const char *tds_modem_get_string(struct tds_modem *modem, const char *key)
{
    const char *value;

    if (get_modem_property(modem, key,
                PROPERTY_TYPE_STRING, &value) == FALSE)
        return NULL;

    return value;
}

int tds_modem_get_integer(struct tds_modem *modem, const char *key)
{
    int value;

    if (get_modem_property(modem, key,
                PROPERTY_TYPE_INTEGER, &value) == FALSE)
        return 0;

    return value;
}

tds_bool_t tds_modem_get_boolean(struct tds_modem *modem, const char *key)
{
    tds_bool_t value;

    if (get_modem_property(modem, key,
                PROPERTY_TYPE_BOOLEAN, &value) == FALSE)
        return FALSE;

    return value;
}

void __tds_modem_append_properties(struct tds_modem *modem,
                        DBusMessageIter *dict)
{
    TDS_LOG_DEBUG("TDS");

    char **interfaces;
    char **features;
    int i;
    GSList *l;
    const char *strtype;

    tds_dbus_dict_append(dict, "Online", DBUS_TYPE_BOOLEAN,
                &modem->online);

    tds_dbus_dict_append(dict, "Powered", DBUS_TYPE_BOOLEAN,
                &modem->powered);

    tds_dbus_dict_append(dict, "Lockdown", DBUS_TYPE_BOOLEAN,
                &modem->lockdown);

    tds_dbus_dict_append(dict, "Emergency", DBUS_TYPE_BOOLEAN,
                &modem->emergency);

    interfaces = g_new0(char *, g_slist_length(modem->interface_list) + 1);
    for (i = 0, l = modem->interface_list; l; l = l->next, i++)
        interfaces[i] = (char *) l->data;
    tds_dbus_dict_append_array(dict, "Interfaces", DBUS_TYPE_STRING,
                    &interfaces);
    g_free(interfaces);

    features = g_new0(char *, g_slist_length(modem->feature_list) + 1);
    for (i = 0, l = modem->feature_list; l; l = l->next, i++)
        features[i] = (char *) l->data;
    tds_dbus_dict_append_array(dict, "Features", DBUS_TYPE_STRING,
                    &features);
    g_free(features);

    strtype = g_strdup("hardware");
    tds_dbus_dict_append(dict, "Type", DBUS_TYPE_STRING, &strtype);
}



static void unregister_property(gpointer data)
{
    struct modem_property *property = (struct modem_property *) data;

    TDS_LOG_DEBUG("property %p", property);

    g_free(property->value);
    g_free(property);
}



struct tds_modem *tds_modem_create(const char *name, const char *type)
{
    struct tds_modem *modem;
    char path[128]={0,};

    TDS_LOG_DEBUG("TDS");
    TDS_LOG_DEBUG("name: %s, type: %s", name, type);

    if (strlen(type) > 16)
        return NULL;

    if (name && strlen(name) > 64)
        return NULL;

    if (name == NULL)
        snprintf(path, sizeof(path), "/%s_%d", type, next_modem_id);
    else
        snprintf(path, sizeof(path), "/%s", name);

    if (__tds_dbus_valid_object_path(path) == FALSE)
        return NULL;

    modem = g_try_new0(struct tds_modem, 1);

    if (modem == NULL)
        return modem;

    modem->path = g_strdup(path);
    modem->properties = g_hash_table_new_full(g_str_hash, g_str_equal,
                        g_free, unregister_property);

    g_modem_list = g_slist_prepend(g_modem_list, modem);

    if (name == NULL)
        next_modem_id += 1;

    TDS_LOG_DEBUG("TDS");

    return modem;
}



static void modem_unregister(struct tds_modem *modem)
{
    DBusConnection *conn = tds_dbus_get_connection();

    TDS_LOG_DEBUG("%p", modem);

    modem->online_watches = NULL;

    modem->powered_watches = NULL;

    g_slist_foreach(modem->interface_list, (GFunc) g_free, NULL);
    g_slist_free(modem->interface_list);
    modem->interface_list = NULL;

    g_slist_foreach(modem->feature_list, (GFunc) g_free, NULL);
    g_slist_free(modem->feature_list);
    modem->feature_list = NULL;

    if (modem->timeout) {
        g_source_remove(modem->timeout);
        modem->timeout = 0;
    }

    if (modem->pending) {
        dbus_message_unref(modem->pending);
        modem->pending = NULL;
    }

    if (modem->interface_update) {
        g_source_remove(modem->interface_update);
        modem->interface_update = 0;
    }

    g_dbus_unregister_interface(conn, modem->path, TDS_MODEM_INTERFACE);

    g_hash_table_destroy(modem->properties);
    modem->properties = NULL;

}

void tds_modem_remove(struct tds_modem *modem)
{
    TDS_LOG_DEBUG("%p", modem);

    if (modem == NULL)
        return;

    modem_unregister(modem);

    g_modem_list = g_slist_remove(g_modem_list, modem);

    g_free(modem->path);
    g_free(modem);
}


void __tds_modem_foreach(tds_modem_foreach_func func, void *userdata)
{
    struct tds_modem *modem;
    GSList *l;

    for (l = g_modem_list; l; l = l->next) {
        modem = (struct tds_modem *)l->data;
        func(modem, userdata);
    }
}

static DBusMessage *modem_get_properties(DBusConnection *conn,
                        DBusMessage *msg, void *data)
{
    TDS_LOG_DEBUG("TDS");
    struct tds_modem *modem = (struct tds_modem *)data;
    DBusMessage *reply;
    DBusMessageIter iter;
    DBusMessageIter dict;

    reply = dbus_message_new_method_return(msg);
    if (reply == NULL)
        return NULL;

    dbus_message_iter_init_append(reply, &iter);

    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
                    TDS_PROPERTIES_ARRAY_SIGNATURE,
                    &dict);
    __tds_modem_append_properties(modem, &dict);
    dbus_message_iter_close_container(&iter, &dict);

    return reply;
}

static DBusMessage *modem_set_property(DBusConnection *conn,
                    DBusMessage *msg, void *data)
{
    TDS_LOG_DEBUG("TDS");
    struct tds_modem *modem = (struct tds_modem *)data;
    DBusMessageIter iter, var;
    const char *name;

    if (dbus_message_iter_init(msg, &iter) == FALSE)
        return __tds_error_invalid_args(msg);

    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING)
        return __tds_error_invalid_args(msg);

    dbus_message_iter_get_basic(&iter, &name);
    dbus_message_iter_next(&iter);

    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_VARIANT)
        return __tds_error_invalid_args(msg);

    dbus_message_iter_recurse(&iter, &var);

	if(NULL != name)
		TDS_LOG_DEBUG("setting property [%s]",name);

    if (g_str_equal(name, "Online"))
    {
        tds_bool_t online;

        if (modem->powered == FALSE)
            return __tds_error_not_available(msg);

        if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_BOOLEAN)
            return __tds_error_invalid_args(msg);

        dbus_message_iter_get_basic(&var, &online);

        if (modem->online == online)
            return dbus_message_new_method_return(msg);

        modem->online = online;

        if (modem->online == online)
            return dbus_message_new_method_return(msg);

        g_dbus_send_reply(conn, msg, DBUS_TYPE_INVALID);

        tds_dbus_signal_property_changed(conn, modem->path,
                        TDS_MODEM_INTERFACE,
                        "Online", DBUS_TYPE_BOOLEAN,
                        &online);
        return NULL;
    }

    if (g_str_equal(name, "Powered") == TRUE) {
        tds_bool_t powered;

        if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_BOOLEAN)
            return __tds_error_invalid_args(msg);

        dbus_message_iter_get_basic(&var, &powered);

        modem->powered = powered;

        if (modem->powered == powered)
            return dbus_message_new_method_return(msg);

        g_dbus_send_reply(conn, msg, DBUS_TYPE_INVALID);

        tds_dbus_signal_property_changed(conn, modem->path,
                        TDS_MODEM_INTERFACE,
                        "Powered", DBUS_TYPE_BOOLEAN,
                        &powered);

        return NULL;
    }

    return __tds_error_invalid_args(msg);
}


int tds_modem_register(struct tds_modem *modem)
{
    DBusConnection *conn = tds_dbus_get_connection();

    TDS_LOG_DEBUG("%p", modem);

    if (modem == NULL)
        return -EINVAL;

    static GDBusMethodTable modem_methods[3];
    memset(&modem_methods[0], 0, sizeof(modem_methods));

    modem_methods[0].name = "GetProperties";
    modem_methods[0].in_args = NULL;
    modem_methods[0].out_args = GDBUS_ARGS({ "properties", "a{sv}" });
    modem_methods[0].function = modem_get_properties;

    modem_methods[1].name = "SetProperty";
    modem_methods[1].in_args = GDBUS_ARGS({ "property", "s" }, { "value", "v" });
    modem_methods[1].out_args = NULL;
    modem_methods[1].function = modem_set_property;

    static GDBusSignalTable modem_signals[2];
    memset(&modem_signals[0], 0, sizeof(modem_signals));

    modem_signals[0].name = "PropertyChanged";
    modem_signals[0].args = (const GDBusArgInfo[]) { { "name", "s" }, { "value", "v" }, { } };

    if (!g_dbus_register_interface(conn, modem->path,
                    TDS_MODEM_INTERFACE,
                    modem_methods, modem_signals, NULL,
                    modem, NULL)) {
        TDS_LOG_DEBUG("Modem register failed on path %s", modem->path);
        return -EIO;
    }

    return 0;
}


struct tds_modem* __tds_modem_dummy_init(void)
{
    struct tds_modem *modem = NULL;

    modem = (struct tds_modem *)tds_modem_create("lgemodem", "DummyModem");
    if(NULL==modem)
        return NULL;

    tds_modem_set_integer(modem, "Registered", 0);

    tds_modem_register(modem);

    return modem;
}

