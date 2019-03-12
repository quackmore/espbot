# espbot_2.0

## Summary

Empty structure for esp8266 apps (NON-OS SDK and C++).

Features:

+ configuration persistency
+ GPIO
+ logger
+ mDns
+ memory monitor (stack and heap)
+ OTA
+ SNTP
+ SPIFFS
+ wifi management (chromecast style)
+ WEBSERVER (APIs listed below)
+ WEBCLIENT

more to come ... (OTA with periodic check of new version available)

## Webserver APIs

checkout postman API documentation:

espbot_2.0_apis

https://documenter.getpostman.com/view/4220776/RznCsKph

espbot_2.0_apis_single_commands
(these are just commands that cannot be run in a test sequence)

https://documenter.getpostman.com/view/4220776/RznCsKpj

espbot_2.0_gpio
(for managing ESP8266 digital I/O)

https://documenter.getpostman.com/view/4220776/RzthSXGC

## Building the binaries

Checkout my other repo https://github.com/quackmore/esp8266_SDK_structure for details.

## Integrating

To integrate espbot in your project as a library use the following files:

+ lib/libespbot.a
+ lib/libdriver.a
+ lib/libspiffs.a
+ lib/libuser.a

To import the library source files use the following files:

+ src/espbot
+ src/driver
+ src/spiffs
+ src/user

Espot include files are:

+ config.hpp
+ debug.hpp
+ esp8266_spiffs.hpp
+ espbot_global.hpp
+ espbot.hpp
+ espbot_http_routes.hpp
+ espbot_utils.hpp
+ gpio.hpp
+ hw_timer.h
+ json.hpp
+ logger.hpp
+ mdns.hpp
+ ota_upgrade.hpp
+ sntp.hpp
+ spiffs_config.h
+ spiffs_flash_functions.hpp
+ spiffs.h
+ spiffs_nucleus.h
+ uart.h
+ uart_register.h
+ user_config.h
+ webclient.hpp
+ webserver.hpp
+ wifi.hpp

## License

Espbot_2.0 comes with a [BEER-WARE] license.

Enjoy.
