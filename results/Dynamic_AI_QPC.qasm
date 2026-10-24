OPENQASM 3.0;
include "stdgates.inc";
gate matrixEdge(theta) source destination {
    ry(theta) source;
    cx source, destination;
    rz((theta / 2)) destination;
}
gate zeryaBroadcast(theta) source branch destination {
    cp(theta) source, branch;
    cx branch, destination;
    p((theta / 2)) destination;
}

qubit[14] q;
bit[13] c;

rx(0.4784939299) q[0];
ry(0.43417807930000002) q[1];
rz(0.38285256839999998) q[2];
h q[0];
cx q[0], q[1];
h q[8];
p(0.44879895051282759) q[8];
cx q[2], q[9];
cx q[4], q[10];
matrixEdge(0.1081724816) q[0], q[3];
matrixEdge(0.093724253100000002) q[1], q[4];
matrixEdge(0.092510468299999996) q[2], q[5];
matrixEdge(0.51282656319999997) q[3], q[6];
matrixEdge(0.46496964629999998) q[4], q[6];
matrixEdge(0.54853064929999995) q[5], q[6];
zeryaBroadcast(0.55928636519999997) q[3], q[9], q[6];
zeryaBroadcast(0.47119877110000002) q[4], q[10], q[6];
ctrl @ matrixEdge(0.41726341680000001) q[8], q[0], q[5];
inv @ matrixEdge(0.1081724816) q[0], q[3];
matrixEdge(0.54853064929999995) q[5], q[6];
matrixEdge(0.54853064929999995) q[5], q[6];
h q[11];
cx q[11], q[12];
cx q[11], q[13];
cx q[13], q[7];
cp(0.4784939299) q[13], q[6];
p(-0.01035533905932738) q[0];
p(-0.01035533905932738) q[1];
p(-0.01035533905932738) q[2];
p(-0.01035533905932738) q[3];
p(-0.01035533905932738) q[4];
p(-0.01035533905932738) q[5];
p(-0.01035533905932738) q[6];
p(-0.01035533905932738) q[7];
p(-0.01035533905932738) q[8];
p(0.086602540378443865) q[9];
p(0.086602540378443865) q[10];
p(-0.01035533905932738) q[11];
p(-0.01035533905932738) q[12];
p(0.086602540378443865) q[13];
p(-0.0051776695296636898) q[0];
p(-0.0051776695296636898) q[1];
p(-0.0051776695296636898) q[2];
p(-0.0051776695296636898) q[3];
p(-0.0051776695296636898) q[4];
p(-0.0051776695296636898) q[5];
p(-0.0051776695296636898) q[6];
p(-0.0051776695296636898) q[7];
p(-0.0051776695296636898) q[8];
p(0.043301270189221933) q[9];
p(0.043301270189221933) q[10];
p(-0.0051776695296636898) q[11];
p(-0.0051776695296636898) q[12];
p(0.043301270189221933) q[13];
c[0] = measure q[9];
if ((c[0] == 1)) {
  x q[6];
} else {
  z q[6];
}
c[1] = measure q[7];
if ((c[1] == 1)) {
  p(0.41726341680000001) q[6];
}
c[2] = measure q[6];
c[3] = measure q[0];
c[4] = measure q[1];
c[5] = measure q[2];
c[6] = measure q[3];
c[7] = measure q[4];
c[8] = measure q[5];
c[9] = measure q[6];
c[10] = measure q[7];
c[11] = measure q[11];
c[12] = measure q[12];
// DRNC-Q QPC lowering: EVET/HAYIR map to physical computational |0>/|1>.
// DRNC modular evolution is lowered to relative phase gates; global phases are omitted.
// Hardware-native mapping, qubit topology, pulse calibration and job submission belong to the local QPU driver.
