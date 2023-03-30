#include "OneWireHub.h"
#include "OneWireItem.h"

#include "platform.h"

OneWireHub::OneWireHub(const uint8_t pin)
{
    _error         = Error::NO_ERROR;

    slave_count    = 0;
    slave_selected = nullptr;

#if OVERDRIVE_ENABLE
    od_mode = false;
#endif

    slave_list[0] = nullptr;

    // prepare pin
    pin_bitMask   = PIN_TO_BITMASK(pin);
    pin_baseReg   = PIN_TO_BASEREG(pin);
    pinMode(pin,
            INPUT); // first port-access should by done by this FN, does more than DIRECT_MODE_....
    DIRECT_WRITE_LOW(pin_baseReg, pin_bitMask);


    static_assert(VALUE_IPL, "Your architecture has not been calibrated yet, please run "
                             "examples/debug/calibrate_by_bus_timing and report instructions per "
                             "loop (IPL) to https://github.com/orgua/OneWireHub");
    static_assert(ONEWIRE_TIME_VALUE_MIN > 2,
                  "YOUR ARCHITECTURE IS TOO SLOW, THIS MAY RESULT IN "
                  "TIMING-PROBLEMS"); // it could work though, never tested
}


// attach a sensor to the hub
uint8_t OneWireHub::attach(OneWireItem &sensor)
{

    slave_list[0] = &sensor;
    slave_count++;
    return 0;
}


// just look through each bit of each ID and build a tree, so there are n=slaveCount decision-points
// trade-off: more online calculation, but @4Slave 16byte storage instead of 3*256 byte
uint8_t OneWireHub::getNrOfFirstBitSet(const mask_t mask) const
{
    mask_t _mask = mask;
    for (uint8_t i = 0; i < ONEWIRESLAVE_LIMIT; ++i)
    {
        if ((_mask & 1) != 0) return i;
        _mask >>= 1;
    }
    return 0;
}


bool OneWireHub::poll(void)
{
    _error = Error::NO_ERROR;

    while (true)
    {
        // this additional check prevents an infinite loop when calling this FN without sensors attached
        if (slave_count == 0) return true;

        // Once reset is done, go to next step
        if (checkReset()) return false;

        // Reset is complete, tell the master we are present
        if (showPresence()) return false;

        // Now that the master should know we are here, we will get a command from the master
        if (recvAndProcessCmd()) return false;

        // on total success we want to start again, because the next reset could only be ~125 us away
    }
}


