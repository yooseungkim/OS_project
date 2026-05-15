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

typedef int pid_t;

static void syscall_handler (struct intr_frame *);
struct lock filesys_lock; 

static void validate_address(void *addr);
static int add_file_to_fdt(struct file *file);
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
  validate_address(esp); 

  int syscall_number = (int) esp[0]; 

  switch (syscall_number) {
    case SYS_HALT:
      halt();
      break; 
    case SYS_EXIT:
      validate_address(esp + 1);
      /* status */
      exit((int) esp[1]);
      break;
    case SYS_EXEC:
      validate_address(esp + 1);
      /* */
      f->eax = exec((const char *) esp[1]);
      break;
    case SYS_WAIT:
      validate_address(esp + 1);
      f->eax = wait((pid_t) esp[1]);
      break;
    case SYS_CREATE:
      validate_address(esp + 1); 
      validate_address(esp + 2); 
      f->eax = create((const char *) esp[1], (unsigned) esp[2]);
      break;
    case SYS_REMOVE:
      validate_address(esp + 1); 
      f->eax = remove((const char *) esp[1]);
      break;
    case SYS_OPEN:
      validate_address(esp + 1); 
      f->eax = open((const char *) esp[1]);
      break;
    case SYS_FILESIZE:
      validate_address(esp + 1);
      f->eax = filesize((int) esp[1]);
      break;
    case SYS_READ:
      validate_address(esp + 1); 
      validate_address(esp + 2);
      validate_address(esp + 3); 
      f->eax = read((int) esp[1], (void *) esp[2], (unsigned) esp[3]);
      break;
    case SYS_WRITE:
      validate_address(esp + 1); 
      validate_address(esp + 2);
      validate_address(esp + 3); 
      f->eax = write((int) esp[1], (const void *) esp[2], (unsigned) esp[3]);
      break;
    case SYS_SEEK:
      validate_address(esp + 1); 
      validate_address(esp + 2);
      seek((int) esp[1], (unsigned) esp[2]);
      break;
    case SYS_TELL:
      validate_address(esp + 1); 
      f->eax = tell((int) esp[1]);
      break;
    case SYS_CLOSE:
      validate_address(esp + 1); 
      close((int) esp[1]);
      break;
    
    /* Project 3 and optionally project 4 */
    case SYS_MMAP:
      break;
    case SYS_MUNMAP:
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
  validate_address((void *)file);
return process_execute(file); 
}

int wait(pid_t pid) {
  return process_wait(pid); 
}

bool create(const char *file, unsigned initial_size) {
  validate_address((void *)file);
  lock_acquire(&filesys_lock); 
  bool success = filesys_create(file, initial_size); 
  lock_release(&filesys_lock);
  return success;
}

bool remove(const char *file) {
  validate_address((void *)file);
  lock_acquire(&filesys_lock); 
  bool success = filesys_remove(file); 
  lock_release(&filesys_lock);
  return success;
}

int open(const char *file) {
  validate_address((void *)file);
  lock_acquire(&filesys_lock); 
  struct file *opened_file = filesys_open(file); 
  int ret = -1;
  if (opened_file != NULL) {
    ret = add_file_to_fdt(opened_file); 
  }
  if (ret == -1) {
    file_close(opened_file);
  }
  lock_release(&filesys_lock);
  return ret;
}

int filesize(int fd) {
  struct thread *curr = thread_current(); 
  struct file *file_obj = curr->fdt[fd];

  if (file_obj == NULL) {
    return -1;
  } 
  lock_acquire(&filesys_lock); 
  int size = file_length(file_obj); 
  lock_release(&filesys_lock); 
  return size; 
}

int read(int fd, void *buffer, unsigned length) {
  validate_address(buffer);
  if (fd == 0) {
    unsigned i; 
    for (i = 0; i < length; i++) {
      ((uint8_t *)buffer)[i] = input_getc(); 
    }
    return length; 
  } 
  else if (fd >= 2 && fd < 128) {
    struct thread *curr = thread_current(); 
    struct file *file_obj = curr->fdt[fd];
    if (file_obj == NULL) {
      return -1; 
    } else {
      lock_acquire(&filesys_lock); 
      int ret = file_read(file_obj, buffer, length); 
      lock_release(&filesys_lock); 
      return ret;
    }
  } else {
    return -1; 
  }
}

int write(int fd, const void *buffer, unsigned size) {
  validate_address((void *)buffer);
  if (fd == 1) {  
    putbuf(buffer, size); 
    return size; 
  } 
  else if (fd >= 2 && fd < 128) {
    struct thread *curr = thread_current(); 
    struct file *file_obj = curr->fdt[fd]; 
    if (file_obj == NULL) {
      return -1;
    } 
    else {
      lock_acquire(&filesys_lock); 
      int ret = file_write(file_obj, buffer, size); 
      lock_release(&filesys_lock);
      return ret; 
    }
  } else {
    return -1; 
  }
}

void seek(int fd, unsigned position) {
  struct thread *curr = thread_current(); 
  struct file *file_obj = curr->fdt[fd];
  
  if (file_obj == NULL) {
    return; 
  }

  lock_acquire(&filesys_lock); 
  file_seek(file_obj, position); 
  lock_release(&filesys_lock); 
}

unsigned tell(int fd) {
  struct thread *curr = thread_current(); 
  struct file *file_obj = curr->fdt[fd]; 

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

/* --- Internal Helper functions --- */
static void validate_address(void *addr) {
  if (addr == NULL || !is_user_vaddr(addr)
      || pagedir_get_page(thread_current()->pagedir, addr) == NULL) {
    exit(-1); 
  }
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
