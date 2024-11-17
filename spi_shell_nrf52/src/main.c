/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/*
	copied and modified from spi_shell.c
*/
/*
	Alban MEFFRE
	novembre 2024

	cet exemple implémente les fonctions suivantes:
	- clignoter une led
	- configurer des commandes shell pour configurer et piloter un bus spi
	- utilisations de commandes shell_error, shell_print, shell_hexdump

	on crée un overlay contenant la définition de la gpio qui servira de chip-select
	au bus spi, ici la pin D7 du connecteur arduino (pin P1.08 du MCU nrf52840 = pin numéro 13 du connecteur "arduino_header")
*/


// #include <stdio.h>
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/shell/shell.h>


/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)

/* The devicetree node identifier for the "arduinospi" alias. */
#define SPI_NODE DT_ALIAS(arduinospi)

/*
	on occupe le CPU avec une tâche "main" blinky,
	juste pour montrer que la carte est vivante.
	le shell tourne dans une autre tâche "shell_uart"
*/

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

int main(void)
{
	int ret;

	if (!gpio_is_ready_dt(&led)) {
		return 0;
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		return 0;
	}

	while (1) {
		ret = gpio_pin_toggle_dt(&led);
		if (ret < 0) {
			return 0;
		}

		k_msleep(100);
	}
	return 0;
}

/*
	on crée une structure de configuration pour le bus spi avec des valeurs par défaut, 
	notamment la fréquence de l'horloge spi et surtout la configuration du chip-select.
	on récupère ces informations depuis le fichier devicetree_generated.h.

	le bus spi3 est défini dans le devicetree et le chip select dans l'overlay (app.overlay).
	en effet la pin chipselect utilisée dépend de l'application utilisateur, alors que les pins du bus spi3 sont 
	prédéfinies par le constructeur de la carte d'évaluation.

		...
		spi3: arduino_spi: spi@4002f000 {
			compatible = "nordic,nrf-spim";
			...
			status = "okay";
			cs-gpios = < &arduino_header 0xd 0x1 >;
			...
		};
		...
*/
static struct device *spi_device;
static struct spi_config config = {
	.frequency = 1000000,
	.operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8),
	.cs = {
		.gpio = {
			.port = DEVICE_DT_GET(DT_GPIO_CTLR(SPI_NODE, cs_gpios)),	// =(&__device_dts_ord_105)
			.pin = DT_GPIO_PIN(SPI_NODE, cs_gpios), 					// = 8
			.dt_flags = DT_GPIO_FLAGS(SPI_NODE, cs_gpios),				// = 1
		},
		.delay = 0,
	}
};

/*
	ce symbole définit la taille maximale des buffers d'émission et réception spi.
	ici CONFIG_SHELL_ARGC_MAX = 20 par défaut, c'est le nombre d'arguments de la ligne de commande complète,
	"spi trx <byte1> byte<2> ...", donc il ne reste que 18 emplacements pour les datas à transmettre.
	la macro MIN(a,b) maximise ici la taille de buffer à 32 octets, au cas où CONFIG_SHELL_ARGC_MAX
	serait défini à une valeur trop élevée.
	on peut redéfinir CONFIG_SHELL_ARGC_MAX dans le fichier prj.conf
*/
#define MAX_SPI_BYTES MIN((CONFIG_SHELL_ARGC_MAX - 2), 32)

static int cmd_spi_trx(const struct shell *ctx, size_t argc, char **argv)
{
	uint8_t rx_buffer[MAX_SPI_BYTES] = {0};
	uint8_t tx_buffer[MAX_SPI_BYTES] = {0};

	if (spi_device == NULL) {
		shell_error(ctx, "SPI device isn't configured. Use `spi conf`");
		return -ENODEV;
	}

	// afficher la liste des arguments *argv[] de la sous-commande trx. pour debug.
	// for (int i = 0; i< argc; i++) shell_print(ctx,"%d:%s",i,argv[i]);	

	/*
		le nombre d'octets à transmettre est égale aux nombres d'arguments,
		moins un car le premier argument est la sous-commande elle-même (argv[0] = "trx")
	*/

	int bytes_to_send = argc - 1;

	/*
		le troisième paramètre de la fonction strtol indique que les arguments
		sont des entiers en hexadécimal (majuscule ou minuscule), ici tronqués 
		aux 8 bits de poids faible (ex "C0CAC01A" -> "1a") car tx_buffer est en uint8_t
	*/
	for (int i = 0; i < bytes_to_send; i++) {
		tx_buffer[i] = strtol(argv[1 + i], NULL, 16);
	}

	const struct spi_buf tx_buffers = {.buf = tx_buffer, .len = bytes_to_send};
	const struct spi_buf rx_buffers = {.buf = rx_buffer, .len = bytes_to_send};

	const struct spi_buf_set tx_buf_set = {.buffers = &tx_buffers, .count = 1};
	const struct spi_buf_set rx_buf_set = {.buffers = &rx_buffers, .count = 1};

	/*
		on fait appel à l'API spi, dont l'implémentation bas niveau dépend du constructeur.
		spi_transceive = inline z_impl_spi_transceive = inline api->transceive 
	*/
	int ret = spi_transceive(spi_device, &config, &tx_buf_set, &rx_buf_set);

	if (ret < 0) {
		shell_error(ctx, "spi_transceive returned %d", ret);
		return ret;
	}

	shell_print(ctx, "TX:");
	shell_hexdump(ctx, tx_buffer, bytes_to_send);

	shell_print(ctx, "RX:");
	shell_hexdump(ctx, rx_buffer, bytes_to_send);

	return ret;
}

