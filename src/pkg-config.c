// u-config: a small, simple, portable pkg-config clone
// https://github.com/skeeto/u-config
//   $ cc -nostartfiles -o pkg-config.exe pkg-config.c
// This is free and unencumbered software released into the public domain.
#define VERSION "0.33.1"

typedef unsigned char    u8;
typedef   signed int     b32;
typedef   signed int     i32;
typedef unsigned int     u32;
typedef __PTRDIFF_TYPE__ size;
typedef          char    byte;

#define assert(c)     while (!(c)) __builtin_unreachable()
#define countof(a)    (size)(sizeof(a) / sizeof(*(a)))
#define new(a, t, n)  (t *)alloc(a, sizeof(t), n)
#define s8(s)         {(u8 *)s, countof(s)-1}
#define S(s)          (s8)s8(s)

typedef struct {
    u8  *s;
    size len;
} s8;

typedef struct {
    byte *beg;
    byte *end;
} arena;

typedef struct {
    arena perm;
    s8   *args;
    size  nargs;
    s8    envpath;       // $PKG_CONFIG_PATH or empty
    s8    fixedpath;     // $PKG_CONFIG_LIBDIR or default
    s8    top_builddir;  // $PKG_CONFIG_TOP_BUILD_DIR or default
    s8    sys_incpath;   // $PKG_CONFIG_SYSTEM_INCLUDE_PATH or default
    s8    sys_libpath;   // $PKG_CONFIG_SYSTEM_LIBRARY_PATH or default
    s8    print_sysinc;  // $PKG_CONFIG_ALLOW_SYSTEM_CFLAGS or empty
    s8    print_syslib;  // $PKG_CONFIG_ALLOW_SYSTEM_LIBS or empty
    b32   define_prefix;
    u8    delim;
} config;


// Platform API

// Application entry point. Returning from this function indicates the
// application itself completed successfully. However, an os_write error
// may result in a non-zero exit.
static void uconfig(config *);

enum { filemap_OK, filemap_NOTFOUND, filemap_READERR };

typedef struct {
    s8  data;
    i32 status;
} filemap;

// Load a file into memory, maybe using the arena. The path must include
// a null terminator since it may be passed directly to the OS interface.
static filemap os_mapfile(arena *, s8 path);

// Write buffer to stdout (1) or stderr (2). The platform must detect
// write errors and arrange for an eventual non-zero exit status.
static void os_write(i32 fd, s8);

// Immediately exit the program with a non-zero status.
static void os_fail(void) __attribute((noreturn));


// Application

static void oom(void)
{
    os_write(2, S("pkg-config: out of memory\n"));
    os_fail();
}

static b32 digit(u8 c)
{
    return c>='0' && c<='9';
}

static b32 whitespace(u8 c)
{
    switch (c) {
    case '\t': case '\n': case '\r': case ' ':
        return 1;
    }
    return 0;
}

static byte *fillbytes(byte *dst, byte c, size len)
{
    byte *r = dst;
    for (; len; len--) {
        *dst++ = c;
    }
    return r;
}

static void u8copy(u8 *dst, u8 *src, size n)
{
    assert(n >= 0);
    for (; n; n--) {
        *dst++ = *src++;
    }
}

static i32 u8compare(u8 *a, u8 *b, size n)
{
    for (; n; n--) {
        i32 d = *a++ - *b++;
        if (d) return d;
    }
    return 0;
}

static b32 pathsep(u8 c)
{
    return c=='/' || c=='\\';
}

__attribute((malloc, alloc_size(3, 2)))
static byte *alloc(arena *a, size objsize, size count)
{
    assert(objsize > 0);
    assert(count >= 0);
    size alignment = -((u32)objsize * (u32)count) & 7;
    size available = a->end - a->beg - alignment;
    if (count > available/objsize) {
        oom();
    }
    size total = objsize * count;
    return fillbytes(a->end -= total + alignment, 0, total);
}

static s8 news8(arena *perm, size len)
{
    s8 r = {0};
    r.s = new(perm, u8, len);
    r.len = len;
    return r;
}

static s8 s8span(u8 *beg, u8 *end)
{
    assert(beg);
    assert(end);
    assert(end >= beg);
    s8 s = {0};
    s.s = beg;
    s.len = end - beg;
    return s;
}

// Copy src into dst returning the remaining portion of dst.
static s8 s8copy(s8 dst, s8 src)
{
    assert(dst.len >= src.len);
    u8copy(dst.s, src.s, src.len);
    dst.s += src.len;
    dst.len -= src.len;
    return dst;
}

static b32 s8equals(s8 a, s8 b)
{
    return a.len==b.len && !u8compare(a.s, b.s, a.len);
}

static s8 cuthead(s8 s, size off)
{
    assert(off >= 0);
    assert(off <= s.len);
    s.s += off;
    s.len -= off;
    return s;
}

static s8 takehead(s8 s, size len)
{
    assert(len >= 0);
    assert(len <= s.len);
    s.len = len;
    return s;
}

static s8 cuttail(s8 s, size len)
{
    assert(len >= 0);
    assert(len <= s.len);
    s.len -= len;
    return s;
}

static s8 taketail(s8 s, size len)
{
    return cuthead(s, s.len-len);
}

static b32 startswith(s8 s, s8 prefix)
{
    return s.len>=prefix.len && s8equals(takehead(s, prefix.len), prefix);
}

static u32 s8hash(s8 s)
{
    u32 h = 0x811c9dc5;
    for (size i = 0; i < s.len; i++) {
        h ^= s.s[i];
        h *= 0x01000193;
    }
    return h;
}

typedef struct {
    s8 head;
    s8 tail;
} s8pair;

static s8pair digits(s8 s)
{
    size len = 0;
    for (; len<s.len && digit(s.s[len]); len++) {}
    s8pair r = {0};
    r.head = takehead(s, len);
    r.tail = cuthead(s, len);
    return r;
}

static b32 tokenspace(u8 c)
{
    return whitespace(c) || c==',';
}

static s8 skiptokenspace(s8 s)
{
    for (; s.len && tokenspace(*s.s); s = cuthead(s, 1)) {}
    return s;
}

static s8pair nexttoken(s8 s)
{
    s = skiptokenspace(s);
    size len = 0;
    for (; len<s.len && !tokenspace(s.s[len]); len++) {}
    s8pair r = {0};
    r.head = takehead(s, len);
    r.tail = cuthead(s, len);
    return r;
}

typedef struct {
    s8  head;
    s8  tail;
    b32 ok;
} cut;

static cut s8cut(s8 s, u8 delim)
{
    cut r = {0};
    size len = 0;
    for (; len < s.len; len++) {
        if (s.s[len] == delim) {
            break;
        }
    }
    if (len == s.len) {
        r.head = s;
        r.tail = cuthead(s, s.len);
        return r;
    }
    r.head = takehead(s, len);
    r.tail = cuthead(s, len+1);
    r.ok = 1;
    return r;
}

// Encode paths with illegal UTF-8 bytes. Reversible. Allows white space
// and meta characters in paths to behave differently. Encode paths
// before variable assignment. Reversed by dequote() when printed.
static u8 pathencode(u8 c)
{
    // NOTE: space classification must agree with whitespace()
    switch (c) {
        case '\t': return 0xf8;
        case '\n': return 0xf9;
        case '\r': return 0xfa;
        case ' ' : return 0xfb;
        case '$' : return 0xfc;
        case '(' : return 0xfd;
        case ')' : return 0xfe;
    }
    return c;
}

static u8 pathdecode(u8 c)
{
    switch (c) {
        case 0xf8: return '\t';
        case 0xf9: return '\n';
        case 0xfa: return '\r';
        case 0xfb: return ' ' ;
        case 0xfc: return '$' ;
        case 0xfd: return '(' ;
        case 0xfe: return ')' ;
    }
    return c;
}

static s8 s8pathencode(s8 s, arena *perm)
{
    b32 encode = 0;
    for (size i = 0; i<s.len && !encode; i++) {
        encode = pathencode(s.s[i]) != s.s[i];
    }
    if (!encode) return s;  // no encoding necessary

    s8 r = news8(perm, s.len);
    for (size i = 0; i < s.len; i++) {
        r.s[i] = pathencode(s.s[i]);
    }
    return r;
}

typedef struct {
    u8    *buf;
    size   cap;
    size   len;
    arena *perm;
    i32    fd;
} u8buf;

// Buffered output for os_write().
static u8buf *newfdbuf(arena *perm, i32 fd, size cap)
{
    u8buf *b = new(perm, u8buf, 1);
    b->cap = cap;
    b->buf = new(perm, u8, cap);
    b->fd  = fd;
    return b;
}

static u8buf *newnullout(arena *perm)
{
    u8buf *b = new(perm, u8buf, 1);
    b->fd = -1;
    return b;
}

// Output to a dynamically-grown arena buffer. The arena cannot be used
// again until this buffer is finalized.
static u8buf newmembuf(arena *perm)
{
    u8buf b = {0};
    b.buf  = (u8 *)perm->beg;
    b.cap  = perm->end - perm->beg;
    b.perm = perm;
    return b;
}

static s8 gets8(u8buf *b)
{
    s8 s = {0};
    s.s = b->buf;
    s.len = b->len;
    return s;
}

// Close the stream and release the arena, returning the result buffer.
static s8 finalize(u8buf *b)
{
    assert(!b->fd);
    b->perm->beg += b->len;
    return gets8(b);
}