bool OneWireHub::checkReset(
        void) // there is a specific high-time needed before a reset may occur -->  >120us
{
    static_assert(
            ONEWIRE_TIME_RESET_MIN[0] > (ONEWIRE_TIME_SLOT_MAX[0] + ONEWIRE_TIME_READ_MAX[0]),
            "Timings are wrong"); // last number should read: max(ONEWIRE_TIME_WRITE_ZERO,ONEWIRE_TIME_READ_MAX)
    static_assert(ONEWIRE_TIME_READ_MAX[0] > ONEWIRE_TIME_WRITE_ZERO[0],
                  "switch ONEWIRE_TIME_WRITE_ZERO with ONEWIRE_TIME_READ_MAX in checkReset(), "
                  "because it is bigger (worst case)");
    static_assert(ONEWIRE_TIME_RESET_MAX[0] > ONEWIRE_TIME_RESET_MIN[0], "Timings are wrong");
#if OVERDRIVE_ENABLE
    static_assert(ONEWIRE_TIME_RESET_MIN[1] > (ONEWIRE_TIME_SLOT_MAX[1] + ONEWIRE_TIME_READ_MAX[1]),
                  "Timings are wrong");
    static_assert(ONEWIRE_TIME_READ_MAX[1] > ONEWIRE_TIME_WRITE_ZERO[1],
                  "switch ONEWIRE_TIME_WRITE_ZERO with ONEWIRE_TIME_READ_MAX in checkReset(), "
                  "because it is bigger (worst case)");
    static_assert(ONEWIRE_TIME_RESET_MAX[0] > ONEWIRE_TIME_RESET_MIN[1], "Timings are wrong");
#endif

    DIRECT_MODE_INPUT(pin_baseReg, pin_bitMask);

    // is entered if there are two resets within a given time (timeslot-detection can issue this skip)
    if (_error == Error::RESET_IN_PROGRESS)
    {
        _error = Error::NO_ERROR;
        if (waitLoopsWhilePinIs(ONEWIRE_TIME_RESET_MIN[od_mode] - ONEWIRE_TIME_SLOT_MAX[od_mode] -
                                        ONEWIRE_TIME_READ_MAX[od_mode],
                                false) ==
            0) // last number should read: max(ONEWIRE_TIME_WRITE_ZERO,ONEWIRE_TIME_READ_MAX)
        {
#if OVERDRIVE_ENABLE
            const timeOW_t loops_remaining = waitLoopsWhilePinIs(
                    ONEWIRE_TIME_RESET_MAX[0],
                    false); // showPresence() wants to start at high, so wait for it
            if (od_mode &&
                ((ONEWIRE_TIME_RESET_MAX[0] - ONEWIRE_TIME_RESET_MIN[od_mode]) > loops_remaining))
            {
                od_mode = false; // normal reset detected, so leave OD-Mode
            };
#else
            waitLoopsWhilePinIs(ONEWIRE_TIME_RESET_MAX[0],
                                false); // showPresence() wants to start at high, so wait for it
#endif
            return false;
        }
    }

    if (!DIRECT_READ(pin_baseReg, pin_bitMask))
        return true; // just leave if pin is Low, don't bother to wait, TODO: really needed?

    // wait for the bus to become low (master-controlled), since we are polling we don't know for how long it was zero
    if (waitLoopsWhilePinIs(ONEWIRE_TIME_RESET_TIMEOUT, true) == 0)
    {
        //_error = Error::WAIT_RESET_TIMEOUT;
        return true;
    }

    const timeOW_t loops_remaining = waitLoopsWhilePinIs(ONEWIRE_TIME_RESET_MAX[0], false);

    // wait for bus-release by master
    if (loops_remaining == 0)
    {
        _error = Error::VERY_LONG_RESET;
        return true;
    }

#if OVERDRIVE_ENABLE
    if (od_mode && ((ONEWIRE_TIME_RESET_MAX[0] - ONEWIRE_TIME_RESET_MIN[0]) > loops_remaining))
    {
        od_mode = false; // normal reset detected, so leave OD-Mode
    };
#endif

    // If the master pulled low for to short this will trigger an error
    //if (loops_remaining > (ONEWIRE_TIME_RESET_MAX[0] - ONEWIRE_TIME_RESET_MIN[od_mode])) _error = Error::VERY_SHORT_RESET; // could be activated again, like the error above, errorhandling is mature enough now

    return (loops_remaining > (ONEWIRE_TIME_RESET_MAX[0] - ONEWIRE_TIME_RESET_MIN[od_mode]));
}


