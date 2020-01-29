[![Actions Status](https://github.com/lefty01/scrum-timer/workflows/test/badge.svg)](https://github.com/lefty01/scrum-timer/actions)

# scrum-timer
esp8266 (wemos D1 min) based scrum timer with LCD display and RGB light-strip or ring.

## Usage
* Press 'Menu' button to cycle through input menu (Persons or Duration of Meeting)
* Enter number of Persons (default=5) using up/down button
* Enter total scrum meeting runtime in minutes (default=15) using up/down button
* Press Start/NXT Button to start timer. If times up for a person you get 'TIMEOUT' indication
* Press Start/NXT before or after timeout to start with next person.
* Press Menu to abort scrum round and allow to enter new values for persons and/or duration


### Buttons: Menu/Rest, Down/decrease, Up/increase, Start/Next

## OTA
esp is now started in wifi "AP" mode, so you cann connect to it and then perform over-the-air updates.

This was tested in a wemos D1 mini (esp8266) with arduino IDE 1.8.10 and esp8266 sdk version 2.6.3.
As board in this case select: LOLIN(WEMOS) D1 R2 & mini, for 'Flash Size' select: '4MB (FS: 1MB OTA:~1019KB)'
