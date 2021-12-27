#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>

#include "mm.h"
#include "memlib.h"

/* If you want debugging output, use the following macro.
 * When you hand in, remove the #define DEBUG line. */
// #define DEBUG
#ifdef DEBUG
#define debug(fmt, ...) printf("%s: " fmt "\n", __func__, __VA_ARGS__)
#define msg(...) printf(__VA_ARGS__)
#else
#define debug(fmt, ...)
#define msg(...)
#endif

#define __unused __attribute__((unused))

/* do not change the following! */
#ifdef DRIVER
/* create aliases for driver tests */
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#endif /* !DRIVER */

typedef int32_t word_t; /* Heap is bascially an array of 4-byte words. */

typedef enum {
  FREE = 0,     /* Block is free */
  USED = 1,     /* Block is used */
  PREVFREE = 2, /* Previous block is free (optimized boundary tags) */
} bt_flags;

static word_t *heap_start; /* Address of the first block */
static word_t *heap_end;   /* Address past last byte of last block */
static word_t *last;       /* Points at last block */
static word_t *free_blocks;


/*__Funkcje obsługujące boundary tagi czyli headera i footera__*/

/* zwraca wielkość bloku pamięci */
static inline word_t bt_size(word_t *bt) {
  return *bt & ~(USED | PREVFREE);
}

/* zwraca 0/1 mówiące czy dany blok jest używany czy nie */
static inline int bt_used(word_t *bt) {
  return *bt & USED;
}
/* zwraca 0/1 mówiące czy dany blok jest wolny czy nie */
static inline int bt_free(word_t *bt) {
  return !(*bt & USED);
}

/* zwraca adres footera */
static inline word_t *bt_footer(word_t *bt) {
  return (void *)bt + bt_size(bt) - sizeof(word_t);
}

/* input adres paylodu output header */
static inline word_t *bt_fromptr(void *ptr) {
  return (word_t *)ptr - 1;
}

/* Stworzenie headera i footera
   W inpucie chce mieć wskaźnik na cały blok pamięci */
static inline void bt_make(word_t *bt, size_t size, bt_flags flags) {
    //ustawiam header
    *bt = size | flags;
    //ustawian footer
     bt = bt_footer(bt);
    *bt = size | flags;
}

/* zwraca adres payload. */
static inline void *bt_payload(word_t *bt) {
  return bt + 1;
}

/* __Funkcje obsługujące tablice wolnych bloków__ */

/* zapamiętanie następnego wollnego bloku */
static inline void set_next_fb(word_t *bt, word_t offset)
{
    *(bt + 2) = offset;  
}
/* zapamiętanie poprzedniego wolnego bloku */
static inline void set_prev_fb(word_t *bt, word_t offset)
{
    *(bt + 1) = offset;  
}

/* zwraca null lub wskaźnik na następny blok pamięci */
static inline word_t *next_fb(word_t *bt) {
    word_t offset = *(bt + 2);
    if(offset == -1)
        return NULL;
    return heap_start + offset ;
}

/* zwraca null lub wskaźnik na poprzedni blok pamięci */
static inline word_t *prev_fb(word_t *bt) {
    word_t offset = *(bt + 1);
   // printf("%d", offset);
    if(offset == -1)
        return NULL;
    return heap_start + offset ;
}

