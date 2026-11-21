#ifndef IX_ARM64_STORE_H
#define IX_ARM64_STORE_H

/* ==========================================================================
 *  AArch64 store-instruction decoder — for hardware write-watchpoint
 *  register overrides.
 *
 *  WHY THIS EXISTS
 *  ---------------
 *  Paradise's hardware breakpoints override CPU registers at hit time
 *  (hwbp_set_record writes back a record plus a 2-bit-per-register
 *  operation mask). To make a write-watchpoint actually change what the
 *  game stores, you have to override the register the store instruction
 *  SOURCES ITS DATA FROM. That register is a property of the compiled
 *  instruction at the trigger PC — it is not fixed, and it is not
 *  knowable in advance.
 *
 *  The code here previously assumed x0/x1/x2 for a Vector3. That is the
 *  AAPCS convention for a Vector3 passed as an ARGUMENT, and even then
 *  it is wrong: a Vector3 is a homogeneous floating-point aggregate, so
 *  AAPCS passes it in s0/s1/s2 (the low 32 bits of q0/q1/q2), not in the
 *  general-purpose registers. ix_pd_probe.h records exactly this from
 *  the device's own disassembly: "Vector3 argument (AAPCS HFA: s0=X
 *  s1=Y s2=Z)". For a STORE, the source register is whatever the
 *  compiler happened to allocate, which is why it gets decoded from the
 *  instruction word rather than assumed.
 *
 *  Everything below is the public AArch64 instruction encoding from the
 *  ARM Architecture Reference Manual (Loads and Stores). No addresses,
 *  offsets or game-specific values appear in this file, so nothing here
 *  is build-dependent.
 *
 *  WHAT IT DECODES
 *  ---------------
 *    STR  (immediate, unsigned offset)   GP and SIMD&FP, 1/2/4/8/16 B
 *    STUR (immediate, unscaled)          GP and SIMD&FP
 *    STR  (register offset)              GP and SIMD&FP
 *    STP  (signed offset / pre / post)   GP and SIMD&FP
 *
 *  Anything else — notably the SIMD structure stores (ST1/ST2/ST3/ST4,
 *  e.g. `st1 {v0.4s}, [x8]`) — is reported as not decoded, with the raw
 *  instruction word preserved so it can be identified and added. A
 *  caller that cannot decode an instruction MUST NOT override any
 *  register for it: writing a value into a register the store does not
 *  read would corrupt whatever that register actually holds.
 * ========================================================================== */

#include <cstdint>

/* Which register file the store reads its data from. */
enum IxArm64RegFile {
    IX_REGFILE_GP = 0,   /* x0..x30 / w0..w30                              */
    IX_REGFILE_FP = 1    /* q0..q31 (s/d/b/h are the low bits of these)    */
};

/* How the base address is formed. */
enum IxArm64AddrForm {
    IX_ADDR_IMM = 0,     /* EA = base + imm                                 */
    IX_ADDR_REG = 1,     /* EA = base + extend(Rm) << shift                 */
    IX_ADDR_POST = 2     /* EA = base (immediate is applied after the store) */
};

struct IxArm64StoreInfo {
    bool     decoded;        /* false => caller must not override anything  */
    uint32_t raw;            /* the instruction word, for logging           */

    IxArm64RegFile  regfile;
    IxArm64AddrForm addr_form;

    uint8_t  rt;             /* first  source register number               */
    uint8_t  rt2;            /* second source register number (pairs only)  */
    bool     is_pair;        /* STP: rt covers [EA, EA+xfer), rt2 the next   */

    uint8_t  xfer_bytes;     /* bytes moved PER source register (1..16)     */

    uint8_t  rn;             /* base register; 31 means SP                  */
    int64_t  imm;            /* byte offset, already scaled                 */

    /* Register-offset form only. */
    uint8_t  rm;             /* index register                              */
    uint8_t  extend_option;  /* option<2:0>: 010=UXTW 011=LSL 110=SXTW 111=SXTX */
    uint8_t  extend_shift;   /* 0, or log2(xfer_bytes) when S==1            */
};

/* Sign-extend the low `bits` of `value`. */
static inline int64_t ix_sign_extend(uint64_t value, unsigned bits) {
    const uint64_t sign = 1ull << (bits - 1);
    if (value & sign)
        return (int64_t)(value | ~((1ull << bits) - 1));
    return (int64_t)(value & ((1ull << bits) - 1));
}

/*
 * Decode one AArch64 store. Returns info.decoded == false for anything
 * that is not one of the supported store forms (including every load —
 * a watchpoint armed for writes should not be hitting those, but the
 * check costs nothing and keeps a read-watchpoint hit from being
 * mistaken for a store).
 */