static void flush(u8buf *b)
{
    switch (b->fd) {
    case -1: break;  // /dev/null
    case  0: oom();
             break;
    default: if (b->len) {
                 os_write(b->fd, gets8(b));
             }
    }
    b->len = 0;
}

static void prints8(u8buf *b, s8 s)
{
    if (b->fd == -1) {
        return;  // /dev/null
    }
    for (size off = 0; off < s.len;) {
        size avail = b->cap - b->len;
        size count = avail<s.len-off ? avail : s.len-off;
        u8copy(b->buf+b->len, s.s+off, count);
        b->len += count;
        off += count;
        if (b->len == b->cap) {
            flush(b);
        }
    }
}

static void printu8(u8buf *b, u8 c)
{
    prints8(b, s8span(&c, &c+1));
}

typedef struct env env;
struct env {
    env *child[4];
    s8   name;
    s8   value;
};

// Return a pointer to the binding so that the caller can bind it. The
// arena is optional. If given, the binding will be created and set to a
// null string. A null pointer is a valid empty environment.
static s8 *insert(env **e, s8 name, arena *perm)
{
    for (u32 h = s8hash(name); *e; h <<= 2) {
        if (s8equals((*e)->name, name)) {
            return &(*e)->value;
        }
        e = &(*e)->child[h>>30];
    }
    if (!perm) {
        return 0;
    }
    *e = new(perm, env, 1);
    (*e)->name = name;
    return &(*e)->value;
}

// Try to find the binding in the global environment, then failing that,
// the second environment. Returns a null string if no entry was found.
// A null pointer is valid for lookups.
static s8 lookup(env *global, env *env, s8 name)
{
    s8 *s = 0;
    s8 null = {0};
    s = s ? s : insert(&global, name, 0);
    s = s ? s : insert(&env,    name, 0);
    s = s ? s : &null;
    return *s;
}

static s8 dirname(s8 path)
{
    size len = path.len;
    while (len>0 && !pathsep(path.s[--len])) {}
    return takehead(path, len);
}

static s8 basename(s8 path)
{
    size len = path.len;
    for (; len>0 && !pathsep(path.s[len-1]); len--) {}
    return taketail(path, path.len-len);
}

static s8 buildpath(s8 dir, s8 pc, arena *perm)
{
    s8 sep = S("/");
    s8 suffix = S(".pc\0");
    size pathlen = dir.len + sep.len + pc.len + suffix.len;
    s8 path = news8(perm, pathlen);
    s8 p = path;
    p = s8copy(p, dir);
    p = s8copy(p, sep);
    p = s8copy(p, pc);
        s8copy(p, suffix);
    return path;
}

enum { pkg_DIRECT=1<<0, pkg_PUBLIC=1<<1 };

typedef struct pkg pkg;
struct pkg {
    pkg *child[4];
    pkg *list;  // total load order list
    s8   path;
    s8   realname;
    s8   contents;
    env *env;
    i32  flags;

    #define PKG_NFIELDS 10
    s8 name;
    s8 description;
    s8 url;
    s8 version;
    s8 requires;
    s8 requiresprivate;
    s8 conflicts;
    s8 libs;
    s8 libsprivate;
    s8 cflags;
};

static s8 *fieldbyid(pkg *p, i32 id)
{
    assert(id >= 0);
    assert(id < PKG_NFIELDS);
    return &p->name + id;
}

static s8 *fieldbyname(pkg *p, s8 name)
{
    static const s8 fields[] = {
        s8("Name"),
        s8("Description"),
        s8("URL"),
        s8("Version"),
        s8("Requires"),
        s8("Requires.private"),
        s8("Conflicts"),
        s8("Libs"),
        s8("Libs.private"),
        s8("Cflags")
    };
    for (i32 i = 0; i < countof(fields); i++) {
        if (s8equals(fields[i], name)) {
            return fieldbyid(p, i);
        }
    }
    return 0;
}

typedef struct {
    pkg  *pkgs;
    pkg  *head;
    pkg **tail;
    size  count;
} pkgs;

static pkgs *newpkgs(arena *perm)
{
    pkgs *p = new(perm, pkgs, 1);
    p->tail = &p->head;
    return p;
}

// Locate a previously-loaded package, or allocate zero-initialized
// space in the set for a new package.
static pkg *locate(pkgs *t, s8 realname, arena *perm)
{
    pkg **p = &t->pkgs;
    for (u32 h = s8hash(realname); *p; h <<= 2) {
        if (s8equals((*p)->realname, realname)) {
            return *p;
        }
        p = &(*p)->child[h>>30];
    }

    *p = new(perm, pkg, 1);
    (*p)->realname = realname;
    t->count++;
    *t->tail = *p;
    t->tail = &(*p)->list;
    return *p;
}

enum { parse_OK, parse_DUPFIELD, parse_DUPVARABLE };

typedef struct {
    pkg pkg;
    s8  dupname;
    i32 err;
} parseresult;

// Return the number of escape bytes at the beginning of the input.
static size escaped(s8 s)
{
    if (startswith(s, S("\\\n"))) {
        return 2;
    }
    if (startswith(s, S("\\\r\n"))) {
        return 3;
    }
    return 0;
}

// Return a copy of the input with the escapes squashed out.
static s8 stripescapes(arena *perm, s8 s)
{
    size len = 0;
    s8 c = news8(perm, s.len);
    for (size i = 0; i < s.len; i++) {
        u8 b = s.s[i];
        if (b == '\\') {
            size r = escaped(cuthead(s, i));
            if (r) {
                i += r - 1;
            } else if (i<s.len-1 && s.s[i+1]=='#') {
                // do not store escape
            } else {
                c.s[len++] = b;
            }
        } else {
            c.s[len++] = b;
        }
    }
    return takehead(c, len);
}

static parseresult parsepackage(s8 src, arena *perm)
{
    u8 *p = src.s;
    u8 *e = src.s + src.len;
    parseresult result = {0};
    result.err = parse_OK;
    result.pkg.contents = src;

    while (p < e) {
        for (; p<e && whitespace(*p); p++) {}
        if (p<e && *p=='#') {
            while (p<e && *p++!='\n') {}
            continue;
        }

        u8 *beg = p;
        u8 *end = p;
        u8 c = 0;
        while (p < e) {
            c = *p++;
            if (c=='\n' || c=='=' || c==':') {
                break;
            }
            end = whitespace(c) ? end : p;
        }

        s8 name = s8span(beg, end);
        s8 *field = 0;
        switch (c) {
        default:
            continue;

        case '=':
            field = insert(&result.pkg.env, name, perm);
            if (field->s) {
                parseresult dup = {0};
                dup.dupname = name;
                dup.err = parse_DUPVARABLE;
                return dup;
            }
            break;

        case ':':
            field = fieldbyname(&result.pkg, name);
            if (field && field->s) {
                parseresult dup = {0};
                dup.dupname = name;
                dup.err = parse_DUPFIELD;
                return dup;
            }
            break;
        }

        // Skip leading space; newlines may be escaped with a backslash
        while (p < e) {
            if (*p == '\\') {
                size r = escaped(s8span(p, e));
                if (r) {
                    p += r;
                } else {
                    break;
                }
            } else if (*p=='\n' || !whitespace(*p)) {
                break;
            } else {
                p++;
            }
        }

        b32 cleanup = 0;
        end = beg = p;
        for (; p<e && *p!='\n'; p++) {
            if (*p == '#') {
                while (p<e && *p++!='\n') {}
                break;
            } else if (*p == '\\') {
                if (p<e-1 && p[1]=='#') {
                    // Escaped #, include in token and skip over
                    p++;
                    end = p + 1;
                    cleanup = 1;
                }
                size r = escaped(s8span(p, e));
                if (r) {
                    // Escaped newline, skip over
                    p += r - 1;
                    cleanup = 1;
                }
            } else {
                end = whitespace(*p) ? end : p+1;
            }
        }

        if (field) {
            *field = s8span(beg, end);
            if (cleanup) {
                // Field contains excess characters. Contents must be
                // modified, so save a copy of it instead.
                *field = stripescapes(perm, *field);
            }
        }
    }

    return result;
}

static void missing(u8buf *err, s8 option)
{
    prints8(err, S("pkg-config: "));
    prints8(err, S("argument missing for -"));
    prints8(err, option);
    prints8(err, S("\n"));
    flush(err);
    os_fail();
}

typedef struct {
    size nargs;
    s8  *args;
    size index;
    b32  dashdash;
} options;

static options newoptions(s8 *args, size nargs)
{
    options r = {0};
    r.nargs = nargs;
    r.args  = args;
    return r;
}

typedef struct {
    s8  arg;
    s8  value;
    b32 isoption;
    b32 ok;
} optresult;

static optresult nextoption(options *p)
{
    optresult r = {0};

    if (p->index == p->nargs) {
        return r;
    }

    for (;;) {
        s8 arg = p->args[p->index++];

        if (p->dashdash || arg.len<2 || arg.s[0]!='-') {
            r.arg = arg;
            r.ok = 1;
            return r;
        }

        if (!p->dashdash && s8equals(arg, S("--"))) {
            p->dashdash = 1;
            continue;
        }

        r.isoption = 1;
        r.ok = 1;
        arg = cuthead(arg, 1);
        cut c = s8cut(arg, '=');
        if (c.ok) {
            r.arg = c.head;
            r.value = c.tail;
        } else {
            r.arg = arg;
        }
        return r;
    }
}

