'use strict';

import { Container, Graphics, TextStyle, Text } from './pixi.mjs';

export const VERSION = '1.0.0';

/*****************************************************************************
 * Screen orientation panels (EFIS/docs/ORIENTATION_PLAN.md).
 *
 * The page never rotates anything itself. It asks the server (sendCommand) and
 * shows what the server reports in dataObject.display:
 *   choose    pick an orientation (opened from the Menu)
 *   confirm   "Switch to X?" [Switch] [Cancel]
 *   switching waiting for the server to start the trial
 *   trial     "Keep X?" [Keep] [Revert] with the server's countdown. Shown whenever
 *             the server reports a trial, also after the page reloads because the
 *             screen turned. Nothing is saved until Keep; the server reverts by itself.
 *   message   a short reason when the server refused (for example while flying)
 ****************************************************************************/

const LABELS = {
    portrait: 'Portrait',
    landscape: 'Landscape',
    portrait_flipped: 'Portrait (flipped)',
    landscape_flipped: 'Landscape (flipped)',
};
const REFUSAL_TEXT = {
    airborne: 'Not while flying',
    busy: 'A change is already being tried',
    already: 'Already in that orientation',
    unknown: 'Unknown orientation',
    failed: 'The screen could not be rotated',
};
const SWITCH_WAIT_MS = 4000;      // give up waiting for the server's answer after this long

const labelOf = (name) => LABELS[name] || name;

export class OrientationPanel {

    /*************************************************************************
     * @param {object} app - PixiJS Application (the primary canvas)
     * @param {function} sendCommand - sends a command object to the server
     *************************************************************************/
    constructor(app, sendCommand) {
        this.app = app;
        this.sendCommand = sendCommand;
        this.state = 'idle';
        this.display = null;            // dataObject.display from the server
        this.choice = null;             // the orientation being confirmed
        this.switchStartedAt = 0;
        this.message = '';
        this.countdownText = null;
        this.overlay = new Container();
        this.onClosed = null;           // called when the panel closes
    }

    /** Open the choose panel (from the Menu). */
    show() {
        if (this.state !== 'idle') return;
        this.state = 'choose';
        this.render();
    }

    /** Called every frame with the server's display facts (dataObject.display, may be null). */
    update(display) {
        this.display = display;
        if (!display) return;

        if (display.trial) {
            if (this.state !== 'trial') {
                this.state = 'trial';
                this.render();
            }
            if (this.countdownText) {
                const left = Math.max(0, Math.ceil(display.trial_seconds_left || 0));
                this.countdownText.text = 'Reverting in ' + left + ' s';
            }
            return;
        }
        if (this.state === 'trial') {
            // the trial ended: kept, reverted or timed out
            this.close();
            return;
        }
        if (this.state === 'switching') {
            const elapsed = (Date.now() - this.switchStartedAt) / 1000;
            if (display.refused && display.refused.age <= elapsed) {
                this.showMessage(REFUSAL_TEXT[display.refused.reason] || display.refused.reason);
            } else if (elapsed * 1000 > SWITCH_WAIT_MS) {
                this.showMessage('No response from the server');
            }
        }
    }

    // ------------------------------------------------------------------ actions
    choose(name) {
        this.choice = name;
        this.state = 'confirm';
        this.render();
    }

    confirmSwitch() {
        this.sendCommand({ orientation_request: this.choice });
        this.switchStartedAt = Date.now();
        this.state = 'switching';
        this.render();
    }

    keep() {
        this.sendCommand({ orientation_keep: true });
    }

    revert() {
        this.sendCommand({ orientation_revert: true });
    }

    showMessage(text) {
        this.message = text;
        this.state = 'message';
        this.render();
    }

    close() {
        this.state = 'idle';
        this.countdownText = null;
        if (this.overlay.parent) this.overlay.parent.removeChild(this.overlay);
        this.overlay.removeChildren();
        if (this.onClosed) this.onClosed();
    }

