// make2compdb
// Generates a Clang's JSON Compilation Database files (`compiler_commands.json`) from make build systems.
// This json file can be usefull for multiple tools including the clangd LSP.
//
// USAGE
//
//      $ make -Bwn | make2compdb.exe > compiler_commands.json
//      $ cat compiler_commands.json
//      [
//        {
//          "directory": "C:\\my_project",
//          "file": "main.c",
//          "output": "main",
//          "arguments": [
//            "gcc",
//            "-o",
//            "main",
//            "main.c"
//          ]
//        }
//
//      Notice that `make` is invoked with `-BWn`
//        -B (--always-make):       force all build commands to print regardless of current build state.
//        -w (--print-directory):   print directory navigation, so we can understand where we are.
//        -n (--dry-run):           dont' actually build. Just print what you would do.
//
//      Learn more about make2compdb using the `--help` CLI flag.
//
//
// BUILD
//      
//      Windows: $ cc -std=c23 -nostartfiles -O2 -s -o make2compdb.exe make2compdb.c -lmemory
//        Linux: $ cc -std=c23 -O2 -o make2compdb make2compdb.c
//
// BUILD TESTS
//
//         Both: $ cc -std=c23 -O2 -DTEST -o test make2compdb.c
//
// BUILD FUZZ
//
//        Linux: Go see the code. There's more documentation there.
//
//
// LIMITATIONS
//
//      On windows, we do not support the CL (MSVC) compiler.
//
//      On linux, your makefile stdout depends on your locale (sadly).
//      Even after working on this problem for a while, I could not find a clean
//      way to parse them for all languages. Therefore, make2compdb expect you to
//      run `make` using the `C.UTF-8` locale or equivalents.
//
//
// LICENCE
//
//      This is free and unencumbered software released into the public domain.
//      For more information, please refer to <https://unlicense.org>
//      Author: g-berthiaume, 2026
//

#include <stddef.h>
#include <stdint.h>

#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 202000L
    #error "This code expects to be compiled by a C23 toolchain"
#endif

#define VERSION "2026-05-20"

typedef uint8_t        u8;
typedef uint16_t       u16;
typedef uint32_t       u32;
typedef uint64_t       u64;
typedef int8_t         i8;
typedef int16_t        i16;
typedef int32_t        i32;
typedef int64_t        i64;
typedef ptrdiff_t      isize;
typedef size_t         usize;
typedef unsigned short c16;
typedef char           byte;
typedef ptrdiff_t      iptr;
typedef uintptr_t      uptr;
typedef int32_t        b32;

#define size_of(A)  ((isize)(sizeof(A)))
#define count_of(A) ((size_of(A) / size_of(*(A))))
#define type_of(T)  typeof(T)
#define align_of(T) _Alignof(type_of(T))
#define min(A, B)   (((A) < (B)) ? (A) : (B))

// Simple assertion - Pretty good if you're using a debugger.
//  `__builtin_trap` will causes the program to terminate abnormally.
#define assert(C)                                                                                                      \
    while (!(C)) __builtin_trap()

#ifndef unreachable
    #define unreachable() __builtin_unreachable()
#endif

static inline isize clamp_isize(isize i, isize min, isize max)
{
    if (i > max) {
        i = max;
    }
    else if (i < min) {
        i = min;
    }
    return i;
}

static inline usize to_usize(isize x)
{
    assert(x >= 0);
    return (usize)x;
}

static inline int32_t to_i32(isize x)
{
    x = clamp_isize(x, INT32_MIN, INT32_MAX);
    return (i32)x;
}

static inline byte *memory_set(byte *mem, byte value, isize size)
{
    if (mem == NULL) return mem;
    for (isize i = 0; i < size; i++) {
        mem[i] = value;
    }
    return mem;
}

static inline b32 is_whitespace(u8 c)
{
    switch (c) {
    case ' ':  // FALLTHROUGH
    case '\n': // FALLTHROUGH
    case '\t': // FALLTHROUGH
    case '\r': return 1;
    default: return 0;
    }
}

static inline b32 is_numeric(u8 c)
{
    switch (c) {
    case '0': // FALLTHROUGH
    case '1': // FALLTHROUGH
    case '2': // FALLTHROUGH
    case '3': // FALLTHROUGH
    case '4': // FALLTHROUGH
    case '5': // FALLTHROUGH
    case '6': // FALLTHROUGH
    case '7': // FALLTHROUGH
    case '8': // FALLTHROUGH
    case '9': return 1;
    default: return 0;
    }
}

// :: Arena
// An `Arena` is a memory allocator that allocates objects linearly.
//
//     arena.start            arena.cursor
//     v                      v
//    |███████████████████████-------------|
//    |<--allocated memory-->|             ^
//                                         arena.end
typedef struct {
    byte *start;
    byte *cursor;
    byte *end;
    b32   is_locked; //< For the acquire/release api
} Arena;

Arena arena_init(isize capacity, byte memory[static capacity])
{
    Arena a     = {0};
    a.start     = memory;
    a.cursor    = memory;
    a.end       = a.cursor + capacity;
    a.is_locked = 0;
    return a;
}

// Modified from Christopher Wellons pck-config.c [1]
// [1] https://github.com/skeeto/w64devkit/blob/master/src/pkg-config.c
static byte *arena_alloc(Arena *a, isize count, isize size, isize align)
{
    assert(NULL != a);
    assert(!a->is_locked);

    isize pad = (isize) - (uintptr_t)a->cursor & (align - 1);
    assert(count < (a->end - a->cursor - pad) / size); // Make sure not OOM.

    byte *r    = a->cursor + pad;
    a->cursor += pad + count * size;
    return memory_set(r, 0, count * size);
}

#define ALLOC(ARENA_PTR, COUNT, TYPE) (type_of(TYPE) *)arena_alloc((ARENA_PTR), (COUNT), size_of(TYPE), align_of(TYPE))

// Acquire the arena, so you can use it as a gigantic linear buffer.
// Typically paired with `arena_release`.
byte *arena_acquire(Arena *a, isize size, isize align, isize *available)
{
    assert(NULL != a);
    assert(!a->is_locked);

    isize pad = (isize) - (uintptr_t)a->cursor & (align - 1);
    a->cursor = a->cursor + pad;

    if (available) {
        *available = (a->end - a->cursor) / size;
    }
    a->is_locked = 1;
    return a->cursor;
}

void arena_release(Arena *a, isize commit_size)
{
    assert(NULL != a);
    assert(a->is_locked);

    if (commit_size > 0) {
        a->cursor = a->cursor + commit_size + 1; // +1 because the cursor points ahead
    }
    a->is_locked = 0;
}

void arena_reset_to(Arena *a, byte *here)
{
    assert(NULL != a);
    assert(a->start <= here && here <= a->cursor);

    a->cursor = here;
}

// :: Str
// A `Str` is a slice of an array of characters.
// It's the same concept as C++ std::string_view, Rust slice (&[T]) and Zig slice ([*]T).
typedef struct Str {
    isize len;
    u8   *ptr;
} Str;

// Transform a C string literal into a `Str`
#define SL(STR_LITERAL)                                                                                                \
    (Str) { .ptr = (u8 *)STR_LITERAL, .len = (size_of(STR_LITERAL) - 1) }

#define STATIC_SL(STR_LITERAL) {.ptr = (u8 *)STR_LITERAL, .len = (size_of(STR_LITERAL) - 1)}

Str str_from_cstr(char *cstr)
{
    Str s = {0};
    s.ptr = (u8 *)cstr;
    if (cstr) {
        while (*cstr != '\0') {
            s.len++;
            cstr++;
        }
    }
    return s;
}

Str str_copy(Arena *a, Str to_copy)
{
    if (to_copy.len <= 0) return SL("");
    assert(to_copy.ptr);

    u8 *ptr  = ALLOC(a, to_copy.len, u8);
    Str dest = {.ptr = ptr, .len = to_copy.len};

    for (isize i = 0; i < to_copy.len; i++) {
        dest.ptr[i] = to_copy.ptr[i];
    }
    return dest;
}

b32 str_equal(Str const a, Str const b)
{
    if (a.len != b.len) {
        return 0;
    }
    else {
        for (isize i = 0; i < a.len; i++) {
            if (a.ptr[i] != b.ptr[i]) {
                return 0;
            }
        }
        return 1;
    }
}

b32 str_stars_with(Str const s, Str const start)
{
    if (!s.ptr) {
        return 0;
    }
    else if (s.len < start.len) {
        return 0;
    }
    else {
        for (isize i = 0; i < start.len; i++) {
            if (s.ptr[i] != start.ptr[i]) {
                return 0;
            }
        }
        return 1;
    }
}

b32 str_ends_with(Str const s, Str const end)
{
    if (!s.ptr) {
        return 0;
    }
    else if (s.len < end.len) {
        return 0;
    }
    else {
        for (isize i = 0; i < end.len; i++) {
            if (s.ptr[(s.len - 1) - i] != end.ptr[(end.len - 1) - i]) {
                return 0;
            }
        }
        return 1;
    }
}

Str str_drop_head(Str s, isize offset_from_the_start)
{
    if (s.len <= 0) return s;
    assert(s.ptr);

    offset_from_the_start = clamp_isize(offset_from_the_start, 0, s.len);
    return (Str){
        .ptr = &s.ptr[offset_from_the_start],
        .len = s.len - offset_from_the_start,
    };
}

Str str_take_head(Str s, isize offset_from_the_start)
{
    if (s.len <= 0) return s;
    assert(s.ptr);

    offset_from_the_start = clamp_isize(offset_from_the_start, 0, s.len);
    return (Str){
        .ptr = s.ptr,
        .len = offset_from_the_start,
    };
}

Str str_drop_tail(Str s, isize offset_from_the_end)
{
    if (s.len <= 0) return s;
    assert(s.ptr);

    offset_from_the_end = clamp_isize(offset_from_the_end, 0, s.len - 1);
    return (Str){
        .ptr = s.ptr,
        .len = s.len - offset_from_the_end,
    };
}

Str str_take_tail(Str s, isize offset_from_the_end)
{
    if (s.len <= 0) return s;
    assert(s.ptr);

    offset_from_the_end = clamp_isize(offset_from_the_end, 0, s.len - 1);
    return (Str){
        .ptr = &s.ptr[offset_from_the_end],
        .len = offset_from_the_end,
    };
}

Str str_trim_prefix(Str s, Str prefix)
{
    if (str_stars_with(s, prefix)) {
        return str_drop_head(s, prefix.len);
    }
    return s;
}

Str str_trim_postfix(Str s, Str postfix)
{
    if (str_ends_with(s, postfix)) {
        return str_drop_tail(s, postfix.len);
    }
    return s;
}

Str str_trim_prefix_if(Str s, b32 (*predicate)(u8 c))
{
    isize i = 0;
    for (; i < s.len; ++i) {
        if (!predicate(s.ptr[i])) break;
    }
    return str_drop_head(s, i);
}

Str str_trim_postfix_if(Str s, b32 (*predicate)(u8 c))
{
    isize i = 0;
    for (; i < s.len; ++i) {
        isize index = (s.len - 1) - i;
        if (!predicate(s.ptr[index])) break;
    }
    return str_drop_tail(s, i);
}

Str str_trim(Str s, Str pattern)
{
    s = str_trim_prefix(s, pattern);
    s = str_trim_postfix(s, pattern);
    return s;
}

Str str_trim_if(Str s, b32 (*predicate)(u8 c))
{
    s = str_trim_prefix_if(s, predicate);
    s = str_trim_postfix_if(s, predicate);
    return s;
}

// When found, return the index of the `delim`. Otherwise, return a negative number.
isize str_find(Str s, Str delim)
{
    if (!s.ptr || !delim.ptr) {
        return -1; // Invalid string can't be found
    }
    else if (s.len < delim.len) {
        return -1; // Impossible to find
    }
    else if (delim.len == 1) {
        u8 const find_me = delim.ptr[0];
        for (isize i = 0; i < s.len; i++) {
            if (s.ptr[i] == find_me) {
                return i;
            }
        }
    }
    else {
        isize len_to_scan = 1 + s.len - delim.len;
        for (isize i = 0; i < len_to_scan; i++) {
            Str search_from_here = str_drop_head(s, i);
            if (str_stars_with(search_from_here, delim)) {
                return i;
            }
        }
    }
    return -1; // Not found
}

isize str_find_any_char(Str s, Str char_list)
{
    if (!s.ptr || !char_list.ptr) {
        return -1; // Invalid string can't be found
    }
    else if (s.len < 1) {
        return -1; // Impossible to find
    }
    else {
        for (isize i = 0; i < s.len; i++) {
            for (isize char_index = 0; char_index < char_list.len; ++char_index) {
                u8 find_me = char_list.ptr[char_index];
                if (s.ptr[i] == find_me) {
                    return i;
                }
            }
        }
        return -1;
    }
}

b32 str_contains(Str const s, Str const this)
{
    isize index = str_find(s, this);
    return index >= 0;
}

b32 str_contains_any_char(Str const s, Str const any)
{
    for (isize i = 0; i < s.len; ++i) {
        for (isize any_i = 0; any_i < any.len; ++any_i) {
            if (s.ptr[i] == any.ptr[any_i]) return 1;
        }
    }
    return 0;
}

u8 str_peak(Str const *const s)
{
    u8 c = '\0';
    if (s && (0 < s->len)) {
        c = s->ptr[0];
    }
    return c;
}

u8 str_pop(Str *const s)
{
    u8 c = '\0';
    if (s && (0 < s->len)) {
        c = s->ptr[0];
        s->ptr++;
        s->len--;
    }
    return c;
}

// :: StrPair
typedef struct {
    // If the `delim` was seen.
    b32 ok;
    // First part of the cut.
    // If the `delim` was not seen, contains everything.
    Str head;
    // Last part of the cut.
    // If the `delim` was not seen, contains nothing.
    Str tail;
} StrPair;

