/*
 *  Copyright 2022 Sergey Khabarov, sergeykhbr@gmail.com
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

#pragma once

#include "api_core.h"
#include "coreservices/imemop.h"
#include <systemc.h>

namespace debugger {

class BusSlave : public sc_module,
                 public IMemoryOperation {
 public:
    sc_in<bool> i_clk;   // system clock
    sc_in<bool> i_nrst;
    sc_out<bool> o_req_valid;
    sc_in<bool> i_req_ready;
    sc_out<sc_uint<7>> o_req_addr;
    sc_out<bool> o_req_write;
    sc_out<sc_uint<32>> o_req_wdata;
    sc_in<bool> i_slv_resp_valid;
    sc_out<bool> o_slv_resp_ready;
    sc_in<sc_uint<32>> i_slv_resp_rdata;

    void registers();

    SC_HAS_PROCESS(BusSlave);

    BusSlave(sc_module_name name);

    /** IMemoryOperation */
    virtual ETransStatus b_transport(Axi4TransactionType *trans);
    virtual ETransStatus nb_transport(Axi4TransactionType *trans,
                                      IAxi4NbResponse *cb) override ;

 private:
    void readreg(uint64_t idx);
    void writereg(uint64_t idx, uint32_t w32);

 private:
    // Last processing transaction
    Axi4TransactionType lasttrans_;
    IAxi4NbResponse *lastcb_;

    bool bus_req_valid_;
    uint32_t bus_req_addr_;
    bool bus_req_write_;
    uint32_t bus_req_wdata_;
    uint32_t bus_resp_data_;
};

}  // namespace debugger