static s8 getargopt(u8buf *err, options *p, s8 option)
{
    if (p->index == p->nargs) {
        missing(err, option);
    }
    return p->args[p->index++];
}

static void usage(u8buf *b)
{
    static const u8 usage[] =
    "u-config " VERSION " https://github.com/skeeto/u-config\n"
    "free and unencumbered software released into the public domain\n"
    "usage: pkg-config [OPTIONS...] [PACKAGES...]\n"
    "  --cflags, --cflags-only-I, --cflags-only-other\n"
    "  --define-prefix, --dont-define-prefix\n"
    "  --define-variable=NAME=VALUE, --variable=NAME\n"
    "  --exists, --validate, --{atleast,exact,max}-version=VERSION\n"
    "  --errors-to-stdout\n"
    "  --keep-system-cflags, --keep-system-libs\n"
    "  --libs, --libs-only-L, --libs-only-l, --libs-only-other\n"
    "  --maximum-traverse-depth=N\n"
    "  --modversion\n"
    "  --msvc-syntax\n"
    "  --newlines\n"
    "  --silence-errors\n"
    "  --static\n"
    "  --with-path=PATH\n"
    "  -h, --help, --version\n"
    "environment:\n"
    "  PKG_CONFIG_PATH\n"
    "  PKG_CONFIG_LIBDIR\n"
    "  PKG_CONFIG_TOP_BUILD_DIR\n"
    "  PKG_CONFIG_SYSTEM_INCLUDE_PATH\n"
    "  PKG_CONFIG_SYSTEM_LIBRARY_PATH\n"
    "  PKG_CONFIG_ALLOW_SYSTEM_CFLAGS\n"
    "  PKG_CONFIG_ALLOW_SYSTEM_LIBS\n";
    prints8(b, S(usage));
}

typedef struct s8node s8node;
struct s8node {
    s8node *next;
    s8      str;
};

typedef struct {
    s8node  *head;
    s8node **tail;
} s8list;

static void append(s8list *list, s8 str, arena *perm)
{
    if (!list->tail) {
        list->tail = &list->head;
    }
    s8node *node = new(perm, s8node, 1);
    node->str = str;
    *list->tail = node;
    list->tail = &node->next;
}

typedef struct {
    s8list list;
    u8     delim;
} search;

static search newsearch(u8 delim)
{
    search r = {0};
    r.delim = delim;
    return r;
}

static void appendpath(search *dirs, s8 path, arena *perm)
{
    while (path.len) {
        cut c = s8cut(path, dirs->delim);
        s8 dir = c.head;
        if (dir.len) {
            append(&dirs->list, dir, perm);
        }
        path = c.tail;
    }
}

static void prependpath(search *dirs, s8 path, arena *perm)
{
    if (!dirs->list.head) {
        // Empty, so appending is the same a prepending
        appendpath(dirs, path, perm);
    } else {
        // Append to an empty Search, then transplant in front
        search temp = newsearch(dirs->delim);
        appendpath(&temp, path, perm);
        *temp.list.tail = dirs->list.head;
        dirs->list.head = temp.list.head;
    }
}

static b32 realnameispath(s8 realname)
{
    return realname.len>3 && s8equals(taketail(realname, 3), S(".pc"));
}

static s8 pathtorealname(s8 path)
{
    if (!realnameispath(path)) {
        return path;
    }

    size baselen = 0;
    for (size i = 0; i < path.len; i++) {
        if (pathsep(path.s[i])) {
            baselen = i + 1;
        }
    }
    s8 name = cuthead(path, baselen);
    return cuttail(name, 3);
}

static s8 readpackage(u8buf *err, s8 path, s8 realname, arena *perm)
{
    if (s8equals(realname, S("pkg-config"))) {
        return S(
            "Name: u-config\n"
            "Version: " VERSION "\n"
            "Description:\n"
        );
    }

    s8 null = {0};
    filemap m = os_mapfile(perm, path);
    switch (m.status) {
    case filemap_NOTFOUND:
        return null;

    case filemap_READERR:
        prints8(err, S("pkg-config: "));
        prints8(err, S("could not read package '"));
        prints8(err, realname);
        prints8(err, S("' from '"));
        prints8(err, path);
        prints8(err, S("'\n"));
        flush(err);
        os_fail();

    case filemap_OK:
        return m.data;
    }
    assert(0);
}

static void expand(u8buf *out, u8buf *err, env *global, pkg *p, s8 str)
{
    i32 top = 0;
    s8 stack[128];

    stack[top] = str;
    while (top >= 0) {
        s8 s = stack[top--];
        for (size i = 0; i < s.len-1; i++) {
            if (s.s[i]=='$' && s.s[i+1]=='{') {
                if (top >= countof(stack)-2) {
                    prints8(err, S("pkg-config: "));
                    prints8(err, S("exceeded max recursion depth in '"));
                    prints8(err, p->path);
                    prints8(err, S("'\n"));
                    flush(err);
                    os_fail();
                }

                prints8(out, takehead(s, i));

                size beg = i + 2;
                size end = beg;
                for (; end<s.len && s.s[end]!='}'; end++) {}
                s8 name = s8span(s.s+beg, s.s+end);
                end += end < s.len;

                // If the tail is empty, this stack push could be elided
                // as a kind of tail call optimization. However, there
                // would need to be another mechanism in place to detect
                // infinite recursion.
                s8 tail = cuthead(s, end);
                stack[++top] = tail;

                s8 value = lookup(global, p->env, name);
                if (!value.s) {
                    prints8(err, S("pkg-config: "));
                    prints8(err, S("undefined variable '"));
                    prints8(err, name);
                    prints8(err, S("' in '"));
                    prints8(err, p->path);
                    prints8(err, S("'\n"));
                    flush(err);
                    os_fail();
                }
                stack[++top] = value;
                s.len = 0;
                break;

            } else if (s.s[i]=='$' && s.s[i+1]=='$') {
                s8 head = takehead(s, i+1);
                prints8(out, head);
                stack[++top] = cuthead(s, i+2);
                s.len = 0;
                break;
            }
        }
        prints8(out, s);
    }
}

// Merge and expand data from "update" into "base".
static void expandmerge(u8buf *err, env *g, pkg *base, pkg *update, arena *perm)
{
    base->path = update->path;
    base->contents = update->contents;
    base->env = update->env;
    base->flags = update->flags;
    for (i32 i = 0; i < PKG_NFIELDS; i++) {
        u8buf mem = newmembuf(perm);
        s8 src = *fieldbyid(update, i);
        expand(&mem, err, g, update, src);
        *fieldbyid(base, i) = finalize(&mem);
    }
}

static pkg findpackage(search *dirs, u8buf *err, s8 realname, arena *perm)
{
    s8 path = {0};
    s8 contents = {0};

    if (realnameispath(realname)) {
        path = news8(perm, realname.len+1);
        s8copy(path, realname).s[0] = 0;
        contents = readpackage(err, path, realname, perm);
        path = cuttail(path, 1);  // remove null terminator
        if (contents.s) {
            realname = pathtorealname(path);
        }
    }

    for (s8node *n = dirs->list.head; n && !contents.s; n = n->next) {
        path = buildpath(n->str, realname, perm);
        contents = readpackage(err, path, realname, perm);
        path = cuttail(path, 1);  // remove null terminator
    }

    if (!contents.s) {
        prints8(err, S("pkg-config: "));
        prints8(err, S("could not find package '"));
        prints8(err, realname);
        prints8(err, S("'\n"));
        flush(err);
        os_fail();
    }

    parseresult r = parsepackage(contents, perm);
    switch (r.err) {
    case parse_DUPVARABLE:
        prints8(err, S("pkg-config: "));
        prints8(err, S("duplicate variable '"));
        prints8(err, r.dupname);
        prints8(err, S("' in '"));
        prints8(err, path);
        prints8(err, S("'\n"));
        flush(err);
        os_fail();

    case parse_DUPFIELD:
        prints8(err, S("pkg-config: "));
        prints8(err, S("duplicate field '"));
        prints8(err, r.dupname);
        prints8(err, S("' in '"));
        prints8(err, path);
        prints8(err, S("'\n"));
        flush(err);
        os_fail();

    case parse_OK:
        break;
    }
    r.pkg.path = path;
    r.pkg.realname = realname;
    s8 pcfiledir = s8pathencode(dirname(path), perm);
    *insert(&r.pkg.env, S("pcfiledir"), perm) = pcfiledir;

    s8 missing = {0};
    if (!r.pkg.name.s) {
        missing = S("Name");
    } else if (!r.pkg.version.s) {
        missing = S("Version");
    } else if (!r.pkg.description.s) {
        missing = S("Description");
    }
    if (missing.s) {
        prints8(err, S("pkg-config: "));
        prints8(err, S("missing field '"));
        prints8(err, missing);
        prints8(err, S("' in '"));
        prints8(err, r.pkg.path);
        prints8(err, S("'\n"));
        flush(err);
        #ifndef FUZZTEST
        // Do not enforce during fuzzing
        os_fail();
        #endif
    }

    return r.pkg;
}

typedef struct {
    s8  arg;
    s8  tail;
    b32 ok;
} dequoted;

// Matches pkg-config's listing, which excludes "$()", but also match
// pathencode()ed bytes for escaping, which handles "$()" in paths.
static b32 shellmeta(u8 c)
{
    s8 meta = S("\"!#%&'*<>?[\\]`{|}");
    for (size i = 0; i < meta.len; i++) {
        if (meta.s[i]==c || pathdecode(c)!=c) {
            return 1;
        }
    }
    return 0;
}