StrPair str_cut(Str s, Str delim)
{
    isize delim_start_index = str_find(s, delim);
    if (delim_start_index < 0) { // Not found
        return (StrPair){
            .ok   = 0,
            .head = s,
            .tail = (Str){0},
        };
    }
    else {
        return (StrPair){
            .ok   = 1,
            .head = str_take_head(s, delim_start_index),
            .tail = str_drop_head(s, delim_start_index + delim.len),
        };
    }
}

// Taken from Christopher Wellons pck-config.c [1]
// [1] https://github.com/skeeto/w64devkit/blob/master/src/pkg-config.c
static u32 str_hash(Str s)
{
    u32 h = 0x811c9dc5;
    for (isize i = 0; i < s.len; i++) {
        h ^= s.ptr[i];
        h *= 0x01000193;
    }
    return h;
}

// :: StrList
// A doubly linked list of `Str`.
// The `StrList` does not own the strings, only the slices to them.
typedef struct StrListNode StrListNode;
struct StrListNode {
    Str          str;
    StrListNode *next;
    StrListNode *prev;
};

typedef struct {
    StrListNode *front;
    StrListNode *back;
    isize        count;
} StrList;

b32 strlist_is_emtpy(StrList const sl) { return sl.front == NULL; }

Str strlist_node_remove(StrListNode *node)
{
    assert(node);

    Str s      = node->str;
    node->str  = (Str){0};
    node->prev = NULL;
    node->next = NULL;
    return s;
}

void strlist_push_front(StrList *sl, Arena *a, Str s)
{
    assert(sl);

    StrListNode *new_node = ALLOC(a, 1, StrListNode);
    new_node->str         = s;

    if (strlist_is_emtpy(*sl)) {
        new_node->prev = NULL;
        new_node->next = NULL;
        sl->front      = new_node;
        sl->back       = new_node;
        sl->count      = 1;
    }
    else {
        StrListNode *prev_front  = sl->front;
        sl->front                = new_node;
        new_node->prev           = NULL;
        new_node->next           = prev_front;
        prev_front->prev         = new_node;
        sl->count               += 1;
    }
}

void strlist_push_back(StrList *sl, Arena *a, Str s)
{
    assert(sl);

    StrListNode *new_node = ALLOC(a, 1, *new_node);
    new_node->str         = s;
    if (strlist_is_emtpy(*sl)) {
        sl->front      = new_node;
        sl->back       = new_node;
        new_node->next = NULL;
        new_node->prev = NULL;
        sl->count      = 1;
    }
    else {
        StrListNode *prev_back  = sl->back;
        sl->back                = new_node;
        new_node->prev          = prev_back;
        new_node->next          = NULL;
        prev_back->next         = new_node;
        sl->count              += 1;
    }
}

// This function traverse the list (O(n)).
Str strlist_peak_at(StrList *sl, isize node_index)
{
    assert(sl);
    assert(0 <= node_index);

    if (strlist_is_emtpy(*sl)) {
        return (Str){0};
    }

    StrListNode *node = sl->front;
    for (isize i = 0; i < node_index; ++i) {
        node = node->next;
        if (node == NULL) { // node_index is too big
            return (Str){0};
        }
    }
    return node->str;
}

Str strlist_peak_front(StrList *sl)
{
    assert(sl);

    Str front = {0};
    if (strlist_is_emtpy(*sl)) {
        return front;
    }
    else {
        StrListNode *front_node = sl->front;
        front                   = front_node->str;
    }
    return front;
}

Str strlist_peak_back(StrList *sl)
{
    assert(sl);

    Str back = {0};
    if (strlist_is_emtpy(*sl)) {
        return back;
    }
    else {
        StrListNode *back_node = sl->back;
        back                   = back_node->str;
    }
    return back;
}

Str strlist_pop_front(StrList *sl)
{
    assert(sl);

    Str front = {0};
    if (strlist_is_emtpy(*sl)) {
        return front;
    }
    else if (sl->front == sl->back) {
        // Only 1 node in the list
        StrListNode *only_node  = sl->front;
        sl->front               = NULL;
        sl->back                = NULL;
        front                   = strlist_node_remove(only_node);
        sl->count              -= 1;
        assert(sl->count == 0);
    }
    else {
        StrListNode *front_node  = sl->front;
        sl->front                = front_node->next;
        sl->front->prev          = NULL;
        front                    = strlist_node_remove(front_node);
        sl->count               -= 1;
    }
    return front;
}

Str strlist_pop_back(StrList *sl)
{
    assert(sl);

    Str back = {0};
    if (strlist_is_emtpy(*sl)) {
        return back;
    }
    else if (sl->front == sl->back) {
        // Only 1 node in the list
        StrListNode *only_node  = sl->back;
        sl->front               = NULL;
        sl->back                = NULL;
        back                    = strlist_node_remove(only_node);
        sl->count              -= 1;
        assert(sl->count == 0);
    }
    else {
        StrListNode *back_node  = sl->back;
        sl->back                = back_node->prev;
        sl->back->next          = NULL;
        back                    = strlist_node_remove(back_node);
        sl->count              -= 1;
    }
    return back;
}

StrList strlist_copy(StrList const sl, Arena *a)
{
    StrList copy = {0};
    if (strlist_is_emtpy(sl)) {
        return copy;
    }

    for (StrListNode *node = sl.front; node != NULL; node = node->next) {
        strlist_push_back(&copy, a, node->str);
    }
    return copy;
}

b32 strlist_equal(StrList a, StrList b)
{
    if (a.count != b.count) {
        return 0;
    }
    else {
        for (isize i = 0; i < a.count; ++i) {
            Str astr = strlist_pop_front(&a);
            Str bstr = strlist_pop_front(&b);
            if (!str_equal(astr, bstr)) {
                return 0;
            }
        }
        return 1;
    }
}

StrList strlist_from_cstrs(Arena *a, isize count, char *cstrs[static count])
{
    assert(cstrs);

    StrList sl = {0};
    for (isize i = 0; i < count; i++) {
        Str s = str_from_cstr(cstrs[i]);
        strlist_push_back(&sl, a, s);
    }
    return sl;
}

// ::StrLookup
// Small MSI hash table for comparing strings efficiently.
#define STR_LOOKUP_EXP 8
typedef struct {
    Str   table[1 << STR_LOOKUP_EXP];
    isize count;
} StrHashTable;

isize strht_get_index(u32 hash, isize index)
{
    u32 mask = ((u32)1 << STR_LOOKUP_EXP) - 1;
    u32 step = (hash >> (32 - STR_LOOKUP_EXP)) | 1;
    index    = (index + step) & mask;
    return index;
}

b32 strht_lookup(StrHashTable *shs, Str s)
{
    assert(shs);

    isize index = 0;
    for (u32 hash = str_hash(s);;) {
        index = strht_get_index(hash, index);
        if (shs->table[index].ptr == NULL) {
            return 0; // Not found
        }
        else if (str_equal(shs->table[index], s)) {
            return 1; // Found
        }
    }
}

b32 strht_insert(StrHashTable *shs, Str s)
{
    assert(shs);

    isize index = 0;
    for (u32 hash = str_hash(s);;) {
        index = strht_get_index(hash, index);
        if (shs->table[index].ptr == NULL) {
            // slot is empty
            assert(shs->count < count_of(shs->table)); // OOM

            shs->table[index]  = s;
            shs->count        += 1;
            return 1; // inserted
        }
        else if (str_equal(shs->table[index], s)) {
            return 0; // not inserted
        }
    }
}

// :: MemBuf
// A memory buffer will lock an arena to create a big append-only buffer.
// Ideal for building strings of unknown size.
typedef struct {
    Arena *arena;
    isize  cap;
    isize  len;
    u8    *ptr;
} MemBuf;

MemBuf membuf_init(Arena *a)
{
    assert(a);
    assert(!a->is_locked);

    // Lock the arena
    a->is_locked = 1;

    MemBuf buf = {0};
    buf.arena  = a;
    buf.cap    = a->end - a->cursor;
    buf.ptr    = (u8 *)a->cursor;
    buf.len    = 0;
    return buf;
}

void membuf_add_u8(MemBuf *buf, u8 value)
{
    assert(buf);
    assert(buf->cap > buf->len); // Arena is OOM
    buf->ptr[buf->len] = value;
    buf->len++;
}

void membuf_add_str(MemBuf *buf, Str str)
{
    assert(buf);
    assert(str.ptr);
    for (isize i = 0; i < str.len; ++i) {
        membuf_add_u8(buf, str.ptr[i]);
    }
}

Str membuf_slice(MemBuf *buf)
{
    assert(buf);
    Str slice = {0};
    slice.ptr = buf->ptr;
    slice.len = buf->len;

    buf->ptr += slice.len;
    buf->len  = 0;
    return slice;
}

// Unlock the arena
Str membuf_finish(MemBuf *buf)
{
    assert(buf);
    Str leftover = membuf_slice(buf);

    // Commit this memory to the arena
    buf->arena->cursor    = (byte *)buf->ptr;
    buf->arena->is_locked = 0;
    return leftover;
}

// :: DirectoryStack
// A directory stack behaves like a stack of directory
// The directory stack's arena owns both the `Str` and the string characters.
// They will be arranged in the arena in the following manner:
//
//         arena.start                     arena.cursor        arena.end
//         v                               v                   v
// arena: |░░░░░░░░░░░░░░░███░░░░░░░░░░░███--------------------|
//         |<---chars--->| ^ |<-chars->| ^
//                         StrNode       StrNode
//
// Therefore, when we pop a `Str`, we can also reset the arena to the start of chars.
typedef struct {
    Arena   arena;
    StrList stack;
} DirectoryStack;

void dirstack_push(DirectoryStack *ds, Str directory)
{
    Str directory_copy = str_copy(&ds->arena, directory);
    strlist_push_back(&ds->stack, &ds->arena, directory_copy);
}

Str dirstack_peak(DirectoryStack *ds, Arena *perm)
{
    Str directory      = strlist_peak_back(&ds->stack);
    Str directory_copy = str_copy(perm, directory);
    return directory_copy;
}

Str dirstack_pop(DirectoryStack *ds, Arena *perm)
{
    Str directory = strlist_pop_back(&ds->stack);
    if (directory.len > 0) {
        Str directory_copy = str_copy(perm, directory);
        arena_reset_to(&ds->arena, (byte *)directory.ptr);
        return directory_copy;
    }
    return directory;
}

// :: OS
// Operating system console buffered interface.
// The vtable will be populated by the `main()` of each platform.
typedef struct {
    struct {
        isize len;
        u8    mem[1 << 11];
    } buf;
    // Saw a end-of-line
    b32 eol;
    // The current tabulation count
    i8 tab;
    // User provided Vtable
    void *ctx;
    void (*flush)(void *ctx, isize len, u8 ptr[static len]);
} OsConsoleInterface;

void print_flush(OsConsoleInterface *console)
{
    assert(console);
    assert(console->buf.len <= count_of(console->buf.mem));

    console->flush(console->ctx, console->buf.len, console->buf.mem);
    console->buf.len = 0;
}

void print_tab(OsConsoleInterface *console)
{
    assert(console);

    if (console->tab < 0) {
        console->tab = 0;
    }

#define SPACE " "
#define TAB   SPACE SPACE SPACE SPACE

    Str   tab      = SL(TAB);
    isize required = tab.len * console->tab;
    assert(count_of(console->buf.mem) >= required); //< Buffer should be big enough to have all the tabs.

    isize available = count_of(console->buf.mem) - console->buf.len;
    if (available < required) {
        print_flush(console);
    }

    for (isize _ = 0; _ < console->tab; ++_) {
        for (isize i = 0; i < tab.len; ++i) {
            console->buf.mem[console->buf.len]  = tab.ptr[i];
            console->buf.len                   += 1;
        }
    }

#undef SPACE
#undef TAB
}

void print_u8(OsConsoleInterface *console, u8 c)
{
    assert(console);

    if (console->eol) {
        print_tab(console);
        console->eol = 0;
    }

    isize available = count_of(console->buf.mem) - console->buf.len;
    if (available <= 0) {
        print_flush(console);
    }

    console->buf.mem[console->buf.len]  = c;
    console->buf.len                   += 1;
    if (c == '\n') {
        console->eol = 1;
    }
}

void print_str(OsConsoleInterface *console, Str s)
{
    assert(console);

    while (s.len > 0) {
        isize available = count_of(console->buf.mem) - console->buf.len;
        if (available <= 0) {
            print_flush(console);
            continue;
        }

        isize write_len = min(available, s.len);
        for (isize i = 0; i < write_len; ++i) {
            print_u8(console, s.ptr[i]);
        }
        s = str_drop_head(s, write_len);
    }
}

void println_str(OsConsoleInterface *console, Str s)
{
    print_str(console, s);
    print_u8(console, '\n');
}

void print_str_escaped_string(OsConsoleInterface *console, Str s)
{
    assert(console);

    print_u8(console, '\"');
    for (isize i = 0; i < s.len; i++) {
        switch (s.ptr[i]) {
        case '\n': print_str(console, SL("\\n")); break;
        case '\r': print_str(console, SL("\\r")); break;
        case '\t': print_str(console, SL("\\t")); break;
        case '\v': print_str(console, SL("\\v")); break;
        case '\b': print_str(console, SL("\\b")); break;
        case '\f': print_str(console, SL("\\f")); break;
        case '\a': print_str(console, SL("\\a")); break;
        case '\\': print_str(console, SL("\\\\")); break;
        case '\"': print_str(console, SL("\\\"")); break;
        case '\'': print_str(console, SL("\\\'")); break;
        default: print_u8(console, s.ptr[i]); break;
        }
    }
    print_u8(console, '\"');
}

void println_str_escaped_string(OsConsoleInterface *console, Str s)
{
    print_str_escaped_string(console, s);
    print_u8(console, '\n');
}

