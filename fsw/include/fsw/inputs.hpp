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
    std::optional<temp_data_t> temp;                      // incoming temperature sample(optional)
    std::optional<wheel_status_t> wheel;                  // wheel status, only on cycles one lands
    std::optional<camera_data_t> camera;                  // incoming camera health (optional)

    // yaw rate in rad/s, assembled at the platform boundary rather than derived here.
    //
    // imu_data_t carries raw counts, which is right for the wire - lossless and small, and the
    // ground converts. it is wrong for the flight software: turning counts into rad/s needs the
    // ICM-20948's full-scale setting, and a control law that knows a specific part's LSB scaling
    // stops being portable the day the imu changes (REQ-PAL-001). so the platform converts and
    // the fsw is handed a physical quantity, the same way it is handed a command rather than a
    // uart. unset on a cycle where no attitude sensor reported
    std::optional<float> body_rate_rads;
    // set on the first cycle after a reset and never again - the platform knows why the computer
    // restarted, and this is how that crosses the boundary as an input rather than a side channel
    std::optional<boot_info_t> boot;
};

}  // namespace fsw

#endif  // FSW_INPUTS_HPP
