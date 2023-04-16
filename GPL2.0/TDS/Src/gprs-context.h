// (c) Copyright 2013 LG Electronics, Inc.

#ifndef __TDS_GPRS_CONTEXT_H
#define __TDS_GPRS_CONTEXT_H

#define TDS_GPRS_MAX_APN_LENGTH 127
#define TDS_GPRS_MAX_USERNAME_LENGTH 63
#define TDS_GPRS_MAX_PASSWORD_LENGTH 255

enum tds_gprs_proto {
    TDS_GPRS_PROTO_IP = 0,
    TDS_GPRS_PROTO_IPV6,
    TDS_GPRS_PROTO_IPV4V6,
};

enum tds_gprs_context_type {
    TDS_GPRS_CONTEXT_TYPE_ANY = 0,
    TDS_GPRS_CONTEXT_TYPE_INTERNET,
    TDS_GPRS_CONTEXT_TYPE_MMS,
    TDS_GPRS_CONTEXT_TYPE_WAP,
    TDS_GPRS_CONTEXT_TYPE_IMS,
};

struct tds_gprs_primary_context {
    unsigned int cid;
    int direction;
    char apn[TDS_GPRS_MAX_APN_LENGTH + 1];
    char username[TDS_GPRS_MAX_USERNAME_LENGTH + 1];
    char password[TDS_GPRS_MAX_PASSWORD_LENGTH + 1];
    enum tds_gprs_proto proto;
};

typedef void (*tds_gprs_context_cb_t)(const struct tds_error *error,
                    void *data);

struct tds_gprs_context_driver {
    const char *name;
    int (*probe)(struct tds_gprs_context *gc, unsigned int vendor,
            void *data);
    void (*remove)(struct tds_gprs_context *gc);
    void (*activate_primary)(struct tds_gprs_context *gc,
                const struct tds_gprs_primary_context *ctx,
                tds_gprs_context_cb_t cb, void *data);
    void (*deactivate_primary)(struct tds_gprs_context *gc,
                    unsigned int id,
                    tds_gprs_context_cb_t cb, void *data);
    void (*detach_shutdown)(struct tds_gprs_context *gc,
                    unsigned int id);
};

void tds_gprs_context_deactivated(struct tds_gprs_context *gc,
                    unsigned int id);

int tds_gprs_context_driver_register(
                const struct tds_gprs_context_driver *d);
void tds_gprs_context_driver_unregister(
                const struct tds_gprs_context_driver *d);

struct tds_gprs_context *tds_gprs_context_create(struct tds_modem *modem,
                        unsigned int vendor,
                        const char *driver, void *data);
void tds_gprs_context_remove(struct tds_gprs_context *gc);

void tds_gprs_context_set_data(struct tds_gprs_context *gc, void *data);
void *tds_gprs_context_get_data(struct tds_gprs_context *gc);

struct tds_modem *tds_gprs_context_get_modem(struct tds_gprs_context *gc);

void tds_gprs_context_set_type(struct tds_gprs_context *gc,
                    enum tds_gprs_context_type type);

void tds_gprs_context_set_interface(struct tds_gprs_context *gc,
                    const char *interface);

void tds_gprs_context_set_ipv4_address(struct tds_gprs_context *gc,
                        const char *address,
                        tds_bool_t static_ip);
void tds_gprs_context_set_ipv4_netmask(struct tds_gprs_context *gc,
                        const char *netmask);
void tds_gprs_context_set_ipv4_gateway(struct tds_gprs_context *gc,
                        const char *gateway);
void tds_gprs_context_set_ipv4_dns_servers(struct tds_gprs_context *gc,
                        const char **dns);

void tds_gprs_context_set_ipv6_address(struct tds_gprs_context *gc,
                        const char *address);
void tds_gprs_context_set_ipv6_prefix_length(struct tds_gprs_context *gc,
                        unsigned char length);
void tds_gprs_context_set_ipv6_gateway(struct tds_gprs_context *gc,
                        const char *gateway);
void tds_gprs_context_set_ipv6_dns_servers(struct tds_gprs_context *gc,
                        const char **dns);

#endif /* __TDS_GPRS_CONTEXT_H */
