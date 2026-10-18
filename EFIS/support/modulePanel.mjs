'use strict';

import { Container, Graphics, TextStyle, Text } from './pixi.mjs';
import { describeModule, WARNING, CAUTION } from './instrumentStatus.mjs';

export const VERSION = '1.0.0';

/*****************************************************************************
 * Module status page: one block per sensor module (IMU, AIR, GPS) showing its
 * state, version, uptime, how long since it was last heard and which sensors
 * it reports as working. Read only. Opened from the Menu.
 * (The EFIS itself is added when the server sends its own heartbeat.)
 ****************************************************************************/

const MODULES = [['imu', 'IMU'], ['air', 'AIR'], ['gps', 'GPS']];

const COLOURS = {
    ok: 0x7FFF00,                   // chartreuse, as the values on the display
    unknown: 0x888888,
};
COLOURS[CAUTION] = 0xFFB000;
COLOURS[WARNING] = 0xFF3B30;

export class ModulePanel {

    /*************************************************************************
     * @param {object} app - PixiJS Application (the primary canvas)
     *************************************************************************/
    constructor(app) {
        this.app = app;
        this.visible = false;
        this.blocks = {};           // module name -> the Text objects of its block

        const screenW = app.screen.width;
        const screenH = app.screen.height;

        const boxWidth = Math.min(500, screenW - 20);
        const titleHeight = 44;
        const blockHeight = 104;
        const closeHeight = 50;
        const padding = 10;
        const boxHeight = titleHeight + MODULES.length * blockHeight + closeHeight + 2 * padding;
        const boxX = (screenW - boxWidth) / 2;
        const boxY = Math.max(2, (screenH - boxHeight) / 2);

        // --- Root container (added/removed from stage on show/hide) ---
        this.overlay = new Container();

        // --- Dimmed background (full canvas, catches taps to dismiss) ---
        const bg = new Graphics();
        bg.fillStyle = { color: 0x000000, alpha: 0.88 };
        bg.rect(0, 0, screenW, screenH);
        bg.fill();
        bg.eventMode = 'static';
        bg.cursor = 'pointer';
        bg.on('pointerdown', (e) => {
            e.stopPropagation();
            this.hide();
        });
        this.overlay.addChild(bg);

        // --- Box ---
        const box = new Container();
        box.position.set(boxX, boxY);
        this.overlay.addChild(box);

        const boxBg = new Graphics();
        boxBg.fillStyle = { color: 0x222222, alpha: 0.97 };
        boxBg.strokeStyle = { color: 0x666666, width: 2, alpha: 1.0, alignment: 1 };
        boxBg.roundRect(0, 0, boxWidth, boxHeight, 8);
        boxBg.fill();
        boxBg.stroke();
        // Block taps from reaching the dismiss background
        boxBg.eventMode = 'static';
        boxBg.on('pointerdown', (e) => { e.stopPropagation(); });
        box.addChild(boxBg);

        // --- Title ---
        box.addChild(this.createText('Modules', boxWidth / 2, padding + titleHeight / 2, 22, 0xffffff, true, 0.5));

        // --- One block per module ---
        MODULES.forEach(([name, label], index) => {
            const y = padding + titleHeight + index * blockHeight;
            this.blocks[name] = this.createBlock(box, name, label, 10, y, boxWidth - 20, blockHeight - 6);
        });

        // --- Close row ---
        const closeY = padding + titleHeight + MODULES.length * blockHeight;
        const closeRow = new Container();
        closeRow.position.set(10, closeY);
        const closeBg = new Graphics();
        closeBg.fillStyle = { color: 0x333333, alpha: 0.9 };
        closeBg.roundRect(0, 0, boxWidth - 20, closeHeight - 6, 4);
        closeBg.fill();
        closeRow.addChild(closeBg);
        closeRow.addChild(this.createText('Close', (boxWidth - 20) / 2, (closeHeight - 6) / 2, 20, 0xbbbbbb, false, 0.5));
        closeRow.eventMode = 'static';
        closeRow.cursor = 'pointer';
        closeRow.on('pointerdown', (e) => {
            e.stopPropagation();
            this.hide();
        });
        box.addChild(closeRow);
    }

