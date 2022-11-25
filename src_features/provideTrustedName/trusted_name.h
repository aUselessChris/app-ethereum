#ifdef HAVE_ENS

#ifndef TRUSTED_NAME_H_
#define TRUSTED_NAME_H_

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t key_id;
    uint8_t algo_id;
    uint8_t name_length;
    uint8_t sig_length;
    const char *name;
    const uint8_t *sig;
} s_trusted_name;

const char *get_trusted_name(uint8_t *out_len, const uint64_t *chain_id, const uint8_t *addr);
void handle_provide_trusted_name(uint8_t p1, uint8_t p2, const uint8_t *data, uint8_t length);

#endif  // TRUSTED_NAME_H_

#endif  // HAVE_ENS
