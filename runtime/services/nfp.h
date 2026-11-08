// The virtual amiibo reader (runtime/services/nfp.c), for the cheat panel.
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
// Put a figure (its 8-byte amiibo ID) on the reader: the game's next amiibo scan reads it.
void nfp_virtual_place(uint64_t amiibo_id);
void nfp_virtual_status(char *out, int n);
#ifdef __cplusplus
}
#endif
