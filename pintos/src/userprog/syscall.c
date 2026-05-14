#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "userprog/process.h"
#include "threads/synch.h"
#include "filesys/filesys.h"
#include "filesys/file.h"

static void syscall_handler (struct intr_frame *);
struct lock filesys_lock; 

void validate_address(void *addr);
int add_file_to_fdt(struct file *file);
void close_file_by_fd(int fd);

void halt (void);
void exit (int status);
int write (int fd, const void *buffer, unsigned size);

/* Syscall Helper Functions */
void sys_halt(struct intr_frame *f);
void sys_exit(struct intr_frame *f);
void sys_exec(struct intr_frame *f);
void sys_wait(struct intr_frame *f);
void sys_create(struct intr_frame *f);
void sys_remove(struct intr_frame *f);
void sys_open(struct intr_frame *f);
void sys_filesize(struct intr_frame *f);
void sys_read(struct intr_frame *f);
void sys_write(struct intr_frame *f);
void sys_seek(struct intr_frame *f);
void sys_tell(struct intr_frame *f);
void sys_close(struct intr_frame *f);


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
  /* Project 2 and later*/
    case SYS_HALT:
      sys_halt(f);
      break; 
    case SYS_EXIT:
      sys_exit(f);
      break;
    case SYS_EXEC:
      sys_exec(f);
      break;
    case SYS_WAIT:
      sys_wait(f);
      break;
    case SYS_CREATE:
      sys_create(f);
      break;
    case SYS_REMOVE:
      sys_remove(f);
      break;
    case SYS_OPEN:
      sys_open(f);
      break;
    case SYS_FILESIZE:
      sys_filesize(f);
      break;
    case SYS_READ:
      sys_read(f);
      break;
    case SYS_WRITE:
      sys_write(f);
      break;
    case SYS_SEEK:
      sys_seek(f);
      break;
    case SYS_TELL:
      sys_tell(f);
      break;
    case SYS_CLOSE:
      sys_close(f);
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

/* --- Syscall Helper Implementations --- */

void sys_halt(struct intr_frame *f UNUSED) {
  halt();
}

void sys_exit(struct intr_frame *f) {
  uint32_t *esp = f->esp;
  validate_address(esp + 1);
  int status = (int) esp[1];
  exit(status);
}

void sys_exec(struct intr_frame *f UNUSED) {
  /* Not implemented */
}

void sys_wait(struct intr_frame *f UNUSED) {
  /* Not implemented */
}

void sys_create(struct intr_frame *f) {
  uint32_t *esp = f->esp;
  validate_address(esp + 1); 
  validate_address(esp + 2); 
  
  char *file_name = (char *) esp[1]; 
  validate_address(file_name); 
  unsigned size = (unsigned) esp[2]; 

  lock_acquire(&filesys_lock); 
  f->eax = filesys_create(file_name, size); 
  lock_release(&filesys_lock);
}

void sys_remove(struct intr_frame *f) {
  uint32_t *esp = f->esp;
  validate_address(esp + 1); 
  
  char *file_name = (char *) esp[1]; 
  validate_address(file_name); 
  
  lock_acquire(&filesys_lock); 
  f->eax = filesys_remove(file_name); 
  lock_release(&filesys_lock);
}

void sys_open(struct intr_frame *f) {
  uint32_t *esp = f->esp;
  validate_address(esp + 1); 
  
  char *file_name = (char *) esp[1]; 
  validate_address(file_name); 

  lock_acquire(&filesys_lock); 

  struct file *opened_file = filesys_open(file_name); 
  if (opened_file == NULL) {
    f->eax = -1; 
  } else {
    f->eax = add_file_to_fdt(opened_file); 
  }
  lock_release(&filesys_lock);
}

void sys_filesize(struct intr_frame *f UNUSED) {
  /* Not implemented */
}

void sys_read(struct intr_frame *f UNUSED) {
  /* Not implemented */
}

void sys_write(struct intr_frame *f) {
  uint32_t *esp = f->esp;
  validate_address(esp + 1); 
  validate_address(esp + 2);
  validate_address(esp + 3); 

  int fd = (int) esp[1]; 
  void *buffer = (void *) esp[2];  
  validate_address(buffer); 
  unsigned size = (unsigned) esp[3]; 

  f->eax = write(fd, buffer, size);
}

void sys_seek(struct intr_frame *f UNUSED) {
  /* Not implemented */
}

void sys_tell(struct intr_frame *f UNUSED) {
  /* Not implemented */
}

void sys_close(struct intr_frame *f) {
  uint32_t *esp = f->esp;
  validate_address(esp + 1); 
  int fd = (int) esp[1]; 

  close_file_by_fd(fd);
}

/* SYSTEM CALL*/

void validate_address(void *addr) {
  /* check if null pointer or not user virtual address*/
  if (addr == NULL || !is_user_vaddr(addr)) {
    exit(-1); 
  }
}

int add_file_to_fdt(struct file *file) {
  struct thread *curr = thread_current(); 
  
  while (curr->next_fd < 128 && curr->fdt[curr->next_fd] != NULL) {
    curr->next_fd++; 
  }

  if (curr->next_fd >= 128) {return -1;}

  curr->fdt[curr->next_fd] = file; 
  return curr->next_fd;  
}

void close_file_by_fd(int fd) {
  struct thread *curr = thread_current(); 

  /* filter invalid fd*/
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

void halt() {
  shutdown_power_off(); 
}

void exit(int status) {
  struct thread *curr = thread_current(); 

  /* Problem 1 */
  printf("%s: exit(%d)\n", curr->name, status);

  /* TODO: save status */
  
  thread_exit(); 
}

int write(int fd, const void *buffer, unsigned size) {
  /* fd = 1 is stdout */
  if (fd == 1) {  
    putbuf(buffer, size); 
    return size; 
  } 
  
  /* TODO: fd != 1 */
  return -1; 
}

