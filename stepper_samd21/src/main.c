/*
	Alban MEFFRE
	novembre 2024

	cet exemple implémente les fonctions suivantes:
	- pilotage d'un moteur pas à pas 28BYJ-48
	- détection d'appui bouton avec filtre anti-rebond logiciel

	il utilise les services kernel suivants:
	- delayable work et workqueue
	- event et polling
	- timing
	- interruptions et callbacks
	
*/

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/stepper.h>
#include <zephyr/logging/log.h>

/*
	ici pas de printf et de printk
	on utilise le systeme de log de zephyr sur le backend par defaut: la console uart.
	on enregistre ce fichier source et on lui affecte le niveau par defaut ou global (ici INFO)
	ou on override le niveau pour ce module (DEBUG)
	ajouter les options suivantes dans prj.conf:
	CONFIG_LOG=y
	CONFIG_LOG_MODE_IMMEDIATE=y
	cette deuxième option permet d'afficher le log sans delai
	voir la doc ici:
	https://docs.zephyrproject.org/latest/services/logging/index.html#global-kconfig-options
*/

LOG_MODULE_REGISTER(main);
// LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);


/*
	declaration des descripteurs de périphériques:
	struct device est un descripteur générique
	struct gpio_dt_spec est un descripteur spécifique aux gpio déclarées dans le devicetree
	pour chaque déclaration on teste s'il existe un noeud correpondant dans le devicetree, et 
	si son statut est "okay".
	si le noeud a un alias, on utilise la macro DT_ALIAS
	s'il n'en a pas on utilise la macro DT_NODELABEL
	par exemple DT_NODELABEL(motor0) va chercher le noeud appelé motor0
	...
		steppers {
			compatible = "gpio-steppers";
			motor0: motor_0 {
				gpios = <&portb 0 GPIO_ACTIVE_HIGH>,
						<&portb 1 GPIO_ACTIVE_HIGH>,
						<&portb 2 GPIO_ACTIVE_HIGH>,
						<&portb 3 GPIO_ACTIVE_HIGH>;
			};
		};
	...
	si le noeud n'existe pas ou s'il n'est pas "okay", la compilation s'arrête sur une erreur
	tous les symboles extraits du devicetree sont définis dans le fichier:
	build/zephyr/include/generated/zephyr/devicetree_generated.h
	qui correspond au devicetree complet généré dans le fichier:
	build/zephyr/zephyr.dts
*/
#if !DT_NODE_HAS_STATUS_OKAY(DT_ALIAS(led0))
	#error "led0 node eroor"
#endif
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

#if !DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(motor0))
	#error "motor0 node eroor"
#endif
const struct device *const motor0_dev = DEVICE_DT_GET(DT_NODELABEL(motor0));

#if !DT_NODE_HAS_STATUS_OKAY(DT_ALIAS(sw0))
	#error "sw0 node eroor"
#endif
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

#if !DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(endstop))
	#error "endstop node eroor"
#endif
static const struct gpio_dt_spec endstop = GPIO_DT_SPEC_GET(DT_NODELABEL(endstop), gpios);

/*
	dans cet exemple on va détecter l'appui d'un bouton et d'un contact fin de course
	grâce aux interruptions, et on va utiliser les services du kernel pour implémenter un anti-rebond
	de 30 millisecondes. lorsqu'un appui franc et détecté, une led flashe pendant 50 ms 
	et une info est logguée dans la console.
*/

/*
	déclaration des structures qui contiennent le contexte d'appel du callback.
	elle seront initialisées dans le code.
*/
static struct gpio_callback button_cb_data;
static struct gpio_callback endstop_cb_data;

/*
	on créé une tâche rapide qui doit juste éteindre la led.
	cette tâche sera empilée sur la workqueue systeme,
	et elle sera exécutée après un delai de 50 ms. sont appel sera assurée par 
	le kernel qui gère la workqueue, indépendamment du code utilisateur.
	https://docs.zephyrproject.org/latest/kernel/services/threads/workqueue.html#delayable-work
*/
static void ledoff_work_handler(struct k_work *work){
	gpio_pin_set_dt(&led, 0);
}

K_WORK_DELAYABLE_DEFINE(ledoff_work,ledoff_work_handler);

/*
	on crée une tâche rapide qui doit vérifier si le bouton ou le contact
	est toujours actif après le delai d'anti-rebond, et si c'est le cas, 
	on loggue une information, on allume une led, et on programme l'extinction 
	de la led en utilisation la workqueue.
*/
static void debounce_work_handler(struct k_work *work){
	if (gpio_pin_get_dt(&button)){
		gpio_pin_set_dt(&led, 1);
		k_work_reschedule(&ledoff_work,K_MSEC(50));
		LOG_INF("button pressed");
	}
	if (gpio_pin_get_dt(&endstop)){
		gpio_pin_set_dt(&led, 1);
		k_work_reschedule(&ledoff_work,K_MSEC(50));
		LOG_INF("endstop pressed");
	}	
}	

