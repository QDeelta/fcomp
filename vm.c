#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define STACK_INIT_CAP 16
#define SLAB_INIT_CAP 128
#define GC_INIT_THRESHOLD 64

typedef char bool_t;
#define FALSE 0
#define TRUE  1

typedef int64_t addr_t;

typedef void (*func_t)(void);

typedef struct {
    enum {NApp, NGlobal, NInd, NData, NInt} type;
    union {
        // NApp
        struct { addr_t left; addr_t right; };
        // NGlobal
        struct { int g_arity; func_t code; };
        // NInd
        struct { addr_t to; };
        // NData
        struct { int tag; int d_arity; addr_t* params; };
        // NInt
        struct { int intv; };
    };
    bool_t gc_reachable;
} node_t;

// free allocated memory in a node
void free_node(node_t n) {
    if (n.type == NData) {
        free(n.params);
    }
}

void global_gc(void);
void exit_program(void);

typedef struct {
    enum {Vacant, Occupied} slot_tag;
    union {
        int64_t next_vacant;
        node_t node;
    };
} slot_t;

slot_t *slab_arr = NULL;
int64_t slab_first_vacant_id;
int64_t slab_size;
int64_t slab_cap;
int64_t slab_occupied_count;
int64_t slab_gc_threshold;

int64_t stat_gc_count = 0;
int64_t stat_alloc_count = 0;
int64_t stat_free_count = 0;

void slab_init(void) {
    slab_arr = malloc(SLAB_INIT_CAP * sizeof(slot_t));
    slab_first_vacant_id = slab_size = 0;
    slab_cap = SLAB_INIT_CAP;
    slab_occupied_count = 0;
    slab_gc_threshold = SLAB_INIT_CAP;
}

addr_t slab_alloc(void) {
    addr_t new_addr;

    if (slab_size == slab_first_vacant_id) {
        if (slab_size == slab_cap) {
            slab_cap *= 2;
            slab_arr = realloc(slab_arr, slab_cap * sizeof(slot_t));
        }
        slab_size += 1;
        new_addr = slab_first_vacant_id;
        slab_first_vacant_id += 1;
    } else {
        slot_t v_slot = slab_arr[slab_first_vacant_id];
        new_addr = slab_first_vacant_id;
        slab_first_vacant_id = v_slot.next_vacant;
    }

    slab_arr[new_addr].slot_tag = Occupied;
    slab_occupied_count += 1;

    stat_alloc_count += 1;
    return new_addr;
}

void slab_free(addr_t a) {
    free_node(slab_arr[a].node);
    slab_arr[a].slot_tag = Vacant;
    slab_arr[a].next_vacant = slab_first_vacant_id;
    slab_first_vacant_id = a;
    slab_occupied_count -= 1;

    stat_free_count += 1;
}

void slab_destroy(void) {
    for (int64_t i = 0; i < slab_size; ++i) {
        if (slab_arr[i].slot_tag == Occupied) {
            free_node(slab_arr[i].node);
        }
    }
    free(slab_arr);
}

#define NODE(a) (&(slab_arr[a].node))

addr_t node_alloc_nogc(void) {
    addr_t a = slab_alloc();
    NODE(a)->gc_reachable = FALSE;
    return a;
}

addr_t node_alloc(void) {
    if (slab_occupied_count >= slab_gc_threshold) {
        global_gc();
        slab_gc_threshold = slab_occupied_count * 2;
    }
    return node_alloc_nogc();
}

void global_init(void);

addr_t *stack_arr = NULL;
int64_t stack_bp;
int64_t stack_sp;
int64_t stack_cap;

#define STACK_OFFSET(n) (stack_arr[stack_sp - 1 - (n)])
#define STACK_TOP (stack_arr[stack_sp - 1])

void stack_init(void) {
    stack_arr = malloc(STACK_INIT_CAP * sizeof(addr_t));
    stack_bp = stack_sp = 0;
    stack_cap = STACK_INIT_CAP;
}

void stack_free(void) {
    free(stack_arr);
    stack_arr = NULL;
    stack_bp = stack_sp = stack_cap = 0;
}

