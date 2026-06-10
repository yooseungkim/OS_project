#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "threads/synch.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/input.h"
#include "threads/malloc.h"
#ifdef VM
#include "vm/page.h"
#endif

typedef int pid_t;

static void syscall_handler (struct intr_frame *);
struct lock filesys_lock; 

static void validate_address(void *addr);
static void validate_user_buffer(const void *buffer, unsigned size,
                                 bool writable);
static void pin_user_buffer(const void *buffer, unsigned size, bool writable);
static void unpin_user_buffer(const void *buffer, unsigned size);
static char *copy_in_string(const char *ustr);
static int add_file_to_fdt(struct file *file);
static struct file *get_file_by_fd(int fd);
static void close_file_by_fd(int fd);

/* Syscall Prototypes */
void halt (void);
void exit (int status);
pid_t exec (const char *file);
int wait (pid_t);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned length);
int write (int fd, const void *buffer, unsigned length);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);
int mmap (int fd, void *addr);
void munmap (int mapid);

void
syscall_init (void) 
{
  lock_init (&filesys_lock);  
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  uint32_t *esp = f->esp; 
#ifdef VM
  thread_current ()->user_esp = f->esp;
#endif
  validate_user_buffer (esp, sizeof *esp, false);

  int syscall_number = (int) esp[0]; 

  switch (syscall_number) {
    case SYS_HALT:
      halt();
      break; 
    case SYS_EXIT:
      validate_user_buffer (esp, sizeof *esp * 2, false);
      /* status */
      exit((int) esp[1]);
      break;
    case SYS_EXEC:
      validate_user_buffer (esp, sizeof *esp * 2, false);
      /* */
      f->eax = exec((const char *) esp[1]);
      break;
    case SYS_WAIT:
      validate_user_buffer (esp, sizeof *esp * 2, false);
      f->eax = wait((pid_t) esp[1]);
      break;
    case SYS_CREATE:
      validate_user_buffer (esp, sizeof *esp * 3, false);
      f->eax = create((const char *) esp[1], (unsigned) esp[2]);
      break;
    case SYS_REMOVE:
      validate_user_buffer (esp, sizeof *esp * 2, false);
      f->eax = remove((const char *) esp[1]);
      break;
    case SYS_OPEN:
      validate_user_buffer (esp, sizeof *esp * 2, false);
      f->eax = open((const char *) esp[1]);
      break;
    case SYS_FILESIZE:
      validate_user_buffer (esp, sizeof *esp * 2, false);
      f->eax = filesize((int) esp[1]);
      break;
    case SYS_READ:
      validate_user_buffer (esp, sizeof *esp * 4, false);
      f->eax = read((int) esp[1], (void *) esp[2], (unsigned) esp[3]);
      break;
    case SYS_WRITE:
      validate_user_buffer (esp, sizeof *esp * 4, false);
      f->eax = write((int) esp[1], (const void *) esp[2], (unsigned) esp[3]);
      break;
    case SYS_SEEK:
      validate_user_buffer (esp, sizeof *esp * 3, false);
      seek((int) esp[1], (unsigned) esp[2]);
      break;
    case SYS_TELL:
      validate_user_buffer (esp, sizeof *esp * 2, false);
      f->eax = tell((int) esp[1]);
      break;
    case SYS_CLOSE:
      validate_user_buffer (esp, sizeof *esp * 2, false);
      close((int) esp[1]);
      break;
    
    /* Project 3 and optionally project 4 */
    case SYS_MMAP:
      validate_user_buffer (esp, sizeof *esp * 3, false);
      f->eax = mmap ((int) esp[1], (void *) esp[2]);
      break;
    case SYS_MUNMAP:
      validate_user_buffer (esp, sizeof *esp * 2, false);
      munmap ((int) esp[1]);
      break;
    
    /* Project 4 only */
    case SYS_CHDIR:
      break;
    case SYS_MKDIR:
      break;
    case SYS_READDIR:
      break;
    case SYS_ISDIR:
      break;
    case SYS_INUMBER:
      break;
    default:
      exit(-1);
      break; 
  }
}