// Process the next token. Return it and the unprocessed remainder.
static dequoted dequote(s8 s, arena *perm)
{
    size i = 0;
    u8 quote = 0;
    b32 escaped = 0;
    dequoted r = {0};
    arena rollback = *perm;
    u8buf mem = newmembuf(perm);

    for (; s.len && whitespace(*s.s); s = cuthead(s, 1)) {}

    for (i = 0; i < s.len; i++) {
        u8 c = s.s[i];
        if (whitespace(c)) {
            c = ' ';
        }
        u8 decoded = pathdecode(c);

        if (quote == '\'') {
            if (c == '\'') {
                quote = 0;
            } else if (c==' ' || shellmeta(c)) {
                printu8(&mem, '\\');
                printu8(&mem, decoded);
            } else {
                printu8(&mem, decoded);
            }

        } else if (quote == '"') {
            if (escaped) {
                escaped = 0;
                if (c!='\\' && c!='"') {
                    printu8(&mem, '\\');
                    if (c==' ' || shellmeta(c)) {
                        printu8(&mem, '\\');
                    }
                }
                printu8(&mem, decoded);
            } else if (c == '\"') {
                quote = 0;
            } else if (c==' ' || shellmeta(c)) {
                printu8(&mem, '\\');
                printu8(&mem, decoded);
            } else {
                escaped = c == '\\';
                printu8(&mem, decoded);
            }

        } else if (c=='\'' || c=='"') {
            quote = c;

        } else if (shellmeta(c)) {
            printu8(&mem, '\\');
            printu8(&mem, decoded);

        } else if (c==' ') {
            break;

        } else {
            printu8(&mem, c);
        }
    }

    if (quote) {
        *perm = rollback;
        return r;
    }

    r.arg  = finalize(&mem);
    r.tail = cuthead(s, i);
    r.ok   = 1;
    return r;
}

// Compare version strings, returning [-1, 0, +1]. Follows the RPM
// version comparison specification like the original pkg-config.
static i32 compareversions(s8 va, s8 vb)
{
    size i = 0;
    while (i<va.len && i<vb.len) {
        u8 a = va.s[i];
        u8 b = vb.s[i];
        if (!digit(a) || !digit(b)) {
            if (a < b) {
                return -1;
            } else if (a > b) {
                return +1;
            }
            i++;
        } else {
            s8pair pa = digits(cuthead(va, i));
            s8pair pb = digits(cuthead(vb, i));
            if (pa.head.len < pb.head.len) {
                return -1;
            } else if (pa.head.len > pb.head.len) {
                return +1;
            }
            for (i = 0; i < pa.head.len; i++) {
                a = pa.head.s[i];
                b = pb.head.s[i];
                if (a < b) {
                    return -1;
                } else if (a > b) {
                    return +1;
                }
            }
            va = pa.tail;
            vb = pb.tail;
            i = 0;
        }
    }
    if (va.len < vb.len) {
        return -1;
    } else if (va.len > vb.len) {
        return +1;
    }
    return 0;
}

typedef enum {
    versop_ERR=0,
    versop_LT,
    versop_LTE,
    versop_EQ,
    versop_GTE,
    versop_GT
} versop;

static versop parseop(s8 s)
{
    if (s8equals(S("<"), s)) {
        return versop_LT;
    } else if (s8equals(S("<="), s)) {
        return versop_LTE;
    } else if (s8equals(S("="), s)) {
        return versop_EQ;
    } else if (s8equals(S(">="), s)) {
        return versop_GTE;
    } else if (s8equals(S(">"), s)) {
        return versop_GT;
    }
    return versop_ERR;
}

static s8 opname(versop op)
{
    switch (op) {
    case versop_ERR: break;
    case versop_LT:  return S("<");
    case versop_LTE: return S("<=");
    case versop_EQ:  return S("=");
    case versop_GTE: return S(">=");
    case versop_GT:  return S(">");
    }
    assert(0);
}

static b32 validcompare(versop op, i32 result)
{
    switch (op) {
    case versop_ERR: break;
    case versop_LT:  return result <  0;
    case versop_LTE: return result <= 0;
    case versop_EQ:  return result == 0;
    case versop_GTE: return result >= 0;
    case versop_GT:  return result >  0;
    }
    assert(0);
}

typedef struct {
    s8     arg;
    pkg   *last;
    i32    depth;
    i32    flags;
    versop op;
} procstate;

typedef struct {
    u8buf    *err;
    search    search;
    env     **global;
    pkgs     *pkgs;
    pkg      *last;
    i32       maxdepth;
    versop    op;
    b32       define_prefix;
    b32       recursive;
    b32       ignore_versions;
    procstate stack[256];
} processor;

static processor *newprocessor(config *c, u8buf *err, env **g, pkgs *pkgs)
{
    arena *perm = &c->perm;
    processor *proc = new(perm, processor, 1);
    proc->err = err;
    proc->search = newsearch(c->delim);
    appendpath(&proc->search, c->envpath, perm);
    appendpath(&proc->search, c->fixedpath, perm);
    proc->global = g;
    proc->pkgs = pkgs;
    proc->maxdepth = (u32)-1 >> 1;
    proc->define_prefix = 1;
    proc->recursive = 1;
    return proc;
}

static void procfail(u8buf *err, versop op, pkg *p)
{
    prints8(err, S("pkg-config: "));
    prints8(err, S("expected version following operator "));
    prints8(err, opname(op));
    if (p) {
        prints8(err, S(" in package '"));
        prints8(err, p->realname);
        prints8(err, S("'"));
    }
    prints8(err, S("\n"));
    flush(err);
    os_fail();
}

static void setprefix(pkg *p, arena *perm)
{
    s8 parent = dirname(p->path);
    if (s8equals(S("pkgconfig"), basename(parent))) {
        s8 prefix = dirname(dirname(parent));
        prefix = s8pathencode(prefix, perm);
        *insert(&p->env, S("prefix"), perm) = prefix;
    }
}

static void failmaxrecurse(u8buf *err, s8 tok)
{
    prints8(err, S("pkg-config: "));
    prints8(err, S("exceeded max recursion depth on '"));
    prints8(err, tok);
    prints8(err, S("'\n"));
    flush(err);
    os_fail();
}

static void failversion(u8buf *err, pkg *pkg, versop op, s8 want)
{
    prints8(err, S("pkg-config: "));
    prints8(err, S("requested '"));
    prints8(err, pkg->realname);
    prints8(err, S("' "));
    prints8(err, opname(op));
    prints8(err, S(" '"));
    prints8(err, want);
    prints8(err, S("' but got '"));
    prints8(err, pkg->version);
    prints8(err, S("'\n"));
    flush(err);
    os_fail();
}

static void process(processor *proc, s8 arg, arena *perm)
{
    u8buf *err = proc->err;
    pkgs *pkgs = proc->pkgs;
    env **global = proc->global;
    search *search = &proc->search;

    procstate *stack = proc->stack;
    i32 cap = countof(proc->stack);
    i32 top = 0;
    stack[0].arg = arg;
    stack[0].last = proc->last;
    stack[0].depth = 0;
    stack[0].flags = pkg_DIRECT | pkg_PUBLIC;
    stack[0].op = proc->op;

    while (top >= 0) {
        procstate *s = stack + top;
        s8pair pair = nexttoken(s->arg);
        s8 tok = pair.head;
        if (!tok.len) {
            if (top>0 && s->op) {
                procfail(err, s->op, s->last);
            }
            top--;
            continue;
        }
        stack[top].arg = pair.tail;

        if (s->op) {
            if (!proc->ignore_versions) {
                i32 cmp = compareversions(s->last->version, tok);
                if (!validcompare(s->op, cmp)) {
                    failversion(err, s->last, s->op, tok);
                }
            }
            s->last = 0;
            s->op = versop_ERR;
            continue;
        }

        s->op = parseop(tok);
        if (s->op) {
            if (!s->last) {
                prints8(err, S("pkg-config: "));
                prints8(err, S("unexpected operator '"));
                prints8(err, tok);
                prints8(err, S("'\n"));
                flush(err);
                os_fail();
            }
            continue;
        }

        i32 depth = s->depth + 1;
        i32 flags = s->flags;
        pkg *p = s->last = locate(pkgs, pathtorealname(tok), perm);
        if (p->contents.s) {
            if (flags&pkg_PUBLIC && !(p->flags & pkg_PUBLIC)) {
                // We're on a public branch, but this package was
                // previously loaded as private. Recursively traverse
                // its public requires and mark all as public.
                p->flags |= pkg_PUBLIC;
                if (proc->recursive && depth<proc->maxdepth) {
                    if (top >= cap-1) {
                        failmaxrecurse(err, tok);
                    }
                    top++;
                    stack[top].arg = p->requires;
                    stack[top].last = 0;
                    stack[top].depth = depth;
                    stack[top].flags = flags & ~pkg_DIRECT;
                    stack[top].op = versop_ERR;
                }
            }
        } else {
            // Package hasn't been loaded yet, so find and load it.
            pkg newpkg = findpackage(search, err, tok, perm);
            if (proc->define_prefix) {
                setprefix(&newpkg, perm);
            }
            expandmerge(err, *global, p, &newpkg, perm);
            if (proc->recursive && depth<proc->maxdepth) {
                if (top >= cap-2) {
                    failmaxrecurse(err, tok);
                }
                top++;
                stack[top].arg = p->requiresprivate;
                stack[top].last = 0;
                stack[top].depth = depth;
                stack[top].flags = 0;
                stack[top].op = versop_ERR;
                top++;
                stack[top].arg = p->requires;
                stack[top].last = 0;
                stack[top].depth = depth;
                stack[top].flags = flags & ~pkg_DIRECT;
                stack[top].op = versop_ERR;
            }
        }
        p->flags |= flags;
    }

    proc->last = stack[0].last;
    proc->op = stack[0].op;
}

