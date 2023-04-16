// (c) Copyright 2013 LG Electronics, Inc.
#ifndef TDS_H_
#define TDS_H_

#include <stdio.h>
#include <glib.h>

#include "tds_types.h"

void __tds_exit(void);

int __tds_manager_init(void);
void __tds_manager_cleanup(void);

void __tds_modem_shutdown(void);

#include "tds_dbus.h"

int __tds_dbus_init(DBusConnection *conn);
void __tds_dbus_cleanup(void);

DBusMessage *__tds_error_invalid_args(DBusMessage *msg);
DBusMessage *__tds_error_invalid_format(DBusMessage *msg);
DBusMessage *__tds_error_not_implemented(DBusMessage *msg);
DBusMessage *__tds_error_failed(DBusMessage *msg);
DBusMessage *__tds_error_busy(DBusMessage *msg);
DBusMessage *__tds_error_not_found(DBusMessage *msg);
DBusMessage *__tds_error_not_active(DBusMessage *msg);
DBusMessage *__tds_error_not_supported(DBusMessage *msg);
DBusMessage *__tds_error_not_available(DBusMessage *msg);
DBusMessage *__tds_error_timed_out(DBusMessage *msg);
DBusMessage *__tds_error_sim_not_ready(DBusMessage *msg);
DBusMessage *__tds_error_in_use(DBusMessage *msg);
DBusMessage *__tds_error_not_attached(DBusMessage *msg);
DBusMessage *__tds_error_attach_in_progress(DBusMessage *msg);
DBusMessage *__tds_error_not_registered(DBusMessage *msg);
DBusMessage *__tds_error_canceled(DBusMessage *msg);
DBusMessage *__tds_error_access_denied(DBusMessage *msg);
DBusMessage *__tds_error_emergency_active(DBusMessage *msg);
DBusMessage *__tds_error_incorrect_password(DBusMessage *msg);
DBusMessage *__tds_error_not_allowed(DBusMessage *msg);
DBusMessage *__tds_error_not_recognized(DBusMessage *msg);

DBusMessage *__tds_error_from_error(const struct tds_error *error, DBusMessage *msg);

void __tds_dbus_pending_reply(DBusMessage **msg, DBusMessage *reply);

gboolean __tds_dbus_valid_object_path(const char *path);

struct tds_watchlist_item {
    unsigned int id;
    void *notify;
    void *notify_data;
    tds_destroy_func destroy;
};

struct tds_watchlist {
    int next_id;
    GSList *items;
    tds_destroy_func destroy;
};

struct tds_watchlist *__tds_watchlist_new(tds_destroy_func destroy);
unsigned int __tds_watchlist_add_item(struct tds_watchlist *watchlist,
                    struct tds_watchlist_item *item);
gboolean __tds_watchlist_remove_item(struct tds_watchlist *watchlist,
                    unsigned int id);
void __tds_watchlist_free(struct tds_watchlist *watchlist);


#include "modem.h"
typedef void (*tds_modem_foreach_func)(struct tds_modem *modem, void *data);
void __tds_modem_foreach(tds_modem_foreach_func cb, void *userdata);
unsigned int __tds_modem_callid_next(struct tds_modem *modem);
void __tds_modem_callid_hold(struct tds_modem *modem, int id);
void __tds_modem_callid_release(struct tds_modem *modem, int id);
void __tds_modem_append_properties(struct tds_modem *modem,
                        DBusMessageIter *dict);


#include "gprs.h"
#include "gprs-context.h"
struct pri_context *__tds_gprs_manager_init(struct tds_modem* modem);


#include "modem.h"
struct tds_modem* __tds_modem_dummy_init(void);


#endif /* TDS_H_ */