void print_b32(OsConsoleInterface *console, b32 b)
{
    print_str(console, b ? SL("true") : SL("false")); //
}

void println_b32(OsConsoleInterface *console, b32 b)
{
    print_b32(console, b);
    print_u8(console, '\n');
}

void print_number(OsConsoleInterface *console, isize number)
{
    assert(console);

    u8 buffer[sizeof("18446744073709551616")] = {0}; // (1 << 64)

    b32   negative = number < 0;
    usize uval     = negative ? ((usize)(0 - number)) : ((usize)number);

    int len = 0;
    do {
        buffer[len++]  = (u8)('0' + (uval % 10));
        uval          /= 10;
    } while (uval);

    if (negative) print_u8(console, '-');
    for (isize i = len - 1; i >= 0; --i) {
        print_u8(console, buffer[i]);
    }
}

void println_number(OsConsoleInterface *console, isize number)
{
    print_number(console, number);
    print_u8(console, '\n');
}

void print_strlist(OsConsoleInterface *console, StrList sl)
{
    assert(console);

    print_str(console, SL("["));
    for (StrListNode *node = sl.front; node != NULL; node = node->next) {
        if (node != sl.front) {
            print_str(console, SL(", "));
        }
        print_str_escaped_string(console, node->str);
    }
    print_str(console, SL("]"));
}

void println_strlist(OsConsoleInterface *console, StrList sl)
{
    print_strlist(console, sl);
    print_u8(console, '\n');
}

// :: ParsingMode
// Each line has a parsing mode which will dictate how we tokenize the chars.
typedef enum {
    PARSING_MODE_SHELL = 0,
    PARSING_MODE_MAKE_ENTER_DIR,
    PARSING_MODE_MAKE_LEAVE_DIR,
} ParsingMode;

ParsingMode identify_parsing_mode(Str make_stdout)
{
    // Our strategy is to try to identify the following patterns:
    // 1. make: Entering directory 'C:/dev/gb/make2compdb/impl_c'
    // 2. make: Leaving directory 'C:/dev/gb/make2compdb/impl_c'
    //
    // If we didn't find those element, we assume this is a shell command.
    StrPair cut = {0};
    cut         = str_cut(make_stdout, SL(" "));
    Str first   = cut.head;
    cut         = str_cut(cut.tail, SL(" "));
    Str second  = cut.head;
    cut         = str_cut(cut.tail, SL(" "));
    Str third   = cut.head;

    (void)first; //< We cannot rely on "make" as this program name changes.

    if (str_equal(third, SL("directory"))) {
        if (str_equal(second, SL("Entering"))) {
            return PARSING_MODE_MAKE_ENTER_DIR;
        }
        else if (str_equal(second, SL("Leaving"))) {
            return PARSING_MODE_MAKE_LEAVE_DIR;
        }
    }
    // Otherwise we default to shell parsing
    return PARSING_MODE_SHELL;
}

Str parse_directory_from_make_dir(Str *make_stdout)
{
    Str dir = SL("");

    //       We want to go here |
    //                          v
    // make: Entering directory 'C:/dev/gb/make2compdb/impl_c'
    //
    Str input = *make_stdout;
    input     = str_cut(input, SL(" ")).tail; //< skip "make:"
    input     = str_cut(input, SL(" ")).tail; //< skip "Entering"

    if (str_stars_with(input, SL("directory"))) {
        input = str_cut(input, SL(" ")).tail; //< skip "directory"

        input     = str_trim_prefix_if(input, is_whitespace);
        Str delim = str_take_head(input, 1);

        // For some reason, certain version of make use different string delimiters.
        // Even stranger, sometimes the delimeter pairs don't match!
        Str valid_delims = SL("\'\"`");
        if (str_contains_any_char(delim, valid_delims)) {
            input            = str_drop_head(input, 1);
            isize next_delim = str_find_any_char(input, valid_delims);

            dir = str_take_head(input, next_delim);
            dir = str_trim_if(dir, is_whitespace);
        }
    }

    *make_stdout = str_cut(input, SL("\n")).tail;
    return dir;
}

// :: Shell
// The Make program is basically a shell invocation program.
// Those invocations can be hard to parse, so we have the following utilities to help.
Str shell_next_token(Arena *perm, Str *const make_stdout, b32 *const end_of_invocation, b32 *const end_of_line)
{
    *end_of_invocation          = 0;
    *end_of_line                = 0;
    b32 in_quote                = 0; // '...'
    b32 in_double_quote         = 0; // "..."
    b32 in_backtick             = 0; // `
    b32 in_subshell_expressions = 0; // $(...)
    b32 in_escape               = 0; // backslash + ...

    MemBuf buf = membuf_init(perm);

    while (0 < make_stdout->len) {
        u8 c = str_pop(make_stdout);

        if (in_subshell_expressions) {
            if (c == ')') {
                in_subshell_expressions = 0;
            }
            continue; //< Eat it: We ignore subshell expressions
        }
        else if (in_escape) {
            in_escape = 0;
            if (c == '\n') {
                continue; //< Eat it: Line breaks (\\n) do not count as EOL
            }
            else if (c == '\r') {
                u8 lookahead = str_peak(make_stdout);
                if (lookahead == '\n') {
                    str_pop(make_stdout);
                    continue; //< Eat it: Line breaks (\\r\n) do not count as EOL
                }
            }
            else {
                // Re-add the backslash
                membuf_add_u8(&buf, '\\');
            }
        }
        else if (in_quote) {
            if (c == '\'') {
                in_quote = 0;
            }
        }
        else if (in_double_quote) {
            if (c == '\"') {
                in_double_quote = 0;
            }
        }
        else if (in_backtick) {
            if (c == '`') {
                in_backtick = 0;
            }
            continue; //< Eat it: We ignore backtick
        }
        else { // Not in a special mode

            if (is_whitespace(c)) {
                if (c == '\n') {
                    *end_of_invocation = 1;
                    *end_of_line       = 1;
                    break; // eat and exit: we just saw the EOL.
                }
                else if (buf.len == 0) {
                    continue; //< Eat leading whitespace: We haven't seen anything yet.
                }
                else {
                    c = ' '; //< Normalized whitespace
                }
            }

            if (c == '\'') {
                in_quote = 1;
            }
            else if (c == '\"') {
                in_double_quote = 1;
            }
            else if (c == '`') {
                in_backtick = 1;
                continue; //< Eat it
            }
            else if (c == '\\') {
                in_escape = 1;
                continue; //< Eat it
            }
            else if (c == ';') {
                *end_of_invocation = 1;
                break; //< Eat and exit: we treat line separator (;) like a EOL.
            }
            else if (c == '$') {
                u8 lookahead = str_peak(make_stdout);
                if (lookahead == '(') {
                    in_subshell_expressions = 1;
                    str_pop(make_stdout);
                    continue; //< Eat it: We ignore subshell expressions ($(...))
                }
            }
            else if (c == '&') {
                u8 lookahead = str_peak(make_stdout);
                if (lookahead == '&') { // &&
                    str_pop(make_stdout);
                }
                else if (lookahead == '>') { // &>
                    lookahead = str_peak(make_stdout);
                    if (lookahead == '>') { // &>>
                        str_pop(make_stdout);
                    }
                }
                *end_of_invocation = 1;
                break; //< Eat and exit: we just saw the end of invocation.
            }
            else if (c == '|') {
                u8 lookahead = str_peak(make_stdout);
                if ('|' == lookahead) { // ||
                    str_pop(make_stdout);
                }
                else if ('&' == lookahead) { // |&
                    str_pop(make_stdout);
                }
                *end_of_invocation = 1;
                break; //< Eat and exit: we just saw the end of invocation.
            }
            else if (c == '<') {
                u8 lookahead = str_peak(make_stdout);
                if (lookahead == '&') // <&
                {
                    str_pop(make_stdout);
                    lookahead = str_peak(make_stdout);
                    if (lookahead == '-') { // <&-
                        str_pop(make_stdout);
                    }
                    else if (is_numeric(lookahead)) { // n<&m
                        *make_stdout = str_trim_prefix_if(*make_stdout, is_numeric);
                    }
                }
                *end_of_invocation = 1;
                break;
            }
            else if (c == '>') {
                u8 lookahead = str_peak(make_stdout);
                if (lookahead == '&') // >&
                {
                    str_pop(make_stdout);
                    lookahead = str_peak(make_stdout);
                    if (lookahead == '-') { // >&-
                        str_pop(make_stdout);
                    }
                    else if (is_numeric(lookahead)) { // n>&m
                        *make_stdout = str_trim_prefix_if(*make_stdout, is_numeric);
                    }
                }
                *end_of_invocation = 1;
                break;
            }
            else if (c == ' ') {
                break; //< Eat and exit: A whitespace not inside a quote means the token is done.
            }
            else if (is_numeric(c)) {
                Str temp     = str_trim_prefix_if(*make_stdout, is_numeric);
                u8  lookhead = str_peak(&temp);
                if (lookhead == '>' || lookhead == '<') {
                    *make_stdout = temp; //< Eat the number
                    continue;            //< This is a pipe
                }
            }
        }
        membuf_add_u8(&buf, c);
    }

    if (make_stdout->len == 0) {
        *end_of_invocation = 1;
        *end_of_line       = 1;
    }

    return membuf_finish(&buf);
}

// Return the next invocation.
// While in a typical makefile, an invocation is a line:
//
//      gcc -o main main.c
//
// A single line can can contains multiple invocations:
//
//      echo "Hello world!" && gcc main.c -o main
//     |<----------------->|  |<---------------->|
//         invocation 1           invocation 2
//
StrList shell_next_invocation(Arena *const a, Str *const make_stdout, b32 *const end_of_line)
{
    StrList invocation = {0};

    b32 end_of_invocation = 0;
    while (!end_of_invocation) {
        Str const token = shell_next_token(a, make_stdout, &end_of_invocation, end_of_line);
        if (token.len) {
            strlist_push_back(&invocation, a, token);
        }
    }
    return invocation;
}

// :: Compiler
// We need to be able to identify the compiler building the code if we
// want to be able to parse it's cli flags.
typedef enum {
    COMPILER_IS_UNKNOWN = 0,
    COMPILER_IS_GCC,
    COMPILER_IS_CLANG,
    COMPILER_IS_ZIG_CC,
    COMPILER_IS_CL, //< MSVC
    COMPILER_COUNT,
} CompilerKind_t;

void print_compiler(OsConsoleInterface *console, CompilerKind_t compiler)
{
    Str compiler_str = {0};
    switch (compiler) {
    default: // FALLTHROUGH
    case COMPILER_IS_UNKNOWN: compiler_str = SL("unknown"); break;
    case COMPILER_IS_GCC: compiler_str = SL("gcc"); break;
    case COMPILER_IS_CLANG: compiler_str = SL("clang"); break;
    case COMPILER_IS_ZIG_CC: compiler_str = SL("zig cc"); break;
    case COMPILER_IS_CL: compiler_str = SL("cl.exe"); break;
    }
    static_assert(COMPILER_COUNT == 5, "This switch needs to be exhaustive");
    print_str(console, compiler_str);
}

void println_compiler(OsConsoleInterface *console, CompilerKind_t compiler)
{
    print_compiler(console, compiler);
    print_u8(console, '\n');
}

// TODO: I'm not sure what's the best strategy here
CompilerKind_t compiler_get_platform_default(void)
{
    // We don't know, so we take a guess based on the OS.
#if defined(_WIN32) || defined(_WIN64)
    return COMPILER_IS_GCC;
#elif defined(__APPLE__) && defined(__MACH__)
    return COMPILER_IS_CLANG;
#elif defined(__linux__)
    return COMPILER_IS_GCC;
#else
    return COMPILER_IS_GCC;
#endif
}

CompilerKind_t compiler_parse(Str input)
{
    // NOTE: On parsing and identifying compilers.
    //
    // C and C++ have multiple compiler implementation.
    // e.g. gcc, clang
    //
    // They can have extensions.
    // e.g. gcc.exe, clang.EXE
    //
    // They can have prefix.
    // e.g. x86_64-w64-mingw32-gcc, arm-none-eabi-gcc
    //
    // They can postfix.
    // e.g. clang-22, x86_64-pc-linux-gnu-gcc-15.2.1
    //
    // They can be alias.
    // e.g. cc
    //
    // And to make matters worse, they can be part of a paths:
    // e.g. C:/Users/John_Falstaff/w64devkit/bin/gcc.exe
    //

    input = str_cut(input, SL(".exe")).head;
    input = str_cut(input, SL(".EXE")).head;

    // If there's a path, only keep the executable
    while (1) {
        StrPair const cut = str_cut(input, SL("/"));
        if (cut.ok) {
            // We found a '/'
            input = cut.tail; //< Consume it
        }
        else {
            break;
        }
    }

    if (str_contains(input, SL("clang-cl"))) {
        return COMPILER_IS_CL;
    }
    else if (str_contains(input, SL("clang"))) {
        return COMPILER_IS_CLANG;
    }
    else if (str_contains(input, SL("gcc")) || str_contains(input, SL("g++"))) {
        return COMPILER_IS_GCC;
    }
    else if (str_contains(input, SL("cc")) || (str_contains(input, SL("c++")))) {
        return compiler_get_platform_default();
    }
    else if (str_contains(input, SL("zig"))) {
        return COMPILER_IS_ZIG_CC;
    }
    else if (str_contains(input, SL("cl"))) {
        return COMPILER_IS_CL;
    }

    return COMPILER_IS_UNKNOWN;
}

