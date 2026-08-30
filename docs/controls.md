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

The center and lower half of the throttle axis produce zero throttle. The upper
half spans from zero to the configured 75% maximum throttle. This behavior
applies to the default half-stick configuration.

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
| Hold L3 | Roll and pitch only | Manually enter FC gestures while `LOCKED` |

Start only arms from `LOCKED` when throttle is zero. Press Start again to disarm.

B causes a local disarm. The TX sends a zero-throttle packet in the same RF cycle.

The TX keeps the selected flight mode, PID profile, and FC LED setting in memory
while locked, but transmits Acro with all AUX channels off to minimize disarmed
FC work. The selections are saved when changed and restored after a reboot.

After Start is pressed, the TX enters `PREARM_MODE`. It sends the saved mode,
PID profile, and FC LED setting with CH5 off, zero throttle, and centered
controls. With extended telemetry, full controls and CH5 are enabled only after
a fresh FC flight page confirms the mode and PID profile and reports that the FC
is still disarmed. Legacy telemetry cannot report these fields, so a fresh reply
instead confirms that the FC received a pre-arm packet. Moving throttle above
zero, pressing Start or B, losing the gamepad, or waiting two seconds without
confirmation cancels arming.

## FC gesture shortcuts

Gesture shortcuts work only while the TX is `LOCKED`. Hold R3 and press the
second control once. The TX enters the complete timed stick sequence for the FC.
Keep the aircraft still and level for commands that save or calibrate settings.

| Xbox shortcut | FC gesture | Rajawali action |
| --- | --- | --- |
| R3 + D-pad Up | Up-Up-Up | Toggle FC binding storage |
| R3 + D-pad Down | Down-Down-Down | Save FC settings; calibrate when no gesture-tuned values are pending |
| R3 + D-pad Left | Left-Left-Left | Toggle low-voltage forced landing |
| R3 + D-pad Right | Right-Right-Right | Toggle switchable feature 1; unused by the current FC configuration |
| R3 + A | Down-Up-Down | Toggle props-in/props-out yaw direction |
| R3 + B | Left-Left-Down | Exit stick-travel check |
| R3 + X | Right-Right-Down | Enter stick-travel check |
| R3 + Y | Up-Down-Up | Select the next PID term: P, I, or D |
| R3 + L3 | Up-Down-Down | Select the next PID axis: roll/pitch or yaw |
| R3 + RB | Up-Down-Right | Increase the selected PID value |
| R3 + LB | Up-Down-Left | Decrease the selected PID value |

For manual gestures, hold L3 and use the right stick. Release L3 afterward.
Manual and automatic gesture modes always force throttle and CH5 off.

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
