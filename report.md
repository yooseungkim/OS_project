## 보고서 전개 방식

이 보고서는 Project 3 전체를 하나의 사용자 프로그램 실행 환경 구축 문제로 보고 top-down 방식으로 전개한다. 먼저 기존 Pintos가 사용자 프로그램을 실행할 때 만족하지 못하는 조건을 정의하고, 그 다음 이를 해결하기 위한 정책과 알고리즘을 설명한 뒤, 마지막으로 실제 구현이 어떤 파일과 함수로 내려갔는지를 정리한다. 단순히 개별 test case를 나열하기보다는, command line 처리, system call interface, process lifecycle, file descriptor, file system synchronization이 서로 어떻게 연결되는지를 중심으로 설명한다.

내용은 Project 3의 세 문제를 모두 포함하되, 본문에서는 기능의 성격에 따라 재구성한다. 먼저 Argument Passing에서는 Problem 2의 요구사항인 command line parsing과 user stack construction을 다룬다. 그 다음 System Calls에서는 Problem 1의 process termination message와 Problem 3의 system call handler, `halt`, `exit`, `exec`, `wait`, file system 관련 system calls, 비정상 종료 처리, 실행 파일 write deny, resource cleanup을 함께 다룬다. 즉, Problem 1은 `exit`와 process termination 경로 안에서 설명하고, Problem 3은 system call dispatch와 process/file state 관리 전체로 확장해 설명한다.

각 장은 Problem Definition, Policy and Algorithm Design, Implementation의 순서로 구성한다. Problem Definition에서는 왜 기존 Pintos 구조로는 사용자 프로그램 테스트를 통과할 수 없는지 설명한다. Policy and Algorithm Design에서는 user stack layout, user memory validation, parent-child synchronization, file descriptor policy, file system locking, executable write protection의 설계 기준을 제시한다. Implementation에서는 `pintos/src/userprog/process.c`, `pintos/src/userprog/syscall.c`, `pintos/src/userprog/exception.c`, `pintos/src/threads/thread.h`, `pintos/src/threads/thread.c`, 그리고 Project 3 실행 환경을 위한 `pintos/src/utils` 변경이 각각 어떤 역할을 맡는지 정리한다. Results section은 최종 실험 결과 이미지를 삽입할 위치만 placeholder로 둔다.

## I. Problem Definition

Project 3의 핵심은 Pintos가 user program을 실제 process처럼 실행할 수 있도록 만드는 것이다. 이전까지의 테스트는 주로 kernel 내부 thread를 대상으로 했지만, 이번 과제에서는 user program이 command line argument를 받고, system call interrupt를 통해 kernel service를 요청하며, 여러 process가 서로의 process state를 침범하지 않도록 관리되어야 한다. 따라서 문제는 하나의 기능 추가가 아니라 user/kernel 경계 전체를 정의하는 일에 가깝다.

### A. Argument Passing

기존 Pintos의 사용자 프로그램 실행 흐름은 이전 과제에서 커널 내부 테스트를 실행하던 방식과 다르다. 사용자 프로그램은 일반적인 C 프로그램처럼 `main(argc, argv)`가 호출된다고 가정한다. Pintos user library의 `_start()` 역시 사용자 스택에서 `argc`와 `argv`를 읽어 `main()`에 전달하고, `main()`의 반환값을 다시 `exit()`에 넘긴다. 따라서 커널은 사용자 프로그램을 시작하기 전에 command line을 해석하고, 그 결과를 사용자 스택에 올바른 형식으로 배치해야 한다.

기본 구현에서는 `process_execute()`와 `load()`가 command line 전체를 하나의 파일 이름처럼 다룬다. 예를 들어 `process_execute("echo x")`가 호출되면 실제 실행 파일 이름은 `echo`이고 첫 번째 인자는 `x`이다. 그러나 기존 구조에서는 `"echo x"`라는 이름의 파일을 열려고 시도하므로 executable file lookup이 실패한다. 설령 실행 파일을 찾더라도 사용자 스택에 `argc`, `argv`, argument string이 없으면 사용자 프로그램은 시작 직후 잘못된 메모리를 읽게 된다.