b32 is_source_file(Str maybe_source)
{
    static Str extensions[] = {
        STATIC_SL(".c"),   STATIC_SL(".cc"), STATIC_SL(".cpp"),  STATIC_SL(".cxx"),
        STATIC_SL(".c++"), STATIC_SL(".C"),  STATIC_SL(".cppm"),
    };

    // Unquote
    maybe_source = str_trim(maybe_source, SL("\""));
    maybe_source = str_trim(maybe_source, SL("\'"));
    maybe_source = str_trim(maybe_source, SL("`"));

    for (isize i = 0; i < count_of(extensions); i++) {
        if (str_ends_with(maybe_source, extensions[i])) {
            return 1;
        }
    }
    return 0;
}

// A "consumer" flag will consume the next token.
// e.g. "-D" will consume the next token as a preprocessor definition.
b32 is_gcc_consumer_flag(Str token)
{
    if (token.len < 1) return 0;

    static StrHashTable flags_lookup = {0};
    if (flags_lookup.count == 0) {
        // clang-format off
        Str flags[] = {
            // Notice that "-x" and "-o" are not part of those flags because we want to handle them specially.
            SL("-I"), SL("--include-directory"),
            SL("-D"), SL("--define-macro"),
            SL("-U"), SL("--undefine-macro"),
            SL("-l"), SL("--library"),
            SL("-e"), SL("--entry"),
            SL("-u"), SL("--undefined"),
            SL("-T"), SL("--script"),
            SL("-L"), SL("--library-directory"),
            SL("-z"),
            SL("-MF"),
            SL("-MT"),
            SL("-MQ"),
            SL("-A"),
            SL("-G"),
            SL("-dumpbase"),
            SL("-dumpbase-ext"),
            SL("-dumpdir"),
            SL("-aux-info"),
            SL("-include"),
            SL("-imacros"),
            SL("-iprefix"),
            SL("-iwithprefix"),
            SL("-iwithprefixbefore"),
            SL("-iquote"),
            SL("-isystem"),
            SL("-idirafter"),
            SL("-isysroot"),
            SL("-imultilib"),
            SL("-Xpreprocessor"),
            SL("-Xassembler"),
            SL("-Xlinker"),
            SL("-bundle_loader"),
            SL("--param"),
            SL("-arch"),
            SL("-wrapper"), SL("-wrapper"),
            // clang-format on
        };
        for (isize i = 0; i < count_of(flags); ++i) {
            strht_insert(&flags_lookup, flags[i]);
        }
    }

    u8 c = str_peak(&token);
    if (c != '-') return 0; // A flag always start with '-'

    b32 const flag_found = strht_lookup(&flags_lookup, token);
    return flag_found;
}

// :: CompilerCmd
// Represent a compiler invocation like `gcc -o main.exe main.c`
typedef struct {
    // Is valid
    b32 ok;
    // e.g. "main.c"
    StrList source_files;
    // e.g. "main.exe"
    Str output_file; //< Optional
    // e.g. "-Wall"
    StrList arguments;
} CompilerCmd;

CompilerCmd compiler_command_from_gcc_tokens(Arena *perm, StrList tokens)
{
    assert(perm);

    StrList args         = {0};
    StrList source_files = {0};

    Str output_file = SL("");

    b32 ignore_next_arg                         = 0;
    b32 dash_o                                  = 0;
    b32 dash_x                                  = 0;
    b32 next_files_should_be_threated_as_target = 0;
    while (!strlist_is_emtpy(tokens)) {
        Str token = strlist_pop_front(&tokens);

        if (ignore_next_arg) {
            ignore_next_arg = 0;
        }
        else if (dash_o) {
            // "-o", "myprogram.exe"
            output_file = token;
            dash_o      = 0;
        }
        else if (dash_x) {
            // "-x", "c"
            next_files_should_be_threated_as_target = 1;
            dash_x                                  = 0;
        }
        else if (str_equal(SL("-o"), token)) {
            // -o file
            // --output=file
            // --output file_
            dash_o = 1;
        }
        else if (token.len > 2 && str_stars_with(token, SL("-o"))) {
            // "-omyprogram.exe"
            Str out     = str_drop_head(token, SL("-o").len);
            output_file = out;
        }
        else if (str_equal(token, SL("-x")) || str_equal(token, SL("--language"))) {
            // -x language
            // --language language
            dash_x = 1;
        }
        else if (str_stars_with(token, SL("--language="))) {
            // --language=language
            Str lang = str_drop_head(token, SL("--language=").len);
            if (str_equal(lang, SL("c")) || str_equal(lang, SL("c++"))) {
                next_files_should_be_threated_as_target = 1;
            }
        }
        else if (is_gcc_consumer_flag(token)) {
            // A "consumer flag" would be the "-D" in: {"-D" "MY_PREPROCESSOR"}
            ignore_next_arg = 1;
        }
        else if (str_stars_with(token, SL("-"))) {
            // This is a flag we don't handle: let's append it.
        }
        else {
            // Either this is a flag we don't care about or this is our target file
            if (is_source_file(token)) {
                strlist_push_back(&source_files, perm, token);
                continue; //< we do not append source file token
            }
            else if (next_files_should_be_threated_as_target) {
                // Because the language flag has been raised, we can't use the file extension to know
                // if the file is a target. So the best we can do is assume it is.
                strlist_push_back(&source_files, perm, token);
                continue; //< we do not append source file token
            }
        }
        strlist_push_back(&args, perm, token);
    }

    CompilerCmd commands  = {0};
    commands.arguments    = args;
    commands.source_files = source_files;
    commands.output_file  = output_file;
    commands.ok           = (args.count > 0) && (source_files.count > 0);
    return commands;
}

void print_compiler_cmd(OsConsoleInterface *console, CompilerCmd cmd)
{
    print_str(console, SL("{"));
    console->tab += 1;

    print_u8(console, '\n');
    print_str(console, SL(".ok = "));
    println_b32(console, cmd.ok);
    print_str(console, SL(".source = "));
    println_strlist(console, cmd.source_files);
    print_str(console, SL(".output = "));
    println_str_escaped_string(console, cmd.output_file);
    print_str(console, SL(".args = "));
    println_strlist(console, cmd.arguments);

    console->tab -= 1;
    print_str(console, SL("}"));
}

void println_compiler_cmd(OsConsoleInterface *console, CompilerCmd cmd)
{
    print_compiler_cmd(console, cmd);
    print_u8(console, '\n');
}

CompilerCmd parse_compiler_command_from_shell_invocation(Arena *perm, StrList invocation)
{
    CompilerCmd compiler_cmd = {0};

    // All commands start with the invocation of the compiler.
    Str first_token  = strlist_peak_at(&invocation, 0);
    Str second_token = strlist_peak_at(&invocation, 1);

    CompilerKind_t compiler = compiler_parse(first_token);
    if (compiler == COMPILER_IS_UNKNOWN) {
        // Sometime the first token could be a compiler wrapper.
        // e.g. "ccache gcc -o main main.c"
        //        ^
        //
        // Let's try to see if we can find a compiler in the second token.
        compiler = compiler_parse(second_token);
        if (compiler == COMPILER_IS_UNKNOWN) {
            // We didn't find anything. Skip this line.
            return compiler_cmd;
        }
        else {
            // Remove the compiler wrapper: we don't want to use it.
            strlist_pop_front(&invocation);
            first_token = second_token;
        }
    }

    switch (compiler) {
    case COMPILER_IS_UNKNOWN: {
        break;
    }
    case COMPILER_IS_GCC: {
        compiler_cmd = compiler_command_from_gcc_tokens(perm, invocation);
        break;
    }
    case COMPILER_IS_CLANG: // FALLTHROUGH
    case COMPILER_IS_ZIG_CC: {
        // Currently we parse clang and zig cc (also clang) like gcc.
        compiler_cmd = compiler_command_from_gcc_tokens(perm, invocation);
        break;
    }
    case COMPILER_IS_CL: {
        // Currently not supported.
        break;
    }
    default: {
        assert(0);
    }
    }

    return compiler_cmd;
}

// :: CommandObject
// A compilation database is a JSON file, which consists of an array of “command objects”, where each command object
// specifies one way a translation unit is compiled in the project.
typedef struct {
    //  The working directory of the compilation. All paths specified in the command or file fields must be either
    //  absolute or relative to this directory.
    Str directory;
    // The main translation unit source processed by this compilation step.
    Str file;
    // The compile command argv as list of strings.
    StrList arguments;
    // The name of the output created by this compilation step. This field is optional.
    Str output;
    // Is this a command object?
    b32 ok;
} CommandObject;

// Slice of command objects
typedef struct {
    isize          len;
    CommandObject *ptr;
} CommandObjects;

void print_command_object(OsConsoleInterface *console, CommandObject obj)
{
    println_str(console, SL("["));
    console->tab += 1;

    println_str(console, SL("{"));
    console->tab += 1;

    print_str(console, SL(".ok = "));
    println_b32(console, obj.ok);
    print_str(console, SL(".directory = "));
    println_str_escaped_string(console, obj.directory);
    print_str(console, SL(".file = "));
    println_str_escaped_string(console, obj.file);
    print_str(console, SL(".output = "));
    println_str_escaped_string(console, obj.output);
    print_str(console, SL(".args = "));
    println_strlist(console, obj.arguments);

    console->tab += -1;
    println_str(console, SL("}"));

    console->tab += -1;
    println_str(console, SL("]"));
}

void println_command_object(OsConsoleInterface *console, CommandObject obj)
{
    print_command_object(console, obj);
    print_u8(console, '\n');
}

CommandObjects command_object_next(Arena *perm, DirectoryStack *dir_stack, Str *make_stdout,
                                   OsConsoleInterface *console, b32 verbose_mode)
{
    CommandObjects command_objects = {0};
    while ((make_stdout->len > 0) && (command_objects.len == 0)) {
        Str before = *make_stdout;

        ParsingMode mode = identify_parsing_mode(*make_stdout);
        if (verbose_mode) {
            console->tab = 0;
            println_str(console, SL("\n------"));
            println_str(console, SL("Step 1: Identifying the parsing mode"));
            console->tab += 1;
        }

        switch (mode) {
        case PARSING_MODE_MAKE_ENTER_DIR: {
            Str dir = parse_directory_from_make_dir(make_stdout);
            if (dir.len > 0) {
                dirstack_push(dir_stack, dir);
            }
            if (verbose_mode) {
                println_str(console, SL("Parsing mode is ENTER_DIR\n"));
                console->tab = 0;
                println_str(console, SL("Step 2: Extacting directory"));
                console->tab += 1;
                print_str(console, SL("Input: "));
                println_str_escaped_string(console, str_take_head(before, before.len - make_stdout->len));
                print_str(console, SL("Output: "));
                println_str_escaped_string(console, dir);
            }
            break;
        }
        case PARSING_MODE_MAKE_LEAVE_DIR: {
            Str dir = parse_directory_from_make_dir(make_stdout);
            if (dir.len > 0) {
                Arena scratch = *perm;

                Str cwd = dirstack_pop(dir_stack, &scratch);

                if (verbose_mode && !str_equal(cwd, dir)) {
                    println_str(console, SL("[!] Exiting a directory we did not enter."));
                    print_str(console, SL("[!] Actual: "));
                    println_str_escaped_string(console, dir);
                    print_str(console, SL("[!] expected: "));
                    println_str_escaped_string(console, cwd);
                }
                (void)cwd;
            }

            if (verbose_mode) {
                println_str(console, SL("Parsing mode is LEAVE_DIR"));
                console->tab = 0;
                println_str(console, SL("\nStep 2: Extacting directory"));
                console->tab += 1;
                print_str(console, SL("Input: "));
                println_str_escaped_string(console, str_take_head(before, before.len - make_stdout->len));
                print_str(console, SL("Output: "));
                println_str_escaped_string(console, dir);
            }
            break;
        }
        case PARSING_MODE_SHELL: {
            if (verbose_mode) {
                println_str(console, SL("Parsing mode is SHELL"));
                console->tab = 0;
                println_str(console, SL("\nStep 2: Parsing shell command"));
                console->tab += 1;
            }

            i8 const tabulation_level = console->tab;

            b32 end_of_line = 0;
            while (!end_of_line) {
                console->tab = tabulation_level;
                before       = *make_stdout;

                // Memory lifetime:
                // The characters and their slice are allocated in scratch.
                Arena   scratch     = *perm;
                StrList invocations = shell_next_invocation(&scratch, make_stdout, &end_of_line);

                if (verbose_mode) {
                    println_str(console, SL("Invocation"));
                    console->tab += 1;
                    print_str(console, SL("Input: "));
                    println_str_escaped_string(console, str_take_head(before, before.len - make_stdout->len));
                    print_str(console, SL("Tokens: "));
                    println_strlist(console, invocations);
                }

                CompilerCmd compiler_command = parse_compiler_command_from_shell_invocation(&scratch, invocations);
                if (!compiler_command.ok) {
                    if (verbose_mode) {
                        println_str(console, SL("Decision: We are skipping this line."));
                    }
                    continue;
                }

                *perm = scratch; // Commit memory

                if (verbose_mode) {
                    println_str(console, SL("Decision: This is a command compiling a source file."));
                }

                // For every source files in the compiler command, create a CommandObject.
                // E.g.
                //      gcc -o main lib.c main.c
                //
                // will create
                //
                //      1. gcc -o main lib.c
                //      2. gcc -o main main.c
                //
                command_objects.len = compiler_command.source_files.count;
                command_objects.ptr = ALLOC(perm, command_objects.len, *command_objects.ptr);

                // The top of the stack is the Current Working Directory
                Str cwd = dirstack_peak(dir_stack, perm);

                if (verbose_mode) {
                    console->tab = 0;
                    println_str(console, SL("\nStep 3: Creating command objects"));
                    console->tab += 1;
                }

                isize i = 0;
                for (StrListNode *node = compiler_command.source_files.front; node != NULL; i++, node = node->next) {
                    Str source_file = node->str;

                    CommandObject cmd = {0};
                    cmd.file          = source_file;
                    cmd.directory     = cwd;
                    cmd.output        = compiler_command.output_file;
                    cmd.ok            = compiler_command.ok;

                    cmd.arguments = strlist_copy(compiler_command.arguments, perm);
                    strlist_push_back(&cmd.arguments, perm, source_file);

                    assert(i < command_objects.len);
                    command_objects.ptr[i] = cmd;

                    if (verbose_mode) {
                        println_command_object(console, cmd);
                    }
                }
            }
            break;
        }
        default: unreachable();
        }
    }

    return command_objects;
}

