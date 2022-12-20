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

extern char end[];              // first address after kernel.
                                // defined by kernel.ld.
int refcount[PHYSTOP / PGSIZE]; // total physical memory by size of a page size
struct run
{
  struct run *next;
};

struct
{
  struct spinlock lock;
  struct run *freelist;
} kmem;
// increase the reference count
void incref(uint64 pa)
{
  int pagenumber = pa / PGSIZE;
  acquire(&kmem.lock); // mutiple process may be trying to refer to the sam epage so you need to acquire lock
  if (pa > PHYSTOP || refcount[pagenumber] < 1)
    panic("incref");
  refcount[pagenumber] += 1;
  release(&kmem.lock);
}
void kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void *)PHYSTOP);
}

void freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char *)PGROUNDUP((uint64)pa_start);
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
  { 
    refcount[(uint64)p/4096]=1;
    kfree(p);
  }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
// here xv6 asssumes there is only one reference count but that isn't the case
void kfree(void *pa)
{
  struct run *r;
 r=(struct run *)pa;
  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");
  // here you put a lock so that multiple process are not decrementing at same time
  acquire(&kmem.lock);
  int page_n = (uint64)pa / PGSIZE; // dividing page size by 4096
  if (refcount[page_n] < 1)
  {
    panic("can't decrement");
  }
  refcount[page_n]--; // decrement the reference count now it may be possible to process exit the core at same time
  // and call kfreee but here lock hepls out
  int temporary = refcount[page_n];
  release(&kmem.lock); // release the lock
  if (temporary > 0)
  {
    return;
  }
  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  

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
  if (r)
  {
    kmem.freelist = r->next;
    int page_n = (uint64)r / PGSIZE; // finding page number
    if (refcount[page_n] != 0)
      panic("new page already pointed by\n");
    refcount[page_n] = 1; // initialising ref to 1
    // increment reference count whenevre cpy and write fork and decrement it when the process exits
    kmem.freelist=r->next;
  }
  release(&kmem.lock);

  if (r)
    memset((char *)r, 5, PGSIZE); // fill with junk
  return (void *)r;
}