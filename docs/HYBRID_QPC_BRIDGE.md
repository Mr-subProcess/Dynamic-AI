# PC matrix checkpoint to QPC circuit

## Data mapping

`scripts/generate_qpc_source.sh` reads the latest PC checkpoint files and inserts twelve diagonal elements into `templates/Dynamic_AI_QPC.drq.in`:

| PC checkpoint value | QPC use |
|---|---|
| `W_sensory[0][0]`, `[1][1]`, `[2][2]` | Three sensory rotations (`RX`, `RY`, `RZ`) |
| `W_memory[0][0]`, `[1][1]`, `[2][2]` | Parameter angles for sensory-to-memory `matrixEdge` connections |
| `W_branch[0][0]`, `[1][1]`, `[2][2]` | Parameter angles for memory-to-output / inverse-power edge calls |
| `W_output[0][0]`, `[1][1]`, `[2][2]` | Zerya broadcast and controlled-edge angles |

The build produces `build/Dynamic_AI_QPC.generated.drq`, then `build/Dynamic_AI_QPC.qasm`, an optimized version, and a resource report. The top-level `Dynamic_AI_QPC.drq` is a checked-in example snapshot; newly generated output lives under `build/` and `results/`.

## What the bridge means

This mapping transfers selected classical learned scalars to unitary gate parameters. It is a deliberately simple hybrid encoding used to connect the two targets. It does not turn the QPC circuit into an exact implementation of the classical matrix network. Classical matrix multiplication, recurrent activations, clipped weight updates, and training loops are not executed inside the QPU circuit by this bridge.

## Execution boundary

OpenQASM 3 output is a portable circuit representation. A real physical device needs a compatible local driver which performs target validation, qubit placement and routing, native-gate decomposition, calibration, submission to control electronics, shot collection, and result reporting. Some devices do not support mid-circuit measurement or classical feed-forward with the same semantics. The bundled mock driver only tests the process ABI.

No provider SDK/API, physical QPU, pulse-control hardware, or device calibration is bundled.