bool OneWireHub::showPresence(void)
{
    static_assert(ONEWIRE_TIME_PRESENCE_MAX[0] > ONEWIRE_TIME_PRESENCE_MIN[0], "Timings are wrong");
#if OVERDRIVE_ENABLE
    static_assert(ONEWIRE_TIME_PRESENCE_MAX[1] > ONEWIRE_TIME_PRESENCE_MIN[1], "Timings are wrong");
#endif

    // Master will delay it's "Presence" check (bus-read)  after the reset
    waitLoopsWhilePinIs(ONEWIRE_TIME_PRESENCE_TIMEOUT,
                        true); // no pinCheck demanded, but this additional check can cut waitTime

    if (USE_GPIO_DEBUG) DIRECT_WRITE_HIGH(debug_baseReg, debug_bitMask);

    // pull the bus low and hold it some time
    DIRECT_WRITE_LOW(pin_baseReg, pin_bitMask);
    DIRECT_MODE_OUTPUT(pin_baseReg, pin_bitMask); // drive output low

    wait(ONEWIRE_TIME_PRESENCE_MIN
                 [od_mode]); // stays till the end, because it drives the bus low itself

    DIRECT_MODE_INPUT(pin_baseReg, pin_bitMask); // allow it to float

    if (USE_GPIO_DEBUG) DIRECT_WRITE_LOW(debug_baseReg, debug_bitMask);

    // When the master or other slaves release the bus within a given time everything is fine
    if (waitLoopsWhilePinIs(
                (ONEWIRE_TIME_PRESENCE_MAX[od_mode] - ONEWIRE_TIME_PRESENCE_MIN[od_mode]), false) ==
        0)
    {
        _error = Error::PRESENCE_LOW_ON_LINE;
        return true;
    }

    return false;
}


bool OneWireHub::recvAndProcessCmd(void)
{
    slave_selected = slave_list[0];

    // If the only slave is not multidrop compatible, pass all data handling to the slave
    if (slave_count == 1u)
    {
        // TODO: this might be expensive for weak uC and OW in Overdrive and only one device emulated
        //  -> look into optimizations (i.e. preselect when only one device present?)

        if (slave_selected->skip_multidrop)
        {
            slave_selected->duty(this);
            return (_error != Error::NO_ERROR);
        }
    }

    uint8_t address[8], cmd;
    bool    flag = false;

    recv(&cmd);

    if (_error == Error::RESET_IN_PROGRESS)
        return false; // stay in poll()-loop and trigger another datastream-detection
    if (_error != Error::NO_ERROR) return true;

    switch (cmd)
    {
        case 0xCC: // SKIP ROM

            // NOTE: If more than one slave is present on the bus,
            // and a read command is issued following the Skip ROM command,
            // data collision will occur on the bus as multiple slaves transmit simultaneously
            if ((slave_selected == nullptr) && (slave_count == 1))
            {
                slave_selected = slave_list[0];
            }
            if (slave_selected != nullptr)
            {
                if (USE_GPIO_DEBUG) DIRECT_WRITE_HIGH(debug_baseReg, debug_bitMask);
                slave_selected->duty(this);
            }
            break;

        default: // Unknown command

            _error     = Error::INCORRECT_ONEWIRE_CMD;
            _error_cmd = cmd;
    }

    if (_error == Error::RESET_IN_PROGRESS) return false;

    return (_error != Error::NO_ERROR);
}


// info: check for errors after calling and break/return if possible, returns true if error is detected
// NOTE: if called separately you need to handle interrupts, should be disabled during this FN
bool OneWireHub::sendBit(const bool value)
{
    const bool writeZero = !value;

    // Wait for bus to rise HIGH, signaling end of last timeslot
    timeOW_t   retries   = ONEWIRE_TIME_SLOT_MAX[od_mode];
    while ((DIRECT_READ(pin_baseReg, pin_bitMask) == 0) && (--retries != 0))
        ;
    if (retries == 0)
    {
        _error = Error::RESET_IN_PROGRESS;
        return true;
    }

    // Wait for bus to fall LOW, start of new timeslot
    retries = ONEWIRE_TIME_MSG_HIGH_TIMEOUT;
    while ((DIRECT_READ(pin_baseReg, pin_bitMask) != 0) && (--retries != 0))
        ;
    if (retries == 0)
    {
        _error = Error::AWAIT_TIMESLOT_TIMEOUT_HIGH;
        return true;
    }

    // first difference to inner-loop of read()
    if (writeZero)
    {
        DIRECT_MODE_OUTPUT(pin_baseReg, pin_bitMask);
        retries = ONEWIRE_TIME_WRITE_ZERO[od_mode];
    }
    else { retries = ONEWIRE_TIME_READ_MAX[od_mode]; }

    while ((DIRECT_READ(pin_baseReg, pin_bitMask) == 0) && (--retries != 0))
        ; // TODO: we should check for (!retries) because there could be a reset in progress...
    DIRECT_MODE_INPUT(pin_baseReg, pin_bitMask);

    return false;
}


