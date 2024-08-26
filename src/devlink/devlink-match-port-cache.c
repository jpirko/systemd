/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include "alloc-util.h"
#include "hash-funcs.h"
#include "macro.h"
#include "siphash24.h"

#include "devlinkd-manager.h"
#include "devlink-key.h"
#include "devlink-match.h"
#include "devlink-match-port-cache.h"

typedef struct DevlinkMatchPortCacheItem {
        DevlinkKey key;
        Manager *manager;
        unsigned n_ref;
        bool in_hashmap_by_key;
        bool in_hashmap_by_ifindex;
        uint64_t ifindex;
        char *ifname;
        bool split;
} DevlinkMatchPortCacheItem;

static DevlinkMatchPortCacheItem *devlink_match_port_cache_item_free(DevlinkMatchPortCacheItem *item) {
        assert(item);

        if (item->in_hashmap_by_ifindex)
                hashmap_remove(item->manager->match_port_cache_by_ifindex, &item->ifindex);

        if (item->in_hashmap_by_key)
                hashmap_remove(item->manager->match_port_cache_by_key, &item->key);

        devlink_key_fini(&item->key);
        free(item->ifname);

        return mfree(item);
}

DEFINE_PRIVATE_HASH_OPS_WITH_VALUE_DESTRUCTOR(
                devlink_match_port_cache_item_hash_ops,
                DevlinkKey,
                devlink_key_hash_func,
                devlink_key_compare_func,
                DevlinkMatchPortCacheItem,
                devlink_match_port_cache_item_free);

DEFINE_TRIVIAL_CLEANUP_FUNC(DevlinkMatchPortCacheItem *, devlink_match_port_cache_item_free);

static DevlinkMatchPortCacheItem *devlink_match_port_cache_item_alloc(Manager *m, DevlinkKey *key, uint64_t ifindex) {
        _cleanup_(devlink_match_port_cache_item_freep) DevlinkMatchPortCacheItem *item;
        int r;

        item = malloc0(sizeof(DevlinkMatchPortCacheItem));
        if (!item)
                return NULL;

        *item = (DevlinkMatchPortCacheItem) {
                .manager = m,
                .ifindex = ifindex,
        };

        devlink_key_init(&item->key, DEVLINK_KIND_PORT);
        r = devlink_key_duplicate(&item->key, key);
        if (r < 0)
                return NULL;

        r = hashmap_ensure_put(&m->match_port_cache_by_key, &devlink_match_port_cache_item_hash_ops, &item->key, item);
        if (r < 0)
                return NULL;
        item->in_hashmap_by_key = true;

        printf("inseting item ifindex %lu\n", item->ifindex);
        r = hashmap_ensure_put(&m->match_port_cache_by_ifindex, &uint64_hash_ops, &item->ifindex, item);
        if (r < 0)
                return NULL;
        item->in_hashmap_by_ifindex = true;

        return TAKE_PTR(item);
}

int devlink_match_port_cache_update(
                Manager *m,
                DevlinkMatch *match,
                uint32_t ifindex,
                const char *ifname,
                bool split) {
        _cleanup_(devlink_match_port_cache_item_freep) DevlinkMatchPortCacheItem *item;
        DevlinkKey key;
        int r;

        devlink_key_init(&key, DEVLINK_KIND_PORT);
        devlink_key_copy_from_match(&key, match, DEVLINK_MATCH_BIT_DEV | DEVLINK_MATCH_BIT_PORT_INDEX);

        item = hashmap_get(m->match_port_cache_by_key, &key);
        if (!item) {
                item = devlink_match_port_cache_item_alloc(m, &key, ifindex);
                if (!item)
                        return -ENOMEM;
        }

        r = free_and_strdup(&item->ifname, ifname);
        if (r < 0)
                return r;
        item->split = split;

        TAKE_PTR(item);
        return 0;
}

void devlink_match_port_cache_remove(
                Manager *m,
                DevlinkMatch *match) {
        _cleanup_(devlink_match_port_cache_item_freep) DevlinkMatchPortCacheItem *item;
        DevlinkKey key;

        devlink_key_init(&key, DEVLINK_KIND_PORT);
        devlink_key_copy_from_match(&key, match, DEVLINK_MATCH_BIT_DEV | DEVLINK_MATCH_BIT_PORT_INDEX);

        item = hashmap_get(m->match_port_cache_by_key, &key);
        printf("removing item %p\n", item);

}

int devlink_match_port_cache_query(
                Manager *m,
                DevlinkMatch *match,
                char **ifname,
                bool *split) {
        DevlinkMatchPortCacheItem *item;
        DevlinkKey key;
        int r;

        devlink_key_init(&key, DEVLINK_KIND_PORT);
        devlink_key_copy_from_match(&key, match, DEVLINK_MATCH_BIT_DEV | DEVLINK_MATCH_BIT_PORT_INDEX);

        item = hashmap_get(m->match_port_cache_by_key, &key);
        if (!item)
                return -ENOENT;
        r = free_and_strdup(ifname, item->ifname);
        if (r < 0)
                return r;
        *split = item->split;
        return 0;
}

void devlink_match_port_cache_update_ifname(
                Manager *m,
                uint64_t ifindex,
                const char *ifname) {
        DevlinkMatchPortCacheItem *item;
        int r;

        printf("ifname update: %lu %s\n", ifindex, ifname);
        assert(m);
        assert(m->genl);

        item = hashmap_get(m->match_port_cache_by_ifindex, &ifindex);
        printf("ifname update item: %p\n", item);
        if (!item)
                return;
        if (!strcmp(item->ifname, ifname))
                return;
        printf("ifname updated: %s->%s\n", item->ifname, ifname);
        r = free_and_strdup(&item->ifname, ifname);
        if (r < 0)
                return;

        /* Ifname got updated, so the configuration for devlink port object identified
         * by ifname that was not matched previously may match now. */

        (void) manager_enumerate_one(m, &item->key);
}
