#include "yk_wifi_list.h"

#include <stdio.h>
#include <string.h>

// The blob's layout is the same on the device and on the host: it is what NVS keeps.
_Static_assert(sizeof(yk_wifi_net_t) == 104, "yk_wifi_net_t is the NVS layout");
_Static_assert(sizeof(yk_wifi_list_t) == 4 + YK_WIFI_LIST_MAX * 104, "yk_wifi_list_t is the NVS layout");

void yk_wifi_list_clear(yk_wifi_list_t *l) {
    memset(l, 0, sizeof(*l));
    l->ver = YK_WIFI_LIST_VER;
    l->cur = YK_WIFI_LIST_NONE;
}

static bool in_use(const yk_wifi_net_t *n) { return n->ssid_len > 0; }

static int most_recent(const yk_wifi_list_t *l) {
    int best = -1;
    for (int i = 0; i < YK_WIFI_LIST_MAX; i++)
        if (in_use(&l->nets[i]) && (best < 0 || l->nets[i].used > l->nets[best].used)) best = i;
    return best;
}

static bool same_name(const yk_wifi_net_t *n, const uint8_t *ssid, size_t len) {
    return in_use(n) && n->ssid_len == len && !memcmp(n->ssid, ssid, len);
}

bool yk_wifi_list_load(yk_wifi_list_t *l, const void *blob, size_t len) {
    if (!blob || len != sizeof(*l)) {
        yk_wifi_list_clear(l);
        return false;
    }
    if (blob != l) memmove(l, blob, sizeof(*l));  // NVS may have read it into the list itself
    if (l->ver != YK_WIFI_LIST_VER) {
        yk_wifi_list_clear(l);
        return false;
    }
    for (int i = 0; i < YK_WIFI_LIST_MAX; i++) {
        yk_wifi_net_t *n = &l->nets[i];
        if (n->ssid_len > sizeof(n->ssid)) {
            yk_wifi_list_clear(l);
            return false;
        }
        if (!in_use(n)) memset(n, 0, sizeof(*n));
        if (n->channel > 14) n->channel = 0;
    }
    for (int i = 0; i < YK_WIFI_LIST_MAX; i++)
        for (int j = i + 1; j < YK_WIFI_LIST_MAX; j++)
            if (in_use(&l->nets[i]) && same_name(&l->nets[j], l->nets[i].ssid, l->nets[i].ssid_len))
                memset(&l->nets[l->nets[i].used < l->nets[j].used ? i : j], 0, sizeof(yk_wifi_net_t));
    const int cur = most_recent(l);
    l->cur = cur < 0 ? YK_WIFI_LIST_NONE : (uint8_t)cur;
    return true;
}

int yk_wifi_list_count(const yk_wifi_list_t *l) {
    int n = 0;
    for (int i = 0; i < YK_WIFI_LIST_MAX; i++) n += in_use(&l->nets[i]);
    return n;
}

int yk_wifi_list_find(const yk_wifi_list_t *l, const uint8_t *ssid, size_t ssid_len) {
    if (!ssid || !ssid_len || ssid_len > 32) return -1;
    for (int i = 0; i < YK_WIFI_LIST_MAX; i++)
        if (same_name(&l->nets[i], ssid, ssid_len)) return i;
    return -1;
}

int yk_wifi_list_order(const yk_wifi_list_t *l, uint8_t out[YK_WIFI_LIST_MAX]) {
    int n = 0;
    for (int i = 0; i < YK_WIFI_LIST_MAX; i++) {
        if (!in_use(&l->nets[i])) continue;
        int at = n++;
        while (at > 0 && l->nets[out[at - 1]].used < l->nets[i].used) {
            out[at] = out[at - 1];
            at--;
        }
        out[at] = (uint8_t)i;
    }
    return n;
}

/** The count for one just joined; renumbers 1..n first should it ever come near the top. */
static uint32_t next_used(yk_wifi_list_t *l) {
    uint32_t top = 0;
    for (int i = 0; i < YK_WIFI_LIST_MAX; i++)
        if (in_use(&l->nets[i]) && l->nets[i].used > top) top = l->nets[i].used;
    if (top < UINT32_MAX - 1) return top + 1;
    uint8_t order[YK_WIFI_LIST_MAX];
    const int n = yk_wifi_list_order(l, order);
    for (int k = 0; k < n; k++) l->nets[order[k]].used = (uint32_t)(n - k);
    return (uint32_t)n + 1;
}

/** The password as the list keeps it: its bytes up to the first zero (all 64 for a raw key), the rest zero. */
static void normal_pass(uint8_t out[64], const uint8_t pass[64]) {
    memset(out, 0, 64);
    if (!pass) return;
    size_t len = 0;
    while (len < 64 && pass[len]) len++;
    memcpy(out, pass, len);
}

