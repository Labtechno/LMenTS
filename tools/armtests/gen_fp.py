#!/usr/bin/env python3
"""Génère fp.S + fp_table.c : un petit sous-programme par instruction flottante testée.

Chaque sous-programme reçoit (a, b, c, io) : il installe FPSCR (io[0]), charge a, b, c
dans s0, s1, s2, exécute l'instruction, puis rend s0, le nouveau FPSCR (io[0]) et s2
(io[1]). Le même binaire est exécuté par l'émulateur LMenTS et par QEMU (référence).
"""
ops = []
def add(*lines): ops.append(list(lines))

# --- Calculs ---
for o in ["vadd", "vsub", "vmul", "vnmul", "vdiv"]:
    add(f"{o}.f32 s0, s0, s1"); add(f"{o}.f32 s2, s1, s0")
for o in ["vmla", "vmls", "vnmla", "vnmls", "vfma", "vfms", "vfnma", "vfnms"]:
    add(f"{o}.f32 s0, s1, s2"); add(f"{o}.f32 s2, s0, s1")
for o in ["vsqrt", "vabs", "vneg", "vmov"]:
    add(f"{o}.f32 s0, s1")
# Registres hauts (autres encodages des numéros de registre)
add("vmov.f32 s31, s0", "vmov.f32 s17, s1", "vadd.f32 s30, s31, s17", "vmul.f32 s15, s30, s2", "vmov.f32 s0, s15", "vmov.f32 s2, s30")
add("vmov.f32 s9, s0", "vmov.f32 s22, s1", "vfma.f32 s9, s22, s2", "vdiv.f32 s2, s9, s22", "vmov.f32 s0, s9")

# --- Comparaisons ---
flags = ["vmrs APSR_nzcv, fpscr", "mrs r1, apsr", "vmov s2, r1"]
add("vcmp.f32 s0, s1", *flags); add("vcmpe.f32 s0, s1", *flags)
add("vcmp.f32 s0, #0", *flags); add("vcmpe.f32 s1, #0", *flags)
for cond, inv in [("eq", "ne"), ("mi", "pl"), ("gt", "le"), ("hi", "ls"), ("vs", "vc"), ("ge", "lt")]:
    add("vcmp.f32 s0, s1", "vmrs APSR_nzcv, fpscr", f"ite {cond}", f"vadd{cond}.f32 s0, s0, s2", f"vsub{inv}.f32 s0, s1, s2")

# --- Conversions ---
for o in ["vcvt.s32.f32", "vcvt.u32.f32", "vcvtr.s32.f32", "vcvtr.u32.f32", "vcvt.f32.s32", "vcvt.f32.u32"]:
    add(f"{o} s0, s1")
for n in [1, 7, 16, 31, 32]:
    for o in ["vcvt.f32.s32", "vcvt.f32.u32", "vcvt.s32.f32", "vcvt.u32.f32"]:
        add(f"{o} s0, s0, #{n}")
for n in [1, 9, 16]:
    for o in ["vcvt.f32.s16", "vcvt.f32.u16", "vcvt.s16.f32", "vcvt.u16.f32"]:
        add(f"{o} s0, s0, #{n}")
for o in ["vcvtb.f32.f16", "vcvtt.f32.f16", "vcvtb.f16.f32", "vcvtt.f16.f32"]:
    add(f"{o} s0, s1")

# --- Constantes et transferts ---
for c in ["1.0", "-2.5", "0.125", "31.0", "-0.1875", "17.0", "0.5", "-1.9375"]:
    add(f"vmov.f32 s0, #{c}")
add("vmov r1, s0", "vmov s2, r1", "vmov s0, r2")
add("vmov d3, r0, r1", "vmov r2, r3, d3", "vmov s0, r3", "vmov s2, r2")
add("vmov s3, s4, r0, r1", "vmov r3, r2, s3, s4", "vmov s0, r2", "vmov s2, r3")
# vmov.32 d1[0], r1 / vmov.32 d1[1], r0 / vmov.32 r1, d0[1] : l'assembleur les refuse pour
# cette puce alors que le manuel ARMv7-M les décrit ; on donne donc leur code directement.
add(".inst.w 0xee011b10", ".inst.w 0xee210b10", ".inst.w 0xee301b10", "vmov s0, r1")
add("vmov d5, r1, r2", "vmov.f32 s0, s11", "vmov.f32 s2, s10")
add("vmrs r1, fpscr", "eor r1, r1, r0", "vmsr fpscr, r1", "vmrs r1, fpscr", "vmov s0, r1")

# --- Mémoire ---
add("vpush {s0-s2}", "vpop {s3-s5}", "vmov.f32 s0, s5", "vmov.f32 s2, s3")
add("vpush {d0-d1}", "ldr r1, [sp, #4]", "ldr r2, [sp, #8]", "add sp, #16", "vmov s0, r2", "vmov s2, r1")
add("sub sp, #16", "vstr s0, [sp, #4]", "vstr s1, [sp, #12]", "vldr s2, [sp, #4]", "vldr s0, [sp, #12]", "add sp, #16")
add("sub sp, #16", "vstr d0, [sp, #8]", "vldr d1, [sp, #8]", "add sp, #16", "vmov.f32 s0, s3")
add("sub sp, #32", "mov r1, sp", "vstmia r1!, {s0-s2}", "vldmdb r1!, {s4-s6}", "add sp, #32", "vmov.f32 s0, s6", "vmov.f32 s2, s4")
add("sub sp, #32", "add r1, sp, #32", "vstmdb r1!, {d0-d1}", "vldmia r1, {d2-d3}", "add sp, #32", "vmov.f32 s0, s7", "vmov.f32 s2, s4")
add("vldr s0, 1f", "vldr s2, 2f", "vadd.f32 s0, s0, s1", "b 3f", ".align 2", "1: .word 0x40490fdb", "2: .word 0xc1200000", "3:")

with open("fp.S", "w") as f:
    f.write(".syntax unified\n.cpu cortex-m4\n.fpu fpv4-sp-d16\n.thumb\n.text\n")
    for i, lines in enumerate(ops):
        f.write(f"\n.global fp_test_{i}\n.thumb_func\n.type fp_test_{i}, %function\nfp_test_{i}:\n")
        f.write("  push {r4, lr}\n  mov r4, r3\n  ldr r3, [r4]\n  vmsr fpscr, r3\n  vmov s0, r0\n  vmov s1, r1\n  vmov s2, r2\n")
        for l in lines:
            f.write(f"  {l}\n" if not l.endswith(":") and not l[1:2] == ":" else f"{l}\n")
        f.write("  vmov r0, s0\n  vmov r1, s2\n  vmrs r3, fpscr\n  str r3, [r4]\n  str r1, [r4, #4]\n  pop {r4, pc}\n")
with open("fp_table.c", "w") as f:
    f.write("#include <stdint.h>\ntypedef uint32_t (*fp_fn)(uint32_t, uint32_t, uint32_t, uint32_t*);\n")
    for i in range(len(ops)):
        f.write(f"uint32_t fp_test_{i}(uint32_t, uint32_t, uint32_t, uint32_t*);\n")
    f.write(f"const int fp_test_count = {len(ops)};\nconst fp_fn fp_tests[] = {{\n")
    for i in range(len(ops)):
        f.write(f"  fp_test_{i},\n")
    f.write("};\n")
print(len(ops), "instructions flottantes testées")
