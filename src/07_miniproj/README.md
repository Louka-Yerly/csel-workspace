# Mini Projet - Programmation noyau et système

Ce `readme` fait office de guide utilisateur pour le mini projet de programmation noyau et système du cours MA_CSEL1.

# Date 
16 juin 2023

# Auteurs
- Louka Yerly
- Luca Srdjenovic

# Mode d'emploi

Voici les instructions à suivre pour pouvoir utiliser le projet se trouvant dans le dossier `src/07_miniproj`.

## Modifier le dtb

En premier, il faut modifier le Device Tree Blob (dtb) pour ajouter le device oled sur le bus i2c.

Il faut aller dans le dossier `./oled` et compiler le fichier `mydt.dts` :

```
$ cd src/07_miniproj/oled
$ make dtb
```


La compilation engendre un fichier `mydt.dtb` qui est le fichier qui sera utilisé par le noyau. Il faut copier ce fichier dans la partition boot de la carte flash eMMC.



Tout d'abord, depuis le NanoPi, il faut monter la partition où se trouve le fichier `nanoPi-neo-plus2.dtb`. Il est important de noter qu'il y a 2 devices `mmcblk1` et `mmcblk2`. Le premier est la eMMC et le deuxième est la carte SD. Il faut donc monter le deuxième device, s'agissant dans notre cas de `mmcblk2`. Le device contient 2 partitions. La première est la partition `boot` et la deuxième est la partition `rootfs`. Il faut donc monter la première partition `boot`.

```
# mkdir /mnt/boot
# mkdir /mnt/rootfs
# mount /dev/mmcblk2p1 /mnt/boot
# mount /dev/mmcblk2p2 /mnt/rootfs
# ls /mnt/boot
Image boot.cifs boot.scr nanoPi-neo-plus2.dtb uboot.env
# ls /mnt/rootfs
bin etc lib64 lost+found mnt proc run sys usr workspace
dev lib linuxrc media opt root sbin tmp var
```

Il faut ensuite copier le fichier `mydt.dtb` dans le dossier `/mnt/boot/nanoPi-neo-plus2.dtb` :

```
# cp oled/mydt.dtb /mnt/boot/nanoPi-neo-plus2.dtb
```

Ensuite démonter la partition boot :

```
# umount /mnt/boot
```

Au prochain démarrage, le NanoPi utilisera le nouveau dtb qui contient le device oled.

Pour vérifier que le device oled est bien présent, il faut lancer la commande suivante :

```
# i2cdetect -y 0
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
00:                         -- -- -- -- -- -- -- -- 
10: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
20: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
30: -- -- -- -- -- -- -- -- -- -- -- -- 3c -- -- -- 
40: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
50: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
60: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
70: -- -- -- -- -- -- -- -- 
```

On peut voir que le device oled est présent à l'adresse `0x3c`.


## Installation du module

Deuxième étape, il faut installer le module qui permet de contrôler le ventilateur. Le module se trouve dans le dossier `./module`.

### Insmod

Pour installer le module avec la commande `insmod`, il faut compiler le module avec la commande :

```
$ cd src/07_miniproj/module
$ make
```

La compilation engendre un fichier `fan_drv.ko` qui est le module à installer sur le NanoPi. Sur le NanoPi, pour installer ce module, il faut lancer la commande suivante :

```
# cd src/07_miniproj/module
# insmod fan_drv.ko
```

Pour vérifier que le module est bien installé, lancer la commande `lsmod` :

```
# lsmod | grep fan_drv
fan_drv                16384  0 
```

Avec la commande `dmesg`, cela permet de voir les messages du kernel. On peut voir que le module a bien été chargé :

```
# dmesg | tail -n 3
[  863.786750] fan_drv: loading out-of-tree module taints kernel.
[  863.794084] Finish initializing driver...
[  863.794091] fan_thread is now active...
```

Dans le dossier `sys/class/misc`, on peut voir que le module a bien été chargé :

```
# ls /sys/class/misc/ | grep fan_drv
fan_drv
# cd /sys/class/misc/fan_drv/
# ls
dev  freq  mode  power	subsystem  uevent
```

Le module est maintenant installé et prêt à être utilisé.

Pour désinstaller le module, il faut lancer la commande suivante :

```
# rmmod fan_drv
# dmesg | tail -n 2
[ 2708.905518] fan_thread exit...
[ 2708.909059] Finish remove driver...
```

### Modprobe

Pour installer le module avec la commande `modprobe`, il faut compiler le module avec la commande :

```
$ make install
make -C /buildroot/output/build/linux-5.15.21/ M=/workspace/src/07_miniproj/module INSTALL_MOD_PATH=/rootfs modules_install
make[1]: Entering directory '/buildroot/output/build/linux-5.15.21'
cat: /workspace/src/07_miniproj/module/modules.order: No such file or directory
  DEPMOD  /rootfs/lib/modules/5.15.21
make[1]: Leaving directory '/buildroot/output/build/linux-5.15.21'
```

