#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "date.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

#define RESET_ACCESS (0xffffffffffffffff - PTE_A)
#define MAX_CHECK_ACCESS 32

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


#ifdef LAB_PGTBL
int
sys_pgaccess(void)
{
  // lab pgtbl: your code here.
  uint64 start_va;
  int page_num;
  uint32 res_abits = 0;
  uint64 dst_addr;

  // 获取起始虚拟地址
  if (argaddr(0, &start_va) < 0) {
    return -1;
  }

  // 获取需要检查的页数
  if (argint(1, &page_num) < 0) {
    return -1;
  }

  if (page_num > MAX_CHECK_ACCESS) {
    return -1;
  }

  // 获取结果存放的地址
  if (argaddr(2, &dst_addr)) {
    return -1;
  }
  
  for (int i = 0; i < page_num; i++) {
    uint32 curr_va = start_va + PGSIZE * i;
    pte_t *curr_pte;

    // 返回当前虚拟地址对应的pte
    curr_pte = walk(myproc()->pagetable, curr_va, 0);
    
    // 判断是否被访问
    if ((*curr_pte & PTE_A)) {
      res_abits |= (1 << i);

      // 清空访问位
      *curr_pte = (*curr_pte) & RESET_ACCESS;
    }

  }

  if (copyout(myproc()->pagetable, dst_addr, (char *)&res_abits, sizeof(res_abits)) < 0) {
    return -1;
  }

  return 0;
}
#endif

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