Argument passing에서 해결해야 할 문제는 세 가지이다. 첫째, command line을 공백 기준으로 parsing하되 여러 공백을 하나의 구분자로 취급해야 한다. 둘째, 첫 번째 token은 executable file name과 process name으로 사용하고, 전체 token sequence는 사용자 프로그램의 argument list로 보존해야 한다. 셋째, 80x86 calling convention에 맞게 argument string, `argv` 배열, `argc`, fake return address를 사용자 스택에 배치해야 한다. 이 구조는 Stanford Pintos 문서의 user program startup convention을 따른다.

### B. System Calls

사용자 프로그램은 커널 함수를 직접 호출할 수 없으므로 `int $0x30`으로 system call interrupt를 발생시킨다. 이때 system call number와 인자들은 사용자 스택에 저장되어 있으며, 커널의 `syscall_handler()`는 `struct intr_frame`의 `esp`를 통해 이 값을 읽어야 한다. 기본 skeleton은 system call을 구분하지 않고 `"system call!"`을 출력한 뒤 thread를 종료하므로, 정상적인 사용자 프로그램 실행에 필요한 인터페이스가 제공되지 않는다.

Project 3에서 필요한 system call은 크게 process control과 file system interface로 나뉜다. `halt`, `exit`, `exec`, `wait`는 사용자 프로그램이 운영체제와 process lifecycle을 주고받기 위한 호출이다. `create`, `remove`, `open`, `filesize`, `read`, `write`, `seek`, `tell`, `close`는 사용자 프로그램이 Pintos file system에 접근하기 위한 호출이다. 따라서 system call 구현은 단순한 switch statement가 아니라, user pointer 검증, return value 전달, file descriptor translation, file system critical section 보호를 함께 해결해야 한다.

Problem 1의 process termination message는 `exit` system call과 같은 종료 경로에서 처리되어야 한다. 사용자 프로세스가 정상적으로 시작된 뒤 종료될 때에는 다음 형식의 메시지가 출력되어야 한다.

```text
process_name: exit(status)
```

여기서 `process_name`은 `process_execute()`에 전달된 전체 command line이 아니라 command-line arguments를 제외한 실행 파일 이름이어야 한다. 또한 `halt` system call은 운영체제 전체를 종료하는 호출이므로 process termination message를 출력하지 않아야 한다. Executable load 실패 시의 termination message는 Pintos 문서에서 optional로 다루기 때문에, 이 경우에는 parent에게 실패 여부를 전달하는 것이 더 중요한 문제가 된다.

Problem 3의 `exec`, `exit`, `wait`는 프로세스 상태를 parent process와 child process 사이에서 전달하는 문제이다. `exec(cmd_line)`은 새로운 process를 생성하되, child가 executable load에 성공했는지 확인한 뒤에만 pid를 반환해야 한다. `exit(status)`는 현재 사용자 프로세스의 종료 status를 커널에 저장하고 프로세스를 종료해야 한다. `wait(pid)`는 pid가 현재 프로세스의 direct child이면 child가 종료될 때까지 기다린 뒤 child의 exit status를 반환해야 한다. 반대로 pid가 direct child가 아니거나 이미 wait된 child이면 즉시 `-1`을 반환해야 한다.

이를 수식처럼 쓰면 다음과 같다.

```text
exec(cmd_line) = TID_ERROR, if child creation or executable load fails
exec(cmd_line) = child_tid, otherwise

wait(pid) = -1, if pid is not a direct child
wait(pid) = -1, if pid was already waited
wait(pid) = child_exit_status, otherwise
```

