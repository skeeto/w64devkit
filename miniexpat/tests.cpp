// miniexpat test suite
// This is free and unencumbered software released into the public domain.
#include "expat.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// Test helper: record events from callbacks
struct Event {
    enum Kind { START, END, CHARDATA, DEFAULT, XMLDECL, DOCTYPE_START,
                DOCTYPE_END, ENTITY_REF } kind;
    std::string data;
    std::vector<std::pair<std::string, std::string>> attrs;
};

static std::vector<Event> events;

static void clear() { events.clear(); }

static void XMLCALL on_start(void *, const XML_Char *name,
                             const XML_Char **atts)
{
    Event e;
    e.kind = Event::START;
    e.data = name;
    for (int i = 0; atts[i]; i += 2)
        e.attrs.push_back({atts[i], atts[i+1]});
    events.push_back(e);
}

static void XMLCALL on_end(void *, const XML_Char *name)
{
    Event e;
    e.kind = Event::END;
    e.data = name;
    events.push_back(e);
}

static void XMLCALL on_chardata(void *, const XML_Char *s, int len)
{
    Event e;
    e.kind = Event::CHARDATA;
    e.data.assign(s, len);
    events.push_back(e);
}

static void XMLCALL on_default(void *, const XML_Char *s, int len)
{
    Event e;
    e.kind = Event::DEFAULT;
    e.data.assign(s, len);
    events.push_back(e);
}

static void XMLCALL on_xmldecl(void *, const XML_Char *version,
                                const XML_Char *encoding, int standalone)
{
    Event e;
    e.kind = Event::XMLDECL;
    e.data = version ? version : "";
    if (encoding) { e.data += " "; e.data += encoding; }
    char buf[32];
    snprintf(buf, sizeof buf, " %d", standalone);
    e.data += buf;
    events.push_back(e);
}

static void XMLCALL on_doctype_start(void *, const XML_Char *name,
                                     const XML_Char *sysid,
                                     const XML_Char *, int)
{
    Event e;
    e.kind = Event::DOCTYPE_START;
    e.data = name;
    if (sysid) { e.data += " "; e.data += sysid; }
    events.push_back(e);
}

static void XMLCALL on_doctype_end(void *)
{
    Event e;
    e.kind = Event::DOCTYPE_END;
    events.push_back(e);
}

static int XMLCALL on_entity_ref(XML_Parser, const XML_Char *,
                                 const XML_Char *, const XML_Char *sysid,
                                 const XML_Char *)
{
    Event e;
    e.kind = Event::ENTITY_REF;
    e.data = sysid ? sysid : "(null)";
    events.push_back(e);
    return XML_STATUS_OK;
}

// Convenience: parse a string and return status
static XML_Status parse(XML_Parser p, const char *xml)
{
    return XML_Parse(p, xml, (int)strlen(xml), 1);
}

// Make a parser with standard handlers
static XML_Parser make_parser()
{
    XML_Parser p = XML_ParserCreateNS(nullptr, '!');
    XML_SetElementHandler(p, on_start, on_end);
    XML_SetCharacterDataHandler(p, on_chardata);
    return p;
}

// 1. Basic self-closing element
static void test_basic_element()
{
    clear();
    XML_Parser p = make_parser();
    assert(parse(p, "<a/>") == XML_STATUS_OK);
    assert(events.size() == 2);
    assert(events[0].kind == Event::START && events[0].data == "a");
    assert(events[1].kind == Event::END && events[1].data == "a");
    XML_ParserFree(p);
    printf("  pass: basic element\n");
}

// 2. Nested elements
static void test_nested()
{
    clear();
    XML_Parser p = make_parser();
    assert(parse(p, "<a><b/></a>") == XML_STATUS_OK);
    assert(events.size() == 4);
    assert(events[0].kind == Event::START && events[0].data == "a");
    assert(events[1].kind == Event::START && events[1].data == "b");
    assert(events[2].kind == Event::END && events[2].data == "b");
    assert(events[3].kind == Event::END && events[3].data == "a");
    XML_ParserFree(p);
    printf("  pass: nested elements\n");
}

// 3. Attributes
static void test_attributes()
{
    clear();
    XML_Parser p = make_parser();
    assert(parse(p, "<a x=\"1\" y=\"2\"/>") == XML_STATUS_OK);
    assert(events.size() == 2);
    assert(events[0].attrs.size() == 2);
    assert(events[0].attrs[0].first == "x" && events[0].attrs[0].second == "1");
    assert(events[0].attrs[1].first == "y" && events[0].attrs[1].second == "2");
    XML_ParserFree(p);
    printf("  pass: attributes\n");
}

