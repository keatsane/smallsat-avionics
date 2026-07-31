"""Frame and message codec for the avionics link.

The single Python implementation of the wire format defined in C++ under
common/protocol/ (frame.hpp, msg.hpp, state.hpp):
[AA 55][id][len][payload][crc16, big-endian].
Shared by the serial monitor and the host tests so the contract lives in one place.
"""

import math
import struct

SYNC0 = 0xAA
SYNC1 = 0x55

# message ids - mirror MsgId in msg.hpp
MSG_COMMAND = 0x01
MSG_COMMAND_ACK = 0x02
MSG_HEARTBEAT = 0x03
MSG_UART_STATUS = 0x04
MSG_LORA_STATUS = 0x05
MSG_NRF24_STATUS = 0x06
MSG_IMU_DATA = 0x07
MSG_POWER_DATA = 0x08
MSG_TEMP_DATA = 0x09
MSG_PAYLOAD_DATA = 0x10
MSG_CAMERA_STATUS = 0x11
MSG_DOWNLINK_STATUS = 0x12
MSG_GROUND_STATUS = 0x13
MSG_ATTITUDE_STATUS = 0x14
MSG_CHUNK_REQUEST = 0x15
MSG_WHEEL_COMMAND = 0x20
MSG_WHEEL_STATUS = 0x21
MSG_TASK_HEALTH = 0x30
MSG_BOOT_INFO = 0x31


def crc16(data: bytes) -> int:
    """CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) - matches frame.cpp."""
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def encode(msg_id: int, payload: bytes) -> bytes:
    """Wrap a message in a frame ready to send (mirror of frame_encode)."""
    body = bytes([msg_id, len(payload)]) + payload
    crc = crc16(body)
    return bytes([SYNC0, SYNC1]) + body + bytes([crc >> 8, crc & 0xFF])


class FrameDecoder:
    """Incremental frame decoder - feed it one byte at a time."""

    def __init__(self):
        self.reset()

    def reset(self):
        self.state = "sync0"
        self.msg_id = 0
        self.length = 0
        self.payload = bytearray()
        self.crc_rx = 0
        self.crc_errors = 0  # frames dropped on a bad crc since reset

    def push(self, byte: int):
        """Return (msg_id, payload) when a valid frame completes, else None."""
        if self.state == "sync0":
            if byte == SYNC0:
                self.state = "sync1"
        elif self.state == "sync1":
            self.state = "id" if byte == SYNC1 else "sync0"
        elif self.state == "id":
            self.msg_id = byte
            self.state = "len"
        elif self.state == "len":
            self.length = byte
            self.payload = bytearray()
            self.state = "crc_hi" if byte == 0 else "payload"
        elif self.state == "payload":
            self.payload.append(byte)
            if len(self.payload) >= self.length:
                self.state = "crc_hi"
        elif self.state == "crc_hi":
            self.crc_rx = byte << 8
            self.state = "crc_lo"
        elif self.state == "crc_lo":
            self.crc_rx |= byte
            self.state = "sync0"
            body = bytes([self.msg_id, self.length]) + bytes(self.payload)
            if crc16(body) == self.crc_rx:
                return self.msg_id, bytes(self.payload)
            self.crc_errors += 1
        return None


# rejection reasons - mirror CmdReject in fsw/include/fsw/comms/command_handler.hpp
REJECT_REASONS = ["Ok", "UnknownId", "IllegalInMode", "BadArg"]


def reject_name(reason: int) -> str:
    """Name for an ack's reason byte, or 'UNKNOWN' if out of range."""
    return REJECT_REASONS[reason] if 0 <= reason < len(REJECT_REASONS) else "UNKNOWN"


GROUND_FLAG_LORA_UP = 0x01
GROUND_FLAG_NRF24_UP = 0x02
ATTITUDE_FLAG_IN_BAND = 0x01
ATTITUDE_FLAG_SATURATED = 0x02

# one byte spanning a full turn - SET_HEADING's argument, mirroring kHeadingStepRad in state.hpp
HEADING_STEPS = 256


