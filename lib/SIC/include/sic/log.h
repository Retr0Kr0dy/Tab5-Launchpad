#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#ifndef SIC_LOG
#define SIC_LOG(level, ...) do{}while(0)
#endif
#define SIC_LOG_ERR 1
#define SIC_LOG_WARN 2
#define SIC_LOG_INFO 3
#define SIC_LOG_DBG 4
#ifdef __cplusplus
}
#endif