// 4. Character data
static void test_chardata()
{
    clear();
    XML_Parser p = make_parser();
    assert(parse(p, "<a>hello</a>") == XML_STATUS_OK);
    assert(events.size() == 3);
    assert(events[1].kind == Event::CHARDATA && events[1].data == "hello");
    XML_ParserFree(p);
    printf("  pass: character data\n");
}

// 5. Entity expansion in text
static void test_entities()
{
    clear();
    XML_Parser p = make_parser();
    assert(parse(p, "<a>&amp;&lt;&gt;&quot;&apos;</a>") == XML_STATUS_OK);
    assert(events[1].kind == Event::CHARDATA);
    assert(events[1].data == "&<>\"'");
    XML_ParserFree(p);
    printf("  pass: entity expansion\n");
}

// 6. Namespace with prefix
static void test_ns_prefix()
{
    clear();
    XML_Parser p = make_parser();
    assert(parse(p, "<a xmlns:ns=\"http://example.com\"><ns:b/></a>")
           == XML_STATUS_OK);
    assert(events[1].kind == Event::START);
    assert(events[1].data == "http://example.com!b");
    XML_ParserFree(p);
    printf("  pass: namespace prefix\n");
}

// 7. Default namespace
static void test_ns_default()
{
    clear();
    XML_Parser p = make_parser();
    assert(parse(p, "<a xmlns=\"http://example.com\"><b/></a>")
           == XML_STATUS_OK);
    // Both a and b should be in the default namespace
    assert(events[0].data == "http://example.com!a");
    assert(events[1].data == "http://example.com!b");
    XML_ParserFree(p);
    printf("  pass: default namespace\n");
}

// 8. Self-closing with attributes (already covered, but let's be explicit)
static void test_self_closing()
{
    clear();
    XML_Parser p = make_parser();
    assert(parse(p, "<reg name=\"rax\" bitsize=\"64\"/>") == XML_STATUS_OK);
    assert(events.size() == 2);
    assert(events[0].kind == Event::START);
    assert(events[0].attrs.size() == 2);
    assert(events[0].attrs[0].first == "name");
    assert(events[0].attrs[0].second == "rax");
    assert(events[1].kind == Event::END);
    XML_ParserFree(p);
    printf("  pass: self-closing with attrs\n");
}

// 9. XML declaration
static void test_xml_decl()
{
    clear();
    XML_Parser p = make_parser();
    XML_SetXmlDeclHandler(p, on_xmldecl);
    assert(parse(p, "<?xml version=\"1.0\" encoding=\"utf-8\"?><a/>")
           == XML_STATUS_OK);
    assert(events[0].kind == Event::XMLDECL);
    assert(events[0].data == "1.0 utf-8 -1");
    XML_ParserFree(p);
    printf("  pass: XML declaration\n");
}

// 10. DOCTYPE
static void test_doctype()
{
    clear();
    XML_Parser p = make_parser();
    XML_SetDoctypeDeclHandler(p, on_doctype_start, on_doctype_end);
    assert(parse(p, "<!DOCTYPE target SYSTEM \"gdb-target.dtd\"><a/>")
           == XML_STATUS_OK);
    assert(events[0].kind == Event::DOCTYPE_START);
    assert(events[0].data == "target gdb-target.dtd");
    assert(events[1].kind == Event::DOCTYPE_END);
    XML_ParserFree(p);
    printf("  pass: DOCTYPE\n");
}

// 11. XML_DefaultCurrent
static XML_Parser default_current_parser;
static void XMLCALL on_start_with_default(void *ud, const XML_Char *name,
                                          const XML_Char **atts)
{
    on_start(ud, name, atts);
    XML_DefaultCurrent(default_current_parser);
}

static void test_default_current()
{
    clear();
    XML_Parser p = XML_ParserCreateNS(nullptr, '!');
    default_current_parser = p;
    XML_SetElementHandler(p, on_start_with_default, on_end);
    XML_SetDefaultHandler(p, on_default);
    assert(parse(p, "<a x=\"1\"/>") == XML_STATUS_OK);
    // Should have: START, DEFAULT (from DefaultCurrent), END
    bool found_default = false;
    for (auto &e : events) {
        if (e.kind == Event::DEFAULT && e.data.find("<a") != std::string::npos) {
            found_default = true;
        }
    }
    assert(found_default);
    XML_ParserFree(p);
    printf("  pass: XML_DefaultCurrent\n");
}

// 12. XML_StopParser
static void XMLCALL on_start_stop(void *ud, const XML_Char *name,
                                  const XML_Char **atts)
{
    on_start(ud, name, atts);
    XML_StopParser((XML_Parser)ud, XML_FALSE);
}

