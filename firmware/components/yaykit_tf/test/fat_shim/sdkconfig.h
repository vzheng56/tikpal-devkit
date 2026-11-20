// What ESP-IDF's ffconf.h reads, for building its FatFs on the host (test_tf_fat.c): shared-test's values (the volume
// label on, as CONFIG_YK_TF_SYSTEM selects it: test_tf_layout.c names the card).
#pragma once
#define CONFIG_FATFS_USE_FASTSEEK 0
#define CONFIG_FATFS_USE_LABEL 1
#define CONFIG_FATFS_CODEPAGE 437
#define CONFIG_FATFS_LFN_HEAP 1
#define CONFIG_FATFS_MAX_LFN 255
#define CONFIG_FATFS_API_ENCODING_UTF_8 1
#define CONFIG_FATFS_VOLUME_COUNT 2
#define CONFIG_WL_SECTOR_SIZE 4096
#define CONFIG_FATFS_PER_FILE_CACHE 1
#define CONFIG_FATFS_DONT_TRUST_FREE_CLUSTER_CNT 0
#define CONFIG_FATFS_DONT_TRUST_LAST_ALLOC 0
#define CONFIG_FATFS_FS_LOCK 0
#define CONFIG_FATFS_TIMEOUT_MS 10000
#define CONFIG_FATFS_USE_DYN_BUFFERS 0
