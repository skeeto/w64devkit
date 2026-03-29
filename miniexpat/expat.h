// miniexpat: minimal expat-compatible XML parser for GDB
// This is free and unencumbered software released into the public domain.
//
// Implements just enough of the expat API for GDB's xml-support.c. Handles
// namespace resolution, entity expansion, XML declarations, DOCTYPEs, and
// the DefaultCurrent/StopParser machinery needed for XInclude processing.
//
// Single-header library: #define EXPAT_IMPL in exactly one C++ translation
// unit before including this header to generate the implementation.
//
// Build as a static library for GDB:
//   c++ -DEXPAT_IMPL -include expat.h -xc++ -c -o expat.o - </dev/null
//   ar rcs libexpat.a expat.o
#ifndef EXPAT_H
#define EXPAT_H

#ifdef __cplusplus
extern "C" {
#endif

#define XML_MAJOR_VERSION 2
#define XML_MINOR_VERSION 0
#define XML_MICRO_VERSION 0

typedef char XML_Char;
typedef unsigned char XML_Bool;
#define XML_TRUE  ((XML_Bool)1)
#define XML_FALSE ((XML_Bool)0)

#ifndef XMLCALL
#define XMLCALL
#endif

typedef struct XML_ParserStruct *XML_Parser;

enum XML_Status {
    XML_STATUS_ERROR = 0,
    XML_STATUS_OK = 1,
    XML_STATUS_SUSPENDED = 2
};

enum XML_Error {
    XML_ERROR_NONE,
    XML_ERROR_NO_MEMORY,
    XML_ERROR_SYNTAX,
    XML_ERROR_NO_ELEMENTS,
    XML_ERROR_INVALID_TOKEN,
    XML_ERROR_UNCLOSED_TOKEN,
    XML_ERROR_PARTIAL_CHAR,
    XML_ERROR_TAG_MISMATCH,
    XML_ERROR_DUPLICATE_ATTRIBUTE,
    XML_ERROR_JUNK_AFTER_DOC_ELEMENT,
    XML_ERROR_PARAM_ENTITY_REF,
    XML_ERROR_UNDEFINED_ENTITY,
    XML_ERROR_RECURSIVE_ENTITY_REF,
    XML_ERROR_ASYNC_ENTITY,
    XML_ERROR_BAD_CHAR_REF,
    XML_ERROR_BINARY_ENTITY_REF,
    XML_ERROR_ATTRIBUTE_EXTERNAL_ENTITY_REF,
    XML_ERROR_MISPLACED_XML_PI,
    XML_ERROR_UNKNOWN_ENCODING,
    XML_ERROR_INCORRECT_ENCODING,
    XML_ERROR_UNCLOSED_CDATA_SECTION,
    XML_ERROR_EXTERNAL_ENTITY_HANDLING,
    XML_ERROR_NOT_STANDALONE,
    XML_ERROR_UNEXPECTED_STATE,
    XML_ERROR_ENTITY_DECLARED_IN_PE,
    XML_ERROR_FEATURE_REQUIRES_XML_DTD,
    XML_ERROR_CANT_CHANGE_FEATURE_ONCE_PARSING,
    XML_ERROR_UNBOUND_PREFIX,
    XML_ERROR_UNDECLARING_PREFIX,
    XML_ERROR_INCOMPLETE_PE,
    XML_ERROR_XML_DECL,
    XML_ERROR_TEXT_DECL,
    XML_ERROR_PUBLICID,
    XML_ERROR_SUSPENDED,
    XML_ERROR_NOT_SUSPENDED,
    XML_ERROR_ABORTED,
    XML_ERROR_FINISHED,
    XML_ERROR_SUSPEND_PE,
    XML_ERROR_RESERVED_PREFIX_XML,
    XML_ERROR_RESERVED_PREFIX_XMLNS,
    XML_ERROR_RESERVED_NAMESPACE_URI
};

enum XML_Parsing {
    XML_INITIALIZED,
    XML_PARSING,
    XML_FINISHED,
    XML_SUSPENDED
};

enum XML_ParamEntityParsing {
    XML_PARAM_ENTITY_PARSING_NEVER,
    XML_PARAM_ENTITY_PARSING_UNLESS_STANDALONE,
    XML_PARAM_ENTITY_PARSING_ALWAYS
};

// Callback typedefs
typedef void (XMLCALL *XML_StartElementHandler)(
    void *userData, const XML_Char *name, const XML_Char **atts);
typedef void (XMLCALL *XML_EndElementHandler)(
    void *userData, const XML_Char *name);
typedef void (XMLCALL *XML_CharacterDataHandler)(
    void *userData, const XML_Char *s, int len);
typedef void (XMLCALL *XML_DefaultHandler)(
    void *userData, const XML_Char *s, int len);
typedef void (XMLCALL *XML_StartDoctypeDeclHandler)(
    void *userData, const XML_Char *doctypeName, const XML_Char *sysid,
    const XML_Char *pubid, int has_internal_subset);
typedef void (XMLCALL *XML_EndDoctypeDeclHandler)(void *userData);
typedef void (XMLCALL *XML_XmlDeclHandler)(
    void *userData, const XML_Char *version, const XML_Char *encoding,
    int standalone);
typedef int (XMLCALL *XML_ExternalEntityRefHandler)(
    XML_Parser parser, const XML_Char *context, const XML_Char *base,
    const XML_Char *systemId, const XML_Char *publicId);

// Parser lifecycle
XML_Parser XMLCALL XML_ParserCreate(const XML_Char *encoding);
XML_Parser XMLCALL XML_ParserCreateNS(const XML_Char *encoding, XML_Char sep);
void XMLCALL XML_ParserFree(XML_Parser parser);
enum XML_Status XMLCALL XML_Parse(
    XML_Parser parser, const char *s, int len, int isFinal);
XML_Parser XMLCALL XML_ExternalEntityParserCreate(
    XML_Parser parser, const XML_Char *context, const XML_Char *encoding);

// User data
void XMLCALL XML_SetUserData(XML_Parser parser, void *userData);
void *XMLCALL XML_GetUserData(XML_Parser parser);

// Callback setters
void XMLCALL XML_SetElementHandler(
    XML_Parser parser, XML_StartElementHandler start,
    XML_EndElementHandler end);
void XMLCALL XML_SetCharacterDataHandler(
    XML_Parser parser, XML_CharacterDataHandler handler);
void XMLCALL XML_SetDefaultHandler(
    XML_Parser parser, XML_DefaultHandler handler);
void XMLCALL XML_SetDoctypeDeclHandler(
    XML_Parser parser, XML_StartDoctypeDeclHandler start,
    XML_EndDoctypeDeclHandler end);
void XMLCALL XML_SetXmlDeclHandler(
    XML_Parser parser, XML_XmlDeclHandler handler);
void XMLCALL XML_SetExternalEntityRefHandler(
    XML_Parser parser, XML_ExternalEntityRefHandler handler);
int XMLCALL XML_SetParamEntityParsing(
    XML_Parser parser, enum XML_ParamEntityParsing parsing);

// DTD
enum XML_Error XMLCALL XML_UseForeignDTD(
    XML_Parser parser, XML_Bool useDTD);

// Utility
long XMLCALL XML_GetCurrentLineNumber(XML_Parser parser);
enum XML_Error XMLCALL XML_GetErrorCode(XML_Parser parser);
const XML_Char *XMLCALL XML_ErrorString(enum XML_Error code);
enum XML_Status XMLCALL XML_StopParser(
    XML_Parser parser, XML_Bool resumable);
void XMLCALL XML_DefaultCurrent(XML_Parser parser);

#ifdef __cplusplus
}
#endif