static void test_stop_parser()
{
    clear();
    XML_Parser p = XML_ParserCreateNS(nullptr, '!');
    XML_SetUserData(p, p);
    XML_SetElementHandler(p, on_start_stop, on_end);
    // Should stop after first element start
    parse(p, "<a><b/><c/></a>");
    // Only the first start event should have fired
    assert(events.size() == 1);
    assert(events[0].kind == Event::START && events[0].data == "a");
    XML_ParserFree(p);
    printf("  pass: XML_StopParser\n");
}

// 13. Foreign DTD trigger
static void test_foreign_dtd()
{
    clear();
    XML_Parser p = make_parser();
    XML_SetExternalEntityRefHandler(p, on_entity_ref);
    XML_SetParamEntityParsing(p, XML_PARAM_ENTITY_PARSING_UNLESS_STANDALONE);
    XML_UseForeignDTD(p, XML_TRUE);
    assert(parse(p, "<a/>") == XML_STATUS_OK);
    // Entity ref handler should have been called with NULL systemId
    bool found = false;
    for (auto &e : events) {
        if (e.kind == Event::ENTITY_REF && e.data == "(null)") {
            found = true;
        }
    }
    assert(found);
    XML_ParserFree(p);
    printf("  pass: foreign DTD\n");
}

// 14. Sub-parser (ExternalEntityParserCreate)
static void test_sub_parser()
{
    XML_Parser p = XML_ParserCreateNS(nullptr, '!');
    XML_Parser sub = XML_ExternalEntityParserCreate(p, nullptr, nullptr);
    // Sub-parser should accept anything
    assert(XML_Parse(sub, "<!ELEMENT foo (bar)>", 20, 1) == XML_STATUS_OK);
    XML_ParserFree(sub);
    XML_ParserFree(p);
    printf("  pass: sub-parser\n");
}

// 15. GDB-realistic target description
static void test_gdb_target()
{
    clear();
    XML_Parser p = make_parser();
    XML_SetXmlDeclHandler(p, on_xmldecl);
    XML_SetDoctypeDeclHandler(p, on_doctype_start, on_doctype_end);

    const char *xml =
        "<?xml version=\"1.0\"?>\n"
        "<!DOCTYPE feature SYSTEM \"gdb-target.dtd\">\n"
        "<feature name=\"org.gnu.gdb.i386.core\">\n"
        "  <flags id=\"i386_eflags\" size=\"4\">\n"
        "    <field name=\"CF\" start=\"0\" end=\"0\"/>\n"
        "    <field name=\"PF\" start=\"2\" end=\"2\"/>\n"
        "  </flags>\n"
        "  <reg name=\"rax\" bitsize=\"64\" type=\"int64\"/>\n"
        "  <reg name=\"rbx\" bitsize=\"64\" type=\"int64\"/>\n"
        "</feature>";

    assert(parse(p, xml) == XML_STATUS_OK);

    // Find key events
    int starts = 0, ends = 0;
    bool found_feature = false, found_reg = false, found_field = false;
    for (auto &e : events) {
        if (e.kind == Event::START) {
            starts++;
            if (e.data == "feature") {
                found_feature = true;
                assert(e.attrs.size() == 1);
                assert(e.attrs[0].second == "org.gnu.gdb.i386.core");
            }
            if (e.data == "reg") {
                found_reg = true;
                // Check it has name, bitsize, type attrs
                assert(e.attrs.size() == 3);
            }
            if (e.data == "field") {
                found_field = true;
                assert(e.attrs.size() == 3);
            }
        }
        if (e.kind == Event::END) ends++;
    }
    assert(found_feature && found_reg && found_field);
    // feature + flags + 2 fields + 2 regs = 6 start elements
    assert(starts == 6);
    assert(ends == 6);

    XML_ParserFree(p);
    printf("  pass: GDB target description\n");
}

// 16. Line numbers
static void test_line_numbers()
{
    XML_Parser p = XML_ParserCreateNS(nullptr, '!');

    // Record line number during callback
    static long captured_line;
    XML_SetElementHandler(p,
        [](void *ud, const XML_Char *, const XML_Char **) {
            captured_line = XML_GetCurrentLineNumber((XML_Parser)ud);
        }, nullptr);
    XML_SetUserData(p, p);

    const char *xml = "\n\n\n<a/>";
    captured_line = 0;
    assert(parse(p, xml) == XML_STATUS_OK);
    assert(captured_line == 4);

    XML_ParserFree(p);
    printf("  pass: line numbers\n");
}

// 17. Comments are skipped
static void test_comments()
{
    clear();
    XML_Parser p = make_parser();
    assert(parse(p, "<!-- comment --><a/>") == XML_STATUS_OK);
    // Should only have start and end for <a/>
    int start_count = 0;
    for (auto &e : events) {
        if (e.kind == Event::START) {
            assert(e.data == "a");
            start_count++;
        }
    }
    assert(start_count == 1);
    XML_ParserFree(p);
    printf("  pass: comments\n");
}