void exit_program(void) {
    fflush(stderr);
    fflush(stdout);
    stack_free();
    slab_destroy();
    exit(0);
}

void stack_push(addr_t a) {
    if (stack_sp == stack_cap) {
        stack_cap *= 2;
        stack_arr = realloc(stack_arr, stack_cap * sizeof(addr_t));
    }
    stack_arr[stack_sp++] = a;
}

void stack_traverse(void (*f)(addr_t)) {
    int64_t sp = stack_sp;
    int64_t bp = stack_bp;
    while (bp > 0) {
        for (int64_t i = sp - 1; i >= bp; --i) {
            f(stack_arr[i]);
        }
        sp = bp - 1;
        bp = (int64_t)stack_arr[sp];
    }
    for (int64_t i = sp - 1; i >= 0; --i) {
        f(stack_arr[i]);
    }
}

void gc_track(addr_t a) {
    if (NODE(a)->type != NGlobal && ! NODE(a)->gc_reachable) {
        NODE(a)->gc_reachable = TRUE;
        switch (NODE(a)->type) {
            case NApp:
                gc_track(NODE(a)->left);
                gc_track(NODE(a)->right);
                break;
            case NInd:
                gc_track(NODE(a)->to);
                break;
            case NData:
                for (int i = NODE(a)->d_arity - 1; i >= 0; --i) {
                    gc_track(NODE(a)->params[i]);
                }
                break;
            default: break;
        }
    }
}

void global_gc(void) {
    stack_traverse(gc_track);
    for (int64_t i = slab_size - 1; i >= 0; --i) {
        if (slab_arr[i].slot_tag == Occupied 
            && slab_arr[i].node.type != NGlobal) {
            if (! slab_arr[i].node.gc_reachable)
                slab_free(i);
            else
                slab_arr[i].node.gc_reachable = FALSE;
        }
    }
    stat_gc_count += 1;
}

void print_stat(void) {
    printf("Slab: Alloc count: %ld\n", stat_alloc_count);
    printf("Slab: Free count: %ld\n", stat_free_count);
    printf("Slab: Now size: %ld\n", slab_size);
    printf("Slab: Now capacity: %ld\n", slab_cap);
    printf("Slab: Now occupied: %ld\n", slab_occupied_count);
    printf("GC: Count: %ld\n", stat_gc_count);
    printf("GC: Now threshold: %ld\n", slab_gc_threshold);
}

void inst_pushg(addr_t p) {
    stack_push(p);
}

void inst_pushi(int val) {
    addr_t a = node_alloc();
    NODE(a)->type = NInt;
    NODE(a)->intv = val;
    stack_push(a);
}

void inst_push(int offset) {
    stack_push(STACK_OFFSET(offset));
}

void inst_mkapp(void) {
    addr_t a0 = STACK_OFFSET(0);
    addr_t a1 = STACK_OFFSET(1);
    addr_t a = node_alloc();
    NODE(a)->type = NApp;
    NODE(a)->left = a0; NODE(a)->right = a1;
    stack_sp -= 1;
    STACK_TOP = a;
}

void inst_update(int offset) {
    addr_t a = STACK_TOP;
    stack_sp -= 1;
    NODE(STACK_OFFSET(offset))->type = NInd;
    NODE(STACK_OFFSET(offset))->to = a;
}

void inst_pack(int tag, int arity) {
    addr_t a = node_alloc();
    NODE(a)->type = NData;
    NODE(a)->tag = tag; NODE(a)->d_arity = arity;
    NODE(a)->params = arity ? malloc(arity * sizeof(addr_t)) : NULL;
    for (int i = 0; i < arity; ++i) {
        NODE(a)->params[i] = STACK_OFFSET(i);
    }
    stack_sp -= arity;
    stack_push(a);
}

void inst_split(void) {
    addr_t a = STACK_TOP;
    stack_sp -= 1;
    for (int i = NODE(a)->d_arity - 1; i >= 0; --i) {
        stack_push(NODE(a)->params[i]);
    }
}

