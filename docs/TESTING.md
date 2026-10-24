# Dynamic AI validation procedure

Run the full project check from the project root:

```bash
./scripts/run_all.sh
```

The workflow:

1. Executes the bundled DRNC-Q v1.8 compiler regression suite.
2. Checks and executes supervised PC training, then verifies that the included dataset produces a lower final training MSE than its initial MSE.
3. Confirms that each checkpoint matrix has nine scalar entries.
4. Runs checkpoint-backed inference and checks that the three fixture inputs return class indices 0, 1 and 2.
5. Applies labelled online feedback and confirms the output checkpoint changes.
6. Generates a QPC source from learned weights and verifies that every template token is substituted.
7. Compiles OpenQASM 3, produces a resource report and applies the compiler's conservative optimizer.
8. Invokes the local mock driver to test the QPU driver process ABI. The mock does not contact physical hardware.

The tests are release smoke/regression checks, not an independent proof of the DRNC theory, model quality, quantum advantage, cryptographic security, or physical QPU correctness.