// 18. Entities in attribute values
static void test_attr_entities()
{
    clear();
    XML_Parser p = make_parser();
    assert(parse(p, "<a name=\"x&amp;y\"/>") == XML_STATUS_OK);
    assert(events[0].attrs[0].second == "x&y");
    XML_ParserFree(p);
    printf("  pass: attribute entities\n");
}

// 19. XInclude namespace (as GDB uses it)
static void test_xinclude_ns()
{
    clear();
    XML_Parser p = make_parser();
    const char *xml =
        "<target>"
        "<xi:include xmlns:xi=\"http://www.w3.org/2001/XInclude\" "
        "href=\"or1k-core.xml\"/>"
        "</target>";
    assert(parse(p, xml) == XML_STATUS_OK);
    // The xi:include should resolve to the full namespace
    bool found = false;
    for (auto &e : events) {
        if (e.kind == Event::START &&
            e.data == "http://www.w3.org/2001/XInclude!include") {
            found = true;
            assert(e.attrs.size() == 1);
            assert(e.attrs[0].first == "href");
            assert(e.attrs[0].second == "or1k-core.xml");
        }
    }
    assert(found);
    XML_ParserFree(p);
    printf("  pass: XInclude namespace\n");
}

// 20. Numeric character references
static void test_numeric_charref()
{
    clear();
    XML_Parser p = make_parser();
    assert(parse(p, "<a>&#65;&#x42;</a>") == XML_STATUS_OK);
    assert(events[1].kind == Event::CHARDATA);
    assert(events[1].data == "AB");
    XML_ParserFree(p);
    printf("  pass: numeric character references\n");
}

// 21. Error reporting
static void test_error_string()
{
    assert(strcmp(XML_ErrorString(XML_ERROR_NONE), "no error") == 0);
    assert(strcmp(XML_ErrorString(XML_ERROR_SYNTAX), "syntax error") == 0);
    printf("  pass: error strings\n");
}

// 22. Single-quoted attributes
static void test_single_quotes()
{
    clear();
    XML_Parser p = make_parser();
    assert(parse(p, "<a x='hello'/>") == XML_STATUS_OK);
    assert(events[0].attrs[0].second == "hello");
    XML_ParserFree(p);
    printf("  pass: single-quoted attributes\n");
}

// 23. DOCTYPE with entity ref handler (like GDB's DTD loading)
static void test_doctype_entity_ref()
{
    clear();
    XML_Parser p = make_parser();
    XML_SetDoctypeDeclHandler(p, on_doctype_start, on_doctype_end);
    XML_SetExternalEntityRefHandler(p, on_entity_ref);
    XML_SetParamEntityParsing(p, XML_PARAM_ENTITY_PARSING_UNLESS_STANDALONE);

    assert(parse(p, "<!DOCTYPE target SYSTEM \"gdb-target.dtd\"><a/>")
           == XML_STATUS_OK);

    // Should have: DOCTYPE_START, ENTITY_REF (for sysid), DOCTYPE_END, START, END
    bool found_entity = false;
    for (auto &e : events) {
        if (e.kind == Event::ENTITY_REF && e.data == "gdb-target.dtd")
            found_entity = true;
    }
    assert(found_entity);
    XML_ParserFree(p);
    printf("  pass: DOCTYPE entity ref\n");
}

// 24. Multiple character data segments (whitespace between elements)
static void test_whitespace_chardata()
{
    clear();
    XML_Parser p = make_parser();
    assert(parse(p, "<a>\n  <b/>\n</a>") == XML_STATUS_OK);
    // Should get chardata events for the whitespace
    int chardata_count = 0;
    for (auto &e : events)
        if (e.kind == Event::CHARDATA) chardata_count++;
    assert(chardata_count >= 1);
    XML_ParserFree(p);
    printf("  pass: whitespace chardata\n");
}

int main()
{
    printf("miniexpat tests:\n");
    test_basic_element();
    test_nested();
    test_attributes();
    test_chardata();
    test_entities();
    test_ns_prefix();
    test_ns_default();
    test_self_closing();
    test_xml_decl();
    test_doctype();
    test_default_current();
    test_stop_parser();
    test_foreign_dtd();
    test_sub_parser();
    test_gdb_target();
    test_line_numbers();
    test_comments();
    test_attr_entities();
    test_xinclude_ns();
    test_numeric_charref();
    test_error_string();
    test_single_quotes();
    test_doctype_entity_ref();
    test_whitespace_chardata();
    printf("All %d tests passed.\n", 24);
    return 0;
}
