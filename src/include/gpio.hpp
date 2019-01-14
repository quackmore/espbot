/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <quackmore-ff@yahoo.com> wrote this file.  As long as you retain this notice
 * you can do whatever you want with this stuff. If we meet some day, and you 
 * think this stuff is worth it, you can buy me a beer in return. Quackmore
 * ----------------------------------------------------------------------------
 */
#ifndef __GPIO_HPP__
#define __GPIO_HPP__

#define ESPBOT_D1 1
#define ESPBOT_D2 2
#define ESPBOT_D3 3
#define ESPBOT_D4 4
#define ESPBOT_D5 5
#define ESPBOT_D6 6
#define ESPBOT_D7 7
#define ESPBOT_D8 8

#define ESPBOT_LOW 0
#define ESPBOT_HIGH 1

#define ESPBOT_GPIO_OK -1
#define ESPBOT_GPIO_WRONG_IDX -2
#define ESPBOT_GPIO_WRONG_LVL -3
#define ESPBOT_GPIO_WRONG_TYPE -4
#define ESPBOT_GPIO_CANNOT_CHANGE_INPUT -5

#define ESPBOT_GPIO_INPUT 0
#define ESPBOT_GPIO_OUTPUT 1
#define ESPBOT_GPIO_UNPROVISIONED 2

class Gpio
{
private:
  int32_t m_gpio_provisioned;
  int32_t m_gpio_config;

  // int restore_cfg(void);          // return CFG_OK on success, otherwise CFG_ERROR
  // int saved_cfg_not_update(void); // return CFG_OK when cfg does not require update
  //                                 // return CFG_REQUIRES_UPDATE when cfg require update
  //                                 // return CFG_ERROR otherwise
  // int save_cfg(void);             // return CFG_OK on success, otherwise CFG_ERROR

public:
  Gpio(){};
  ~Gpio(){};

  void init(void);

  int config(int t_idx, int t_type); // take the gpio index (1..8) and ESPBOT_GPIO_INPUT/ESPBOT_GPIO_INPUT
  int unconfig(int);                 // return ESPBOT_GPIO_OK on success
                                     // return ESPBOT_GPIO_WRONG_IDX if idx < 1 or idx > 8
                                     // return ESPBOT_GPIO_WRONG_TYPE if t_type is different from
                                     //        ESPBOT_GPIO_INPUT/ESPBOT_GPIO_INPUT
                                     // gpio configuration is not persistent

  int get_config(int); // takes the gpio index (1..8)
                       // returns ESPBOT_GPIO_UNPROVISIONED or
                       //         ESPBOT_GPIO_INPUT or
                       //         ESPBOT_GPIO_OUTPUT on success
                       // returns ESPBOT_GPIO_WRONG_IDX if idx < 1 or idx > 8

  int read(int); // takes the gpio index (1..8)
                 // returns ESPBOT_LOW or ESPBOT_HIGH on success
                 // returns ESPBOT_GPIO_WRONG_IDX if idx < 1 or idx > 8
                 // returns ESPBOT_GPIO_UNPROVISIONED if gpio wasn't set as input/output

  int set(int, int); // takes the gpio index (1..8) and the output level (ESPBOT_LOW or ESPBOT_HIGH)
                     // returns ESPBOT_GPIO_OK on success
                     // returns ESPBOT_GPIO_WRONG_IDX if idx < 1 or idx > 8
                     // returns ESPBOT_GPIO_UNPROVISIONED if gpio wasn't set as input/output
                     // returns ESPBOT_GPIO_CANNOT_SET_INPUT if gpio was set as input
};

#endif
