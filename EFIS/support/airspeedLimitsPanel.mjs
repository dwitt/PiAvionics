'use strict';

import { Container, Graphics, TextStyle, Text } from './pixi.mjs';

export const VERSION = '1.0.0';

/*****************************************************************************
 * Airspeed limits page (Menu > Airspeed Limits): the speeds that colour the speed tape, in knots, kept on the Pi
 * (aircraft_settings.py). Opened from the Menu on the controller (the Pi's own page).
 *
 *   edit      the five speeds; tap one (or Tab / arrows on a USB keyboard) and type with the keypad or keyboard.
 *             Only on the ground: in the air the speeds are shown but cannot be changed.
 *   confirm   the changes, old -> new, [Save] [Back]
 *   saving    waiting for the server's answer (it checks the speeds and the ground rule again, then saves)
 *   message   saved, or why not
 *
 * The same checks as the server (aircraft_settings.check_airspeed), so the page says what is wrong before Save.
 ****************************************************************************/

const FIELDS = [
    { name: 'vso', label: 'Vso', text: 'Stall, landing' },
    { name: 'vs1', label: 'Vs1', text: 'Stall, clean' },
    { name: 'vfe', label: 'Vfe', text: 'Max flaps extended' },
    { name: 'vno', label: 'Vno', text: 'Max structural cruise' },
    { name: 'vne', label: 'Vne', text: 'Never exceed' },
];
const MAX_KT = 300;             // as aircraft_settings.MAX_KT
const AIRBORNE_KT = 30;         // as aircraft_settings.AIRBORNE_KT: editing stops above this airspeed or ground speed
const MAX_DIGITS = 3;
const SAVE_WAIT_MS = 4000;      // give up waiting for the server's answer after this long

const CYAN = 0x00ffff;
const AMBER = 0xffb000;
const GREY = 0xbbbbbb;
const DIM = 0x666666;

export class AirspeedLimitsPanel {

    /*************************************************************************
     * @param {object} app - PixiJS Application (the primary canvas)
     * @param {function} sendCommand - sends a command object to the server
     *************************************************************************/
    constructor(app, sendCommand) {
        this.app = app;
        this.sendCommand = sendCommand;
        this.state = 'idle';
        this.saved = null;          // the limits when the page was opened
        this.values = {};           // what is being typed, as strings
        this.selected = 0;          // the field the keypad types into
        this.fresh = true;          // the next digit replaces the field (it was just selected)
        this.airborne = false;
        this.requestId = null;
        this.saveStartedAt = 0;
        this.answer = null;         // the server's answer to the save
        this.message = null;
        this.overlay = new Container();
        window.addEventListener('keydown', (e) => this.onKey(e));
    }

    /**
     * Open the page (from the Menu).
     * @param {{vso:number, vs1:number, vfe:number, vno:number, vne:number}} limits The limits in use
     */
    show(limits) {
        if (this.state !== 'idle' || !limits) return;
        this.saved = Object.fromEntries(FIELDS.map(f => [f.name, limits[f.name]]));
        this.values = Object.fromEntries(FIELDS.map(f => [f.name, String(limits[f.name])]));
        this.selected = 0;
        this.fresh = true;
        this.state = 'edit';
        this.render();
    }

    /**
     * Called every frame.
     * @param {number|null} airspeed Knots, null when not valid
     * @param {number|null} groundSpeed Knots, null when not valid
     * @param {object|null} saveAnswer dataObject.aircraft_save: {id, ok, reason}
     */
    update(airspeed, groundSpeed, saveAnswer) {
        if (this.state === 'idle') return;
        const airborne = [airspeed, groundSpeed].some(s => typeof s === 'number' && s > AIRBORNE_KT);
        if (airborne !== this.airborne) {
            this.airborne = airborne;
            if (this.state === 'edit' || this.state === 'confirm') this.render();
        }
        if (this.state === 'saving') {
            if (saveAnswer && saveAnswer.id === this.requestId) {
                this.showMessage(saveAnswer.ok, saveAnswer.reason);
            } else if (Date.now() - this.saveStartedAt > SAVE_WAIT_MS) {
                this.showMessage(false, 'No response from the server');
            }
        }
    }