static void endprocessor(processor *proc, u8buf *err)
{
    if (proc->op) {
        procfail(err, proc->op, 0);
    }
}

typedef enum {
    filter_ANY,
    filter_I,
    filter_L,
    filter_l,
    filter_OTHERC,
    filter_OTHERL
} filter;

static b32 filterok(filter f, s8 arg)
{
    switch (f) {
    case filter_ANY:
        return 1;
    case filter_I:
        return startswith(arg, S("-I"));
    case filter_L:
        return startswith(arg, S("-L"));
    case filter_l:
        return startswith(arg, S("-l"));
    case filter_OTHERC:
        return !startswith(arg, S("-I"));
    case filter_OTHERL:
        return !startswith(arg, S("-L")) && !startswith(arg, S("-l"));
    }
    assert(0);
}

static void msvcize(u8buf *out, s8 arg)
{
    if (startswith(arg, S("-L"))) {
        prints8(out, S("/libpath:"));
        prints8(out, cuthead(arg, 2));
    } else if (startswith(arg, S("-I"))) {
        prints8(out, S("/I"));
        prints8(out, cuthead(arg, 2));
    } else if (startswith(arg, S("-l"))) {
        prints8(out, cuthead(arg, 2));
        prints8(out, S(".lib"));
    } else if (startswith(arg, S("-D"))) {
        prints8(out, S("/D"));
        prints8(out, cuthead(arg, 2));
    } else if (s8equals(arg, S("-mwindows"))) {
        prints8(out, S("/subsystem:windows"));
    } else if (s8equals(arg, S("-mconsole"))) {
        prints8(out, S("/subsystem:console"));
    } else {
        prints8(out, arg);
    }
}

typedef struct argpos argpos;
struct argpos {
    argpos *child[4];
    argpos *next;
    s8      arg;
    size    position;
};

typedef struct {
    s8list  list;
    argpos *positions;
    size    count;
} args;

static argpos *findargpos(argpos **m, s8 arg, arena *perm)
{
    for (u32 h = s8hash(arg); *m; h <<= 2) {
        if (s8equals((*m)->arg, arg)) {
            return *m;
        }
        m = &(*m)->child[h>>30];
    }
    if (perm) {
        *m = new(perm, argpos, 1);
        (*m)->arg = arg;
    }
    return *m;
}

static b32 dedupable(s8 arg)
{
    // Do not count "-I" or "-L" with detached argument
    if (arg.len<3 || arg.s[0]!='-') {
        return 0;
    } else if (s8equals(arg, S("-pthread"))) {
        return 1;
    }
    s8 flags = S("DILflm");
    for (size i = 0; i < flags.len; i++) {
        if (arg.s[1] == flags.s[i]) {
            return 1;
        }
    }
    return 0;
}

static void appendarg(args *args, s8 arg, arena *perm)
{
    append(&args->list, arg, perm);
    size position = args->count++;
    if (dedupable(arg)) {
        argpos *n = findargpos(&args->positions, arg, perm);
        if (!n->position || startswith(arg, S("-l"))) {
            // Zero position reserved for null, so bias it by 1
            n->position = 1 + position;
        }
    }
}

static void excludearg(args *args, s8 arg, arena *perm)
{
    argpos *n = findargpos(&args->positions, arg, perm);
    n->position = -1;  // i.e. position before first argument
}

// Is this the correct position for the given argument?
static b32 inposition(args *args, s8 arg, size position)
{
    argpos *n = findargpos(&args->positions, arg, 0);
    return !n || n->position==position+1;
}

typedef struct {
    arena *perm;
    size  *argcount;
    args   args;
    filter filter;
    b32    msvc;
    u8     delim;
} fieldwriter;

static fieldwriter newfieldwriter(filter f, size *argcount, arena *perm)
{
    fieldwriter w = {0};
    w.perm = perm;
    w.filter = f;
    w.argcount = argcount;
    return w;
}

static void insertsyspath(fieldwriter *w, s8 path, u8 delim, u8 flag)
{
    arena *perm = w->perm;

    u8 flagbuf[3] = {0};
    flagbuf[0] = '-';
    flagbuf[1] = flag;
    s8 prefix = S(flagbuf);

    while (path.len) {
        cut c = s8cut(path, delim);
        s8 dir = c.head;
        path = c.tail;
        if (!dir.len) {
            continue;
        }

        // Prepend option flag
        dir = s8pathencode(dir, perm);
        s8 syspath = news8(perm, prefix.len+dir.len);
        s8copy(s8copy(syspath, prefix), dir);

        // Process as an argument, as though being printed
        dequoted dr = dequote(syspath, perm);
        syspath = dr.arg;

        // NOTE(NRK): Technically, the path doesn't need to follow the flag
        // immediately e.g `-I /usr/include` (notice the space between -I and
        // the include dir!).
        //
        // But I have not seen a single pc file that does this and so we're not
        // handling this edge-case. As a proof that this should be fine in
        // practice, `pkgconf` which is used by many distros, also doesn't
        // handle it.
        if (dr.ok && !dr.tail.len) {
            excludearg(&w->args, syspath, perm);
        }
    }
}

static void appendfield(u8buf *err, fieldwriter *w, pkg *p, s8 field)
{
    arena *perm = w->perm;
    filter f = w->filter;
    while (field.len) {
        dequoted r = dequote(field, perm);
        if (!r.ok) {
            prints8(err, S("pkg-config: "));
            prints8(err, S("unmatched quote in '"));
            prints8(err, p->realname);
            prints8(err, S("'\n"));
            flush(err);
            os_fail();
        }
        if (filterok(f, r.arg)) {
            appendarg(&w->args, r.arg, perm);
        }
        field = r.tail;
    }
}

static void writeargs(u8buf *out, fieldwriter *w)
{
    size position = 0;
    u8 delim = w->delim ? w->delim : ' ';
    for (s8node *n = w->args.list.head; n; n = n->next) {
        s8 arg = n->str;
        if (inposition(&w->args, arg, position++)) {
            if ((*w->argcount)++) {
                printu8(out, delim);
            }
            if (w->msvc) {
                msvcize(out, arg);
            } else {
                prints8(out, arg);
            }
        }
    }
}

static i32 parseuint(s8 s, i32 hi)
{
    i32 v = 0;
    for (size i = 0; i < s.len; i++) {
        u8 c = s.s[i];
        if (digit(c)) {
            v = v*10 + c - '0';
            if (v >= hi) {
                return hi;
            }
        }
    }
    return v;
}

