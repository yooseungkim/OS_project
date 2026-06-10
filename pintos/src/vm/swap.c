#include "vm/swap.h"
#include <bitmap.h>
#include <debug.h>
#include "devices/block.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

#define SECTORS_PER_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)

static struct block *swap_block;
static struct bitmap *swap_map;
static struct lock swap_lock;

void
swap_init (void)
{
  swap_block = block_get_role (BLOCK_SWAP);
  lock_init (&swap_lock);

  if (swap_block == NULL)
    swap_map = NULL;
  else
    swap_map = bitmap_create (block_size (swap_block) / SECTORS_PER_PAGE);
}

size_t
swap_out (const void *kpage)
{
  size_t slot;
  size_t i;

  ASSERT (pg_ofs (kpage) == 0);
  if (swap_block == NULL || swap_map == NULL)
    PANIC ("no swap device");

  lock_acquire (&swap_lock);
  slot = bitmap_scan_and_flip (swap_map, 0, 1, false);
  lock_release (&swap_lock);

  if (slot == BITMAP_ERROR)
    PANIC ("swap partition is full");

  for (i = 0; i < SECTORS_PER_PAGE; i++)
    block_write (swap_block, slot * SECTORS_PER_PAGE + i,
                 (const uint8_t *) kpage + i * BLOCK_SECTOR_SIZE);

  return slot;
}

bool
swap_in (size_t slot, void *kpage)
{
  size_t i;

  ASSERT (pg_ofs (kpage) == 0);
  if (slot == SWAP_SLOT_INVALID || swap_block == NULL || swap_map == NULL)
    return false;

  lock_acquire (&swap_lock);
  if (slot >= bitmap_size (swap_map) || !bitmap_test (swap_map, slot))
    {
      lock_release (&swap_lock);
      return false;
    }
  lock_release (&swap_lock);

  for (i = 0; i < SECTORS_PER_PAGE; i++)
    block_read (swap_block, slot * SECTORS_PER_PAGE + i,
                (uint8_t *) kpage + i * BLOCK_SECTOR_SIZE);

  lock_acquire (&swap_lock);
  bitmap_set (swap_map, slot, false);
  lock_release (&swap_lock);
  return true;
}

void
swap_free (size_t slot)
{
  if (slot == SWAP_SLOT_INVALID || swap_map == NULL)
    return;

  lock_acquire (&swap_lock);
  if (slot < bitmap_size (swap_map) && bitmap_test (swap_map, slot))
    bitmap_set (swap_map, slot, false);
  lock_release (&swap_lock);
}
