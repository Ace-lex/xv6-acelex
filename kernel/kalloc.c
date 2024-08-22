// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock[NCPU];
  struct run *freelist[NCPU];
} kmem;

// 由主进程调用，初始化所有锁
void
kinit()
{
  for (int i = 0; i < NCPU; i++) {
    initlock(&kmem.lock[i], "kmem");
  }
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
// 放回属于当前CPU的freelist
void
kfree(void *pa)
{
  struct run *r;

  push_off();
  int cpu_id = cpuid(); // 获取当前cpuid
  pop_off();
 
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock[cpu_id]);
  r->next = kmem.freelist[cpu_id];
  kmem.freelist[cpu_id] = r;
  release(&kmem.lock[cpu_id]);
}

// 当前CPU的freelist满但其他CPU的freelist有剩余
void steal(struct run **r) {
  for (int i = 0; i <  NCPU; i++) {
    if (kmem.freelist[i]) {
      acquire(&kmem.lock[i]);
      *r = kmem.freelist[i];
      kmem.freelist[i] = (*r)->next;
      release(&kmem.lock[i]);
      break;
    }
  }
}


// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.

// 每个CPU从属于自己的freelist中获取内存块
void *
kalloc(void)
{
  struct run *r;
  push_off();
  int cpu_id = cpuid(); // 获取当前cpuid
  pop_off();

  acquire(&kmem.lock[cpu_id]);
  r = kmem.freelist[cpu_id];
  if(r)
    kmem.freelist[cpu_id] = r->next;
  else {
    // 尝试从其他CPU中的freelist获取空闲内存
    steal(&r);
  }
  release(&kmem.lock[cpu_id]);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
