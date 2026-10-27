# PiAvionics plan

Agreed 2026-10-27. Item numbers are kept from the review so earlier references still work (gaps are dropped items).

## Scope decisions

- The backup display (Sensors/Backup_Display) is only a backup. It backs up both the Pi EFIS and the MGL EMS-2, which is the primary engine monitor.
- The Pi EFIS gets no Engine page. The conversion table (`Protocol/engine_conversions.yaml`, `Protocol/engine_convert.py`) stays as the reference for the backup display only.
- No EGT caution (the EMS-2 manual gives none). EGT has a high alarm only.
- No request for the uAvionix UCP ICD. The tailBeaconX serial research stays in RESUME.md as a note only.

## A. Engine alarms

1. Gate the low oil pressure alarm on RPM, so it stays quiet on a stopped engine. Needs an RPM source (item 10).
2. Add volts and amps limits and alarms. The limits are not chosen yet.
3. Add a simulated high CHT/EGT mode, to test those alarms without probes.

## B. Backup display

4. Make units (psi/F) settable, or leave them fixed (the EMS-2 is the primary display).
5. Line up the G on the Flight page with HDG and ALT.

## D. Bench and hardware

9. Check the real VDO senders: oil T in hot water, oil pressure against a known pressure.
10. Choose the RPM source (Magnatron P-lead or Littelfuse Hall geartooth) and set pulses per revolution.
11. Calibrate the LM335 and the current sensor (zero and gain).
12. Confirm the thermocouple-to-cylinder wiring.
13. Buy the EGT probes.
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
