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
//        Linux: Go see the code. There's more information there.
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
// REFERENCE
//
//      JSON Compilation Database Format Specification
//      https://clang.llvm.org/docs/JSONCompilationDatabase.html
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

#define VERSION "2026-06-04"

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

#define size_of(A)         ((isize)(sizeof(A)))
#define count_of(A)        ((size_of(A) / size_of(*(A))))
#define type_of(T)         typeof(T)
#define align_of(T)        _Alignof(type_of(T))
#define min(A, B)          (((A) < (B)) ? (A) : (B))
#define make_u16(MSB, LSB) (((MSB) << 8UL) | (LSB))

// Simple assertion - Pretty good if you're using a debugger.
//  `__builtin_trap` will causes the program to terminate abnormally.
#define assert(C)                                                                                                                \
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

static inline byte *memory_set(byte *mem, byte value, isize size)
{
    if (!mem) return mem;
    return __builtin_memset(mem, value, to_usize(size));
}

static inline byte *memory_copy(byte *dest, byte *src, isize size)
{
    if (!dest || !src || size <= 0) return NULL;
    return __builtin_memcpy(dest, src, to_usize(size));
}

static inline b32 is_whitespace(u8 c)
{
    switch (c) {
    case ' ':  // FALLTHROUGH
    case '\n': // FALLTHROUGH
    case '\t': // FALLTHROUGH
    case '\r': return 1;
    default:   return 0;
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
    default:  return 0;
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
} Arena;

static b32 memory_is_in_arena(Arena a, byte *mem)
{
    if (!mem) return 0;
    return a.start <= mem && mem <= a.end;
}

static Arena arena_init(isize capacity, byte memory[static capacity])
{
    Arena a  = {0};
    a.start  = memory;
    a.cursor = memory;
    a.end    = a.cursor + capacity;
    return a;
}

// Modified from Christopher Wellons pck-config.c [1]
// [1] https://github.com/skeeto/w64devkit/blob/master/src/pkg-config.c
static byte *arena_alloc(Arena *a, isize count, isize size, isize align)
{
    assert(NULL != a);

    isize pad = (isize) - (uintptr_t)a->cursor & (align - 1);
    assert(count < (a->end - a->cursor - pad) / size); // Make sure not OOM.

    byte *r    = a->cursor + pad;
    a->cursor += pad + count * size;
    return memory_set(r, 0, count * size);
}

#define SLICE_TYPE(SLICE)             type_of(*(SLICE).ptr)

#define ALLOC(ARENA_PTR, COUNT, TYPE) (type_of(TYPE) *)arena_alloc((ARENA_PTR), (COUNT), size_of(TYPE), align_of(TYPE))

#define ALLOC_SLICE(ARENA_PTR, COUNT, TYPE)                                                                                      \
    {.ptr = (type_of((TYPE).ptr))arena_alloc((ARENA_PTR), (COUNT), size_of(SLICE_TYPE(TYPE)), align_of(SLICE_TYPE(TYPE))),       \
     .len = (COUNT)}

static void arena_reset_to(Arena *a, byte *here)
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
#define STATIC_SL(STR_LITERAL) {.ptr = (u8 *)STR_LITERAL, .len = (size_of(STR_LITERAL) - 1)}
#define SL(STR_LITERAL)        (Str) STATIC_SL(STR_LITERAL)

static b32 str_equal(Str a, Str b)
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

static b32 str_starts_with(Str s, Str start)
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

static b32 str_ends_with(Str s, Str end)
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

static Str str_drop_head(Str s, isize offset_from_the_start)
{
    if (s.len <= 0) return s;
    assert(s.ptr);

    offset_from_the_start = clamp_isize(offset_from_the_start, 0, s.len);
    return (Str){
        .ptr = &s.ptr[offset_from_the_start],
        .len = s.len - offset_from_the_start,
    };
}

static Str str_take_head(Str s, isize offset_from_the_start)
{
    if (s.len <= 0) return s;
    assert(s.ptr);

    offset_from_the_start = clamp_isize(offset_from_the_start, 0, s.len);
    return (Str){
        .ptr = s.ptr,
        .len = offset_from_the_start,
    };
}

static Str str_drop_tail(Str s, isize offset_from_the_end)
{
    if (s.len <= 0) return s;
    assert(s.ptr);

    offset_from_the_end = clamp_isize(offset_from_the_end, 0, s.len - 1);
    return (Str){
        .ptr = s.ptr,
        .len = s.len - offset_from_the_end,
    };
}

static Str str_trim_prefix(Str s, Str prefix)
{
    if (str_starts_with(s, prefix)) {
        return str_drop_head(s, prefix.len);
    }
    return s;
}

static Str str_trim_postfix(Str s, Str postfix)
{
    if (str_ends_with(s, postfix)) {
        return str_drop_tail(s, postfix.len);
    }
    return s;
}

static Str str_trim_prefix_if(Str s, b32 (*predicate)(u8 c))
{
    isize i = 0;
    for (; i < s.len; ++i) {
        if (!predicate(s.ptr[i])) break;
    }
    return str_drop_head(s, i);
}

static Str str_trim_postfix_if(Str s, b32 (*predicate)(u8 c))
{
    isize i = 0;
    for (; i < s.len; ++i) {
        isize index = (s.len - 1) - i;
        if (!predicate(s.ptr[index])) break;
    }
    return str_drop_tail(s, i);
}

static Str str_trim(Str s, Str pattern)
{
    s = str_trim_prefix(s, pattern);
    s = str_trim_postfix(s, pattern);
    return s;
}

static Str str_trim_if(Str s, b32 (*predicate)(u8 c))
{
    s = str_trim_prefix_if(s, predicate);
    s = str_trim_postfix_if(s, predicate);
    return s;
}

// When found, return the index of the `delim`. Otherwise, return a negative number.
static isize str_find(Str s, Str delim)
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
            if (str_starts_with(search_from_here, delim)) {
                return i;
            }
        }
    }
    return -1; // Not found
}

// Will stop if it sees a char inside the `char_list`.
static isize str_find_any(Str s, Str char_list)
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

