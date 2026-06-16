/**
 * @file   inputs.hpp
 * @brief  what the flight software receives each cycle - fault samples, commands, sensor readings
 */

#ifndef FSW_INPUTS_HPP
#define FSW_INPUTS_HPP

#include <optional>

#include "etl/vector.h"
#include "protocol/msg.hpp"
#include "protocol/state.hpp"

namespace fsw {

// a fault and whether this sample is bad - the injected path (SIL, or non-sensor faults)
struct FaultReport {
    Fault fault;  // which fault
    bool bad;     // true if this sample is faulty
};

// everything handed to Executive::cycle for one control cycle. the caller assembles it - main on
// the target, the SIL shim on the host - so the flight logic stays a pure function of its inputs
struct Inputs {
    etl::vector<FaultReport, kFaultCount> fault_updates;  // fault samples since last cycle
    std::optional<command_t> command;                     // incoming command (optional)
    std::optional<imu_data_t> imu;                        // incoming imu sample (optional)
    std::optional<power_data_t> power;                    // incoming power sample (optional)
};

}  // namespace fsw

#endif  // FSW_INPUTS_HPP