    /**
     * Build the block for one module and return the Text objects that change
     */
    createBlock(parent, name, label, x, y, width, height) {
        const block = new Container();
        block.position.set(x, y);
        parent.addChild(block);

        const bg = new Graphics();
        bg.fillStyle = { color: 0x333333, alpha: 0.9 };
        bg.roundRect(0, 0, width, height, 4);
        bg.fill();
        block.addChild(bg);

        const parts = {};
        // line 1: module name on the left, state on the right
        block.addChild(this.createText(label, 12, 18, 22, 0xffffff, true, 0));
        parts.state = this.createText('', width - 12, 18, 22, COLOURS.unknown, true, 1);
        block.addChild(parts.state);

        // line 2: version, uptime and when it was last heard
        parts.version = this.createText('', 12, 46, 16, 0xbbbbbb, false, 0);
        parts.uptime = this.createText('', width * 0.30, 46, 16, 0xbbbbbb, false, 0);
        parts.heard = this.createText('', width - 12, 46, 16, 0xbbbbbb, false, 1);
        block.addChild(parts.version);
        block.addChild(parts.uptime);
        block.addChild(parts.heard);

        // line 3: the sensors the module reports, then a restart note if there was one
        parts.sensors = [];
        let sensorX = 12;
        for (const sensor of describeModule(name, undefined).sensors) {
            const text = this.createText(sensor.label, sensorX, 76, 16, COLOURS.unknown, true, 0);
            block.addChild(text);
            parts.sensors.push({ key: sensor.key, text: text });
            sensorX += text.width + 30;      // room for the ' X' added when a sensor is lost
        }
        parts.restarted = this.createText('', width - 12, 76, 16, COLOURS[CAUTION], false, 1);
        block.addChild(parts.restarted);
        return parts;
    }

    createText(text, x, y, size, colour, bold, anchorX) {
        // a TextStyle is shared by reference, so every Text gets its own
        const t = new Text({
            text: text,
            style: new TextStyle({
                fontFamily: 'Tahoma',
                fontSize: size,
                fill: colour,
                fontWeight: bold ? 'bold' : 'normal',
            }),
        });
        t.anchor.set(anchorX, 0.5);
        t.position.set(x, y);
        return t;
    }

    /**
     * Update the texts from the server's module facts. Call every frame; does nothing while hidden.
     * @param {object} modules dataObject.modules (may be undefined before the first message)
     */
    update(modules) {
        if (!this.visible) return;
        for (const [name] of MODULES) {
            const parts = this.blocks[name];
            const d = describeModule(name, modules ? modules[name] : undefined);
            this.setText(parts.state, d.state, COLOURS[d.level]);
            this.setText(parts.version, d.version);
            this.setText(parts.uptime, d.uptime);
            this.setText(parts.heard, d.heard);
            this.setText(parts.restarted, d.restarted);
            d.sensors.forEach((sensor, i) => {
                const colour = sensor.ok === null ? COLOURS.unknown
                             : sensor.ok ? COLOURS.ok : COLOURS[WARNING];
                this.setText(parts.sensors[i].text, sensor.label + (sensor.ok === false ? ' X' : ''), colour);
            });
        }
    }

    /** Change a Text only when it differs, so Pixi does not redraw it every frame */
    setText(text, value, colour) {
        if (text.text !== value) text.text = value;
        if (colour !== undefined && text.style.fill !== colour) text.style.fill = colour;
    }

    show() {
        if (this.visible) return;
        this.app.stage.addChild(this.overlay);
        this.visible = true;
    }

    hide() {
        if (!this.visible) return;
        this.app.stage.removeChild(this.overlay);
        this.visible = false;
    }
}