K_WORK_DELAYABLE_DEFINE(debounce_work,debounce_work_handler);

/*
	voici le callback appelé par le handler d'interruption associé aux boutons/contacts.
	il place sur la workqueue la tache debounce_work qui assurera le filtrage anti-rebond.
*/
void button_pressed(const struct device *dev, struct gpio_callback *cb,
		    uint32_t pins)
{
	k_work_reschedule(&debounce_work,K_MSEC(30));
}


/*
	la fonction main() est exécutée par le "main thread".
	si main ne bloque pas dans une boucle et se termine par un return, 
	le kernel continue de gérer les autres thread et la workqueue, et les callback
	d'interruption mis en place continuent de fonctionner.
*/

int main(void)
{
	/*
		cette variable contiendra le code de retour d'erreur de toutes les fonctions
		susceptibles de retourner un code. indispensable lors de la mise au point
		du programme pour pouvoir debugguer chaque étape de l'initialisation du hardware.
	*/
	int ret;



	/*
		un device "not ready" signifie que la structure device associée n'est pas déclarée (NULL)
	*/
	if (!gpio_is_ready_dt(&led)) {LOG_ERR("led not ready");return 0;}
	if (!gpio_is_ready_dt(&button)){LOG_ERR("button not ready"); return 0;}
	if (!gpio_is_ready_dt(&endstop)){LOG_ERR("endstop not ready"); return 0;}

	/*
		configuration de la gpio associée à la led, initialisée à l'état inactif.
		l'état actif de la gpio est configurée dans le devicetree.
	*/
	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {LOG_ERR("led configure");return 0;}
	LOG_INF("led configuration ok.");

	/*
		configuration des gpio associées aux boutons, l'état actif peut être l'état logique 0 ou 1,
		est configuré dans le devicetree de même que les résistances de pull-up/pull-down éventuelles.
		par exemple:
		...
		buttons {
			compatible = "gpio-keys";
			endstop: button_1 {
				gpios = < &portb 4 (GPIO_PULL_UP | GPIO_ACTIVE_HIGH) >;
			};
		};
		...
		si l'on oublie le pull-up le contact n'est plus détecté.
	*/
	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret < 0){LOG_ERR("button configure");return 0;}	
	ret = gpio_pin_configure_dt(&endstop, GPIO_INPUT);
	if (ret < 0){LOG_ERR("endstop configure");return 0;}
	LOG_INF("buttons configuration ok.");

#if 1	
	/*
		configuration des interruptions et du callback associé à chaque périphérique.
		dans cet exemple le callback est commun aux deux boutons/contacts.
	*/
	ret = gpio_pin_interrupt_configure_dt(&button,GPIO_INT_EDGE_TO_ACTIVE);
	if (ret < 0){LOG_ERR("button interrupt configure");return 0;}
	ret = gpio_pin_interrupt_configure_dt(&endstop,GPIO_INT_EDGE_TO_ACTIVE);
	if (ret < 0){LOG_ERR("endstop interrupt configure");return 0;}

	gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
	gpio_init_callback(&endstop_cb_data, button_pressed, BIT(endstop.pin));

	ret = gpio_add_callback(button.port, &button_cb_data);
	if (ret < 0){LOG_ERR("button add callback");return 0;}
	ret = gpio_add_callback(endstop.port, &endstop_cb_data);
	if (ret < 0){LOG_ERR("endstop add callback");return 0;}
	LOG_INF("buttons interrupt configuration ok.");
	/*
		à partir de cet instant les interruptions sont activées.
	*/

#endif	

#if 1
	int32_t pos;

	/*
		on vérifie que le descripteur du device a bien été instancié
	*/
	if (!device_is_ready(motor0_dev)) {LOG_ERR("stepper not ready");return 0;}

	/*
		le driver de moteur pas à pas utilise ici les gpio directement.
		on a 4 signaux de phase pour piloter deux bobinages bipolaires à point milieu
		via 4 transistors de puissance en collecteur ouvert.
		ce moteur peut être piloté en 4 pas complets ou 8 demi-pas
		en pas complet le couple est plus élevé mais la fréquence de pilotage est réduite.
		le réglage "max velocity" correspond au nombre de pas par seconde, 
		parfois donné en hertz dans les spécifications du moteur.
	*/
#if 0	//FULL STEP HIGH TORQUE
	ret = stepper_set_micro_step_res(motor0_dev, STEPPER_FULL_STEP);
	if (ret < 0) {LOG_ERR("stepper set micro step");return 0;}

	ret = stepper_set_max_velocity(motor0_dev, 250);
	if (ret < 0) {LOG_ERR("stepper set max velocity");return 0;}
