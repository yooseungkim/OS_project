#include "vm/page.h"
#include <debug.h>
#include <hash.h>
#include <round.h>
#include <string.h>
#include "filesys/file.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"

#define STACK_MAX_SIZE (8 * 1024 * 1024)

extern struct lock filesys_lock;

struct mmap_file
  {
    int mapid;
    struct file *file;
    struct list pages;
    struct list_elem elem;
  };

static unsigned page_hash (const struct hash_elem *, void *);
static bool page_less (const struct hash_elem *, const struct hash_elem *,
                       void *);
static void destroy_page_action (struct hash_elem *, void *);
static bool install_page (struct vm_page *, struct frame *);
static void init_page (struct vm_page *, void *, bool, enum vm_page_type);
static bool should_grow_stack (void *, void *);
static bool grow_stack (void *);
static struct mmap_file *find_mapping (int);
static void do_munmap (struct mmap_file *);
static void free_page (struct vm_page *, bool);
static void close_file_locked (struct file *);

void
vm_init (void)
{
  frame_init ();
  swap_init ();
}

bool
vm_process_init (void)
{
  struct thread *cur = thread_current ();

  if (cur->vm_initialized)
    return true;

  if (!hash_init (&cur->spt, page_hash, page_less, NULL))
    return false;

  list_init (&cur->mmap_list);
  cur->next_mapid = 1;
  cur->user_esp = NULL;
  cur->vm_initialized = true;
  return true;
}

void
vm_process_cleanup (void)
{
  struct thread *cur = thread_current ();

  if (!cur->vm_initialized)
    return;

  while (!list_empty (&cur->mmap_list))
    {
      struct mmap_file *mapping;

      mapping = list_entry (list_front (&cur->mmap_list),
                            struct mmap_file, elem);
      do_munmap (mapping);
    }

  hash_destroy (&cur->spt, destroy_page_action);
  cur->vm_initialized = false;
}

struct vm_page *
vm_page_lookup (const void *uaddr)
{
  struct thread *cur = thread_current ();
  struct vm_page page;
  struct hash_elem *elem;

  if (!cur->vm_initialized)
    return NULL;

  page.upage = pg_round_down (uaddr);
  elem = hash_find (&cur->spt, &page.hash_elem);
  return elem == NULL ? NULL : hash_entry (elem, struct vm_page, hash_elem);
}

bool
vm_add_file_page (struct file *file, off_t ofs, void *upage,
                  size_t read_bytes, size_t zero_bytes, bool writable)
{
  struct vm_page *page;

  ASSERT (pg_ofs (upage) == 0);
  ASSERT (read_bytes + zero_bytes == PGSIZE);

  if (vm_page_lookup (upage) != NULL)
    return false;

  page = malloc (sizeof *page);
  if (page == NULL)
    return false;

  init_page (page, upage, writable, VM_FILE);
  page->file = file;
  page->ofs = ofs;
  page->read_bytes = read_bytes;
  page->zero_bytes = zero_bytes;

  if (hash_insert (&thread_current ()->spt, &page->hash_elem) != NULL)
    {
      free (page);
      return false;
    }
  return true;
}

bool
vm_add_stack_page (void *upage, bool load_now)
{
  struct vm_page *page;

  ASSERT (pg_ofs (upage) == 0);
  if (vm_page_lookup (upage) != NULL)
    return false;

  page = malloc (sizeof *page);
  if (page == NULL)
    return false;

  init_page (page, upage, true, VM_STACK);
  page->file = NULL;
  page->ofs = 0;
  page->read_bytes = 0;
  page->zero_bytes = PGSIZE;

  if (hash_insert (&thread_current ()->spt, &page->hash_elem) != NULL)
    {
      free (page);
      return false;
    }

  if (load_now && !vm_load_page (page))
    {
      hash_delete (&thread_current ()->spt, &page->hash_elem);
      free (page);
      return false;
    }
  return true;
}

bool
vm_load_page (struct vm_page *page)
{
  struct frame *frame;
  bool success = false;

  if (page == NULL)
    return false;
  lock_acquire (&page->lock);
  if (page->loaded)
    {
      lock_release (&page->lock);
      return true;
    }

  frame = frame_alloc (page);
  if (frame == NULL)
    {
      lock_release (&page->lock);
      return false;
    }

  switch (page->type)
    {
    case VM_FILE:
    case VM_MMAP:
      if (page->read_bytes > 0)
        {
          lock_acquire (&filesys_lock);
          success = file_read_at (page->file, frame->kpage, page->read_bytes,
                                  page->ofs) == (off_t) page->read_bytes;
          lock_release (&filesys_lock);
        }
      else
        success = true;
      if (success)
        memset ((uint8_t *) frame->kpage + page->read_bytes, 0,
                page->zero_bytes);
      break;

    case VM_STACK:
      if (page->swap_slot != SWAP_SLOT_INVALID)
        {
          success = swap_in (page->swap_slot, frame->kpage);
          if (success)
            page->swap_slot = SWAP_SLOT_INVALID;
        }
      else
        {
          memset (frame->kpage, 0, PGSIZE);
          success = true;
        }
      break;

    case VM_SWAP:
      success = swap_in (page->swap_slot, frame->kpage);
      if (success)
        page->swap_slot = SWAP_SLOT_INVALID;
      break;
    }

  if (!success || !install_page (page, frame))
    {
      frame_free (frame);
      lock_release (&page->lock);
      return false;
    }

  page->loaded = true;
  page->frame = frame;
  frame_unpin (frame);
  pagedir_set_dirty (thread_current ()->pagedir, page->upage, false);
  lock_release (&page->lock);
  return true;
}

