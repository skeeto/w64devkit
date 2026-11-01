# Security Guidelines

Most programs in w64devkit are not designed to safely process untrusted
inputs. In general do not use the kit on untrusted projects, even just to
examine source code. Therefore bugs in these programs are not security
bugs. That includes these programs:

* Binutils (see `binutils/SECURITY.txt`)
  * GDB (see `gdb/SECURITY.txt`)
* GNU Compiler Collection
  * dcmake (always invokes `cmake`)
* All busybox-w32 apps
  * `w64devkit.exe` (always invokes the shell)
  * Be especially careful with `wget` and `httpd`
* Mingw-w64 tools: `gendef`, `widl`
* Ccache
* Ninja
* CMake
* GNU Make
* Vim
* NSIS

A few programs in the kit are thoroughly tested and expected to safely
operate on untrusted inputs. Therefore bugs in these programs may have
security implications. That includes:

* aas-sign
* debugbreak
* make2compdb
* nroff
* peports
* quicktags (`ctags`)
* Quilt.cpp (`quilt`)
* recycle
* rexxd (`xxd`)
* tugz (`gzip`, `gunzip`, `zcat`, `zip`, `unzip`, `zipinfo`)
* u-config (`pkg-config`)
* uuidgen (no inputs)
* vc++filt
* zstd

If you believe you found a security bug in these programs, consider
disclosing it privately.
