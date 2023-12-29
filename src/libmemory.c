#if 0
# memset, memcpy, memmove, and memcmp via x86 string instructions
# Execute this source with a shell to build libmemory.a.
# This is free and unencumbered software released into the public domain.
set -e
CFLAGS="-Os -fno-builtin -fno-asynchronous-unwind-tables -fno-ident"
objects=""
for func in memset memcpy memmove memcmp strlen; do
    FUNC="$(echo $func | tr '[:lower:]' '[:upper:]')"
    objects="$objects $func.o"
    (set -x; ${CC:-cc} -c -D$FUNC -Wa,--no-pad-sections $CFLAGS -o $func.o $0)
done
rm -f "${DESTDIR}libmemory.a"
ar r "${DESTDIR}libmemory.a" $objects
rm $objects
exit 0
#endif

typedef __SIZE_TYPE__    size_t;
typedef __UINTPTR_TYPE__ uintptr_t;

#ifdef MEMSET
void *memset(void *dst, int c, size_t len)
{
    void *r = dst;
    asm volatile (
        "rep stosb"
        : "+D"(dst), "+c"(len)
        : "a"(c)
        : "memory"
    );
    return r;
}
#endif

#ifdef MEMCPY
void *memcpy(void *restrict dst, void *restrict src, size_t len)
{
    void *r = dst;
    asm volatile (
        "rep movsb"
        : "+D"(dst), "+S"(src), "+c"(len)
        :
        : "memory"
    );
    return r;
}
#endif

#ifdef MEMMOVE
void *memmove(void *dst, void *src, size_t len)
{
    // Use uintptr_t to bypass pointer semantics:
    // (1) comparing unrelated pointers
    // (2) pointer arithmetic on null (i.e. gracefully handle null dst/src)
    // (3) pointer overflow ("one-before-the-beginning" in reversed copy)
    uintptr_t d = (uintptr_t)dst;
    uintptr_t s = (uintptr_t)src;
    if (d > s) {
        d += len - 1;
        s += len - 1;
        asm ("std");
    }
    asm volatile (
        "rep movsb; cld"
        : "+D"(d), "+S"(s), "+c"(len)
        :
        : "memory"
    );
    return dst;
}
#endif

#ifdef MEMCMP
int memcmp(void *s1, void *s2, size_t len)
{
    // CCa "after"  == CF=0 && ZF=0
    // CCb "before" == CF=1
    int a, b;
    asm volatile (
        "xor %%eax, %%eax\n"  // CF=0, ZF=1 (i.e. CCa = CCb = 0)
        "repz cmpsb\n"
        : "+D"(s1), "+S"(s2), "+c"(len), "=@cca"(a), "=@ccb"(b)
        :
        : "ax", "memory"
    );
    return b - a;
}
#endif

#ifdef STRLEN
size_t strlen(char *s)
{
    size_t n = -1;
    asm volatile (
        "repne scasb"
        : "+D"(s), "+c"(n)
        : "a"(0)
        : "memory"
    );
    return -n - 2;
}
#endif

#ifdef TEST
// $ sh libmemory.c
// $ cc -nostdlib -fno-builtin -DTEST -g3 -O -o test libmemory.c libmemory.a
// $ gdb -ex r -ex q ./test

#define assert(c) while (!(c)) __builtin_trap()
void   *memset(void *, int, size_t);
int     memcmp(void *, void *, size_t);
void   *memcpy(void *restrict, void *restrict, size_t);
void   *memmove(void *, void *, size_t);
size_t  strlen(char *);

#if defined(__linux) && defined(__amd64)
asm ("        .global _start\n"
     "_start: call mainCRTStartup\n"
     "        mov  %eax, %edi\n"
     "        mov  $60, %eax\n"
     "        syscall\n");
#elif defined(__linux) && defined(__i386)
asm ("        .global _start\n"
     "_start: call mainCRTStartup\n"
     "        mov  %eax, %ebx\n"
     "        mov  $1, %eax\n"
     "        int  $0x80\n");
#endif

int mainCRTStartup(void)
{
    {
        char buf[12] = "............";
        memset(buf+4, 'x', 4);
        assert(!memcmp(buf, "....xxxx....", 12));
        memset(buf, 0, 12);
        assert(!memcmp(buf, (char[12]){0}, 12));
        memset(buf+8, 1, 0);
        assert(!memcmp(buf, (char[12]){0}, 12));
    }

    {
        char buf[7] = "abcdefg";
        memcpy(buf+0, buf+3, 3);
        assert(!memcmp(buf, "defdefg", 7));
        memcpy(buf+5, buf+1, 2);
        assert(!memcmp(buf, "defdeef", 7));
        memcpy(buf+1, buf+4, 0);
        assert(!memcmp(buf, "defdeef", 7));
    }

    {
        char buf[] = "abcdefgh";
        memmove(buf+0, buf+1, 7);
        assert(!memcmp(buf, "bcdefghh", 8));
        buf[7] = 0;
        memmove(buf+1, buf+0, 7);
        assert(!memcmp(buf, "bbcdefgh", 8));
        memmove(buf+2, buf+1, 0);
        assert(!memcmp(buf, "bbcdefgh", 8));
    }

    assert(memcmp("\xff", "1", 1) > 0);
    assert(memcmp("", "", 0) == 0);  // test empty after > result
    assert(memcmp("1", "\xff", 1) < 0);
    assert(memcmp("", "", 0) == 0);  // test empty after < result
    assert(memcmp("ab", "aa", 2) > 0);
    assert(memcmp("aa", "ab", 2) < 0);
    assert(memcmp("x", "y", 0) == 0);

    assert(0 == strlen(""));
    assert(1 == strlen(" "));
    assert(1 == strlen("\xff"));
    assert(5 == strlen("hello"));

    return 0;
}
#endif