static void uconfig(config *conf)
{
    arena *perm = &conf->perm;

    env *global = 0;
    filter filterc = filter_ANY;
    filter filterl = filter_ANY;
    pkgs *pkgs = newpkgs(perm);
    u8buf *out = newfdbuf(perm, 1, 1<<12);
    u8buf *err = newfdbuf(perm, 2, 1<<7);
    processor *proc = newprocessor(conf, err, &global, pkgs);
    size argcount = 0;

    b32 msvc = 0;
    b32 libs = 0;
    b32 cflags = 0;
    b32 err_to_stdout = 0;
    b32 silent = 0;
    b32 static_ = 0;
    u8 argdelim = ' ';
    b32 modversion = 0;
    versop override_op = versop_ERR;
    s8 override_version = {0};
    b32 print_sysinc = !!conf->print_sysinc.s;
    b32 print_syslib = !!conf->print_syslib.s;
    s8 variable = {0};

    proc->define_prefix = conf->define_prefix;
    s8 top_builddir = conf->top_builddir;
    if (top_builddir.s) {
        top_builddir = s8pathencode(top_builddir, perm);
    } else {
        top_builddir = S("$(top_builddir)");
    }

    *insert(&global, S("pc_path"), perm) = conf->fixedpath;
    *insert(&global, S("pc_system_includedirs"), perm) = conf->sys_incpath;
    *insert(&global, S("pc_system_libdirs"), perm) = conf->sys_libpath;
    *insert(&global, S("pc_sysrootdir"), perm) = S("/");
    *insert(&global, S("pc_top_builddir"), perm) = top_builddir;

    s8 *args = new(perm, s8, conf->nargs);
    size nargs = 0;

    for (options opts = newoptions(conf->args, conf->nargs);;) {
        optresult r = nextoption(&opts);
        if (!r.ok) {
            break;
        }

        if (!r.isoption) {
            args[nargs++] = r.arg;

        } else if (s8equals(r.arg, S("h")) || s8equals(r.arg, S("-help"))) {
            usage(out);
            flush(out);
            return;

        } else if (s8equals(r.arg, S("-version"))) {
            prints8(out, S(VERSION));
            printu8(out, '\n');
            flush(out);
            return;

        } else if (s8equals(r.arg, S("-modversion"))) {
            modversion = 1;

        } else if (s8equals(r.arg, S("-define-prefix"))) {
            proc->define_prefix = 1;

        } else if (s8equals(r.arg, S("-dont-define-prefix"))) {
            proc->define_prefix = 0;

        } else if (s8equals(r.arg, S("-cflags"))) {
            cflags = 1;
            filterc = filter_ANY;

        } else if (s8equals(r.arg, S("-libs"))) {
            libs = 1;
            filterl = filter_ANY;

        } else if (s8equals(r.arg, S("-variable"))) {
            if (!r.value.s) {
                r.value = getargopt(err, &opts, r.arg);
            }
            variable = r.value;

        } else if (s8equals(r.arg, S("-static"))) {
            static_ = 1;

        } else if (s8equals(r.arg, S("-libs-only-L"))) {
            libs = 1;
            filterl = filter_L;

        } else if (s8equals(r.arg, S("-libs-only-l"))) {
            libs = 1;
            filterl = filter_l;

        } else if (s8equals(r.arg, S("-libs-only-other"))) {
            libs = 1;
            filterl = filter_OTHERL;

        } else if (s8equals(r.arg, S("-cflags-only-I"))) {
            cflags = 1;
            filterc = filter_I;

        } else if (s8equals(r.arg, S("-cflags-only-other"))) {
            cflags = 1;
            filterc = filter_OTHERC;

        } else if (s8equals(r.arg, S("-with-path"))) {
            if (!r.value.s) {
                r.value = getargopt(err, &opts, r.arg);
            }
            prependpath(&proc->search, r.value, perm);

        } else if (s8equals(r.arg, S("-maximum-traverse-depth"))) {
            if (!r.value.s) {
                r.value = getargopt(err, &opts, r.arg);
            }
            proc->maxdepth = parseuint(r.value, 1000);

        } else if (s8equals(r.arg, S("-msvc-syntax"))) {
            msvc = 1;

        } else if (s8equals(r.arg, S("-define-variable"))) {
            if (!r.value.s) {
                r.value = getargopt(err, &opts, r.arg);
            }
            cut c = s8cut(r.value, '=');
            if (!c.ok) {
                prints8(err, S("pkg-config: "));
                prints8(err, S("value missing in --define-variable for '"));
                prints8(err, r.value);
                prints8(err, S("'\n"));
                flush(err);
                os_fail();
            }
            *insert(&global, c.head, perm) = c.tail;

        } else if (s8equals(r.arg, S("-newlines"))) {
            argdelim = '\n';

        } else if (s8equals(r.arg, S("-exists"))) {
            // The check already happens, just disable the messages
            silent = 1;

        } else if (s8equals(r.arg, S("-atleast-pkgconfig-version"))) {
            if (!r.value.s) {
                r.value = getargopt(err, &opts, r.arg);
            }
            return;  // always succeeds

        } else if (s8equals(r.arg, S("-atleast-version"))) {
            if (!r.value.s) {
                r.value = getargopt(err, &opts, r.arg);
            }
            override_op = versop_GTE;
            override_version = r.value;
            silent = 1;
            proc->recursive = 0;
            proc->ignore_versions = 1;

        } else if (s8equals(r.arg, S("-exact-version"))) {
            if (!r.value.s) {
                r.value = getargopt(err, &opts, r.arg);
            }
            override_op = versop_EQ;
            override_version = r.value;
            silent = 1;
            proc->recursive = 0;
            proc->ignore_versions = 1;

        } else if (s8equals(r.arg, S("-max-version"))) {
            if (!r.value.s) {
                r.value = getargopt(err, &opts, r.arg);
            }
            override_op = versop_LTE;
            override_version = r.value;
            silent = 1;
            proc->recursive = 0;
            proc->ignore_versions = 1;

        } else if (s8equals(r.arg, S("-silence-errors"))) {
            silent = 1;

        } else if (s8equals(r.arg, S("-errors-to-stdout"))) {
            err_to_stdout = 1;

        } else if (s8equals(r.arg, S("-print-errors"))) {
            // Ignore

        } else if (s8equals(r.arg, S("-short-errors"))) {
            // Ignore

        } else if (s8equals(r.arg, S("-uninstalled"))) {
            // Ignore

        } else if (s8equals(r.arg, S("-keep-system-cflags"))) {
            print_sysinc = 1;

        } else if (s8equals(r.arg, S("-keep-system-libs"))) {
            print_syslib = 1;

        } else if (s8equals(r.arg, S("-validate"))) {
            silent = 1;
            proc->recursive = 0;

        } else {
            prints8(err, S("pkg-config: "));
            prints8(err, S("unknown option -"));
            prints8(err, r.arg);
            prints8(err, S("\n"));
            flush(err);
            os_fail();
        }
    }

    if (err_to_stdout) {
        err = out;
    }

    if (silent) {
        err = newnullout(perm);
    }

    for (size i = 0; i < nargs; i++) {
        process(proc, args[i], perm);
    }
    endprocessor(proc, err);

    if (!pkgs->count) {
        prints8(err, S("pkg-config: "));
        prints8(err, S("requires at least one package name\n"));
        flush(err);
        os_fail();
    }

    // --{atleast,exact,max}-version
    if (override_op) {
        for (pkg *p = pkgs->head; p; p = p->list) {
            i32 cmp = compareversions(p->version, override_version);
            if (!validcompare(override_op, cmp)) {
                failversion(err, p, override_op, override_version);
            }
        }
    }

    if (modversion) {
        for (pkg *p = pkgs->head; p; p = p->list) {
            if (p->flags & pkg_DIRECT) {
                prints8(out, p->version);
                prints8(out, S("\n"));
            }
        }
    }

    if (variable.s) {
        for (pkg *p = pkgs->head; p; p = p->list) {
            if (p->flags & pkg_DIRECT) {
                s8 value = lookup(global, p->env, variable);
                if (value.s) {
                    expand(out, err, global, p, value);
                    prints8(out, S("\n"));
                }
            }
        }
    }

    if (cflags) {
        arena scratch = *perm;
        fieldwriter fw = newfieldwriter(filterc, &argcount, &scratch);
        fw.delim = argdelim;
        fw.msvc = msvc;
        if (!print_sysinc) {
            insertsyspath(&fw, conf->sys_incpath, conf->delim, 'I');
        }
        for (pkg *p = pkgs->head; p; p = p->list) {
            appendfield(err, &fw, p, p->cflags);
        }
        writeargs(out, &fw);
    }

    if (libs) {
        arena scratch = *perm;
        fieldwriter fw = newfieldwriter(filterl, &argcount, &scratch);
        fw.delim = argdelim;
        fw.msvc = msvc;
        if (!print_syslib) {
            insertsyspath(&fw, conf->sys_libpath, conf->delim, 'L');
        }
        for (pkg *p = pkgs->head; p; p = p->list) {
            if (static_) {
                appendfield(err, &fw, p, p->libs);
                appendfield(err, &fw, p, p->libsprivate);
            } else if (p->flags & pkg_PUBLIC) {
                appendfield(err, &fw, p, p->libs);
            }
        }
        writeargs(out, &fw);
    }

    if (cflags || libs) {
        prints8(out, S("\n"));
    }

    flush(out);
}

// Win32 types, constants, and declarations (replaces windows.h)
// This is free and unencumbered software released into the public domain.

typedef __SIZE_TYPE__  uptr;
typedef unsigned short char16_t;
typedef char16_t       c16;

enum {
    FILE_ATTRIBUTE_NORMAL = 0x80,

    FILE_SHARE_ALL = 7,

    GENERIC_READ = 0x80000000,

    INVALID_HANDLE_VALUE = -1,

    MEM_COMMIT  = 0x1000,
    MEM_RESERVE = 0x2000,

    OPEN_EXISTING = 3,

    PAGE_READWRITE = 4,

    STD_OUTPUT_HANDLE = -11,
    STD_ERROR_HANDLE  = -12,
};

#define W32(r) __declspec(dllimport) r __stdcall
W32(b32)    CloseHandle(uptr);
W32(i32)    CreateFileW(c16 *, i32, i32, uptr, i32, i32, i32);
W32(void)   ExitProcess(i32);
W32(c16 *)  GetCommandLineW(void);
W32(b32)    GetConsoleMode(uptr, i32 *);
W32(i32)    GetEnvironmentVariableW(c16 *, c16 *, i32);
W32(i32)    GetModuleFileNameW(uptr, c16 *, i32);
W32(i32)    GetStdHandle(i32);
W32(b32)    ReadFile(uptr, u8 *, i32, i32 *, uptr);
W32(byte *) VirtualAlloc(uptr, size, i32, i32);
W32(b32)    WriteConsoleW(uptr, c16 *, i32, i32 *, uptr);
W32(b32)    WriteFile(uptr, u8 *, i32, i32 *, uptr);

#define CMDLINE_CMD_MAX  32767  // max command line length on Windows
#define CMDLINE_ARGV_MAX (16384+(98298+(i32)sizeof(u8 *))/(i32)sizeof(u8 *))

