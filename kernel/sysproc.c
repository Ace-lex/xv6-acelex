#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "fcntl.h"

#include "sleeplock.h"
#include "fs.h"
#include "file.h"

#define INVALID_ADDR 0xffffffffffffffff

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  if(argint(n, &fd) < 0)
    return -1;
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}


uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;

  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_mmap(void) {
  int length;
  struct file *f;
  int prot;
  int flags;

  if (argint(1, &length) < 0) {
    return INVALID_ADDR;
  }
   
  if (argint(2, &prot) < 0) {
    return INVALID_ADDR;
  }

  if (argint(3, &flags) < 0) {
    return INVALID_ADDR;
  }

  if (argfd(4, 0, &f) < 0) {
    return INVALID_ADDR;
  } 

  // 检查prot是否符合文件读写权限要求
  if ((prot & PROT_READ) && f->readable == 0) {
    return -1;
  }

  // 检查prot是否符合文件读写权限要求
  if ((prot & PROT_WRITE) && f->writable == 0 && flags == MAP_SHARED) {
    return -1;
  }


  // 存储文件的空间从堆顶到堆底分配
  uint64 va = PGROUNDDOWN(myproc()->curr_addr - length);

  int curr_mmap = myproc()->curr_mmap;
  myproc()->vmas[curr_mmap].addr = va;
  myproc()->vmas[curr_mmap].length = length;
  myproc()->vmas[curr_mmap].mmap_file = f;
  myproc()->vmas[curr_mmap].prot = prot;
  myproc()->vmas[curr_mmap].flags = flags;
  myproc()->vmas[curr_mmap].valid = 1;

  filedup(f);

  // 更新可用内存区域地址
  myproc()->curr_mmap++;
  myproc()->curr_addr = va;
  return va;
}

// 检查mmapregion是否可被释放（如果所有页面均被解除映射则可以释放）
int check(struct vma *curr_vma) {
  for (uint64 addr = curr_vma->addr; addr < curr_vma->addr + curr_vma->length; addr += PGSIZE) {
    if (walkaddr(myproc()->pagetable, PGROUNDDOWN(addr)) != 0) {
      return 0;
    }
  }
  return 1;
}


// munmap只会释放mmaped region的一部分，不会出现跨越区域的情况
uint64
sys_munmap(void) {
  uint64 addr;
  int length;
  uint64 start_va;
  int npages;

  if (argaddr(0, &addr) < 0) {
    return -1;
  }

  if (argint(1, &length) < 0) {
    return -1;
  }

  struct vma *curr_vma = 0;

  // 判断待释放的地址属于哪一个mmaped region
  for (uint i = 0; i < myproc()->curr_mmap; i++) {
    if (addr >= myproc()->vmas[i].addr && addr < myproc()->vmas[i].addr + myproc()->vmas[i].length && myproc()->vmas[i].valid) {
      curr_vma = &myproc()->vmas[i];
      break;
    }
  }
  
  if (curr_vma == 0) return 0;
  
  // 如果本身就没有映射进内存，直接返回
  if (walkaddr(myproc()->pagetable, PGROUNDDOWN(addr)) == 0) {
    return 0;
  }

  // 如果flag为MAP_SHARED且文件可写，写回磁盘
  if (curr_vma->flags == MAP_SHARED && curr_vma->mmap_file->writable) {
    if (filewrite(curr_vma->mmap_file, addr, length) == -1) {
      printf("filewrite fail\n");
      return -1;
    }
  }

  // 解除映射（一定要先写回磁盘再解除映射）
  start_va = PGROUNDDOWN(addr);
  npages = ((addr + length) - start_va + PGSIZE - 1) / PGSIZE;

  uvmunmap(myproc()->pagetable, start_va, npages, 1);
  

  // 如果一个mmap region的空间全部解除映射，则文件引用数减1
  if (check(curr_vma)) {
    fileclose(curr_vma->mmap_file);
    // 标记为已经unmap
    curr_vma->valid = 0;
  }

  return 0;
}