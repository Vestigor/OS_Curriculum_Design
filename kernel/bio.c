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

#define NBUCKET 13
#define HASH(blockno) (blockno % NBUCKET) // 哈希函数

struct {
  struct spinlock lock;  // 用于保护驱逐操作
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.

  // 哈希桶
  struct {
    struct spinlock lock;
    struct buf head;
  } bucket[NBUCKET];
} bcache;

void
binit(void)
{
  struct buf *b;

  // 初始化每个哈希桶的锁
  for(int i = 0; i < NBUCKET; i++) {
    initlock(&bcache.bucket[i].lock, "bcache.bucket");
    bcache.bucket[i].head.prev = &bcache.bucket[i].head;
    bcache.bucket[i].head.next = &bcache.bucket[i].head;
  }

  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.bucket[0].head.next;
    b->prev = &bcache.bucket[0].head;
    initsleeplock(&b->lock, "buffer");
    bcache.bucket[0].head.next->prev = b;
    bcache.bucket[0].head.next = b;

    b->timestamp = 0;
    b->dev = 0;
    b->blockno = 0;
    b->refcnt = 0;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int idx = HASH(blockno);

  acquire(&bcache.bucket[idx].lock);

  // Is the block already cached?
  for(b = bcache.bucket[idx].head.next; b != &bcache.bucket[idx].head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.bucket[idx].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.bucket[idx].lock);

  acquire(&bcache.lock);

  // 再次检查，避免竞争条件
  acquire(&bcache.bucket[idx].lock);
  for(b = bcache.bucket[idx].head.next; b != &bcache.bucket[idx].head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.bucket[idx].lock);
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.bucket[idx].lock);

  // 查找最久未使用的缓冲区进行驱逐
  struct buf *victim = 0;
  uint min_time = __UINT32_MAX__;
  int victim_idx = -1;
  for(int i = 0; i < NBUCKET; i++) {
    // 最多同时持有两个桶的锁：曾经最优桶和当前最优桶
    acquire(&bcache.bucket[i].lock);
    for(b = bcache.bucket[i].head.next; b != &bcache.bucket[i].head; b = b->next) {
      // 找到新的缓冲块
      if(b->refcnt == 0 && b->timestamp < min_time) {
        // 更新时，需要解开曾经最优桶的锁（不是本身体时），否则可能存在两个以上的桶的锁
        if(victim_idx != -1 && i != victim_idx)
          release(&bcache.bucket[victim_idx].lock);
        min_time = b->timestamp;
        victim = b;
        victim_idx = i;
      }
    }
    // 如果i桶不符合要求，释放其锁
    if(i != victim_idx)
      release(&bcache.bucket[i].lock);
  }

  // 当没有剩余缓冲区可以使用时释放全局锁
  // 此时对于每个i，i != victim_idx都成立，不会持有任何桶的锁
  if(victim == 0) {
    release(&bcache.lock);
    panic("bget: no buffers");
  }

  if (victim_idx != idx) {
    victim->prev->next = victim->next;
    victim->next->prev = victim->prev;
    release(&bcache.bucket[victim_idx].lock);

    acquire(&bcache.bucket[idx].lock);
    victim->next = bcache.bucket[idx].head.next;
    victim->prev = &bcache.bucket[idx].head;
    bcache.bucket[idx].head.next->prev = victim;
    bcache.bucket[idx].head.next = victim;
  }
  
  // 更新victim的信息
  victim->dev = dev;
  victim->blockno = blockno;
  victim->valid = 0;
  victim->refcnt = 1;
  
  release(&bcache.bucket[idx].lock);
  release(&bcache.lock);
  
  acquiresleep(&victim->lock);
  return victim;
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int idx = HASH(b->blockno);
  acquire(&bcache.bucket[idx].lock);
  b->refcnt--;
  // 只在 refcnt 为 0 时更新 timestamp，是因为用到 timestamp 前提是 refcnt 为0
  if (b->refcnt == 0)
    b->timestamp = ticks;
  
  release(&bcache.bucket[idx].lock);
}

void
bpin(struct buf *b) {
  int idx = HASH(b->blockno);
  acquire(&bcache.bucket[idx].lock);
  b->refcnt++;
  release(&bcache.bucket[idx].lock);
}

void
bunpin(struct buf *b) {
  int idx = HASH(b->blockno);
  acquire(&bcache.bucket[idx].lock);
  b->refcnt--;
  release(&bcache.bucket[idx].lock);
}


