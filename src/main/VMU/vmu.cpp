#ifndef ENABLE_UNIT_TEST

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"

#include <cstdio>

#include "configuration.h"

#include "DreamcastMainNode.hpp"
#include "DreamcastNodeData.hpp"
#include "PlayerData.hpp"
#include "ScreenData.hpp"
#include "PrioritizedTxScheduler.hpp"

#include "SerialStreamParser.hpp"
#include "MaplePassthroughTtyCommandHandler.hpp"

#include "CriticalSectionMutex.hpp"
#include "Mutex.hpp"
#include "Clock.hpp"

#include "hal/MapleBus/MapleBusInterface.hpp"
#include "hal/Usb/usb_interface.hpp"

#include <map>
#include <memory>

// Pinout of the VMU connector as wired to the pico for a direct connection.
//
// 1 - 3.3V
// 2 - 5.5V
// 3 -
// 4 -
// 5 -
// 6 -
// 7 -
// 8 -
// 9 -
// 10 -
// 11 -
// 12 -
// 13 -
// 14 -

//! GPIO which is driven high to hold the VMU in maple mode
static const uint32_t VMU_MAPLE_MODE_PIN = 22;
//! GPIO which enables the 5V rail to the VMU
static const uint32_t VMU_5V_ENABLE_PIN = 14;
//! GPIO which enables the 3.3V rail to the VMU
static const uint32_t VMU_3V3_ENABLE_PIN = 15;
//! Maple bus pin A which the VMU's response is sensed on (pin B is the very next GPIO)
static const uint32_t VMU_BUS_RX_PIN_A = 18;
//! Maple bus pin A which this device drives (pin B is the very next GPIO)
static const uint32_t VMU_BUS_TX_PIN_A = 20;
//! Address of this host on the maple bus
static const uint8_t VMU_MAPLE_HOST_ADDR = 0x00;

static Clock gClock;
static std::map<uint8_t, DreamcastNodeData> gDcNodes;

//! Brings up the VMU's supply rails and holds it in maple mode.
//! @note The VMU is driven directly here rather than being powered through a controller, so the
//!       rails have to be raised and allowed to settle before any bus traffic is attempted.
static void vmu_power_up()
{
    // Wait for steady state
    sleep_ms(100);

    // Hold the VMU in maple mode rather than letting it boot its own UI
    gpio_init(VMU_MAPLE_MODE_PIN);
    gpio_put(VMU_MAPLE_MODE_PIN, true);
    gpio_set_dir(VMU_MAPLE_MODE_PIN, true);

    gpio_init(VMU_5V_ENABLE_PIN);
    gpio_put(VMU_5V_ENABLE_PIN, true);
    gpio_set_dir(VMU_5V_ENABLE_PIN, true);

    gpio_init(VMU_3V3_ENABLE_PIN);
    gpio_put(VMU_3V3_ENABLE_PIN, true);
    gpio_set_dir(VMU_3V3_ENABLE_PIN, true);

    // Give the VMU time to come up before talking to it
    sleep_ms(1000);
}

//! Builds the single node which talks to the directly connected VMU
static void setup_dreamcast_nodes()
{
    static CriticalSectionMutex screenMutex;
    static Mutex schedulerMutex;

    DreamcastControllerObserver** observers = get_usb_controller_observers();

    DreamcastNodeData node;

    node.playerDef = std::make_shared<PlayerDefinition>(PlayerDefinition{
        .index = 0,
        .autoDetectOnly = false,
        .detectionMode = DppSettings::PlayerDetectionMode::kEnable,
        .gpioA = VMU_BUS_TX_PIN_A,
        .gpioDir = 0,
        .dirOutHigh = DIR_OUT_HIGH,
        .mapleHostAddr = VMU_MAPLE_HOST_ADDR
    });

    node.playerData = std::make_shared<PlayerData>(PlayerData{
        .playerIndex = 0,
        .gamepad = *(observers[0]),
        .screenData = std::make_shared<ScreenData>(screenMutex, 0),
        .clock = gClock,
        .fileSystem = usb_msc_get_file_system()
    });

    node.scheduler = std::make_shared<PrioritizedTxScheduler>(schedulerMutex, VMU_MAPLE_HOST_ADDR);

    node.mainNode = std::make_shared<DreamcastMainNode>(
        // The VMU is sensed on one pin pair and driven on another, so the bus is given separate
        // receive and transmit pairs rather than the usual single pair.
        create_maple_bus(VMU_BUS_RX_PIN_A, VMU_BUS_TX_PIN_A, -1, DIR_OUT_HIGH),
        node.playerData,
        node.scheduler,
        false, // detectionOnly
        true   // directSubPeripheral - the VMU answers on its own address, not behind a controller
    );

    gDcNodes.insert_or_assign(0, std::move(node));
}

// Second Core Process
// The second core is in charge of handling communication with Dreamcast peripherals
void core1()
{
    // Initialize CDC to Maple Bus interfaces
    static Mutex ttyParserMutex;
    SerialStreamParser ttyParser(ttyParserMutex, 'h');
    usb_cdc_set_parser(&ttyParser);
    ttyParser.addTtyCommandHandler(std::make_shared<MaplePassthroughTtyCommandHandler>(gDcNodes));

    while(true)
    {
        for (auto& node : gDcNodes)
        {
            node.second.mainNode->task(time_us_64());
        }

        // Process any waiting commands in the TTY parser
        ttyParser.process();
    }
}

// First Core Process
// The first core is in charge of initialization and USB communication
int main()
{
    set_sys_clock_khz(CPU_FREQ_KHZ, true);

    stdio_uart_init();
    printf("\n\n");

    vmu_power_up();

    setup_dreamcast_nodes();

    static Mutex fileMutex;
    static Mutex cdcStdioMutex;
    static Mutex webusbMutex;
    usb_init(&fileMutex, &cdcStdioMutex, &webusbMutex, -1, -1);

    multicore_launch_core1(core1);

    usb_start();

    while(true)
    {
        usb_task();
    }
}

#endif