#ifdef EXPAT_IMPL

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <utility>

struct XML_ParserStruct {
    char ns_sep;
    void *user_data;

    XML_StartElementHandler start_handler;
    XML_EndElementHandler end_handler;
    XML_CharacterDataHandler char_handler;
    XML_DefaultHandler default_handler;
    XML_StartDoctypeDeclHandler doctype_start_handler;
    XML_EndDoctypeDeclHandler doctype_end_handler;
    XML_XmlDeclHandler xml_decl_handler;
    XML_ExternalEntityRefHandler entity_ref_handler;

    int param_entity_parsing;
    bool use_foreign_dtd;

    XML_Error error_code;
    bool stopped;

    long line;

    // Namespace stack: each frame is the xmlns decls for one element
    std::vector<std::vector<std::pair<std::string, std::string>>> ns_stack;

    // For XML_DefaultCurrent
    const char *current_event;
    int current_event_len;

    // True if created via XML_ExternalEntityParserCreate
    bool is_sub_parser;
};

static bool miniexpat_is_name_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' ||
           c == ':' || (unsigned char)c >= 0x80;
}

static bool miniexpat_is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

struct MiniexpatCtx {
    XML_Parser p;
    const char *buf;
    int len;
    int pos;

    char peek() { return pos < len ? buf[pos] : 0; }
    char get()  { char c = peek(); if (c == '\n') p->line++; pos++; return c; }
    bool at_end() { return pos >= len; }
    bool starts_with(const char *s) {
        int n = (int)strlen(s);
        return len - pos >= n && memcmp(buf + pos, s, n) == 0;
    }
    void advance(int n) {
        for (int i = 0; i < n && pos < len; i++) get();
    }
    void skip_spaces() { while (!at_end() && miniexpat_is_space(peek())) get(); }
};

