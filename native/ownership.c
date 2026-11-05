#include "ownership.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
struct S4Allocation { struct S4Allocation *next,*prev; S4Owner *owner;size_t size; max_align_t align; };
static S4Owner shared;
static _Thread_local S4Owner *current;
S4Owner *s4_owner_use(S4Owner *o) {S4Owner *p=current;current=o;return p;}
void *s4_owned_malloc(size_t n) {
 if(n>SIZE_MAX-sizeof(S4Allocation)) return NULL;
 S4Allocation *a=malloc(sizeof *a+n);if(!a)return NULL;
 S4Owner *o=current?current:&shared;
 a->size=n;a->owner=o;a->prev=NULL;a->next=o->head;
 if(a->next)a->next->prev=a;o->head=a;o->bytes+=n;o->blocks++;
 return a+1;
}
void *s4_shared_malloc(size_t n) {S4Owner *old=s4_owner_use(NULL);void *p=s4_owned_malloc(n);s4_owner_use(old);return p;}
void s4_owned_free(void *p) {
 if(!p)return;S4Allocation *a=(S4Allocation*)p-1;S4Owner *o=a->owner;
 if(a->prev)a->prev->next=a->next;else o->head=a->next;
 if(a->next)a->next->prev=a->prev;o->bytes-=a->size;o->blocks--;free(a);
}
void *s4_owned_realloc(void *p,size_t n) {
 if(!p)return s4_owned_malloc(n);if(!n){s4_owned_free(p);return NULL;}
 S4Allocation *a=(S4Allocation*)p-1;S4Owner *old=s4_owner_use(a->owner);
 void *q=s4_owned_malloc(n);s4_owner_use(old);if(!q)return NULL;
 memcpy(q,p,a->size<n?a->size:n);s4_owned_free(p);return q;
}
void *s4_owned_calloc(size_t n,size_t size) {
 if(size && n>SIZE_MAX/size)return NULL;void *p=s4_owned_malloc(n*size);if(p)memset(p,0,n*size);return p;
}
int s4_owner_contains(const S4Owner *o,const void *p) {
 uintptr_t q=(uintptr_t)p;
 for(S4Allocation *a=o->head;a;a=a->next) {
  uintptr_t b=(uintptr_t)(a+1);
  /* Rule cursors may temporarily point one byte before a tape. */
  if(q>=b-1 && q<=b+a->size)return 1;
 }
 return 0;
}
void s4_owner_destroy(S4Owner *o) {s4_forget_owner(o);while(o->head)s4_owned_free(o->head+1);}
/* The last owner is released only with no live engines under core serialization. */
void s4_shared_shutdown(void) {
 current=NULL;
 /* All borrowed image roots are discarded immediately afterward; no visitors. */
 while(shared.head)s4_owned_free(shared.head+1);
 if(getenv("S4_OWNER_TRACE")) fprintf(stderr,"SHUTDOWN owned_blocks=%llu owned_bytes=%llu\n",(unsigned long long)shared.blocks,(unsigned long long)shared.bytes);
}
void s4_owner_report(const S4Owner *o) {
 if(getenv("S4_OWNER_TRACE")) {fprintf(stderr,"OWNER private_blocks=%llu private_bytes=%llu shared_blocks=%llu shared_bytes=%llu\n",(unsigned long long)o->blocks,(unsigned long long)o->bytes,(unsigned long long)shared.blocks,(unsigned long long)shared.bytes);fflush(stderr);}
}
