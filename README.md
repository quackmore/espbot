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

To import the library source files use the following files:

+ src/driver
+ src/espbot
+ src/spiffs

Espot include files are:

+ driver_hw_timer.h
+ driver_uart_register.h
+ driver_uart.h
+ esp8266_io.h
+ espbot_config.hpp
+ espbot_debug.hpp
+ espbot_global.hpp
+ espbot_gpio.hpp
+ espbot_http_routes.hpp
+ espbot_json.hpp
+ espbot_logger.hpp
+ espbot_mdns.hpp
+ espbot_ota.hpp
+ espbot_sntp.hpp
+ espbot_utils.hpp
+ espbot_webclient.hpp
+ espbot_webserver.hpp
+ espbot_wifi.hpp
+ espbot.hpp
+ spiffs_config.h
+ spiffs_esp8266.hpp
+ spiffs_flash_functions.hpp
+ spiffs_nucleus.h
+ spiffs.h

## License

Espbot_2.0 comes with a [BEER-WARE] license.

Enjoy.
