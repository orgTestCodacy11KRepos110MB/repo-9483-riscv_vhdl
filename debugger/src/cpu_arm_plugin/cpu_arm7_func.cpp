/*
 *  Copyright 2018 Sergey Khabarov, sergeykhbr@gmail.com
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include <api_core.h>
#include "cpu_arm7_func.h"
#include "srcproc/thumb_disasm.h"

namespace debugger {

CpuCortex_Functional::CpuCortex_Functional(const char *name) :
    CpuGeneric(name),
    portRegs_(this, "regs", 0x8000, Reg_Total) {
    registerInterface(static_cast<ICpuArm *>(this));
    registerAttribute("VectorTable", &vectorTable_);
    registerAttribute("DefaultMode", &defaultMode_);
    p_psr_ = reinterpret_cast<ProgramStatusRegsiterType *>(
            &portRegs_.getp()[Reg_cpsr]);
}

CpuCortex_Functional::~CpuCortex_Functional() {
}

void CpuCortex_Functional::postinitService() {
    // Supported instruction sets:
    for (int i = 0; i < INSTR_HASH_TABLE_SIZE; i++) {
        listInstr_[i].make_list(0);
    }
    addArm7tmdiIsa();

    CpuGeneric::postinitService();

    pcmd_br_ = new CmdBrArm(itap_);
    icmdexec_->registerCommand(static_cast<ICommand *>(pcmd_br_));

    pcmd_reg_ = new CmdRegArm(itap_);
    icmdexec_->registerCommand(static_cast<ICommand *>(pcmd_reg_));

    pcmd_regs_ = new CmdRegsArm(itap_);
    icmdexec_->registerCommand(static_cast<ICommand *>(pcmd_regs_));

    if (defaultMode_.is_equal("Thumb")) {
        setInstrMode(THUMB_mode);
    }
}

void CpuCortex_Functional::predeleteService() {
    CpuGeneric::predeleteService();

    icmdexec_->unregisterCommand(static_cast<ICommand *>(pcmd_br_));
    icmdexec_->unregisterCommand(static_cast<ICommand *>(pcmd_reg_));
    icmdexec_->unregisterCommand(static_cast<ICommand *>(pcmd_regs_));
    delete pcmd_br_;
    delete pcmd_reg_;
    delete pcmd_regs_;
}

/** HAP_ConfigDone */
void CpuCortex_Functional::hapTriggered(IFace *isrc, EHapType type,
                                        const char *descr) {
    AttributeType pwrlist;
    IPower *ipwr;
    RISCV_get_iface_list(IFACE_POWER, &pwrlist);
    for (unsigned i = 0; i < pwrlist.size(); i++) {
        ipwr = static_cast<IPower *>(pwrlist[i].to_iface());
        ipwr->power(POWER_ON);
    }

    CpuGeneric::hapTriggered(isrc, type, descr);
}

unsigned CpuCortex_Functional::addSupportedInstruction(
                                    ArmInstruction *instr) {
    AttributeType tmp(instr);
    listInstr_[instr->hash()].add_to_list(&tmp);
    return 0;
}

void CpuCortex_Functional::handleTrap() {
    if ((interrupt_pending_[0] | interrupt_pending_[1]) == 0) {
        return;
    }
    if (interrupt_pending_[0] & (1ull << Interrupt_SoftwareIdx)) {
        DsuMapType::udbg_type::debug_region_type::breakpoint_control_reg t1;
        t1.val = br_control_.getValue().val;
        if (t1.bits.trap_on_break == 0) {
            sw_breakpoint_ = true;
            interrupt_pending_[0] &= ~(1ull << Interrupt_SoftwareIdx);
            npc_.setValue(pc_.getValue());
            halt("SWI Breakpoint");
            return;
        }
    }
    npc_.setValue(0 + 4*0);
    interrupt_pending_[0] = 0;
}

uint64_t CpuCortex_Functional::getResetAddress() {
    Axi4TransactionType tr;
    tr.action = MemAction_Read;
    tr.addr = resetVector_.to_uint64() + 4;
    tr.source_idx = sysBusMasterID_.to_int();
    tr.xsize = 4; 
    dma_memop(&tr);
    return tr.rpayload.b32[0] - 1;
}

