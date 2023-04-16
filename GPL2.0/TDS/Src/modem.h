// (c) Copyright 2013 LG Electronics, Inc.
#ifndef __TDS_MODEM_H
#define __TDS_MODEM_H

#include "tds_types.h"

enum property_type {
    PROPERTY_TYPE_INVALID = 0,
    PROPERTY_TYPE_STRING,
    PROPERTY_TYPE_INTEGER,
    PROPERTY_TYPE_BOOLEAN,
};

struct tds_modem {
    char                    *path;
    GSList                  *interface_list;
    GSList                  *feature_list;
    unsigned int            call_ids;
    DBusMessage             *pending;
    guint                   interface_update;
    tds_bool_t              powered;
    tds_bool_t              powered_pending;
    tds_bool_t              get_online;
    tds_bool_t              lockdown;
    char                    *lock_owner;
    guint                   lock_watch;
    guint                   timeout;
    tds_bool_t              online;
    struct tds_watchlist    *online_watches;
    struct tds_watchlist    *powered_watches;
    guint                   emergency;
    GHashTable              *properties;
    struct tds_gprs         *gprs;
};

struct modem_property {
    enum property_type type;
    void *value;
};

enum tds_modem_type {
    TDS_MODEM_TYPE_HARDWARE = 0,
    TDS_MODEM_TYPE_HFP,
    TDS_MODEM_TYPE_SAP,
};

typedef void (*tds_modem_online_cb_t)(const struct tds_error *error,
                    void *data);


void tds_modem_add_interface(struct tds_modem *modem,
                const char *interface);
void tds_modem_remove_interface(struct tds_modem *modem,
                    const char *interface);

const char *tds_modem_get_path(struct tds_modem *modem);

void tds_modem_set_data(struct tds_modem *modem, void *data);
void *tds_modem_get_data(struct tds_modem *modem);

struct tds_modem *tds_modem_create(const char *name, const char *type);
int tds_modem_register(struct tds_modem *modem);

tds_bool_t tds_modem_is_registered(struct tds_modem *modem);
void tds_modem_remove(struct tds_modem *modem);

void tds_modem_reset(struct tds_modem *modem);

void tds_modem_set_powered(struct tds_modem *modem, tds_bool_t powered);
tds_bool_t tds_modem_get_powered(struct tds_modem *modem);

tds_bool_t tds_modem_get_online(struct tds_modem *modem);

tds_bool_t tds_modem_get_emergency_mode(struct tds_modem *modem);

int tds_modem_set_string(struct tds_modem *modem,
                const char *key, const char *value);
const char *tds_modem_get_string(struct tds_modem *modem, const char *key);

int tds_modem_set_integer(struct tds_modem *modem,
                const char *key, int value);
int tds_modem_get_integer(struct tds_modem *modem, const char *key);

int tds_modem_set_boolean(struct tds_modem *modem,
                const char *key, tds_bool_t value);
tds_bool_t tds_modem_get_boolean(struct tds_modem *modem,
                    const char *key);

#endif /* __TDS_MODEM_H */
