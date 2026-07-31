/**
 * @file   sil_shim.cpp
 * @brief  drives Executive::cycle() from a timeline on stdin, prints tagged observations (SSA-017)
 *
 * input, one line per cycle:
 *   <t_ms> [plant <rate_rad_s>] [nudge <delta_rad_s>] [fault <NAME> <0|1>]...
 *          [cmd <NAME> <arg> <seq>]
 *
 * `plant` switches on the single-axis rigid-body model and sets the platform's starting rate.
 * It is opt-in on purpose: with the model off the shim behaves exactly as it always has, so the
 * scenarios written before it existed are unaffected by it existing. With it on, the loop closes
 * - commanded torque drives the model, and the model's rate comes back as this cycle's body rate.
 * `nudge` adds to the platform's rate mid-run, which is the sim's version of shoving the rig by
 * hand - the disturbance a pointing controller exists to reject.
 * output, tagged lines on stdout:  CYCLE / FRAME (hex) / EVENT cmd|fault|mode / END
 *
 * host tooling only - no flight logic lives here, and no verdict either: grading is the
 * scenario runner's job (REQ-VV-001)
 */

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

#include "fsw/executive.hpp"
#include "plant.hpp"

using namespace fsw;

// print one error line and bail - a parse failure is a shim failure (exit 1), never a
// scenario result; stderr so the parseable stdout stream stays clean
[[noreturn]] static void die(const std::string& msg) {
    std::cerr << "sil_shim: " << msg << "\n";
    std::exit(1);
}

// --- the shim's platform backend: frames out as hex lines (time enters via cycle's t_ms) ---

namespace fsw::platform {

void send_telemetry(const uint8_t* frame, uint32_t len) {
    std::printf("FRAME ");
    for (uint32_t i = 0; i < len; ++i) {
        std::printf("%02x", frame[i]);
    }
    std::printf("\n");
}

// the rig, when a scenario asks for it. a file-scope object rather than something passed in,
// because the platform layer is a set of free functions by design - the same shape the stm32
// backend has, where the "state" is a peripheral
fsw::sil::Plant g_plant;
bool g_plant_on = false;

float g_torque_nm = 0.0F;

void set_wheel_torque_nm(float torque_nm) {
    // recorded, not applied here. the physics is advanced in the main loop with the timeline's
    // real interval, because the esc holds the last command until the next one arrives and a
    // callback has no idea how long that will be
    g_torque_nm = torque_nm;
    // millinewton metres, so a scenario grades an integer instead of matching float text
    std::printf("WHEEL %d\n", static_cast<int>(torque_nm * 1000.0F));
}

void poll_telemetry(uint8_t msg_id) { std::printf("POLL %u\n", static_cast<unsigned>(msg_id)); }

void capture_image(uint8_t resolution) {
    // the size is printed too, so a scenario can grade that the argument reached the payload and
    // not merely that a capture was dispatched
    std::printf("CAPTURE %u\n", static_cast<unsigned>(resolution));
}

// no image bytes in SIL - they are hardware, not decision logic. the edges are still observable so
// a scenario can grade that DOWNLINK started the stream and leaving it stopped the stream
void set_payload_downlink(bool active) {
    static bool last = false;
    if (active != last) {
        last = active;
        std::printf("PAYLOAD %s\n", active ? "ON" : "OFF");
    }
}

}  // namespace fsw::platform

// --- name lookups ---

// name -> command id by walking the catalog (state.hpp only gives id -> name)
static std::optional<Command> command_from_name(const std::string& name) {
    for (uint8_t i = 0; i < kCommandCount; ++i) {
        if (name == command_name(i)) {
            return static_cast<Command>(i);
        }
    }
    return std::nullopt;  // unknown name - caller decides to die
}

// name -> fault id by walking the catalog (state.hpp only gives id -> name)
static std::optional<Fault> fault_from_name(const std::string& name) {
    for (uint8_t i = 0; i < kFaultCount; ++i) {
        if (name == fault_name(i)) {
            return static_cast<Fault>(i);
        }
    }
    return std::nullopt;  // unknown name - caller decides to die
}

// trigger and reject names for the EVENT lines (internal enums, so no catalog in state.hpp)
static const char* trigger_name(Trigger t) {
    switch (t) {
        case Trigger::PowerOn:
            return "PowerOn";
        case Trigger::Nominal:
            return "Nominal";
        case Trigger::FaultEntry:
            return "FaultEntry";
        case Trigger::FaultCleared:
            return "FaultCleared";
        case Trigger::Timeout:
            return "Timeout";
        case Trigger::Command:
            return "Command";
    }
    return "UNKNOWN";
}

static const char* reject_name(CmdReject r) {
    switch (r) {
        case CmdReject::Ok:
            return "Ok";
        case CmdReject::UnknownId:
            return "UnknownId";
        case CmdReject::IllegalInMode:
            return "IllegalInMode";
        case CmdReject::BadArg:
            return "BadArg";
    }
    return "UNKNOWN";
}

