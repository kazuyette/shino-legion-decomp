Shinobi Legions PC - v@VERSION@ (Windows 64-bit)
==================================================

Unofficial PC port of the Sega Saturn game Shinobi Legions (USA, 1995).
NO GAME DATA IS INCLUDED: you need your own disc image (see below).

------------------------------------------------------------------------------
ENGLISH
------------------------------------------------------------------------------

1. Getting the game files (once)
   You need a dump of YOUR OWN Shinobi Legions (USA) disc (product T-2301H) as
   a .cue + .bin image (one .bin or one .bin per track). A plain .iso works too,
   but it has no CD audio, so the title / ending music will be missing.

   Either:
   a) copy the .cue and its .bin file(s) next to shinobi_legions_pc.exe and start
      the game: it offers to extract the files (answer Yes), or
   b) drag the .cue file (or the .bin / .iso) onto shinobi_legions_pc.exe, or
   c) from a command prompt:
        shinobi_legions_pc.exe --extract "D:\Games\Shinobi Legions (USA).cue"

   The files (about 400 MB) are written to the "assets" folder next to the
   executable and checked against checksums of the original disc. You can
   delete the disc image afterwards. Messages:
   - "wrong disc" / "other region": only the USA release is supported (not the
     Japanese Shinobiden nor the European Shinobi X);
   - "bad dump": the image is damaged or incomplete; the files that did not
     match are named, dump the disc again.

2. Controls (defaults, change them in shinobi.ini)
   Action               Saturn   Keyboard        Controller (Xbox / PlayStation)
   Move / crouch        D-pad    Arrows          D-pad or left stick
   Sword (hold: guard)  B        X               X / Square
   Jump (hold: higher)  C        C               A / Cross
   Shuriken             A        Z               B / Circle
   Ninjutsu             X        A               Y / Triangle
   Guard                Y        S               LB / L1
   Start                Start    Enter           Start
   Keyboard names are key positions on a US keyboard (on AZERTY, "Z" is the
   key labelled W, "A" the key labelled Q).

   PC keys: F1 (or controller Back/Select) = debug / cheat menu (pauses the
   game: lives, energy, shurikens, ninjutsu, stage select...);
   Alt+Enter or F11 = fullscreen; F12 = screenshot (screenshots folder);
   quit by closing the window. A+B+C+Start = soft reset, as on the Saturn.

3. Settings: shinobi.ini
   Created next to the executable on the first start, with every option
   commented: window scale, fullscreen, vsync, smoothing, 4:3 aspect, volumes,
   screenshot format, keyboard and controller bindings. Delete it to get the
   defaults back.

------------------------------------------------------------------------------
FRANCAIS
------------------------------------------------------------------------------

1. Obtenir les fichiers du jeu (une seule fois)
   Il faut une copie de VOTRE PROPRE disque Shinobi Legions (USA) (reference
   T-2301H) sous forme d'image .cue + .bin (un seul .bin ou un .bin par piste).
   Une image .iso simple fonctionne aussi, mais sans pistes audio CD : la
   musique du titre et de la fin manquera.

   Au choix :
   a) copiez le .cue et son ou ses .bin a cote de shinobi_legions_pc.exe et
      lancez le jeu : il propose d'extraire les fichiers (repondez Yes/Oui) ;
   b) faites glisser le fichier .cue (ou le .bin / .iso) sur
      shinobi_legions_pc.exe ;
   c) en ligne de commande :
        shinobi_legions_pc.exe --extract "D:\Jeux\Shinobi Legions (USA).cue"

   Les fichiers (environ 400 Mo) sont ecrits dans le dossier "assets" a cote de
   l'executable et verifies (sommes de controle du disque original). L'image
   disque peut ensuite etre supprimee. Messages :
   - "wrong disc" / "other region" : seule la version USA est prise en charge
     (ni Shinobiden japonais, ni Shinobi X europeen) ;
   - "bad dump" : image abimee ou incomplete ; les fichiers en cause sont
     nommes, refaites l'image du disque.

2. Commandes (par defaut, modifiables dans shinobi.ini)
   Action                    Saturn  Clavier          Manette (Xbox / PlayStation)
   Deplacement / accroupi    Croix   Fleches          Croix ou stick gauche
   Sabre (maintenu : garde)  B       X                X / Carre
   Saut (maintenu : + haut)  C       C                A / Croix
   Shuriken                  A       Z                B / Rond
   Ninjutsu                  X       A                Y / Triangle
   Garde                     Y       S                LB / L1
   Start                     Start   Entree           Start
   Les noms de touches sont des positions de clavier US : sur AZERTY, "Z" est
   la touche marquee W et "A" la touche marquee Q.

   Touches PC : F1 (ou Back/Select de la manette) = menu debug / triche (met le
   jeu en pause : vies, energie, shurikens, ninjutsu, choix du niveau...) ;
   Alt+Entree ou F11 = plein ecran ; F12 = capture d'ecran (dossier
   screenshots) ; on quitte en fermant la fenetre. A+B+C+Start = reset comme
   sur la Saturn.

3. Reglages : shinobi.ini
   Cree a cote de l'executable au premier lancement, chaque option commentee :
   taille de fenetre, plein ecran, vsync, lissage, format 4:3, volumes, format
   des captures, touches clavier et manette. Supprimez-le pour revenir aux
   valeurs par defaut.
