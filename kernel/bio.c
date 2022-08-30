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

struct {
  struct spinlock lock;
  struct buf buf[NBUF];

} bcache[NBUCKETS];

void
binit(void)
{
  for (int i=0; i<NBUCKETS; i++){
    initlock(&bcache[i].lock, "bcache");
    for(struct buf* b=bcache[i].buf; b!=bcache[i].buf+NBUF; b++)
      initsleeplock(&b->lock, "buffer");
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  uint id = blockno % NBUCKETS;
  acquire(&bcache[id].lock);
  // Is the block already cached?
  struct buf* lru = 0;
  for(struct buf* b=bcache[id].buf; b!=bcache[id].buf+NBUF; b++)
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache[id].lock);
      acquiresleep(&b->lock);
      return b;
    } else if(b->refcnt == 0 && (lru==0 || lru->tstamp > b->tstamp)) lru = b;
  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  if (lru){
    lru->dev = dev;
    lru->blockno = blockno;
    lru->valid = 0;
    lru->refcnt = 1;
    lru->tstamp = ticks;
    release(&bcache[id].lock);
    acquiresleep(&lru->lock);
    return lru;
  }
  release(&bcache[id].lock);
  panic("bget: no buffers");
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
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  uint id = b->blockno % NBUCKETS;
  acquire(&bcache[id].lock);
  b->refcnt--;
  b->tstamp = ticks;
  release(&bcache[id].lock);
}

void
bpin(struct buf *b) {
  uint id = b->blockno % NBUCKETS;
  acquire(&bcache[id].lock);
  b->refcnt++;
  release(&bcache[id].lock);
}

void
bunpin(struct buf *b) {
  uint id = b->blockno % NBUCKETS;
  acquire(&bcache[id].lock);
  b->refcnt--;
  release(&bcache[id].lock);
}