def heading_arg(degrees: float) -> int:
    """Degrees -> the binary angle SET_HEADING carries. Wraps, so 370 and 10 are the same aim."""
    return int(round((degrees % 360.0) * HEADING_STEPS / 360.0)) % HEADING_STEPS


GROUND_FLAG_PANEL1 = 0x04
GROUND_FLAG_PANEL2 = 0x08


def decode_attitude_status(payload: bytes) -> dict:
    """Unpack an attitude_status_t payload (msg.hpp) into a dict, in degrees."""
    t_ms, heading, target, rate, torque, flags = struct.unpack("<IhhhhB", payload)
    return {
        "t_ms": t_ms,
        "heading_deg": math.degrees(heading / 1000.0),
        "target_deg": math.degrees(target / 1000.0),
        "rate_dps": math.degrees(rate / 1000.0),
        "torque_mnm": torque,
        "flags": flags,
    }


def decode_ground_status(payload: bytes) -> dict:
    """Unpack a ground_status_t payload (msg.hpp) into a dict."""
    t_ms, lora_frames, nrf24_frames, nrf24_packets, busy_pct, flags = struct.unpack(
        "<IIIIBB", payload
    )
    return {
        "t_ms": t_ms,
        "lora_frames": lora_frames,
        "nrf24_frames": nrf24_frames,
        "nrf24_packets": nrf24_packets,
        "busy_pct": busy_pct,
        "flags": flags,
    }


def decode_downlink_status(payload: bytes) -> dict:
    """Unpack a downlink_status_t payload (msg.hpp) into a dict."""
    t_ms, image_id, chunk, chunks, radio_sent, radio_dropped = struct.unpack("<IHHHIH", payload)
    return {
        "t_ms": t_ms,
        "image_id": image_id,
        "chunk": chunk,
        "chunks": chunks,
        "radio_sent": radio_sent,
        "radio_dropped": radio_dropped,
    }


