#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "userprog/process.h"

static void syscall_handler (struct intr_frame *);
struct lock filesys_lock; 

void validate_address(void *addr);

void halt (void);
void exit (int status);
int write (int fd, const void *buffer, unsigned size);


void
syscall_init (void) 
{
  lock_init (&filesys_lock);  
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  uint32_t *esp = f->esp; 
  validate_address(esp); 


  int syscall_number = *(int *)esp; 

  switch (syscall_number) {
  /* Project 2 and later*/
    case SYS_HALT:
      halt(); 
      break; 
    case SYS_EXIT:
      validate_address(esp + 1);
      int status = *(int *) esp + 1; 
      exit(status); 
      break;
    case SYS_EXEC:
      break;
    case SYS_WAIT:
      break;
    case SYS_CREATE:
      validate_address(esp + 1); 
      validate_address(esp + 2); 
      
      char *file_name = *(char **) esp + 1; 
      validate_address(file_name); 
      unsigned size = *(unsigned *) esp + 2; 

      lock_acquire(&filesys_lock); 
      f->eax = filesys_create(file_name, size); 
      lock_release(&filesys_lock);
      break;
    case SYS_REMOVE:
      validate_address(esp + 1); 
      
      char *file_name = *(char **) esp + 1; 
      validate_address(file_name); 
      
      lock_acquire(&filesys_lock); 
      f->eax = filesys_remove(file_name); 
      lock_release(&filesys_lock);
      break;
    case SYS_OPEN:
      break;
    case SYS_FILESIZE:
      break;
    case SYS_READ:
      break;
    case SYS_WRITE:
      validate_address(esp + 1); 
      validate_address(esp + 2);
      validate_address(esp + 3); 

      int fd = *(int *) esp + 1; 
      void *buffer = *(void **) esp + 2;  
      /* Make sure to check every pointers */
      validate_address(buffer); 
      unsigned size = *(unsigned *) esp + 3; 

      f->eax = write(fd, buffer, size);
      break;
    case SYS_SEEK:
      break;
    case SYS_TELL:
      break;
    case SYS_CLOSE:
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
      break; 

  }

}

/* SYSTEM CALL*/

void validate_address(void *addr) {
  /* check if null pointer or not user virtual address*/
  if (addr == NULL || !is_user_vaddr(addr)) {
    exit(-1); 
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

