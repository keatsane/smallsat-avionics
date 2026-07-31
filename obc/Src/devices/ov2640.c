/**
 * @file   ov2640.c
 * @brief  arducam mini 2mp driver - ov2640 sensor over sccb, frame fifo over spi
 *
 * the module is two chips behind one connector. the ov2640 image sensor is configured over sccb
 * (i2c1, addr 0x30) and never talks to the obc again; the arduchip in front of it owns the frame
 * fifo and is driven over spi3. capture is therefore fire-and-forget - the arduchip fills the
 * fifo at sensor rate and the obc drains it whenever it likes, which is the whole reason this
 * part was chosen for a payload
 */

#include "devices/ov2640.h"

#include "devices/ov2640_regs.h"
#include "drivers/i2c.h"
#include "drivers/spi.h"
#include "drivers/systick.h"

#define OV2640_SCCB_ADDR 0x30U  // the sensor's sccb address on i2c1

// ---- sccb (sensor) registers ----
#define SENSOR_BANK  0xFFU  // 0x00 selects the dsp bank, 0x01 the sensor bank
#define BANK_DSP     0x00U
#define BANK_SENSOR  0x01U
#define SENSOR_PIDH  0x0AU  // reads 0x26 on an ov2640
#define SENSOR_PIDL  0x0BU  // 0x41 or 0x42 depending on die revision
#define SENSOR_COM7  0x12U  // bit 7 resets the sensor
#define SENSOR_COM10 0x15U  // vsync/href polarity - the arduchip latches frames on these

// ---- arduchip (fifo) registers, from arducam's ArduCAM.h ----
#define ARDUCHIP_TEST1  0x00U
#define ARDUCHIP_FIFO   0x04U
#define BURST_FIFO_READ 0x3CU
#define ARDUCHIP_TRIG   0x41U
#define FIFO_SIZE1      0x42U
#define FIFO_SIZE2      0x43U
#define FIFO_SIZE3      0x44U

#define FIFO_CLEAR_MASK     0x01U
#define FIFO_START_MASK     0x02U
#define FIFO_RDPTR_RST_MASK 0x10U
#define FIFO_WRPTR_RST_MASK 0x20U
#define CAP_DONE_MASK       0x08U

#define TEST_PATTERN 0x55U  // arbitrary - the round-trip is the point, not the value

// a uxga jpeg takes well under this even on a slow sensor clock; past it the arduchip is stuck
#define CAPTURE_TIMEOUT_MS 1000U

// the fifo is 8 mbit, so a size read far above that is a mute or floating bus rather than a frame
#define FIFO_MAX_BYTES 0x100000U

static ov2640_state_t state = OV2640_IDLE;
static uint32_t capture_start_ms = 0U;
static uint32_t frame_bytes = 0U;  // size latched when the capture completed
static uint32_t frame_read = 0U;   // how much of it has been handed out
static bool burst_open = false;    // a burst read is mid-flight, cs still asserted
static bool configured = false;    // init got all the way through

// the camera is reached by two tasks - the control task polls its health, the downlink task
// drains its fifo - and it shares spi3 with both radios. one lock covers both problems: the spi3
// bus lock is taken across every public call here, so it serialises the driver's state machine
// and the bus together. nothing else on spi3 can be mid-transaction while the camera is.
//
// this used to be the camera's own mutex, which was enough while the camera was alone on the
// bus and is not enough now that the radios have drivers coming
static bool cam_lock(void) { return spi_bus_lock(spi_camera); }

static void cam_unlock(bool locked) { spi_bus_unlock(spi_camera, locked); }

// ---- arduchip helpers - bit 7 of the address selects write ----

static uint8_t chip_read(uint8_t reg) {
    spi_select(spi_camera);
    spi_transfer_byte(spi_camera, reg & 0x7FU);
    uint8_t v = spi_transfer_byte(spi_camera, 0x00U);
    spi_deselect(spi_camera);
    return v;
}

static void chip_write(uint8_t reg, uint8_t val) {
    spi_select(spi_camera);
    spi_transfer_byte(spi_camera, reg | 0x80U);
    spi_transfer_byte(spi_camera, val);
    spi_deselect(spi_camera);
}

// does the arduchip still answer - the test register is scratch, so this is free to call
static bool chip_alive(void) {
    chip_write(ARDUCHIP_TEST1, TEST_PATTERN);
    return chip_read(ARDUCHIP_TEST1) == TEST_PATTERN;
}

// ---- sccb helpers ----

static bool sccb_write(uint8_t reg, uint8_t val) {
    return i2c_write_regs(i2c_sensors, OV2640_SCCB_ADDR, reg, &val, 1U) == I2C_OK;
}

static bool sccb_read(uint8_t reg, uint8_t* val) {
    return i2c_read_regs(i2c_sensors, OV2640_SCCB_ADDR, reg, val, 1U) == I2C_OK;
}