여기서 어려운 점은 parent와 child의 종료 순서가 정해져 있지 않다는 것이다. Child가 parent보다 먼저 종료될 수도 있고, parent가 child를 wait하지 않은 채 먼저 종료될 수도 있다. 따라서 `wait`는 단순히 child thread가 끝날 때까지 기다리는 함수가 아니라, exit status를 정확히 한 번만 회수하고 공유 상태 객체의 lifetime을 안전하게 관리하는 문제로 보아야 한다.

File system 관련 system call에서 추가로 해결해야 하는 문제는 kernel 내부의 `struct file *`를 user program에 직접 노출하지 않는 것이다. User program은 정수 file descriptor만 알고 있어야 하며, kernel은 process마다 독립적인 descriptor table을 유지해야 한다. 또한 Pintos의 기본 file system은 내부 synchronization을 제공하지 않기 때문에, 여러 user process가 동시에 file system 함수를 호출하면 directory나 file state가 손상될 수 있다.

마지막으로, user program이 실행 중인 executable file을 동시에 수정할 수 있으면 아직 load되지 않은 page나 disk image와 실행 중인 code 사이의 일관성이 깨질 수 있다. 따라서 실행 중인 executable file에는 write를 금지하고, process가 종료될 때까지 해당 file을 닫지 않는 정책이 필요하다. 이는 Project 3의 system call 구현이 단순히 file operation을 노출하는 것을 넘어, process의 lifetime과 file object의 lifetime을 함께 관리해야 함을 의미한다.

## II. Policy and Algorithm Design

### A. Argument Passing

Argument passing의 핵심 정책은 command line의 두 가지 의미를 분리하는 것이다. 실행 파일을 열 때에는 첫 번째 token만 필요하지만, 사용자 프로그램의 시작 스택을 만들 때에는 command line 전체가 필요하다. 따라서 executable name을 얻기 위한 parsing과 사용자 스택을 구성하기 위한 parsing을 분리한다.

Stack construction은 다음 불변식을 만족하도록 설계한다. 사용자 스택의 높은 주소 영역에는 실제 argument string들이 저장된다. 그 아래에는 `argv[argc] == NULL` sentinel과 각 argument string을 가리키는 포인터들이 저장된다. 그 아래에는 `argv`, `argc`, fake return address가 순서대로 저장된다. 또한 포인터 접근이 정렬되도록 string을 복사한 뒤 stack pointer를 4-byte boundary에 맞춘다.

스택은 개념적으로 다음과 같은 형태를 갖는다.

```text
high address
  argument strings
  word alignment padding
  argv[argc] = NULL
  argv[argc - 1]
  ...
  argv[0]
  argv
  argc
  fake return address
low address
```

이 설계는 `load()`의 책임을 두 단계로 분리한다. 먼저 첫 번째 token으로 executable file을 찾고 ELF segment를 load한다. 그 다음 `setup_stack()`으로 빈 stack page를 만든 뒤, 전체 command line을 기반으로 `argc`와 `argv`를 구성한다. 이 방식은 인자가 없는 경우, 인자가 하나인 경우, 여러 개인 경우, 그리고 중간에 여러 공백이 섞인 경우 모두 같은 규칙으로 처리할 수 있다.

### B. System Calls

System call 설계는 user/kernel 경계를 검증하는 계층, process 상태를 전달하는 계층, file descriptor를 file object로 변환하는 계층으로 나누어 구성한다. 이 구분을 두면 system call handler가 단순히 번호별 함수를 호출하는 코드를 넘어, user program이 kernel 내부 상태를 직접 훼손하지 못하도록 막는 경계 역할을 하게 된다.

#### 1. User Memory and Dispatch

System call handler의 기본 정책은 user memory를 신뢰하지 않는 것이다. System call number 자체가 user stack에 있으므로 handler는 먼저 `esp`가 user address space에 속하고 현재 page table에 mapping되어 있는지 확인해야 한다. 그 후 `esp[0]`에서 system call number를 읽고, 각 system call이 요구하는 stack argument slot과 pointer argument의 시작 주소를 검증한 뒤 실제 handler로 분기한다. 유효하지 않은 주소나 정의되지 않은 system call은 현재 process를 `exit(-1)`로 종료시킨다.