// should be the preferred function for writes, returns true if error occurred
bool OneWireHub::send(const uint8_t address[], const uint8_t data_length)
{
    noInterrupts(); // will be enabled at the end of function
    DIRECT_WRITE_LOW(pin_baseReg, pin_bitMask);
    DIRECT_MODE_INPUT(pin_baseReg, pin_bitMask);
    uint8_t bytes_sent = 0;

    for (; bytes_sent < data_length; ++bytes_sent) // loop for sending bytes
    {
        const uint8_t dataByte = address[bytes_sent];

        for (uint8_t bitMask = 0x01; bitMask != 0; bitMask <<= 1) // loop for sending bits
        {
            if (sendBit(static_cast<bool>(bitMask & dataByte)))
            {
                if ((bitMask == 0x01) && (_error == Error::AWAIT_TIMESLOT_TIMEOUT_HIGH))
                    _error = Error::FIRST_BIT_OF_BYTE_TIMEOUT;
                interrupts();
                return true;
            }
        }
        if (USE_GPIO_DEBUG)
        {
            DIRECT_WRITE_HIGH(debug_baseReg, debug_bitMask);
            DIRECT_WRITE_LOW(debug_baseReg, debug_bitMask);
        }
    }
    interrupts();
    return (bytes_sent != data_length);
}

bool OneWireHub::send(const uint8_t address[], const uint8_t data_length, uint16_t &crc16)
{
    noInterrupts(); // will be enabled at the end of function
    DIRECT_WRITE_LOW(pin_baseReg, pin_bitMask);
    DIRECT_MODE_INPUT(pin_baseReg, pin_bitMask);
    uint8_t bytes_sent = 0;

    for (; bytes_sent < data_length; ++bytes_sent) // loop for sending bytes
    {
        uint8_t dataByte = address[bytes_sent];

        for (uint8_t counter = 0; counter < 8; ++counter) // loop for sending bits
        {
            if (sendBit(static_cast<bool>(0x01 & dataByte)))
            {
                if ((counter == 0) && (_error == Error::AWAIT_TIMESLOT_TIMEOUT_HIGH))
                    _error = Error::FIRST_BIT_OF_BYTE_TIMEOUT;
                interrupts();
                return true;
            }

            const uint8_t mix = ((uint8_t) crc16 ^ dataByte) & static_cast<uint8_t>(0x01);
            crc16 >>= 1;
            if (mix != 0) crc16 ^= static_cast<uint16_t>(0xA001);
            dataByte >>= 1;
        }
        if (USE_GPIO_DEBUG)
        {
            DIRECT_WRITE_HIGH(debug_baseReg, debug_bitMask);
            DIRECT_WRITE_LOW(debug_baseReg, debug_bitMask);
        }
    }
    interrupts();
    return (bytes_sent != data_length);
}

bool OneWireHub::send(const uint8_t dataByte) { return send(&dataByte, 1); }

// NOTE: if called separately you need to handle interrupts, should be disabled during this FN
bool OneWireHub::recvBit(void)
{
    // Wait for bus to rise HIGH, signaling end of last timeslot
    timeOW_t retries = ONEWIRE_TIME_SLOT_MAX[od_mode];
    while ((DIRECT_READ(pin_baseReg, pin_bitMask) == 0) && (--retries != 0))
        ;
    if (retries == 0)
    {
        _error = Error::RESET_IN_PROGRESS;
        return true;
    }

    // Wait for bus to fall LOW, start of new timeslot
    retries = ONEWIRE_TIME_MSG_HIGH_TIMEOUT;
    while ((DIRECT_READ(pin_baseReg, pin_bitMask) != 0) && (--retries != 0))
        ;
    if (retries == 0)
    {
        _error = Error::AWAIT_TIMESLOT_TIMEOUT_HIGH;
        return true;
    }

    // wait a specific time to do a read (data is valid by then), // first difference to inner-loop of write()
    retries = ONEWIRE_TIME_READ_MIN[od_mode];
    while ((DIRECT_READ(pin_baseReg, pin_bitMask) == 0) && (--retries != 0))
        ;

    return (retries > 0);
}


