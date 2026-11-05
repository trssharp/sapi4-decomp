/* Preview02 explicit allocation domains, serialized by core g_lock. */
#pragma once
#include <stddef.h>
typedef struct S4Allocation S4Allocation;
typedef struct S4Owner { S4Allocation *head; size_t bytes,blocks; } S4Owner;
S4Owner *s4_owner_use(S4Owner *);
void *s4_owned_malloc(size_t);
void *s4_shared_malloc(size_t);
void s4_owned_free(void *);
void *s4_owned_realloc(void *,size_t);
void *s4_owned_calloc(size_t,size_t);
int s4_owner_contains(const S4Owner *,const void *);
void s4_owner_destroy(S4Owner *);
void s4_owner_report(const S4Owner *);
void s4_forget_owner(const S4Owner *);
