# PiAvionics plan

Agreed 2026-10-27. Item numbers are kept from the review so earlier references still work (gaps are dropped items).

## Scope decisions

- The backup display (Sensors/Backup_Display) is only a backup. It backs up both the Pi EFIS and the MGL EMS-2, which is the primary engine monitor.
- The Pi EFIS gets no Engine page. The conversion table (`Protocol/engine_conversions.yaml`, `Protocol/engine_convert.py`) stays as the reference for the backup display only.
- No EGT caution (the EMS-2 manual gives none). EGT has a high alarm only.
- No request for the uAvionix UCP ICD. The tailBeaconX serial research stays in RESUME.md as a note only.

## A. Engine alarms

1. Gate the low oil pressure alarm on RPM, so it stays quiet on a stopped engine. Needs an RPM source (item 10).
2. Volts limits and alarms: done in firmware 1.7.42 (low 11.0 alarm and 12.0 caution, high 14.8 caution and 15.5 alarm). Amps limits wait for the current sensor calibration (item 11) and the RPM gate (items 1 and 10): a discharge caution that applies only with the engine running.
3. Add a simulated high CHT/EGT mode, to test those alarms without probes.

## B. Backup display

4. Make units (psi/F) settable, or leave them fixed (the EMS-2 is the primary display).
5. Line up the G on the Flight page with HDG and ALT.

## D. Bench and hardware

9. Check the real VDO senders: oil T in hot water, oil pressure against a known pressure.
10. RPM source decided 2026-10-30: the alternator stator is the primary and the Littelfuse 55505 Hall geartooth sensor on the flywheel ring gear is the backup, one on each RDAC RPM input. Still to do:
    - Count the ring gear teeth (the toothed part is magnetic, so the sensor will see it). **Tooth count: not known yet.**
    - Measure the stator output on the bench before connecting it. The RDAC manual gives no maximum input voltage; add a series resistor if it is well above 35 V peak to peak.
    - Settings for both inputs: low gain, ballast off, filter off, sensitivity trimmer set well below the signal.
    - Hall wiring: black GND to the RDAC ground (engine block), white/black OUT to the RPM input (internal pull-up, no external one), red VDD to the 12 V supply (not `5VOU`: VDD minus 2 V is below the 4 V peak to peak low-gain minimum). Rated to 15 kHz and 2 mm air gap.
    - Bench-test that the RDAC counts the Hall pulse rate (teeth x rpm / 60) and that the flywheel magnets do not disturb the sensor.
    - Firmware: one pulses-per-rev value per input (the stator is 6) and a rule for choosing between the two, then the oil pressure gate (item 1). Enter both values in the EMS-2 setup (item 14).
11. Calibrate the LM335 and the current sensor (zero and gain).
12. Confirm the thermocouple-to-cylinder wiring.
13. Buy the EGT probes.
13a. Test and set the crowbar OVP module (`Crowbar_OVP/`, trimmer RV1). It must trip clearly above the 15.5 V high alarm, about 16 V, so the display warns before the bus is shorted. Not yet tested or set.
14. Set up the EMS-2 (including the oil T sender type VDO 150, so it agrees with the backup display). The checklist is `EMS2_SETUP.md`; do it when the EMS-2 is online.

## E. Docs

15. Update RESUME.md and the memory file with the alarms, acknowledge, CYL label, HDG move, start with PG selected, the dropped EGT caution and the scope decisions above.

## F. Parked

16. Aircraft test of the slip ball, horizon and G on both displays.
17. COM radio Feather module (GTR 205x, RS-232 `$PGRMC`).
19. Airport data source (OurAirports vs NASR). Waits on which airport David flies from.
20. Flight plan transfer from ForeFlight to the Pi at the aircraft. Needs a way to switch the Pi's local network on, and a fixed Samba share (the path is `/home/pi` but the user is `david`). Merges the old items 20 and 21.

## G. System test

22. Write and run the full system test table (this was "plan step 7"). It covers pulling each module's CAN cable and closing the Pi page, and checking that the failure detection and heartbeat bits respond correctly. Do it last, once the module set is final.

## Dropped

- 6 to 8: Main EFIS engine page.
- 18: tailBeaconX UCP ICD request.
