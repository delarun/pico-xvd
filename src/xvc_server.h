#pragma once

#define XVC_PORT        2542
// Максимальная длина вектора в байтах (сообщается клиенту в getinfo)
#ifndef XVC_MAX_VECTOR
#define XVC_MAX_VECTOR  2048
#endif

void xvc_server_init(void);
