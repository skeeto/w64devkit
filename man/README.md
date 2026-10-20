# Manual pages

Hand-written manual pages for commands in w64devkit that have no upstream
page: the kit's own tools in `src/`, and the busybox applets. The tree
mirrors `share/man`, and the Dockerfile copies each `manN/` directory into
`$PREFIX/share/man/`. This file does not ship.

Pages are man(7) source, formatted in the kit by `nroff`, the kit's own
formatter (`nroff.1`), and displayed by busybox `man` through `less -R`.
The pages in `man1/` for `xxd`, `peports`, and the other kit tools are
the style exemplars. When in doubt, copy them.

## Writing a page

Write from the source code, not from usage strings or another system's
manual. Read the code for every option, behaviour, environment variable
and exit status, and document what the code does, including surprises.
Leave out what the code does not do. A busybox applet differs from its
GNU or BSD namesake, and its usage text is often incomplete.

Plain, precise, terse. Present tense, active voice, no marketing, no
"simply" or "just". Describe behaviour; do not explain C or Windows
basics. Prefer a short accurate sentence to a long complete one.

## Header and sections

    .TH XXD 1 2026-10-19 "w64devkit" "w64devkit Manual"

The title is the page name in capitals, the date is the last substantive
change in ISO 8601, and the last two fields are exactly as shown.

Top-level sections, in this order. NAME, SYNOPSIS and DESCRIPTION are
required. Omit any other section with nothing to say, and add no others:
put extra material in `.SS` subsections of DESCRIPTION.

1. **NAME**: `name \- summary` on one line. The summary is lowercase,
   with no final period, and fits on one 80-column line. A page that
   covers several commands lists them all: `grep, egrep, fgrep \- ...`
2. **SYNOPSIS**: one line per distinct form (separated by `.br`).
3. **DESCRIPTION**: what it does, its inputs and outputs, and any output
   format, in that order. Long pages use `.SS` subsections.
4. **OPTIONS**: one `.TP` entry per option. An optional paragraph before
   the first entry gives option syntax rules (grouping, `--`, attached
   arguments).
5. **ENVIRONMENT**: every environment variable the command reads,
   including those read by shared library code, and any it sets for
   child processes.
6. **EXIT STATUS**: a `.TP` entry per status value.
7. **EXAMPLES**: a sentence saying what each example does, then the
   example.
8. **NOTES**: Windows and w64devkit specifics: differences from the
   usual Unix command, path and encoding handling, console behaviour,
   memory limits, and caveats.
9. **SEE ALSO**: `.BR name (N),` entries, sorted by section and then by
   name, with no final comma. List only pages that exist in the kit.

## Macros

Use only these man(7) macros: `TH SH SS PP TP IP RS RE B I BR BI IB IR
RB RI EX EE`, plus `.br` in SYNOPSIS. No `UR`, `MT`, `SY`, `OP`, `MR`,
`TQ`, `HP`, `LP`, `P`, mdoc macros, `tbl` tables, or low-level requests
such as `.sp`, `.in`, `.ti`, `.nf` and `.ad`. Use `.PP` between
paragraphs, never a blank line.

Fonts:

* **Bold**: text typed literally: command names, options, environment
  variable names, keywords, and literal values.
* *Italic*: placeholders the user replaces, and file paths.
* Roman: everything else, including `[ ] | ...` in synopses.

Prefer the font macros (`.B`, `.BR`, `.IR`, ...) to inline `\fB`, `\fI`
and `\fR`. Use inline escapes only where a macro cannot express the
mix, such as an option tag with three fonts, and always end with `\fR`.
Refer to another page as `.BR ls (1)`. ENVIRONMENT entries are `.TP`
with a `.B NAME` tag.

Source lines:

* One sentence per line, so the formatter spaces sentence ends.
* Escape every minus or dash that is typed, such as an option or a
  negative number, as `\-`. A bare `-` is a hyphen and may print as a
  Unicode hyphen. Write a backslash as `\e`, an apostrophe in example
  code as `\(aq`, and a bullet as `\(bu`.
* A line that would begin with `.` or `'` starts with `\&`.
* A line that ends in `.`, `?` or `!` without ending a sentence, such as
  `.B ?`, ends with `\&`, or the formatter adds sentence spacing.
* Use the serial comma: "`-e`, `-i`, or `-m`".
* ASCII only. Escape anything else (`\(em`, `\[u00E9]`), and prefer
  avoiding it.