bool
vm_handle_fault (void *fault_addr, bool write, void *esp)
{
  struct vm_page *page;
  void *upage;

  if (fault_addr == NULL || !is_user_vaddr (fault_addr))
    return false;

  upage = pg_round_down (fault_addr);
  page = vm_page_lookup (upage);
  if (page != NULL)
    {
      if (write && !page->writable)
        return false;
      return vm_load_page (page);
    }

  if (should_grow_stack (fault_addr, esp))
    return grow_stack (upage);

  return false;
}

bool
vm_load_user_page (const void *uaddr, bool writable)
{
  struct vm_page *page;

  if (uaddr == NULL || !is_user_vaddr (uaddr))
    return false;

  if (!vm_handle_fault ((void *) uaddr, writable, thread_current ()->user_esp))
    return false;

  page = vm_page_lookup (uaddr);
  if (page == NULL || (writable && !page->writable))
    return false;
  return pagedir_get_page (thread_current ()->pagedir, page->upage) != NULL;
}

bool
vm_pin_user_page (const void *uaddr, bool writable)
{
  struct vm_page *page;

  for (;;)
    {
      if (!vm_load_user_page (uaddr, writable))
        return false;

      page = vm_page_lookup (uaddr);
      if (page == NULL)
        return false;

      lock_acquire (&page->lock);
      if (page->loaded && page->frame != NULL)
        {
          frame_pin (page->frame);
          lock_release (&page->lock);
          return true;
        }
      lock_release (&page->lock);
    }
}

void
vm_unpin_user_page (const void *uaddr)
{
  struct vm_page *page = vm_page_lookup (uaddr);

  if (page != NULL)
    {
      lock_acquire (&page->lock);
      if (page->frame != NULL)
        frame_unpin (page->frame);
      lock_release (&page->lock);
    }
}

int
vm_mmap (int fd, void *addr)
{
  struct thread *cur = thread_current ();
  struct file *file;
  struct file *reopened;
  struct mmap_file *mapping;
  off_t length;
  off_t ofs = 0;
  uint8_t *upage = addr;

  if (fd < 2 || fd >= FDT_MAX || addr == NULL || pg_ofs (addr) != 0
      || !is_user_vaddr (addr))
    return -1;

  file = cur->fdt[fd];
  if (file == NULL)
    return -1;

  lock_acquire (&filesys_lock);
  length = file_length (file);
  reopened = length > 0 ? file_reopen (file) : NULL;
  lock_release (&filesys_lock);
  if (length <= 0 || reopened == NULL)
    return -1;

  if ((uintptr_t) addr + length < (uintptr_t) addr
      || !is_user_vaddr ((uint8_t *) addr + length - 1))
    {
      close_file_locked (reopened);
      return -1;
    }

  for (upage = addr; ofs < length; upage += PGSIZE, ofs += PGSIZE)
    {
      if (vm_page_lookup (upage) != NULL)
        {
          close_file_locked (reopened);
          return -1;
        }
    }

  mapping = malloc (sizeof *mapping);
  if (mapping == NULL)
    {
      close_file_locked (reopened);
      return -1;
    }
  mapping->mapid = cur->next_mapid++;
  mapping->file = reopened;
  list_init (&mapping->pages);

  ofs = 0;
  for (upage = addr; ofs < length; upage += PGSIZE, ofs += PGSIZE)
    {
      struct vm_page *page = malloc (sizeof *page);
      size_t page_read_bytes = length - ofs < PGSIZE ? length - ofs : PGSIZE;

      if (page == NULL)
        {
          while (!list_empty (&mapping->pages))
            {
              struct vm_page *old;

              old = list_entry (list_pop_front (&mapping->pages),
                                struct vm_page, mmap_elem);
              hash_delete (&cur->spt, &old->hash_elem);
              free (old);
            }
          close_file_locked (reopened);
          free (mapping);
          return -1;
        }

      init_page (page, upage, true, VM_MMAP);
      page->file = reopened;
      page->ofs = ofs;
      page->read_bytes = page_read_bytes;
      page->zero_bytes = PGSIZE - page_read_bytes;
      page->mapid = mapping->mapid;

      if (hash_insert (&cur->spt, &page->hash_elem) != NULL)
        {
          free (page);
          while (!list_empty (&mapping->pages))
            {
              struct vm_page *old;

              old = list_entry (list_pop_front (&mapping->pages),
                                struct vm_page, mmap_elem);
              hash_delete (&cur->spt, &old->hash_elem);
              free (old);
            }
          close_file_locked (reopened);
          free (mapping);
          return -1;
        }
      list_push_back (&mapping->pages, &page->mmap_elem);
    }

  list_push_back (&cur->mmap_list, &mapping->elem);
  return mapping->mapid;
}

