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

typedef __SIZE_TYPE__ size_t;

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
    void *r = dst;
    if ((size_t)dst > (size_t)src) {
        dst += len - 1;
        src += len - 1;
        asm ("std");
    }
    asm volatile (
        "rep movsb; cld"
        : "+D"(dst), "+S"(src), "+c"(len)
        :
        : "memory"
    );
    return r;
}
#endif

#ifdef MEMCMP
int memcmp(void *s1, void *s2, size_t len)
{
    int a, b;
    asm volatile (
        "test %%eax, %%eax; repz cmpsb"
        : "+D"(s1), "+S"(s2), "+c"(len), "=@cca"(a), "=@ccb"(b)
        :
        : "memory"
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
