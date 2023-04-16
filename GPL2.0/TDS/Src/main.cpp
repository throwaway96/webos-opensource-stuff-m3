// (c) Copyright 2013 LG Electronics, Inc.

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/signalfd.h>

#include <gdbus.h>

#include "tds.h"
#include "tds_dbus.h"
#include "luna_helper.h"
#include "luna_svc_mgr.h"
#include "til_handler.h"
#include "logging.h"

LunaIpcHelper lunaCm("com.palm.telephonydataservice");

extern pri_context *g_pri_context;

PmLogContext gLogContext;

static const char* const kLogContextName = "telephonydataservice";

static void system_bus_disconnected(DBusConnection *conn, void *user_data)
{
    TDS_LOG_DEBUG("System bus has disconnected");
}

int main (int argc, char *argv[])
{
    DBusConnection *conn;
    DBusError error;
    int retval = 0;

    (void)PmLogGetContext(kLogContextName, &gLogContext);

    dbus_error_init(&error);

    conn = g_dbus_setup_bus(DBUS_BUS_SYSTEM, TDS_SERVICE, &error);
    if (conn == NULL) {
        if (dbus_error_is_set(&error) == TRUE) {
            TDS_LOG_DEBUG("Unable to hop onto D-Bus: %s", error.message);
            dbus_error_free(&error);
        } else {
            TDS_LOG_DEBUG("Unable to hop onto D-Bus");
        }
        return 0;
    }

    g_dbus_set_disconnect_function(conn, system_bus_disconnected, NULL, NULL);

    __tds_dbus_init(conn);
    __tds_manager_init();

    struct tds_modem* modem = __tds_modem_dummy_init();

    g_pri_context = __tds_gprs_manager_init(modem);

    ServiceMgr::getInstance().registerServices();
    if (!ServiceMgr::getInstance().start()) {
        TDS_LOG_DEBUG("Failed to Start TDS Properly");
        retval = 6;
    }

    __tds_dbus_cleanup();
    dbus_connection_unref(conn);
	return retval;
}