/* System Call */

void halt(void) {
  shutdown_power_off();
}

void exit(int status) {
  struct thread *curr = thread_current(); 
  printf("%s: exit(%d)\n", curr->name, status);
  curr->exit_status = status; 
  thread_exit(); 
}

pid_t exec(const char *file) {
  char *kfile = copy_in_string (file);
  pid_t ret = process_execute (kfile);

  palloc_free_page (kfile);
  return ret;
}

int wait(pid_t pid) {
  return process_wait(pid); 
}

bool create(const char *file, unsigned initial_size) {
  char *kfile = copy_in_string (file);
  lock_acquire(&filesys_lock); 
  bool success = filesys_create(kfile, initial_size);
  lock_release(&filesys_lock);
  palloc_free_page (kfile);
  return success;
}

bool remove(const char *file) {
  char *kfile = copy_in_string (file);
  lock_acquire(&filesys_lock); 
  bool success = filesys_remove(kfile);
  lock_release(&filesys_lock);
  palloc_free_page (kfile);
  return success;
}

int open(const char *file) {
  char *kfile = copy_in_string (file);
  lock_acquire(&filesys_lock); 
  struct file *opened_file = filesys_open(kfile);
  int ret = -1;
  if (opened_file != NULL) {
    ret = add_file_to_fdt(opened_file); 
  }
  if (ret == -1) {
    file_close(opened_file);
  }
  lock_release(&filesys_lock);
  palloc_free_page (kfile);
  return ret;
}

int filesize(int fd) {
  struct file *file_obj = get_file_by_fd(fd);

  if (file_obj == NULL) {
    return -1;
  } 
  lock_acquire(&filesys_lock); 
  int size = file_length(file_obj); 
  lock_release(&filesys_lock); 
  return size; 
}

int read(int fd, void *buffer, unsigned length) {
  pin_user_buffer (buffer, length, true);
  if (fd == 0) {
    unsigned i; 
    for (i = 0; i < length; i++) {
      ((uint8_t *)buffer)[i] = input_getc(); 
    }
    unpin_user_buffer (buffer, length);
    return length; 
  } 
  else if (fd >= 2 && fd < 128) {
    struct file *file_obj = get_file_by_fd(fd);
    if (file_obj == NULL) {
      unpin_user_buffer (buffer, length);
      return -1; 
    } else {
      lock_acquire(&filesys_lock); 
      int ret = file_read(file_obj, buffer, length); 
      lock_release(&filesys_lock); 
      unpin_user_buffer (buffer, length);
      return ret;
    }
  } else {
    unpin_user_buffer (buffer, length);
    return -1; 
  }
}

int write(int fd, const void *buffer, unsigned size) {
  pin_user_buffer (buffer, size, false);
  if (fd == 1) {  
    putbuf(buffer, size); 
    unpin_user_buffer (buffer, size);
    return size; 
  } 
  else if (fd >= 2 && fd < 128) {
    struct file *file_obj = get_file_by_fd(fd);
    if (file_obj == NULL) {
      unpin_user_buffer (buffer, size);
      return -1;
    } 
    else {
      lock_acquire(&filesys_lock); 
      int ret = file_write(file_obj, buffer, size); 
      lock_release(&filesys_lock);
      unpin_user_buffer (buffer, size);
      return ret; 
    }
  } else {
    unpin_user_buffer (buffer, size);
    return -1; 
  }
}

void seek(int fd, unsigned position) {
  struct file *file_obj = get_file_by_fd(fd);
  
  if (file_obj == NULL) {
    return; 
  }

  lock_acquire(&filesys_lock); 
  file_seek(file_obj, position); 
  lock_release(&filesys_lock); 
}

unsigned tell(int fd) {
  struct file *file_obj = get_file_by_fd(fd);

  if (file_obj == NULL) {
    return -1; 
  } 
  lock_acquire(&filesys_lock);
  int position = file_tell(file_obj);
  lock_release(&filesys_lock);
  return position;
}