static inline void set_new_fb(word_t *bt, word_t *next)
{
   word_t *prev = prev_fb(next); 
  if(prev == NULL)
  {
    free_blocks = bt;
    set_prev_fb(bt, -1);
  }
  else{
    set_next_fb(prev, bt - heap_start);
    set_prev_fb(bt, prev - heap_start);
  }
  set_next_fb(bt, next- heap_start);

  set_prev_fb(next, bt - heap_start);
}
static inline void add_new_fb(word_t *bt)
{
  
  
    if(free_blocks == NULL)
    {
       set_next_fb(bt, -1);
       set_prev_fb(bt, -1);
       free_blocks = bt;
       //f("ijk %d", *free_blocks);
       return;
    }
    else{
        word_t *block = free_blocks;
        word_t *next_block = next_fb(block);
        word_t size = bt_size(bt);
        word_t listed_block_size = bt_size(block);
        while(next_block != NULL)
        {
            if(size <= listed_block_size)
            {
                set_new_fb(bt, block);
                return;
            }
            block = next_block;
            listed_block_size = bt_size(block);
            next_block = next_fb(next_block);
        }
        //ostatni blok
        set_next_fb(block, bt - heap_start);
        set_prev_fb(bt, block - heap_start);
        set_next_fb(bt, -1);
    }
}
//nie zmienia FLAGI!!!!
static inline void remove_fb(word_t *bt)
{
  word_t *prev = prev_fb(bt);
  word_t *next = next_fb(bt);
  //*bt = *bt | USED;
  //został tylko jeden blok na liście
  if(prev == NULL && next == NULL)
  {
    //printf("samotny1");
    free_blocks = NULL;
  }
  //usun 1 blok
  else if(prev == NULL)
  {
   //printf("samotny2");
    free_blocks = next;
    set_prev_fb(next, -1);
  }
  //usun ostatni blok
  else if(next == NULL)
  {
    //printf("samotny3");
    set_next_fb(prev, -1);
  }
  //usun blok pomiędzy dwoma innymi blokami
  else{
   // printf("samotny4");
    set_next_fb(prev, next_fb(bt)- heap_start);
    set_prev_fb(next, prev_fb(bt)- heap_start);

  }
}
/* złączenie dwóch bloków, algorytm jest wolną adaptacją kodu funkcji coalasce
   z książki CS:APP 
   na początku sprawdzam czy bloki z tyłu z przodu są wolne, aby potem wykonać odpowiednie kroki*/
static inline word_t *coalasce(word_t *bt)
{
  word_t *next;
  word_t *prev;
  word_t next_free;
  word_t prev_free;
  word_t size;
  if(bt != heap_start )
  {
    //printf("a");
    prev = bt - 1;
    //printf("a");
    prev_free = bt_free(prev);
    prev = bt - (bt_size(prev)/4);
   // printf("prev_free %d", prev_free);
  }
  else
  {
   // printf("b");
    prev_free = 0;
  }
  next = bt  + (bt_size(bt)/4);
  if(next < heap_end)
  {
    //printf("c");
    //next = bt  + (bt_size(bt)/4);
    next_free = bt_free(next);
    //printf("\nnext free: %d\n", next_free);
  }
  else{
  //  printf("d");
    next_free = 0;
  }
  //okalające bloki są wolne
  if(next_free && prev_free)
  {
  //  printf("e");
    remove_fb(prev);
    remove_fb(next);
    size = bt_size(prev) + bt_size(bt) + bt_size(next);
    bt = prev;
    bt_make(bt, size, FREE);
    add_new_fb(bt);
  }
  //poprzedni blok jest wolny
  else if(prev_free && !next_free)
  {
    //printf("f");
    //prev = bt - (bt_size(prev)/4);
    remove_fb(prev);
    size = bt_size(prev) + bt_size(bt);
    bt = prev;
    bt_make(bt, size, FREE);
    add_new_fb(bt);
  }
  //następny blok jest wolny
  else if(!prev_free && next_free)
  {
    //printf("g");
    //mm_checkheap(1);
   // printf("\n%d\n", (bt_size(bt)/4));
    remove_fb(next);
    //printf("g");
    size = bt_size(bt) + bt_size(next);
    bt_make(bt, size, FREE);
    add_new_fb(bt);
  }
  //bloki z przodu i tyłu są zajęte
  else{
   // printf("h");
    size = bt_size(bt);
    bt_make(bt, size, FREE);
    add_new_fb(bt);

    //printf("%ls", free_blocks);

  }
  return bt;


}
/* --=[ miscellanous procedures ]=------------------------------------------ */

/* Calculates block size incl. header, footer & payload,
 * and aligns it to block boundary (ALIGNMENT).
  korzystam z round_up
 */
static inline size_t normalize_size(size_t size) {
  size += 2 * sizeof(word_t);//miejsce na footer i header 
  return (size + ALIGNMENT - 1) & -ALIGNMENT;
}

