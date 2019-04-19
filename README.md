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

## Using ESPBOT

### Building the binaries and flashing ESP8266

Needed:
+[Espressif NON-OS SDK](https://github.com/espressif/ESP8266_NONOS_SDK) in a separate repository.
+[esp-open-sdk toolchain](https://github.com/pfalcon/esp-open-sdk) in a separate repository; build the bare Xtensa toolchain and leave ESP8266 SDK separate using:

      $ make STANDALONE=n
 

Build steps (linux)
+Clone the repository.
+Customize build variables according to your ESP8266 module: 
      
      $ cd <your path>/espbot
      $ ./gen_env.sh

      this will generate a env.sh file
      for instance a WEMOS D1 mini file will look like this:
      
      export SDK_DIR=<your path to ESP8266_NONOS_SDK>
      export COMPILE=gcc
      export BOOT=new
      export APP=1
      export SPI_SPEED=40
      export SPI_MODE=DIO
      export SPI_SIZE_MAP=4

+Build with
  
      $ . env.sh
      $ make

+Flash ESP8266 using esptool.py (checkout you distribution packages or [github repository](https://github.com/espressif/esptool))
  
      this is an example that works for WEMOS D1 mini, customize memory addresses according to your flash size
      
      Obtaining a clean starting point
      $ esptool.py erase_flash
      
      Initizalizing flash for ESP SDK
      $ esptool.py --port /dev/ttyUSB0 write_flash -fm dio -fs 32m -ff 40m 0x00000 <your path to ESP NONOS SDK>/bin/boot_v1.7.bin 
      $ esptool.py --port /dev/ttyUSB0 write_flash -fm dio -fs 32m -ff 40m 0x3FB000 <your path to ESP NONOS SDK>/bin/blank.bin
      $ esptool.py --port /dev/ttyUSB0 write_flash -fm dio -fs 32m -ff 40m 0x3FC000 <your path to ESP NONOS SDK>/bin/esp_init_data_default_v08.bin
      $ esptool.py --port /dev/ttyUSB0 write_flash -fm dio -fs 32m -ff 40m 0x3FE000 <your path to ESP NONOS SDK>/bin/blank.bin
      
      Flashing espbot code
      $ cd <your path>/espbot
      $ esptool.py --port /dev/ttyUSB0 write_flash -fm dio -fs 32m -ff 40m 0x01000 bin/upgrade/user1.4096.new.4.bin

### FOTA example

Here is an example on how to use espbot FOTA using a [docker](https://www.docker.com/community-edition#/download) container as the http server (thank you docker for existing).
Consider that binary names change according to your ESP8266 module flash size and map.
      
      Generating the binaries:
      $ cd <your espbot directory>
      $ mkdir bin/upgrade/www
      $ . env.sh
      $ export APP=1
      $ make clean
      $ make
      $ cp bin/upgrade/user1.4096.new.4.bin bin/upgrade/www/user1.bin
      $ export APP=2
      $ make clean
      $ make
      $ cp bin/upgrade/user2.4096.new.4.bin bin/upgrade/www/user2.bin
      $ make | grep VERSION: | awk '{print $2}' >bin/upgrade/www/version.txt

      Start an http server using docker:
      $ docker run -d --name espbot-http-upgrade -p 80:80 -v <your espbot directory>/bin/upgrade/www:/usr/share/nginx/html:ro nginx:alpine

      Configure espbot:

      curl --location --request POST "http://192.168.1.187/api/ota/cfg" \
           --header "Content-Type: application/json" \
           --data "{
                     \"host\": \"yourHostIP\",
                     \"port\": 80,
                     \"path\": \"/\",
                     \"check_version\": \"false\",
                     \"reboot_on_completion\": \"true\"
                   }"
      
      Start upgrade:

      curl --location --request POST "http://192.168.1.187/api/ota/upgrade" --data ""

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
+ espbot_list.hpp
+ espbot_logger.hpp
+ espbot_mdns.hpp
+ espbot_ota.hpp
+ espbot_queue.hpp
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
