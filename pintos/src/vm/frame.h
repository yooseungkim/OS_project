#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <list.h>
#include <stddef.h>

struct thread;
struct vm_page;

struct frame
  {
    void *kpage;
    struct thread *owner;
    struct vm_page *page;
    size_t pin_count;
    struct list_elem elem;
  };

void frame_init (void);
struct frame *frame_alloc (struct vm_page *page);
void frame_free (struct frame *frame);
void frame_pin (struct frame *frame);
void frame_unpin (struct frame *frame);

#endif /* vm/frame.h */