static inline IxArm64StoreInfo ix_arm64_decode_store(uint32_t w) {
    IxArm64StoreInfo s{};
    s.raw     = w;
    s.decoded = false;

    const uint8_t rt  = (uint8_t)( w        & 0x1F);
    const uint8_t rn  = (uint8_t)((w >>  5) & 0x1F);
    const uint8_t rt2 = (uint8_t)((w >> 10) & 0x1F);
    const uint8_t V   = (uint8_t)((w >> 26) & 0x1);

    /* ── STP / LDP family: bits 29:27 == 101 ────────────────────────────
     * size(31:30)=opc  101  V(26)  idx(25:23)  L(22)  imm7(21:15)
     *                                            Rt2(14:10) Rn(9:5) Rt(4:0)
     * idx: 001 = post-index, 010 = signed offset, 011 = pre-index.
     * L == 0 is the store direction.                                     */
    if ((w & 0x38000000u) == 0x28000000u) {
        const uint8_t opc = (uint8_t)((w >> 30) & 0x3);
        const uint8_t idx = (uint8_t)((w >> 23) & 0x7);
        const uint8_t L   = (uint8_t)((w >> 22) & 0x1);
        if (L != 0) return s;                        /* a load            */
        if (idx != 0x1 && idx != 0x2 && idx != 0x3) return s;

        unsigned scale;
        if (V) {
            /* SIMD&FP pair: opc 00 = s (4 B), 01 = d (8 B), 10 = q (16 B) */
            if (opc == 0)      scale = 2;
            else if (opc == 1) scale = 3;
            else if (opc == 2) scale = 4;
            else               return s;
            s.regfile = IX_REGFILE_FP;
        } else {
            /* GP pair: opc 00 = 32-bit, 10 = 64-bit (01 is LDPSW, a load) */
            if (opc == 0)      scale = 2;
            else if (opc == 2) scale = 3;
            else               return s;
            s.regfile = IX_REGFILE_GP;
        }

        s.is_pair    = true;
        s.rt         = rt;
        s.rt2        = rt2;
        s.rn         = rn;
        s.xfer_bytes = (uint8_t)(1u << scale);
        s.imm        = ix_sign_extend((w >> 15) & 0x7F, 7) * (int64_t)(1u << scale);
        /* Post-index stores to the un-updated base. */
        s.addr_form  = (idx == 0x1) ? IX_ADDR_POST : IX_ADDR_IMM;
        s.decoded    = true;
        return s;
    }

    /* ── Load/store register family: bits 29:27 == 111, bit 25 == 0 ─────
     * size(31:30) 111 V(26) idx(25:24) opc(23:22) ... Rn(9:5) Rt(4:0)
     * idx 01 = unsigned immediate; idx 00 = unscaled/register forms.
     * opc 00 is a store; opc 10 with V==1 is the 128-bit (q) store.
     * The mask must leave bits 25:24 free so BOTH idx values reach the
     * dispatch below — pinning them here would silently reject every
     * `str` with an unsigned offset, which is the most common form. */
    if ((w & 0x3A000000u) == 0x38000000u) {
        const uint8_t size = (uint8_t)((w >> 30) & 0x3);
        const uint8_t idx  = (uint8_t)((w >> 24) & 0x3);
        const uint8_t opc  = (uint8_t)((w >> 22) & 0x3);

        unsigned scale;
        if (V) {
            if (opc == 0)                   scale = size;      /* b/h/s/d */
            else if (opc == 2 && size == 0) scale = 4;         /* q       */
            else                            return s;          /* load    */
            s.regfile = IX_REGFILE_FP;
        } else {
            if (opc != 0) return s;                            /* load    */
            scale     = size;                                  /* w or x  */
            s.regfile = IX_REGFILE_GP;
        }

        s.is_pair    = false;
        s.rt         = rt;
        s.rn         = rn;
        s.xfer_bytes = (uint8_t)(1u << scale);

        if (idx == 0x1) {
            /* STR (immediate, unsigned offset): imm12 is scaled. */
            s.addr_form = IX_ADDR_IMM;
            s.imm       = (int64_t)((w >> 10) & 0xFFF) * (int64_t)(1u << scale);
            s.decoded   = true;
            return s;
        }

        if (idx == 0x0) {
            const uint8_t bit21 = (uint8_t)((w >> 21) & 0x1);
            const uint8_t op11  = (uint8_t)((w >> 10) & 0x3);

            if (bit21 == 0 && op11 == 0x0) {
                /* STUR (immediate, unscaled): imm9, NOT scaled. */
                s.addr_form = IX_ADDR_IMM;
                s.imm       = ix_sign_extend((w >> 12) & 0x1FF, 9);
                s.decoded   = true;
                return s;
            }
            if (bit21 == 0 && op11 == 0x1) {
                /* STR (immediate, post-index): stores to the old base. */
                s.addr_form = IX_ADDR_POST;
                s.imm       = ix_sign_extend((w >> 12) & 0x1FF, 9);
                s.decoded   = true;
                return s;
            }
            if (bit21 == 0 && op11 == 0x3) {
                /* STR (immediate, pre-index): base is updated first. */
                s.addr_form = IX_ADDR_IMM;
                s.imm       = ix_sign_extend((w >> 12) & 0x1FF, 9);
                s.decoded   = true;
                return s;
            }
            if (bit21 == 1 && op11 == 0x2) {
                /* STR (register offset). */
                s.addr_form     = IX_ADDR_REG;
                s.rm            = (uint8_t)((w >> 16) & 0x1F);
                s.extend_option = (uint8_t)((w >> 13) & 0x7);
                s.extend_shift  = (uint8_t)(((w >> 12) & 0x1) ? scale : 0);
                s.imm           = 0;
                s.decoded       = true;
                return s;
            }
        }
        return s;
    }

    /* Not a store form this decoder handles (SIMD ST1/ST2/ST3/ST4, the
     * atomics, exclusives, etc). decoded stays false on purpose. */
    return s;
}

/*
 * Apply the extend/shift of a register-offset store to an index value,
 * per the ARM ARM ExtendReg() for option<2:0>.
 */
static inline uint64_t ix_arm64_extend_reg(uint64_t rm_value, uint8_t option,
                                           uint8_t shift) {
    uint64_t v;
    switch (option) {
        case 0x2: v = (uint64_t)(uint32_t)rm_value; break;                 /* UXTW */
        case 0x3: v = rm_value; break;                                      /* LSL  */
        case 0x6: v = (uint64_t)(int64_t)(int32_t)(uint32_t)rm_value; break; /* SXTW */
        case 0x7: v = rm_value; break;                                      /* SXTX */
        default:  v = rm_value; break;
    }
    return v << shift;
}

#endif /* IX_ARM64_STORE_H */