#else 	//HALF STEP LOW TORQUE
	ret = stepper_set_micro_step_res(motor0_dev, STEPPER_MICRO_STEP_2);
	if (ret < 0) {LOG_ERR("stepper set micro step");return 0;}

	ret = stepper_set_max_velocity(motor0_dev, 500);
	if (ret < 0) {LOG_ERR("stepper set max velocity");return 0;}
#endif	//END STEP

	/*
		on informe le driver que sa position absolue courante est zéro
	*/
	ret = stepper_set_actual_position(motor0_dev, 0);
	if (ret < 0) {LOG_ERR("stepper set actual position");return 0;}

	/*
		on active le driver du moteur
	*/
	ret = stepper_enable(motor0_dev, true);
	if (ret < 0) {LOG_ERR("stepper enable");return 0;}
	LOG_INF("stepper configuration ok.");

	/*
		on vérifie que la position courante est toujours à zéro.
		c'est facultatif, mais ça illustre juste la fonction
	*/
	ret = stepper_get_actual_position(motor0_dev, &pos);
	if (ret < 0) {LOG_ERR("stepper get actual position");return 0;}
	LOG_DBG("position = %d", pos);

	/*
		on crée un évènement de type signal. au lieu de faire du polling pour vérifier si le moteur
		a terminé son mouvement vers la position absolue ciblée et de bloquer l'exécution, 
		on demande à ce que le driver génère un signal lorsque le mouvement est terminé. 
		ceci permet de suspendre le thread courant et de le mettre en attente sur cet évènement 
		pour permettre au kernel d'exécuter les autres thread, et on ne bloque pas inutilement
		l'exécution du programme. 
		il existe différents type d'évènements k_poll
		https://docs.zephyrproject.org/latest/kernel/services/polling.html
		ici le signal est juste une sorte de flag, mis à 1 par l'émetteur et lu puis remis à zéro 
		par le récepteur en attente du signal.
	*/
	struct k_poll_signal async_signal;
    struct k_poll_event move_event;

	/*
		on initialise le signal, et on l'associe à un évèmenent système.
		c'est obligatoire pour que le kernel puisse rendre la main au thread qui attend le signal.
	*/
	k_poll_signal_init(&async_signal);
    k_poll_event_init(&move_event, K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &async_signal);

	/*
		on entre dans la boucle infinie
	*/
	LOG_INF("main loop...");
	while (true)
	{
		/*
			stepper_move initie un déplacement relatif,
			stepper_set_target_position initie un déplacement à une position absolue.
			les deux fonctions génèrement un signal asynchrone lorsque la position finale est atteinte
		*/

		// ret = stepper_move(motor0_dev, 1000, &async_signal);
		// if (ret < 0) {LOG_ERR("stepper move");return 0;}
		ret = stepper_set_target_position(motor0_dev, 1000, &async_signal);
		if (ret < 0) {LOG_ERR("stepper set target position");return 0;}

		/*
			le thread courant est suspendu ad vitam aeternam (timeout = K_FOREVER), 
			et reprendra son exécution lorsque l'évènement signal sera émis.
		*/
		k_poll(&move_event, 1, K_FOREVER);

		/*
			on lit et affiche la position absolue courante du moteur.
		*/
		ret = stepper_get_actual_position(motor0_dev, &pos);
		if (ret < 0) {LOG_ERR("stepper get actual position");return 0;}
		LOG_DBG("position = %d", pos);

		// ret = stepper_move(motor0_dev, -1000, &async_signal);
		// if (ret < 0) {LOG_ERR("stepper move");return 0;}
		ret = stepper_set_target_position(motor0_dev, -1000, &async_signal);
		if (ret < 0) {LOG_ERR("stepper set target position");return 0;}

		k_poll(&move_event, 1, K_FOREVER);

		ret = stepper_get_actual_position(motor0_dev, &pos);
		if (ret < 0) {LOG_ERR("stepper get actual position");return 0;}
		LOG_DBG("position = %d", pos);
	}
#endif	

/*
	au lieu de tester le moteur on peut juste faire clignoter une led
*/
#if 0
	LOG_INF("led blink loop...");
	while (1) {
		ret = gpio_pin_toggle_dt(&led);
		if (ret < 0) {return 0;}

		/*
			le delai d'attente n'est pas une fonction bloquante.
			ici on suspend le thread courant, et l'exécution reprendra 
			lorsque le delai sera écoulé.
		*/
		k_msleep(250);
	}
#endif


	return 0;
	/*
		lorsque la fonction main() se termine, le kernel continue de tourner, 
		ainsi que les callback d'interruption qui n'ont pas été suspendus
	*/
}