// walk a vendored table until its {0xFF, 0xFF} terminator. 0xFF is also the bank-select register,
// so the terminator is only a terminator when the value is 0xFF too - an actual bank select is
// {0xFF, 0x00} or {0xFF, 0x01} and passes straight through
static bool sccb_write_table(const ov2640_reg_t* table) {
    for (const ov2640_reg_t* r = table; !(r->reg == 0xFFU && r->val == 0xFFU); r++) {
        if (!sccb_write(r->reg, r->val)) {
            return false;
        }
    }
    return true;
}

// ---- fifo ----

// upstream drives the fifo with this one write and never touches the pointer-reset bits in the
// capture path - it is both their flush_fifo() and their clear_fifo_flag(), which is why the
// capture sequence below issues it twice
static void fifo_flush(void) { chip_write(ARDUCHIP_FIFO, FIFO_CLEAR_MASK); }

static uint32_t fifo_length(void) {
    const uint32_t b1 = chip_read(FIFO_SIZE1);
    const uint32_t b2 = chip_read(FIFO_SIZE2);
    const uint32_t b3 = chip_read(FIFO_SIZE3) & 0x7FU;  // only 7 bits of the top byte are length
    return (b3 << 16) | (b2 << 8) | b1;
}

// find where the jpeg actually ends, then rewind so it can be read from the start.
//
// the arduchip's length register is not the image size - measured on the bench, two captures of
// different scenes both reported 7688 while the jpeg inside them differed by ~100 bytes. the
// surplus is padding past the end-of-image marker, which is why arducam's own example scans for
// FF D9 rather than trusting the length. downlink is the consumer that cares: sending the fifo
// length means spending part of every contact pass transmitting zeros.
//
// costs one extra pass over the frame, once per capture - a few ms inside a 100 ms cycle
static uint32_t scan_jpeg_length(uint32_t fifo_bytes) {
    spi_select(spi_camera);
    spi_transfer_byte(spi_camera, BURST_FIFO_READ);

    uint32_t end = 0U;  // bytes up to and including the FF D9 pair
    uint8_t prev = 0U;
    for (uint32_t i = 0U; i < fifo_bytes; i++) {
        const uint8_t b = spi_transfer_byte(spi_camera, 0x00U);
        if (prev == 0xFFU && b == 0xD9U) {
            end = i + 1U;
            break;  // first marker wins - anything after it is padding
        }
        prev = b;
    }
    spi_deselect(spi_camera);

    // rewind the read pointer so the caller's first chunk starts at the SOI again
    chip_write(ARDUCHIP_FIFO, FIFO_RDPTR_RST_MASK);
    return end;
}

// close an open burst so the next chip_read/write starts from a known state
static void burst_close(void) {
    if (burst_open) {
        spi_deselect(spi_camera);
        burst_open = false;
    }
}

// ---- public ----

bool ov2640_init(void) {
    state = OV2640_IDLE;
    frame_bytes = 0U;
    frame_read = 0U;
    burst_open = false;
    configured = false;

    spi_camera_init();

    // spi first - it is the cheaper of the two links to disprove, and everything else needs it
    if (!chip_alive()) {
        return false;
    }

    // sensor id over sccb. the low byte is 0x41 or 0x42 across die revisions, so only the high
    // byte is a hard gate
    if (!sccb_write(SENSOR_BANK, BANK_SENSOR)) {
        return false;
    }
    uint8_t pidh = 0U;
    if (!sccb_read(SENSOR_PIDH, &pidh) || pidh != 0x26U) {
        return false;
    }

    // refuse to claim the camera is up on the stub tables - a sensor left in its power-on state
    // returns something over spi, and calling that a working payload is the exact lie the
    // honest-status rule exists to stop
    if (!ov2640_tables_vendored) {
        return false;
    }

    // sensor reset, then the vendored bring-up. the reset needs a moment before it will accept
    // register writes again
    if (!sccb_write(SENSOR_BANK, BANK_SENSOR) || !sccb_write(SENSOR_COM7, 0x80U)) {
        return false;
    }
    delay_ms(100U);

    if (!sccb_write_table(ov2640_jpeg_init) || !sccb_write_table(ov2640_yuv422) ||
        !sccb_write_table(ov2640_jpeg_enable)) {
        return false;
    }

    // upstream sits these two writes between the jpeg tables and the size table. 0x15 is COM10 in
    // the sensor bank, which governs the vsync/href polarity the arduchip latches frames on - skip
    // it and the tables all write cleanly but no frame is ever delivered
    if (!sccb_write(SENSOR_BANK, BANK_SENSOR) || !sccb_write(SENSOR_COM10, 0x00U)) {
        return false;
    }

    if (!sccb_write_table(ov2640_320x240)) {
        return false;
    }

    fifo_flush();  // upstream clears the done flag once here before the first capture
    configured = true;
    return true;
}

static bool capture_start_locked(void) {
    if (!configured || state == OV2640_CAPTURING) {
        return false;
    }

    // upstream's exact order: flush, clear the done flag, then start. the two identical writes
    // look redundant and are not - the second is what guarantees a stale done flag from the last
    // capture cannot be read as this one finishing instantly
    burst_close();
    fifo_flush();
    fifo_flush();
    chip_write(ARDUCHIP_FIFO, FIFO_START_MASK);

    state = OV2640_CAPTURING;
    capture_start_ms = millis();
    frame_bytes = 0U;
    frame_read = 0U;
    return true;
}

