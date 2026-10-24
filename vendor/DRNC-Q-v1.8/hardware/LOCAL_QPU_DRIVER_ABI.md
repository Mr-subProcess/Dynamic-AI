# DRNC-Q local physical-QPU driver ABI v1

## Status

This is a local process interface, not a cloud/provider API. DRNC-Q v1.8 contains the compiler and the ABI caller, but it does **not** contain a physical QPU or a hardware-calibrated device driver. A usable physical run requires an executable driver for the concrete device/controller that is actually connected to the host.

## Invocation

The compiler invokes the driver as a child process:

```text
/path/to/driver --drncq-qpc-driver-v1 --qasm /tmp/drncq-qpc-XXXXXX --shots N
```

Equivalent CLI:

```sh
./drnq qpc-run circuit.drq --driver /path/to/driver --shots 1024
```

or set `DRNC_QPC_DRIVER=/path/to/driver` and use `./drnq run circuit.drq` for a `%set[QPC]` entry point (default shot count 1024).

Requirements:

1. The driver must be an executable local file. `qpc-run` checks execute permission before launching it.
2. It must accept exactly the documented `--drncq-qpc-driver-v1`, `--qasm PATH`, and `--shots N` arguments. Unknown arguments should be rejected.
3. The temporary QASM file is readable only for the duration of the child process. The driver must read it before exiting and must not defer reading its path asynchronously.
4. The driver must validate the OpenQASM subset and reject unsupported instructions before applying a circuit to hardware.
5. It must map logical qubits to the physical device topology, route/decompose non-native gates, use calibrated control/readout, execute the requested number of shots, and label output bit order exactly as documented by its device backend.
6. Exit code `0` means the driver reports completion. Any nonzero code is reported by DRNC-Q as a failed job. Diagnostics belong on stderr; results belong on stdout. Suggested stdout result shape is one JSON object, e.g. `{"status":"completed","shots":1024,"counts":{"00":510,"11":514}}`, but v1 of the process ABI passes stdout through rather than parsing this schema.
7. Device credentials, if any, are the responsibility of the local driver. The DRNC-Q compiler itself does not use a quantum-provider API or silently establish a network connection.

## The required device-specific layer

A device driver is not merely a file uploader. It is responsible for:

- checking qubit count and connectivity;
- compiling logical gates to native gate/pulse instructions;
- calibrating/readjusting gate timing and readout if supported by the platform;
- validating the availability of mid-circuit measurement and conditional feed-forward;
- handling device reset, shot scheduling, readout decoding, timeout, abort and hardware faults;
- preserving result bit order and recording target identity/version where practical.

A general OpenQASM circuit does not supply these calibrations. Hardware implementations can support different subsets of the circuit language. Therefore a universal built-in driver cannot truthfully be claimed without selecting and testing an actual QPU architecture and its control stack.

## Testing boundary

`tests/mock_qpc_driver.sh` validates that DRNC-Q passes the correct arguments and a valid generated OpenQASM file to a local process. It prints `MOCK DRIVER ABI PASS (not a physical QPU)` and must never be used to claim hardware execution.


## v1.7 parameterized-gate note

The QPC backend can now emit user-defined gate parameters and nested gate calls. The driver must parse the OpenQASM 3 gate declarations/modifiers it accepts and either lower them to the chosen hardware's native gate set or reject the circuit before execution. For `ctrl`, `inv`, and parameterized calls, support varies by target; this ABI does not mean every QPU supports the entire OpenQASM language.


## v1.8 circuit tooling

The compiler can emit a static report (`qpc-report`), emit a conservatively optimized QASM file (`qpc-optimize`), and submit either source-generated QASM with `qpc-run --optimize` or an existing `OPENQASM 3.0;` artifact with `qpc-run-qasm`. These features do not change the ABI command line. The driver must still validate the complete QASM grammar and device subset before touching hardware.
