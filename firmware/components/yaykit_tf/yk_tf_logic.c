#include "yk_tf_logic.h"

#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------- paths

bool yk_tf_name_ok(const char *name) {
    if (!name || !*name) return false;
    size_t n = 0;
    for (const char *p = name; *p; p++, n++) {
        const bool alnum = (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9');
        if (!alnum && (*p != '-' || p == name)) return false;
    }
    return n <= YK_TF_NAME_MAX;
}

static bool rel_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
}

bool yk_tf_rel_ok(const char *rel) {
    if (!rel || !*rel) return false;
    size_t n = 0;
    int levels = 1;
    char before = '/';  // as if after a slash: the first part is checked like the others
    for (const char *p = rel; *p; before = *p++, n++) {
        if (n >= YK_TF_REL_MAX) return false;
        if (*p == '/') {
            if (before == '/' || before == '.') return false;  // empty part, absolute, or a part ending in a dot
            if (++levels > YK_TF_REL_LEVELS) return false;
        } else {
            if (!rel_char(*p)) return false;
            if (before == '/' && *p == '.') return false;  // `.`, `..`, `...`, `.x`
        }
    }
    return before != '/' && before != '.';
}

yk_tf_join_t yk_tf_join(const char *dir, const char *rel, char *out, size_t size) {
    if (!out || !size) return YK_TF_JOIN_SMALL;
    out[0] = 0;
    if (!dir || !*dir || !yk_tf_rel_ok(rel)) return YK_TF_JOIN_BAD;
    const size_t a = strlen(dir), b = strlen(rel);
    if (a + 1 + b + 1 > size) return YK_TF_JOIN_SMALL;
    memcpy(out, dir, a);
    out[a] = '/';
    memcpy(out + a + 1, rel, b + 1);
    return YK_TF_JOIN_OK;
}

bool yk_tf_slug_from_project(const char *project, char *out, size_t size) {
    static const char PREFIX[] = "yaykit_";
    if (!out || !size) return false;
    out[0] = 0;
    if (!project || strncmp(project, PREFIX, sizeof(PREFIX) - 1)) return false;
    project += sizeof(PREFIX) - 1;
    const size_t n = strlen(project);
    if (n + 1 > size) return false;
    for (size_t i = 0; i <= n; i++) out[i] = project[i] == '_' ? '-' : project[i];
    if (yk_tf_name_ok(out)) return true;
    out[0] = 0;
    return false;
}

// ---------------------------------------------------------------- how much

uint64_t yk_tf_quota_for(uint64_t total) {
    const uint64_t share = total / YK_TF_QUOTA_SHARE;
    return share < YK_TF_QUOTA_MAX ? share : YK_TF_QUOTA_MAX;
}

uint64_t yk_tf_used_add(uint64_t used, uint64_t size, uint32_t cluster) {
    uint64_t take = size;
    if (cluster && size % cluster) {
        const uint64_t pad = cluster - size % cluster;
        take = size > UINT64_MAX - pad ? UINT64_MAX : size + pad;
    }
    return used > UINT64_MAX - take ? UINT64_MAX : used + take;
}

// ---------------------------------------------------------------- memory and the controller

int yk_tf_bounce_sectors(size_t internal_free) {
    return internal_free >= YK_TF_RAM_BATCH ? YK_TF_BOUNCE_BATCH : YK_TF_BOUNCE_SECTOR;
}

uint32_t yk_tf_timeout_cycles(uint32_t card_khz) {
    const uint64_t cycles = (uint64_t)YK_TF_DATA_TIMEOUT_MS * card_khz;
    return cycles > YK_TF_TIMEOUT_REG_MAX ? YK_TF_TIMEOUT_REG_MAX : (uint32_t)cycles;
}

// ---------------------------------------------------------------- the card's state

/** The rest is over: one more try. */
static void rest_over(yk_tf_core_t *c) {
    if (!c->resting) return;
    c->resting = false;
    c->fails = YK_TF_FAILS_MAX - 1;
}

yk_tf_go_t yk_tf_core_acquire(yk_tf_core_t *c, uint64_t now_ms, bool bluetooth, size_t internal_free) {
    if (c->ending) return YK_TF_NO_ENDING;
    if (c->pauses) return YK_TF_NO_PAUSED;
    if (bluetooth) return YK_TF_NO_BLUETOOTH;
    if (c->resting && now_ms < c->rest_until_ms) return YK_TF_NO_RESTING;
    if (c->mounted) {
        rest_over(c);
        c->refs++;
        return YK_TF_GO_HOLD;
    }
    if (internal_free < YK_TF_RAM_MOUNT) return YK_TF_NO_RAM;
    rest_over(c);
    c->mounting = true;
    c->not_fat = false;
    c->lost = false;
    c->session_bad = false;
    c->restarts = 0;
    c->good = 0;
    return YK_TF_GO_MOUNT;
}

