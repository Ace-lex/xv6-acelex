// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define BUCKET_SIZE 13

struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head;

  // 哈希表
  struct buf hash_table[BUCKET_SIZE];
  struct spinlock bucket_lock[BUCKET_SIZE];
} bcache;

void binit(void) {
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  // Create linked list of buffers
  // 头插法
  // bcache.head.prev = &bcache.head;
  // bcache.head.next = &bcache.head;
  // for (b = bcache.buf; b < bcache.buf + NBUF; b++) {
  //   b->next = bcache.head.next;
  //   b->prev = &bcache.head;
  //   initsleeplock(&b->lock, "buffer");
  //   bcache.head.next->prev = b;
  //   bcache.head.next = b;
  // }

  // 初始化哈希表和bucket锁
  for (int i = 0; i < BUCKET_SIZE; i++) {
    initlock(&bcache.bucket_lock[i], "bcache.bucket");
    bcache.hash_table[i].prev = &bcache.hash_table[i];
    bcache.hash_table[i].next = &bcache.hash_table[i];
  }

  // 将所有buf先放到第一个桶中
  for (b = bcache.buf; b < bcache.buf + NBUF; b++) {
    b->next = bcache.hash_table[0].next;
    b->prev = &bcache.hash_table[0];
    initsleeplock(&b->lock, "buffer");
    bcache.hash_table[0].next->prev = b;
    bcache.hash_table[0].next = b;
  }
}

// 从其他的bucket中steal
int steal_buf(struct buf **b, int exclude) {
  struct buf *tmp;

  // 遍历所有桶的双端链表，查找空闲block
  for (int i = 0; i < BUCKET_SIZE; i++) {
    // 注意不要遍历当前桶，否则会出现同一进程重复加锁的情况
    if (i != exclude) {
      acquire(&bcache.bucket_lock[i]);
      for (tmp = bcache.hash_table[i].next; tmp != &bcache.hash_table[i];
           tmp = tmp->next) {
        if (tmp->refcnt == 0) {
          // 从原链表中删除节点
          tmp->next->prev = tmp->prev;
          tmp->prev->next = tmp->next;

          *b = tmp;
          release(&bcache.bucket_lock[i]);
          return 1;
        }
      }
      release(&bcache.bucket_lock[i]);
    }
  }
  return 0;
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf *bget(uint dev, uint blockno) {
  // printf("bget\n");
  struct buf *b;

  // acquire(&bcache.lock);

  // 获取哈希表索引
  int bucket_idx = blockno % BUCKET_SIZE;

  // 为指定的桶加锁
  acquire(&bcache.bucket_lock[bucket_idx]);

  // // Is the block already cached?
  // for(b = bcache.head.next; b != &bcache.head; b = b->next){
  //   if(b->dev == dev && b->blockno == blockno){
  //     b->refcnt++;
  //     release(&bcache.lock);
  //     acquiresleep(&b->lock);
  //     return b;
  //   }
  // }

  // 遍历指定桶的双端链表，查找block
  for (b = bcache.hash_table[bucket_idx].next;
       b != &bcache.hash_table[bucket_idx]; b = b->next) {
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      // release(&bcache.lock);
      release(&bcache.bucket_lock[bucket_idx]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  // for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
  //   if(b->refcnt == 0) {
  //     b->dev = dev;
  //     b->blockno = blockno;
  //     b->valid = 0;
  //     b->refcnt = 1;
  //     release(&bcache.lock);
  //     acquiresleep(&b->lock);
  //     return b;
  //   }
  // }

  // 桶中没有存放指定block
  for (b = bcache.hash_table[bucket_idx].prev;
       b != &bcache.hash_table[bucket_idx]; b = b->next) {
    if (b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.bucket_lock[bucket_idx]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // 为确保加锁顺序（先加大锁，后加小锁），防止死锁，先释放之前的锁，再重新加锁
  release(&bcache.bucket_lock[bucket_idx]);
  acquire(&bcache.lock);
  acquire(&bcache.bucket_lock[bucket_idx]);
  
  // 检查是否有其他进程在释放锁重新加锁的间隙插入了buf
  for (b = bcache.hash_table[bucket_idx].next;
       b != &bcache.hash_table[bucket_idx]; b = b->next) {
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      // release(&bcache.lock);
      release(&bcache.bucket_lock[bucket_idx]);
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  if (steal_buf(&b, bucket_idx)) {
    // 插入到当前Bucket中
    b->next = &bcache.hash_table[bucket_idx];
    b->prev = bcache.hash_table[bucket_idx].prev;

    bcache.hash_table[bucket_idx].prev->next = b;
    bcache.hash_table[bucket_idx].prev = b;

    b->dev = dev;
    b->blockno = blockno;
    b->valid = 0;
    b->refcnt = 1;
    release(&bcache.lock);
    release(&bcache.bucket_lock[bucket_idx]);
    acquiresleep(&b->lock);
    return b;
  }

  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf *bread(uint dev, uint blockno) {
  struct buf *b;

  b = bget(dev, blockno);
  if (!b->valid) {
    // printf("virtio_disk_rw\n");
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void bwrite(struct buf *b) {
  if (!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void brelse(struct buf *b) {
  // printf("brelse\n");
  if (!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  // acquire(&bcache.lock);

  // 给指定桶加锁
  int bucket_idx = b->blockno % BUCKET_SIZE;
  acquire(&bcache.bucket_lock[bucket_idx]);

  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    // b->next = bcache.head.next;
    // b->prev = &bcache.head;
    // bcache.head.next->prev = b;
    // bcache.head.next = b;

    // 调整指定桶的双端链表
    b->next = bcache.hash_table[bucket_idx].next;
    b->prev = &bcache.hash_table[bucket_idx];
    bcache.hash_table[bucket_idx].next->prev = b;
    bcache.hash_table[bucket_idx].next = b;
  }
  release(&bcache.bucket_lock[bucket_idx]);
  // release(&bcache.lock);
}

void bpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt++;
  release(&bcache.lock);
}

void bunpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt--;
  release(&bcache.lock);
}
