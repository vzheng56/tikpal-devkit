// 生成的，别手改：packages/contracts/src/deployments.ts 的 DEPLOYMENTS，由 pnpm contracts:sync 写出
// （packages/contracts/src/firmware-regions.ts）。改登记表，再跑 pnpm contracts:sync。
// 两套部署的地区表（docs/delivery/2026-11-20-两套部署-总纲.md）：圆屏按激活码前缀认出自己属于哪一行，
// 兑换成功后记进 NVS；没记过的用第 0 行 global。还没有代码读它，由固件在 CONFIG_YK_REGIONS 后面接上。
#pragma once

typedef struct {
    const char *id;              // 部署名，跟 DEPLOY_REGION、hello 的 region 一样
    const char *claim_prefix;    // 这一边发的激活码的前缀
    const char *device_origin;   // 设备接口
    const char *session_url;     // 语音会话
    const char *onboarding_url;  // 开机二维码，最多 YK_REGION_ONBOARDING_URL_MAX 字节
    const char *computer_url;    // 更新页「在电脑上打开」的那行字
} yk_region_t;

#define YK_REGION_COUNT 2
#define YK_REGION_DEFAULT 0
#define YK_REGION_ONBOARDING_URL_MAX 32

// unused：只引了头文件、没用到表的源文件不报 -Wunused-const-variable。
static const yk_region_t YK_REGIONS[YK_REGION_COUNT] __attribute__((unused)) = {
    {
        .id = "global",
        .claim_prefix = "ykc_",
        .device_origin = "https://device.yaykit.io",
        .session_url = "wss://device.yaykit.io/device/v1/session",
        .onboarding_url = "https://tikpal.ai/get?from=voice",
        .computer_url = "yaykit.io/app/yay",
    },
    {
        .id = "cn",
        .claim_prefix = "ykcn_",
        .device_origin = "https://device.tikpal.com.cn",
        .session_url = "wss://device.tikpal.com.cn/device/v1/session",
        .onboarding_url = "https://tikpal.cn/get?f=v",
        .computer_url = "tikpal.cn/app/yay",
    },
};