bool OneWireHub::recv(uint8_t address[], const uint8_t data_length)
{
    noInterrupts(); // will be enabled at the end of function
    DIRECT_WRITE_LOW(pin_baseReg, pin_bitMask);
    DIRECT_MODE_INPUT(pin_baseReg, pin_bitMask);

    uint8_t bytes_received = 0;
    for (; bytes_received < data_length; ++bytes_received)
    {
        uint8_t value = 0;

        for (uint8_t bitMask = 0x01; bitMask != 0; bitMask <<= 1)
        {
            if (recvBit()) value |= bitMask;
            if (_error != Error::NO_ERROR)
            {
                if ((bitMask == 0x01) && (_error == Error::AWAIT_TIMESLOT_TIMEOUT_HIGH))
                    _error = Error::FIRST_BIT_OF_BYTE_TIMEOUT;
                interrupts();
                return true;
            }
        }

        address[bytes_received] = value;

        if (USE_GPIO_DEBUG)
        {
            DIRECT_WRITE_HIGH(debug_baseReg, debug_bitMask);
            DIRECT_WRITE_LOW(debug_baseReg, debug_bitMask);
        }
    }

    interrupts();
    return (bytes_received != data_length);
}


// should be the preferred function for reads, returns true if error occurred
bool OneWireHub::recv(uint8_t address[], const uint8_t data_length, uint16_t &crc16)
{
    noInterrupts(); // will be enabled at the end of function
    DIRECT_WRITE_LOW(pin_baseReg, pin_bitMask);
    DIRECT_MODE_INPUT(pin_baseReg, pin_bitMask);

    uint8_t bytes_received = 0;
    for (; bytes_received < data_length; ++bytes_received)
    {
        uint8_t value = 0;
        uint8_t mix   = 0;
        for (uint8_t bitMask = 0x01; bitMask != 0; bitMask <<= 1)
        {
            if (recvBit())
            {
                value |= bitMask;
                mix = 1;
            }
            else mix = 0;

            if (_error != Error::NO_ERROR)
            {
                if ((bitMask == 0x01) && (_error == Error::AWAIT_TIMESLOT_TIMEOUT_HIGH))
                    _error = Error::FIRST_BIT_OF_BYTE_TIMEOUT;
                interrupts();
                return true;
            }

            mix ^= static_cast<uint8_t>(crc16) & static_cast<uint8_t>(0x01);
            crc16 >>= 1;
            if (mix != 0) crc16 ^= static_cast<uint16_t>(0xA001);
        }

        address[bytes_received] = value;
        if (USE_GPIO_DEBUG)
        {
            DIRECT_WRITE_HIGH(debug_baseReg, debug_bitMask);
            DIRECT_WRITE_LOW(debug_baseReg, debug_bitMask);
        }
    }

    interrupts();
    return (bytes_received != data_length);
}


void OneWireHub::wait(const uint16_t timeout_us) const
{
    timeOW_t loops = timeUsToLoops(timeout_us);
    bool     state = false;
    while (loops != 0)
    {
        loops = waitLoopsWhilePinIs(loops, state);
        state = !state;
    }
}

void OneWireHub::wait(const timeOW_t loops_wait) const
{
    timeOW_t loops = loops_wait;
    bool     state = false;
    while (loops != 0)
    {
        loops = waitLoopsWhilePinIs(loops, state);
        state = !state;
    }
}


