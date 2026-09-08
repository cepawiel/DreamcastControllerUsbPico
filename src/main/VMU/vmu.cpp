#ifndef ENABLE_UNIT_TEST

#include "pico/stdlib.h"
#include "pico/multicore.h"

#include "configuration.h"

#include "DreamcastMainNode.hpp"
#include "PlayerData.hpp"
#include "MaplePassthroughCommandParser.hpp"

#include "CriticalSectionMutex.hpp"
#include "Mutex.hpp"
#include "Clock.hpp"

#include "hal/System/LockGuard.hpp"
#include "hal/MapleBus/MapleBusInterface.hpp"
#include "hal/Usb/usb_interface.hpp"
#include "hal/Usb/TtyParser.hpp"

#include <memory>
#include <algorithm>

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

// Second Core Process
// The second core is in charge of handling communication with Dreamcast peripherals
void core1()
{
    set_sys_clock_khz(CPU_FREQ_KHZ, true);

    // Wait for steady state
    sleep_ms(100);

    uint8_t address[] = {0x00};

    //
    // gpio_init(26);
    // gpio_put(26, true);
    // gpio_set_dir(26, true);

    // high for maple mode
    gpio_init(22);
    gpio_put(22, true);
    gpio_set_dir(22, true);

    // 5V
    gpio_init(14);
    gpio_put(14, true);
    gpio_set_dir(14, true);

    // 3.3V
    gpio_init(15);
    gpio_put(15, true);
    gpio_set_dir(15, true);

    sleep_ms(1000);

    // appears to be driven from VMU
    // // high from megavolt's diagram
    // gpio_init(18);
    // gpio_put(18, true);
    // gpio_set_dir(18, true);

    // // low from megavolt's diagram
    // gpio_init(21);
    // gpio_put(21, true);
    // gpio_set_dir(21, true);

    CriticalSectionMutex screenMutex;
    Clock clock;
    DreamcastControllerObserver** observers = get_usb_controller_observers();
    std::shared_ptr<ScreenData> screenData = std::make_shared<ScreenData>(screenMutex);
    std::shared_ptr<PlayerData> playerData = std::make_shared<PlayerData>(0,
                                                     *(observers[0]),
                                                     *screenData,
                                                     clock,
                                                     usb_msc_get_file_system());
    // std::shared_ptr<MapleBusInterface> txBus = create_maple_bus(16, -1, DIR_OUT_HIGH); // also need to toggle pin 16 & 19 when tx'ing
    // std::shared_ptr<MapleBusInterface> rxBus = create_maple_bus(20, -1, DIR_OUT_HIGH);
    std::shared_ptr<MapleBusInterface> bus = create_maple_bus(18,  20, -1, DIR_OUT_HIGH);
    std::shared_ptr<PrioritizedTxScheduler> scheduler = std::make_shared<PrioritizedTxScheduler>(address[0]);
    std::shared_ptr<DreamcastMainNode> dreamcastMainNode = std::make_shared<DreamcastMainNode>(
            *bus,
            *playerData,
            scheduler);
       
    // Initialize CDC to Maple Bus interfaces
    Mutex ttyParserMutex;
    TtyParser* ttyParser = usb_cdc_create_parser(&ttyParserMutex, 'h');
    ttyParser->addCommandParser(
        std::make_shared<MaplePassthroughCommandParser>(
            &scheduler, address , 1));

    while(true)
    {
        // Worst execution duration of below is ~350 us at 133 MHz when debug print is disabled
        dreamcastMainNode->task(time_us_64());
        
        // Process any waiting commands in the TTY parser
        ttyParser->process();
    }
}

// First Core Process
// The first core is in charge of initialization and USB communication
int main()
{
   set_sys_clock_khz(CPU_FREQ_KHZ, true);

#if 1
    stdio_uart_init();
    printf("\n\n");
#endif

    multicore_launch_core1(core1);

    Mutex fileMutex;
    Mutex cdcStdioMutex;
    usb_init(&fileMutex, &cdcStdioMutex);

    while(true)
    {
        usb_task();
    }
}

#endif