void close(int fd) {
  close_file_by_fd(fd);
}

int mmap(int fd, void *addr) {
#ifdef VM
  return vm_mmap (fd, addr);
#else
  return -1;
#endif
}

void munmap(int mapid) {
#ifdef VM
  vm_munmap (mapid);
#else
  (void) mapid;
#endif
}

/* --- Internal Helper functions --- */
static void validate_address(void *addr) {
#ifdef VM
  if (!vm_load_user_page (addr, false))
    exit(-1);
#else
  if (addr == NULL || !is_user_vaddr(addr)
      || pagedir_get_page(thread_current()->pagedir, addr) == NULL) {
    exit(-1); 
  }
#endif
}

static void validate_user_buffer(const void *buffer, unsigned size,
                                 bool writable) {
  const uint8_t *start = buffer;
  const uint8_t *end;
  const uint8_t *page;
  const uint8_t *last_page;

  if (size == 0)
    return;
  if (buffer == NULL)
    exit (-1);

  end = start + size - 1;
  if (end < start || !is_user_vaddr (start) || !is_user_vaddr (end))
    exit (-1);

  last_page = pg_round_down (end);
  for (page = pg_round_down (start); page <= last_page; page += PGSIZE)
    {
#ifdef VM
      if (!vm_load_user_page (page, writable))
        exit (-1);
#else
      if (pagedir_get_page (thread_current ()->pagedir, page) == NULL)
        exit (-1);
#endif
    }
}

static void pin_user_buffer(const void *buffer, unsigned size, bool writable) {
  const uint8_t *start = buffer;
  const uint8_t *end;
  const uint8_t *page;
  const uint8_t *last_page;

  validate_user_buffer (buffer, size, writable);
#ifdef VM
  if (size == 0)
    return;
  end = start + size - 1;
  last_page = pg_round_down (end);
  for (page = pg_round_down (start); page <= last_page; page += PGSIZE)
    {
      if (!vm_pin_user_page (page, writable))
        exit (-1);
    }
#endif
}

static void unpin_user_buffer(const void *buffer, unsigned size) {
#ifdef VM
  const uint8_t *start = buffer;
  const uint8_t *end;
  const uint8_t *page;
  const uint8_t *last_page;

  if (size == 0 || buffer == NULL)
    return;

  end = start + size - 1;
  if (end < start)
    return;

  last_page = pg_round_down (end);
  for (page = pg_round_down (start); page <= last_page; page += PGSIZE)
    vm_unpin_user_page (page);
#else
  (void) buffer;
  (void) size;
#endif
}

static char *copy_in_string(const char *ustr) {
  char *kstr;
  size_t i;

  kstr = palloc_get_page (0);
  if (kstr == NULL)
    exit (-1);

  for (i = 0; i < PGSIZE; i++)
    {
      validate_address ((void *) (ustr + i));
      kstr[i] = ustr[i];
      if (kstr[i] == '\0')
        return kstr;
    }

  palloc_free_page (kstr);
  exit (-1);
}

static int add_file_to_fdt(struct file *file) {
  struct thread *curr = thread_current(); 
  while (curr->next_fd < 128 && curr->fdt[curr->next_fd] != NULL) {
    curr->next_fd++; 
  }
  if (curr->next_fd >= 128) {return -1;}
  curr->fdt[curr->next_fd] = file; 
  return curr->next_fd;  
}

static struct file *get_file_by_fd(int fd) {
  if (fd < 2 || fd >= FDT_MAX)
    return NULL;
  return thread_current ()->fdt[fd];
}

static void close_file_by_fd(int fd) {
  struct thread *curr = thread_current(); 
  if (fd < 2 || fd >= 128) {
    return; 
  } 
  if (curr->fdt[fd] != NULL) {
    lock_acquire(&filesys_lock); 
    file_close(curr->fdt[fd]); 
    lock_release(&filesys_lock); 
    curr->fdt[fd] = NULL;
  }
}
