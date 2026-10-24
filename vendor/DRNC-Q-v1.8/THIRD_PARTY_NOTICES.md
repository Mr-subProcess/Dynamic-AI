# Third-party notices

The DRNC-Q compiler/runtime is implemented in C++17 and links against the C++ standard library and platform runtime. The project does not bundle Python, Qiskit, cloud-provider SDKs, or third-party quantum-provider clients.

Build toolchains (GCC or Clang) are used only if rebuilding from source. The packaged Linux binary statically links `libstdc++` and `libgcc`, but dynamically uses the host Linux C runtime (`glibc`) and math library. Any redistribution or use remains subject to the commercial terms in `LICENSE` and any applicable third-party runtime/toolchain notices.