System call의 return value는 80x86 calling convention에 따라 `struct intr_frame`의 `eax`에 저장한다. 따라서 반환값이 있는 system call은 handler에서 직접 `f->eax`를 갱신하고, 반환값이 없는 호출은 필요한 side effect만 수행한다. 이 정책은 user program 입장에서 일반 C 함수 호출처럼 system call 결과를 받을 수 있게 한다.

#### 2. Process Lifecycle

`exit`의 정책은 정상 종료와 비정상 종료를 같은 status propagation 경로로 통합하는 것이다. 사용자 프로그램이 `exit(status)`를 호출하면 현재 thread에 status를 저장하고 termination message를 출력한 뒤 일반적인 `thread_exit()` 경로로 들어간다. User mode에서 page fault나 다른 exception이 발생한 경우에도 `exit(-1)`을 호출하게 하여 parent가 `wait`을 통해 일관되게 `-1`을 받을 수 있게 한다. 반면 `halt()`는 `exit()`를 거치지 않고 shutdown을 수행하므로 termination message를 출력하지 않는다.

`exec`와 `wait`는 parent와 child가 공유하는 `child_status` 객체를 중심으로 설계한다. Parent의 `children` list에는 child마다 하나의 `child_status`가 들어간다. 이 객체는 child tid, load 성공 여부, exit status, wait 여부, load semaphore, exit semaphore, reference count를 가진다. Parent와 child가 같은 객체를 참조하므로 reference count의 초기값은 2로 설정한다.

동기화는 load 단계와 exit 단계로 나뉜다. Parent는 child를 생성한 뒤 child가 executable load에 성공했는지 알 때까지 `load_sema`에서 기다린다. Child는 `load()` 결과를 `load_success`에 저장하고 `load_sema`를 올린다. 이 정책은 load에 실패한 child의 pid가 parent에게 성공한 exec 결과처럼 반환되는 race를 막는다.

그 다음 parent가 `wait(pid)`를 호출하면 parent의 `children` list에서 direct child인지 확인한다. 존재하지 않거나 이미 wait된 child이면 즉시 `-1`을 반환한다. 유효한 child이면 wait 대상에서 중복 회수되지 않도록 list에서 제거하고, `exit_sema`에서 child의 종료를 기다린다. Child가 이미 종료되어 semaphore를 올린 상태라면 parent는 즉시 진행하고, child가 아직 실행 중이면 종료될 때까지 block된다.

#### 3. File Descriptor and File System

File descriptor 정책은 Unix 계열의 기본 규칙을 따른다. `0`은 standard input, `1`은 standard output으로 예약하고, 실제 file을 열 때에는 `2` 이상의 descriptor를 할당한다. 각 process는 독립적인 file descriptor table을 가지므로 child process는 parent의 descriptor를 상속하지 않는다. 같은 file을 여러 번 열면 각각 다른 descriptor와 file position을 갖는다.

```text
fd = 0                  standard input
fd = 1                  standard output
2 <= fd < FDT_MAX       process-local open file
```

이 범위 밖의 descriptor는 일반 file object와 연결하지 않는 값으로 취급한다. User program이 system call을 통해 접근하는 file system operation은 하나의 global lock으로 보호한다. Pintos의 기본 file system은 동시에 여러 thread가 접근해도 안전하다는 보장을 제공하지 않으므로, `create`, `remove`, `open`, `filesize`, file `read`, file `write`, `seek`, `tell`처럼 file system 상태를 읽거나 바꾸는 호출은 critical section 안에서 실행한다. 반면 console I/O는 file system object를 거치지 않으므로 별도로 처리한다. `read(0, ...)`은 keyboard input을 읽고, `write(1, ...)`은 `putbuf()`로 console에 출력한다. 그 외 descriptor는 file descriptor table을 통해 `struct file *`로 변환한 뒤 file operation을 수행한다.