// Convert an ill-formed-UTF-16 command line to a WTF-8 argv following
// field splitting semantics identical to CommandLineToArgvW, including
// undocumented behavior. Populates argv with pointers into itself and
// returns argc, which is always positive.
//
// Expects that cmd has no more than 32,767 (CMDLINE_CMD_MAX) elements
// including the null terminator, and argv has at least CMDLINE_ARGV_MAX
// elements. This covers the worst possible cases for a Windows command
// string, so no further allocation is ever necessary.
//
// Unlike CommandLineToArgvW, when the command line string is zero
// length this function does not invent an artificial argv[0] based on
// the calling module file name. To implement this behavior yourself,
// test if cmd[0] is zero and then act accordingly.
//
// If the input is UTF-16, then the output is UTF-8.
static i32 cmdline_to_argv8(c16 *cmd, u8 **argv)
{
    i32 argc  = 1;  // worst case: argv[0] is an empty string
    i32 state = 6;  // special argv[0] state
    i32 slash = 0;
    // Use second half as byte buffer
    u8 *buf = (u8 *)(argv + 16384);

    argv[0] = buf;
    while (*cmd) {
        i32 c = *cmd++;
        if (c>>10 == 0x36 && *cmd>>10 == 0x37) {  // surrogates?
            c = 0x10000 + ((c - 0xd800)<<10) + (*cmd++ - 0xdc00);
        }

        switch (state) {
        case 0: switch (c) {  // outside token
                case 0x09:
                case 0x20: continue;
                case 0x22: argv[argc++] = buf;
                           state = 2;
                           continue;
                case 0x5c: argv[argc++] = buf;
                           slash = 1;
                           state = 3;
                           break;
                default  : argv[argc++] = buf;
                           state = 1;
                } break;
        case 1: switch (c) {  // inside unquoted token
                case 0x09:
                case 0x20: *buf++ = 0;
                           state = 0;
                           continue;
                case 0x22: state = 2;
                           continue;
                case 0x5c: slash = 1;
                           state = 3;
                           break;
                } break;
        case 2: switch (c) {  // inside quoted token
                case 0x22: state = 5;
                           continue;
                case 0x5c: slash = 1;
                           state = 4;
                           break;
                } break;
        case 3:
        case 4: switch (c) {  // backslash sequence
                case 0x22: buf -= (1 + slash) >> 1;
                           if (slash & 1) {
                               state -= 2;
                               break;
                           } // fallthrough
                default  : cmd -= 1 + (c >= 0x10000);
                           state -= 2;
                           continue;
                case 0x5c: slash++;
                } break;
        case 5: switch (c) {  // quoted token exit
                default  : cmd -= 1 + (c >= 0x10000);
                           state = 1;
                           continue;
                case 0x22: state = 1;
                } break;
        case 6: switch (c) {  // begin argv[0]
                case 0x09:
                case 0x20: *buf++ = 0;
                           state = 0;
                           continue;
                case 0x22: state = 8;
                           continue;
                default  : state = 7;
                } break;
        case 7: switch (c) {  // unquoted argv[0]
                case 0x09:
                case 0x20: *buf++ = 0;
                           state = 0;
                           continue;
                } break;
        case 8: switch (c) {  // quoted argv[0]
                case 0x22: *buf++ = 0;
                           state = 0;
                           continue;
                } break;
        }

        // WTF-8/UTF-8 encoding
        switch ((c >= 0x80) + (c >= 0x800) + (c >= 0x10000)) {
        case 0: *buf++ = (u8)(0x00 | ((c >>  0)     )); break;
        case 1: *buf++ = (u8)(0xc0 | ((c >>  6)     ));
                *buf++ = (u8)(0x80 | ((c >>  0) & 63)); break;
        case 2: *buf++ = (u8)(0xe0 | ((c >> 12)     ));
                *buf++ = (u8)(0x80 | ((c >>  6) & 63));
                *buf++ = (u8)(0x80 | ((c >>  0) & 63)); break;
        case 3: *buf++ = (u8)(0xf0 | ((c >> 18)     ));
                *buf++ = (u8)(0x80 | ((c >> 12) & 63));
                *buf++ = (u8)(0x80 | ((c >>  6) & 63));
                *buf++ = (u8)(0x80 | ((c >>  0) & 63)); break;
        }
    }

    *buf = 0;
    argv[argc] = 0;
    return argc;
}

// Mingw-w64 Win32 platform layer for u-config
// $ cc -nostartfiles -o pkg-config win32_main.c
// This is free and unencumbered software released into the public domain.

#ifndef PKG_CONFIG_PREFIX
#  define PKG_CONFIG_PREFIX
#endif

// For communication with os_write()
static struct {
    i32 handle;
    b32 isconsole;
    b32 err;
} handles[3];

typedef struct {
    c16 *s;
    size len;
} s16;

static s16 s16cuthead_(s16 s, size off)
{
    assert(off >= 0);
    assert(off <= s.len);
    s.s += off;
    s.len -= off;
    return s;
}

