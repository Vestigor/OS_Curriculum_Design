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
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct {
  struct spinlock lock;
  int refcount[PHYSTOP / PGSIZE];  // 存放每个页面被引用的次数
} page_ref;

// 获取页面索引
static int
page_index(void *pa)
{
  return ((uint64)pa - (uint64)end) / PGSIZE;
}

void
setref(void *pa)
{
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("ref_init");

  acquire(&page_ref.lock);
  page_ref.refcount[page_index(pa)] = 1;
  release(&page_ref.lock);
}

// 增加引用计数并返回新值
int
incref(void *pa)
{
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("incref");
  
  acquire(&page_ref.lock);
  int ref = ++page_ref.refcount[page_index(pa)];
  release(&page_ref.lock);
  return ref;
}

// 减少引用计数并返回新值
int
decref(void *pa)
{
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("decref");
  
  acquire(&page_ref.lock);
  int idx = page_index(pa);
  if(page_ref.refcount[idx] <= 0)
    panic("decref: refcount already zero");
  int ref = --page_ref.refcount[idx];
  release(&page_ref.lock);
  return ref;
}

// 获取引用计数
int
getref(void *pa)
{
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("getref");
  
  acquire(&page_ref.lock);
  int ref = page_ref.refcount[page_index(pa)];
  release(&page_ref.lock);
  return ref;
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&page_ref.lock, "page_ref");
  memset(page_ref.refcount, 0, sizeof(page_ref.refcount));
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    setref((void *) p);
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
    
  // 原子地减少引用计数并检查
  if(decref(pa) > 0) {
    // 还有其他引用，不释放
    return;
  }

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
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
  if(r) {
    kmem.freelist = r->next;
    setref((void*)r);
  }
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