static void *morecore(size_t size) {
  void *ptr = mem_sbrk(size);
  if (ptr == (void *)-1)
    return NULL;
  return ptr;
}

/* --=[ mm_init ]=---------------------------------------------------------- */

int mm_init(void) {
  void *ptr = morecore(ALIGNMENT - sizeof(word_t));
  if (!ptr)
    return -1;
  heap_start = NULL;
  heap_end = NULL;
  last = NULL;
  free_blocks = NULL;
  return 0;
}

/* --=[ malloc ]=----------------------------------------------------------- 
   defakto tutaj mamy bloki posortowane już w fb od najmniejszego do największego
   a wię przechodząc ta list po koleji 1 który nam się pojawi będzie tym porządanym
  najmniejszy<...<największy
*/

/* Best fit startegy. */
static word_t *find_fit(size_t reqsz) {
  word_t *fb = free_blocks;
  word_t *prev = NULL;
  size_t size = bt_size(fb);
  while(fb != NULL)
  {
    if(reqsz<=size)
    {
     // printf("req %ld\t ten ma%ld\n", reqsz, size);
      remove_fb(fb);
      size_t size_diff =  size - reqsz; 
      if(size_diff > 6* sizeof(word_t))
      {
       // printf("tu %ld - %ld",size, reqsz);
        word_t *new_fb = fb + (reqsz/4);
        bt_make(fb, reqsz, USED);
        bt_make(new_fb, size_diff, FREE);
        add_new_fb(new_fb); 
      }
      return fb;
    }
    prev = fb;
    fb = next_fb(fb);
    size = bt_size(fb);
  }
  if((prev != NULL) && (prev + (bt_size(prev) / 4) >= heap_end))
  {
    remove_fb(prev);
    word_t size_diff = reqsz - bt_size(prev);
    mem_sbrk(size_diff);
    last = prev;
    heap_end = heap_end + (size_diff / 4);
    bt_make(prev, reqsz, FREE);
    return prev;
  }
  return NULL;

}

/* 
   Funkcja malloc jest zainspirowana kodem z książki CS:APP to znaczy, 
   że inspirowałem się stworzonym tam algorytmem i adoptowałem go
   do moich potrzeb.
   NIe złącza ostatniego wolnego bloku!!!!
   
*/
//int i = 0;
void *mm_malloc(size_t size) {
 // i++;
  //print_memory();
 // printf("malloc\n");
  //mm_checkheap(1);
  if(size == 0)
  {
    return NULL;
  }
  size = normalize_size(size);
  //if(free_blocks!= NULL)
   // printf("\nmalloc %ld free_block_size:%d\n",size,  *free_blocks);
  //else
  //  printf("\nmalloc %ld\n", size);
  size_t blocks = size / 4;
  word_t *new_block;
  if(free_blocks == NULL)
  {
    new_block = mem_sbrk(size);
    last = new_block;
    heap_end = new_block + blocks;
    if(heap_start == 0)
    {
      heap_start = new_block;
    }
    //return new_block;
  }
  else{
    new_block = find_fit(size);
    if(new_block == NULL)
    {
      new_block = mem_sbrk(size);
      last = new_block;
      heap_end = new_block + blocks;
    }
    else
    {
      size = bt_size(new_block);
      //  size = block_size;
      }
    }
  
  bt_make(new_block, size, USED);
  new_block = bt_payload(new_block);
  return new_block;

}

/* --=[ free ]=-------------------------------------------------------------
    implementacj na podstawie  */

void mm_free(void *ptr) {
  //_memory();
  //printf("free\n");
  
  if(ptr != NULL)
  {
    word_t *bt = bt_fromptr(ptr);//dostaniemy bt
   // size_t size = get_size(bt);
   //bt_make(bt, size, FREE);
    bt = coalasce(bt);
  }

}

/* --=[ realloc ]=---------------------------------------------------------- 
  implementacja na podstawie dokumentacji funkcji z so21_projekt_malloc_v0.pdf*/

