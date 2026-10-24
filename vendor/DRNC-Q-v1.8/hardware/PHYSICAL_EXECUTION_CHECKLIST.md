# Physical execution checklist (hardware-specific)

Before calling any `%set[QPC]` program a physical run, the deployment must have all of these:

1. **A real QPU and local controller**: identify the actual technology (for example, superconducting, trapped-ion, neutral-atom, photonic) and the attached control system.
2. **A device-specific DRNC-Q local driver**: implement `DRNC-Q local physical-QPU driver ABI v1` and do not simply return success after reading the circuit.
3. **A target profile**: available physical qubits, coupling/topology, native gate set, measurement/reset ability, connectivity and supported mid-circuit feed-forward.
4. **Gate decomposition and routing**: every gate in the lowered IR must map to native device instructions or be rejected.
5. **Calibration and timing**: actual pulse/control parameters and readout calibration come from the physical device, not the language compiler.
6. **Readout mapping and result accounting**: document endianness, shots, failures, resets and any dropped/invalid shots.
7. **Physical validation**: compare measurement distributions with known circuits such as |0>, X|0>, H|0>, Bell-state correlations, and a small teleportation check with an independently prepared reference state.

The shipped QPC backend and mock driver only test compilation and local process hand-off. They do not meet these physical qualification steps on their own.
