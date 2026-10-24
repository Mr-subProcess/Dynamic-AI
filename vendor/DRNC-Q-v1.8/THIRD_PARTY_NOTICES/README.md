# Bundled runtime notices

The distributed Linux executable bundles GCC's C++ standard library and GCC runtime support (`-static-libstdc++ -static-libgcc`) under the applicable GNU runtime licenses and exceptions. The Debian GCC package copyright notice is included in `GCC-14-BASE-COPYRIGHT.txt`.

The executable is dynamically linked to the host Linux C runtime (glibc); glibc is not copied into this distribution. Rebuilding the executable requires a C++17 toolchain. DRNC-Q's own source and software license are separately described in `LICENSE`; that license does not override third-party runtime licenses.