void yk_tf_core_mounted(yk_tf_core_t *c, bool ok, uint64_t now_ms) {
    c->mounting = false;
    if (ok) {
        c->mounted = true;
        c->opened = true;
        c->refs = 1;
    } else if (!c->session_bad) {
        yk_tf_core_fail(c, now_ms);
    }
}

bool yk_tf_core_release(yk_tf_core_t *c) {
    if (!c->refs) return false;
    if (--c->refs) return false;
    if (!c->session_bad) c->fails = 0;
    c->mounted = false;
    return true;
}

void yk_tf_core_fail(yk_tf_core_t *c, uint64_t now_ms) {
    c->session_bad = true;
    if (c->fails < YK_TF_FAILS_MAX) c->fails++;
    if (c->fails >= YK_TF_FAILS_MAX && !c->resting) {
        c->resting = true;
        c->rest_until_ms = now_ms + YK_TF_REST_MS;
    }
}

void yk_tf_core_pause(yk_tf_core_t *c) { __atomic_add_fetch(&c->pauses, 1, __ATOMIC_SEQ_CST); }

void yk_tf_core_resume(yk_tf_core_t *c) {
    if (c->pauses) __atomic_sub_fetch(&c->pauses, 1, __ATOMIC_SEQ_CST);
}

bool yk_tf_core_free(const yk_tf_core_t *c) { return !c->refs && !c->mounting && !c->lent; }

bool yk_tf_core_lend(yk_tf_core_t *c, uint64_t now_ms) {
    if (c->ending || c->lent || !yk_tf_core_free(c)) return false;
    if (c->resting && now_ms < c->rest_until_ms) return false;
    rest_over(c);
    c->lent = true;
    c->lost = false;
    c->session_bad = false;
    c->restarts = 0;
    c->good = 0;
    return true;
}

void yk_tf_core_lend_end(yk_tf_core_t *c) {
    if (!c->lent) return;
    c->lent = false;
    if (!c->session_bad) c->fails = 0;
}

void yk_tf_core_end(yk_tf_core_t *c) { c->ending = true; }

bool yk_tf_core_stop_asked(const yk_tf_core_t *c) { return c->pauses || c->ending; }

yk_tf_state_t yk_tf_core_state(const yk_tf_core_t *c, uint64_t now_ms) {
    if (c->resting && now_ms < c->rest_until_ms) return YK_TF_RESTING;
    if (c->pauses || c->ending) return YK_TF_PAUSED;
    return c->mounted ? YK_TF_READY : YK_TF_OFF;
}

void yk_tf_core_known(const yk_tf_core_t *c, uint64_t now_ms, uint32_t total_kb, uint32_t free_kb, yk_tf_known_t *out) {
    *out = (yk_tf_known_t){.state = yk_tf_core_state(c, now_ms), .fails = c->fails, .opened = c->opened, .not_fat = c->not_fat};
    if (!total_kb || free_kb > total_kb) return;
    out->total = (uint64_t)total_kb * 1024;
    out->used = (uint64_t)(total_kb - free_kb) * 1024;
}

static int io_done(yk_tf_core_t *c) {
    if (c->good < YK_TF_GOOD_RUN && ++c->good == YK_TF_GOOD_RUN) c->restarts = 0;
    return 0;
}

int yk_tf_core_io(yk_tf_core_t *c, const yk_tf_io_t *io) {
    if ((!c->mounted && !c->mounting && !c->lent) || c->ending || c->lost || c->resting) return -1;
    int r = io->transfer(io->ctx);
    if (!r) return io_done(c);
    c->good = 0;
    if (c->restarts >= YK_TF_RESTARTS_MAX) {
        yk_tf_core_fail(c, io->now_ms);
        return r;
    }
    c->restarts++;
    if (io->restart(io->ctx)) {
        c->lost = true;
        yk_tf_core_fail(c, io->now_ms);
        return r;
    }
    r = io->transfer(io->ctx);
    if (!r) return io_done(c);
    yk_tf_core_fail(c, io->now_ms);
    return r;
}

