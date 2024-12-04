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
### stepper_samd21

Ce projet est testé sur carte d'évaluation **ATMEL SAMD21 XPLAINED PRO**, mais il pourrait fonctionner sur à peu près n'importe quelle carte pourvue d'un bouton et d'une led. Il suffit de modifier éventuellement le *devicetree* et renommer le ficher overlay du projet.

Le programme pilote un moteur pas à pas **28BYJ-48** par l'intermédiaire d'un **ULN2003**, et permet de détecter l'appui d'un bouton et d'un contact fin de course sur interruption avec filtrage anti-rebonds, avec affichage des informations par LED et sortie sur console série. Il utilise les services fournis par le kernel **zephyr**.

Dans cet exemple l'attribution des broches est la suivante:

- ULN2003 {IN1,IN2,IN3,IN4} <-> GPIO {PB00,PB01,PB02,PB03}
- Contact fin de course <-> GPIO PB04

Pour compiler le programme, on tape ***west build -p always -b samd21_xpro*** 

### spi_shell_nrf52

Ce projet testé sur carte d'évaluation **Nordic nrf52840DK (PCA10056)**, permet de tester des commandes spi via une console shell. cette carte dispose d'un header de type "arduino uno" avec une interface spi dédiée sur les pins D11,D12,D13 (voir pinout arduino uno). la pin choisie pour le chip-select est D7. ce projet devrait pouvoir directement fonctionner sur toute carte munie du header arduino (ST nucleo, etc ...)

Dans cet exemple on crée une commande "spi" et deux sous-commandes "conf" et "trx", qui permettent de configurer l'interface et d'envoyer et recevoir des octets.

La configuration du port spi inclut la définition de la gpio qui sert pour le chip-select.

pour compiler le programme, on tape ***west build -p always -b nrf52840dk/nrf52840***

### blinky_rtt_f411re_bmp

Ce projet utilise une carte d'évaluation **ST Nucleo F411re** modifiée, la sonde de débug **STLINK** intégrée a été remplacée par une sonde **BlackMagic Probe**. Les fonctionnalités suivantes sont illustrées: 

