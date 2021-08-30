#include <stdlib.h>
#include <stdio.h>

#define STACK_INIT_SIZE 16

typedef unsigned uint_t;

struct _node;
typedef struct _node *addr_t;

typedef void (*func_t)(void);

typedef struct _node {
    enum {NApp, NGlobal, NInd, NData, NInt} type;
    union {
        // NApp
        struct { addr_t left; addr_t right; };
        // NGlobal
        struct { uint_t g_arity; func_t code; };
        // NInd
        struct { addr_t to; };
        // NData
        struct { uint_t tag; uint_t d_arity; addr_t* params; };
        // NInt
        struct { int intv; };
    };
} node_t;

node_t *init_heap;
void heap_init();

void free_node(node_t *p) {
    if (p->type == NData) {
        free(p->params);
    }
    free(p);
}

addr_t *stack_arr = NULL;
size_t stack_bp;
size_t stack_sp;
size_t stack_cap;

#define STACK_OFFSET(n) (stack_arr[stack_sp - 1 - (n)])
#define STACK_TOP (stack_arr[stack_sp - 1])

void stack_init(void) {
    stack_arr = malloc(STACK_INIT_SIZE * sizeof(addr_t));
    stack_bp = stack_sp = 0;
    stack_cap = STACK_INIT_SIZE;
}

void stack_free(void) {
    free(stack_arr);
    stack_arr = NULL;
    stack_bp = stack_sp = stack_cap = 0;
}

void stack_push(addr_t a) {
    if (stack_sp == stack_cap) {
        stack_cap *= 2;
        stack_arr = realloc(stack_arr, stack_cap * sizeof(addr_t));
    }
    stack_arr[stack_sp++] = a;
}

void inst_pushg(size_t p) {
    stack_push(init_heap + p);
}

void inst_pushi(int val) {
    addr_t a = malloc(sizeof(node_t));
    a->type = NInt;
    a->intv = val;
    stack_push(a);
}

void inst_push(uint_t offset) {
    stack_push(STACK_OFFSET(offset));
}

void inst_mkapp(void) {
    addr_t a0 = STACK_OFFSET(0);
    addr_t a1 = STACK_OFFSET(1);
    addr_t a = malloc(sizeof(node_t));
    a->type = NApp;
    a->left = a0; a->right = a1;
    stack_sp -= 1;
    STACK_TOP = a;
}

void inst_add(void) {
    addr_t a0 = STACK_OFFSET(0);
    addr_t a1 = STACK_OFFSET(1);
    addr_t a = malloc(sizeof(node_t));
    a->type = NInt;
    a->intv = a0->intv + a1->intv;
    stack_sp -= 1;
    STACK_TOP = a;
}

void inst_sub(void) {
    addr_t a0 = STACK_OFFSET(0);
    addr_t a1 = STACK_OFFSET(1);
    addr_t a = malloc(sizeof(node_t));
    a->type = NInt;
    a->intv = a0->intv - a1->intv;
    stack_sp -= 1;
    STACK_TOP = a;
}

void inst_mul(void) {
    addr_t a0 = STACK_OFFSET(0);
    addr_t a1 = STACK_OFFSET(1);
    addr_t a = malloc(sizeof(node_t));
    a->type = NInt;
    a->intv = a0->intv * a1->intv;
    stack_sp -= 1;
    STACK_TOP = a;
}

void inst_update(uint_t offset) {
    addr_t a = STACK_TOP;
    stack_sp -= 1;
    STACK_OFFSET(offset)->type = NInd;
    STACK_OFFSET(offset)->to = a;
}

void inst_pack(uint_t tag, uint_t arity) {
    addr_t a = malloc(sizeof(node_t));
    a->type = NData;
    a->tag = tag; a->d_arity = arity;
    a->params = arity ? malloc(arity * sizeof(addr_t)) : NULL;
    for (int i = 0; i < arity; ++i) {
        a->params[i] = STACK_OFFSET(i);
    }
    stack_sp -= arity;
    stack_push(a);
}

void inst_split(void) {
    addr_t a = STACK_TOP;
    stack_sp -= 1;
    for (int i = a->d_arity - 1; i >= 0; --i) {
        stack_push(a->params[i]);
    }
}

void inst_slide(uint_t n) {
    STACK_OFFSET(n) = STACK_TOP;
    stack_sp -= n;
}

void inst_unwind(void);

void inst_eval(void) {
    addr_t a = STACK_TOP;
    STACK_TOP = (addr_t)stack_bp;
    stack_bp = stack_sp;
    stack_push(a);
    inst_unwind();
}

void inst_unwind(void) {
    while (1) {
        addr_t a = STACK_TOP;
        uint_t arity;
        switch (a->type) {
            case NApp: stack_push(a->left); break;
            case NInd: STACK_TOP = a->to; break;
            case NGlobal:
                arity = a->g_arity;
                if (stack_sp - stack_bp - 1 >= arity) {
                    for (int i = 0; i < arity; ++i) {
                        STACK_OFFSET(i) = STACK_OFFSET(i+1)->right;
                    }
                    a->code();
                } else {
                    stack_sp = stack_bp;
                    stack_bp = (size_t)STACK_TOP;
                    STACK_TOP = stack_arr[stack_sp];
                    return;
                }
                break;
            default:
                stack_sp = stack_bp;
                stack_bp = (size_t)STACK_TOP;
                STACK_TOP = a;
                return;
        }
    }
}

void inst_pop(uint_t n) {
    stack_sp -= n;
}

void inst_alloc(uint_t n) {
    for (int i = 0; i < n; ++i) {
        addr_t a = malloc(sizeof(node_t));
        a->type = NInd;
        a->to = NULL;
        stack_push(a);
    }
}

size_t main_func_id;

int main(void) {
    heap_init();
    stack_init();
    inst_pushg(main_func_id);
    inst_eval();
    if (STACK_TOP->type == NInt) {
        printf("%d\n", STACK_TOP->intv);
    }
    return 0;
}