// returns false if pins stays in the wanted state all the time
timeOW_t OneWireHub::waitLoopsWhilePinIs(volatile timeOW_t retries, const bool pin_value) const
{
    if (retries == 0) return 0;
    while ((DIRECT_READ(pin_baseReg, pin_bitMask) == pin_value) && (--retries != 0))
        ;
    return retries;
}

void OneWireHub::waitLoops1ms(void)
{
    if (USE_GPIO_DEBUG)
    {
        constexpr timeOW_t loops_1ms  = 1000_us;
        timeOW_t           loops_left = 1;
        while (loops_left != 0)
        {
            waitLoopsWhilePinIs(loops_1ms, false);
            DIRECT_MODE_INPUT(pin_baseReg, pin_bitMask);
            DIRECT_WRITE_HIGH(debug_baseReg, debug_bitMask);
            loops_left = waitLoopsWhilePinIs(loops_1ms, true);
            DIRECT_WRITE_LOW(debug_baseReg, debug_bitMask);
        }
    }
}

// this calibration calibrates timing with the longest low-state on the OW-Bus.
// first it measures some resets with the millis()-fn to get real timing.
// after that it measures with a waitLoops()-FN to determine the instructions-per-loop-value for the used architecture
timeOW_t OneWireHub::waitLoopsCalibrate(void)
{
    const timeOW_t wait_loops{
            1000000 *
            microsecondsToClockCycles(
                    1)}; // loops before canceling a pin-change-wait, 1s, TODO: change back to constexpr if possible (ardu due / zero are blocking)
    constexpr uint32_t TIME_RESET_MIN_US = 430;

    timeOW_t           time_for_reset    = 0;
    timeOW_t           repetitions       = 10;

    DIRECT_MODE_INPUT(pin_baseReg, pin_bitMask);

    // repetitions the longest low-states on the bus with millis(), assume it is a OW-reset
    while (repetitions-- != 0)
    {
#if defined(ARDUINO_ARCH_ESP8266)
        ESP.wdtFeed();
#endif
        uint32_t time_needed = 0;

        // try to catch a OW-reset each time
        while (time_needed < TIME_RESET_MIN_US)
        {
            if (waitLoopsWhilePinIs(wait_loops, true) == 0) continue;
            const uint32_t time_start = micros();
            waitLoopsWhilePinIs(TIMEOW_MAX, false);
            const uint32_t time_stop = micros();
            time_needed              = time_stop - time_start;
        }

        if (time_needed > time_for_reset) time_for_reset = time_needed;
    }

    timeOW_t loops_for_reset = 0;
    repetitions              = 0;

    noInterrupts();
    while (repetitions++ < REPETITIONS)
    {
#if defined(ARDUINO_ARCH_ESP8266)
        ESP.wdtFeed();
#endif
        if (waitLoopsWhilePinIs(wait_loops, true) == 0) continue;
        const timeOW_t loops_left   = waitLoopsWhilePinIs(TIMEOW_MAX, false);
        const timeOW_t loops_needed = TIMEOW_MAX - loops_left;
        if (loops_needed > loops_for_reset) loops_for_reset = loops_needed;
    }
    interrupts();

    waitLoops1ms();

    const timeOW_t value_ipl = (time_for_reset * microsecondsToClockCycles(1)) / loops_for_reset;

    return value_ipl;
}


void OneWireHub::waitLoopsDebug(void) const
{
    //
}

void OneWireHub::printError(void) const
{
    //
}

Error OneWireHub::getError(void) const { return (_error); }

bool  OneWireHub::hasError(void) const { return (_error != Error::NO_ERROR); }

void  OneWireHub::raiseSlaveError(const uint8_t cmd)
{
    _error     = Error::INCORRECT_SLAVE_USAGE;
    _error_cmd = cmd;
}

Error OneWireHub::clearError(void) // and return it if needed
{
    const Error _tmp = _error;
    _error           = Error::NO_ERROR;
    return _tmp;
}
