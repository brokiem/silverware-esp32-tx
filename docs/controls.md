# Gamepad controls

[Project overview](../README.md)

> [!WARNING]
> Test each control without propellers before the first flight.

## Sticks

| Control | Function |
| --- | --- |
| Left stick up | Throttle |
| Left stick left or right | Yaw |
| Right stick up or down | Pitch |
| Right stick left or right | Roll |

The center and lower half of the throttle axis produce zero throttle. This behavior applies to the default half-stick configuration.

## Buttons

| Button | Bayang channel | Function |
| --- | --- | --- |
| Start | CH5 | Arm and idle-up while active |
| View | Bind packet | Start binding from `LOCKED` |
| B | None | Disarm the TX immediately |
| A | CH6 | Toggle level mode |
| X | CH7 | Toggle race mode |
| Y | CH8 | Toggle horizon mode |
| RB | CH9 | Toggle the PID profile |
| LB | CH10 | Toggle the FC LED channel |
| Hold L3 | Pitch only | Enable FC gestures while `LOCKED` |

Start only arms from `LOCKED` when throttle is zero. Press Start again to disarm.

B causes a local disarm. The TX sends a zero-throttle packet in the same RF cycle.

The TX keeps the selected flight mode, PID profile, and FC LED setting while
locked. CH5 and throttle remain off, so preserving these settings cannot arm the
aircraft. A reboot resets the selections to their defaults.

## Rajawali flight modes

The FC requires level mode before it applies race mode or horizon mode.

| Level | Race | Horizon | FC mode |
| --- | --- | --- | --- |
| Off | Any | Any | Acro mode |
| On | Off | Off | Level mode |
| On | On | Off | Race mode |
| On | Off | On | Horizon mode |
| On | On | On | Combined race-horizon mode |

The TX uses the `USE_MULTI` Bayang channel layout. Do not enable analog AUX on only one device.