#### 4. Executable Write Protection and Cleanup

실행 중인 executable file은 별도의 lifetime 정책을 적용한다. `load()`에서 executable file을 연 뒤 `file_deny_write()`를 호출하고, 그 file pointer를 현재 thread에 저장한다. 기존 loader처럼 `load()`가 끝날 때 즉시 file을 닫으면 deny 상태도 함께 해제되므로, 성공적으로 load된 경우에는 process가 종료될 때까지 file을 열어 둔다. Load에 실패한 경우에는 열린 file을 닫고, 성공한 경우에는 `process_exit()`에서 닫아 write deny를 해제한다.

## III. Implementation

### A. Argument Passing

Argument passing은 주로 `pintos/src/userprog/process.c`에서 구현되었다. Problem 2 구현은 `process_execute()`, `load()`, `construct_stack()`의 세 위치에서 command line을 처리한다.

`process_execute()`는 먼저 full command line을 page 단위로 복사하여 child process에 전달할 수 있게 한다. 동시에 local copy를 `strtok_r()`로 parsing하여 첫 번째 token을 추출하고, 이를 `thread_create()`의 thread name으로 사용한다. 이로써 `process_execute("echo x")`의 경우 child thread name은 `"echo"`가 되며, Problem 1의 termination message에서도 argument가 제외된 process name을 사용할 수 있다. 또한 child 생성에 필요한 `process_start_args`를 함께 구성하여, full command line과 parent-child 공유 상태를 child thread에 한 번에 전달한다.

`start_process()`는 기존처럼 단순한 `char *file_name`만 받지 않고, command line을 담은 `process_start_args`를 받는다. Child thread는 이 구조체에서 full command line을 꺼내 `load()`에 전달한다. 이를 통해 parent가 넘긴 command line 원본이 child의 executable lookup과 stack construction 단계까지 보존된다.

`load()`는 전달받은 full command line을 다시 parsing하여 첫 번째 token만 `filesys_open()`에 넘긴다. 따라서 argument가 포함된 command line이 들어오더라도 executable file lookup은 program name만 기준으로 수행된다. 이후 `setup_stack()`이 사용자 주소 공간의 최상단에 빈 stack page를 만들면, `construct_stack()`이 full command line을 기반으로 실제 인자들을 사용자 스택에 배치한다. Load에 성공한 executable file은 `file_deny_write()`가 적용된 상태로 현재 thread의 `executable` 필드에 저장되고, load에 실패한 경우에만 즉시 닫힌다.

`construct_stack()`은 command line을 token으로 분리한 뒤, argument string들을 뒤에서부터 stack에 복사한다. 각 string이 복사된 user virtual address는 별도의 배열에 저장한다. 이후 stack pointer를 4-byte boundary에 맞추고, `argv[argc] == NULL`, 각 `argv[i]`, `argv`, `argc`, fake return address 순서로 값을 push한다. 구현에서는 command line copy와 argument pointer 배열에 합리적인 고정 크기 제한을 두어 Project 3 테스트에서 요구하는 argument passing 범위를 처리한다.

### B. System Calls

System Calls 구현은 `pintos/src/userprog/syscall.c`의 dispatch layer, `pintos/src/userprog/process.c`의 process lifecycle layer, `pintos/src/userprog/exception.c`의 abnormal termination path, 그리고 `pintos/src/threads/thread.h`와 `pintos/src/threads/thread.c`의 per-process state 확장으로 나뉜다.

#### 1. Dispatch and User Memory Check