static bool miniexpat_expand_entity(const char *s, int len, std::string &out)
{
    if (len == 3 && s[0] == 'a' && s[1] == 'm' && s[2] == 'p')
        { out += '&'; return true; }
    if (len == 2 && s[0] == 'l' && s[1] == 't')
        { out += '<'; return true; }
    if (len == 2 && s[0] == 'g' && s[1] == 't')
        { out += '>'; return true; }
    if (len == 4 && memcmp(s, "quot", 4) == 0)
        { out += '"'; return true; }
    if (len == 4 && memcmp(s, "apos", 4) == 0)
        { out += '\''; return true; }
    if (len >= 2 && s[0] == '#') {
        unsigned long cp = 0;
        if (s[1] == 'x') {
            for (int i = 2; i < len; i++) {
                char c = s[i];
                if (c >= '0' && c <= '9') cp = cp * 16 + (c - '0');
                else if (c >= 'a' && c <= 'f') cp = cp * 16 + (c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') cp = cp * 16 + (c - 'A' + 10);
                else return false;
            }
        } else {
            for (int i = 1; i < len; i++) {
                char c = s[i];
                if (c >= '0' && c <= '9') cp = cp * 10 + (c - '0');
                else return false;
            }
        }
        if (cp < 0x80) {
            out += (char)cp;
        } else if (cp < 0x800) {
            out += (char)(0xC0 | (cp >> 6));
            out += (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += (char)(0xE0 | (cp >> 12));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x110000) {
            out += (char)(0xF0 | (cp >> 18));
            out += (char)(0x80 | ((cp >> 12) & 0x3F));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        } else {
            return false;
        }
        return true;
    }
    return false;
}

static std::string miniexpat_decode_entities(const char *s, int len)
{
    std::string out;
    out.reserve(len);
    for (int i = 0; i < len; i++) {
        if (s[i] == '&') {
            int start = i + 1;
            int end = start;
            while (end < len && s[end] != ';') end++;
            if (end < len) {
                if (miniexpat_expand_entity(s + start, end - start, out)) {
                    i = end;
                    continue;
                }
            }
            out += s[i];
        } else {
            out += s[i];
        }
    }
    return out;
}

static std::string miniexpat_resolve_ns(XML_Parser p, const char *qname,
                                        bool is_attr)
{
    const char *colon = strchr(qname, ':');
    if (!colon) {
        if (!is_attr) {
            for (int i = (int)p->ns_stack.size() - 1; i >= 0; i--)
                for (auto &pair : p->ns_stack[i])
                    if (pair.first.empty())
                        return pair.second + p->ns_sep + qname;
        }
        return qname;
    }
    std::string prefix(qname, colon - qname);
    const char *local = colon + 1;
    for (int i = (int)p->ns_stack.size() - 1; i >= 0; i--)
        for (auto &pair : p->ns_stack[i])
            if (pair.first == prefix)
                return pair.second + p->ns_sep + local;
    return qname;
}

static void miniexpat_set_error(MiniexpatCtx &ctx, XML_Error err)
{
    ctx.p->error_code = err;
    ctx.p->stopped = true;
}

static void miniexpat_dispatch_default(XML_Parser p, const char *s, int len)
{
    if (p->default_handler)
        p->default_handler(p->user_data, s, len);
}

static void miniexpat_parse_content(MiniexpatCtx &ctx);

static void miniexpat_parse_xml_decl(MiniexpatCtx &ctx)
{
    const char *start = ctx.buf + ctx.pos;
    ctx.advance(5);
    ctx.skip_spaces();

    std::string version, encoding;
    int standalone = -1;

    while (!ctx.at_end() && !ctx.starts_with("?>")) {
        ctx.skip_spaces();
        if (ctx.starts_with("?>")) break;

        int name_start = ctx.pos;
        while (!ctx.at_end() && miniexpat_is_name_char(ctx.peek()))
            ctx.advance(1);
        std::string name(ctx.buf + name_start, ctx.pos - name_start);

        ctx.skip_spaces();
        if (ctx.get() != '=') {
            miniexpat_set_error(ctx, XML_ERROR_SYNTAX);
            return;
        }
        ctx.skip_spaces();

        char quote = ctx.get();
        if (quote != '"' && quote != '\'') {
            miniexpat_set_error(ctx, XML_ERROR_SYNTAX);
            return;
        }
        int val_start = ctx.pos;
        while (!ctx.at_end() && ctx.peek() != quote) ctx.advance(1);
        std::string val(ctx.buf + val_start, ctx.pos - val_start);
        ctx.advance(1);

        if (name == "version") version = val;
        else if (name == "encoding") encoding = val;
        else if (name == "standalone") standalone = (val == "yes") ? 1 : 0;
    }

    if (ctx.starts_with("?>")) {
        ctx.advance(2);
    } else {
        miniexpat_set_error(ctx, XML_ERROR_SYNTAX);
        return;
    }

    if (ctx.p->xml_decl_handler) {
        const char *ev_end = ctx.buf + ctx.pos;
        ctx.p->current_event = start;
        ctx.p->current_event_len = (int)(ev_end - start);
        ctx.p->xml_decl_handler(
            ctx.p->user_data,
            version.empty() ? nullptr : version.c_str(),
            encoding.empty() ? nullptr : encoding.c_str(),
            standalone);
    } else {
        const char *ev_end = ctx.buf + ctx.pos;
        miniexpat_dispatch_default(ctx.p, start, (int)(ev_end - start));
    }
}

static void miniexpat_parse_pi(MiniexpatCtx &ctx)
{
    const char *start = ctx.buf + ctx.pos;
    ctx.advance(2);
    while (!ctx.at_end() && !ctx.starts_with("?>")) ctx.advance(1);
    if (ctx.starts_with("?>")) ctx.advance(2);
    miniexpat_dispatch_default(ctx.p, start, (int)(ctx.buf + ctx.pos - start));
}

static void miniexpat_parse_comment(MiniexpatCtx &ctx)
{
    const char *start = ctx.buf + ctx.pos;
    ctx.advance(4);
    while (!ctx.at_end() && !ctx.starts_with("-->")) ctx.advance(1);
    if (ctx.starts_with("-->")) ctx.advance(3);
    miniexpat_dispatch_default(ctx.p, start, (int)(ctx.buf + ctx.pos - start));
}

static void miniexpat_parse_doctype(MiniexpatCtx &ctx)
{
    const char *start = ctx.buf + ctx.pos;
    ctx.advance(9);
    ctx.skip_spaces();

    int name_start = ctx.pos;
    while (!ctx.at_end() && miniexpat_is_name_char(ctx.peek()))
        ctx.advance(1);
    std::string doctype_name(ctx.buf + name_start, ctx.pos - name_start);
    ctx.skip_spaces();

    std::string sysid, pubid;
    int has_internal = 0;

    if (ctx.starts_with("SYSTEM")) {
        ctx.advance(6);
        ctx.skip_spaces();
        char quote = ctx.get();
        int val_start = ctx.pos;
        while (!ctx.at_end() && ctx.peek() != quote) ctx.advance(1);
        sysid.assign(ctx.buf + val_start, ctx.pos - val_start);
        ctx.advance(1);
        ctx.skip_spaces();
    } else if (ctx.starts_with("PUBLIC")) {
        ctx.advance(6);
        ctx.skip_spaces();
        char quote = ctx.get();
        int val_start = ctx.pos;
        while (!ctx.at_end() && ctx.peek() != quote) ctx.advance(1);
        pubid.assign(ctx.buf + val_start, ctx.pos - val_start);
        ctx.advance(1);
        ctx.skip_spaces();
        quote = ctx.get();
        val_start = ctx.pos;
        while (!ctx.at_end() && ctx.peek() != quote) ctx.advance(1);
        sysid.assign(ctx.buf + val_start, ctx.pos - val_start);
        ctx.advance(1);
        ctx.skip_spaces();
    }

    if (ctx.peek() == '[') {
        has_internal = 1;
        int depth = 1;
        ctx.advance(1);
        while (!ctx.at_end() && depth > 0) {
            if (ctx.peek() == '[') depth++;
            else if (ctx.peek() == ']') depth--;
            if (depth > 0) ctx.advance(1);
        }
        if (ctx.peek() == ']') ctx.advance(1);
        ctx.skip_spaces();
    }

    if (ctx.peek() == '>') ctx.advance(1);

    const char *ev_end = ctx.buf + ctx.pos;
    ctx.p->current_event = start;
    ctx.p->current_event_len = (int)(ev_end - start);

    if (ctx.p->doctype_start_handler) {
        ctx.p->doctype_start_handler(
            ctx.p->user_data,
            doctype_name.c_str(),
            sysid.empty() ? nullptr : sysid.c_str(),
            pubid.empty() ? nullptr : pubid.c_str(),
            has_internal);
    }

    if (ctx.p->stopped) return;

    if (!sysid.empty() && ctx.p->entity_ref_handler &&
        ctx.p->param_entity_parsing != XML_PARAM_ENTITY_PARSING_NEVER) {
        ctx.p->entity_ref_handler(ctx.p, nullptr, nullptr,
                                  sysid.c_str(), nullptr);
        if (ctx.p->stopped) return;
    }

    if (ctx.p->doctype_end_handler)
        ctx.p->doctype_end_handler(ctx.p->user_data);
}

static void miniexpat_parse_start_tag(MiniexpatCtx &ctx)
{
    const char *tag_start = ctx.buf + ctx.pos;
    ctx.advance(1);

    int name_start = ctx.pos;
    while (!ctx.at_end() && miniexpat_is_name_char(ctx.peek()))
        ctx.advance(1);
    std::string raw_name(ctx.buf + name_start, ctx.pos - name_start);

    std::vector<std::pair<std::string, std::string>> attrs;
    std::vector<std::pair<std::string, std::string>> ns_decls;

    ctx.skip_spaces();
    while (!ctx.at_end() && ctx.peek() != '>' && ctx.peek() != '/') {
        int aname_start = ctx.pos;
        while (!ctx.at_end() && miniexpat_is_name_char(ctx.peek()))
            ctx.advance(1);
        std::string aname(ctx.buf + aname_start, ctx.pos - aname_start);

        ctx.skip_spaces();
        if (ctx.get() != '=') {
            miniexpat_set_error(ctx, XML_ERROR_SYNTAX);
            return;
        }
        ctx.skip_spaces();

        char quote = ctx.get();
        if (quote != '"' && quote != '\'') {
            miniexpat_set_error(ctx, XML_ERROR_SYNTAX);
            return;
        }
        int val_start = ctx.pos;
        while (!ctx.at_end() && ctx.peek() != quote) ctx.advance(1);
        std::string aval = miniexpat_decode_entities(
            ctx.buf + val_start, ctx.pos - val_start);
        ctx.advance(1);
        ctx.skip_spaces();

        if (aname == "xmlns") {
            ns_decls.push_back({"", aval});
        } else if (aname.compare(0, 6, "xmlns:") == 0) {
            ns_decls.push_back({aname.substr(6), aval});
        } else {
            attrs.push_back({aname, aval});
        }
    }

    bool self_closing = false;
    if (ctx.peek() == '/') {
        self_closing = true;
        ctx.advance(1);
    }
    if (ctx.peek() == '>') ctx.advance(1);

    ctx.p->ns_stack.push_back(ns_decls);

    std::string resolved_name = miniexpat_resolve_ns(
        ctx.p, raw_name.c_str(), false);

    std::vector<std::string> resolved_attrs;
    for (auto &a : attrs) {
        resolved_attrs.push_back(
            miniexpat_resolve_ns(ctx.p, a.first.c_str(), true));
        resolved_attrs.push_back(a.second);
    }

    std::vector<const char *> atts;
    for (auto &s : resolved_attrs)
        atts.push_back(s.c_str());
    atts.push_back(nullptr);

    const char *tag_end = ctx.buf + ctx.pos;
    ctx.p->current_event = tag_start;
    ctx.p->current_event_len = (int)(tag_end - tag_start);

    if (ctx.p->start_handler) {
        ctx.p->start_handler(ctx.p->user_data, resolved_name.c_str(),
                             atts.data());
    } else {
        miniexpat_dispatch_default(
            ctx.p, tag_start, (int)(tag_end - tag_start));
    }

    if (ctx.p->stopped) return;

    if (self_closing) {
        ctx.p->current_event = tag_start;
        ctx.p->current_event_len = (int)(tag_end - tag_start);
        if (ctx.p->end_handler) {
            ctx.p->end_handler(ctx.p->user_data, resolved_name.c_str());
        } else {
            miniexpat_dispatch_default(
                ctx.p, tag_start, (int)(tag_end - tag_start));
        }
        ctx.p->ns_stack.pop_back();
    }
}

static void miniexpat_parse_end_tag(MiniexpatCtx &ctx)
{
    const char *tag_start = ctx.buf + ctx.pos;
    ctx.advance(2);

    int name_start = ctx.pos;
    while (!ctx.at_end() && miniexpat_is_name_char(ctx.peek()))
        ctx.advance(1);
    std::string raw_name(ctx.buf + name_start, ctx.pos - name_start);

    ctx.skip_spaces();
    if (ctx.peek() == '>') ctx.advance(1);

    std::string resolved_name = miniexpat_resolve_ns(
        ctx.p, raw_name.c_str(), false);

    const char *tag_end = ctx.buf + ctx.pos;
    ctx.p->current_event = tag_start;
    ctx.p->current_event_len = (int)(tag_end - tag_start);

    if (ctx.p->end_handler) {
        ctx.p->end_handler(ctx.p->user_data, resolved_name.c_str());
    } else {
        miniexpat_dispatch_default(
            ctx.p, tag_start, (int)(tag_end - tag_start));
    }

    if (!ctx.p->ns_stack.empty())
        ctx.p->ns_stack.pop_back();
}

static void miniexpat_parse_char_data(MiniexpatCtx &ctx)
{
    const char *start = ctx.buf + ctx.pos;
    while (!ctx.at_end() && ctx.peek() != '<') ctx.advance(1);
    int raw_len = (int)(ctx.buf + ctx.pos - start);

    if (raw_len == 0) return;

    ctx.p->current_event = start;
    ctx.p->current_event_len = raw_len;

    if (ctx.p->char_handler) {
        std::string decoded = miniexpat_decode_entities(start, raw_len);
        ctx.p->char_handler(ctx.p->user_data, decoded.c_str(),
                            (int)decoded.size());
    } else {
        miniexpat_dispatch_default(ctx.p, start, raw_len);
    }
}

static void miniexpat_parse_content(MiniexpatCtx &ctx)
{
    while (!ctx.at_end() && !ctx.p->stopped) {
        if (ctx.peek() != '<') {
            miniexpat_parse_char_data(ctx);
            continue;
        }

        if (ctx.starts_with("<!--")) {
            miniexpat_parse_comment(ctx);
        } else if (ctx.starts_with("<?xml") &&
                   (ctx.pos + 5 >= ctx.len ||
                    !miniexpat_is_name_char(ctx.buf[ctx.pos + 5]))) {
            miniexpat_parse_xml_decl(ctx);
        } else if (ctx.starts_with("<?")) {
            miniexpat_parse_pi(ctx);
        } else if (ctx.starts_with("<!DOCTYPE")) {
            miniexpat_parse_doctype(ctx);
        } else if (ctx.starts_with("</")) {
            miniexpat_parse_end_tag(ctx);
        } else {
            miniexpat_parse_start_tag(ctx);
        }
    }
}

static XML_Parser miniexpat_create(char ns_sep)
{
    XML_Parser p = new XML_ParserStruct();
    p->ns_sep = ns_sep;
    p->user_data = nullptr;
    p->start_handler = nullptr;
    p->end_handler = nullptr;
    p->char_handler = nullptr;
    p->default_handler = nullptr;
    p->doctype_start_handler = nullptr;
    p->doctype_end_handler = nullptr;
    p->xml_decl_handler = nullptr;
    p->entity_ref_handler = nullptr;
    p->param_entity_parsing = XML_PARAM_ENTITY_PARSING_NEVER;
    p->use_foreign_dtd = false;
    p->error_code = XML_ERROR_NONE;
    p->stopped = false;
    p->line = 1;
    p->current_event = nullptr;
    p->current_event_len = 0;
    p->is_sub_parser = false;
    return p;
}

extern "C" {

XML_Parser XMLCALL XML_ParserCreate(const XML_Char *)
{
    return miniexpat_create(0);
}

XML_Parser XMLCALL XML_ParserCreateNS(const XML_Char *, XML_Char sep)
{
    return miniexpat_create(sep);
}

void XMLCALL XML_ParserFree(XML_Parser parser)
{
    delete parser;
}

enum XML_Status XMLCALL XML_Parse(XML_Parser parser, const char *s, int len,
                                  int)
{
    if (!parser) return XML_STATUS_ERROR;

    if (parser->is_sub_parser)
        return XML_STATUS_OK;

    if (parser->use_foreign_dtd && parser->entity_ref_handler &&
        parser->param_entity_parsing != XML_PARAM_ENTITY_PARSING_NEVER) {
        int ret = parser->entity_ref_handler(parser, nullptr, nullptr,
                                             nullptr, nullptr);
        if (ret == XML_STATUS_ERROR) {
            parser->error_code = XML_ERROR_EXTERNAL_ENTITY_HANDLING;
            return XML_STATUS_ERROR;
        }
        parser->use_foreign_dtd = false;
    }

    if (parser->stopped) return XML_STATUS_ERROR;

    MiniexpatCtx ctx;
    ctx.p = parser;
    ctx.buf = s;
    ctx.len = len;
    ctx.pos = 0;

    miniexpat_parse_content(ctx);

    return parser->error_code == XML_ERROR_NONE ? XML_STATUS_OK
                                                : XML_STATUS_ERROR;
}

XML_Parser XMLCALL XML_ExternalEntityParserCreate(XML_Parser parser,
                                                  const XML_Char *,
                                                  const XML_Char *)
{
    XML_Parser sub = XML_ParserCreateNS(nullptr, parser->ns_sep);
    sub->user_data = parser->user_data;
    sub->start_handler = parser->start_handler;
    sub->end_handler = parser->end_handler;
    sub->char_handler = parser->char_handler;
    sub->default_handler = parser->default_handler;
    sub->entity_ref_handler = parser->entity_ref_handler;
    sub->is_sub_parser = true;
    return sub;
}

void XMLCALL XML_SetUserData(XML_Parser parser, void *userData)
{
    parser->user_data = userData;
}

void *XMLCALL XML_GetUserData(XML_Parser parser)
{
    return parser->user_data;
}

void XMLCALL XML_SetElementHandler(XML_Parser parser,
                                   XML_StartElementHandler start,
                                   XML_EndElementHandler end)
{
    parser->start_handler = start;
    parser->end_handler = end;
}

void XMLCALL XML_SetCharacterDataHandler(XML_Parser parser,
                                         XML_CharacterDataHandler handler)
{
    parser->char_handler = handler;
}

void XMLCALL XML_SetDefaultHandler(XML_Parser parser,
                                   XML_DefaultHandler handler)
{
    parser->default_handler = handler;
}

void XMLCALL XML_SetDoctypeDeclHandler(XML_Parser parser,
                                       XML_StartDoctypeDeclHandler start,
                                       XML_EndDoctypeDeclHandler end)
{
    parser->doctype_start_handler = start;
    parser->doctype_end_handler = end;
}

void XMLCALL XML_SetXmlDeclHandler(XML_Parser parser,
                                   XML_XmlDeclHandler handler)
{
    parser->xml_decl_handler = handler;
}

void XMLCALL XML_SetExternalEntityRefHandler(
    XML_Parser parser, XML_ExternalEntityRefHandler handler)
{
    parser->entity_ref_handler = handler;
}

int XMLCALL XML_SetParamEntityParsing(XML_Parser parser,
                                      enum XML_ParamEntityParsing parsing)
{
    parser->param_entity_parsing = parsing;
    return 1;
}

enum XML_Error XMLCALL XML_UseForeignDTD(XML_Parser parser, XML_Bool useDTD)
{
    parser->use_foreign_dtd = useDTD != 0;
    return XML_ERROR_NONE;
}

long XMLCALL XML_GetCurrentLineNumber(XML_Parser parser)
{
    return parser->line;
}

enum XML_Error XMLCALL XML_GetErrorCode(XML_Parser parser)
{
    return parser->error_code;
}

const XML_Char *XMLCALL XML_ErrorString(enum XML_Error code)
{
    switch (code) {
    case XML_ERROR_NONE: return "no error";
    case XML_ERROR_NO_MEMORY: return "out of memory";
    case XML_ERROR_SYNTAX: return "syntax error";
    case XML_ERROR_NO_ELEMENTS: return "no element found";
    case XML_ERROR_INVALID_TOKEN: return "not well-formed (invalid token)";
    case XML_ERROR_UNCLOSED_TOKEN: return "unclosed token";
    case XML_ERROR_TAG_MISMATCH: return "mismatched tag";
    case XML_ERROR_DUPLICATE_ATTRIBUTE: return "duplicate attribute";
    case XML_ERROR_UNDEFINED_ENTITY: return "undefined entity";
    case XML_ERROR_EXTERNAL_ENTITY_HANDLING:
        return "error in processing external entity reference";
    case XML_ERROR_UNBOUND_PREFIX: return "unbound prefix";
    default: return "unknown error";
    }
}

enum XML_Status XMLCALL XML_StopParser(XML_Parser parser, XML_Bool)
{
    parser->stopped = true;
    return XML_STATUS_OK;
}

void XMLCALL XML_DefaultCurrent(XML_Parser parser)
{
    if (parser->default_handler && parser->current_event)
        parser->default_handler(parser->user_data, parser->current_event,
                                parser->current_event_len);
}

} // extern "C"

#endif // EXPAT_IMPL
#endif // EXPAT_H
