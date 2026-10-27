# MGL Blaze EMS-2 setup checklist

Drafted 2026-10-27 from the Blaze EMS-2 Operating Manual (`~/Documents/_E-Equipment_Manuals/MGL Avionics/Blaze EMS-2/Blaze EMS-2.pdf`, sections 4.x and 11.14). The EMS-2 was not online when this was written, so the menu names come from the manual and have not been checked on the unit. Tick each line as it is done. Limits are the same as the backup display's (`Protocol/engine_conversions.yaml` and the sketch), so the two displays agree.

## Before you start

- Press the rotary control during normal display to enter the menu. Turn it to move, press to select.
- Press F1/Up at "EXIT" to leave. Changes are saved only on exit (removing power first loses them).
- The EMS-2 reads the RDAC only where a menu says **Data = RDAC**. There is no global switch, so it must be set in every menu below.
- Wherever **Data = RDAC** is set, also set **RDAC Address = 32** (the address of our RDAC, SEL A and B off).
- Our RDAC and the EMS-2 share the CAN bus. The EMS-2 only listens.
- It is worth writing down the factory settings (MISC, Information) first. Do not use "Default Settings" unless you want to wipe everything.

## 1. Analog channels (4.6)

RDAC mapping: Channel 1 = RDAC OIL T, Channel 2 = RDAC OIL P, Channel 3 = RDAC AUX1, Channel 4 = RDAC AUX2.

### Channel 1: oil temperature (VDO 323-055, 150 C)

- [ ] Mode = TEMP
- [ ] Sender = **VDO 150** (not VDO 120)
- [ ] Label = `OIL T`
- [ ] Temp Unit = F
- [ ] High Alarm on, 240 F
- [ ] High Caution 230 F
- [ ] Low Caution and Low Alarm: not set (the backup display has none). The AeroVee manual says 160 F minimum for take-off, so decide whether to add a low caution.
- [ ] Display Min and Max: your choice
- [ ] Data = RDAC, RDAC Address = 32

### Channel 2: oil pressure (VDO 360-001, marked 80PS, 5 bar)

- [ ] Mode = PRESSURE
- [ ] Type = RESISTIVE
- [ ] Sender = VDO
- [ ] Model = **5 Bar**
- [ ] Label = `OIL P`
- [ ] Unit = psi
- [ ] Low Alarm on, 10 psi
- [ ] Low Caution 20 psi
- [ ] High Alarm: your choice (AeroVee maximum is 100 psi). The backup display has no high oil pressure limit.
- [ ] Display Min and Max: your choice
- [ ] Data = RDAC, RDAC Address = 32

### Channel 3: AUX1, LM335 outside air temperature

- [ ] Mode = TEMP
- [ ] Sender = **LM335**
- [ ] Label = `OAT`
- [ ] Temp Unit: your choice
- [ ] Calibrate: with the probe at a known ambient temperature, adjust the LM335 value until it matches (the value is in C even if the display is in F)
- [ ] Data = RDAC, RDAC Address = 32

### Channel 4: AUX2

- [ ] Mode = OFF (nothing connected)

## 2. Thermocouples (4.5)

Type K, four EGT on RDAC TC1 to TC4 and four CHT on TC5 to TC8. The manual lists separate EGT and CHT setup menus, so do both.

### EGT

- [ ] Channels = 4
- [ ] Probe = K
- [ ] Temp Unit = F
- [ ] High Alarm on, 1400 F
- [ ] High Caution: **not set** (decided 2026-10-26: the backup display has no EGT caution)
- [ ] Display = HIGHEST (the number of the hottest cylinder is highlighted)
- [ ] Display Min and Max: your choice
- [ ] Data = RDAC, RDAC Address = 32

### CHT

- [ ] Channels = 4
- [ ] Probe = K
- [ ] Temp Unit = F
- [ ] High Alarm on, 450 F
- [ ] High Caution 420 F
- [ ] Display = HIGHEST
- [ ] Display Min and Max: your choice
- [ ] Data = RDAC, RDAC Address = 32

Check on the unit: that EGT1 to 4 show TC1 to TC4 and CHT1 to 4 show TC5 to TC8 (this matched on the bench on 2026-10-25), and how the 4 + 4 split is chosen if it is not done by the two menus.

## 3. RPM (4.3)

- [ ] Display = RPM
- [ ] High Alarm on, 4000 rpm
- [ ] High Caution 3800 rpm
- [ ] Low Alarm / Low Caution: your choice (the AeroVee idle is 800 to 900)
- [ ] **Pulses/Rev: open.** It depends on the RPM source chosen (PLAN item 10). Magneto P-lead on a four-cylinder four-stroke gives 2 pulses per revolution. A flywheel Hall sensor depends on the tooth count.
- [ ] Increment: your choice
- [ ] Label = `RPM`
- [ ] Data = RDAC, RDAC Address = 32
- [ ] RPM 2 / Fuel Flow: Mode = OFF
- [ ] Flight timer: FLIGHT = RPM1 and set T/O RPM (manual 4.2, recommended automatic detection)

## 4. Volts (4.7)

- [ ] Display: numeric or bargraph, your choice
- [ ] Label = `VOLTS`
- [ ] Limits: **open.** The backup display has no volts limits yet (PLAN item 2); choose them once, then set both displays to match.
- [ ] Cal: with a meter on the battery, adjust until the EMS-2 matches (RDAC volts scale is raw / 57.3758)

## 5. Manifold pressure (4.8)

- [ ] Display = OFF (the AeroVee is not turbocharged, and we do not display MAP). If you want it, calibrate Cal in mB against the barometric pressure.

## 6. Current (4.9, RDAC and the MGL closed loop current sensor)

- [ ] Display on
- [ ] Label = `AMPS`
- [ ] Zero Sensor with the sensor off the conductor
- [ ] Gain: adjust until it matches a meter in series (gain 140 for plus or minus 24 A with the conductor folded once, see the sensor documentation)
- [ ] Limits: open (same as volts)
- [ ] RDAC Address = 32
- [ ] Do the same zero and gain calibration for our backup display later (PLAN item 11)

## 7. Fuel level, ECU, COMM, timers, misc

- [ ] Fuel level and fuel flow: not used (gravity fed AeroVee, no inputs)
- [ ] ECU: not used
- [ ] COMM (4.12): Serial Out = OFF unless we decide to read it. The RS-232 output is 8N1.
- [ ] Timers (4.10): set UTC time and UTC offset, and set the Hobbs to the engine's real time
- [ ] MISC (4.13): backlight (manual or automatic); security password only if wanted

## 8. After setup: check the two displays agree

- [ ] Resistor on OIL P (for example 3 and 4 parallel 120 ohm as in the backup display bench test): both displays show the same pressure and the same caution and alarm
- [ ] OIL T: a 120 ohm resistor reads about 171 F on the backup display, so the EMS-2 should read the same once the sender is VDO 150
- [ ] Thermocouples read the cold junction (room temperature) with nothing connected, as on the backup display. They give no open-probe warning.
- [ ] Volts match a meter on both displays
- [ ] Alarms are active only 10 s after power-up on both displays

## Open items needing a decision

- Volts and amps limits (also PLAN item 2)
- RPM pulses per revolution (PLAN item 10)
- Low oil temperature caution
- High oil pressure alarm
- Display min and max for each bar
