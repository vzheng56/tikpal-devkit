#pragma once
void stub_log(char level, const char *tag, const char *fmt, ...) __attribute__((format(__printf__, 3, 4)));
#define ESP_LOGI(tag, ...) stub_log('I', tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) stub_log('W', tag, __VA_ARGS__)
#define ESP_LOGE(tag, ...) stub_log('E', tag, __VA_ARGS__)