static int cmd_spi_conf(const struct shell *ctx, size_t argc, char **argv)
{
	spi_operation_t operation = SPI_WORD_SET(8) | SPI_OP_MODE_MASTER;

	/*
		les déclarations suivantes sont toutes équivalentes, 
		elle permettent de récupérer l'adresse du bloc de données décrivant 
		la configuration du bus spi (les valeurs à passer aux registres de config lors de l'init).
		ce bloc de données est généré par le DTC (device tree compiler) à partir 
		du code source du devicetree zephyr.dts, et intégré au binaire de l'application
		lors de l'édition de lien. on peut retrouver son emplacement dans le fichier zephyr.map.
		son adresse est symbolisée par "&(__device_dts_ord_106)".

		pour éviter le warning suivant lors de la compilation:
		"warning: initialization discards 'const' qualifier from pointer"
		il faut faire un cast vers (struct device *) qui supprime le const implicite attendu
	*/

	// struct device *dev = (struct device *)device_get_binding("spi@4002f000");
	// struct device *dev = (struct device *)&__device_dts_ord_106;
	// struct device *dev = (struct device *)DEVICE_DT_GET(DT_NODELABEL(spi3));
	// struct device *dev = (struct device *)DEVICE_DT_GET(DT_NODELABEL(arduino_spi));
	// struct device *dev = (struct device *)DEVICE_DT_GET(DT_ALIAS(arduinospi));
	struct device *dev = (struct device *)DEVICE_DT_GET(SPI_NODE);

	if (dev == NULL) {
		shell_error(ctx, "device %s not found.",DT_NODE_FULL_NAME(SPI_NODE));
		return -ENODEV;
	}

	/* no frequency setting */
	if (argc == (1)) {
		goto out;	/* en général on n'utilise pas goto, mais ici cela évite d'écrire des "if" imbriqués ou un switch-case*/
	}

	uint32_t frequency = strtol(argv[1], NULL, 10);

	if (!IN_RANGE(frequency, 100 * 1000, 80 * 1000 * 1000)) {
		shell_error(ctx, "frequency must be between 100000  and 80000000");
		return -EINVAL;
	}

	config.frequency = frequency;

	/* no settings */
	if (argc == (2)) {
		goto out;
	}

	char *opts = argv[2];
	bool all_opts_is_valid = true;

	while (*opts != '\0') {
		switch (*opts) {
		case 'o':
			operation |= SPI_MODE_CPOL;
			break;
		case 'h':
			operation |= SPI_MODE_CPHA;
			break;
		case 'l':
			operation |= SPI_TRANSFER_LSB;
			break;
		case 'T':
			operation |= SPI_FRAME_FORMAT_TI;
			break;
		default:
			all_opts_is_valid = false;
			shell_error(ctx, "invalid setting %c", *opts);
		}
		opts++;
	}

	if (!all_opts_is_valid) {
		return -EINVAL;
	}

out:
	config.operation = operation;
	spi_device = dev;

	return 0;
}


/*
	les macros suivantes créent la commande "spi", et deux sous-commandes statiques "conf" et "trx".
	la macro SHELL_STATIC_SUBCMD_SET_CREATE déclare une table de sous-commandes SHELL_CMD_ARG, 
	qui se termine par une macro SHELL_SUBCMD_SET_END (=NULL).
	SHELL_CMD_ARG prend notamment en paramètre la fonction handler associée à la sous-commande.c
	la macro SHELL_CMD_REGISTER enregistre la commande spi et ses deux sous-commandes
*/
SHELL_STATIC_SUBCMD_SET_CREATE(sub_spi_cmds,
			       SHELL_CMD_ARG(conf, NULL,
					     "Configure SPI\n"
					     "Usage: spi conf <frequency> [<settings>]\n"
					     "<settings> - any sequence of letters:\n"
					     "o - SPI_MODE_CPOL\n"
					     "h - SPI_MODE_CPHA\n"
					     "l - SPI_TRANSFER_LSB\n"
					     "T - SPI_FRAME_FORMAT_TI\n"
					     "example: spi conf 1000000 ol",
					     cmd_spi_conf, 1, 1),
			       SHELL_CMD_ARG(trx, NULL,
					     "Transceive data to and from an SPI device\n"
					     "Usage: spi trx <TX byte 1> [<TX byte 2> ...]",
					     cmd_spi_trx, 2, MAX_SPI_BYTES),
			       SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(spi, &sub_spi_cmds, "SPI commands", NULL);