void
vm_munmap (int mapid)
{
  struct mmap_file *mapping = find_mapping (mapid);

  if (mapping != NULL)
    do_munmap (mapping);
}

static bool
install_page (struct vm_page *page, struct frame *frame)
{
  return pagedir_get_page (thread_current ()->pagedir, page->upage) == NULL
         && pagedir_set_page (thread_current ()->pagedir, page->upage,
                              frame->kpage, page->writable);
}

static void
init_page (struct vm_page *page, void *upage, bool writable,
           enum vm_page_type type)
{
  page->upage = upage;
  page->writable = writable;
  page->loaded = false;
  page->type = type;
  page->swap_slot = SWAP_SLOT_INVALID;
  page->mapid = -1;
  page->frame = NULL;
  lock_init (&page->lock);
}

static bool
should_grow_stack (void *fault_addr, void *esp)
{
  if (esp == NULL)
    esp = thread_current ()->user_esp;

  return esp != NULL
         && is_user_vaddr (fault_addr)
         && (uint8_t *) fault_addr >= (uint8_t *) PHYS_BASE - STACK_MAX_SIZE
         && (uint8_t *) fault_addr >= (uint8_t *) esp - 32;
}

static bool
grow_stack (void *upage)
{
  return vm_add_stack_page (upage, true);
}

static struct mmap_file *
find_mapping (int mapid)
{
  struct list_elem *elem;
  struct thread *cur = thread_current ();

  for (elem = list_begin (&cur->mmap_list);
       elem != list_end (&cur->mmap_list); elem = list_next (elem))
    {
      struct mmap_file *mapping;

      mapping = list_entry (elem, struct mmap_file, elem);
      if (mapping->mapid == mapid)
        return mapping;
    }
  return NULL;
}

static void
do_munmap (struct mmap_file *mapping)
{
  struct thread *cur = thread_current ();

  while (!list_empty (&mapping->pages))
    {
      struct vm_page *page;

      page = list_entry (list_pop_front (&mapping->pages),
                         struct vm_page, mmap_elem);
      free_page (page, true);
      hash_delete (&cur->spt, &page->hash_elem);
      free (page);
    }

  list_remove (&mapping->elem);
  close_file_locked (mapping->file);
  free (mapping);
}

static void
free_page (struct vm_page *page, bool write_back)
{
  if (page->loaded && page->frame != NULL)
    {
      if (write_back && page->type == VM_MMAP
          && pagedir_is_dirty (thread_current ()->pagedir, page->upage)
          && page->read_bytes > 0)
        {
          frame_pin (page->frame);
          lock_acquire (&filesys_lock);
          file_write_at (page->file, page->frame->kpage, page->read_bytes,
                         page->ofs);
          lock_release (&filesys_lock);
          frame_unpin (page->frame);
        }
      pagedir_clear_page (thread_current ()->pagedir, page->upage);
      frame_free (page->frame);
      page->loaded = false;
    }

  if (page->swap_slot != SWAP_SLOT_INVALID)
    {
      swap_free (page->swap_slot);
      page->swap_slot = SWAP_SLOT_INVALID;
    }
}

static void
destroy_page_action (struct hash_elem *elem, void *aux UNUSED)
{
  struct vm_page *page = hash_entry (elem, struct vm_page, hash_elem);

  free_page (page, false);
  free (page);
}

static void
close_file_locked (struct file *file)
{
  lock_acquire (&filesys_lock);
  file_close (file);
  lock_release (&filesys_lock);
}

static unsigned
page_hash (const struct hash_elem *elem, void *aux UNUSED)
{
  const struct vm_page *page = hash_entry (elem, struct vm_page, hash_elem);

  return hash_bytes (&page->upage, sizeof page->upage);
}

static bool
page_less (const struct hash_elem *a, const struct hash_elem *b,
           void *aux UNUSED)
{
  const struct vm_page *pa = hash_entry (a, struct vm_page, hash_elem);
  const struct vm_page *pb = hash_entry (b, struct vm_page, hash_elem);

  return pa->upage < pb->upage;
}
