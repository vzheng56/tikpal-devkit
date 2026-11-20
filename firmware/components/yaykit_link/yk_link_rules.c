// yk_link_rules.h.
#include "yk_link_rules.h"

uint16_t yk_rules_chr_flags(uint8_t flags, bool unpaired) {
    uint16_t f = 0;
    if (flags & YK_KEEP_READ) f |= YK_RULES_F_READ | (unpaired ? 0 : YK_RULES_F_READ_ENC);
    if (flags & YK_KEEP_WRITE) f |= YK_RULES_F_WRITE | (unpaired ? 0 : YK_RULES_F_WRITE_ENC);
    if ((flags & YK_RULES_WRITE_NR) && unpaired) f |= YK_RULES_F_WRITE_NO_RSP;
    if (flags & YK_KEEP_NOTIFY) f |= YK_RULES_F_NOTIFY;
    return f;
}

bool yk_rules_unpaired_ok(const yk_link_keep_unpaired_t *cfg) {
    return cfg && cfg->nvs_ns && cfg->nvs_ns[0];
}

yk_rules_adv_t yk_rules_adv(bool fast) {
    if (fast) return (yk_rules_adv_t){.itvl_min = 32, .itvl_max = 48, .duration_ms = YK_RULES_FAST_MS};
    return (yk_rules_adv_t){.itvl_min = 244, .itvl_max = 244, .duration_ms = 0};  // 152.5 ms
}

bool yk_rules_static_addr_ok(const uint8_t addr[6]) {
    if ((addr[5] & 0xC0) != 0xC0) return false;
    bool zero = (addr[5] & 0x3F) == 0, ones = (addr[5] & 0x3F) == 0x3F;
    for (int i = 0; i < 5; i++) {
        zero = zero && addr[i] == 0;
        ones = ones && addr[i] == 0xFF;
    }
    return !zero && !ones;
}

bool yk_rules_static_addr_make(uint8_t addr[6]) {
    addr[5] |= 0xC0;
    return yk_rules_static_addr_ok(addr);
}

uint16_t yk_rules_payload_max(uint16_t mtu) {
    if (mtu <= 3) return 0;
    const uint16_t n = mtu - 3;
    return n > YK_RULES_VALUE_MAX ? YK_RULES_VALUE_MAX : n;
}

uint16_t yk_rules_frame_room(uint16_t mtu) {
    const uint16_t n = yk_rules_payload_max(mtu);
    if (n <= 20) return 0;  // the envelope: counter 4, tag 16
    return n - 20 > YK_LINK_FRAME_MAX ? YK_LINK_FRAME_MAX : n - 20;
}
