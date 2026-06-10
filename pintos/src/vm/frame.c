#include "vm/frame.h"
#include <debug.h>
#include <list.h>
#include <stdbool.h>
#include "filesys/file.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "vm/page.h"
#include "vm/swap.h"

extern struct lock filesys_lock;

static struct list frame_table;
static struct list_elem *clock_hand;
static struct lock frame_lock;

static struct frame *evict_frame (void);
static struct frame *select_victim (void);
static void write_back_mmap_page (struct vm_page *, void *);

void
frame_init (void)
{
  list_init (&frame_table);
  clock_hand = NULL;
  lock_init (&frame_lock);
}

struct frame *
frame_alloc (struct vm_page *page)
{
  void *kpage;
  struct frame *frame;

  lock_acquire (&frame_lock);
  kpage = palloc_get_page (PAL_USER);
  if (kpage != NULL)
    {
      frame = malloc (sizeof *frame);
      if (frame == NULL)
        {
          lock_release (&frame_lock);
          palloc_free_page (kpage);
          return NULL;
        }
      frame->kpage = kpage;
      list_push_back (&frame_table, &frame->elem);
      if (clock_hand == NULL)
        clock_hand = list_begin (&frame_table);
      frame->owner = NULL;
      frame->page = NULL;
      frame->pin_count = 1;
      lock_release (&frame_lock);
    }
  else
    {
      lock_release (&frame_lock);
      frame = evict_frame ();
      if (frame == NULL)
        return NULL;
    }

  lock_acquire (&frame_lock);
  frame->owner = thread_current ();
  frame->page = page;
  frame->pin_count = 1;
  lock_release (&frame_lock);
  return frame;
}

void
frame_free (struct frame *frame)
{
  if (frame == NULL)
    return;

  lock_acquire (&frame_lock);
  if (clock_hand == &frame->elem)
    {
      clock_hand = list_next (clock_hand);
      if (clock_hand == list_end (&frame_table))
        clock_hand = list_begin (&frame_table);
    }

  list_remove (&frame->elem);
  if (list_empty (&frame_table))
    clock_hand = NULL;

  if (frame->page != NULL && frame->page->frame == frame)
    frame->page->frame = NULL;
  palloc_free_page (frame->kpage);
  free (frame);
  lock_release (&frame_lock);
}

void
frame_pin (struct frame *frame)
{
  if (frame == NULL)
    return;

  lock_acquire (&frame_lock);
  frame->pin_count++;
  lock_release (&frame_lock);
}

void
frame_unpin (struct frame *frame)
{
  if (frame == NULL)
    return;

  lock_acquire (&frame_lock);
  ASSERT (frame->pin_count > 0);
  frame->pin_count--;
  lock_release (&frame_lock);
}

static struct frame *
evict_frame (void)
{
  struct frame *victim;
  struct vm_page *page;
  struct thread *owner;
  bool dirty;

  for (;;)
    {
      lock_acquire (&frame_lock);
      victim = select_victim ();
      if (victim == NULL)
        {
          lock_release (&frame_lock);
          return NULL;
        }

      page = victim->page;
      owner = victim->owner;
      ASSERT (page != NULL);
      ASSERT (owner != NULL);
      victim->pin_count++;
      lock_release (&frame_lock);

      lock_acquire (&page->lock);
      lock_acquire (&frame_lock);
      if (victim->pin_count == 1 && victim->page == page
          && victim->owner == owner && page->loaded
          && page->frame == victim)
        {
          lock_release (&frame_lock);
          break;
        }
      victim->pin_count--;
      lock_release (&frame_lock);
      lock_release (&page->lock);
    }

  dirty = pagedir_is_dirty (owner->pagedir, page->upage);
  pagedir_clear_page (owner->pagedir, page->upage);
  page->loaded = false;
  page->frame = NULL;

  if (page->type == VM_MMAP)
    {
      if (dirty)
        write_back_mmap_page (page, victim->kpage);
    }
  else if (page->type == VM_FILE && !dirty)
    {
      /* Clean executable/file-backed pages can be reloaded from file. */
    }
  else
    {
      page->swap_slot = swap_out (victim->kpage);
      page->type = VM_SWAP;
    }
  lock_release (&page->lock);

  lock_acquire (&frame_lock);
  victim->owner = NULL;
  victim->page = NULL;
  victim->pin_count = 1;
  lock_release (&frame_lock);
  return victim;
}

static struct frame *
select_victim (void)
{
  size_t scanned = 0;

  if (list_empty (&frame_table))
    return NULL;

  if (clock_hand == NULL || clock_hand == list_end (&frame_table))
    clock_hand = list_begin (&frame_table);

  while (scanned < list_size (&frame_table) * 2)
    {
      struct frame *frame;

      if (clock_hand == list_end (&frame_table))
        clock_hand = list_begin (&frame_table);

      frame = list_entry (clock_hand, struct frame, elem);
      clock_hand = list_next (clock_hand);
      scanned++;

      if (frame->pin_count > 0)
        continue;

      if (pagedir_is_accessed (frame->owner->pagedir, frame->page->upage))
        {
          pagedir_set_accessed (frame->owner->pagedir, frame->page->upage,
                                false);
          continue;
        }

      return frame;
    }

  return NULL;
}

static void
write_back_mmap_page (struct vm_page *page, void *kpage)
{
  if (page->read_bytes == 0)
    return;

  lock_acquire (&filesys_lock);
  file_write_at (page->file, kpage, page->read_bytes, page->ofs);
  lock_release (&filesys_lock);
}