// :: Json
// Simple json writer
void json_write_header(OsConsoleInterface *console)
{
    if (!console) return;

    console->tab = 0;
    Str header   = SL("[");
    println_str(console, header);
    console->tab += 1;
}

void json_write_command_object(OsConsoleInterface *console, CommandObject command, isize command_count)
{
    if (!console) return;

    // Our goal is to produce the following output:
    //
    //      {
    //        "directory": "C:/dev/myproject",
    //        "file": "main.c",
    //        "output": "main.o"
    //        "arguments": [
    //          "gcc",
    //          "-c",
    //          "-o",
    //          "main.o",
    //          "main.c"
    //        ],
    //      }
    //

    if (command_count > 0) {
        // Add the comma at the end of the last object `}`
        print_str(console, SL(",\n"));
    }

    print_str(console, SL("{\n"));
    console->tab += 1;

    // "directory": "C:/dev/gb/make2compdb/input/simple with space",
    print_str(console, SL("\"directory\": "));
    print_str_escaped_string(console, command.directory);
    println_str(console, SL(","));

    // "file": "main.c",
    print_str(console, SL("\"file\": "));
    print_str_escaped_string(console, command.file);
    println_str(console, SL(","));

    if (command.output.len > 0) { //< Ouput is sometime not specified
        // "output": "main.o"
        print_str(console, SL("\"output\": "));
        print_str_escaped_string(console, command.output);
        println_str(console, SL(","));
    }

    // "arguments": [
    //          "gcc",
    //           ...
    //  ]
    println_str(console, SL("\"arguments\": ["));
    console->tab += 1;
    for (StrListNode *arg = command.arguments.front; arg != NULL; arg = arg->next) {
        print_str_escaped_string(console, arg->str);

        b32 const is_last = (arg->next == NULL);
        if (!is_last) {
            print_u8(console, ',');
        }
        print_u8(console, '\n');
    }

    console->tab -= 1;
    print_str(console, SL("]\n")); //< no comma!

    console->tab -= 1;
    print_str(console, SL("}"));
}

void json_write_footer(OsConsoleInterface *console, isize command_count)
{
    if (!console) return;

    if (command_count > 0) {
        print_u8(console, '\n');
    }

    console->tab -= 1;
    Str footer    = SL("]");
    println_str(console, footer);
    console->tab = 0;
}

// :: make2compdb
// Main program entry.
int make2compdb(Arena *perm, OsConsoleInterface *console, StrList cli_args, Str make_stdout,
                Str initial_working_directory)
{
    Str const help_message = SL("make2compdb - Create a compile_commands.json from a Make output\n"
                                "Usage: make2compdb [Option]\n"
                                "\n"
                                "Options:\n"
                                "       --version:  Display the version number.\n"
                                "   -h, --help:     Display this help page.\n"
                                "   -v, --verbose:  Display debug information to help with problem diagnosis.\n"
                                "\n"
                                "Examples:\n"
                                "   $ make -Bwn | make2compdb.exe > compile_commands.json\n"
                                "   $ echo \"gcc -o main main.c\" | make2compdb.exe > compile_commands.json\n");

    Str program_name = {0};
    b32 verbose_mode = 0;
    for (StrListNode *arg = cli_args.front; arg != NULL; arg = arg->next) {
        if (arg == cli_args.front) {
            program_name = cli_args.front->str;
        }
        else if (str_equal(arg->str, SL("-h")) || str_equal(arg->str, SL("--help"))) {
            println_str(console, help_message);
            return 0;
        }
        else if (str_equal(arg->str, SL("--version"))) {
            println_str(console, SL("Version: " VERSION "\n"));
            return 0;
        }
        else if (str_equal(arg->str, SL("-v")) || str_equal(arg->str, SL("--verbose"))) {
            verbose_mode = 1;
        }
        else {
            print_str(console, SL("Unknown CLI arg: '"));
            print_str(console, arg->str);
            println_str(console, SL("'\n"));
            return -1;
        }
    }
    (void)program_name; //< Not used currently

    if (verbose_mode) {
        console->tab = 0;
        println_str(console, SL("make2compdb"));
        println_str(console, SL("Version: " VERSION));
        println_str(console, SL("Verbose mode: true"));
        print_str(console, SL("Directory: "));
        println_str_escaped_string(console, initial_working_directory);
        print_str(console, SL("CLI args: "));
        println_strlist(console, cli_args);
        print_str(console, SL("Input len: "));
        println_number(console, make_stdout.len);
    }

    DirectoryStack dir_stack = {0};
    {
        isize dir_arena_cap = 1 << 14;
        byte *mem           = ALLOC(perm, dir_arena_cap, byte);
        dir_stack.arena     = arena_init(dir_arena_cap, mem);

        dirstack_push(&dir_stack, initial_working_directory);
    }

    // In verbose mode, we don't print json.
    OsConsoleInterface *json_console = verbose_mode ? NULL : console;

    json_write_header(json_console);

    int   err           = 0;
    isize command_count = 0;
    while (make_stdout.len > 0) {
        Arena          scratch  = *perm;
        CommandObjects commands = command_object_next(&scratch, &dir_stack, &make_stdout, console, verbose_mode);
        for (isize i = 0; i < commands.len; ++i) {
            CommandObject command = commands.ptr[i];
            if (!command.ok) {
                err = 1;
                continue;
            }
            json_write_command_object(json_console, command, command_count);
        }
        command_count += commands.len;
    }

    json_write_footer(json_console, command_count);

    if (verbose_mode) {
        console->tab = 0;
        print_u8(console, '\n');
        print_str(console, SL("Total command objects: "));
        println_number(console, command_count);
        println_str(console, SL("make2compdb is done."));
    }
    return err;
}

// OS Stream aka standard device (standard input, standard output, or standard error).
typedef struct {
    // Opaque handle to the device
    uintptr_t handle;
    // Whether the device is a console or not.
    // e.g. If stdin is not a console, it could be a file or a pipe.
    b32 is_console;
    // If the stream is in error.
    b32 err;
} OsStream;

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
#if defined(TEST) // Unit tests

#include <stdio.h>
#include <stdlib.h>

#define STRINGIFY_(S) #S
#define STRINGIFY(S)  STRINGIFY_(S)

#define CHECK(COND)                                                                                                    \
    do {                                                                                                               \
        if (!(COND)) {                                                                                                 \
            puts("Test failed:");                                                                                      \
            puts(STRINGIFY(COND));                                                                                     \
            puts("\n");                                                                                                \
            assert(0);                                                                                                 \
        }                                                                                                              \
    } while (0)

// Because our tests can use the CRT, we can use variadic macros!
#define SLIST(A, ...) strlist_from_cstrs(A, (sizeof(__VA_ARGS__) / sizeof(*__VA_ARGS__)), __VA_ARGS__)

void test_shell_tokenizer(Arena a)
{
    b32 end_of_line = 0;
    {
        Arena   scratch = a;
        Str     input   = SL("gcc -std=c11 -g -o app main.c");
        StrList line    = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_equal(line, SLIST(&scratch, (char *[]){"gcc", "-std=c11", "-g", "-o", "app", "main.c"})));

        line = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_is_emtpy(line));
    }

    // With complex gcc
    {
        Arena   scratch = a;
        Str     input   = SL("x86_64-w64-mingw32-gcc -std=c11 -g -o app main.c");
        StrList line    = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_equal(
            line, SLIST(&scratch, (char *[]){"x86_64-w64-mingw32-gcc", "-std=c11", "-g", "-o", "app", "main.c"})));

        line = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_is_emtpy(line));
    }

    // With full path
    {
        Arena   scratch = a;
        Str     input   = SL("C:/Users/John_Falstaff/w64devkit/bin/gcc.exe -std=c11 -g -o app main.c");
        StrList line    = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_equal(line, SLIST(&scratch, (char *[]){"C:/Users/John_Falstaff/w64devkit/bin/gcc.exe", "-std=c11",
                                                             "-g", "-o", "app", "main.c"})));

        line = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_is_emtpy(line));
    }

    // Special char handling: ';'
    {
        Arena   scratch = a;
        Str     input   = SL("gcc -o app main.c;ls -la");
        StrList line    = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_equal(line, SLIST(&scratch, (char *[]){"gcc", "-o", "app", "main.c"})));

        line = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_equal(line, SLIST(&scratch, (char *[]){"ls", "-la"})));

        line = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_is_emtpy(line));
    }

    // Special char handling: '&'
    {
        Arena scratch = a;
        Str   input   = SL("echo \"hey\" & gcc -o app main.c");

        StrList line = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_equal(line, SLIST(&scratch, (char *[]){"echo", "\"hey\""})));

        line = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_equal(line, SLIST(&scratch, (char *[]){"gcc", "-o", "app", "main.c"})));

        line = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_is_emtpy(line));
    }

    // Special char handling: '|'
    {
        Arena scratch = a;
        Str   input   = SL("echo \"hey\" | gcc -o app main.c");

        StrList line = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_equal(line, SLIST(&scratch, (char *[]){"echo", "\"hey\""})));

        line = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_equal(line, SLIST(&scratch, (char *[]){"gcc", "-o", "app", "main.c"})));

        line = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_is_emtpy(line));
    }

    // Special char handling: '&&'
    {
        Arena scratch = a;
        Str   input   = SL("echo \"hey\" && gcc -o app main.c");

        StrList line = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_equal(line, SLIST(&scratch, (char *[]){"echo", "\"hey\""})));

        line = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_equal(line, SLIST(&scratch, (char *[]){"gcc", "-o", "app", "main.c"})));

        line = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_is_emtpy(line));
    }

    // Special char handling: '||'
    {
        Arena scratch = a;
        Str   input   = SL("echo \"hey\" || gcc -o app main.c");

        StrList line = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_equal(line, SLIST(&scratch, (char *[]){"echo", "\"hey\""})));

        line = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_equal(line, SLIST(&scratch, (char *[]){"gcc", "-o", "app", "main.c"})));

        line = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_is_emtpy(line));
    }

    // Special char handling: '|&'
    {
        Arena scratch = a;
        Str   input   = SL("echo \"hey\" |& gcc -o app main.c");

        StrList line = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_equal(line, SLIST(&scratch, (char *[]){"echo", "\"hey\""})));

        line = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_equal(line, SLIST(&scratch, (char *[]){"gcc", "-o", "app", "main.c"})));

        line = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_is_emtpy(line));
    }

    // Handle quotes inside of double quotes
    {
        Arena   scratch = a;
        Str     input   = SL("echo \"The Knights Who Say 'Ni'\"");
        StrList line    = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_equal(line, SLIST(&scratch, (char *[]){"echo", "\"The Knights Who Say 'Ni'\""})));

        line = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_is_emtpy(line));
    }

    // Handle double quotes inside of quotes
    {
        Arena   scratch = a;
        Str     input   = SL("echo 'The Knights Who Say \"Ni\"'");
        StrList line    = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_equal(line, SLIST(&scratch, (char *[]){"echo", "'The Knights Who Say \"Ni\"'"})));

        line = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_is_emtpy(line));
    }

    // Make sure we delete unecessary whitespace everywhere except in quotes
    {
        Arena   scratch = a;
        Str     input   = SL("   echo    '  H E L L O   W O R L D  '");
        StrList line    = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_equal(line, SLIST(&scratch, (char *[]){"echo", "'  H E L L O   W O R L D  '"})));

        line = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_is_emtpy(line));
    }

    // Ignore subshell expressions
    {
        Arena   scratch = a;
        Str     input   = SL("gcc $(pkg-config --cflags glib) foo.c");
        StrList line    = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_equal(line, SLIST(&scratch, (char *[]){"gcc", "foo.c"})));

        line = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_is_emtpy(line));
    }

    // With line breaks
    {
        Arena   scratch = a;
        Str     input   = SL("gcc \\\n -std=c11 \\\n -Wall \\\n -Wextra \\\n -g \\\n -o out/continued \\\n main.c");
        StrList line    = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_equal(line, SLIST(&scratch, (char *[]){"gcc", "-std=c11", "-Wall", "-Wextra", "-g", "-o",
                                                             "out/continued", "main.c"})));

        line = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_is_emtpy(line));
    }

    // With line breaks windows
    {
        Arena scratch = a;
        Str   input =
            SL("gcc \\\r\n -std=c11 \\\r\n -Wall \\\r\n -Wextra \\\r\n -g \\\r\n -o out/continued \\\r\n main.c");
        StrList line = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_equal(line, SLIST(&scratch, (char *[]){"gcc", "-std=c11", "-Wall", "-Wextra", "-g", "-o",
                                                             "out/continued", "main.c"})));

        line = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_is_emtpy(line));
    }

    // With quotes and space in the compiler path
    {
        Arena   scratch = a;
        Str     input   = SL("\"C:/Users/John Falstaff/w64devkit/bin/gcc.exe\" -std=c11 -g -o app main.c");
        StrList line    = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_equal(line, SLIST(&scratch, (char *[]){"\"C:/Users/John Falstaff/w64devkit/bin/gcc.exe\"",
                                                             "-std=c11", "-g", "-o", "app", "main.c"})));

        line = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_is_emtpy(line));
    }

    // With backslash in path
    {
        Arena   scratch = a;
        Str     input   = SL("C:/Users/John\\ Falstaff/w64devkit/bin/gcc.exe -std=c11 -g -o app main.c");
        StrList line    = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_equal(line, SLIST(&scratch, (char *[]){"C:/Users/John\\ Falstaff/w64devkit/bin/gcc.exe",
                                                             "-std=c11", "-g", "-o", "app", "main.c"})));

        line = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_is_emtpy(line));
    }

    // With single quoted name
    {
        Arena   scratch = a;
        Str     input   = SL("C:/Users/'John Falstaff'/w64devkit/bin/gcc.exe -std=c11 -g -o app main.c");
        StrList line    = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_equal(line, SLIST(&scratch, (char *[]){"C:/Users/'John Falstaff'/w64devkit/bin/gcc.exe",
                                                             "-std=c11", "-g", "-o", "app", "main.c"})));

        line = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_is_emtpy(line));
    }

    // With double quoted name
    {
        Arena   scratch = a;
        Str     input   = SL("C:/Users/\"John Falstaff\"/w64devkit/bin/gcc.exe -std=c11 -g -o app main.c");
        StrList line    = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_equal(line, SLIST(&scratch, (char *[]){"C:/Users/\"John Falstaff\"/w64devkit/bin/gcc.exe",
                                                             "-std=c11", "-g", "-o", "app", "main.c"})));

        line = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_is_emtpy(line));
    }

    // Multi-line make output
    {
        Arena scratch = a;
        Str   input   = SL("make: Entering directory 'C:/dev/gb/make2compdb/input/simple_inline'\n"
                           "gcc -std=c11 -Wall -Wextra -Wpedantic -g -o app main.c\n"
                           "make: Leaving directory 'C:/dev/gb/make2compdb/input/simple_inline'\n");

        StrList line = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_equal(line, SLIST(&scratch, (char *[]){"make:", "Entering", "directory",
                                                             "'C:/dev/gb/make2compdb/input/simple_inline'"})));

        line = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_equal(line, SLIST(&scratch, (char *[]){"gcc", "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-g",
                                                             "-o", "app", "main.c"})));

        line = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_equal(line, SLIST(&scratch, (char *[]){"make:", "Leaving", "directory",
                                                             "'C:/dev/gb/make2compdb/input/simple_inline'"})));

        line = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_is_emtpy(line));
    }

    // With windows end-of-line
    {
        Arena scratch = a;

        // Taken from a stm32cubemx build script
        Str input =
            SL("make: Entering directory 'C:/dev/gb/test'\r\n"
               "mkdir build\r\n"
               "arm-none-eabi-gcc -c -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -DUSE_HAL_DRIVER "
               "Core/Src/main.c -o build/main.o\r\n");

        StrList line = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_equal(line, SLIST(&scratch, (char *[]){"make:", "Entering", "directory", "'C:/dev/gb/test'"})));

        line = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_equal(line, SLIST(&scratch, (char *[]){"mkdir", "build"})));

        line = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(
            strlist_equal(line, SLIST(&scratch, (char *[]){"arm-none-eabi-gcc", "-c", "-mcpu=cortex-m4", "-mthumb",
                                                           "-mfpu=fpv4-sp-d16", "-mfloat-abi=hard", "-DUSE_HAL_DRIVER",
                                                           "Core/Src/main.c", "-o", "build/main.o"})));

        line = shell_next_invocation(&scratch, &input, &end_of_line);
        CHECK(strlist_is_emtpy(line));
    }
}