def progress_bar(done: int, total: int, width: int = 20) -> str:
    """A fixed-width [####----] bar. Empty when the total is unknown."""
    if total <= 0:
        return "[" + "-" * width + "]"
    filled = min(width, max(0, done * width // total))
    return "[" + "#" * filled + "-" * (width - filled) + "]"


def decode_command_ack(payload: bytes) -> dict:
    """Unpack a command_ack_t payload (msg.hpp) into a dict."""
    cmd_id, seq, accepted, reason = struct.unpack("<BH2B", payload)
    return {
        "cmd_id": cmd_id,
        "seq": seq,
        "accepted": bool(accepted),
        "reason": reject_name(reason),
    }


# commands - mirror FSW_COMMAND_LIST in common/protocol/state.hpp (drift-checked by test_frames).
# a command's index here is its id on the wire
COMMANDS = ["NOOP", "SET_MODE", "CLEAR_FAULT", "CAPTURE_IMAGE", "SET_HEADING", "REQUEST_TELEMETRY"]

# what REQUEST_TELEMETRY can ask for, by the name the console already prints for that kind. the
# values are wire message ids - the one catalog here that is a mapping rather than an index,
# because msg ids are not contiguous
POLL_TARGETS = {
    "IMU": 0x07,
    "POWER": 0x08,
    "TEMP": 0x09,
    "CAMERA": 0x11,
    "UART": 0x04,
    "LORA": 0x05,
    "NRF24": 0x06,
    "TASKS": 0x30,
}

# image sizes CAPTURE_IMAGE can ask for - mirrors FSW_RESOLUTION_LIST in state.hpp, index is the
# wire value (drift-checked by test_frames)
RESOLUTIONS = ["320x240", "800x600", "1600x1200"]


def arg_label(cmd_id: int, arg: int) -> str:
    """A command's argument as the name it was typed as, falling back to the raw number.

    The wire carries an index into a catalog, and printing the index makes a log that has to be
    decoded by hand against a header. Every catalog is already mirrored here.
    """
    name = command_name(cmd_id)
    catalog = {"SET_MODE": MODES, "CLEAR_FAULT": FAULTS, "CAPTURE_IMAGE": RESOLUTIONS}.get(name)
    if catalog is None or arg >= len(catalog):
        return str(arg)
    return catalog[arg]


def command_name(cmd_id: int) -> str:
    """Name for a command id, or 'UNKNOWN' if out of range."""
    return COMMANDS[cmd_id] if 0 <= cmd_id < len(COMMANDS) else "UNKNOWN"


# modes - mirror FSW_MODE_LIST in common/protocol/state.hpp (drift-checked by test_frames)
MODES = ["BOOT", "STANDBY", "DETUMBLE", "POINTING", "DOWNLINK", "SAFE"]


def mode_name(mode: int) -> str:
    """Name for a heartbeat's mode byte, or 'UNKNOWN' if out of range."""
    return MODES[mode] if 0 <= mode < len(MODES) else "UNKNOWN"


# reset causes - mirror FSW_RESET_CAUSE_LIST in common/protocol/state.hpp (drift-checked)
RESET_CAUSES = [
    "UNKNOWN",
    "POWER_ON",
    "RESET_PIN",
    "BROWNOUT",
    "SOFTWARE",
    "WATCHDOG",
    "WINDOW_WATCHDOG",
    "LOWPOWER",
]


def reset_cause_name(cause: int) -> str:
    """Name for a boot_info_t reset cause, or 'UNKNOWN' if out of range."""
    return RESET_CAUSES[cause] if 0 <= cause < len(RESET_CAUSES) else "UNKNOWN"


# faults - mirror FSW_FAULT_LIST in common/protocol/state.hpp (drift-checked by test_frames).
# a fault's index here is its bit position in the heartbeat fault bitmask
FAULTS = [
    "COMMAND_LINK_LOSS",
    "ACCEL_GYRO_DROPOUT",
    "MAG_DROPOUT",
    "POWER_DROPOUT",
    "UNDERVOLTAGE",
    "OVERVOLTAGE",
    "OVERCURRENT",
    "TEMP_DROPOUT",
    "UNDERTEMPERATURE",
    "OVERTEMPERATURE",
    "WHEEL_DROPOUT",
    "CAMERA_DROPOUT",
]


def fault_names(faults: int) -> str:
    """Readable set of the latched faults in a fault bitmask, e.g. '{UNDERVOLTAGE}' or '{}'."""
    names = (
        FAULTS[bit] if bit < len(FAULTS) else f"bit{bit}"
        for bit in range(faults.bit_length())
        if faults & (1 << bit)
    )
    return "{" + ", ".join(names) + "}"


def decode_heartbeat(payload: bytes) -> dict:
    """Unpack a heartbeat_t payload (msg.hpp) into a dict."""
    uptime_ms, mode, faults, inhibited, seq, bus_mv = struct.unpack("<IBIIHH", payload)
    return {
        "uptime_ms": uptime_ms,
        "inhibited": inhibited,
        "mode": mode_name(mode),
        "faults": faults,
        "seq": seq,
        "bus_mv": bus_mv,
    }


def decode_imu_data(payload: bytes) -> dict:
    """Unpack a imu_data_t payload (msg.hpp) into a dict."""
    t_ms, ax, ay, az, gx, gy, gz, mx, my, mz, flags = struct.unpack("<I9hB", payload)
    return {
        "t_ms": t_ms,
        "accel": (ax, ay, az),
        "gyro": (gx, gy, gz),
        "mag": (mx, my, mz),
        "flags": flags,
    }


def decode_power_data(payload: bytes) -> dict:
    """Unpack a power_data_t payload (msg.hpp) into a dict."""
    t_ms, bus_mv, current_ma, power_mw, flags = struct.unpack("<2IiIB", payload)
    return {
        "t_ms": t_ms,
        "bus_mv": bus_mv,
        "current_ma": current_ma,
        "power_mw": power_mw,
        "flags": flags,
    }


def decode_temp_data(payload: bytes) -> dict:
    """Unpack a temp_data_t payload (msg.hpp) into a dict."""
    t_ms, temp_mc, flags = struct.unpack("<IiB", payload)
    return {
        "t_ms": t_ms,
        "temp_mc": temp_mc,
        "flags": flags,
    }


CHUNK_REQUEST_MAX = 28  # mirrors kChunkRequestMax in msg.hpp


def encode_chunk_request(image_id: int, chunks: list[int]) -> bytes:
    """Frame a request for up to CHUNK_REQUEST_MAX missing payload chunks.

    This is the ground's half of selective repeat: the vehicle sent the image once, this names
    the gaps, and the vehicle resends exactly those. The struct is fixed-size on the wire, so
    unused entries are zero and the count field says how many are real.
    """
    chunks = chunks[:CHUNK_REQUEST_MAX]
    padded = chunks + [0] * (CHUNK_REQUEST_MAX - len(chunks))
    payload = struct.pack(f"<HB{CHUNK_REQUEST_MAX}H", image_id, len(chunks), *padded)
    return encode(MSG_CHUNK_REQUEST, payload)


def encode_command(cmd_id: int, arg: int, seq: int) -> bytes:
    """Frame a command_t (msg.hpp) ready to send to the OBC."""
    return encode(MSG_COMMAND, struct.pack("<BBH", cmd_id, arg, seq))


# bytes of image per chunk - mirrors kPayloadChunkBytes in msg.hpp
PAYLOAD_CHUNK_BYTES = 56


def decode_payload_data(payload: bytes) -> dict:
    """Unpack a payload_data_t payload (msg.hpp) into a dict; data is trimmed to its valid length."""
    image_id, chunk, chunks, length, _reserved = struct.unpack("<3H2B", payload[:8])
    return {
        "image_id": image_id,
        "chunk": chunk,
        "chunks": chunks,
        "len": length,
        "data": payload[8 : 8 + length],
    }


def decode_camera_data(payload: bytes) -> dict:
    """Unpack a camera_data_t payload (msg.hpp) into a dict."""
    t_ms, frame_bytes, flags = struct.unpack("<IIB", payload)
    return {
        "t_ms": t_ms,
        "frame_bytes": frame_bytes,
        "flags": flags,
    }


RADIO_FLAG_CONFIGURED = 0x01
RADIO_FLAG_ANSWERING = 0x02


def decode_radio_status(payload: bytes) -> dict:
    """Unpack a radio_status_t payload (msg.hpp) into a dict - same shape for both radios."""
    t_ms, sent, dropped, flags = struct.unpack("<IIHB", payload)
    return {
        "t_ms": t_ms,
        "sent": sent,
        "dropped": dropped,
        "flags": flags,
    }


def decode_boot_info(payload: bytes) -> dict:
    """Unpack a boot_info_t payload (msg.hpp) into a dict."""
    t_ms, clk_hz, reset_cause = struct.unpack("<IIB", payload)
    return {
        "t_ms": t_ms,
        "clk_hz": clk_hz,
        "reset_cause": reset_cause,
    }


def encode_wheel_command(torque_mv: int, seq: int) -> bytes:
    """Frame a wheel_command_t (msg.hpp) ready to send to the ESC node."""
    return encode(MSG_WHEEL_COMMAND, struct.pack("<hH", torque_mv, seq))


def decode_wheel_status(payload: bytes) -> dict:
    """Unpack a wheel_status_t payload (msg.hpp) into a dict."""
    velocity_mrad_s, angle_mrad, torque_mv, flags, seq = struct.unpack("<iihBH", payload)
    return {
        "velocity_mrad_s": velocity_mrad_s,
        "angle_mrad": angle_mrad,
        "torque_mv": torque_mv,
        "flags": flags,
        "seq": seq,
    }


# task slots, mirroring TaskId in obc/Inc/task_health.hpp. reserved slots are named because a
# report only carries the tasks that exist, so an id has to decode without one being present
TASK_NAMES = (
    "control",
    "sensors",
    "health",
    "uplink",
    "telemetry",
    "downlink",
    "idle",
)

TASK_STATES = ("run", "rdy", "blk", "susp", "del")

TASK_HEALTH_MAX = 7  # kTaskHealthMaxTasks in msg.hpp
TASK_HEALTH_LEN = 6 + 6 * TASK_HEALTH_MAX  # t_ms, count, flags, then one 6-byte entry per slot

# kTaskHealthFlagWatchdogFed in msg.hpp - clear means the watchdog was deliberately not serviced
TASK_FLAG_WATCHDOG_FED = 0x01

# a check-in age the obc could not report: never checked in, or the task does not check in at all
TASK_CHECKIN_NONE = 0xFFFF


def task_name(task_id: int) -> str:
    """Name for a task id, falling back to the raw id for one this build does not know."""
    if task_id < len(TASK_NAMES):
        return TASK_NAMES[task_id]
    return f"id{task_id}"


def task_state_name(state: int) -> str:
    """Short name for a FreeRTOS eTaskState value."""
    if state < len(TASK_STATES):
        return TASK_STATES[state]
    return f"s{state}"


def decode_task_health(payload: bytes) -> dict:
    """Unpack a task_health_t payload (msg.hpp) into a dict, trimmed to its valid entries.

    The struct is fixed-size and every slot is on the wire, so the whole thing is unpacked at once
    and the count decides how many entries mean anything. A short payload raises rather than
    decoding partially - a truncated report is not a report.
    """
    fields = struct.unpack("<IBB" + "BBHH" * TASK_HEALTH_MAX, payload)
    t_ms, count, flags = fields[0], fields[1], fields[2]
    tasks = []
    for i in range(min(count, TASK_HEALTH_MAX)):
        task_id, state, stack_free_words, checkin_age_ms = fields[3 + 4 * i : 7 + 4 * i]
        tasks.append(
            {
                "id": task_id,
                "name": task_name(task_id),
                "state": state,
                "stack_free_words": stack_free_words,
                "checkin_age_ms": checkin_age_ms,
            }
        )
    return {"t_ms": t_ms, "count": count, "flags": flags, "tasks": tasks}


def format_task_health(payload: bytes) -> str:
    """One line for a task_health_t: name, state, stack words still free, check-in age.

    A task with no check-in age (the kernel's idle task never checks in) simply has none printed -
    a placeholder there reads as a missing value rather than an inapplicable one.
    """
    d = decode_task_health(payload)
    parts = []
    for t in d["tasks"]:
        fields = [task_state_name(t["state"]), f"{t['stack_free_words']}w"]
        if t["checkin_age_ms"] != TASK_CHECKIN_NONE:
            fields.append(f"{t['checkin_age_ms']}ms")
        parts.append(f"{t['name']}: {' '.join(fields)}")
    # only ever said when it is false - a watchdog being serviced is the routine case, and the
    # line is already long. "WATCHDOG UNFED" means the spacecraft is about to reset itself
    wdg = "" if d["flags"] & TASK_FLAG_WATCHDOG_FED else "  WATCHDOG UNFED"
    return f"{'TASKS':<12} t={d['t_ms']} ms  " + " | ".join(parts) + wdg


def format_frame(msg_id: int, payload: bytes) -> str:
    """One-line summary of a decoded frame; the kind is left-justified so the fields line up.

    Kind naming: a subsystem that sends one message is named bare (IMU, POWER, CAMERA), and a
    suffix is added only where one subsystem sends more than one (WHEEL_CMD vs WHEEL_STATUS).
    Every millisecond stamp is `t=`, since they all come off the same millis() time base.
    """
    if msg_id == MSG_COMMAND and len(payload) == 4:
        cmd_id, arg, seq = struct.unpack("<BBH", payload)
        return (
            f"{'COMMAND':<12} cmd={command_name(cmd_id)}  arg={arg_label(cmd_id, arg)}  seq={seq}"
        )
    if msg_id == MSG_COMMAND_ACK and len(payload) == 5:
        d = decode_command_ack(payload)
        verdict = "accepted" if d["accepted"] else f"rejected (reason={d['reason']})"
        return f"{'COMMAND_ACK':<12} cmd={command_name(d['cmd_id'])}  seq={d['seq']}  {verdict}"
    if msg_id == MSG_HEARTBEAT and len(payload) == 17:
        d = decode_heartbeat(payload)
        # inhibited is shown only when it is non-empty: on a flight build it always is empty, and
        # a permanent "inhibited={}" would train the eye to skip the field that matters
        inhibited = f"  inhibited={fault_names(d['inhibited'])}" if d["inhibited"] else ""
        # the bus voltage is a vital sign on battery power, so it rides the heartbeat line -
        # suppressed while zero, which means no power sample has ever arrived
        bus = f"  bus={d['bus_mv'] / 1000:.2f}V" if d["bus_mv"] else ""
        return (
            f"{'HEARTBEAT':<12} t={d['uptime_ms']} ms  mode={d['mode']}  "
            f"faults={fault_names(d['faults'])}{inhibited}{bus}  seq={d['seq']}"
        )
    if msg_id == MSG_UART_STATUS and len(payload) == 16:
        overrun, framing, noise, dropped = struct.unpack("<IIII", payload)
        return (
            f"{'UART':<12} overrun={overrun}  framing={framing}  noise={noise}  dropped={dropped}"
        )
    if msg_id == MSG_IMU_DATA and len(payload) == 23:
        d = decode_imu_data(payload)
        return (
            f"{'IMU':<12} t={d['t_ms']} ms  accel={d['accel']}  "
            f"gyro={d['gyro']}  mag={d['mag']}  flags=0x{d['flags']:02X}"
        )
    if msg_id == MSG_POWER_DATA and len(payload) == 17:
        d = decode_power_data(payload)
        return (
            f"{'POWER':<12} t={d['t_ms']} ms  bus_mv={d['bus_mv']}  "
            f"current_ma={d['current_ma']}  power_mw={d['power_mw']}  flags=0x{d['flags']:02X}"
        )
    if msg_id == MSG_TEMP_DATA and len(payload) == 9:
        d = decode_temp_data(payload)
        return f"{'TEMP':<12} t={d['t_ms']} ms  temp_mc={d['temp_mc']}  flags=0x{d['flags']:02X}"
    if msg_id == MSG_PAYLOAD_DATA and len(payload) == 8 + PAYLOAD_CHUNK_BYTES:
        d = decode_payload_data(payload)
        return (
            f"{'PAYLOAD':<12} image={d['image_id']}  chunk={d['chunk']}/{d['chunks']}  "
            f"len={d['len']}"
        )
    if msg_id == MSG_ATTITUDE_STATUS and len(payload) == 13:
        d = decode_attitude_status(payload)
        # the error is what a pointing run is actually judged on, so it is stated rather than left
        # to be subtracted by eye - and wrapped, because 122.8 minus -136.0 is +258.7 only on a
        # number line. on a circle it is -101.3, and the bench showed the unwrapped version
        err = ((d["heading_deg"] - d["target_deg"] + 180.0) % 360.0) - 180.0
        marks = []
        if d["flags"] & ATTITUDE_FLAG_IN_BAND:
            marks.append("in band")
        if d["flags"] & ATTITUDE_FLAG_SATURATED:
            # the controller asked for more torque than the wheel has and was clamped at the
            # ceiling - routine during a large slew, chronic when the target is unreachable
            marks.append("torque clamped at limit")
        tail = ("  " + ", ".join(marks)) if marks else ""
        return (
            f"{'ATTITUDE':<12} hdg={d['heading_deg']:+.1f} deg  target={d['target_deg']:+.1f}  "
            f"err={err:+.1f}  rate={d['rate_dps']:+.1f} deg/s  "
            f"torque={d['torque_mnm']} mNm{tail}"
        )
    if msg_id == MSG_GROUND_STATUS and len(payload) == 18:
        d = decode_ground_status(payload)
        lora = "up" if d["flags"] & GROUND_FLAG_LORA_UP else "DOWN"
        nrf = "up" if d["flags"] & GROUND_FLAG_NRF24_UP else "DOWN"
        # channel occupancy, always shown: the useful reading is how it moves when the vehicle
        # starts transmitting, and a field that appears and disappears cannot be compared
        rf = f"  ch {d['busy_pct']}% busy"
        # the addresses that actually acked on the bus, named rather than counted - "oled x1" left
        # it ambiguous which panel was missing, and the whole question is whether 0x3D is there
        addrs = []
        if d["flags"] & GROUND_FLAG_PANEL1:
            addrs.append("3C")
        if d["flags"] & GROUND_FLAG_PANEL2:
            addrs.append("3D")
        panels = "+".join(addrs) if addrs else "none"
        return (
            f"{'GROUND':<12} t={d['t_ms']} ms  lora {lora} ({d['lora_frames']} frames)  "
            f"nrf24 {nrf} ({d['nrf24_packets']} pkt, {d['nrf24_frames']} frames){rf}  "
            f"oled {panels}"
        )
    if msg_id == MSG_DOWNLINK_STATUS and len(payload) == 16:
        d = decode_downlink_status(payload)
        radio = f"radio sent={d['radio_sent']} dropped={d['radio_dropped']}"
        # chunks==0 means nothing is in flight. drawing a bar for that is how a finished pass ends
        # up looking like a stalled one, sitting at 100% for as long as the vehicle stays in the
        # mode - the useful reading while idle is that the link is alive at all
        if d["chunks"] == 0:
            return f"{'DOWNLINK':<12} idle  {radio}"
        pct = d["chunk"] * 100 // d["chunks"]
        return (
            f"{'DOWNLINK':<12} image={d['image_id']}  {progress_bar(d['chunk'], d['chunks'])} "
            f"{d['chunk']}/{d['chunks']} ({pct}%)  {radio}"
        )
    if msg_id == MSG_CAMERA_STATUS and len(payload) == 9:
        d = decode_camera_data(payload)
        return (
            f"{'CAMERA':<12} t={d['t_ms']} ms  frame_bytes={d['frame_bytes']}  "
            f"flags=0x{d['flags']:02X}"
        )
    if msg_id == MSG_TASK_HEALTH and len(payload) == TASK_HEALTH_LEN:
        return format_task_health(payload)
    if msg_id in (MSG_LORA_STATUS, MSG_NRF24_STATUS) and len(payload) == 11:
        d = decode_radio_status(payload)
        kind = "LORA" if msg_id == MSG_LORA_STATUS else "NRF24"
        state = "up" if (d["flags"] & RADIO_FLAG_ANSWERING) else "NOT ANSWERING"
        return f"{kind:<12} t={d['t_ms']} ms  {state}  sent={d['sent']}  dropped={d['dropped']}"
    if msg_id == MSG_BOOT_INFO and len(payload) == 9:
        d = decode_boot_info(payload)
        return (
            f"{'BOOT':<12} t={d['t_ms']} ms  reset={reset_cause_name(d['reset_cause'])}  "
            f"clk={d['clk_hz']} Hz"
        )
    if msg_id == MSG_WHEEL_COMMAND and len(payload) == 4:
        torque_mv, seq = struct.unpack("<hH", payload)
        return f"{'WHEEL_CMD':<12} torque_mv={torque_mv}  seq={seq}"
    if msg_id == MSG_WHEEL_STATUS and len(payload) == 13:
        d = decode_wheel_status(payload)
        return (
            f"{'WHEEL_STATUS':<12} vel={d['velocity_mrad_s']} mrad/s  ang={d['angle_mrad']} mrad  "
            f"torque_mv={d['torque_mv']}  flags=0x{d['flags']:02X}  seq={d['seq']}"
        )
    return f"{'UNKNOWN':<12} id=0x{msg_id:02X}  len={len(payload)}  payload={payload.hex()}"