static ov2640_state_t poll_locked(uint32_t t_ms) {
    if (state != OV2640_CAPTURING) {
        return state;
    }

    if ((chip_read(ARDUCHIP_TRIG) & CAP_DONE_MASK) != 0U) {
        const uint32_t fifo_bytes = fifo_length();
        // a zero-length or absurd frame means the arduchip flagged done without a real capture,
        // which is a failure rather than an empty success
        if (fifo_bytes == 0U || fifo_bytes > FIFO_MAX_BYTES) {
            state = OV2640_FAILED;
            return state;
        }

        frame_bytes = scan_jpeg_length(fifo_bytes);
        state = (frame_bytes > 0U) ? OV2640_READY : OV2640_FAILED;
        return state;
    }

    if ((t_ms - capture_start_ms) > CAPTURE_TIMEOUT_MS) {
        state = OV2640_FAILED;
    }
    return state;
}

static ov2640_sample_t read_locked(void) {
    ov2640_sample_t s;

    // a burst read leaves cs asserted, and the health check would corrupt it
    burst_close();

    s.t_ms = millis();
    s.state = state;
    s.frame_bytes = (state == OV2640_READY) ? (frame_bytes - frame_read) : 0U;
    s.valid = chip_alive();
    s.frame_ready = (state == OV2640_READY);
    return s;
}

static uint32_t frame_bytes_locked(void) {
    return (state == OV2640_READY) ? (frame_bytes - frame_read) : 0U;
}

bool ov2640_frame_ready(void) { return state == OV2640_READY; }

static size_t read_chunk_locked(uint8_t* buf, size_t n) {
    if (state != OV2640_READY || buf == NULL || n == 0U) {
        return 0U;
    }

    const uint32_t left = frame_bytes - frame_read;
    if (left == 0U) {
        return 0U;
    }
    if (n > left) {
        n = left;
    }

    // one burst spans as many calls as it takes to drain the frame: the command byte goes out
    // once, then every subsequent clock is frame data. no dummy byte follows the command on this
    // arduchip - measured on the bench, where discarding one turned the frame's opening FF D8 FF
    // into D8 FF E1
    if (!burst_open) {
        spi_select(spi_camera);
        spi_transfer_byte(spi_camera, BURST_FIFO_READ);
        burst_open = true;
    }

    for (size_t i = 0U; i < n; i++) {
        buf[i] = spi_transfer_byte(spi_camera, 0x00U);
    }
    frame_read += n;

    // close it every call, not just at the end of the frame. the burst used to span as many
    // calls as the frame took, which saved re-sending one command byte per chunk - but it leaves
    // chip select asserted between calls, and the moment a radio shares spi3 that means the
    // camera is clocked by traffic meant for something else. the arduchip's read pointer
    // survives the deselect (it takes an explicit FIFO_RDPTR_RST to rewind, which is what
    // scan_jpeg_length uses), so re-issuing the command resumes where this left off.
    //
    // costs one byte per 56-byte chunk, under 2%, against a whole class of bus corruption
    burst_close();

    if (frame_read >= frame_bytes) {
        state = OV2640_IDLE;
        frame_bytes = 0U;
        frame_read = 0U;
    }
    return n;
}

static uint8_t chip_reg_locked(uint8_t reg) {
    burst_close();  // a raw read mid-burst would corrupt the frame
    return chip_read(reg);
}

static void discard_locked(void) {
    burst_close();
    fifo_flush();
    state = OV2640_IDLE;
    frame_bytes = 0U;
    frame_read = 0U;
}

// ---- locked wrappers ----
//
// every entry point that touches the bus or the state machine goes through the mutex. the bodies
// above are the originals, renamed - keeping the wrappers separate means the early returns inside
// them did not have to grow an unlock on every path

bool ov2640_capture_start(void) {
    const bool l = cam_lock();
    const bool r = capture_start_locked();
    cam_unlock(l);
    return r;
}

ov2640_state_t ov2640_poll(uint32_t t_ms) {
    const bool l = cam_lock();
    const ov2640_state_t r = poll_locked(t_ms);
    cam_unlock(l);
    return r;
}

ov2640_sample_t ov2640_read(void) {
    const bool l = cam_lock();
    const ov2640_sample_t r = read_locked();
    cam_unlock(l);
    return r;
}

uint32_t ov2640_frame_bytes(void) {
    const bool l = cam_lock();
    const uint32_t r = frame_bytes_locked();
    cam_unlock(l);
    return r;
}

size_t ov2640_read_chunk(uint8_t* buf, size_t n) {
    const bool l = cam_lock();
    const size_t r = read_chunk_locked(buf, n);
    cam_unlock(l);
    return r;
}

uint8_t ov2640_chip_reg(uint8_t reg) {
    const bool l = cam_lock();
    const uint8_t r = chip_reg_locked(reg);
    cam_unlock(l);
    return r;
}

void ov2640_discard(void) {
    const bool l = cam_lock();
    discard_locked();
    cam_unlock(l);
}