static arena newarena_(size cap)
{
    arena arena = {0};
    arena.beg = VirtualAlloc(0, cap, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
    if (!arena.beg) {
        arena.beg = (byte *)16;  // aligned, non-null, zero-size arena
        cap = 0;
    }
    arena.end = arena.beg + cap;
    return arena;
}

typedef i32 char32_t;
typedef char32_t c32;

enum {
    REPLACEMENT_CHARACTER = 0xfffd
};

typedef struct {
    s8  tail;
    c32 rune;
} utf8;

static utf8 utf8decode_(s8 s)
{
    assert(s.len);
    utf8 r = {0};
    switch (s.s[0]&0xf0) {
    default  : r.rune = s.s[0];
               if (r.rune > 0x7f) break;
               r.tail = cuthead(s, 1);
               return r;
    case 0xc0:
    case 0xd0: if (s.len < 2) break;
               if ((s.s[1]&0xc0) != 0x80) break;
               r.rune = (i32)(s.s[0]&0x1f) << 6 |
                        (i32)(s.s[1]&0x3f) << 0;
               if (r.rune < 0x80) break;
               r.tail = cuthead(s, 2);
               return r;
    case 0xe0: if (s.len < 3) break;
               if ((s.s[1]&0xc0) != 0x80) break;
               if ((s.s[2]&0xc0) != 0x80) break;
               r.rune = (i32)(s.s[0]&0x0f) << 12 |
                        (i32)(s.s[1]&0x3f) <<  6 |
                        (i32)(s.s[2]&0x3f) <<  0;
               if (r.rune < 0x800) break;
               if (r.rune>=0xd800 && r.rune<=0xdfff) break;
               r.tail = cuthead(s, 3);
               return r;
    case 0xf0: if (s.len < 4) break;
               if ((s.s[1]&0xc0) != 0x80) break;
               if ((s.s[2]&0xc0) != 0x80) break;
               if ((s.s[3]&0xc0) != 0x80) break;
               r.rune = (i32)(s.s[0]&0x0f) << 18 |
                        (i32)(s.s[1]&0x3f) << 12 |
                        (i32)(s.s[2]&0x3f) <<  6 |
                        (i32)(s.s[3]&0x3f) <<  0;
               if (r.rune < 0x10000) break;
               if (r.rune > 0x10ffff) break;
               r.tail = cuthead(s, 4);
               return r;
    }
    r.rune = REPLACEMENT_CHARACTER;
    r.tail = cuthead(s, 1);
    return r;
}

// Encode code point returning the output length (1-4).
static i32 utf8encode_(u8 *s, c32 rune)
{
    if (rune<0 || (rune>=0xd800 && rune<=0xdfff) || rune>0x10ffff) {
        rune = REPLACEMENT_CHARACTER;
    }
    switch ((rune >= 0x80) + (rune >= 0x800) + (rune >= 0x10000)) {
    case 0: s[0] = (u8)(0x00 | ((rune >>  0)     )); return 1;
    case 1: s[0] = (u8)(0xc0 | ((rune >>  6)     ));
            s[1] = (u8)(0x80 | ((rune >>  0) & 63)); return 2;
    case 2: s[0] = (u8)(0xe0 | ((rune >> 12)     ));
            s[1] = (u8)(0x80 | ((rune >>  6) & 63));
            s[2] = (u8)(0x80 | ((rune >>  0) & 63)); return 3;
    case 3: s[0] = (u8)(0xf0 | ((rune >> 18)     ));
            s[1] = (u8)(0x80 | ((rune >> 12) & 63));
            s[2] = (u8)(0x80 | ((rune >>  6) & 63));
            s[3] = (u8)(0x80 | ((rune >>  0) & 63)); return 4;
    }
    assert(0);
}

typedef struct {
    s16 tail;
    c32 rune;
} utf16;

static utf16 utf16decode_(s16 s)
{
    assert(s.len);
    utf16 r = {0};
    r.rune = s.s[0];
    if (r.rune>=0xdc00 && r.rune<=0xdfff) {
        goto reject;  // unpaired low surrogate
    } else if (r.rune>=0xd800 && r.rune<=0xdbff) {
        if (s.len < 2) {
            goto reject;  // missing low surrogate
        }
        i32 hi = r.rune;
        i32 lo = s.s[1];
        if (lo<0xdc00 || lo>0xdfff) {
            goto reject;  // expected low surrogate
        }
        r.rune = 0x10000 + ((hi - 0xd800)<<10) + (lo - 0xdc00);
        r.tail = s16cuthead_(s, 2);
        return r;
    }
    r.tail = s16cuthead_(s, 1);
    return r;

    reject:
    r.rune = REPLACEMENT_CHARACTER;
    r.tail = s16cuthead_(s, 1);
    return r;
}

// Encode code point returning the output length (1-2).
static i32 utf16encode_(c16 *dst, c32 rune)
{
    if (rune<0 || (rune>=0xd800 && rune<=0xdfff) || rune>0x10ffff) {
        rune = REPLACEMENT_CHARACTER;
    }
    if (rune >= 0x10000) {
        rune -= 0x10000;
        dst[0] = (c16)((rune >> 10) + 0xd800);
        dst[1] = (c16)((rune&0x3ff) + 0xdc00);
        return 2;
    }
    dst[0] = (c16)rune;
    return 1;
}

static s16 towide_(arena *perm, s8 s)
{
    size len = 0;
    utf8 state = {0};
    state.tail = s;
    while (state.tail.len) {
        state = utf8decode_(state.tail);
        c16 tmp[2];
        len += utf16encode_(tmp, state.rune);
    }

    s16 w = {0};
    w.s = new(perm, c16, len);
    state.tail = s;
    while (state.tail.len) {
        state = utf8decode_(state.tail);
        w.len += utf16encode_(w.s+w.len, state.rune);
    }
    return w;
}

static s8 fromwide_(arena *perm, s16 w)
{
    size len = 0;
    utf16 state = {0};
    state.tail = w;
    while (state.tail.len) {
        state = utf16decode_(state.tail);
        u8 tmp[4];
        len += utf8encode_(tmp, state.rune);
    }

    s8 s = {0};
    s.s = new(perm, u8, len);
    state.tail = w;
    while (state.tail.len) {
        state = utf16decode_(state.tail);
        s.len += utf8encode_(s.s+s.len, state.rune);
    }
    return s;
}

static s8 fromenv_(arena *perm, c16 *name)
{
    // Given no buffer, unset variables report as size 0, while empty
    // variables report as size 1 for the null terminator.
    i32 wlen = GetEnvironmentVariableW(name, 0, 0);
    if (!wlen) {
        s8 r = {0};
        return r;
    }

    // Store temporarily at the beginning of the arena.
    size cap = (perm->end - perm->beg) / (size)sizeof(c16);
    if (wlen > cap) {
        oom();
    }
    s16 wvar = {0};
    wvar.s   = (c16 *)perm->beg;
    wvar.len = wlen - 1;
    GetEnvironmentVariableW(name, wvar.s, wlen);

    byte *save = perm->beg;
    perm->beg = (byte *)(wvar.s + wvar.len);
    s8 var = fromwide_(perm, wvar);
    perm->beg = save;

    return var;
}

static i32 truncsize(size len)
{
    i32 max = 0x7fffffff;
    return len>max ? max : (i32)len;
}

static s8 installdir_(arena *perm)
{
    byte *save = perm->beg;

    // GetModuleFileNameW does not communicate length. It only indicates
    // success (buffer large enough) or failure (result truncated). To
    // make matters worse, long paths have no fixed upper limit, though
    // 64KiB is given as an approximate. To deal with this, offer the
    // entire free region of the arena, far exceeding any path length.
    //
    // Computing sizes outside of the allocator isn't great, but the
    // situation is constrained by this crummy API.
    s16 exe   = {0};
    exe.s     = (c16 *)perm->beg;
    i32 cap   = truncsize(perm->end - perm->beg) / (i32)sizeof(c16);
    exe.len   = GetModuleFileNameW(0, exe.s, cap);
    perm->beg = (byte *)(exe.s + exe.len);

    // Normalize by converting backslashes to slashes
    for (size i = 0; i < exe.len; i++) {
        if (exe.s[i] == '\\') {
            exe.s[i] = '/';
        }
    }

    s8 path = fromwide_(perm, exe);
    perm->beg = save;  // free the wide path
    return dirname(dirname(path));
}

static s8 append2_(arena *perm, s8 pre, s8 suf)
{
    s8 s = news8(perm, pre.len+suf.len);
    s8copy(s8copy(s, pre), suf);
    return s;
}

static s8 makepath_(arena *perm, s8 base, s8 lib, s8 share)
{
    s8 delim = S(";");
    size len = base.len + lib.len + delim.len + base.len + share.len;
    s8 s = news8(perm, len);
    s8 r = s8copy(s, base);
       r = s8copy(r, lib);
       r = s8copy(r, delim);
       r = s8copy(r, base);
           s8copy(r, share);
    return s;
}

static s8 fromcstr_(u8 *z)
{
    s8 s = {0};
    s.s = z;
    if (s.s) {
        for (; s.s[s.len]; s.len++) {}
    }
    return s;
}

static config *newconfig_()
{
    arena perm = newarena_(1<<22);
    config *conf = new(&perm, config, 1);
    conf->perm = perm;
    return conf;
}

__attribute((force_align_arg_pointer))
void mainCRTStartup(void)
{
    config *conf = newconfig_();
    conf->delim = ';';
    conf->define_prefix = 1;
    arena *perm = &conf->perm;

    i32 dummy;
    handles[1].handle = GetStdHandle(STD_OUTPUT_HANDLE);
    handles[1].isconsole = GetConsoleMode(handles[1].handle, &dummy);
    handles[2].handle = GetStdHandle(STD_ERROR_HANDLE);
    handles[2].isconsole = GetConsoleMode(handles[2].handle, &dummy);

    u8 **argv = new(perm, u8 *, CMDLINE_ARGV_MAX);
    c16 *cmdline = GetCommandLineW();
    conf->nargs = cmdline_to_argv8(cmdline, argv) - 1;
    conf->args = new(perm, s8, conf->nargs);
    for (size i = 0; i < conf->nargs; i++) {
        conf->args[i] = fromcstr_(argv[i+1]);
    }

    s8 base = installdir_(perm);
    conf->envpath = fromenv_(perm, L"PKG_CONFIG_PATH");
    conf->fixedpath = fromenv_(perm, L"PKG_CONFIG_LIBDIR");
    if (!conf->fixedpath.s) {
        s8 lib   = S(PKG_CONFIG_PREFIX "/lib/pkgconfig");
        s8 share = S(PKG_CONFIG_PREFIX "/share/pkgconfig");
        conf->fixedpath = makepath_(perm, base, lib, share);
    }
    conf->top_builddir = fromenv_(perm, L"PKG_CONFIG_TOP_BUILD_DIR");
    conf->sys_incpath  = append2_(perm, base, S(PKG_CONFIG_PREFIX "/include"));
    conf->sys_libpath  = append2_(perm, base, S(PKG_CONFIG_PREFIX "/lib"));
    conf->print_sysinc = fromenv_(perm, L"PKG_CONFIG_ALLOW_SYSTEM_CFLAGS");
    conf->print_syslib = fromenv_(perm, L"PKG_CONFIG_ALLOW_SYSTEM_LIBS");

    uconfig(conf);
    ExitProcess(handles[1].err || handles[2].err);
    assert(0);
}

static filemap os_mapfile(arena *perm, s8 path)
{
    assert(path.len > 0);
    assert(!path.s[path.len-1]);

    filemap r = {0};

    i32 handle = 0;
    {
        arena scratch = *perm;
        s16 wpath = towide_(&scratch, path);
        handle = CreateFileW(
            wpath.s,
            GENERIC_READ,
            FILE_SHARE_ALL,
            0,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            0
        );
        if (handle == INVALID_HANDLE_VALUE) {
            r.status = filemap_NOTFOUND;
            return r;
        }
    }

    r.data.s = (u8 *)perm->beg;
    size cap = perm->end - perm->beg;
    while (r.data.len < cap) {
        i32 len = truncsize(cap - r.data.len);
        ReadFile(handle, r.data.s+r.data.len, len, &len, 0);
        if (len < 1) {
            break;
        }
        r.data.len += len;
    }
    CloseHandle(handle);

    if (r.data.len == cap) {
        // If it filled all available space, assume the file is too large.
        r.status = filemap_READERR;
        return r;
    }

    perm->beg += r.data.len;
    r.status = filemap_OK;
    return r;
}

static void os_fail(void)
{
    ExitProcess(1);
    assert(0);
}

typedef struct {
    c16 buf[1<<8];
    i32 len;
    i32 handle;
    b32 err;
} u16buf;

static void flushconsole_(u16buf *b)
{
    if (!b->err && b->len) {
        i32 dummy;
        b->err = !WriteConsoleW(b->handle, b->buf, b->len, &dummy, 0);
    }
    b->len = 0;
}

static void printc32_(u16buf *b, c32 rune)
{
    if (b->len > countof(b->buf)-2) {
        flushconsole_(b);
    }
    b->len += utf16encode_(b->buf+b->len, rune);
}

static void os_write(i32 fd, s8 s)
{
    assert((i32)s.len == s.len);  // NOTE: assume it's not a huge buffer
    assert(fd==1 || fd==2);

    b32 *err = &handles[fd].err;
    if (*err) {
        return;
    }

    i32 handle = handles[fd].handle;
    if (handles[fd].isconsole) {
        // NOTE: There is a small chance that a multi-byte code point
        // spans flushes from the application. With no decoder state
        // tracked between os_write calls, this will mistranslate for
        // console outputs. The application could avoid such flushes,
        // which would require a distinct "close" flush before exits.
        //
        // Alternatively, the platform layer could detect truncated
        // encodings and buffer up to 3 bytes between calls. This buffer
        // would need to be flushed on exit by the platform.
        //
        // The primary use case for u-config is non-console outputs into
        // a build system, which will not experience this issue. Console
        // output is mainly for human-friendly debugging, so the risk is
        // acceptable.
        u16buf b = {0};
        b.handle = handle;
        utf8 state = {0};
        state.tail = s;
        while (state.tail.len) {
            state = utf8decode_(state.tail);
            printc32_(&b, state.rune);
        }
        flushconsole_(&b);
        *err = b.err;
    } else {
        i32 dummy;
        *err = !WriteFile(handle, s.s, (i32)s.len, &dummy, 0);
    }
}