// print every log row stamped with this cycle's time. the logs are rings that overwrite, so
// match on t instead of counting rows - safe because the timeline's t strictly increases
static void print_events(const Executive& exec, uint32_t t_ms) {
    for (const CommandEvent& e : exec.commands().log()) {
        if (e.t_ms != t_ms) {
            continue;
        }
        std::printf("EVENT cmd t=%u cmd=%s accepted=%d reason=%s\n", e.t_ms, command_name(e.cmd_id),
                    e.accepted ? 1 : 0, reject_name(e.reason));
    }
    for (const FaultEvent& e : exec.faults().log()) {
        if (e.t_ms != t_ms) {
            continue;
        }
        std::printf("EVENT fault t=%u fault=%s edge=%s\n", e.t_ms,
                    fault_name(static_cast<uint8_t>(e.fault)), e.latched ? "latch" : "clear");
    }
    for (const ModeTransition& e : exec.modes().log()) {
        if (e.t_ms != t_ms) {
            continue;
        }
        std::printf("EVENT mode t=%u from=%s to=%s trigger=%s req=%s\n", e.t_ms,
                    mode_name(static_cast<uint8_t>(e.from)), mode_name(static_cast<uint8_t>(e.to)),
                    trigger_name(e.trigger), e.req_id);
    }
}

int main() {
    Executive exec;

    uint32_t last_t = 0;
    bool ran = false;

    std::string line;
    while (std::getline(std::cin, line)) {
        // <t_ms> [fault <NAME> <0|1>]... [cmd <NAME> <arg> <seq>]
        if (line.empty() || line[0] == '#') {
            continue;
        }

        std::istringstream iss(line);

        Inputs inputs;
        uint32_t t_ms;
        if (!(iss >> t_ms)) {
            die("expected integer t_ms at line start: '" + line + "'");
        }
        if (ran && t_ms <= last_t) {
            die("t must strictly increase (got " + std::to_string(t_ms) + " after " +
                std::to_string(last_t) + ")");
        }

        std::string word;
        while (iss >> word) {
            if (word == "plant") {
                double rate;
                if (!(iss >> rate)) {
                    die("expected a starting rate in rad/s after 'plant'");
                }
                fsw::platform::g_plant = fsw::sil::Plant{};
                fsw::platform::g_plant.set_platform_rate(rate);
                fsw::platform::g_plant_on = true;
                fsw::platform::g_torque_nm = 0.0F;
            } else if (word == "nudge") {
                double delta;
                if (!(iss >> delta)) {
                    die("expected a rate delta in rad/s after 'nudge'");
                }
                if (!fsw::platform::g_plant_on) {
                    die("'nudge' needs a scenario that turned the plant on");
                }
                fsw::platform::g_plant.set_platform_rate(fsw::platform::g_plant.platform_rate() +
                                                         delta);
            } else if (word == "fault") {
                std::string name;
                if (!(iss >> name)) {
                    die("expected fault name after 'fault'");
                }
                auto f = fault_from_name(name);
                if (!f) {
                    die("unknown fault '" + name + "'");
                }
                int bad;  // 0|1 via int - never stream into 8-bit types (they read as chars)
                if (!(iss >> bad) || (bad != 0 && bad != 1)) {
                    die("expected 0|1 after fault '" + name + "'");
                }
                if (inputs.fault_updates.full()) {
                    die("too many fault items on one line");
                }
                inputs.fault_updates.push_back(FaultReport{*f, bad == 1});
            } else if (word == "cmd") {
                if (inputs.command) {
                    die("only one cmd per line");
                }
                std::string name;
                if (!(iss >> name)) {
                    die("expected command name after 'cmd'");
                }
                auto c = command_from_name(name);
                if (!c) {
                    die("unknown command '" + name + "'");
                }
                // parse via locals: streams read 8-bit types as chars, and command_t is
                // packed, so its fields can't bind to >>'s reference parameter anyway
                int arg;
                if (!(iss >> arg) || arg < 0 || arg > 255) {
                    die("expected 8-bit arg after command '" + name + "'");
                }
                uint16_t seq;
                if (!(iss >> seq)) {
                    die("expected seq after command '" + name + "'");
                }
                command_t cmd{};
                cmd.cmd_id = static_cast<uint8_t>(*c);
                cmd.arg = static_cast<uint8_t>(arg);
                cmd.seq = seq;
                inputs.command = cmd;
            } else {
                die("unknown token '" + word + "'");
            }
        }

        // advance the rig by the timeline's own interval, holding the torque commanded last
        // cycle, then hand the executive the rate that produced. this is where the loop closes:
        // everything before it is the shim reading a script, and this line is the vehicle
        // answering back
        if (fsw::platform::g_plant_on) {
            if (ran) {
                fsw::platform::g_plant.step(fsw::platform::g_torque_nm,
                                            static_cast<double>(t_ms - last_t) / 1000.0);
            }
            inputs.body_rate_rads = static_cast<float>(fsw::platform::g_plant.platform_rate());
            // the plant's true angle stands in for the magnetometer - noiseless, which makes SIL
            // an upper bound on pointing performance rather than a prediction of it
            inputs.mag_heading_rad = static_cast<float>(fsw::platform::g_plant.platform_angle());
            std::printf("PLANT rate=%.4f wheel=%.2f angle=%.4f\n",
                        fsw::platform::g_plant.platform_rate(), fsw::platform::g_plant.wheel_rate(),
                        fsw::platform::g_plant.platform_angle());
        }

        // one declared cycle: CYCLE opens the block, frames land inside it as cycle() emits
        // them, then the log rows this cycle appended
        std::printf("CYCLE t=%u\n", t_ms);
        exec.cycle(inputs, t_ms);
        print_events(exec, t_ms);
        std::fflush(stdout);

        last_t = t_ms;
        ran = true;
    }

    std::printf("END t=%u mode=%s faults=0x%08x\n", last_t,
                mode_name(static_cast<uint8_t>(exec.modes().mode())),
                static_cast<unsigned>(exec.faults().active()));
    std::fflush(stdout);
    return 0;
}