void inst_slide(int n) {
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
        int arity;
        switch (NODE(a)->type) {
            case NApp: stack_push(NODE(a)->left); break;
            case NInd: STACK_TOP = NODE(a)->to; break;
            case NGlobal:
                arity = NODE(a)->g_arity;
                if (stack_sp - stack_bp - 1 >= arity) {
                    for (int i = 0; i < arity; ++i) {
                        STACK_OFFSET(i) = NODE(STACK_OFFSET(i + 1))->right;
                    }
                    NODE(a)->code();
                } else {
                    stack_sp = stack_bp;
                    stack_bp = (int64_t)STACK_TOP;
                    STACK_TOP = stack_arr[stack_sp];
                    return;
                }
                break;
            default:
                stack_sp = stack_bp;
                stack_bp = (int64_t)STACK_TOP;
                STACK_TOP = a;
                return;
        }
    }
}

void inst_pop(int n) {
    stack_sp -= n;
}

void inst_alloc(int n) {
    for (int i = 0; i < n; ++i) {
        addr_t a = node_alloc();
        NODE(a)->type = NInd;
        NODE(a)->to = -1;
        stack_push(a);
    }
}

#define INST_INT_ARITH_BINOP(op) \
    do { \
    addr_t a0 = STACK_OFFSET(0); \
    addr_t a1 = STACK_OFFSET(1); \
    addr_t a = node_alloc(); \
    NODE(a)->type = NInt; \
    NODE(a)->intv = (NODE(a0)->intv) op (NODE(a1)->intv); \
    stack_sp -= 1; \
    STACK_TOP = a; \
    } while (0)

void inst_add(void) { INST_INT_ARITH_BINOP(+); }
void inst_sub(void) { INST_INT_ARITH_BINOP(-); }
void inst_mul(void) { INST_INT_ARITH_BINOP(*); }
void inst_div(void) { INST_INT_ARITH_BINOP(/); }
void inst_rem(void) { INST_INT_ARITH_BINOP(%); }

#define INST_INT_CMP_BINOP(op) \
    do { \
    addr_t a0 = STACK_OFFSET(0); \
    addr_t a1 = STACK_OFFSET(1); \
    addr_t a = node_alloc(); \
    NODE(a)->type = NData; \
    NODE(a)->tag = (NODE(a0)->intv) op (NODE(a1)->intv) ? 1 : 0; \
    NODE(a)->params = NULL; NODE(a)->d_arity = 0; \
    stack_sp -= 1; \
    STACK_TOP = a; \
    } while (0)

void inst_iseq(void) { INST_INT_CMP_BINOP(==); }
void inst_isgt(void) { INST_INT_CMP_BINOP(>); }
void inst_islt(void) { INST_INT_CMP_BINOP(<); }
void inst_isne(void) { INST_INT_CMP_BINOP(!=); }
void inst_isge(void) { INST_INT_CMP_BINOP(>=); }
void inst_isle(void) { INST_INT_CMP_BINOP(<=); }

void inst_not(void) {
    addr_t a0 = STACK_TOP;
    addr_t a = node_alloc();
    NODE(a)->type = NData;
    NODE(a)->tag = (! NODE(a0)->tag) ? 1 : 0;
    NODE(a)->params = NULL; NODE(a)->d_arity = 0;
    STACK_TOP = a;
}

addr_t entry_func_addr;

void print_head(const char* format) {
    inst_split();
    inst_eval();
    printf(format, NODE(STACK_TOP)->intv);
    inst_pop(1);
    inst_eval();
}

int main(void) {
    slab_init();
    stack_init();

    global_init();

    int input;
    scanf("%d", &input);
    
    inst_pushi(input);
    inst_pushg(entry_func_addr);
    inst_mkapp();
    inst_eval();

    if (NODE(STACK_TOP)->tag != 0) {
        print_head("%d");
    }
    while (NODE(STACK_TOP)->tag != 0) {
        print_head(",%d");
    }
    putchar('\n');

    print_stat();

    exit_program();
}