    // ------------------------------------------------------------------ checks
    /** The speeds as numbers, or null if one is empty */
    numbers() {
        const n = {};
        for (const f of FIELDS) {
            if (this.values[f.name] === '') return null;
            n[f.name] = Number(this.values[f.name]);
        }
        return n;
    }

    /** Why the speeds cannot be saved, or null (aircraft_settings.check_airspeed) */
    problem() {
        for (const f of FIELDS) {
            if (this.values[f.name] === '') return f.label + ' is empty';
            const v = Number(this.values[f.name]);
            if (!(v > 0 && v < MAX_KT)) return f.label + ' must be 1 to ' + (MAX_KT - 1) + ' kt';
        }
        const { vso, vs1, vfe, vno, vne } = this.numbers();
        if (!(vso <= vs1 && vs1 < vno && vno < vne)) return 'Must be Vso <= Vs1 < Vno < Vne';
        if (!(vso < vfe && vfe <= vne)) return 'Must be Vso < Vfe <= Vne';
        return null;
    }

    changes() {
        const n = this.numbers();
        if (!n) return [];
        return FIELDS.filter(f => n[f.name] !== this.saved[f.name]);
    }

    // ------------------------------------------------------------------ actions
    select(index) {
        this.selected = (index + FIELDS.length) % FIELDS.length;
        this.fresh = true;
        this.render();
    }

    digit(d) {
        const name = FIELDS[this.selected].name;
        const current = this.fresh ? '' : this.values[name];
        if (current.length >= MAX_DIGITS) return;
        this.values[name] = (current + d).replace(/^0+(?=\d)/, '');
        this.fresh = false;
        this.render();
    }

    deleteDigit() {
        const name = FIELDS[this.selected].name;
        this.values[name] = this.fresh ? '' : this.values[name].slice(0, -1);
        this.fresh = false;
        this.render();
    }

    canSave() {
        return !this.airborne && this.problem() === null && this.changes().length > 0;
    }

    review() {
        if (!this.canSave()) return;
        this.state = 'confirm';
        this.render();
    }

    save() {
        if (!this.canSave()) return;
        this.requestId = Date.now();
        this.saveStartedAt = Date.now();
        this.sendCommand({ airspeed_limits: this.numbers(), request_id: this.requestId });
        this.state = 'saving';
        this.render();
    }

    back() {
        this.state = 'edit';
        this.render();
    }

    showMessage(ok, reason) {
        this.message = { ok: ok, reason: reason };
        this.state = 'message';
        this.render();
    }

    close() {
        this.state = 'idle';
        if (this.overlay.parent) this.overlay.parent.removeChild(this.overlay);
        this.clear();
    }

    /** A USB keyboard: digits, Backspace, Tab / arrows / Enter to move, Enter on the last speed to review, Escape */
    onKey(e) {
        if (this.state === 'idle') return;
        const key = e.key;
        if (this.state === 'edit') {
            if (key === 'Escape') this.close();
            else if (this.airborne) return;
            else if (/^[0-9]$/.test(key)) this.digit(key);
            else if (key === 'Backspace') this.deleteDigit();
            else if (key === 'ArrowUp' || (key === 'Tab' && e.shiftKey)) this.select(this.selected - 1);
            else if (key === 'ArrowDown' || key === 'Tab') this.select(this.selected + 1);
            else if (key === 'Enter') {
                if (this.selected === FIELDS.length - 1) this.review();
                else this.select(this.selected + 1);
            }
            else return;
        } else if (this.state === 'confirm') {
            if (key === 'Enter') this.save();
            else if (key === 'Escape') this.back();
            else return;
        } else if (this.state === 'message') {
            if (key === 'Enter' || key === 'Escape') this.close();
            else return;
        } else {
            return;
        }
        e.preventDefault();
    }

    // ------------------------------------------------------------------ drawing
    clear() {
        // destroyed after the tap that caused the redraw has finished with them
        const old = this.overlay.removeChildren();
        setTimeout(() => { for (const child of old) child.destroy({ children: true }); }, 0);
    }