void test_parse_directory(Arena a)
{
    (void)a;

    Str dir = {0};

    // Simple test: We enter and exit directory
    {
        Str input = SL("make: Entering directory 'C:/dev/gb/make2compdb/input/simple_inline'\n"
                       "gcc -std=c11 -Wall -Wextra -Wpedantic -g -o app main.c\n"
                       "make: Leaving directory 'C:/dev/gb/make2compdb/input/simple_inline'\n");

        dir = parse_directory_from_make_dir(&input);
        CHECK(str_equal(dir, SL("C:/dev/gb/make2compdb/input/simple_inline")));

        dir = parse_directory_from_make_dir(&input);
        CHECK(str_equal(dir, SL("")));

        dir = parse_directory_from_make_dir(&input);
        CHECK(str_equal(dir, SL("C:/dev/gb/make2compdb/input/simple_inline")));
    }

    // Windows EOL
    {
        Str input = SL("make: Entering directory 'C:/dev/gb/make2compdb/input/simple_inline'\r\n"
                       "gcc -std=c11 -Wall -Wextra -Wpedantic -g -o app main.c\r\n"
                       "make: Leaving directory 'C:/dev/gb/make2compdb/input/simple_inline'\r\n");

        dir = parse_directory_from_make_dir(&input);
        CHECK(str_equal(dir, SL("C:/dev/gb/make2compdb/input/simple_inline")));

        dir = parse_directory_from_make_dir(&input);
        CHECK(str_equal(dir, SL("")));

        dir = parse_directory_from_make_dir(&input);
        CHECK(str_equal(dir, SL("C:/dev/gb/make2compdb/input/simple_inline")));
    }

    // Non-standard `make` program
    // See https://github.com/nickdiego/compiledb/issues/146
    {
        Str input = SL("make-3.81: Entering directory 'C:/dev/gb/make2compdb/input/simple_inline'\n"
                       "gcc -std=c11 -Wall -Wextra -Wpedantic -g -o app main.c\n"
                       "make-3.81: Leaving directory 'C:/dev/gb/make2compdb/input/simple_inline'\n");

        dir = parse_directory_from_make_dir(&input);
        CHECK(str_equal(dir, SL("C:/dev/gb/make2compdb/input/simple_inline")));

        dir = parse_directory_from_make_dir(&input);
        CHECK(str_equal(dir, SL("")));

        dir = parse_directory_from_make_dir(&input);
        CHECK(str_equal(dir, SL("C:/dev/gb/make2compdb/input/simple_inline")));
    }

    // The directory has a \n in it's name
    {
        //                                         look here
        //                                           vv
        Str input = SL("make: Entering directory 'C:\\nice\\dir'\n"
                       "gcc -std=c11 -Wall -Wextra -Wpedantic -g -o app main.c\n"
                       "make: Leaving directory 'C:\\nice\\dir'\n");

        dir = parse_directory_from_make_dir(&input);
        CHECK(str_equal(dir, SL("C:\\nice\\dir")));

        dir = parse_directory_from_make_dir(&input);
        CHECK(str_equal(dir, SL("")));

        dir = parse_directory_from_make_dir(&input);
        CHECK(str_equal(dir, SL("C:\\nice\\dir")));
    }

    // Weird mix of delimiter (seems to be the case for the BSD make?)
    // See https://github.com/fcying/compiledb-go/issues/1
    {
        Str input = SL("make: Entering directory `/home/test'\n"
                       "gcc -std=c11 -Wall -Wextra -Wpedantic -g -o app main.c\n"
                       "make: Leaving directory `/home/test'\n");

        dir = parse_directory_from_make_dir(&input);
        CHECK(str_equal(dir, SL("/home/test")));

        dir = parse_directory_from_make_dir(&input);
        CHECK(str_equal(dir, SL("")));

        dir = parse_directory_from_make_dir(&input);
        CHECK(str_equal(dir, SL("/home/test")));
    }
}

void test_compiler_parse(Arena a)
{
    (void)a;
    Str input;
    {
        input = SL("gcc");
        CHECK(COMPILER_IS_GCC == compiler_parse(input));

        input = SL("gcc.exe");
        CHECK(COMPILER_IS_GCC == compiler_parse(input));

        input = SL("arm-none-eabi-gcc");
        CHECK(COMPILER_IS_GCC == compiler_parse(input));

        input = SL("x86_64-pc-linux-gnu-gcc-15.2.1");
        CHECK(COMPILER_IS_GCC == compiler_parse(input));

        input = SL("C:/Users/gberthiaume/scoop/apps/w64devkit/current/bin/gcc");
        CHECK(COMPILER_IS_GCC == compiler_parse(input));

        input = SL("C:/Users/gberthiaume/scoop/apps/w64devkit/current/bin/x86_64-pc-linux-gnu-gcc-15.2.1");
        CHECK(COMPILER_IS_GCC == compiler_parse(input));

        // Let's be naughty and have "clang" in our path
        input = SL("C:/clang/gcc");
        CHECK(COMPILER_IS_GCC == compiler_parse(input));
    }
    {
        input = SL("clang");
        CHECK(COMPILER_IS_CLANG == compiler_parse(input));

        input = SL("clang.exe");
        CHECK(COMPILER_IS_CLANG == compiler_parse(input));

        input = SL("clang-22");
        CHECK(COMPILER_IS_CLANG == compiler_parse(input));

        input = SL("ccache-clang-11");
        CHECK(COMPILER_IS_CLANG == compiler_parse(input));

        input = SL("C:/Users/gberthiaume/scoop/apps/llvm/current/bin/clang");
        CHECK(COMPILER_IS_CLANG == compiler_parse(input));

        input = SL("C:/Users/gberthiaume/scoop/apps/llvm/current/bin/clang-22");
        CHECK(COMPILER_IS_CLANG == compiler_parse(input));
    }
    {
        input = SL("zig");
        CHECK(COMPILER_IS_ZIG_CC == compiler_parse(input));

        input = SL("zig.exe");
        CHECK(COMPILER_IS_ZIG_CC == compiler_parse(input));

        input = SL("C:/Users/gberthiaume/scoop/apps/zig/0.16.0/zig.exe");
        CHECK(COMPILER_IS_ZIG_CC == compiler_parse(input));
    }
}

