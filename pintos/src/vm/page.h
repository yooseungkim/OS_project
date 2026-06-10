#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include <list.h>
#include <stdbool.h>
#include <stddef.h>
#include "filesys/off_t.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "vm/swap.h"

struct file;
struct frame;

enum vm_page_type
  {
    VM_FILE,
    VM_MMAP,
    VM_STACK,
    VM_SWAP
  };

struct vm_page
  {
    void *upage;
    bool writable;
    bool loaded;
    enum vm_page_type type;

    struct file *file;
    off_t ofs;
    size_t read_bytes;
    size_t zero_bytes;

    size_t swap_slot;
    int mapid;

    struct frame *frame;
    struct lock lock;
    struct hash_elem hash_elem;
    struct list_elem mmap_elem;
  };

void vm_init (void);
bool vm_process_init (void);
void vm_process_cleanup (void);

struct vm_page *vm_page_lookup (const void *uaddr);
bool vm_add_file_page (struct file *file, off_t ofs, void *upage,
                       size_t read_bytes, size_t zero_bytes, bool writable);
bool vm_add_stack_page (void *upage, bool load_now);
bool vm_load_page (struct vm_page *page);
bool vm_handle_fault (void *fault_addr, bool write, void *esp);

bool vm_load_user_page (const void *uaddr, bool writable);
bool vm_pin_user_page (const void *uaddr, bool writable);
void vm_unpin_user_page (const void *uaddr);

int vm_mmap (int fd, void *addr);
void vm_munmap (int mapid);

#endif /* vm/page.h */