int yk_wifi_list_remember(yk_wifi_list_t *l, const uint8_t *ssid, size_t ssid_len, const uint8_t pass[64],
                          uint8_t channel, int seen, bool *changed) {
    bool dirty = false;
    if (changed) *changed = false;
    if (!ssid || !ssid_len || ssid_len > 32) return -1;
    uint8_t key[64];
    normal_pass(key, pass);
    int slot = yk_wifi_list_find(l, ssid, ssid_len);
    if (slot < 0) {
        for (int i = 0; i < YK_WIFI_LIST_MAX && slot < 0; i++)
            if (!in_use(&l->nets[i])) slot = i;
        if (slot < 0) {  // full: the least recently used other than the one in use makes room
            for (int i = 0; i < YK_WIFI_LIST_MAX; i++)
                if (i != l->cur && (slot < 0 || l->nets[i].used < l->nets[slot].used)) slot = i;
        }
        yk_wifi_net_t *n = &l->nets[slot];
        memset(n, 0, sizeof(*n));
        memcpy(n->ssid, ssid, ssid_len);
        n->ssid_len = (uint8_t)ssid_len;
        n->flags = YK_WIFI_NET_BY_NAME;  // until a scan has seen it
        dirty = true;
    }
    yk_wifi_net_t *n = &l->nets[slot];
    if (memcmp(n->pass, key, sizeof(key))) {
        memcpy(n->pass, key, sizeof(key));
        dirty = true;
    }
    memset(key, 0, sizeof(key));
    if (channel >= 1 && channel <= 14 && channel != n->channel) {
        n->channel = channel;
        dirty = true;
    }
    if (seen >= 0) {
        const uint8_t flags = seen ? (uint8_t)(n->flags & ~YK_WIFI_NET_BY_NAME) : (uint8_t)(n->flags | YK_WIFI_NET_BY_NAME);
        if (flags != n->flags) {
            n->flags = flags;
            dirty = true;
        }
    }
    if (l->cur != slot || !n->used) {
        n->used = 0;  // not counted while the next count is found
        n->used = next_used(l);
        l->cur = (uint8_t)slot;
        dirty = true;
    }
    if (changed) *changed = dirty;
    return slot;
}

bool yk_wifi_list_adopt(yk_wifi_list_t *l, const uint8_t *ssid, size_t ssid_len, const uint8_t pass[64],
                        uint8_t channel) {
    if (!ssid || !ssid_len || ssid_len > 32) return false;
    bool changed = false;
    yk_wifi_list_remember(l, ssid, ssid_len, pass, channel, -1, &changed);
    return changed;
}

int yk_wifi_list_line(const yk_wifi_list_t *l, int order, char *out, size_t size) {
    uint8_t slots[YK_WIFI_LIST_MAX];
    const int n = yk_wifi_list_order(l, slots);
    if (order < 0 || order >= n) return -1;
    const yk_wifi_net_t *net = &l->nets[slots[order]];
    return snprintf(out, size, "YK_WIFI_NET order=%d last=%d channel=%u by_name=%d ssid=%.*s", order + 1,
                    slots[order] == l->cur, (unsigned)net->channel, (net->flags & YK_WIFI_NET_BY_NAME) != 0,
                    (int)net->ssid_len, (const char *)net->ssid);
}

void yk_wifi_round_plan(yk_wifi_round_t *r, const yk_wifi_list_t *l, const uint8_t seen[YK_WIFI_LIST_MAX]) {
    memset(r, 0, sizeof(*r));
    uint8_t order[YK_WIFI_LIST_MAX];
    const int n = yk_wifi_list_order(l, order);
    for (int k = 0; k < n; k++)  // the ones the scan saw, the most recently used first: twice each
        if (seen && seen[order[k]]) r->slot[r->n] = order[k], r->times[r->n++] = 2;
    for (int k = 0; k < n; k++)  // then by name, seen or not: once each
        if (!(seen && seen[order[k]]) && (l->nets[order[k]].flags & YK_WIFI_NET_BY_NAME))
            r->slot[r->n] = order[k], r->times[r->n++] = 1;
}

int yk_wifi_round_next(yk_wifi_round_t *r, bool refused) {
    if (r->started && r->pos < r->n && (refused || r->tries >= r->times[r->pos])) {
        r->pos++;
        r->tries = 0;
    }
    r->started = true;
    if (r->pos >= r->n) return -1;
    r->tries++;
    return r->slot[r->pos];
}

int yk_wifi_list_wait_s(int before_s) {
    if (before_s <= 0) return YK_WIFI_WAIT_FIRST_S;
    return before_s * 2 > YK_WIFI_WAIT_MAX_S ? YK_WIFI_WAIT_MAX_S : before_s * 2;
}