    // ------------------------------------------------------------------ drawing
    render() {
        this.countdownText = null;
        this.overlay.removeChildren();
        const w = this.app.screen.width;
        const h = this.app.screen.height;

        // Dimmed background; it swallows taps (the trial panel must not be dismissed by a stray touch)
        const bg = new Graphics();
        bg.fillStyle = { color: 0x000000, alpha: 0.88 };
        bg.rect(0, 0, w, h);
        bg.fill();
        bg.eventMode = 'static';
        bg.on('pointerdown', (e) => { e.stopPropagation(); });
        this.overlay.addChild(bg);

        const rows = this.contentFor(this.state);   // [{type, ...}]
        const boxWidth = Math.min(420, w - 20);
        const rowHeight = 50;
        const padding = 10;
        const boxHeight = rows.length * rowHeight + 2 * padding;
        const box = new Container();
        box.position.set((w - boxWidth) / 2, Math.max(5, (h - boxHeight) / 2));
        this.overlay.addChild(box);

        const boxBg = new Graphics();
        boxBg.fillStyle = { color: 0x222222, alpha: 0.97 };
        boxBg.strokeStyle = { color: 0x666666, width: 2, alpha: 1.0, alignment: 1 };
        boxBg.roundRect(0, 0, boxWidth, boxHeight, 8);
        boxBg.fill();
        boxBg.stroke();
        box.addChild(boxBg);

        rows.forEach((row, i) => {
            const y = padding + i * rowHeight;
            if (row.type === 'title') {
                box.addChild(this.makeText(row.text, boxWidth / 2, y + rowHeight / 2, 22, 0xffffff, true, 0.5));
            } else if (row.type === 'text') {
                const t = this.makeText(row.text, boxWidth / 2, y + rowHeight / 2, 18, row.color || 0xbbbbbb, false, 0.5);
                if (row.countdown) this.countdownText = t;
                box.addChild(t);
            } else if (row.type === 'buttons') {
                const count = row.buttons.length;
                const gap = 10;
                const bw = (boxWidth - 20 - gap * (count - 1)) / count;
                row.buttons.forEach((b, j) => {
                    box.addChild(this.makeButton(b, 10 + j * (bw + gap), y, bw, rowHeight - 6));
                });
            } else if (row.type === 'button') {
                box.addChild(this.makeButton(row, 10, y, boxWidth - 20, rowHeight - 6));
            }
        });

        this.app.stage.addChild(this.overlay);      // (re)adding brings it to the front
    }

    contentFor(state) {
        const current = this.display ? this.display.orientation : null;
        const saved = this.display ? this.display.saved : null;
        const options = (this.display && this.display.options) || Object.keys(LABELS);

        if (state === 'choose') {
            const rows = [{ type: 'title', text: 'Display orientation' }];
            for (const name of options) {
                const isNow = name === current && name === saved;
                rows.push({
                    type: 'button',
                    text: labelOf(name) + (isNow ? '  (current)' : ''),
                    enabled: !isNow,
                    onTap: () => this.choose(name),
                });
            }
            rows.push({ type: 'button', text: 'Cancel', onTap: () => this.close() });
            return rows;
        }
        if (state === 'confirm') {
            return [
                { type: 'title', text: 'Switch to ' + labelOf(this.choice) + '?' },
                { type: 'text', text: 'The screen will rotate and reload.' },
                { type: 'text', text: 'You will then be asked to keep it.' },
                { type: 'buttons', buttons: [
                    { text: 'Switch', accent: 0x00ffff, onTap: () => this.confirmSwitch() },
                    { text: 'Cancel', onTap: () => this.close() },
                ] },
            ];
        }
        if (state === 'switching') {
            return [
                { type: 'title', text: 'Switching to ' + labelOf(this.choice) },
                { type: 'text', text: 'Please wait...' },
            ];
        }
        if (state === 'trial') {
            const trial = this.display ? this.display.trial : '';
            const left = this.display ? Math.max(0, Math.ceil(this.display.trial_seconds_left || 0)) : 0;
            return [
                { type: 'title', text: 'Keep ' + labelOf(trial) + '?' },
                { type: 'text', text: 'Reverting in ' + left + ' s', countdown: true, color: 0xffb000 },
                { type: 'buttons', buttons: [
                    { text: 'Keep', accent: 0x00ffff, onTap: () => this.keep() },
                    { text: 'Revert', onTap: () => this.revert() },
                ] },
            ];
        }
        // message
        return [
            { type: 'title', text: 'Orientation not changed' },
            { type: 'text', text: this.message, color: 0xffb000 },
            { type: 'button', text: 'OK', onTap: () => this.close() },
        ];
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

        button.addChild(this.makeText(b.text, width / 2, height / 2, 20,
            enabled ? 0xffffff : 0x666666, false, 0.5));

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
