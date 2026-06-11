/**
 * @file   sil_shim.cpp
 * @brief  drives Executive::cycle() from a timeline on stdin, prints tagged observations (SSA-017)
 *
 * input, one line per cycle:  <t_ms> [fault <NAME> <0|1>]... [cmd <NAME> <arg> <seq>]
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

using namespace fsw;

// print one error line and bail - a parse failure is a shim failure (exit 1), never a
// scenario result; stderr so the parseable stdout stream stays clean
[[noreturn]] static void die(const std::string& msg) {
    std::cerr << "sil_shim: " << msg << "\n";
    std::exit(1);
}

// --- the shim's platform backend: injected scenario time, frames out as hex lines ---

namespace {
uint32_t g_now_ms = 0;  // current cycle's injected time
}

namespace fsw::platform {

uint32_t now_ms() { return g_now_ms; }

void send_telemetry(const uint8_t* frame, uint32_t len) {
    std::printf("FRAME ");
    for (uint32_t i = 0; i < len; ++i) {
        std::printf("%02x", frame[i]);
    }
    std::printf("\n");
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
            if (word == "fault") {
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

        // one declared cycle: CYCLE opens the block, frames land inside it as cycle() emits
        // them, then the log rows this cycle appended
        std::printf("CYCLE t=%u\n", t_ms);
        g_now_ms = t_ms;
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
