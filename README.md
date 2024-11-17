# zephyr-examples-git

Ce dépot comprend quelques exemples de code C avec **zephyr**.

Il est constitué quelques applications concrètes testées sur carte d'évaluation.

Les applications sont développées avec **VSCODE** et son extension C/C++.
pour chaque projet je configure au minimum le fichier **settings.json** et **launch.json**.

La compilation et le flashage/debug se font directement en ligne de commande, 
dans un **venv** python, conformément au processus d'installation de zephyr décrit dans le **getting started** du site officiel.

Pour se connecter à la console série uart, je recommande l'outil **picocom** sous linux, ou **putty/kitty/Mobaxterm** sous windoze.

Pour le lancer il suffit de taper par exemple ***picocom -b 115200 /dev/ttyACM0***.

## liste des projets:
- **stepper_samd21**

Ce projet est testé sur carte d'évaluation **ATMEL SAMD21 XPLAINED PRO**, mais il pourrait fonctionner sur à peu près n'importe quelle carte pourvue d'un bouton et d'une led. Il suffit de modifier éventuellement le *devicetree* et renommer le ficher overlay du projet.

Le programme pilote un moteur pas à pas **28BYJ-48** par l'intermédiaire d'un **ULN2003**, et permet de détecter l'appui d'un bouton et d'un contact fin de course sur interruption avec filtrage anti-rebonds, avec affichage des informations par LED et sortie sur console série. Il utilise les services fournis par le kernel **zephyr**.

Dans cet exemple l'attribution des broches est la suivante:

ULN2003 {IN1,IN2,IN3,IN4} <-> GPIO {PB00,PB01,PB02,PB03}

Contact fin de course <-> GPIO PB04

Pour compiler le programme, on tape ***west build -p always -b samd21_xpro*** 

- **spi_shell_nrf52**

Ce projet testé sur carte d'évaluation nrf52840DK (PCA10056), permet de tester des commandes spi via une console shell. cette carte dispose d'un header de type "arduino uno" avec une interface spi dédiée sur les pins D11,D12,D13 (voir pinout arduino uno). la pin choisie pour le chip-select est D7. ce projet devrait pouvoir directement fonctionner sur toute carte munie du header arduino (ST nucleo, etc ...)

Dans cet exemple on crée une commande "spi" et deux sous-commandes "conf" et "trx", qui permettent de configurer l'interface et d'envoyer et recevoir des octets.

La configuration du port spi inclut la définition de la gpio qui sert pour le chip-select.

pour compiler le programme, on tape ***west build -p always -b nrf52840dk/nrf52840***

