#ifndef _GPIO_H_
#define _GPIO_H_

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>


#define DEV_GPIO "/dev/gpiochip0"

#define GPIO_PD5_NUM "101"
#define GPIO_PD26_NUM "122"  
#define GPIO_PC21_NUM "85"
#define GPIO_PC30_NUM "94"
#define GPIO_PB2_NUM "34" 
#define GPIO_PA25_NUM "25"
#define GPIO_PC11_NUM "75"

typedef struct {
    char  gpio_name[16];
    int   connected_count;
} tcp_gpio_state_t;

int gpio_init( const char *gpio_num, const char *gpio_name );

void uart_dir_gpio_init();

//void gpio_PD26_UART1_tx_high(void) ;

//void gpio_PD26_UART1_rx_low( void );

void gpio_set_tx_high(const char *gpio_name) ;

void gpio_set_rx_low(const char *gpio_name) ;

void tcp_gpio_connect(const char *gpio_name);

void tcp_gpio_disconnect(const char *gpio_name);

#endif /*_GPIO_H_*/
