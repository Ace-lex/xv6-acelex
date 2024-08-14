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

uint ref_count[PHYSTOP / PGSIZE] = {0};

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

// 防止多线程同时访问ref_count
struct spinlock ref_count_lock;

void IncrRef(uint64 pa) {
  acquire(&ref_count_lock);
  ref_count[(uint64)pa / PGSIZE]++;
  release(&ref_count_lock);
}

void DecrRef(uint64 pa) {
  acquire(&ref_count_lock);
  ref_count[(uint64)pa / PGSIZE]--;
  release(&ref_count_lock);
}


void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&ref_count_lock, "refcount");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    acquire(&ref_count_lock);
    ref_count[(uint64)p / 4096] = 1;
    release(&ref_count_lock);
    kfree(p);
  }
    
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // 只有页面引用数为0时可以free页面
  // 注意执行顺序，保证可以free后再进行页面清空等操作
  acquire(&ref_count_lock);
  if (ref_count[(uint64)pa / 4096] <= 0) {
    panic("ref_count error\n");
  }
  ref_count[(uint64)pa / 4096]--;
  if (ref_count[(uint64)pa / 4096] == 0) {
    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);

    r = (struct run*)pa;

    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
  };
  release(&ref_count_lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r) {
    memset((char*)r, 5, PGSIZE); // fill with junk
    // 设置引用数为1
    if (ref_count[(uint64)r / PGSIZE] != 0) panic("kalloc error\n");
    acquire(&ref_count_lock);
    ref_count[(uint64)r / PGSIZE] = 1;
    release(&ref_count_lock);
  }
  return (void*)r;
}
