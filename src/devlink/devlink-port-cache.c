/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include "alloc-util.h"
#include "hash-funcs.h"
#include "macro.h"
#include "siphash24.h"

#include "devlink-port-cache.h"
#include "devlinkd-manager.h"

typedef struct DevlinkPortCacheKey {
        char *bus_name;
        char *dev_name;
        uint32_t index;
} DevlinkPortCacheKey;

typedef struct DevlinkPortCacheItem {
        DevlinkPortCacheKey key;
        Manager *manager;
        unsigned n_ref;
        bool in_hashmap;
        char *ifname;
        bool split;
} DevlinkPortCacheItem;

static void devlink_port_cache_item_hash_func(const DevlinkPortCacheKey *key, struct siphash *state) {
        assert(key->bus_name);
        assert(key->dev_name);

        string_hash_func(key->bus_name, state);
        string_hash_func(key->dev_name, state);
        siphash24_compress(&key->index, sizeof(key->index), state);
}

static int devlink_port_cache_item_compare_func(const DevlinkPortCacheKey *x, const DevlinkPortCacheKey *y) {
        int d;

        assert(x);
        assert(y);

        d = strcmp(x->bus_name, y->bus_name);
        if (d)
                return d;
        d = strcmp(x->dev_name, y->dev_name);
        if (d)
                return d;
        return CMP(x->index, y->index);
}

static DevlinkPortCacheItem *devlink_port_cache_item_free(DevlinkPortCacheItem *item) {
        assert(item);

        if (item->in_hashmap)
                hashmap_remove(item->manager->port_cache, &item->key);

        free(item->key.bus_name);
        free(item->key.dev_name);
        free(item->ifname);

        return mfree(item);
}

DEFINE_PRIVATE_HASH_OPS_WITH_VALUE_DESTRUCTOR(
                devlink_port_cache_item_hash_ops,
                DevlinkPortCacheKey,
                devlink_port_cache_item_hash_func,
                devlink_port_cache_item_compare_func,
                DevlinkPortCacheItem,
                devlink_port_cache_item_free);

DEFINE_PRIVATE_TRIVIAL_REF_UNREF_FUNC(DevlinkPortCacheItem, devlink_port_cache_item, devlink_port_cache_item_free);
DEFINE_TRIVIAL_CLEANUP_FUNC(DevlinkPortCacheItem *, devlink_port_cache_item_unref);

static DevlinkPortCacheItem *devlink_port_cache_item_alloc(Manager *m, DevlinkPortCacheKey *key) {
        _cleanup_(devlink_port_cache_item_unrefp) DevlinkPortCacheItem *item;
        int r;

        item = malloc0(sizeof(DevlinkPortCacheItem));
        if (!item)
                return NULL;

        *item = (DevlinkPortCacheItem) {
                .key.index = key->index,
                .manager = m,
                .n_ref = 1,
        };
        r = free_and_strdup(&item->key.bus_name, key->bus_name);
        if (r < 0)
                return NULL;
        r = free_and_strdup(&item->key.dev_name, key->dev_name);
        if (r < 0)
                return NULL;
        r = hashmap_ensure_put(&m->devlink_objs, &devlink_port_cache_item_hash_ops, &item->key, item);
        if (r < 0)
                return NULL;
        item->in_hashmap = true;
        return item;
}

int devlink_port_cache_update(
                Manager *m,
                const char *bus_name,
                const char *dev_name,
                uint32_t index,
                const char *ifname,
                bool split) {
        DevlinkPortCacheKey key = {
                .bus_name = (char *) bus_name,
                .dev_name = (char *)dev_name,
                .index = index
        };
        _cleanup_(devlink_port_cache_item_unrefp) DevlinkPortCacheItem *item = hashmap_get(m->port_cache, &key);
        int r;

        if (!item)
                item = devlink_port_cache_item_alloc(m, &key);
        if (!item)
                return -ENOMEM;

        r = free_and_strdup(&item->ifname, ifname);
        if (r < 0)
                return r;
        item->split = split;

        devlink_port_cache_item_ref(item);

        return 0;
}

int devlink_port_cache_query(
                Manager *m,
                const char *bus_name,
                const char *dev_name,
                uint32_t index,
                char **ifname,
                bool *split) {
        DevlinkPortCacheKey key = {
                .bus_name = (char *) bus_name,
                .dev_name = (char *) dev_name,
                .index = index
        };
        DevlinkPortCacheItem *item = hashmap_get(m->port_cache, &key);
        int r;

        if (!item)
                return -ENOENT;
        r = free_and_strdup(ifname, item->ifname);
        if (r < 0)
                return r;
        *split = item->split;
        return 0;
}