void test_compiler_command_from_gcc_tokens(Arena a)
{
    Arena       scratch;
    CompilerCmd cmd;

    // Just a simple test
    {
        scratch             = a;
        StrList inputs_list = SLIST(&scratch, (char *[]){"gcc", "-o", "main", "main.c"});

        cmd = compiler_command_from_gcc_tokens(&scratch, inputs_list);
        CHECK(cmd.ok == 1);
        CHECK(strlist_equal(cmd.source_files, SLIST(&scratch, (char *[]){"main.c"})));
        CHECK(str_equal(cmd.output_file, SL("main")));
        CHECK(strlist_equal(cmd.arguments, SLIST(&scratch, (char *[]){"gcc", "-o", "main"})));
    }

    // With a "-omain"
    {
        scratch             = a;
        StrList inputs_list = SLIST(&scratch, (char *[]){"gcc", "-omain", "main.c"});

        cmd = compiler_command_from_gcc_tokens(&scratch, inputs_list);
        CHECK(cmd.ok == 1);
        CHECK(strlist_equal(cmd.source_files, SLIST(&scratch, (char *[]){"main.c"})));
        CHECK(str_equal(cmd.output_file, SL("main")));
        CHECK(strlist_equal(cmd.arguments, SLIST(&scratch, (char *[]){"gcc", "-omain"})));
    }
    {
        // No ouput specified
        scratch             = a;
        StrList inputs_list = SLIST(&scratch, (char *[]){"gcc", "main.c"});

        cmd = compiler_command_from_gcc_tokens(&scratch, inputs_list);
        CHECK(cmd.ok == 1);
        CHECK(strlist_equal(cmd.source_files, SLIST(&scratch, (char *[]){"main.c"})));
        CHECK(str_equal(cmd.output_file, SL("")));
        CHECK(strlist_equal(cmd.arguments, SLIST(&scratch, (char *[]){"gcc"})));
    }
    {
        // More flags
        scratch             = a;
        StrList inputs_list = SLIST(&scratch, (char *[]){"gcc", "-std=c23", "-O0", "-g3", "-Wall", "-Wextra",
                                                         "-Wpedantic", "main.c", "-o", "main"});

        cmd = compiler_command_from_gcc_tokens(&scratch, inputs_list);
        CHECK(cmd.ok == 1);
        CHECK(strlist_equal(cmd.source_files, SLIST(&scratch, (char *[]){"main.c"})));
        CHECK(str_equal(cmd.output_file, SL("main")));
        CHECK(strlist_equal(cmd.arguments, SLIST(&scratch, (char *[]){"gcc", "-std=c23", "-O0", "-g3", "-Wall",
                                                                      "-Wextra", "-Wpedantic", "-o", "main"})));
    }

    // With include dir
    {
        scratch             = a;
        StrList inputs_list = SLIST(&scratch, (char *[]){"gcc", "-I", "mydir", "-o", "main", "main.c"});

        cmd = compiler_command_from_gcc_tokens(&scratch, inputs_list);
        CHECK(cmd.ok == 1);
        CHECK(strlist_equal(cmd.source_files, SLIST(&scratch, (char *[]){"main.c"})));
        CHECK(str_equal(cmd.output_file, SL("main")));
        CHECK(strlist_equal(cmd.arguments, SLIST(&scratch, (char *[]){"gcc", "-I", "mydir", "-o", "main"})));
    }

    // With include directory in the same token
    {
        scratch             = a;
        StrList inputs_list = SLIST(&scratch, (char *[]){"gcc", "-Imydir", "-o", "main", "main.c"});

        cmd = compiler_command_from_gcc_tokens(&scratch, inputs_list);
        CHECK(cmd.ok == 1);
        CHECK(strlist_equal(cmd.source_files, SLIST(&scratch, (char *[]){"main.c"})));
        CHECK(str_equal(cmd.output_file, SL("main")));
        CHECK(strlist_equal(cmd.arguments, SLIST(&scratch, (char *[]){"gcc", "-Imydir", "-o", "main"})));
    }

    // With include directory with path
    // https://github.com/fcying/compiledb-go/issues/11
    {
        scratch = a;
        StrList inputs_list =
            SLIST(&scratch, (char *[]){"gcc", " -I'D:\\path to scoop\\scoop\\apps\\gcc\\current\\include'", "-o",
                                       "main", "main.c"});

        cmd = compiler_command_from_gcc_tokens(&scratch, inputs_list);
        CHECK(cmd.ok == 1);
        CHECK(strlist_equal(cmd.source_files, SLIST(&scratch, (char *[]){"main.c"})));
        CHECK(str_equal(cmd.output_file, SL("main")));
        CHECK(strlist_equal(
            cmd.arguments,
            SLIST(&scratch,
                  (char *[]){"gcc", " -I'D:\\path to scoop\\scoop\\apps\\gcc\\current\\include'", "-o", "main"})));
    }

    // With include directory that looks like a c file
    {
        scratch             = a;
        StrList inputs_list = SLIST(&scratch, (char *[]){"gcc", "-I", "test.c", "-o", "main", "main.c"});

        cmd = compiler_command_from_gcc_tokens(&scratch, inputs_list);
        CHECK(cmd.ok == 1);
        CHECK(strlist_equal(cmd.source_files, SLIST(&scratch, (char *[]){"main.c"})));
        CHECK(str_equal(cmd.output_file, SL("main")));
        CHECK(strlist_equal(cmd.arguments, SLIST(&scratch, (char *[]){"gcc", "-I", "test.c", "-o", "main"})));
    }

    // With preprocessor that looks like a c file
    {
        scratch             = a;
        StrList inputs_list = SLIST(&scratch, (char *[]){"gcc", "-D", "test.c", "-o", "main", "main.c"});

        cmd = compiler_command_from_gcc_tokens(&scratch, inputs_list);
        CHECK(cmd.ok == 1);
        CHECK(strlist_equal(cmd.source_files, SLIST(&scratch, (char *[]){"main.c"})));
        CHECK(str_equal(cmd.output_file, SL("main")));
        CHECK(strlist_equal(cmd.arguments, SLIST(&scratch, (char *[]){"gcc", "-D", "test.c", "-o", "main"})));
    }

    // Single header library: With "-x c" that changes the language of the next token.
    // Ref: https://github.com/nickdiego/compiledb/issues/140
    {
        scratch = a;
        StrList inputs_list =
            SLIST(&scratch, (char *[]){"gcc", "-x", "c", "-c", "./stb_image.h", "-DSTB_IMAGE_IMPLEMENTATION"});

        cmd = compiler_command_from_gcc_tokens(&scratch, inputs_list);
        CHECK(cmd.ok == 1);
        CHECK(strlist_equal(cmd.source_files, SLIST(&scratch, (char *[]){"./stb_image.h"})));
        CHECK(str_equal(cmd.output_file, SL("")));
        CHECK(strlist_equal(cmd.arguments,
                            SLIST(&scratch, (char *[]){"gcc", "-x", "c", "-c", "-DSTB_IMAGE_IMPLEMENTATION"})));
    }

    // Supports macros with quotes
    // https://github.com/nickdiego/compiledb/issues/131
    {
        scratch = a;
        StrList inputs_list =
            SLIST(&scratch, (char *[]){"cc", "-MMD", "-MP", "-O2", "-march=native", "-iquote", "./include",
                                       "-U_FORTIFY_SOURCE", "-DPROG_NAME=\"waffle\"", "-c", "src/cursor_events.c", "-o",
                                       ".build/release/cursor_events.o"});
        cmd = compiler_command_from_gcc_tokens(&scratch, inputs_list);
        CHECK(cmd.ok == 1);
        CHECK(strlist_equal(cmd.source_files, SLIST(&scratch, (char *[]){"src/cursor_events.c"})));
        CHECK(str_equal(cmd.output_file, SL(".build/release/cursor_events.o")));
        CHECK(strlist_equal(cmd.arguments,
                            SLIST(&scratch, (char *[]){"cc", "-MMD", "-MP", "-O2", "-march=native", "-iquote",
                                                       "./include", "-U_FORTIFY_SOURCE", "-DPROG_NAME=\"waffle\"", "-c",
                                                       "-o", ".build/release/cursor_events.o"})));
    }

    // With multiple c files
    {
        scratch             = a;
        StrList inputs_list = SLIST(&scratch, (char *[]){"gcc", "-o", "app", "main.c", "mathlib.c", "strlib.c"});

        cmd = compiler_command_from_gcc_tokens(&scratch, inputs_list);
        CHECK(cmd.ok == 1);
        CHECK(strlist_equal(cmd.source_files, SLIST(&scratch, (char *[]){"main.c", "mathlib.c", "strlib.c"})));
        CHECK(str_equal(cmd.output_file, SL("app")));
        CHECK(strlist_equal(cmd.arguments, SLIST(&scratch, (char *[]){"gcc", "-o", "app"})));
    }

    // Embedded usecase with assembly language
    {
        scratch             = a;
        StrList inputs_list = SLIST(&scratch, (char *[]){
                                                  "arm-none-eabi-gcc",
                                                  "-x",
                                                  "assembler-with-cpp",
                                                  "-c",
                                                  "-mcpu=cortex-m4",
                                                  "-mthumb",
                                                  "-mfpu=fpv4-sp-d16",
                                                  "-mfloat-abi=hard",
                                                  "-DUSE_HAL_DRIVER",
                                                  "-DSTM32G431xx",
                                                  "-ICore/Inc",
                                                  "-IDrivers/STM32G4xx_HAL_Driver/Inc",
                                                  "-IDrivers/STM32G4xx_HAL_Driver/Inc/Legacy",
                                                  "-IDrivers/CMSIS/Device/ST/STM32G4xx/Include",
                                                  "-IDrivers/CMSIS/Include",
                                                  "-Og",
                                                  "-Wall",
                                                  "-fdata-sections",
                                                  "-ffunction-sections",
                                                  "-g",
                                                  "-gdwarf-2",
                                                  "-MMD",
                                                  "-MP",
                                                  "-MFbuild/startup_stm32g431xx.d",
                                                  "startup_stm32g431xx.s",
                                                  "-o",
                                                  "build/startup_stm32g431xx.o",
                                              });

        cmd = compiler_command_from_gcc_tokens(&scratch, inputs_list);
        CHECK(cmd.ok == 1);
        CHECK(strlist_equal(cmd.source_files, SLIST(&scratch, (char *[]){"startup_stm32g431xx.s"})));
        CHECK(str_equal(cmd.output_file, SL("build/startup_stm32g431xx.o")));
        CHECK(strlist_equal(cmd.arguments, SLIST(&scratch, (char *[]){
                                                               "arm-none-eabi-gcc",
                                                               "-x",
                                                               "assembler-with-cpp",
                                                               "-c",
                                                               "-mcpu=cortex-m4",
                                                               "-mthumb",
                                                               "-mfpu=fpv4-sp-d16",
                                                               "-mfloat-abi=hard",
                                                               "-DUSE_HAL_DRIVER",
                                                               "-DSTM32G431xx",
                                                               "-ICore/Inc",
                                                               "-IDrivers/STM32G4xx_HAL_Driver/Inc",
                                                               "-IDrivers/STM32G4xx_HAL_Driver/Inc/Legacy",
                                                               "-IDrivers/CMSIS/Device/ST/STM32G4xx/Include",
                                                               "-IDrivers/CMSIS/Include",
                                                               "-Og",
                                                               "-Wall",
                                                               "-fdata-sections",
                                                               "-ffunction-sections",
                                                               "-g",
                                                               "-gdwarf-2",
                                                               "-MMD",
                                                               "-MP",
                                                               "-MFbuild/startup_stm32g431xx.d",
                                                               "-o",
                                                               "build/startup_stm32g431xx.o",
                                                           })));
    }
}

void null_console_flush(void *ctx, isize len, u8 *ptr)
{
    (void)ctx; // Just a fake
    (void)len;
    (void)ptr;
}

void test_integration(Arena arena)
{
    OsConsoleInterface console   = {.flush = null_console_flush};
    DirectoryStack     dir_stack = {0};

    // Cursed input
    {
        Arena scratch       = arena;
        Str   input         = SL("cd /opt/compiledb_test && printf 't' 1>&2; gcc \\\n -c `test -f 'bad_file.c' || echo "
                                 "'src/'`good_file.c `echo -DNESTED_CMD` &&");
        CommandObjects objs = command_object_next(&scratch, &dir_stack, &input, &console, 0);
        CHECK(objs.len == 1);

        CommandObject obj = objs.ptr[0];
        CHECK(obj.ok == 1);
        CHECK(str_equal(obj.file, SL("good_file.c")));
        CHECK(str_equal(obj.output, SL("")));
        CHECK(str_equal(obj.directory, SL("")));
        CHECK(strlist_equal(obj.arguments, SLIST(&scratch, (char *[]){"gcc", "-c", "good_file.c"})));
    }
}