- Création d'une carte personalisée (avec sonde **BMP** au lieu de la **STLINK**),
- Utilisation de la sonde de débug **[BlackMagic Probe](https://black-magic.org/)** et du protocole **[Real Time Transfert (RTT)](https://www.segger.com/products/debug-probes/j-link/technology/about-real-time-transfer/)**,
- Utilisation de deux backends différents pour la console(Uart) et le LOG(RTT),
- Configuration des extensions VSCODE **Cortex-Debug** et **Peripheral-Viewer**.

#### Création d'une carte personalisée

On copie l'intégralité du dossier de la carte préexistante **zephyr/boards/st/nucleo_f411re** dans le répertoire du projet et on le renomme en **boards/arm/nucleo_f411re_bmp**. Se référer à la [documentation zephyr](https://docs.zephyrproject.org/latest/hardware/porting/board_porting.html#create-your-board-directory) pour savoir comment modifier les fichiers de description de la carte. 

Dans notre cas c'est surtout le fichier **board.cmake** qui doit contenir la ligne
```
include(${ZEPHYR_BASE}/boards/common/blackmagicprobe.board.cmake)
```
Il faut aussi modifier le fichier **CMakeLists.txt** pour lui indiquer qu'il doit chercher la nouvelle carte dans le répertoire du projet.

```
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
```

#### préparation de la sonde BlackMagic

Tout d'abord compiler le firmware BMP, disponible sur le dépot [github](https://github.com/blackmagic-debug/blackmagic/tree/main). Pour Celà, il faut modifier le fichier **cross-file/stlink.ini** et lui ajouter/modifier les options suivantes:
```
[project options]
targets = 'cortexm,stm'
rtt_support = true
bmd_bootloader = false
```
En cas d'erreur ou de modification de la configuration des sources, il est plus simple de supprimer le dossier **build/** et de recommencer la configuration *from scratch*. Vous pouvez aussi vous bagarrer avec **meson** si ça vous amuse, mais dans mon cas l'option **--reconfigure** ne donnait pas les résultats espérés et je n'ai donc pas insisté.

Ensuite on configure les sources avec ***meson setup build --cross-file=cross-file/stlink.ini***,
Puis on lance la compilation avec ***meson compile -C build***.

Il faudra aussi compiler l'outil **Stlink Tool** disponible sur [github](https://github.com/blackmagic-debug/stlink-tool]).
Il va permettre de reprogrammer le **STLINK** intégré à la carte nucleo avec le firmware blackmagicprobe fraîchement compilé, grâce à une simple commande ***stlink-tool blackmagic_stlink_firmware.bin***.

Pour que la nouvelle sonde soit détectée sous linux, il est indispensable d'ajouter au système les règles **udev** disponibles [ici](https://github.com/blackmagic-debug/blackmagic/tree/main/driver). installer les règles dans **/etc/udev/rules.d/** et les recharger avec la commande **sudo udevadm control --reload**. Pour vérifier la bonne détection de la sonde, on peut taper la commande **blackmagic -t** qui a été compilée en même temps que le firmware.

La sonde **BlackMagic** est maintenant accessible par deux ports série virtuels de type ACM, les fichiers **/dev/ttyBmpGdb** pour le débug et le fichier **/dev/ttyBmpTarg** pour le port UART de la console et de RTT, les deux étant partagés, la vitesse par défaut est de 115200 Bauds (8 bits, pas de parité).

#### Backends console UART et RTT

Dans ce projet les deux backends cohabitent sur le même port série virtuel **ttyBmpTarg**, toutefois leur fonctionnement diffère. 

La console UART utilise un vrai port série du microcontrôleur cible, ici l'**usart2** du **STM32F411RE**, avec le gros inconvénient du temps d'attente d'écriture sur le port (sauf à mettre en place du DMA), alors que RTT consiste à écrire quasi-instantanément les caractères dans une fifo en RAM, dont le firmware BMP transfère ensuite le contenu vert le port série ACM. 

De ce fait, l'avantage de RTT réside dans sa grande vitesse d'exécution ce qui est très utile pour tracer l'exécution d'un système temps réel, ne disposant pas d'un port série disponible. 

L'inconvénient est par contre la latence de sortie des caractères, quelques dizaines de millisecondes (ajustable) avec un débit maximum d'environ 50000 caractères par seconde. 

Un autre inconvénient du RTT est qu'il ne fonctionne que pendant une session de debug (gdb) et doit être explicitement activé avec la commande **monitor rtt enable**, alors que le port série lui est disponible en permanence et ne nécessite pas de sonde de debug.

#### extensions VSCODE Cortex-Debug et Peripheral-Viewer

L'extension VSCODE **Cortex-Debug** peut être programmée pour exploiter la blackmagicprobe. Dans ce projet le fichier **launch.json** est configuré pour lancer une session de debug directement sur la sonde sans passer par un serveur tiers comme **OpenOCD**. 

L'extension VSCODE **Peripheral-Viewer** permet d'inspecter les registres des périphériques du **STM32F411RE**, il faut pour celà lui fournir les définitions des registres sous la forme d'un fichier SVD, ici placé dans **boards/arm/nucleo_f411re_bmp/support/STM32F411.svd**. Ce fichier a été téléchargé sur le site de **ST**, depuis la documentation du **STM32F411**. Les registres apparaitrons lors de la session débug dans une section **XPERIPHERALS** de l'onglet **Run and Debug** de VSCODE. 

Dans ce projet on pourra inspecter le registre ODR du périphérique GPIOA, dont le bit 5 correspond à la LED.

#### compilation du projet

Le projet peut être compilé avec la commande ***west build -p always -b nucleo_f411re_bmp***, le fichier **prj.conf** contient les options **KConfig** nécessaires pour faciliter le debug.