    render() {
        this.clear();
        const w = this.app.screen.width;
        const h = this.app.screen.height;

        // Dimmed background; it swallows taps (a stray touch must not throw the edits away)
        const bg = new Graphics();
        bg.fillStyle = { color: 0x000000, alpha: 0.88 };
        bg.rect(0, 0, w, h);
        bg.fill();
        bg.eventMode = 'static';
        bg.on('pointerdown', (e) => { e.stopPropagation(); });
        this.overlay.addChild(bg);

        const boxWidth = Math.min(600, w - 20);
        const content = (this.state === 'edit') ? this.editContent(boxWidth) : this.rowsContent(boxWidth);
        const box = content.box;
        const boxHeight = content.height;
        box.position.set((w - boxWidth) / 2, Math.max(5, (h - boxHeight) / 2));

        const boxBg = new Graphics();
        boxBg.fillStyle = { color: 0x222222, alpha: 0.97 };
        boxBg.strokeStyle = { color: DIM, width: 2, alpha: 1.0, alignment: 1 };
        boxBg.roundRect(0, 0, boxWidth, boxHeight, 8);
        boxBg.fill();
        boxBg.stroke();
        box.addChildAt(boxBg, 0);
        this.overlay.addChild(box);

        this.app.stage.addChild(this.overlay);      // (re)adding brings it to the front
    }

    /** The edit page: the speeds on the left, the keypad on the right, a status line and Save / Cancel */
    editContent(boxWidth) {
        const box = new Container();
        const padding = 10;
        const inner = boxWidth - 2 * padding;
        const fieldWidth = Math.round(inner * 0.58);
        const keypadX = padding + fieldWidth + 10;
        const keypadWidth = boxWidth - padding - keypadX;
        const titleHeight = 44;
        const rowHeight = 50;
        const top = padding + titleHeight;
        const editable = !this.airborne;

        box.addChild(this.makeText('Airspeed Limits (kt)', boxWidth / 2, padding + titleHeight / 2, 22, 0xffffff, true, 0.5));

        // the speeds
        FIELDS.forEach((f, i) => {
            const row = new Container();
            row.position.set(padding, top + i * rowHeight);
            const selected = editable && i === this.selected;
            const rowBg = new Graphics();
            rowBg.fillStyle = { color: 0x333333, alpha: 0.95 };
            if (selected) rowBg.strokeStyle = { color: CYAN, width: 2, alpha: 1.0, alignment: 1 };
            rowBg.roundRect(0, 0, fieldWidth, rowHeight - 6, 4);
            rowBg.fill();
            if (selected) rowBg.stroke();
            row.addChild(rowBg);
            const mid = (rowHeight - 6) / 2;
            row.addChild(this.makeText(f.label, 12, mid, 20, 0xffffff, true, 0));
            row.addChild(this.makeText(f.text, 62, mid, 15, GREY, false, 0));
            const value = this.values[f.name];
            const shown = (selected && !this.fresh) ? value + '_' : (value === '' ? '---' : value);
            row.addChild(this.makeText(shown, fieldWidth - 12, mid, 22, editable ? CYAN : DIM, true, 1));
            if (editable) {
                row.eventMode = 'static';
                row.cursor = 'pointer';
                row.on('pointerdown', (e) => { e.stopPropagation(); this.select(i); });
            }
            box.addChild(row);
        });

        // the keypad
        const keys = ['1', '2', '3', '4', '5', '6', '7', '8', '9', 'Del', '0', 'Next'];
        const gap = 6;
        const keyWidth = (keypadWidth - 2 * gap) / 3;
        const keyHeight = (FIELDS.length * rowHeight - 6 - 3 * gap) / 4;
        keys.forEach((k, j) => {
            const x = keypadX + (j % 3) * (keyWidth + gap);
            const y = top + Math.floor(j / 3) * (keyHeight + gap);
            const onTap = (k === 'Del') ? () => this.deleteDigit()
                        : (k === 'Next') ? () => this.select(this.selected + 1)
                        : () => this.digit(k);
            box.addChild(this.makeButton({ text: k, enabled: editable, onTap: onTap, size: k.length > 1 ? 18 : 24 },
                x, y, keyWidth, keyHeight));
        });

        // status line: why it cannot be saved yet
        const statusY = top + FIELDS.length * rowHeight;
        const problem = this.problem();
        let status, colour;
        if (this.airborne) { status = 'Editing only on the ground'; colour = AMBER; }
        else if (problem) { status = problem; colour = AMBER; }
        else if (this.changes().length === 0) { status = 'Tap a speed, then type it'; colour = GREY; }
        else { status = this.changes().length + ' changed'; colour = GREY; }
        box.addChild(this.makeText(status, boxWidth / 2, statusY + 18, 18, colour, false, 0.5));

        // Save / Cancel
        const buttonsY = statusY + 36;
        const bw = (inner - 10) / 2;
        box.addChild(this.makeButton({ text: 'Save', accent: CYAN, enabled: this.canSave(), onTap: () => this.review() },
            padding, buttonsY, bw, rowHeight - 6));
        box.addChild(this.makeButton({ text: editable ? 'Cancel' : 'Close', onTap: () => this.close() },
            padding + bw + 10, buttonsY, bw, rowHeight - 6));

        return { box: box, height: buttonsY + rowHeight - 6 + padding };
    }