void CpuCortex_Functional::reset(IFace *isource) {
    Axi4TransactionType tr;
    CpuGeneric::reset(isource);
    portRegs_.reset();
    ITBlockMask_ = 0;
    ITBlockCnt_ = 0;

    tr.action = MemAction_Read;
    tr.addr = resetVector_.to_uint64();
    tr.source_idx = sysBusMasterID_.to_int();
    tr.xsize = 4; 
    dma_memop(&tr);
    setReg(Reg_sp, tr.rpayload.b32[0]);
    if (defaultMode_.is_equal("Thumb")) {
        setInstrMode(THUMB_mode);
    }
    estate_ = CORE_Halted;
}

void CpuCortex_Functional::StartITBlock(uint32_t firstcond, uint32_t mask) {
    ITBlockMask_ = mask;
    ITBlockCond_[0] = firstcond;
}

GenericInstruction *CpuCortex_Functional::decodeInstruction(Reg64Type *cache) {
    GenericInstruction *instr = NULL;
    uint32_t ti = cacheline_[0].buf32[0];

    EIsaArmV7 etype;
    if (getInstrMode() == THUMB_mode) {
        uint32_t tio;
        etype = decoder_thumb(ti, &tio, errmsg_, sizeof(errmsg_));
        //cacheline_[0].buf32[0] = tio;
    } else {
        etype = decoder_arm(ti, errmsg_, sizeof(errmsg_));
    }

    if (etype < ARMV7_Total) {
        instr = isaTableArmV7_[etype];
    } else {
        RISCV_error("ARM decoder error [%08" RV_PRI64 "x] %08x",
                    getPC(), ti);
    }
    portRegs_.getp()[Reg_pc].val = getPC();
    return instr;
}

void CpuCortex_Functional::generateIllegalOpcode() {
    //raiseSignal(EXCEPTION_InstrIllegal);
    RISCV_error("Illegal instruction at 0x%08" RV_PRI64 "x", getPC());
}

void CpuCortex_Functional::traceOutput() {
    char tstr[1024];
    trace_action_type *pa;

    disasm_thumb(trace_data_.pc,
                 trace_data_.instr,
                 trace_data_.disasm,
                 sizeof(trace_data_.disasm));

    RISCV_sprintf(tstr, sizeof(tstr),
        "%9" RV_PRI64 "d: %08" RV_PRI64 "x: %s \n",
            trace_data_.step_cnt - 1,
            trace_data_.pc,
            trace_data_.disasm);
    (*trace_file_) << tstr;

    for (int i = 0; i < trace_data_.action_cnt; i++) {
        pa = &trace_data_.action[i];
        if (!pa->memop) {
            RISCV_sprintf(tstr, sizeof(tstr),
                "%21s %10s <= %08x\n",
                    "",
                    IREGS_NAMES[pa->waddr],
                    static_cast<uint32_t>(pa->wdata));
        } else if (pa->memop_write) {
            RISCV_sprintf(tstr, sizeof(tstr),
                "%21s [%08" RV_PRI64 "x] <= %08x\n",
                    "",
                    pa->memop_addr,
                    pa->memop_data.buf32[0]);
        } else {
            RISCV_sprintf(tstr, sizeof(tstr),
                "%21s [%08" RV_PRI64 "x] => %08x\n",
                    "",
                    pa->memop_addr,
                    pa->memop_data.buf32[0]);
        }
        (*trace_file_) << tstr;
    }

    trace_file_->flush();
}

void CpuCortex_Functional::raiseSignal(int idx) {
    RISCV_error("Raise unsupported signal %d", idx);
}

void CpuCortex_Functional::lowerSignal(int idx) {
    interrupt_pending_[idx >> 6] &= ~(1ull << (idx & 0x3F));
    RISCV_error("Lower unsupported signal %d", idx);
}

void CpuCortex_Functional::raiseSoftwareIrq() {
    interrupt_pending_[0] |= (1ull << Interrupt_SoftwareIdx);
}

}  // namespace debugger

