// (c) Copyright 2013 LG Electronics, Inc.

#ifndef __TDS_DBUS_H
#define __TDS_DBUS_H

#include <dbus/dbus.h>

#define TDS_SERVICE             "com.lge.tds"
#define TDS_MANAGER_INTERFACE   "com.lge.tds.Manager"
#define TDS_MANAGER_PATH        "/"
#define TDS_MODEM_INTERFACE     "com.lge.tds.Modem"
#define TDS_CONNECTION_CONTEXT_INTERFACE "com.lge.tds.ConnectionContext"
#define TDS_CONNECTION_MANAGER_INTERFACE "com.lge.tds.ConnectionManager"

#define TDS_PROPERTIES_ARRAY_SIGNATURE DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING \
                    DBUS_TYPE_STRING_AS_STRING \
                    DBUS_TYPE_VARIANT_AS_STRING \
                    DBUS_DICT_ENTRY_END_CHAR_AS_STRING

DBusConnection *tds_dbus_get_connection(void);

void tds_dbus_dict_append(DBusMessageIter *dict, const char *key, int type,
                void *value);

void tds_dbus_dict_append_array(DBusMessageIter *dict, const char *key,
                    int type, void *val);

void tds_dbus_dict_append_dict(DBusMessageIter *dict, const char *key,
                    int type, void *val);

int tds_dbus_signal_property_changed(DBusConnection *conn, const char *path,
                    const char *interface, const char *name,
                    int type, void *value);

int tds_dbus_signal_array_property_changed(DBusConnection *conn,
                        const char *path,
                        const char *interface,
                        const char *name, int type,
                        void *value);

int tds_dbus_signal_dict_property_changed(DBusConnection *conn,
                        const char *path,
                        const char *interface,
                        const char *name, int type,
                        void *value);

#endif /* __TDS_DBUS_H */
