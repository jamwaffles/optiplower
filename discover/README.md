# 1Wire device scanner

A quick and dirty tool to run on STM32F411 to discover devices on the 1Wire bus.

Example output when DS2502 is connected:

```
INFO  Start
└─ discover::app::init @ src/main.rs:49
INFO  Init complete
└─ discover::app::init @ src/main.rs:63
INFO  Looking for 1W devices
└─ discover::app::find_device @ src/main.rs:86
INFO  --> Found device at address 16140901434897782537 with family code: 9
└─ discover::app::find_device @ src/main.rs:94
INFO  Done
└─ discover::app::find_device @ src/main.rs:100
```
