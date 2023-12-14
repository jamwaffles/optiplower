//! DS2502 emulator.

#![no_main]
#![no_std]

use defmt_rtt as _;
use panic_halt as _;

use embedded_hal::blocking::delay::DelayUs;

#[rtic::app(device = stm32f4xx_hal::pac, dispatchers = [USART1])]
mod app {
    use fugit::ExtU64;
    use one_wire_bus::OneWire;
    use stm32f4xx_hal::{
        gpio::{OpenDrain, Output, PinState, Pull, PA5, PC0},
        pac::{self, TIM5},
        prelude::*,
        timer::{DelayUs, MonoTimer64Us}, // Extended 64-bit timer for 16/32-bit TIMs
    };

    #[shared]
    struct Shared {}

    #[local]
    struct Local {
        led: PA5<Output>,
        ow: OneWire<PC0<Output<OpenDrain>>>,
        delay: DelayUs<TIM5>,
    }

    //#[monotonic(binds = TIM2, default = true)]
    //type MicrosecMono = MonoTimerUs<pac::TIM2>;
    #[monotonic(binds = TIM3, default = true)]
    type MicrosecMono = MonoTimer64Us<pac::TIM3>;

    #[init]
    fn init(ctx: init::Context) -> (Shared, Local, init::Monotonics) {
        let rcc = ctx.device.RCC.constrain();
        let clocks = rcc.cfgr.sysclk(48.MHz()).freeze();

        let gpioc = ctx.device.GPIOC.split();
        let gpioa = ctx.device.GPIOA.split();
        let led = gpioa.pa5.into_push_pull_output();
        defmt::info!("Start");

        let one_wire_pin = gpioc.pc0.into_open_drain_output();

        // Does not work for STM32F411: Forego the external 5K pullup and use internal MCU pullup
        // instead.
        // one_wire_pin.set_internal_resistor(Pull::Up);

        //let mono = ctx.device.TIM2.monotonic_us(&clocks);
        let mono = ctx.device.TIM3.monotonic64_us(&clocks);

        // let mut delay = ctx.device.TIM3.delay_us(&clocks);
        let mut delay = ctx.device.TIM5.delay_us(&clocks);

        defmt::info!("Init complete");

        let mut ow = OneWire::new(one_wire_pin).expect("OneWire init");

        find_device(&mut delay, &mut ow);

        tick::spawn().ok();

        (Shared {}, Local { led, ow, delay }, init::Monotonics(mono))
    }

    #[task(local = [led])]
    fn tick(ctx: tick::Context) {
        tick::spawn_after(1_u64.secs()).ok();

        ctx.local.led.toggle();
    }

    /// We're only looking for one device on the bus.
    fn find_device(
        delay: &mut impl crate::DelayUs<u16>,
        one_wire_bus: &mut OneWire<PC0<Output<OpenDrain>>>,
    ) {
        defmt::info!("Looking for 1W devices");

        let device_address = one_wire_bus
            .device_search(None, false, delay)
            .expect("Search")
            .expect("No device found")
            .0;

        defmt::info!(
            "--> Found device at address {:?} with family code: {:x}",
            device_address.0,
            device_address.family_code()
        );

        defmt::info!("Done");
    }
}
