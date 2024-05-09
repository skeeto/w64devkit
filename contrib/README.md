# Unsupported w64devkit enhancements

The files in this directory enhance w64devkit with specially-built
third-party libraries, such as garbage collection and POSIX `regex.h`.
Falling outside the scope of w64devkit, these libraries are not included
in the distribution, but may be situationally useful.

The header of each file has the instructions for its use. The file itself
is mostly a custom build script, and you will need to obtain the library
sources yourself the usual way.
