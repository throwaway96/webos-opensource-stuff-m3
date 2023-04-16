// (c) Copyright 2013 LG Electronics, Inc.

#include <string.h>
#include <glib.h>
#include <gdbus.h>

#include "tds.h"
#include "logging.h"


static void append_modem(struct tds_modem *modem, void *userdata)
{
    DBusMessageIter *array = (DBusMessageIter *) userdata;
    const char *path = tds_modem_get_path(modem);
    DBusMessageIter entry, dict;

    dbus_message_iter_open_container(array, DBUS_TYPE_STRUCT,
                        NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_OBJECT_PATH,
                    &path);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_ARRAY,
                TDS_PROPERTIES_ARRAY_SIGNATURE,
                &dict);

    __tds_modem_append_properties(modem, &dict);

    dbus_message_iter_close_container(&entry, &dict);
    dbus_message_iter_close_container(array, &entry);

    TDS_LOG_DEBUG("TDS");
}


static DBusMessage *manager_get_modems(DBusConnection *conn,
                    DBusMessage *msg, void *data)
{
    TDS_LOG_DEBUG("TDS");

    DBusMessage *reply;
    DBusMessageIter iter;
    DBusMessageIter array;

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

    __tds_modem_foreach(append_modem, &array);

    dbus_message_iter_close_container(&iter, &array);

    TDS_LOG_DEBUG("TDS");

    return reply;
}

int __tds_manager_init(void)
{
    TDS_LOG_DEBUG("TDS");
    DBusConnection *conn = tds_dbus_get_connection();
    gboolean ret;

    static GDBusMethodTable manager_methods[2];
    memset(&manager_methods[0], 0, sizeof(manager_methods));

    manager_methods[0].name = "GetModems";
    manager_methods[0].in_args = NULL;
    manager_methods[0].out_args = GDBUS_ARGS({ "modems", "a(oa{sv})" });
    manager_methods[0].function = manager_get_modems;

    static GDBusSignalTable manager_signals[3];
    memset(&manager_signals[0], 0, sizeof(manager_signals));

    manager_signals[0].name = "ModemAdded";
    manager_signals[0].args = GDBUS_ARGS({ "path", "o" }, { "properties", "a{sv}" });
    manager_signals[1].name = "ModemRemoved";
    manager_signals[1].args = GDBUS_ARGS({ "path", "o" });

    ret = g_dbus_register_interface(conn, TDS_MANAGER_PATH,
                    TDS_MANAGER_INTERFACE,
                    manager_methods, manager_signals,
                    NULL, NULL, NULL);

    if (ret == FALSE)
        return -1;

    return 0;
}

void __tds_manager_cleanup(void)
{
    DBusConnection *conn = tds_dbus_get_connection();

    g_dbus_unregister_interface(conn, TDS_MANAGER_PATH,
                    TDS_MANAGER_INTERFACE);
}