System call dispatch는 `pintos/src/userprog/syscall.c`에서 구현되었다. `syscall_init()`은 interrupt `0x30`을 등록하고, file system 관련 system call에서 사용할 전역 lock을 초기화한다. `syscall_handler()`는 `f->esp`를 `uint32_t *`로 해석하기 전에 `validate_address()`로 user stack pointer를 검사한다. 이후 `esp[0]`에서 system call number를 읽고 `SYS_HALT`부터 `SYS_CLOSE`까지 각 case로 분기한다. 반환값이 있는 `exec`, `wait`, `create`, `remove`, `open`, `filesize`, `read`, `write`, `tell`은 결과를 `f->eax`에 저장한다. Project 3 범위를 벗어나는 `mmap`, `munmap`, directory 관련 system call은 별도 동작을 수행하지 않도록 남겨 두었다.

`validate_address()`는 주소가 `NULL`이 아니고, user virtual address 범위에 있으며, 현재 process의 page table에 mapping되어 있는지 확인한다. 조건을 만족하지 않으면 `exit(-1)`을 호출한다. 현재 구현은 system call argument가 놓인 stack slot과 pointer argument의 시작 주소를 검사하는 방식으로 kernel이 명백히 잘못된 user address를 직접 역참조하지 않게 한다. 즉, buffer 전체 범위를 별도 loop로 검사하기보다는 각 system call 진입 시 필요한 대표 주소를 검증하는 단순한 방식이다.

#### 2. Process Termination and Exceptions

`exit(int status)`는 현재 thread의 이름과 status를 출력하고, `thread_current()->exit_status`에 status를 저장한 뒤 `thread_exit()`을 호출한다. Process termination message는 thread name을 사용하므로, 앞서 `process_execute()`에서 첫 번째 token을 thread name으로 설정한 것이 그대로 Problem 1의 요구사항과 연결된다. `halt()`는 `shutdown_power_off()`만 호출하므로 `exit` message를 출력하지 않는다. `exec(const char *file)`은 user pointer를 검증한 뒤 `process_execute()`를 호출하고, `wait(pid)`는 `process_wait()`에 위임한다.

비정상 종료는 `pintos/src/userprog/exception.c`에서 같은 흐름으로 연결된다. 이를 위해 `pintos/src/userprog/syscall.h`에 `exit(int status)` prototype을 추가하여 exception handler가 system call 구현의 exit 경로를 직접 사용할 수 있게 했다. User code segment에서 exception이 발생하거나 user mode page fault가 발생하면 기존의 interrupt frame dump 및 직접 `thread_exit()` 경로 대신 `exit(-1)`을 호출한다. Kernel mode에서 발생한 fault는 여전히 kernel bug로 처리한다. 이로써 parent는 child가 system call로 종료했는지, user exception으로 종료되었는지와 무관하게 `wait`를 통해 일관된 status를 얻는다.

#### 3. Exec/Wait Synchronization

`wait`의 핵심 구현은 `pintos/src/userprog/process.c`의 `child_status`와 `process_wait()`이다. `process_execute()`는 child 생성 전에 `child_status`와 `process_start_args`를 할당한다. `process_start_args`는 full command line과 `child_status` 포인터를 함께 child에게 전달하기 위한 구조체이다. Child 생성에 성공하면 parent는 `child_status`를 자신의 `children` list에 추가하고, child가 load 결과를 기록할 때까지 `load_sema`에서 대기한다. Load가 실패하면 list에서 제거하고 상태 객체를 release한 뒤 `TID_ERROR`를 반환한다.

`start_process()`는 전달받은 `process_start_args`에서 full command line과 `child_status`를 꺼낸 뒤, 현재 thread의 `child_status` 필드에 저장한다. 이후 `load()`를 호출하고 그 결과를 `cs->load_success`에 기록한 뒤 `load_sema`를 올린다. Load에 실패한 child는 `exit_status`를 `-1`로 설정하고 종료한다. 이 경로에서는 `exit()` system call을 직접 호출하지 않으므로 별도의 termination message를 출력하지 않는다.

