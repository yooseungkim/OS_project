#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stdbool.h>
#include <stddef.h>

#define SWAP_SLOT_INVALID ((size_t) -1)

void swap_init (void);
size_t swap_out (const void *kpage);
bool swap_in (size_t slot, void *kpage);
void swap_free (size_t slot);

#endif /* vm/swap.h */
