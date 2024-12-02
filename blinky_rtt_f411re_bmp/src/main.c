/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

/* 1000 msec = 1 sec */
#define SLEEP_TIME_MS   1000

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)

/*
 * A build error on this line means your board is unsupported.
 * See the sample documentation for information on how to fix this.
 */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

/*
	j'ai ajouté un bête compteur pour distinguer les lignes sur la sortie console et RTT.
*/

int counter = 0;

int main(void)
{
	int ret;
	bool led_state = true;

	if (!gpio_is_ready_dt(&led)) {
		return 0;
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		return 0;
	}

	while (1) {
		counter++;
		
		ret = gpio_pin_toggle_dt(&led);
		if (ret < 0) {
			return 0;
		}

		led_state = !led_state;

		/*
			dans cet exemple le LOG et le printf (console) utilisent la même UART /dev/ttyBmpTarg.
			En dehors d'une session debug, le LOG ne fonctionne pas car RTT n'est pas activé 
			et seuls les printfs sont affichés. Lors d'une session debug les deux sorties LOG et printf sont affichées.
		*/

		/*
			le printf utilise le backend UART et pas le RTT.
			CONFIG_RTT_CONSOLE=n
			CONFIG_UART_CONSOLE=y
			l'UART utilisée pour la console peut être changée dans le devicetree:
			...
			chosen {
				zephyr,console = &usart2;	<- ici
				zephyr,shell-uart = &usart2;
				zephyr,sram = &sram0;
				zephyr,flash = &flash0;
			};
			...
		*/
		printf("%d:printf:LED state: %s\r\n", counter, led_state ? "ON" : "OFF");

		/*
			si le système de LOG est activé, printk est dirigé par défaut vers 
			le même backend que le log, c'est à dire RTT.
			pour diriger printk vers la même console que printf, il faut ajouter l'option 
			CONFIG_LOG_PRINTK=n dans prj.conf
		*/
		printk("%d:printk:LED state: %s\r\n", counter, led_state ? "ON" : "OFF");

		/*
			le systeme de log utilise le backend RTT et pas l'UART
			CONFIG_LOG_BACKEND_UART=n
			l'option suivante est implicite, pas besoin de la préciser dans prj.conf
			(CONFIG_LOG_BACKEND_RTT=y)
		*/
		LOG_DBG("%d:LED state: %s", counter, led_state ? "ON" : "OFF");

		k_msleep(SLEEP_TIME_MS);
	}
	return 0;
}