`process_wait(child_tid)`는 현재 thread의 `children` list에서 child tid와 일치하는 `child_status`를 찾는다. 찾지 못하면 direct child가 아니거나 이미 list에서 제거된 child로 보고 `-1`을 반환한다. 유효한 child라면 `waited`를 설정하고 list에서 제거한 뒤 `exit_sema`를 기다린다. Child가 종료되면 `process_exit()`이 `child_status->exit_status`에 자신의 `exit_status`를 복사하고 `exit_sema`를 올리므로, parent는 semaphore에서 깨어난 뒤 status를 반환할 수 있다.

Parent와 child의 종료 순서가 달라질 수 있으므로 `child_status_release()`는 reference count와 lock을 사용한다. Parent가 wait을 끝냈거나 wait하지 않은 child 목록을 정리할 때 한 번 release하고, child가 `process_exit()`에서 자신의 상태 전달을 끝냈을 때 한 번 release한다. Reference count가 0이 되는 순간에만 상태 객체를 해제하므로 child가 먼저 끝나는 경우와 parent가 먼저 끝나는 경우 모두에서 use-after-free를 방지한다.

#### 4. File Descriptor and File System Calls

File descriptor table은 `pintos/src/threads/thread.h`의 `struct thread`에 `fdt[FDT_MAX]`와 `next_fd`로 추가되었다. `FDT_MAX`는 128로 두고, `next_fd`는 `pintos/src/threads/thread.c`의 `init_thread()`에서 2로 초기화한다. `add_file_to_fdt()`는 `next_fd`부터 비어 있는 entry를 찾아 열린 file을 저장하고 descriptor를 반환한다. `open(file)`은 `filesys_open()`으로 얻은 file object를 descriptor table에 등록하고, 등록할 수 없으면 열린 file object를 닫은 뒤 `-1`을 반환하도록 구성하였다. `close(fd)`는 `close_file_by_fd()`를 통해 descriptor table entry를 비우고 file을 닫는다.

File system 관련 호출은 대부분 Pintos가 제공하는 file system 함수를 감싼다. `create`와 `remove`는 각각 `filesys_create()`와 `filesys_remove()`를 호출한다. `filesize`는 유효한 descriptor가 가리키는 file에 대해 `file_length()`를 반환한다. `read`는 descriptor `0`이면 `input_getc()`로 keyboard input을 buffer에 채우고, 일반 file이면 `file_read()`를 호출한다. `write`는 descriptor `1`이면 `putbuf()`로 console에 출력하고, 일반 file이면 `file_write()`를 호출한다. `seek`와 `tell`은 file position을 각각 `file_seek()`와 `file_tell()`로 조작한다. 이 계층은 user-visible descriptor를 kernel 내부 file object로 변환하는 역할을 맡기 때문에, user program은 `struct file`의 존재를 알 필요가 없다.

#### 5. Resource Cleanup and Runtime Configuration

추가적인 process 상태는 `pintos/src/threads/thread.c`에서 초기화된다. `init_thread()`는 thread 구조체를 0으로 초기화한 뒤 `next_fd`를 2로 설정하고, `exit_status`, `executable`, `children`, `child_status`를 초기 상태로 만든다. `process_exit()`은 child status 전달뿐 아니라 wait하지 않은 child 상태 객체를 release하고, 열린 file descriptor와 executable file을 닫아 process 종료 시 자원이 남지 않도록 한다. 이때 executable file을 닫으면 `file_deny_write()`로 걸어둔 write deny도 함께 해제된다.

마지막으로 `pintos/src/utils/pintos`와 `pintos/src/utils/Pintos.pm`의 기본 kernel 및 loader 경로를 `threads/build`에서 `userprog/build`로 변경하였다. Project 3의 `make check`는 `pintos/src/userprog` 기준으로 실행되므로, 이 설정을 바꾸어야 user program 테스트가 Project 3에서 빌드한 kernel과 loader를 사용한다.

## IV. Results

![Project3 Result](./result.png)