La commande `make install` permet de copier le module dans le dossier `/rootfs/lib/modules/5.15.21` directement sur le NanoPi. Ensuite, pour installer le module, il faut lancer la commande suivante :

```
# modprobe fan_drv
```

Pour vérifier que le module est bien installé, lancer la commande `lsmod` :

```
# lsmod | grep fan_drv
fan_drv                16384  0 
```

Avec la commande `dmesg`, cela permet de voir les messages du kernel. On peut voir que le module a bien été chargé :

```
# dmesg | tail -n 3
[  863.786750] fan_drv: loading out-of-tree module taints kernel.
[  863.794084] Finish initializing driver...
[  863.794091] fan_thread is now active...
```

Pour désinstaller le module, il faut lancer la commande suivante :

```
# modprobe -r fan_drv
# dmesg | tail -n 2
[ 2708.905518] fan_thread exit...
[ 2708.909059] Finish remove driver...
```


## Installation du script de démarrage

Troisième étape, il faut installer le script de démarrage qui permet de lancer le deamon au démarrage du NanoPi permettant de contrôler le ventilateur depuis l'espace utilisateur.

Avant d'installer le script de démarrage, il faut s'assurer que le module puisse être correctement installé avec la commande `modprobe fan_drv`. La raison est que le script de démarrage installe le module avant de lancer le deamon.

Dans le dossier `./deamon` se trouve le code `fand.c` qui est le code du deamon qui permet de contrôler le ventilateur depuis l'espace
utilisateur. Pour le compiler, il faut lancer la commande :

```
$ cd ./deamon
$ make
```

La compilation engendre un binaire `fand` qui est le deamon à installer sur le NanoPi. Sur le NanoPi, pour installer ce deamon, il faut lancer la commande suivante :

```
# mkdir /usr/local
# cp ./deamon/fand /usr/local/
# chmod +x /usr/local/fand
```

Encore dans le dossier `./deamon`, se trouve un script `S60_fand` qui permet de démarrer le deamon au démarrage du NanoPi. Il faut copier ce script dans le dossier `/etc/init.d/` depuis le NanoPi :

```
# cp ./deamon/S60_fand /etc/init.d/
```

Il faut ensuite rendre le script exécutable :

```
# chmod +x /etc/init.d/S60_fand
```

Maintenant, redémarrer le NanoPi pour que le script soit exécuté au démarrage ou lancez la commande suivante :

```
# /etc/init.d/S60_fand start
```

Pour vérifier que le deamon est bien lancé, lancer la commande `ps` :

```
# ps aux | grep fand
root       311  5.6  0.0   2132    80 ?        D    00:25   0:01 /usr/local/fand
```

Le processus `fand` est bien lancé.

## Utilisation du CLI

Une fois le deamon lancé, il est possible de contrôler le ventilateur depuis l'espace utilisateur via le CLI (Command Line Interface)

Le CLI se trouve dans le dossier `./cli` . Il contrôle le processus `fand` qui lui contrôle le ventilateur. Pour compiler le CLI, il faut lancer la commande :

```
$ cd src/07_miniproj/cli
$ make
```

La compilation engendre un fichier `fan_ctrl` qui est le CLI. Depuis le NanoPi, lancez la commande suivante pour voir les options du CLI :

```
# ./fan_ctrl
Usage: fan_ctrl [OPTION]...
   --mode=[manual | auto]     set the mode of the fan_driver
   --freq=value               set the frequency of the fan. The
                              fan_driver must be in "manual" mode
                              The value must be in range [1;50]
```

Il y a deux options disponibles :

- `--mode` : permet de changer le mode du driver. Il y a deux modes disponibles : `manual` et `auto`. Le mode `manual` permet de contrôler la vitesse du ventilateur manuellement. Le mode `auto` permet de contrôler la vitesse du ventilateur automatiquement en fonction de la température du CPU.

- `--freq` : permet de changer la fréquence du ventilateur. La fréquence doit être comprise entre 1 et 50. La fréquence est en Hz. La fréquence par défaut est de 10 Hz. La fréquence est utilisée uniquement lorsque le mode du driver est en `manual`.

Pour changer le mode du driver, il faut lancer la commande suivante :

```
# ./fan_ctrl --mode=manual
```

Pour changer la fréquence du ventilateur, il faut lancer la commande suivante :

```
# ./fan_ctrl --freq=10
```

Ou pour faire les deux en même temps :

```
# ./fan_ctrl --mode=auto
# ./fan_ctrl --mode=manual --freq=10
```

Sur l'écran du NanoPi, il est possible de voir la vitesse du ventilateur (Hz) et le mode du driver (manual ou auto) à chaque changement.

