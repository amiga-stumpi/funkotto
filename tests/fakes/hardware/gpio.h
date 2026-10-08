#ifndef TEST_GPIO_H
#define TEST_GPIO_H
#include <stdbool.h>
#define GPIO_IN 0u
#define GPIO_OUT 1u
void gpio_init(unsigned pin);
void gpio_disable_pulls(unsigned pin);
void gpio_put(unsigned pin, bool value);
void gpio_set_dir(unsigned pin, bool output);
unsigned gpio_get_dir(unsigned pin);
bool gpio_get_out_level(unsigned pin);
#endif