// Like `str_reverse_find_any`, but from the end.
static isize str_reverse_find_any(Str s, Str char_list)
{
    if (!s.ptr || !char_list.ptr) {
        return -1; // Invalid string can't be found
    }
    else if (s.len < 1) {
        return -1; // Impossible to find
    }
    else {
        for (isize i = s.len - 1; i >= 0; i--) {
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

static b32 str_contains_any(Str s, Str any)
{
    isize index = str_find_any(s, any);
    return index >= 0;
}

static u8 str_peek(Str s)
{
    u8 c = '\0';
    if (0 < s.len) {
        c = s.ptr[0];
    }
    return c;
}

static u8 str_pop(Str *s)
{
    u8 c = '\0';
    if (s && (0 < s->len)) {
        c = s->ptr[0];
        s->ptr++;
        s->len--;
    }
    return c;
}

static u8 str_set(Str *s, isize index, u8 value)
{
    u8 prev = 0;
    if (s && 0 <= index && index < s->len) {
        prev          = s->ptr[index];
        s->ptr[index] = value;
    }
    return prev;
}

static Str str_duplicate(Arena *perm, Str to_copy)
{
    Str dest = ALLOC_SLICE(perm, to_copy.len, dest);
    memory_copy((byte *)dest.ptr, (byte *)to_copy.ptr, to_copy.len);
    return dest;
}

static Str str_concat(Arena *perm, Str head, Str tail)
{
    if (head.ptr == NULL && memory_is_in_arena(*perm, (byte *)tail.ptr)) {
        // This is an optimisation if you're trying to concat to an empty slice.
        //
        //      Str a = {0};
        //      Str b = return_some_str(arena);
        //      a = str_concat(a, b); //< no str_duplicate
        //
        head = tail;
    }
    else {
        byte *head_position = (head.len > 0) ? (byte *)(head.ptr + head.len) : NULL;

        if (head_position != perm->cursor) {
            // If `head` is not at the end of the arena, we need copy it to make the concat operation possible.
            head = str_duplicate(perm, head);
        }

        assert((byte *)(head.ptr + head.len) == perm->cursor);
        head.len += str_duplicate(perm, tail).len;
    }
    return head;
}

// Remove escape character from a string.
static Str str_unescape(Arena *perm, Str s)
{
    isize index    = 0;
    Str   unescape = ALLOC_SLICE(perm, s.len, unescape);

    while (s.len > 0) {
        u8 c = str_pop(&s);
        switch (c) {
        case '\'': break; //< Unquote
        case '\"': break; //< Unquote
        case '\\': {
            u8 p = str_pop(&s);
            switch (p) {
            case '"':  str_set(&unescape, index++, '"'); break;
            case '\\': str_set(&unescape, index++, '\\'); break;
            case '/':  str_set(&unescape, index++, '/'); break;
            case 'b':  str_set(&unescape, index++, '\b'); break;
            case 'f':  str_set(&unescape, index++, '\f'); break;
            case 'n':  str_set(&unescape, index++, '\n'); break;
            case 'r':  str_set(&unescape, index++, '\r'); break;
            case 't':  str_set(&unescape, index++, '\t'); break;
            case ' ':  str_set(&unescape, index++, ' '); break;
            }
            break;
        }
        default: {
            str_set(&unescape, index++, c);
            break;
        }
        }
    }

    unescape = str_take_head(unescape, index);
    arena_reset_to(perm, (byte *)unescape.ptr + unescape.len); // reclaim unused memory
    return unescape;
}

static b32 str_is_source_file(Str maybe_source)
{
    static Str extensions[] = {
        // C
        STATIC_SL(".c"),
        // C++
        STATIC_SL(".cc"),
        STATIC_SL(".cpp"),
        STATIC_SL(".cxx"),
        STATIC_SL(".c++"),
        STATIC_SL(".C"),
        STATIC_SL(".cppm"),
        // Assembly
        STATIC_SL(".s"),
        STATIC_SL(".S"),
    };

    // Unquote
    maybe_source = str_trim_postfix(maybe_source, SL("\""));
    maybe_source = str_trim_postfix(maybe_source, SL("\'"));
    maybe_source = str_trim_postfix(maybe_source, SL("`"));

    for (isize i = 0; i < count_of(extensions); i++) {
        if (str_ends_with(maybe_source, extensions[i])) {
            return 1;
        }
    }
    return 0;
}

// :: Str_Pair
typedef struct {
    // If the `delim` was seen.
    b32 ok;
    // First part of the cut.
    Str head;
    // Last part of the cut.
    Str tail;
} Str_Pair;

// Cut a string in half.
// If the `delim` was not seen, `head` contains everything.
// If the `delim` was not seen, `tail` contains nothing.
static Str_Pair str_cut(Str s, Str delim)
{
    isize delim_start_index = str_find(s, delim);
    if (delim_start_index < 0) { // Not found
        return (Str_Pair){
            .ok   = 0,
            .head = s,
            .tail = (Str){0},
        };
    }
    else {
        return (Str_Pair){
            .ok   = 1,
            .head = str_take_head(s, delim_start_index),
            .tail = str_drop_head(s, delim_start_index + delim.len),
        };
    }
}

// Cut a string in half, starting at the end.
// If the `delim` was not seen, `head` contains nothing.
// If the `delim` was not seen, `tail` contains everything.
static Str_Pair str_reverse_cut_any(Str s, Str char_list)
{
    isize delim_start_index = str_reverse_find_any(s, char_list);
    if (delim_start_index < 0) { // Not found
        return (Str_Pair){
            .ok   = 0,
            .head = (Str){0},
            .tail = s,
        };
    }
    else {
        return (Str_Pair){
            .ok   = 1,
            .head = str_take_head(s, delim_start_index),
            .tail = str_drop_head(s, delim_start_index + 1),
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

static b32 strlist_is_empty(StrList sl) { return sl.front == NULL; }

static Str strlist_node_remove(StrListNode *node)
{
    assert(node);

    Str s      = node->str;
    node->str  = (Str){0};
    node->prev = NULL;
    node->next = NULL;
    return s;
}

static void strlist_push_back(StrList *sl, Arena *a, Str s)
{
    assert(sl);

    StrListNode *new_node = ALLOC(a, 1, *new_node);
    new_node->str         = s;
    if (strlist_is_empty(*sl)) {
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

static Str strlist_peek_back(StrList sl)
{
    Str back = {0};
    if (strlist_is_empty(sl)) {
        return back;
    }
    else {
        StrListNode *back_node = sl.back;
        back                   = back_node->str;
    }
    return back;
}

static Str strlist_pop_front(StrList *sl)
{
    assert(sl);

    Str front = {0};
    if (strlist_is_empty(*sl)) {
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

static Str strlist_pop_back(StrList *sl)
{
    assert(sl);

    Str back = {0};
    if (strlist_is_empty(*sl)) {
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

static StrList strlist_copy(StrList sl, Arena *perm)
{
    StrList copy = {0};
    if (strlist_is_empty(sl)) {
        return copy;
    }

    for (StrListNode *node = sl.front; node != NULL; node = node->next) {
        strlist_push_back(&copy, perm, node->str);
    }
    return copy;
}

// :: StrHashTable
// Small MSI hash table for comparing strings efficiently.
#define STR_LOOKUP_EXP 8
typedef struct {
    Str   table[1 << STR_LOOKUP_EXP];
    isize count;
} StrHashTable;

static isize strht_get_index(u32 hash, isize index)
{
    u32 mask = ((u32)1 << STR_LOOKUP_EXP) - 1;
    u32 step = (hash >> (32 - STR_LOOKUP_EXP)) | 1;
    index    = (index + step) & mask;
    return index;
}

static b32 strht_lookup(StrHashTable *shs, Str s)
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

static b32 strht_insert(StrHashTable *shs, Str s)
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

static void dirstack_push(DirectoryStack *ds, Str directory)
{
    Str directory_copy = str_duplicate(&ds->arena, directory);
    strlist_push_back(&ds->stack, &ds->arena, directory_copy);
}

static Str dirstack_peek(DirectoryStack ds, Arena *perm)
{
    Str directory      = strlist_peek_back(ds.stack);
    Str directory_copy = str_duplicate(perm, directory);
    return directory_copy;
}

static Str dirstack_pop(DirectoryStack *ds, Arena *perm)
{
    Str directory = strlist_pop_back(&ds->stack);
    if (directory.len > 0) {
        Str directory_copy = str_duplicate(perm, directory);
        arena_reset_to(&ds->arena, (byte *)directory.ptr);
        return directory_copy;
    }
    return directory;
}

// :: OS
// Operating system buffered writer interface.
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
} OsWriterInterface;

static void print_flush(OsWriterInterface *w)
{
    assert(w);
    assert(w->buf.len <= count_of(w->buf.mem));

    w->flush(w->ctx, w->buf.len, w->buf.mem);
    w->buf.len = 0;
}

static void print_tab(OsWriterInterface *w)
{
    assert(w);

#define SPACE " "
#define TAB   SPACE SPACE SPACE SPACE

    if (w->tab < 0) {
        w->tab = 0;
    }

    Str   tab      = SL(TAB);
    isize required = tab.len * w->tab;
    assert(count_of(w->buf.mem) >= required); //< Buffer should be big enough to have all the tabs.

    isize available = count_of(w->buf.mem) - w->buf.len;
    if (available < required) {
        print_flush(w);
    }

    for (isize _ = 0; _ < w->tab; ++_) {
        for (isize i = 0; i < tab.len; ++i) {
            w->buf.mem[w->buf.len]  = tab.ptr[i];
            w->buf.len             += 1;
        }
    }

#undef SPACE
#undef TAB
}

static void print_u8(OsWriterInterface *w, u8 c)
{
    assert(w);

    if (w->eol) {
        print_tab(w);
        w->eol = 0;
    }

    isize available = count_of(w->buf.mem) - w->buf.len;
    if (available <= 0) {
        print_flush(w);
    }

    w->buf.mem[w->buf.len]  = c;
    w->buf.len             += 1;
    if (c == '\n') {
        w->eol = 1;
    }
}

static void print_str(OsWriterInterface *w, Str s)
{
    assert(w);

    while (s.len > 0) {
        isize available = count_of(w->buf.mem) - w->buf.len;
        if (available <= 0) {
            print_flush(w);
            continue;
        }

        isize write_len = min(available, s.len);
        for (isize i = 0; i < write_len; ++i) {
            print_u8(w, s.ptr[i]);
        }
        s = str_drop_head(s, write_len);
    }
}

static void println_str(OsWriterInterface *w, Str s)
{
    print_str(w, s);
    print_u8(w, '\n');
}

// Will escape special characters.
static void print_str_escaped_string(OsWriterInterface *w, Str s)
{
    assert(w);

    print_u8(w, '\"');
    while (s.len > 0) {
        u8 c = str_pop(&s);
        switch (c) {
        case '\"': print_str(w, SL("\\\"")); break;
        case '\n': print_str(w, SL("\\n")); break;
        case '\r': print_str(w, SL("\\r")); break;
        case '\t': print_str(w, SL("\\t")); break;
        case '\v': print_str(w, SL("\\v")); break;
        case '\b': print_str(w, SL("\\b")); break;
        case '\f': print_str(w, SL("\\f")); break;
        case '\a': print_str(w, SL("\\a")); break;
        case '\\': print_str(w, SL("\\\\")); break;
        default:   print_u8(w, c); break;
        }
    }
    print_u8(w, '\"');
}

static void println_str_escaped_string(OsWriterInterface *w, Str s)
{
    print_str_escaped_string(w, s);
    print_u8(w, '\n');
}

static void print_b32(OsWriterInterface *w, b32 b)
{
    print_str(w, b ? SL("true") : SL("false")); //
}

static void println_b32(OsWriterInterface *w, b32 b)
{
    print_b32(w, b);
    print_u8(w, '\n');
}

static void print_number(OsWriterInterface *w, isize number)
{
    assert(w);

    u8 buffer[sizeof("18446744073709551616")] = {0}; // (1 << 64)

    b32   negative = number < 0;
    usize uval     = negative ? ((usize)(0 - number)) : ((usize)number);

    int len = 0;
    do {
        buffer[len++]  = (u8)('0' + (uval % 10));
        uval          /= 10;
    } while (uval);

    if (negative) print_u8(w, '-');
    for (isize i = len - 1; i >= 0; --i) {
        print_u8(w, buffer[i]);
    }
}

static void println_number(OsWriterInterface *w, isize number)
{
    print_number(w, number);
    print_u8(w, '\n');
}

static void print_strlist(OsWriterInterface *w, StrList sl)
{
    assert(w);

    print_str(w, SL("["));
    for (StrListNode *node = sl.front; node != NULL; node = node->next) {
        if (node != sl.front) {
            print_str(w, SL(", "));
        }
        print_str_escaped_string(w, node->str);
    }
    print_str(w, SL("]"));
}

static void println_strlist(OsWriterInterface *w, StrList sl)
{
    print_strlist(w, sl);
    print_u8(w, '\n');
}

// :: ParsingMode
// Each line has a parsing mode which will dictate how we tokenize the chars.
typedef enum {
    PARSING_MODE_SHELL = 0,
    PARSING_MODE_MAKE_ENTER_DIR,
    PARSING_MODE_MAKE_LEAVE_DIR,
} ParsingMode;

static ParsingMode identify_parsing_mode(Str make_stdout)
{
    // Our strategy is to try to identify the following patterns:
    // 1. make: Entering directory 'C:/dev/gb/make2compdb/impl_c'
    // 2. make: Leaving directory 'C:/dev/gb/make2compdb/impl_c'
    //
    // If we didn't find those element, we assume this is a shell command.
    Str_Pair cut = {0};
    cut          = str_cut(make_stdout, SL(" "));
    Str first    = cut.head;
    cut          = str_cut(cut.tail, SL(" "));
    Str second   = cut.head;
    cut          = str_cut(cut.tail, SL(" "));
    Str third    = cut.head;

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

// :: Directory Parsing
static Str directory_from_make_dir_line(Str *make_stdout)
{
    Str dir = SL("");

    //       We want to go here |
    //                          v
    // make: Entering directory 'C:/dev/gb/make2compdb/impl_c'
    //
    Str input = *make_stdout;
    input     = str_cut(input, SL(" ")).tail; //< skip "make:"
    input     = str_cut(input, SL(" ")).tail; //< skip "Entering"

    if (str_starts_with(input, SL("directory"))) {
        input = str_cut(input, SL(" ")).tail; //< skip "directory"

        input     = str_trim_prefix_if(input, is_whitespace);
        Str delim = str_take_head(input, 1);

        // For some reason, certain version of make use different string delimiters.
        // Even stranger, sometimes the delimeter U8_pairs don't match!
        Str valid_delims = SL("\'\"`");

        if (str_contains_any(delim, valid_delims)) {
            input            = str_drop_head(input, 1);
            isize next_delim = str_find_any(input, valid_delims);

            dir = str_take_head(input, next_delim);
            dir = str_trim_if(dir, is_whitespace);
        }
    }

    *make_stdout = str_cut(input, SL("\n")).tail;
    return dir;
}

// :: Shell Definition
// https://www.gnu.org/savannah-checkouts/gnu/bash/manual/bash.html#Definitions-1

// control operator - A token that performs a control function.
// NOTE: This list needs to be order longer token first.
static Str g_shell_control_operators[] = {
    STATIC_SL(";;&"), STATIC_SL(";;"), STATIC_SL(";&"), STATIC_SL(";"), STATIC_SL("&&"), STATIC_SL("&"),
    STATIC_SL("||"),  STATIC_SL("|&"), STATIC_SL("|"),  STATIC_SL("("), STATIC_SL(")"),
};

// Before a command is executed, its input and output may be redirected using a special notation.
// NOTE: This list needs to be order longer token first.
static Str g_shell_redirects_operators[] = {
    STATIC_SL("&>>"), STATIC_SL("&>|"), STATIC_SL("&>"), STATIC_SL(">|"), STATIC_SL("<<<"), STATIC_SL("<<-"), STATIC_SL(">>"),
    STATIC_SL("<<"),  STATIC_SL(">&"),  STATIC_SL("<&"), STATIC_SL("<>"), STATIC_SL(">"),   STATIC_SL("<"),
};

// :: Scanner
// Helper to consume a `input` string.
typedef struct {
    Str   input;
    isize position;
} Scanner;

static b32 scanner_at_start(Scanner sc)
{
    return sc.position == 0; //
}

static b32 scanner_at_end(Scanner sc)
{
    return sc.position >= sc.input.len; //
}

static u8 scanner_peek(Scanner sc)
{
    u8 c = 0;
    if (0 <= sc.position && sc.position < sc.input.len) {
        c = sc.input.ptr[sc.position];
    }
    return c;
}

static void scanner_next(Scanner *sc)
{
    if (sc->position < sc->input.len) {
        sc->position += 1;
    }
}

static u8 scanner_pop(Scanner *sc)
{
    u8 c = scanner_peek(*sc);
    scanner_next(sc);
    return c;
}

static void scanner_rewind_by(Scanner *sc, isize offset_from_end)
{
    sc->position -= offset_from_end;
    sc->position  = clamp_isize(sc->position, 0, sc->input.len);
}

static Str scanner_rest(Scanner sc)
{
    sc.position = clamp_isize(sc.position, 0, sc.input.len);
    return (Str){
        .ptr = &sc.input.ptr[sc.position],
        .len = sc.input.len - sc.position,
    };
}

static b32 scanner_match(Scanner *sc, Str m)
{
    Str cursor = scanner_rest(*sc);
    if (str_starts_with(cursor, m)) {
        sc->position += m.len;
        return 1;
    }
    return 0;
}

static b32 scanner_match_u8(Scanner *sc, u8 m)
{
    return scanner_match(sc, (Str){.ptr = &m, .len = 1}); //
}

static Str scanner_finish(Scanner *sc)
{
    return (Str){
        .ptr = sc->input.ptr,
        .len = sc->position,
    };
}

static b32 scanner_match_control_char(Scanner *sc)
{
    for (isize i = 0; i < count_of(g_shell_control_operators); ++i) {
        Str op = g_shell_control_operators[i];
        if (scanner_match(sc, op)) return 1;
    }
    return 0;
}

static b32 scanner_match_redirect(Scanner *sc)
{
    for (isize i = 0; i < count_of(g_shell_redirects_operators); ++i) {
        Str op = g_shell_redirects_operators[i];
        if (scanner_match(sc, op)) return 1;
    }
    return 0;
}

// :: Shell
// The `make` program is basically a shell invocation program.
// Therefore, we need to implement a shell parser.

static b32 shell_token_is_control_operator(Str token)
{
    if (token.len <= 0) return 0;

    for (isize i = 0; i < count_of(g_shell_control_operators); ++i) {
        Str op = g_shell_control_operators[i];
        if (str_equal(token, op)) return 1;
    }
    return 0;
}

static b32 shell_token_is_redirect_operator(Str token)
{
    if (token.len <= 0) return 0;

    // Skip the optional number, before the redirect operator
    while (is_numeric(str_peek(token))) {
        str_pop(&token);
    }

    for (isize i = 0; i < count_of(g_shell_redirects_operators); ++i) {
        Str op = g_shell_redirects_operators[i];
        if (str_equal(token, op)) return 1;
    }
    return 0;
}

static b32 shell_token_is_shell_expansion(Str token)
{
    if (token.len <= 0) return 0;

    return (str_starts_with(token, SL("${")) && str_ends_with(token, SL("}")));
}

static b32 shell_token_is_shell_substitution(Str token)
{
    if (token.len <= 0) return 0;

    return (str_starts_with(token, SL("`")) && str_ends_with(token, SL("`"))) ||
           (str_starts_with(token, SL("$(")) && str_ends_with(token, SL(")"))) || //
           (str_starts_with(token, SL("$")) && token.len > 1);
}

static Str shell_tokenize_next(Str *shell_input, b32 *eol)
{
    enum : u16 {
        NORMAL = 0,
        IN_QUOTE,              // '...'
        IN_DOUBLE_QUOTE,       // "..."
        IN_BACKTICK,           // `...`
        IN_BRACES,             // {...} or ${...}
        IN_PARENTHESES,        // (...) or $(...)
        IN_DOUBLE_PARENTHESES, // ((...))
        IN_BRACKETS,           // [...]
        IN_DOUBLE_BRACKETS,    // [[...]]
    } mode = NORMAL;

    Scanner sc    = {.input = *shell_input};
    Str     token = {0};
    while (!scanner_at_end(sc)) {
        u8 c = scanner_pop(&sc);

        switch (make_u16(mode, c)) {
        case make_u16(IN_QUOTE, '\''):             mode = NORMAL; break;
        case make_u16(IN_DOUBLE_QUOTE, '\"'):      mode = NORMAL; break;
        case make_u16(IN_BACKTICK, '`'):           mode = NORMAL; break;
        case make_u16(IN_BRACES, '}'):             mode = NORMAL; break;
        case make_u16(IN_PARENTHESES, ')'):        mode = NORMAL; break;
        case make_u16(IN_DOUBLE_PARENTHESES, ')'): mode = IN_PARENTHESES; break;
        case make_u16(IN_BRACKETS, ']'):           mode = NORMAL; break;
        case make_u16(IN_DOUBLE_BRACKETS, ']'):    mode = IN_BRACKETS; break;
        default:                                   {
            if (mode != NORMAL) break;

            switch (c) {
            case '\'': mode = IN_QUOTE; break;
            case '\"': mode = IN_DOUBLE_QUOTE; break;
            case '`':  mode = IN_BACKTICK; break;
            case '{':  mode = IN_BRACES; break;
            case '(':  {
                if (scanner_match_u8(&sc, '(')) {
                    mode = IN_DOUBLE_PARENTHESES;
                }
                else {
                    mode = IN_PARENTHESES;
                }
                break;
            }
            case '[': {
                if (scanner_match_u8(&sc, '[')) {
                    mode = IN_DOUBLE_BRACKETS;
                }
                else {
                    mode = IN_BRACKETS;
                }
                break;
            }
            case '&': // FALLTHROUGHds
            case ';': // FALLTHROUGHds
            case '|': {
                scanner_rewind_by(&sc, 1);
                if (scanner_at_start(sc)) {
                    if (scanner_match(&sc, SL("&>>")) || //
                        scanner_match(&sc, SL("&>|")) || //
                        scanner_match(&sc, SL("&>")) ||  //
                        scanner_match_control_char(&sc)) {
                        token = scanner_finish(&sc);
                        goto token_done;
                    }
                }
                else {
                    token = scanner_finish(&sc);
                    goto token_done;
                }
                unreachable();
            }
            case '>': // FALLTHROUGH
            case '<': {
                scanner_rewind_by(&sc, 1);
                if (scanner_at_start(sc)) {
                    if (scanner_match_redirect(&sc)) {
                        token = scanner_finish(&sc);
                        goto token_done;
                    }
                }
                else {
                    token = scanner_finish(&sc);
                    goto token_done;
                }
                unreachable();
            }
            case '0': // FALLTHROUGH
            case '1': // FALLTHROUGH
            case '2': // FALLTHROUGH
            case '3': // FALLTHROUGH
            case '4': // FALLTHROUGH
            case '5': // FALLTHROUGH
            case '6': // FALLTHROUGH
            case '7': // FALLTHROUGH
            case '8': // FALLTHROUGH
            case '9': {
                b32 at_start = sc.position == 1;

                // Eat all numbers
                while (is_numeric(scanner_peek(sc))) {
                    scanner_next(&sc);
                }

                if (at_start && scanner_match_redirect(&sc)) {
                    // We just found a redirect with a leading number (e.g. 2&>)
                    token = scanner_finish(&sc);
                    goto token_done;
                }
                else {
                    break; // It was just a numbers, continue.
                }
            }
            case '\\': {
                b32 at_start = sc.position == 1;

                // line continuation
                isize position = sc.position;
                if (scanner_match(&sc, SL("\r\n")) || scanner_match(&sc, SL("\n"))) {
                    if (at_start) {
                        token = (Str){0}; // Delete this token
                        goto token_done;
                    }
                    else {
                        isize size_to_drop = 1 + (sc.position - position); //< \\+\n

                        token = str_drop_tail(scanner_finish(&sc), size_to_drop);
                        goto token_done;
                    }
                }
                else { // just eat the escaped the character
                    scanner_next(&sc);
                    break;
                }
            }
            case '\r': {
                if (scanner_match_u8(&sc, '\n')) {
                    *eol  = 1;
                    token = str_drop_tail(scanner_finish(&sc), 2);
                    goto token_done;
                }
                [[fallthrough]];
            }
            case '\n': *eol = 1; [[fallthrough]];
            case ' ':  // FALLTHROUGH
            case '\t': {
                b32 at_start = sc.position == 1;
                if (!eol && at_start) {
                    break; // Skip leading whitespace
                }
                else {
                    // We drop the whitespace
                    token = str_drop_tail(scanner_finish(&sc), 1);
                    goto token_done;
                }
            }
            }
        } // END: case NORMAL
        } // END: switch(mode)
    } // END: while()

    assert(scanner_at_end(sc));
    assert(token.len == 0);
    *eol  = 1;
    token = scanner_finish(&sc);

token_done:

    *shell_input = scanner_rest(sc);
    return str_trim_if(token, is_whitespace);
}

static StrList shell_tokenize_logical_line(Arena *perm, Str *shell_input)
{
    StrList tokens = {0};

    b32 end_of_line = 0;
    while (!end_of_line && (shell_input->len > 0)) {
        Str token = shell_tokenize_next(shell_input, &end_of_line);
        if (token.len > 0) {
            strlist_push_back(&tokens, perm, token);
        }
    }
    return tokens;
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
} CompilerKind;

static CompilerKind compiler_get_platform_default(void)
{
    // TODO: I'm not sure what's the best strategy here
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

static CompilerKind compiler_parse(Str input)
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

    input = str_trim_if(input, is_whitespace);
    input = str_trim(input, SL("\""));
    input = str_trim(input, SL("'"));
    input = str_trim_postfix(input, SL(".exe"));
    input = str_trim_postfix(input, SL(".EXE"));

    // If there's a path, only keep the executable
    input = str_reverse_cut_any(input, SL("\\/")).tail;

    Str_Pair segment = {.tail = input};
    while (segment.tail.len > 0) {
        segment = str_cut(segment.tail, SL("-"));

        if (str_equal(segment.head, SL("clang"))) {
            return COMPILER_IS_CLANG;
        }
        else if (str_equal(segment.head, SL("gcc")) || str_equal(segment.head, SL("g++"))) {
            return COMPILER_IS_GCC;
        }
        else if (str_equal(segment.head, SL("cc")) || (str_equal(segment.head, SL("c++")))) {
            return compiler_get_platform_default();
        }
        else if (str_equal(segment.head, SL("zig"))) {
            return COMPILER_IS_ZIG_CC;
        }
        else if (str_equal(segment.head, SL("cl"))) {
            return COMPILER_IS_CL;
        }
    }

    return COMPILER_IS_UNKNOWN;
}

static void print_compiler(OsWriterInterface *w, CompilerKind compiler)
{
    Str compiler_str = {0};
    switch (compiler) {
    default: // FALLTHROUGH
    case COMPILER_IS_UNKNOWN: compiler_str = SL("unknown"); break;
    case COMPILER_IS_GCC:     compiler_str = SL("gcc"); break;
    case COMPILER_IS_CLANG:   compiler_str = SL("clang"); break;
    case COMPILER_IS_ZIG_CC:  compiler_str = SL("zig cc"); break;
    case COMPILER_IS_CL:      compiler_str = SL("cl.exe"); break;
    }
    static_assert(COMPILER_COUNT == 5, "This switch needs to be exhaustive");
    print_str(w, compiler_str);
}

static void println_compiler(OsWriterInterface *w, CompilerKind compiler)
{
    print_compiler(w, compiler);
    print_u8(w, '\n');
}

// :: CompilerInvocation
// Represent a compiler invocation like `gcc -o main.exe main.c`
// e.g. "gcc main.c -o main"
typedef struct {
    CompilerKind compiler;
    StrList      tokens;
} CompilerInvocation;

static CompilerInvocation compiler_invocation_from_shell_line(Arena *perm, StrList *line_tokens, OsWriterInterface *w,
                                                              b32 verbose)
{
    enum {
        SEARCH_COMPILER = 0,
        BUILD_INVOCATION,
    } state = SEARCH_COMPILER;

    CompilerKind compiler = COMPILER_IS_UNKNOWN;
    StrList      tokens   = {0};

    if (verbose) {
        println_str(w, SL("Tokenized shell line:"));
        w->tab += 1;
        println_strlist(w, *line_tokens);
        w->tab -= 1;
    }

    while (!strlist_is_empty(*line_tokens)) {
        Str token = strlist_pop_front(line_tokens);

        switch (state) {
        case SEARCH_COMPILER: {
            compiler = compiler_parse(token);
            if (COMPILER_IS_UNKNOWN != compiler) {
                Str unescaped_token = str_unescape(perm, token);
                strlist_push_back(&tokens, perm, unescaped_token);
                state = BUILD_INVOCATION;
            }
            break;
        }
        case BUILD_INVOCATION: {
            if (shell_token_is_control_operator(token)) {
                goto end_of_invocation;
            }
            else if (shell_token_is_redirect_operator(token)) {
                // Delete the redirect file
                strlist_pop_front(line_tokens);
            }
            else if (shell_token_is_shell_substitution(token)) {
                continue; // Currrently we ignore subshell expression
            }
            else if (shell_token_is_shell_expansion(token)) {
                continue; // Currrently we ignore shell expansion
            }
            else {
                Str unescaped_token = str_unescape(perm, token);
                strlist_push_back(&tokens, perm, unescaped_token);
            }
            break;
        }
        } // END: switch(state)
    } // END: for

end_of_invocation:

    CompilerInvocation invo = {0};
    invo.compiler           = compiler;
    invo.tokens             = tokens;
    return invo;
}

static void print_compiler_invocation(OsWriterInterface *w, CompilerInvocation invo)
{
    print_str(w, SL("Compiler: "));
    println_compiler(w, invo.compiler);
    print_str(w, SL("Tokens: "));
    print_strlist(w, invo.tokens);
}

static void println_compiler_invocation(OsWriterInterface *w, CompilerInvocation invo)
{
    print_compiler_invocation(w, invo);
    print_u8(w, '\n');
}

// :: CompilerCommand
// A compiler command is a parsed version of `CompilerInvocation`.
typedef struct {
    b32 ok;
    // e.g. "main.c"
    StrList source_files;
    // e.g. "main.exe"
    Str output_file; //< Optional
    // e.g. "-Wall"
    StrList arguments;
} CompilerCommand;

// A "consumer" flag will consume the next token.
// e.g. "-D" will consume the next token as a preprocessor definition.
static b32 is_gcc_consumer_flag(Str token)
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

    u8 c = str_peek(token);
    if (c != '-') return 0; // A flag always start with '-'

    b32 flag_found = strht_lookup(&flags_lookup, token);
    return flag_found;
}

static CompilerCommand compiler_command_from_gcc_invocation(Arena *perm, StrList tokens)
{
    assert(perm);

    StrList args = strlist_copy(tokens, perm);

    StrList source_files = {0};
    Str     output_file  = SL("");

    b32 ignore_file_extension = 0;
    while (!strlist_is_empty(tokens)) {
        Str token = strlist_pop_front(&tokens);

        if (str_equal(SL("-o"), token) || str_equal(SL("--output"), token)) {
            // -o file
            // --output file
            output_file = strlist_pop_front(&tokens);
        }
        else if (str_starts_with(token, SL("--output="))) {
            // --output=file
            Str file    = str_drop_head(token, SL("--output=").len);
            output_file = file;
        }
        else if (token.len > 2 && (str_starts_with(token, SL("-o")))) {
            // "-omyprogram.exe"
            Str file    = str_drop_head(token, SL("-o").len);
            output_file = file;
        }
        else if (str_equal(token, SL("-x")) || str_equal(token, SL("--language"))) {
            // -x language
            // --language language
            token = strlist_pop_front(&tokens);
            if (str_equal(token, SL("none"))) {
                ignore_file_extension = 0;
            }
            else {
                ignore_file_extension = 1;
            }
        }
        else if (str_starts_with(token, SL("--language="))) {
            // --language=language
            Str lang = str_drop_head(token, SL("--language=").len);
            if (str_equal(lang, SL("none"))) {
                ignore_file_extension = 0;
            }
            else {
                ignore_file_extension = 1;
            }
        }
        else if (is_gcc_consumer_flag(token)) {
            // A "consumer flag" would be the "-D" in: {"-D" "MY_PREPROCESSOR"}
            strlist_pop_front(&tokens); // Consume
        }
        else if (!str_starts_with(token, SL("-"))) {
            // Either this is a flag we don't care about or this is our target file
            if (str_is_source_file(token)) {
                strlist_push_back(&source_files, perm, token);
            }
            else if (ignore_file_extension) {
                // Because the language flag has been raised, we can't use the file extension to know
                // if the file is a target. So the best we can do is assume it is.
                strlist_push_back(&source_files, perm, token);
            }
        }
    }

    CompilerCommand commands = {0};
    commands.arguments       = args;
    commands.source_files    = source_files;
    commands.output_file     = output_file;
    commands.ok              = (args.count > 0) && (source_files.count > 0);
    return commands;
}

static CompilerCommand compiler_command_from_invocation(Arena *perm, CompilerInvocation invocation, OsWriterInterface *w,
                                                        b32 verbose)
{
    CompilerCommand compiler_command = {0};

    if (verbose) {
        println_str(w, SL("Detected compiler invocation:"));
        w->tab += 1;
        println_compiler_invocation(w, invocation);
        w->tab -= 1;
    }

    switch (invocation.compiler) {
    case COMPILER_IS_UNKNOWN: {
        break;
    }
    case COMPILER_IS_GCC:   // FALLTHROUGH
    case COMPILER_IS_CLANG: // FALLTHROUGH
    case COMPILER_IS_ZIG_CC: {
        compiler_command = compiler_command_from_gcc_invocation(perm, invocation.tokens);
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

    return compiler_command;
}

static void print_compiler_command(OsWriterInterface *w, CompilerCommand cmd)
{
    print_str(w, SL("ok = "));
    println_b32(w, cmd.ok);
    print_str(w, SL("source = "));
    println_strlist(w, cmd.source_files);
    print_str(w, SL("output = "));
    println_str_escaped_string(w, cmd.output_file);
    print_str(w, SL("args = "));
    print_strlist(w, cmd.arguments);
}

static void println_compiler_command(OsWriterInterface *w, CompilerCommand cmd)
{
    print_compiler_command(w, cmd);
    print_u8(w, '\n');
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

static CommandObjects command_objects_from_command(Arena *perm, DirectoryStack dir_stack, CompilerCommand compiler_command,
                                                   OsWriterInterface *w, b32 verbose)
{
    CommandObjects command_objects = {0};

    if (verbose) {
        println_str(w, SL("Parsed compiler command:"));
        w->tab += 1;
        println_compiler_command(w, compiler_command);
        w->tab -= 1;
    }

    if (!compiler_command.ok) {
        return command_objects;
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
    assert(compiler_command.source_files.count > 0);
    command_objects.len = compiler_command.source_files.count;
    command_objects.ptr = ALLOC(perm, command_objects.len, *command_objects.ptr);

    // The top of the stack is the Current Working Directory
    Str cwd = dirstack_peek(dir_stack, perm);

    isize i = 0;
    for (StrListNode *node = compiler_command.source_files.front; node != NULL; node = node->next) {
        Str source_file = node->str;

        CommandObject cmd = {0};
        cmd.file          = source_file;
        cmd.directory     = cwd;
        cmd.output        = compiler_command.output_file;
        cmd.ok            = compiler_command.ok;
        cmd.arguments     = strlist_copy(compiler_command.arguments, perm);
        assert(i < command_objects.len);

        command_objects.ptr[i] = cmd;
        i++;
    }

    assert(command_objects.len == i);
    return command_objects;
}

static void print_command_object(OsWriterInterface *w, CommandObject obj)
{
    print_str(w, SL("ok = "));
    println_b32(w, obj.ok);
    print_str(w, SL("directory = "));
    println_str_escaped_string(w, obj.directory);
    print_str(w, SL("file = "));
    println_str_escaped_string(w, obj.file);
    print_str(w, SL("output = "));
    println_str_escaped_string(w, obj.output);
    print_str(w, SL("args = "));
    print_strlist(w, obj.arguments);
}

static void println_command_object(OsWriterInterface *w, CommandObject obj)
{
    print_command_object(w, obj);
    print_u8(w, '\n');
}

static void println_command_objects(OsWriterInterface *w, CommandObjects objs)
{
    for (isize i = 0; i < objs.len; ++i) {
        println_command_object(w, objs.ptr[i]);
        print_u8(w, '\n');
    }
}

// :: Json
// Simple json writer
static void json_write_header(OsWriterInterface *w)
{
    if (!w) return;

    w->tab     = 0;
    Str header = SL("[");
    println_str(w, header);
    w->tab += 1;
}

static void json_write_command_object(OsWriterInterface *w, CommandObject command, isize command_count)
{
    if (!w) return;

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
        print_str(w, SL(",\n"));
    }

    print_str(w, SL("{\n"));
    w->tab += 1;

    // "directory": "C:/dev/gb/make2compdb/input/simple with space",
    print_str(w, SL("\"directory\": "));
    print_str_escaped_string(w, command.directory);
    println_str(w, SL(","));

    // "file": "main.c",
    print_str(w, SL("\"file\": "));
    print_str_escaped_string(w, command.file);
    println_str(w, SL(","));

    if (command.output.len > 0) { //< Ouput is sometime not specified
        // "output": "main.o"
        print_str(w, SL("\"output\": "));
        print_str_escaped_string(w, command.output);
        println_str(w, SL(","));
    }

    // "arguments": [
    //          "gcc",
    //           ...
    //  ]
    println_str(w, SL("\"arguments\": ["));
    w->tab += 1;
    for (StrListNode *arg = command.arguments.front; arg != NULL; arg = arg->next) {
        print_str_escaped_string(w, arg->str);

        b32 const is_last = (arg->next == NULL);
        if (!is_last) {
            print_u8(w, ',');
        }
        print_u8(w, '\n');
    }

    w->tab -= 1;
    print_str(w, SL("]\n")); //< no comma!

    w->tab -= 1;
    print_str(w, SL("}"));
}

static void json_write_command_objects(OsWriterInterface *w, CommandObjects commands, isize command_count)
{
    for (isize i = 0; i < commands.len; ++i) {
        json_write_command_object(w, commands.ptr[i], command_count);
        command_count++;
    }
}

static void json_write_footer(OsWriterInterface *w, isize command_count)
{
    if (!w) return;

    if (command_count > 0) {
        print_u8(w, '\n');
    }

    w->tab     -= 1;
    Str footer  = SL("]");
    println_str(w, footer);
    w->tab = 0;
}

// :: make2compdb
// Main program function.
static int make2compdb(Arena *perm, OsWriterInterface *w, StrList cli_args, Str make_stdout, Str initial_working_directory)
{
    Str const help_message = SL("make2compdb - Create a compile_commands.json from a Make output\n"
                                "Usage: make2compdb [option]\n"
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
    b32 verbose      = 0;
    for (StrListNode *arg = cli_args.front; arg != NULL; arg = arg->next) {
        if (arg == cli_args.front) {
            program_name = cli_args.front->str;
        }
        else if (str_equal(arg->str, SL("-h")) || str_equal(arg->str, SL("--help"))) {
            println_str(w, help_message);
            return 0;
        }
        else if (str_equal(arg->str, SL("--version"))) {
            println_str(w, SL("Version: " VERSION "\n"));
            return 0;
        }
        else if (str_equal(arg->str, SL("-v")) || str_equal(arg->str, SL("--verbose"))) {
            verbose = 1;
        }
        else {
            print_str(w, SL("Unknown CLI arg: '"));
            print_str(w, arg->str);
            println_str(w, SL("'\n"));
            return -1;
        }
    }
    (void)program_name; //< Not used currently

    if (verbose) {
        w->tab = 0;
        println_str(w, SL("make2compdb"));
        println_str(w, SL("Version: " VERSION));
        println_str(w, SL("Verbose mode: true"));
        print_str(w, SL("Directory: "));
        println_str_escaped_string(w, initial_working_directory);
        print_str(w, SL("CLI args: "));
        println_strlist(w, cli_args);
        print_str(w, SL("Input len: "));
        println_number(w, make_stdout.len);
        println_str(w, SL("====\n"));
    }

    DirectoryStack dir_stack = {0};
    {
        // DirectoryStack has its own arena because its internal directory
        // needs to persist for all the program lifetime.
        isize dir_arena_cap = 1 << 14;
        byte *mem           = ALLOC(perm, dir_arena_cap, byte);
        dir_stack.arena     = arena_init(dir_arena_cap, mem);
    }

    // The first directory in our stack is the CWD.
    dirstack_push(&dir_stack, initial_working_directory);

    // In verbose mode, we don't print json.
    OsWriterInterface *json_console = verbose ? NULL : w;
    json_write_header(json_console);

    Str   input         = make_stdout;
    isize command_count = 0;
    while (input.len > 0) {
        // This naive line splitting strategy is good enough for identifying the parsing mode.
        Str naive_line = str_cut(input, SL("\n")).head;

        ParsingMode mode = identify_parsing_mode(naive_line);
        switch (mode) {
        case PARSING_MODE_MAKE_ENTER_DIR: {
            if (verbose) println_str(w, SL("Parsing mode: ENTER DIR"));

            // NOTE: On directory parsing.
            // Because of the `-w` flag, `make` will print information everytime it enters directory.
            // Something like "make: Entering directory 'C:/dev/'"

            Str dir = directory_from_make_dir_line(&input);
            if (dir.len > 0) {
                dirstack_push(&dir_stack, dir);
            }
            break;
        }
        case PARSING_MODE_MAKE_LEAVE_DIR: {
            if (verbose) println_str(w, SL("Parsing mode: LEAVE DIR"));

            // NOTE: On directory parsing.
            // Because of the `-w` flag, `make` will print information everytime it leaves a directory.
            // Something like "make: Leaving directory 'C:/dev/'"

            Str dir = directory_from_make_dir_line(&input);
            if (dir.len > 0) {
                Arena scratch = *perm;

                Str cwd = dirstack_pop(&dir_stack, &scratch);
                (void)cwd;
            }
            break;
        }
        case PARSING_MODE_SHELL: {
            if (verbose) println_str(w, SL("Parsing mode: SHELL"));

            // NOTE: On shell parsing.
            // The difference between logical line and compiler invocation:
            //
            //                     logical line
            //     |<----------------------------------------->|  |<---------- ...
            //      mkdir -p build && ccache gcc main.c -o main \n echo 'done' ...
            //                              |<---------------->|
            //                               compiler invocation
            //
            // A "compiler command" is a parsed version of a compiler invocation.
            // A "command object" is what we want to ouput in our json (compiler command + directory).
            // Note that a "command objet" can only have 1 source file. Therefore a "compiler command"
            // with multiple file (e.g `gcc main.c lib.c`) will create multiple "command object".
            //

            Arena   scratch = *perm;
            StrList line    = shell_tokenize_logical_line(&scratch, &input);
            while (!strlist_is_empty(line)) {
                Arena              scratch2     = scratch;
                CompilerInvocation invocation   = compiler_invocation_from_shell_line(&scratch2, &line, w, verbose);
                CompilerCommand    compiler_cmd = compiler_command_from_invocation(&scratch2, invocation, w, verbose);
                CommandObjects     command_objs = command_objects_from_command(&scratch2, dir_stack, compiler_cmd, w, verbose);

                if (command_objs.len > 0) {
                    json_write_command_objects(json_console, command_objs, command_count);
                    command_count += command_objs.len;

                    if (verbose) {
                        println_str(w, SL("Produced command objects:"));
                        w->tab += 1;
                        println_command_objects(w, command_objs);
                        w->tab -= 1;
                    }
                }
                else {
                    if (verbose) println_str(w, SL("No command object produced."));
                }
            }
            break;
        }
        default: {
            unreachable();
            break;
        }
        } // END: switch(mode)

        if (verbose) {
            static isize s_prev_command_count = 0;
            print_str(w, SL("-> +"));
            print_number(w, command_count - s_prev_command_count);
            print_str(w, SL(" command objects (total = "));
            print_number(w, command_count);
            println_str(w, SL(")\n\n---\n"));
            s_prev_command_count = command_count;
        }
    } // END: while (input.len > 0)

    json_write_footer(json_console, command_count);

    if (verbose) {
        w->tab = 0;
        print_u8(w, '\n');
        print_str(w, SL("Total command objects: "));
        println_number(w, command_count);
        println_str(w, SL("make2compdb is done."));
    }
    return 0;
}

// :: Os
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

#define CHECK(COND)                                                                                                              \
    do {                                                                                                                         \
        if (!(COND)) {                                                                                                           \
            puts("Test failed:");                                                                                                \
            puts(STRINGIFY(COND));                                                                                               \
            puts("\n");                                                                                                          \
            assert(0);                                                                                                           \
        }                                                                                                                        \
    } while (0)

// StrList builder
// e.g. SLIST(arena, "gcc", "main.c");
#define SLIST(A, ...)                                                                                                            \
    strlist_from_cstrs(A, (sizeof((char *[]){__VA_ARGS__}) / sizeof(*(char *[]){__VA_ARGS__})), (char *[]){__VA_ARGS__})

static Str str_from_cstr(char *cstr)
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

static StrList strlist_from_cstrs(Arena *a, isize count, char *cstrs[static count])
{
    StrList sl = {0};
    for (isize i = 0; i < count; i++) {
        Str s = str_from_cstr(cstrs[i]);
        strlist_push_back(&sl, a, s);
    }
    return sl;
}

static b32 strlist_equal(StrList a, StrList b)
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

static void run_test_shell_tokenizer(Arena scratch, Str input, StrList expected_tokens)
{
    StrList line = shell_tokenize_logical_line(&scratch, &input);
    CHECK(strlist_equal(line, expected_tokens));

    line = shell_tokenize_logical_line(&scratch, &input);
    CHECK(strlist_is_empty(line));
}

static void test_shell_tokenizer(Arena a)
{
    run_test_shell_tokenizer(a, SL("gcc -std=c11 -g -o app main.c"), SLIST(&a, "gcc", "-std=c11", "-g", "-o", "app", "main.c"));

    run_test_shell_tokenizer(a, SL("C:/Users/John_Falstaff/w64devkit/bin/gcc.exe -std=c11 -g -o app main.c"),
                             SLIST(&a, "C:/Users/John_Falstaff/w64devkit/bin/gcc.exe", "-std=c11", "-g", "-o", "app", "main.c"));

    // Handle quotes inside of double quotes
    run_test_shell_tokenizer(a, SL("echo \"The Knights Who Say 'Ni'\""), SLIST(&a, "echo", "\"The Knights Who Say 'Ni'\""));

    // Handle double quotes inside of quotes
    run_test_shell_tokenizer(a, SL("echo 'The Knights Who Say \"Ni\"'"), SLIST(&a, "echo", "'The Knights Who Say \"Ni\"'"));

    // Unnecessary whitespace deleted everywhere except in quotes
    run_test_shell_tokenizer(a, SL("   echo    '  H E L L O   W O R L D  '     "),
                             SLIST(&a, "echo", "'  H E L L O   W O R L D  '"));

    // Subshell expression kept as a single token
    run_test_shell_tokenizer(a, SL("gcc $(pkg-config --cflags glib) foo.c"),
                             SLIST(&a, "gcc", "$(pkg-config --cflags glib)", "foo.c"));

    // Backslash line continuations (Unix)
    run_test_shell_tokenizer(a, SL("gcc \\\n -std=c11 \\\n -Wall \\\n -Wextra \\\n -g \\\n -o out/continued \\\n main.c"),
                             SLIST(&a, "gcc", "-std=c11", "-Wall", "-Wextra", "-g", "-o", "out/continued", "main.c"));

    // Backslash line continuations (Windows \r\n)
    run_test_shell_tokenizer(
        a, SL("gcc \\\r\n -std=c11 \\\r\n -Wall \\\r\n -Wextra \\\r\n -g \\\r\n -o out/continued \\\r\n main.c"),
        SLIST(&a, "gcc", "-std=c11", "-Wall", "-Wextra", "-g", "-o", "out/continued", "main.c"));

    // Compiler path with spaces in double quotes
    run_test_shell_tokenizer(
        a, SL("\"C:/Users/John Falstaff/w64devkit/bin/gcc.exe\" -std=c11 -g -o app main.c"),
        SLIST(&a, "\"C:/Users/John Falstaff/w64devkit/bin/gcc.exe\"", "-std=c11", "-g", "-o", "app", "main.c"));

    // Compiler path with backslash-escaped space
    run_test_shell_tokenizer(
        a, SL("C:/Users/John\\ Falstaff/w64devkit/bin/gcc.exe -std=c11 -g -o app main.c"),
        SLIST(&a, "C:/Users/John\\ Falstaff/w64devkit/bin/gcc.exe", "-std=c11", "-g", "-o", "app", "main.c"));

    // Compiler path with single-quoted path component
    run_test_shell_tokenizer(
        a, SL("C:/Users/'John Falstaff'/w64devkit/bin/gcc.exe -std=c11 -g -o app main.c"),
        SLIST(&a, "C:/Users/'John Falstaff'/w64devkit/bin/gcc.exe", "-std=c11", "-g", "-o", "app", "main.c"));

    // Compiler path with double-quoted path component
    run_test_shell_tokenizer(
        a, SL("C:/Users/\"John Falstaff\"/w64devkit/bin/gcc.exe -std=c11 -g -o app main.c"),
        SLIST(&a, "C:/Users/\"John Falstaff\"/w64devkit/bin/gcc.exe", "-std=c11", "-g", "-o", "app", "main.c"));

    // redirect
    run_test_shell_tokenizer(a, SL("cmd &>file"), SLIST(&a, "cmd", "&>", "file"));
    run_test_shell_tokenizer(a, SL("cmd &>>file"), SLIST(&a, "cmd", "&>>", "file"));
    run_test_shell_tokenizer(a, SL("cmd &>|file"), SLIST(&a, "cmd", "&>|", "file"));
    run_test_shell_tokenizer(a, SL("cmd &&>file"), SLIST(&a, "cmd", "&&", ">", "file"));
    run_test_shell_tokenizer(a, SL("cmd &>"), SLIST(&a, "cmd", "&>"));
    run_test_shell_tokenizer(a, SL("cmd >file"), SLIST(&a, "cmd", ">", "file"));
    run_test_shell_tokenizer(a, SL("cmd >|file"), SLIST(&a, "cmd", ">|", "file"));
    run_test_shell_tokenizer(a, SL("cmd >>|file"), SLIST(&a, "cmd", ">>", "|", "file"));
    run_test_shell_tokenizer(a, SL("cmd 2>file"), SLIST(&a, "cmd", "2>", "file"));
    run_test_shell_tokenizer(a, SL("cmd 12>file"), SLIST(&a, "cmd", "12>", "file"));
    run_test_shell_tokenizer(a, SL("cmd 2>>file"), SLIST(&a, "cmd", "2>>", "file"));
    run_test_shell_tokenizer(a, SL("cmd 2>|file"), SLIST(&a, "cmd", "2>|", "file"));
    run_test_shell_tokenizer(a, SL("echo2>file"), SLIST(&a, "echo2", ">", "file"));
    run_test_shell_tokenizer(a, SL("ls -1 2 >file"), SLIST(&a, "ls", "-1", "2", ">", "file"));
    run_test_shell_tokenizer(a, SL("cmd <file"), SLIST(&a, "cmd", "<", "file"));
    run_test_shell_tokenizer(a, SL("cmd <>file"), SLIST(&a, "cmd", "<>", "file"));
    run_test_shell_tokenizer(a, SL("cmd 3<>file"), SLIST(&a, "cmd", "3<>", "file"));
    run_test_shell_tokenizer(a, SL("cmd <&3"), SLIST(&a, "cmd", "<&", "3"));
    run_test_shell_tokenizer(a, SL("cmd <&3-"), SLIST(&a, "cmd", "<&", "3-"));
    run_test_shell_tokenizer(a, SL("cmd <&-"), SLIST(&a, "cmd", "<&", "-"));
    run_test_shell_tokenizer(a, SL("cmd 2>&1"), SLIST(&a, "cmd", "2>&", "1"));
    run_test_shell_tokenizer(a, SL("cmd 2>&1-"), SLIST(&a, "cmd", "2>&", "1-"));
    run_test_shell_tokenizer(a, SL("cmd 2>&-"), SLIST(&a, "cmd", "2>&", "-"));
    run_test_shell_tokenizer(a, SL("cmd <<EOF"), SLIST(&a, "cmd", "<<", "EOF"));
    run_test_shell_tokenizer(a, SL("cmd <<-EOF"), SLIST(&a, "cmd", "<<-", "EOF"));
    run_test_shell_tokenizer(a, SL("cmd <<< word"), SLIST(&a, "cmd", "<<<", "word"));
    run_test_shell_tokenizer(a, SL("cmd <<<word"), SLIST(&a, "cmd", "<<<", "word"));
    run_test_shell_tokenizer(a, SL("cmd <<'EOF'"), SLIST(&a, "cmd", "<<", "'EOF'"));
    run_test_shell_tokenizer(a, SL("cmd <<EOF >out"), SLIST(&a, "cmd", "<<", "EOF", ">", "out"));

    // pipes
    run_test_shell_tokenizer(a, SL("cmd1 | cmd2"), SLIST(&a, "cmd1", "|", "cmd2"));
    run_test_shell_tokenizer(a, SL("cmd1 |& cmd2"), SLIST(&a, "cmd1", "|&", "cmd2"));
    run_test_shell_tokenizer(a, SL("cmd1 || cmd2"), SLIST(&a, "cmd1", "||", "cmd2"));
    run_test_shell_tokenizer(a, SL("cmd1 ||| cmd2"), SLIST(&a, "cmd1", "||", "|", "cmd2")); /* greedy left match */
    run_test_shell_tokenizer(a, SL("cmd1|cmd2"), SLIST(&a, "cmd1", "|", "cmd2"));
    run_test_shell_tokenizer(a, SL("cmd1 2>&1|cmd2"), SLIST(&a, "cmd1", "2>&", "1", "|", "cmd2"));

    // list
    run_test_shell_tokenizer(a, SL("cmd1 && cmd2"), SLIST(&a, "cmd1", "&&", "cmd2"));
    run_test_shell_tokenizer(a, SL("cmd1&&cmd2"), SLIST(&a, "cmd1", "&&", "cmd2"));
    run_test_shell_tokenizer(a, SL("cmd1 & cmd2"), SLIST(&a, "cmd1", "&", "cmd2")); /* single & is background */
    run_test_shell_tokenizer(a, SL("cmd1 &>file && cmd2"), SLIST(&a, "cmd1", "&>", "file", "&&", "cmd2"));
    run_test_shell_tokenizer(a, SL("cmd1 &>>file && cmd2"), SLIST(&a, "cmd1", "&>>", "file", "&&", "cmd2"));

    // Multi-line make output (Unix)
    {
        StrList line;
        Arena   scratch = a;
        Str     input   = SL("zig cc -c -o main.o main.c\n"
                             "zig cc -c -o strlib.o strlib.c    \n" //< intentional whitespace
                             "zig cc -o app main.o strlib.o\t\n");  //< intentional tab

        line = shell_tokenize_logical_line(&scratch, &input);
        CHECK(strlist_equal(line, SLIST(&scratch, "zig", "cc", "-c", "-o", "main.o", "main.c")));

        line = shell_tokenize_logical_line(&scratch, &input);
        CHECK(strlist_equal(line, SLIST(&scratch, "zig", "cc", "-c", "-o", "strlib.o", "strlib.c")));

        line = shell_tokenize_logical_line(&scratch, &input);
        CHECK(strlist_equal(line, SLIST(&scratch, "zig", "cc", "-o", "app", "main.o", "strlib.o")));

        line = shell_tokenize_logical_line(&scratch, &input);
        CHECK(strlist_is_empty(line));
    }

    // Multi-line make output (Windows \r\n)
    {
        StrList line;
        Arena   scratch = a;
        Str     input   = SL("zig cc -c -o main.o main.c\r\n"
                             "zig cc -c -o strlib.o strlib.c\r\n"
                             "zig cc -o app main.o strlib.o\r\n");

        line = shell_tokenize_logical_line(&scratch, &input);
        CHECK(strlist_equal(line, SLIST(&scratch, "zig", "cc", "-c", "-o", "main.o", "main.c")));

        line = shell_tokenize_logical_line(&scratch, &input);
        CHECK(strlist_equal(line, SLIST(&scratch, "zig", "cc", "-c", "-o", "strlib.o", "strlib.c")));

        line = shell_tokenize_logical_line(&scratch, &input);
        CHECK(strlist_equal(line, SLIST(&scratch, "zig", "cc", "-o", "app", "main.o", "strlib.o")));

        line = shell_tokenize_logical_line(&scratch, &input);
        CHECK(strlist_is_empty(line));
    }
}

static void run_test_parse_dir(Str input, Str expected_enter_dir, Str expected_leave_dir)
{
    Str dir;
    dir = directory_from_make_dir_line(&input);
    CHECK(str_equal(dir, expected_enter_dir));

    dir = directory_from_make_dir_line(&input);
    CHECK(str_equal(dir, SL("")));

    dir = directory_from_make_dir_line(&input);
    CHECK(str_equal(dir, expected_leave_dir));
}

static void test_parse_directory(Arena a)
{
    (void)a;

    // Simple test: enter and exit directory
    run_test_parse_dir(SL("make: Entering directory 'C:/dev/gb/make2compdb/input/simple_inline'\n"
                          "gcc -std=c11 -Wall -Wextra -Wpedantic -g -o app main.c\n"
                          "make: Leaving directory 'C:/dev/gb/make2compdb/input/simple_inline'\n"),
                       SL("C:/dev/gb/make2compdb/input/simple_inline"), SL("C:/dev/gb/make2compdb/input/simple_inline"));

    // Windows EOL
    run_test_parse_dir(SL("make: Entering directory 'C:/dev/gb/make2compdb/input/simple_inline'\r\n"
                          "gcc -std=c11 -Wall -Wextra -Wpedantic -g -o app main.c\r\n"
                          "make: Leaving directory 'C:/dev/gb/make2compdb/input/simple_inline'\r\n"),
                       SL("C:/dev/gb/make2compdb/input/simple_inline"), SL("C:/dev/gb/make2compdb/input/simple_inline"));

    // Windows EOL and path
    run_test_parse_dir(SL("make: Entering directory 'C:\\dev\\gb\\make2compdb\\input\\simple_inline'\r\n"
                          "gcc -std=c11 -Wall -Wextra -Wpedantic -g -o app main.c\r\n"
                          "make: Leaving directory 'C:\\dev\\gb\\make2compdb\\input\\simple_inline'\r\n"),
                       SL("C:\\dev\\gb\\make2compdb\\input\\simple_inline"),
                       SL("C:\\dev\\gb\\make2compdb\\input\\simple_inline"));

    // Non-standard `make` program name (e.g. make-3.81)
    // See https://github.com/nickdiego/compiledb/issues/146
    run_test_parse_dir(SL("make-3.81: Entering directory 'C:/dev/gb/make2compdb/input/simple_inline'\n"
                          "gcc -std=c11 -Wall -Wextra -Wpedantic -g -o app main.c\n"
                          "make-3.81: Leaving directory 'C:/dev/gb/make2compdb/input/simple_inline'\n"),
                       SL("C:/dev/gb/make2compdb/input/simple_inline"), SL("C:/dev/gb/make2compdb/input/simple_inline"));

    // Directory name contains backslashes (looks like \n but isn't)
    run_test_parse_dir(SL("make: Entering directory 'C:\\nice\\dir'\n"
                          "gcc -std=c11 -Wall -Wextra -Wpedantic -g -o app main.c\n"
                          "make: Leaving directory 'C:\\nice\\dir'\n"),
                       SL("C:\\nice\\dir"), SL("C:\\nice\\dir"));

    // BSD make: backtick open, single-quote close
    // See https://github.com/fcying/compiledb-go/issues/1
    run_test_parse_dir(SL("make: Entering directory `/home/test'\n"
                          "gcc -std=c11 -Wall -Wextra -Wpedantic -g -o app main.c\n"
                          "make: Leaving directory `/home/test'\n"),
                       SL("/home/test"), SL("/home/test"));
}

static void test_compiler_parse(Arena a)
{
    (void)a;

    // gcc
    CHECK(COMPILER_IS_GCC == compiler_parse(SL("gcc")));
    CHECK(COMPILER_IS_GCC == compiler_parse(SL("gcc.exe")));
    CHECK(COMPILER_IS_GCC == compiler_parse(SL("gcc-12")));
    CHECK(COMPILER_IS_GCC == compiler_parse(SL("gcc-12.1")));
    CHECK(COMPILER_IS_GCC == compiler_parse(SL("arm-none-eabi-gcc")));
    CHECK(COMPILER_IS_GCC == compiler_parse(SL("x86_64-pc-linux-gnu-gcc-15.2.1")));
    CHECK(COMPILER_IS_GCC == compiler_parse(SL("C:/Users/gberthiaume/scoop/apps/w64devkit/current/bin/gcc")));
    CHECK(COMPILER_IS_GCC == compiler_parse(SL("'C:/my folder/gcc'")));
    CHECK(COMPILER_IS_GCC == compiler_parse(SL("\"C:/my folder/gcc\"")));
    CHECK(COMPILER_IS_GCC == compiler_parse(SL("C:\\Users\\gberthiaume\\scoop\\apps\\w64devkit\\current\\bin\\gcc")));
    CHECK(COMPILER_IS_GCC == compiler_parse(SL("C:/clang/gcc")));

    // zig cc
    CHECK(COMPILER_IS_ZIG_CC == compiler_parse(SL("zig")));
    CHECK(COMPILER_IS_ZIG_CC == compiler_parse(SL("zig.exe")));
    CHECK(COMPILER_IS_ZIG_CC == compiler_parse(SL("C:/Users/gberthiaume/scoop/apps/zig/0.16.0/zig.exe")));

    // not a compiler
    CHECK(COMPILER_IS_UNKNOWN == compiler_parse(SL("ccache")));
    CHECK(COMPILER_IS_UNKNOWN == compiler_parse(SL("distcc")));
    CHECK(COMPILER_IS_UNKNOWN == compiler_parse(SL("C:/gcc/myprogram")));
    CHECK(COMPILER_IS_UNKNOWN == compiler_parse(SL("CC=gcc make")));
}

static void run_invo_test(Arena scratch, Str input, CompilerKind expected_compiler, StrList expected_tokens)
{
    StrList            line = shell_tokenize_logical_line(&scratch, &input);
    CompilerInvocation invo = compiler_invocation_from_shell_line(&scratch, &line, NULL, 0);

    CHECK(invo.compiler == expected_compiler);
    if (expected_compiler == COMPILER_IS_UNKNOWN) {
        CHECK(strlist_is_empty(invo.tokens));
    }
    else {
        CHECK(strlist_equal(invo.tokens, expected_tokens));
    }
}

static void test_extract_compiler_invocation(Arena a)
{
    // clang-format off

    // generic
    run_invo_test(a, SL("gcc main.c"), COMPILER_IS_GCC, SLIST(&a, "gcc", "main.c"));
    run_invo_test(a, SL("ccache gcc main.c"), COMPILER_IS_GCC, SLIST(&a, "gcc", "main.c"));
    run_invo_test(a, SL("distcc ccache gcc main.c"), COMPILER_IS_GCC, SLIST(&a, "gcc", "main.c"));

    // list
    run_invo_test(a, SL("make && gcc main.c"), COMPILER_IS_GCC, SLIST(&a, "gcc", "main.c"));
    run_invo_test(a, SL("make || gcc main.c"), COMPILER_IS_GCC, SLIST(&a, "gcc", "main.c"));
    run_invo_test(a, SL("mkdir build; gcc main.c -o build/main"), COMPILER_IS_GCC, SLIST(&a, "gcc", "main.c", "-o", "build/main"));
    run_invo_test(a, SL("gcc main.c && echo done"), COMPILER_IS_GCC, SLIST(&a, "gcc", "main.c"));
    run_invo_test(a, SL("mkdir build && gcc main.c -o build/main && strip build/main"), COMPILER_IS_GCC, SLIST(&a, "gcc", "main.c", "-o", "build/main"));
    run_invo_test(a, SL("mkdir build && echo done"), COMPILER_IS_UNKNOWN, (StrList){0});
    run_invo_test(a, SL("make & gcc main.c"), COMPILER_IS_GCC, SLIST(&a, "gcc", "main.c"));

    // pipes
    run_invo_test(a, SL("gcc main.c | tee log.txt"), COMPILER_IS_GCC, SLIST(&a, "gcc", "main.c"));
    run_invo_test(a, SL("gcc main.c |& tee log.txt"), COMPILER_IS_GCC, SLIST(&a, "gcc", "main.c"));
    run_invo_test(a, SL("echo main.c | xargs gcc"), COMPILER_IS_GCC, SLIST(&a, "gcc"));

    // redirect
    run_invo_test(a, SL("gcc main.c > log.txt"), COMPILER_IS_GCC, SLIST(&a, "gcc", "main.c"));
    run_invo_test(a, SL("gcc main.c >> log.txt"), COMPILER_IS_GCC, SLIST(&a, "gcc", "main.c"));
    run_invo_test(a, SL("gcc main.c 2> err.txt"), COMPILER_IS_GCC, SLIST(&a, "gcc", "main.c"));
    run_invo_test(a, SL("gcc main.c &> log.txt"), COMPILER_IS_GCC, SLIST(&a, "gcc", "main.c"));
    run_invo_test(a, SL("gcc main.c &>> log.txt"), COMPILER_IS_GCC, SLIST(&a, "gcc", "main.c"));
    run_invo_test(a, SL("gcc main.c >| log.txt"), COMPILER_IS_GCC, SLIST(&a, "gcc", "main.c"));
    run_invo_test(a, SL("gcc main.c 2>&1"), COMPILER_IS_GCC, SLIST(&a, "gcc", "main.c"));
    run_invo_test(a, SL("gcc main.c 2>&-"), COMPILER_IS_GCC, SLIST(&a, "gcc", "main.c"));
    run_invo_test(a, SL("gcc main.c <<< inline"), COMPILER_IS_GCC, SLIST(&a, "gcc", "main.c"));
    run_invo_test(a, SL("gcc < input.txt main.c"), COMPILER_IS_GCC, SLIST(&a, "gcc", "main.c"));
    run_invo_test(a, SL("gcc main.c > log.txt -o app"), COMPILER_IS_GCC, SLIST(&a, "gcc", "main.c", "-o", "app"));
    run_invo_test(a, SL("gcc main.c -o 1>&2 2>/dev/null app"), COMPILER_IS_GCC, SLIST(&a, "gcc", "main.c", "-o", "app"));

    // subshell expansion and substitution
    run_invo_test(a, SL("gcc `echo main.c`"), COMPILER_IS_GCC, SLIST(&a, "gcc"));
    run_invo_test(a, SL("gcc $(find . -name '*.c')"), COMPILER_IS_GCC, SLIST(&a, "gcc"));
    run_invo_test(a, SL("gcc main.c $(extra_flags) -o app"), COMPILER_IS_GCC, SLIST(&a, "gcc", "main.c", "-o", "app"));
    run_invo_test(a, SL("gcc $CFLAGS main.c"), COMPILER_IS_GCC, SLIST(&a, "gcc", "main.c"));
    run_invo_test(a, SL("gcc ${CFLAGS} main.c"), COMPILER_IS_GCC, SLIST(&a, "gcc", "main.c"));
    run_invo_test(a, SL("gcc main.c -o $(basename main.c .c)"), COMPILER_IS_GCC, SLIST(&a, "gcc", "main.c", "-o"));
    run_invo_test(a, SL("gcc `echo main.c` -o app"), COMPILER_IS_GCC, SLIST(&a, "gcc", "-o", "app"));
    run_invo_test(a, SL("mkdir build && gcc `echo \"test\"` main.c -o build/main.exe > log.txt"), COMPILER_IS_GCC, SLIST(&a, "gcc", "main.c", "-o", "build/main.exe"));

    // clang-format on
}

static void run_test_gcc_invocation(Arena scratch, StrList input_tokens, CompilerCommand expected_cmd)
{
    CompilerCommand cmd = compiler_command_from_gcc_invocation(&scratch, input_tokens);

    CHECK(cmd.ok == expected_cmd.ok);
    CHECK(strlist_equal(cmd.source_files, expected_cmd.source_files));
    CHECK(str_equal(cmd.output_file, expected_cmd.output_file));
    CHECK(strlist_equal(cmd.arguments, expected_cmd.arguments));
}

static void test_compiler_command_from_gcc_invocation(Arena a)
{
    StrList args;

    // Simple test
    args = SLIST(&a, "gcc", "-o", "main", "main.c");
    run_test_gcc_invocation(a, args,
                            (CompilerCommand){
                                .ok           = 1,
                                .source_files = SLIST(&a, "main.c"),
                                .output_file  = SL("main"),
                                .arguments    = strlist_copy(args, &a),
                            });

    // With "-omain" (output file attached to flag)
    args = SLIST(&a, "gcc", "-omain", "main.c");
    run_test_gcc_invocation(a, args,
                            (CompilerCommand){
                                .ok           = 1,
                                .source_files = SLIST(&a, "main.c"),
                                .output_file  = SL("main"),
                                .arguments    = strlist_copy(args, &a),
                            });

    // Long -o version
    args = SLIST(&a, "gcc", "--output", "main", "main.c");
    run_test_gcc_invocation(a, args,
                            (CompilerCommand){
                                .ok           = 1,
                                .source_files = SLIST(&a, "main.c"),
                                .output_file  = SL("main"),
                                .arguments    = strlist_copy(args, &a),
                            });

    // Long -o version nospace
    args = SLIST(&a, "gcc", "--output=main", "main.c");
    run_test_gcc_invocation(a, args,
                            (CompilerCommand){
                                .ok           = 1,
                                .source_files = SLIST(&a, "main.c"),
                                .output_file  = SL("main"),
                                .arguments    = strlist_copy(args, &a),
                            });

    // No output specified
    args = SLIST(&a, "gcc", "main.c");
    run_test_gcc_invocation(a, args,
                            (CompilerCommand){
                                .ok           = 1,
                                .source_files = SLIST(&a, "main.c"),
                                .output_file  = SL(""),
                                .arguments    = strlist_copy(args, &a),
                            });

    // More flags
    args = SLIST(&a, "gcc", "-std=c23", "-O0", "-g3", "-Wall", "-Wextra", "-Wpedantic", "main.c", "-o", "main");
    run_test_gcc_invocation(a, args,
                            (CompilerCommand){
                                .ok           = 1,
                                .source_files = SLIST(&a, "main.c"),
                                .output_file  = SL("main"),
                                .arguments    = strlist_copy(args, &a),
                            });

    // With include dir (separate token) — "mydir" consumed by -I, excluded from arguments
    args = SLIST(&a, "gcc", "-I", "mydir", "-o", "main", "main.c");
    run_test_gcc_invocation(a, args,
                            (CompilerCommand){
                                .ok           = 1,
                                .source_files = SLIST(&a, "main.c"),
                                .output_file  = SL("main"),
                                .arguments    = strlist_copy(args, &a),
                            });

    // With include dir attached to flag
    args = SLIST(&a, "gcc", "-Imydir", "-o", "main", "main.c");
    run_test_gcc_invocation(a, args,
                            (CompilerCommand){
                                .ok           = 1,
                                .source_files = SLIST(&a, "main.c"),
                                .output_file  = SL("main"),
                                .arguments    = strlist_copy(args, &a),
                            });

    // With include path containing spaces and quotes
    // https://github.com/fcying/compiledb-go/issues/11
    args = SLIST(&a, "gcc", " -I'D:\\path to scoop\\scoop\\apps\\gcc\\current\\include'", "-o", "main", "main.c");
    run_test_gcc_invocation(a, args,
                            (CompilerCommand){
                                .ok           = 1,
                                .source_files = SLIST(&a, "main.c"),
                                .output_file  = SL("main"),
                                .arguments    = strlist_copy(args, &a),
                            });

    // Include dir that looks like a C file - must not be treated as source
    args = SLIST(&a, "gcc", "-I", "test.c", "-o", "main", "main.c");
    run_test_gcc_invocation(a, args,
                            (CompilerCommand){
                                .ok           = 1,
                                .source_files = SLIST(&a, "main.c"),
                                .output_file  = SL("main"),
                                .arguments    = strlist_copy(args, &a),
                            });

    // Preprocessor define that looks like a C file - must not be treated as source
    args = SLIST(&a, "gcc", "-D", "test.c", "-o", "main", "main.c");
    run_test_gcc_invocation(a, args,
                            (CompilerCommand){
                                .ok           = 1,
                                .source_files = SLIST(&a, "main.c"),
                                .output_file  = SL("main"),
                                .arguments    = strlist_copy(args, &a),
                            });

    // Macro with quotes in value
    // https://github.com/nickdiego/compiledb/issues/131
    args = SLIST(&a, "cc", "-MMD", "-MP", "-O2", "-march=native", "-iquote", "./include", "-U_FORTIFY_SOURCE",
                 "-DPROG_NAME=\"waffle\"", "-c", "src/cursor_events.c", "-o", ".build/release/cursor_events.o");
    run_test_gcc_invocation(a, args,
                            (CompilerCommand){
                                .ok           = 1,
                                .source_files = SLIST(&a, "src/cursor_events.c"),
                                .output_file  = SL(".build/release/cursor_events.o"),
                                .arguments    = strlist_copy(args, &a),
                            });

    // Multiple C source files — source files excluded from arguments
    args = SLIST(&a, "gcc", "-o", "app", "main.c", "mathlib.c", "strlib.c");
    run_test_gcc_invocation(a, args,
                            (CompilerCommand){
                                .ok           = 1,
                                .source_files = SLIST(&a, "main.c", "mathlib.c", "strlib.c"),
                                .output_file  = SL("app"),
                                .arguments    = strlist_copy(args, &a),
                            });

    // Embedded use case: ARM cross-compiler with assembly source
    // (Taken from STM32 cubemx generated makefile)
    args = SLIST(&a, "arm-none-eabi-gcc", "-x", "assembler-with-cpp", "-c", "-mcpu=cortex-m4", "-mthumb", "-mfpu=fpv4-sp-d16",
                 "-mfloat-abi=hard", "-DUSE_HAL_DRIVER", "-DSTM32G431xx", "-ICore/Inc", "-IDrivers/STM32G4xx_HAL_Driver/Inc",
                 "-IDrivers/STM32G4xx_HAL_Driver/Inc/Legacy", "-IDrivers/CMSIS/Device/ST/STM32G4xx/Include",
                 "-IDrivers/CMSIS/Include", "-Og", "-Wall", "-fdata-sections", "-ffunction-sections", "-g", "-gdwarf-2", "-MMD",
                 "-MP", "-MFbuild/startup_stm32g431xx.d", "startup_stm32g431xx.s", "-o", "build/startup_stm32g431xx.o");
    run_test_gcc_invocation(a, args,
                            (CompilerCommand){
                                .ok           = 1,
                                .source_files = SLIST(&a, "startup_stm32g431xx.s"),
                                .output_file  = SL("build/startup_stm32g431xx.o"),
                                .arguments    = strlist_copy(args, &a),
                            });

    // -x language (short form, space separated)
    // https://github.com/nickdiego/compiledb/issues/140
    args = SLIST(&a, "gcc", "-x", "c", "-c", "stb_image.h");
    run_test_gcc_invocation(a, args,
                            (CompilerCommand){
                                .ok           = 1,
                                .source_files = SLIST(&a, "stb_image.h"),
                                .output_file  = SL(""),
                                .arguments    = strlist_copy(args, &a),
                            });

    // --language=c (long form, attached)
    args = SLIST(&a, "gcc", "--language=c", "-c", "stb_image.h");
    run_test_gcc_invocation(a, args,
                            (CompilerCommand){
                                .ok           = 1,
                                .source_files = SLIST(&a, "stb_image.h"),
                                .output_file  = SL(""),
                                .arguments    = strlist_copy(args, &a),
                            });

    // --language c (long form, space separated)
    args = SLIST(&a, "gcc", "--language", "c", "-c", "stb_image.h");
    run_test_gcc_invocation(a, args,
                            (CompilerCommand){
                                .ok           = 1,
                                .source_files = SLIST(&a, "stb_image.h"),
                                .output_file  = SL(""),
                                .arguments    = strlist_copy(args, &a),
                            });

    // C++ - single-header C++ library
    args = SLIST(&a, "gcc", "-x", "c++", "-c", "mylib.hpp");
    run_test_gcc_invocation(a, args,
                            (CompilerCommand){
                                .ok           = 1,
                                .source_files = SLIST(&a, "mylib.hpp"),
                                .output_file  = SL(""),
                                .arguments    = strlist_copy(args, &a),
                            });

    // assembler - plain assembly, no preprocessing
    args = SLIST(&a, "gcc", "-x", "assembler", "-c", "startup.s", "-o", "startup.o");
    run_test_gcc_invocation(a, args,
                            (CompilerCommand){
                                .ok           = 1,
                                .source_files = SLIST(&a, "startup.s"),
                                .output_file  = SL("startup.o"),
                                .arguments    = strlist_copy(args, &a),
                            });

    // assembler-with-cpp - assembly with C preprocessing (hyphens in value)
    args = SLIST(&a, "gcc", "-x", "assembler-with-cpp", "-c", "startup.S", "-o", "startup.o");
    run_test_gcc_invocation(a, args,
                            (CompilerCommand){
                                .ok           = 1,
                                .source_files = SLIST(&a, "startup.S"),
                                .output_file  = SL("startup.o"),
                                .arguments    = strlist_copy(args, &a),
                            });

    // -x none reset: main.c still recognized as source by extension after reset
    args = SLIST(&a, "gcc", "-x", "c", "header.h", "-x", "none", "main.c");
    run_test_gcc_invocation(a, args,
                            (CompilerCommand){
                                .ok           = 1,
                                .source_files = SLIST(&a, "header.h", "main.c"),
                                .output_file  = SL(""),
                                .arguments    = strlist_copy(args, &a),
                            });

    // Reset restores extension-based detection - other.h is NOT a source after reset
    args = SLIST(&a, "gcc", "-x", "c", "header.h", "-x", "none", "other.h");
    run_test_gcc_invocation(a, args,
                            (CompilerCommand){
                                .ok           = 1,
                                .source_files = SLIST(&a, "header.h"),
                                .output_file  = SL(""),
                                .arguments    = strlist_copy(args, &a),
                            });
}

int main(void)
{
    usize cap = 1 << 26; // 64 MiB
    byte *mem = malloc(cap);
    assert(mem); //< to make static analyzer happy.
    Arena arena = arena_init((isize)cap, mem);

    puts("Running unit tests...");
    {
        test_shell_tokenizer(arena);
        test_compiler_parse(arena);
        test_extract_compiler_invocation(arena);
        test_parse_directory(arena);
        test_compiler_command_from_gcc_invocation(arena);
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

    usize cap = 1 << 26; // 64 MiB
    byte *mem = malloc(cap);
    assert(mem); //< to make static analyzer happy.

    Arena arena = arena_init((isize)cap, mem);

    StrList cli_args = {0};
    strlist_push_back(&cli_args, &arena, SL("make2compdb"));
#ifdef FUZZ_VERBOSE
    strlist_push_back(&cli_args, &arena, SL("--verbose"));
#endif

    OsWriterInterface writer = {.ctx = NULL, .flush = null_console_flush};
    Str               cwd    = SL("/dev");

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
            make2compdb(&scratch, &writer, cli_args, fuzzed_make_stdout, cwd);
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

static Str16 str16_drop_head(Str16 s, isize offset_from_the_start)
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
enum : u32 {
    // U+FFFD (�) is used to replace an unknown, unrecognised, or unrepresentable character.
    REPLACEMENT_CHARACTER = 0xfffd,
};

// Consume a string to produce a codepoint.
static u32 codepoint_take_from_utf8(Str *in)
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
static u32 codepoint_take_from_utf16(Str16 *in)
{
    assert(in);
    assert(in->len > 0);

    u32 cp = 0;
    if (in->ptr[0] >= 0xdc00 && in->ptr[0] <= 0xdfff) {
        goto reject; // unU8_paired low surrogate
    }
    else if (in->ptr[0] >= 0xd800 && in->ptr[0] <= 0xdbff) {
        // Surrogate U8_pair !
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
static Str utf8_from_codepoint(u8 mem[static 4], u32 codepoint)
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
static Str16 utf16_from_codepoint(u16 mem[static 2], u32 codepoint)
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

static Str utf8_from_utf16(Arena *perm, Str16 in)
{
    Str s = {0};
    while (in.len > 0) {
        u8 mem[size_of(u32)] = {0};

        u32 cp     = codepoint_take_from_utf16(&in);
        Str cp_str = utf8_from_codepoint(mem, cp);
        s          = str_concat(perm, s, cp_str);
    }
    return s;
}

static Str utf8_from_cstr16(Arena *perm, c16 *in)
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
// We instead use this neat technique:
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
    OsStream std_out;
    OsStream std_err;
    struct {
        c16   mem[1 << 11];
        isize len;
    } buf;
} WinOsWriter;

static inline int32_t to_i32(isize x)
{
    x = clamp_isize(x, INT32_MIN, INT32_MAX);
    return (i32)x;
}

static Str os_cwd_read(Arena *perm)
{
    isize count = GetCurrentDirectoryW(0, NULL);
    Str16 dir16 = ALLOC_SLICE(perm, count, dir16);

    count = GetCurrentDirectoryW(to_i32(dir16.len), dir16.ptr);
    assert(count == (dir16.len - 1));
    return utf8_from_utf16(perm, dir16);
}

static void os_writer_flush(WinOsWriter *w)
{
    if (!w->std_out.is_console) return;

    c16 *buf     = w->buf.mem;
    i32  buf_len = to_i32(w->buf.len);

    if (buf_len <= 0 || w->std_out.err) {
        return;
    }

    w->std_out.err = !WriteConsoleW(w->std_out.handle, buf, buf_len, NULL, 0);
    w->buf.len     = 0;
}

static void os_writer_write(WinOsWriter *w, Str in)
{
    if (w->std_out.is_console) {
        // In console mode, we need to convert our utf8 into the windows native utf16
        c16 mem[2] = {0};
        while (in.len > 0) {
            u32   cp      = codepoint_take_from_utf8(&in);
            Str16 encoded = utf16_from_codepoint(mem, cp);

            isize available = count_of(w->buf.mem) - w->buf.len;
            if (encoded.len > available) {
                os_writer_flush(w);
            }

            assert(count_of(w->buf.mem) > encoded.len);
            for (isize i = 0; i < encoded.len; ++i) {
                w->buf.mem[w->buf.len++] = encoded.ptr[i];
            }
        }
    }
    else {
        // In file or pipe mode, we can write directly.
        i32 dummy      = 0;
        w->std_out.err = !WriteFile(w->std_out.handle, in.ptr, to_i32(in.len), &dummy, 0);
    }
}

static void os_writer_write_wrapper(void *ctx, isize len, u8 *ptr)
{
    WinOsWriter *w = ctx;
    os_writer_write(w, (Str){.len = len, .ptr = ptr});
}

static Str os_stdin_read(Arena *perm, OsStream *std_in)
{
    // Some programs (like ffmpeg) have such a big makefile output that it will overflow the pipe.
    // We read in smaller chunks to make sure we can read all of it.
    enum { CHUNK_SIZE = 1 << 14 };
    Str chunk = ALLOC_SLICE(perm, CHUNK_SIZE, chunk);

    Str stdin_buf  = {0};
    i32 read_count = 0;
    while (ReadFile(std_in->handle, chunk.ptr, to_i32(chunk.len), &read_count, 0)) {
        if (read_count == 0) {
            break; //< no more data
        }

        Str chunk_data = str_take_head(chunk, read_count);
        stdin_buf      = str_concat(perm, stdin_buf, chunk_data);
    }
    return stdin_buf;
}

static StrList os_args_read(Arena *perm)
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
    isize cap      = 1 << 26; // 64 MiB
    byte *mem      = VirtualAlloc(0, cap, WIN32_MEM_COMMIT | WIN32_MEM_RESERVE, WIN32_PAGE_READWRITE);
    Arena arena[1] = {arena_init(cap, mem)};

    i32 not_needed = 0;

    OsStream std_in   = {0};
    std_in.handle     = GetStdHandle(WIN32_STD_INPUT_HANDLE);
    std_in.is_console = GetConsoleMode(std_in.handle, &not_needed);

    OsStream std_out   = {0};
    std_out.handle     = GetStdHandle(WIN32_STD_OUTPUT_HANDLE);
    std_out.is_console = GetConsoleMode(std_out.handle, &not_needed);

    OsStream std_err   = {0};
    std_err.handle     = GetStdHandle(WIN32_STD_ERROR_HANDLE);
    std_err.is_console = GetConsoleMode(std_err.handle, &not_needed);

    Str make_stdout = {0};
    if (!std_in.is_console) {
        // Only read the stdin if it's not a console (i.e. a pipe or a file)
        make_stdout = os_stdin_read(arena, &std_in);
    }

    OsWriterInterface *writer_interface = ALLOC(arena, 1, *writer_interface);
    WinOsWriter       *writer           = ALLOC(arena, 1, *writer);
    writer->std_out                     = std_out;
    writer->std_err                     = std_err;
    writer_interface->ctx               = writer;
    writer_interface->flush             = os_writer_write_wrapper;

    StrList cli_args  = os_args_read(arena);
    Str     cwd       = os_cwd_read(arena);
    i32     exit_code = make2compdb(arena, writer_interface, cli_args, make_stdout, cwd);

    print_flush(writer_interface);
    os_writer_flush(writer);
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

typedef struct {
    isize len;
    byte *ptr;
} Bytes;

static Str str_from_cstr(char *cstr)
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

static StrList strlist_from_cstrs(Arena *a, isize count, char *cstrs[static count])
{
    StrList sl = {0};
    for (isize i = 0; i < count; i++) {
        Str s = str_from_cstr(cstrs[i]);
        strlist_push_back(&sl, a, s);
    }
    return sl;
}

static Str os_cwd_read(Arena *perm)
{
    Bytes all_arena = {.ptr = perm->cursor, .len = perm->end - perm->cursor};

    char *cwd_ptr = getcwd(all_arena.ptr, to_usize(all_arena.len));
    if (cwd_ptr == NULL) return SL("");

    Str cwd       = str_from_cstr(cwd_ptr);
    perm->cursor += cwd.len; //< Commit memory in the arena
    return cwd;
}

static void os_console_write(OsStream *std_out, Str in)
{
    assert(std_out);
    if (in.len > 0 && !std_out->err) {
        std_out->err = (in.len != write((int)std_out->handle, in.ptr, to_usize(in.len)));
    }
}

static void os_console_write_wrapper(void *ctx, isize len, u8 *ptr)
{
    OsStream *std_out = ctx;
    os_console_write(std_out, (Str){.len = len, .ptr = ptr});
}

static Str os_stdin_read(Arena *perm, OsStream *std_in)
{
    // Some programs (like ffmpeg) have such a big makefile stdout that it will overflow the pipe.
    // We read in smaller chunks to make sure we can read all of it.
    enum { CHUNK_SIZE = 1 << 14 };
    Str chunk = ALLOC_SLICE(perm, CHUNK_SIZE, chunk);

    Str stdin_buf = {0};
    while (1) {
        isize len = read((int)std_in->handle, chunk.ptr, to_usize(chunk.len));
        if (len < 0) return SL("");

        if (len == 0) {
            break; //< No more data
        }

        Str chunk_data = str_take_head(chunk, len);
        stdin_buf      = str_concat(perm, stdin_buf, chunk_data);
    }
    return stdin_buf;
}

int main(int argc, char **argv)
{
    isize cap      = 1 << 26; // 64 MiB
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

    OsWriterInterface *writer_interface = ALLOC(arena, 1, *writer_interface);
    writer_interface->ctx               = &std_out;
    writer_interface->flush             = os_console_write_wrapper;

    StrList cli_args = strlist_from_cstrs(arena, argc, argv);
    Str     cwd      = os_cwd_read(arena);

    i32 exit_code = make2compdb(arena, writer_interface, cli_args, make_stdout, cwd);
    print_flush(writer_interface);
    _exit(exit_code);
    unreachable();
}
#endif
