#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int transport_tcp_init(void);
int transport_tcp_send(const uint8_t* data, size_t length, size_t* sent);
int transport_tcp_receive(uint8_t* out_data, size_t max_length, size_t* received);

#ifdef __cplusplus
}
#endif