    /** The confirm, saving and message pages: rows of text and buttons, as the orientation panel */
    rowsContent(boxWidth) {
        const rows = [];
        if (this.state === 'confirm') {
            rows.push({ type: 'title', text: 'Save these limits?' });
            const n = this.numbers();
            for (const f of this.changes()) {
                rows.push({ type: 'text', text: f.label + '   ' + this.saved[f.name] + '  →  ' + n[f.name] + ' kt',
                            color: CYAN, size: 22 });
            }
            rows.push({ type: 'text', text: 'Check them against the flight manual.' });
            rows.push({ type: 'buttons', buttons: [
                { text: 'Save', accent: CYAN, enabled: this.canSave(), onTap: () => this.save() },
                { text: 'Back', onTap: () => this.back() },
            ] });
        } else if (this.state === 'saving') {
            rows.push({ type: 'title', text: 'Saving' });
            rows.push({ type: 'text', text: 'Please wait...' });
        } else {
            const ok = this.message && this.message.ok;
            rows.push({ type: 'title', text: ok ? 'Airspeed limits saved' : 'Airspeed limits not saved' });
            rows.push(ok ? { type: 'text', text: 'The speed tape now uses them.' }
                         : { type: 'text', text: (this.message && this.message.reason) || '', color: AMBER });
            rows.push({ type: 'button', text: 'OK', onTap: () => this.close() });
        }

        const box = new Container();
        const padding = 10;
        const rowHeight = 50;
        rows.forEach((row, i) => {
            const y = padding + i * rowHeight;
            if (row.type === 'title') {
                box.addChild(this.makeText(row.text, boxWidth / 2, y + rowHeight / 2, 22, 0xffffff, true, 0.5));
            } else if (row.type === 'text') {
                box.addChild(this.makeText(row.text, boxWidth / 2, y + rowHeight / 2, row.size || 18,
                    row.color || GREY, false, 0.5));
            } else if (row.type === 'buttons') {
                const count = row.buttons.length;
                const bw = (boxWidth - 20 - 10 * (count - 1)) / count;
                row.buttons.forEach((b, j) => box.addChild(this.makeButton(b, 10 + j * (bw + 10), y, bw, rowHeight - 6)));
            } else if (row.type === 'button') {
                box.addChild(this.makeButton(row, 10, y, boxWidth - 20, rowHeight - 6));
            }
        });
        return { box: box, height: rows.length * rowHeight + 2 * padding };
    }

    makeText(text, x, y, size, color, bold, anchorX) {
        const t = new Text({
            text: text,
            style: new TextStyle({
                fontFamily: 'Tahoma',
                fontSize: size,
                fill: color,
                fontWeight: bold ? 'bold' : 'normal',
            }),
        });
        t.anchor.set(anchorX, 0.5);
        t.position.set(x, y);
        return t;
    }

    makeButton(b, x, y, width, height) {
        const enabled = b.enabled !== false;
        const button = new Container();
        button.position.set(x, y);

        const bg = new Graphics();
        bg.fillStyle = { color: 0x333333, alpha: 0.95 };
        if (b.accent && enabled) bg.strokeStyle = { color: b.accent, width: 2, alpha: 1.0, alignment: 1 };
        bg.roundRect(0, 0, width, height, 4);
        bg.fill();
        if (b.accent && enabled) bg.stroke();
        button.addChild(bg);

        button.addChild(this.makeText(b.text, width / 2, height / 2, b.size || 20,
            enabled ? 0xffffff : DIM, false, 0.5));

        if (enabled) {
            button.eventMode = 'static';
            button.cursor = 'pointer';
            button.on('pointerdown', (e) => {
                e.stopPropagation();
                b.onTap();
            });
        }
        return button;
    }
}