int main(void)
{
    usize cap = 1 << 24; // 16 MiB
    byte *mem = malloc(cap);
    assert(mem); //< to make static analyzer happy.
    Arena arena = arena_init((isize)cap, mem);

    puts("Running unit tests...");
    {
        test_shell_tokenizer(arena);
        test_compiler_parse(arena);
        test_parse_directory(arena);
        test_compiler_command_from_gcc_tokens(arena);
        test_integration(arena);
    }
    puts("All tests passed!");

    free(mem);
    return 0;
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
#elif defined(FUZZ) // Fuzz testing using AFL++

// On a linux Shell:
//
// $ afl-clang-fast -DFUZZ -std=c23 -g3 -fsanitize=address,undefined -fsanitize-trap  make2combdb.c -o fuzz_me
// $ mkdir tmp
// $ mkdir tmp/i
// $ mkdir tmp/o
// $ echo "gcc main.c -o main" > tmp/i/seed
// $ afl-fuzz -m32T -i tmp/i -o tmp/o ./fuzz_me
//

#include <stdlib.h> //< malloc
#include <string.h> //< memcpy
#include <unistd.h> //< required by afl

void null_console_flush(void *ctx, isize len, u8 *ptr)
{
    (void)ctx; // Just a fake
    (void)len;
    (void)ptr;
}

__AFL_FUZZ_INIT();

int main(void)
{
#ifdef __AFL_HAVE_MANUAL_CONTROL
    __AFL_INIT();
#endif

    usize cap = 1 << 24; // 16 MiB
    byte *mem = malloc(cap);
    assert(mem); //< to make static analyzer happy.

    Arena arena = arena_init((isize)cap, mem);

    StrList cli_args = {0};
    strlist_push_back(&cli_args, &arena, SL("make2compdb"));
#ifdef FUZZ_VERBOSE
    strlist_push_back(&cli_args, &arena, SL("--verbose"));
#endif

    OsConsoleInterface console_interface = {.ctx = NULL, .flush = null_console_flush};
    Str                cwd               = SL("/dev");

    u8 *buf = __AFL_FUZZ_TESTCASE_BUF; //< must be declare after __AFL_INIT and before the __AFL_LOOP

    while (__AFL_LOOP(10'000)) {
        int len = __AFL_FUZZ_TESTCASE_LEN;

        u8 *src = NULL;
        src     = realloc(src, len);
        assert(src);
        memcpy(src, buf, len);

        {
            Arena scratch = arena;

            Str fuzzed_make_stdout = {.ptr = src, .len = len};
            make2compdb(&scratch, &console_interface, cli_args, fuzzed_make_stdout, cwd);
        }

        free(src);
        src = NULL;
    }
    free(mem);
    return 0;
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
#elif defined(_WIN32) // Windows no CRT (from XP to 11)

// :: Str16
// Like `Str` but for utf-16 strings
typedef struct {
    isize len;
    c16  *ptr;
} Str16;

Str16 str16_drop_head(Str16 s, isize offset_from_the_start)
{
    assert(s.ptr);
    offset_from_the_start = clamp_isize(offset_from_the_start, 0, s.len);
    return (Str16){
        .ptr = &s.ptr[offset_from_the_start],
        .len = s.len - offset_from_the_start,
    };
}

// :: Unicode
// Unicode operations
// Modified from Christopher Wellons pck-config.c [1]
// [1] https://github.com/skeeto/w64devkit/blob/master/src/pkg-config.c
enum u32 {
    // U+FFFD (�) is used to replace an unknown, unrecognised, or unrepresentable character.
    REPLACEMENT_CHARACTER = 0xfffd,
};

// Consume a string to produce a codepoint.
u32 codepoint_take_from_utf8(Str *in)
{
    assert(in);
    assert(in->len > 0);

    u32 cp = 0;
    switch (in->ptr[0] & 0xf0) {
    default:
        cp = in->ptr[0];
        if (cp > 0x7f) break;
        *in = str_drop_head(*in, 1);
        return cp;
    case 0xc0:
    case 0xd0:
        if (in->len < 2) break;
        if ((in->ptr[1] & 0xc0) != 0x80) break;

        cp = (u32)(in->ptr[0] & 0x1f) << 6 | (u32)(in->ptr[1] & 0x3f) << 0;
        if (cp < 0x80) break;

        *in = str_drop_head(*in, 2);
        return cp;
    case 0xe0:
        if (in->len < 3) break;
        if ((in->ptr[1] & 0xc0) != 0x80) break;
        if ((in->ptr[2] & 0xc0) != 0x80) break;

        cp = (u32)(in->ptr[0] & 0x0f) << 12 | (u32)(in->ptr[1] & 0x3f) << 6 | (u32)(in->ptr[2] & 0x3f) << 0;
        if (cp < 0x800) break;
        if (cp >= 0xd800 && cp <= 0xdfff) break;

        *in = str_drop_head(*in, 3);
        return cp;
    case 0xf0:
        if (in->len < 4) break;
        if ((in->ptr[1] & 0xc0) != 0x80) break;
        if ((in->ptr[2] & 0xc0) != 0x80) break;
        if ((in->ptr[3] & 0xc0) != 0x80) break;

        cp = (u32)(in->ptr[0] & 0x0f) << 18 | (u32)(in->ptr[1] & 0x3f) << 12 | (u32)(in->ptr[2] & 0x3f) << 6 |
             (u32)(in->ptr[3] & 0x3f) << 0;
        if (cp < 0x10000) break;
        if (cp > 0x10ffff) break;

        *in = str_drop_head(*in, 4);
        return cp;
    }

    // We just assume 1 char is invalid
    cp  = REPLACEMENT_CHARACTER;
    *in = str_drop_head(*in, 1);
    return cp;
}

// Consume a string to produce a codepoint.
u32 codepoint_take_from_utf16(Str16 *in)
{
    assert(in);
    assert(in->len > 0);

    u32 cp = 0;
    if (in->ptr[0] >= 0xdc00 && in->ptr[0] <= 0xdfff) {
        goto reject; // unpaired low surrogate
    }
    else if (in->ptr[0] >= 0xd800 && in->ptr[0] <= 0xdbff) {
        // Surrogate pair !
        if (in->len < 2) {
            goto reject; // missing low surrogate
        }

        u32 hi = in->ptr[0];
        u32 lo = in->ptr[1];
        if (lo < 0xdc00 || lo > 0xdfff) {
            goto reject; // expected low surrogate
        }
        cp  = 0x10000 + ((hi - 0xd800) << 10) + (lo - 0xdc00);
        *in = str16_drop_head(*in, 2);
        return cp;
    }
    else {
        // Single surrogate
        cp  = in->ptr[0];
        *in = str16_drop_head(*in, 1);
        return cp;
    }

reject:
    cp  = REPLACEMENT_CHARACTER;
    *in = str16_drop_head(*in, 1);
    return cp;
}

// Encode codepoint into utf-8 characters.
Str utf8_from_codepoint(u8 mem[static 4], u32 codepoint)
{
    b32 const is_invalid = ((codepoint >= 0xd800 && codepoint <= 0xdfff) || codepoint > 0x10ffff);
    if (is_invalid) {
        codepoint = REPLACEMENT_CHARACTER;
    }

    isize len = 0;
    switch ((codepoint >= 0x80) + (codepoint >= 0x800) + (codepoint >= 0x10000)) {
    case 0:
        mem[0] = (u8)(0x00 | ((codepoint >> 0)));
        len    = 1;
        break;
    case 1:
        mem[0] = (u8)(0xc0 | ((codepoint >> 6)));
        mem[1] = (u8)(0x80 | ((codepoint >> 0) & 63));
        len    = 2;
        break;
    case 2:
        mem[0] = (u8)(0xe0 | ((codepoint >> 12)));
        mem[1] = (u8)(0x80 | ((codepoint >> 6) & 63));
        mem[2] = (u8)(0x80 | ((codepoint >> 0) & 63));
        len    = 3;
        break;
    case 3:
        mem[0] = (u8)(0xf0 | ((codepoint >> 18)));
        mem[1] = (u8)(0x80 | ((codepoint >> 12) & 63));
        mem[2] = (u8)(0x80 | ((codepoint >> 6) & 63));
        mem[3] = (u8)(0x80 | ((codepoint >> 0) & 63));
        len    = 4;
        break;
    default: {
        assert(0);
    }
    }
    return (Str){.len = len, .ptr = mem};
}

// Encode codepoint into utf-16 characters.
Str16 utf16_from_codepoint(u16 mem[static 2], u32 codepoint)
{
    if ((codepoint >= 0xd800 && codepoint <= 0xdfff) || codepoint > 0x10ffff) {
        codepoint = REPLACEMENT_CHARACTER;
    }

    isize len = 0;
    if (codepoint >= 0x10000) {
        codepoint -= 0x10000;
        mem[0]     = (c16)((codepoint >> 10) + 0xd800);
        mem[1]     = (c16)((codepoint & 0x3ff) + 0xdc00);
        len        = 2;
    }
    else {
        mem[0] = (c16)codepoint;
        len    = 1;
    }

    return (Str16){.len = len, .ptr = mem};
}

Str utf8_from_utf16(Arena *perm, Str16 in)
{
    MemBuf buf = membuf_init(perm);

    u8 mem[size_of(u32)] = {0};
    while (in.len > 0) {
        u32 cp = codepoint_take_from_utf16(&in);
        Str s  = utf8_from_codepoint(mem, cp);
        membuf_add_str(&buf, s);
    }
    return membuf_finish(&buf);
}

Str utf8_from_cstr16(Arena *perm, c16 *in)
{
    assert(perm);
    assert(in);

    isize len    = 0;
    c16  *cursor = in;
    while (*cursor != 0) {
        len++;
        cursor++;
    }

    Str16 s = {.len = len, .ptr = in};
    return utf8_from_utf16(perm, s);
}

// :: Windows dependencies
// To interact with the file system and the console, we need to talk to the windows API.
// Typically, this is done by including `windows.h`, but we don't want to do that because
// it would pollutes our namespace and slows the compilation down.
// We instead use this neet technique:
#define W32 [[gnu::dllimport, gnu::stdcall]]

// KERNEL32.dll
W32 void  ExitProcess(i32);
W32 u16  *GetCommandLineW(void);
W32 b32   GetConsoleMode(uptr, int32_t *);
W32 u32   GetCurrentDirectoryW(i32, c16 *);
W32 uptr  GetStdHandle(i32);
W32 b32   ReadFile(uptr, u8 *, i32, i32 *, uptr);
W32 byte *VirtualAlloc(uptr, isize, i32, i32);
W32 i32   WriteConsoleW(uptr, c16 *, i32, i32 *, uptr);
W32 b32   WriteFile(uptr, u8 *, i32, i32 *, uptr);

// SHELL32.dll
W32 u16 **CommandLineToArgvW(u16 *, i32 *);

enum {
    WIN32_STD_INPUT_HANDLE  = -10,
    WIN32_STD_OUTPUT_HANDLE = -11,
    WIN32_STD_ERROR_HANDLE  = -12,

    WIN32_MEM_COMMIT     = 0x1000,
    WIN32_MEM_RESERVE    = 0x2000,
    WIN32_PAGE_READWRITE = 4,
};

typedef struct {
    OsStream *std_in;
    OsStream *std_out;
    OsStream *std_err;
    c16       buffer[1 << 11];
    isize     len;
} WinConsole;

Str os_cwd_read(Arena *perm)
{
    Str cwd = {0};
    {
        isize available_bytes = 0;
        c16  *cwd_u16         = (c16 *)arena_acquire(perm, size_of(*cwd_u16), align_of(*cwd_u16), &available_bytes);
        isize available       = available_bytes / size_of(*cwd_u16);

        u32 count = GetCurrentDirectoryW(to_i32(available), cwd_u16);
        arena_release(perm, count * sizeof(*cwd_u16));

        cwd = utf8_from_utf16(perm, (Str16){count, cwd_u16});
    }
    return cwd;
}

void os_console_flush(WinConsole *console)
{
    c16 *buf     = console->buffer;
    i32  buf_len = to_i32(console->len);
    if (buf_len <= 0 || console->std_out->err) {
        return;
    }

    if (console->std_out->is_console) {
        console->std_out->err = !WriteConsoleW(console->std_out->handle, buf, buf_len, NULL, 0);
    }
    else {
        i32 dummy             = 0;
        console->std_out->err = !WriteFile(console->std_out->handle, (u8 *)buf, (buf_len * size_of(c16)), &dummy, 0);
    }
    console->len = 0;
}

void os_console_write(WinConsole *console, Str in)
{
    c16 mem[2] = {0};
    while (in.len > 0) {
        u32   cp      = codepoint_take_from_utf8(&in);
        Str16 encoded = utf16_from_codepoint(mem, cp);

        isize available = count_of(console->buffer) - console->len;
        if (encoded.len > available) {
            os_console_flush(console);
        }

        assert(count_of(console->buffer) > encoded.len);
        for (isize i = 0; i < encoded.len; ++i) {
            console->buffer[console->len++] = encoded.ptr[i];
        }
    }
}

void os_console_write_wrapper(void *ctx, isize len, u8 *ptr)
{
    WinConsole *console = ctx;
    os_console_write(console, (Str){.len = len, .ptr = ptr});
}

Str os_stdin_read(Arena *perm, OsStream *std_in)
{
    isize available = 0;
    u8   *mem       = (u8 *)arena_acquire(perm, size_of(*mem), align_of(*mem), &available);

    // Some programs (like ffmpeg) have such a big makefile output that it will overflow the pipe.
    // We read in smaller chunks to make sure we can read all of it.
    enum { CHUNK_SIZE = 1 << 14 };

    Str read_stdin = {.len = 0, .ptr = mem};
    i32 count      = 0;
    while (ReadFile(std_in->handle, &read_stdin.ptr[read_stdin.len], CHUNK_SIZE, &count, 0)) {
        if (count == 0) {
            break;
        }

        assert(available > (read_stdin.len + count)); // OOM
        read_stdin.len += count;
    }
    arena_release(perm, read_stdin.len);

    return read_stdin;
}

StrList os_args_read(Arena *perm)
{
    StrList args = {0};

    u16  *cmdline    = GetCommandLineW();
    i32   argc       = 0;
    c16 **win32_argv = CommandLineToArgvW(cmdline, &argc);
    assert(argc >= 1);

    for (isize i = 0; i < argc; ++i) {
        Str arg = utf8_from_cstr16(perm, win32_argv[i]);
        strlist_push_back(&args, perm, arg);
    }
    return args;
}

[[gnu::stdcall]]
void mainCRTStartup(void);

[[gnu::stdcall]]
void mainCRTStartup(void)
{
    isize cap      = 1 << 24; // 16 MiB
    byte *mem      = VirtualAlloc(0, cap, WIN32_MEM_COMMIT | WIN32_MEM_RESERVE, WIN32_PAGE_READWRITE);
    Arena arena[1] = {arena_init(cap, mem)};

    WinConsole *console = ALLOC(arena, 1, *console);

    i32 not_needed = 0;

    OsStream std_in   = {0};
    std_in.handle     = GetStdHandle(WIN32_STD_INPUT_HANDLE);
    std_in.is_console = GetConsoleMode(std_in.handle, &not_needed);
    console->std_in   = &std_in;

    OsStream std_out   = {0};
    std_out.handle     = GetStdHandle(WIN32_STD_OUTPUT_HANDLE);
    std_out.is_console = GetConsoleMode(std_out.handle, &not_needed);
    console->std_out   = &std_out;

    OsStream std_err   = {0};
    std_err.handle     = GetStdHandle(WIN32_STD_ERROR_HANDLE);
    std_err.is_console = GetConsoleMode(std_err.handle, &not_needed);
    console->std_err   = &std_err;

    Str make_stdout = {0};
    if (!std_in.is_console) {
        // Only read the stdin if it's not a console (i.e. a pipe or a file)
        make_stdout = os_stdin_read(arena, &std_in);
    }

    OsConsoleInterface *console_interface = ALLOC(arena, 1, *console_interface);
    console_interface->ctx                = console;
    console_interface->flush              = os_console_write_wrapper;

    StrList cli_args = os_args_read(arena);
    Str     cwd      = os_cwd_read(arena);

    i32 exit_code = make2compdb(arena, console_interface, cli_args, make_stdout, cwd);

    print_flush(console_interface);
    os_console_flush(console);
    ExitProcess(exit_code);
    unreachable();
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
#else // POSIX

#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>

Str os_cwd_read(Arena *perm)
{
    isize available = 0;
    byte *mem       = arena_acquire(perm, size_of(char), align_of(char), &available);

    char *cwd_ptr = getcwd(mem, to_usize(available));
    if (cwd_ptr == NULL) return SL("");

    Str cwd = str_from_cstr(cwd_ptr);
    arena_release(perm, cwd.len);
    return cwd;
}

void os_console_write(OsStream *std_out, Str in)
{
    assert(std_out);
    if (in.len > 0 && !std_out->err) {
        std_out->err = (in.len != write((int)std_out->handle, in.ptr, to_usize(in.len)));
    }
}

void os_console_write_wrapper(void *ctx, isize len, u8 *ptr)
{
    OsStream *std_out = ctx;
    os_console_write(std_out, (Str){.len = len, .ptr = ptr});
}

Str os_stdin_read(Arena *perm, OsStream *std_in)
{
    isize available = 0;
    byte *mem       = arena_acquire(perm, size_of(char), align_of(char), &available);

    // Some programs (like ffmpeg) have such a big makefile stdout that it will overflow the pipe.
    // We read in smaller chunks to make sure we can read all of it.
    enum { CHUNK_SIZE = 1 << 14 };

    Str in = {.len = 0, .ptr = (u8 *)mem};
    while (1) {
        isize len = read((int)std_in->handle, &in.ptr[in.len], CHUNK_SIZE);
        if (len < 0) return SL("");

        if (len == 0) {
            break;
        }
        assert(available > (in.len + len));
        in.len += len;
    }
    arena_release(perm, in.len);

    return in;
}

int main(int argc, char **argv)
{
    isize cap      = 1 << 24; // 16 MiB
    byte *mem      = mmap(0, to_usize(cap), PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    Arena arena[1] = {arena_init(cap, mem)};

    OsStream std_in   = {0};
    std_in.handle     = STDIN_FILENO;
    std_in.is_console = isatty((int)std_in.handle);

    OsStream std_out   = {0};
    std_out.handle     = STDOUT_FILENO;
    std_out.is_console = isatty((int)std_out.handle);

    OsStream std_err   = {0};
    std_err.handle     = STDERR_FILENO;
    std_err.is_console = isatty((int)std_err.handle);

    Str make_stdout = {0};
    if (!std_in.is_console) {
        // Only read the stdin if it's not a console (i.e. a pipe or a file)
        make_stdout = os_stdin_read(arena, &std_in);
    }

    OsConsoleInterface *console_interface = ALLOC(arena, 1, *console_interface);
    console_interface->ctx                = &std_out;
    console_interface->flush              = os_console_write_wrapper;

    StrList cli_args = strlist_from_cstrs(arena, argc, argv);
    Str     cwd      = os_cwd_read(arena);

    i32 exit_code = make2compdb(arena, console_interface, cli_args, make_stdout, cwd);
    print_flush(console_interface);
    _exit(exit_code);
    unreachable();
}

#endif