* Keep source lines within 80 columns, except in `.EX` blocks.

## Synopsis

    .SH SYNOPSIS
    .B peports
    .RB [ \-ehim ]
    .RI [ file ...]
    .br
    .B xxd \-r
    .RB [ \-p ]
    .RB [ \-c
    .IR cols ]
    .RI [ infile
    .RI [ outfile ]]

Flags without arguments go in one bracket, as in `.RB [ \-ehim ]`, when
the command accepts them grouped, and in separate brackets otherwise.
Each option with an argument gets its own bracket, split across `.RB`
and `.IR` lines as shown. When the argument must be attached, end the
`.RB` line with `\c` so no space follows: `.RB [ \-n\c` then
`.IR count ]`. When a command has many options, write
`.RI [ options ]` and leave the details to OPTIONS.

## Options

    .TP
    .B \-a
    Toggle autoskip.
    .TP
    .BI "\-c " cols
    Write
    .I cols
    octets per line.
    .TP
    .BI \-n count
    ...
    .TP
    .BR \-h ", " \-\-help
    Print a usage message on standard output and exit.

* An option that takes an argument separately: `.BI "\-c " cols`. The
  space is inside the bold argument, so it is not underlined.
* An option whose argument must be attached: `.BI \-n count`.
* An option with a long and short form, or synonyms: one tag,
  `.BR \-h ", " \-\-help`.
* A tag needing more than two fonts uses inline escapes:
  `\fB\-s\fR [\fB+\fR][\fB\-\fR]\fIoffset\fR`.
* Order entries as the command's usage text does. For the kit tools that
  is alphabetical, with lowercase after uppercase of the same letter.
* State defaults, units and limits in the entry. Note interactions with
  other options there too ("Incompatible with `-r`").
* A further paragraph inside an entry is `.IP` with no arguments.

## Examples

    .SH EXAMPLES
    Dump a file:
    .PP
    .RS 4
    .EX
    $ xxd hello.txt
    00000000: 4865 6c6c 6f2c 2077 6f72 6c64 210a       Hello, world!.
    .EE
    .RE

Prefix commands with `$ `. Show output only when it explains something,
and only output actually produced by the kit's command. Example lines
must fit within 66 columns so they stay inside an 80-column terminal at
the `.RS 4` indent. Examples run in the kit's shell, so use Unix shell
syntax and forward slashes.

## Lists

Bulleted lists use `.IP \(bu 2`, and numbered lists `.IP 1. 4`, one
entry per item. Term lists, such as exit statuses or configuration
keys, use `.TP`. A list nested in a `.TP` entry is wrapped in `.RS` and
`.RE`, and the entry continues afterwards with `.IP`.

## Alias stubs

A command documented on another page gets a stub instead of a copy or a
link. A stub is a single line naming the target relative to the man
root:

    .so man1/grep.1

The file is at most 300 bytes, has LF line endings, and contains nothing
else, not even a comment. busybox `man` only follows `.so` in files of
300 bytes or less that begin with `.so `, and resolves a target
containing a `/` against the man root. Name the stub after the alias, as
in `man1/egrep.1`, and list the alias in the target page's NAME line.

Commands with a target triplet prefix, such as `x86_64-w64-mingw32-gcc`,
need no stubs: `man` retries with the part after `-w64-mingw32-`.

## Checking

Every page must lint clean and read well at 80 columns. In the kit,
`nroff -w page.1 >/dev/null` must be silent, and the page should be
checked in a real console with `man ./page.1`. Also check with groff and
mandoc in Docker:

    docker run --rm -v "$PWD/man:/man" -w /man/man1 debian:trixie-slim sh -c '
        apt-get update -qq && apt-get install -qqy groff-base mandoc >/dev/null
        for f in *.1; do
            mandoc -T lint -W all "$f"
            groff -man -ww -z -rCHECKSTYLE=3 "$f"
        done
        groff -man -Tutf8 -rLL=78n -rLT=78n -P-cbou xxd.1'

`man` passes `-rLL=78n` on an 80-column terminal.

## Pages still missing

* `ninja`: no upstream page.
* `makensis`: no upstream page.
* `gendef`: no upstream page.
* `widl`: adapt Wine's `tools/widl/widl.man.in` at wine-11.5 (LGPL).
* `cmcldeps`: no upstream page.