uint32_t yk_tf_crc32(const void *data, size_t size) {
    const uint8_t *p = data;
    uint32_t crc = 0xFFFFFFFFu;
    while (size--) {
        crc ^= *p++;
        for (int i = 0; i < 8; i++) crc = crc >> 1 ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

// ---------------------------------------------------------------- the card's name

/** Where the boot sector's label is, and its extended boot signature: FAT32's BPB is longer (its 16-bit FAT size 0). */
static bool boot_label_at(const uint8_t *sec, size_t *label) {
    if (sec[510] != 0x55 || sec[511] != 0xAA || !memcmp(sec + 3, "EXFAT   ", 8)) return false;
    const bool fat32 = !sec[22] && !sec[23];
    const size_t sig = fat32 ? 66 : 38;
    *label = fat32 ? 71 : 43;
    return sec[sig] == 0x29;
}

bool yk_tf_boot_label(const uint8_t *sec, char *out) {
    out[0] = 0;
    size_t at;
    if (!sec || !boot_label_at(sec, &at)) return false;
    size_t n = 11;
    while (n && (sec[at + n - 1] == ' ' || !sec[at + n - 1])) n--;
    memcpy(out, sec + at, n);  // the code page's bytes: ASCII for any name here; others only compared, never shown
    out[n] = 0;
    return true;
}

bool yk_tf_boot_label_set(uint8_t *sec, const char *label, uint32_t *backup) {
    if (backup) *backup = 0;
    size_t at;
    const size_t n = label ? strlen(label) : 0;
    if (!sec || n > 11 || !boot_label_at(sec, &at)) return false;
    memset(sec + at, ' ', 11);
    memcpy(sec + at, label, n);
    if (backup && !sec[22] && !sec[23]) {
        const uint32_t bk = (uint32_t)sec[50] | (uint32_t)sec[51] << 8, reserved = (uint32_t)sec[14] | (uint32_t)sec[15] << 8;
        if (bk && bk < reserved) *backup = bk;
    }
    return true;
}

static bool no_name(const char *l) { return !l[0] || !strcmp(l, "NO NAME"); }

bool yk_tf_label_follow(const char *dir, const char *boot) {
    return dir && boot && !strcmp(dir, YK_TF_LABEL) && !no_name(boot) && strcmp(boot, YK_TF_LABEL);
}

static const char *named(const char *l) { return no_name(l) ? "" : l; }

/** What the boot sector's field takes (yk_tf_boot_label_set): upper case ASCII, 11 at most. */
static bool boot_can(const char *l) {
    size_t n = 0;
    for (; l[n]; n++)
        if (l[n] < 0x20 || l[n] > 0x7E || (l[n] >= 'a' && l[n] <= 'z')) return false;
    return n <= 11;
}

yk_tf_label_do_t yk_tf_label_sync(const char *dir, const char *boot, const char *was_dir, const char *was_boot, char *to,
                                  bool *set_dir, bool *set_boot) {
    *set_dir = *set_boot = false;
    to[0] = 0;
    if (boot && was_dir && was_boot) {
        const bool dir_moved = strcmp(named(dir), named(was_dir)) != 0, boot_moved = strcmp(named(boot), named(was_boot)) != 0;
        if (dir_moved || boot_moved) {
            const char *name = boot_moved && (!dir_moved || !named(dir)[0]) ? named(boot) : named(dir);
            snprintf(to, YK_TF_LABEL_BUF, "%s", name[0] ? name : YK_TF_LABEL);
            *set_dir = strcmp(dir, to) != 0;
            *set_boot = strcmp(boot, to) != 0 && boot_can(to);
            if (!*set_dir && !*set_boot) return YK_TF_LABEL_KEEP;
            return name[0] ? YK_TF_LABEL_RENAMED : YK_TF_LABEL_NONE;
        }
    }
    if (yk_tf_label_follow(dir, boot)) {
        snprintf(to, YK_TF_LABEL_BUF, "%s", boot);
        *set_dir = true;
        return YK_TF_LABEL_RENAMED;
    }
    yk_tf_label_plan(dir, boot, set_dir, set_boot);
    if (!*set_dir && !*set_boot) return YK_TF_LABEL_KEEP;
    snprintf(to, YK_TF_LABEL_BUF, "%s", YK_TF_LABEL);
    return YK_TF_LABEL_NONE;
}

bool yk_tf_label_parse(const char *text, size_t n, char *dir, char *boot) {
    char *outs[2] = {dir, boot};
    size_t at = 0;
    for (int k = 0; k < 2; k++) {
        size_t len = 0;
        while (at + len < n && text[at + len] != '\n' && text[at + len]) len++;
        if (at + len >= n || text[at + len] != '\n' || len >= YK_TF_LABEL_BUF) return false;
        memcpy(outs[k], text + at, len);
        outs[k][len] = 0;
        at += len + 1;
    }
    return true;
}

void yk_tf_label_plan(const char *dir, const char *boot, bool *set_dir, bool *set_boot) {
    *set_dir = *set_boot = false;
    const char *b = boot ? boot : "";
    if (!no_name(dir) && strcmp(dir, YK_TF_LABEL)) return;         // the owner's, in the root directory
    if (!dir[0] && !no_name(b) && strcmp(b, YK_TF_LABEL)) return;  // the owner's, only in the boot sector
    if (yk_tf_label_follow(dir, boot)) return;                     // renamed on a Mac since ours: copied over instead
    *set_dir = strcmp(dir, YK_TF_LABEL) != 0;
    *set_boot = boot && strcmp(boot, YK_TF_LABEL) != 0;
}