void *mm_realloc(void *old_ptr, size_t size) {
   //printf("realloc");
  //jeśli ptr == NULL, to wywołanie jest tożsame z «mm_malloc(size)»,
  if(old_ptr == NULL)
  {
    return mm_malloc(size);
  }
  //jeśli «size» jest równy 0, to wywołanie jest tożsame z «mm_free(ptr)»,
  if(size == 0)
  {
    mm_free(old_ptr);
    return NULL;
  }
  //wpp
  word_t *bt = bt_fromptr(old_ptr);
  size_t size_bt = bt_size(bt);
  //jeżeli zmniejszamy blok to po prost oddajemy stary blok
  if(size_bt - 2*(sizeof(word_t))  >= size)
  {
    return old_ptr;
  }
  //gdy trzeba powiększyć
  word_t *next = bt + bt_size(bt)/4;
   //printf("aaaaaaaaa");
  if(next < heap_end && bt_free(next))
  {
    size_t size_next = bt_size(next);
    if(size_next + size_bt - (2*sizeof(word_t)) >= size)
    {
      //mm_checkheap(1);
      //printf("%ld\n", size);
      remove_fb(next);
     // printf("aaaaaaaaa");
      bt_make(bt, size_next + size_bt, USED);
      return bt_payload(bt);
    }
  }
 // printf("a");
  void *new = mm_malloc(size);

  memcpy(new , old_ptr, size_bt);
   free(old_ptr);
  return new;
  //return NULL;
}

/* --=[ calloc ]=----------------------------------------------------------- 
    z mm-implicit.c nic nie zmieniłem*/

void *mm_calloc(size_t nmemb, size_t size) {
 // printf("calloc");
  size_t bytes = nmemb * size;
  void *new_ptr = malloc(bytes);
  if (new_ptr)
    memset(new_ptr, 0, bytes);
  return new_ptr;
}

/* --=[ mm_checkheap ]=----------------------------------------------------- 
   Błędy:
   1 - wolny blok nie jest znaczony jako wolny blok
   2  - wskaźniki na poprzedni i następny blok wskazują poza zaalokowaną sterte
   3 - występują dwa wolne bloki obok siebie
   4 - nie wszystkie wolne bloki są na liście wolnych bloków
   */

void mm_checkheap(int verbose) {
  int error = 0;
  int error_num = -1;
  //sprawdzam czy wszytkie bloki na wolnej liście są ustawione jako wolne
  int count_free_block = 0;
  word_t *block = free_blocks;
  word_t *next;
  word_t *prev;
  while(block != NULL)
  {
    count_free_block++;
    if(bt_used(block))
    {
      error = 1;
      error_num = 1;
    }
    next = next_fb(block);
    prev = prev_fb(block);
    if(!(next == NULL || next < heap_end))
    {
      error = 1;
      error_num = 2;
    }
    if(!(prev == NULL || prev >= heap_start))
    {
      error = 1;
      error_num = 2;
    }
    block = next;
  }
  block = heap_start;
  while(block!=heap_end)
  {
    next = block + (bt_size(block)/4);
    if(bt_free(block))    
    {
      if(bt_free(next) && next!=heap_end)
      {
        error = 1;
        error_num = 3;
      }
      count_free_block--;
     
    }
    block = next;
  }
  if(count_free_block != 0)
  {
    error = 1;
    error_num = 4;
  }
  if(verbose != 0)
  {
  printf("printed debug info\n");
  if(heap_start == NULL)
  {
    printf("empty memory");
    return;
  }
  block = heap_start;
 
  word_t size;
  int free;
  int index_next;
  int index_prev;
  printf("heap start: %ls\t, heap end: %ls\n", heap_start, heap_end);
  while(block!=heap_end)
  {
    size = bt_size(block);
    free = bt_free(block);
    if(free)
    {
      index_prev= *(block + 1);
      index_next = *(block + 2);
      printf("\nF: %d, %d, %d\n", size, index_prev, index_next);
    }
    else{
      printf("Z: %d", size);
    }
    block = block + (size /4);
    

  }

}
  if(error)
  {
    printf("\n%d\n", error_num);
    exit(error_num);
  }
}

