// Editable text on the 96 x 73 handset LCD: one model for every field (sound prompt, licence code, sample name).
//
// A hidden <textarea> keeps the text, the selection and the undo history, and edits it natively: typing,
// Backspace/Delete, Shift/Ctrl+arrows, Ctrl+A/C/X/V/Z/Y. This class adds what a bitmap screen needs on top:
// line layout, caret and selection geometry, line-wise cursor keys (the textarea itself is 1 px wide, so its own
// Up/Down/Home/End would be meaningless), scrolling, and mouse selection (drag, double-click word,
// triple-click line). If the browser refuses to focus the textarea (some DAW hosts), handleKey() edits the
// text by hand instead so the field never goes dead.
//
// Drawing relies on the handset renderer in app.js (ctx, getColor, drawString, measureString, FONTS).
class LcdTextField {
    constructor(input, { maxLength = 450, multiline = false, normalize = null, onChange = () => {} } = {}) {
        this.input = input;
        this.maxLength = maxLength;
        this.multiline = multiline;
        this.normalize = normalize;
        this.onChange = onChange;
        this.scroll = 0;      // first visible line (multi-line)
        this.scrollX = 0;     // horizontal scroll in pixels (single line)
        this.goalX = null;    // remembered column for repeated Up/Down
        this.caretOn = true;
        this.geo = null;      // geometry of the last draw(), used to turn pointer positions into indexes
        this.dragging = false;
        this.dragAnchor = 0;
        this.clicks = 0;
        this.lastClickAt = 0;
        this.lastClickIndex = -1;
        input.addEventListener('input', () => { this.sanitise(); this.goalX = null; this.touch(); this.onChange(); });
        input.addEventListener('select', () => { this.touch(); this.onChange(); });
    }

    // ---- model -----------------------------------------------------------------------------------
    // maxLength is a number, or a function of the current text.
    limit(text = this.value) { return typeof this.maxLength === 'function' ? this.maxLength(text) : this.maxLength; }
    get value() { return this.input.value; }
    get selStart() { return this.input.selectionStart ?? 0; }
    get selEnd() { return this.input.selectionEnd ?? this.selStart; }
    get hasSelection() { return this.selEnd > this.selStart; }
    get backward() { return this.input.selectionDirection === 'backward'; }
    get caret() { return this.backward ? this.selStart : this.selEnd; }
    get anchor() { return this.backward ? this.selEnd : this.selStart; }

    setSelection(anchor, focus) {
        const length = this.value.length;
        const a = Math.max(0, Math.min(length, anchor)), f = Math.max(0, Math.min(length, focus));
        this.input.setSelectionRange(Math.min(a, f), Math.max(a, f), a <= f ? 'forward' : 'backward');
    }
    selectAll() { this.setSelection(0, this.value.length); this.touch(); }
    touch() { this.caretOn = true; }
    focus() { if (document.activeElement !== this.input) this.input.focus({ preventScroll: true }); }

    setValue(text, selectAll = false) {
        this.input.value = text;
        this.sanitise();
        this.scroll = 0; this.scrollX = 0; this.goalX = null;
        if (selectAll) this.selectAll(); else this.setSelection(this.value.length, this.value.length);
        this.touch();
    }
    sanitise() {
        const before = this.input.selectionStart ?? 0;
        let text = this.input.value;
        if (this.normalize) text = this.normalize(text);
        // The limit can depend on the text itself (the sound prompt shares 450 characters with its system prompt).
        for (let pass = 0; pass < 8 && text.length > this.limit(text); pass++) text = text.slice(0, Math.max(0, this.limit(text)));
        if (text !== this.input.value) {
            this.input.value = text;
            const position = Math.min(before, text.length);
            this.input.setSelectionRange(position, position);
        }
    }

    // ---- layout ----------------------------------------------------------------------------------
    // Lines as {start,end} index ranges. Words wrap at spaces; a word wider than the field is broken.
    layoutLines(width, font) {
        const text = this.value;
        if (!this.multiline) return [{ start: 0, end: text.length }];
        const lines = [];
        let start = 0;
        while (start < text.length) {
            let end = start, lastSpace = -1, used = 0;
            while (end < text.length) {
                const next = used + (end > start ? font.spacing : 0) + font.getGlyph(text[end]).w;
                if (next > width) break;
                used = next; end++;
                if (text[end - 1] === ' ') lastSpace = end;
            }
            if (end < text.length) {
                if (lastSpace > start) end = lastSpace;
                else if (end === start) end = start + 1;
            }
            lines.push({ start, end });
            start = end;
        }
        if (!lines.length) lines.push({ start: 0, end: 0 });
        return lines;
    }
    // The caret at a wrap boundary belongs to the start of the next line, except at the very end.
    lineOf(layout, index) {
        for (let i = 0; i < layout.length - 1; i++) if (index < layout[i].end) return i;
        return layout.length - 1;
    }
    // Pixel offset of the start of character `index` within its line.
    xAt(line, index, font) {
        if (index <= line.start) return 0;
        return measureString(this.value.slice(line.start, index), font) + font.spacing;
    }
    indexAtX(layout, lineNumber, x, font) {
        const line = layout[lineNumber], text = this.value;
        const limit = lineNumber < layout.length - 1 ? line.end - 1 : line.end; // cannot land after a wrap point
        let index = line.start, position = 0;
        while (index < limit) {
            const width = font.getGlyph(text[index]).w;
            if (x < position + width / 2) break;
            position += width + font.spacing;
            index++;
        }
        return Math.max(line.start, index);
    }
    ensureVisible(layout, visibleLines) {
        const line = this.lineOf(layout, this.caret);
        if (line < this.scroll) this.scroll = line;
        else if (line >= this.scroll + visibleLines) this.scroll = line - visibleLines + 1;
        this.scroll = Math.max(0, Math.min(Math.max(0, layout.length - visibleLines), this.scroll));
    }
    ensureVisibleX(width, font) {
        const whole = this.value.length ? measureString(this.value, font) + font.spacing : 0;
        const caretX = this.xAt({ start: 0 }, this.caret, font);
        if (caretX < this.scrollX) this.scrollX = caretX;
        else if (caretX > this.scrollX + width - 2) this.scrollX = caretX - width + 2;
        this.scrollX = Math.max(0, Math.min(Math.max(0, whole - width + 2), this.scrollX));
    }

    // ---- movement --------------------------------------------------------------------------------
    wordAt(index) {
        const text = this.value;
        const probe = index < text.length ? text[index] : text[index - 1];
        if (probe === undefined) return [index, index];
        const kind = ch => /[A-Za-z0-9_À-ɏ]/.test(ch) ? 'w' : /\s/.test(ch) ? 's' : 'p';
        const want = kind(probe);
        let a = index < text.length ? index : index - 1, b = a + 1;
        while (a > 0 && kind(text[a - 1]) === want) a--;
        while (b < text.length && kind(text[b]) === want) b++;
        return [a, b];
    }
    prevWord(index) {
        const text = this.value;
        let i = index;
        while (i > 0 && /\s/.test(text[i - 1])) i--;
        const [start] = this.wordAt(Math.max(0, i - 1));
        return i > 0 ? start : 0;
    }
    nextWord(index) {
        const text = this.value;
        let i = index;
        if (i < text.length) { const [, end] = this.wordAt(i); i = end; }
        while (i < text.length && /\s/.test(text[i])) i++;
        return i;
    }
    // Home/End/Up/Down/PageUp/PageDown, with Shift to extend and Ctrl for the text ends.
    move(key, { shift = false, ctrl = false } = {}, layout, visibleLines) {
        const text = this.value, length = text.length, here = this.caret, anchor = this.anchor;
        const font = this.geo ? this.geo.font : FONTS.Regular;
        const lineNumber = this.lineOf(layout, here), line = layout[lineNumber];
        let target = here, vertical = false;
        if (key === 'Home') target = ctrl ? 0 : line.start;
        else if (key === 'End') {
            target = ctrl ? length : lineNumber < layout.length - 1 ? line.end - (text[line.end - 1] === ' ' ? 1 : 0) : length;
        } else {
            const up = key === 'ArrowUp' || key === 'PageUp';
            if (!this.multiline) target = up ? 0 : length;
            else {
                vertical = true;
                const step = key === 'PageUp' || key === 'PageDown' ? Math.max(1, visibleLines - 1) : 1;
                if (this.goalX === null) this.goalX = this.xAt(line, here, font);
                const next = lineNumber + (up ? -step : step);
                target = next < 0 ? 0 : next >= layout.length ? length : this.indexAtX(layout, next, this.goalX, font);
            }
        }
        if (!vertical) this.goalX = null;
        this.setSelection(shift ? anchor : target, target);
        this.touch();
    }

    // Returns true when the key was fully handled here (the caller should preventDefault). Returning false
    // means the textarea edits natively; the caller should redraw after the browser has done it.
    handleKey(e, layoutWidth, visibleLines, font = FONTS.Regular) {
        const ctrl = e.ctrlKey || e.metaKey;
        const layout = this.layoutLines(layoutWidth, font);
        if (['ArrowUp', 'ArrowDown', 'Home', 'End', 'PageUp', 'PageDown'].includes(e.key) && !e.altKey) {
            this.focus();
            this.move(e.key, { shift: e.shiftKey, ctrl }, layout, visibleLines);
            return true;
        }
        if (ctrl && !e.altKey && e.key.toLowerCase() === 'a') { this.focus(); this.selectAll(); return true; }
        this.focus();
        if (document.activeElement !== this.input) return this.manualKey(e);
        this.goalX = null; this.touch();
        return false;
    }

    // Fallback editing when the textarea cannot take focus.
    manualKey(e) {
        const ctrl = e.ctrlKey || e.metaKey, key = e.key, start = this.selStart, end = this.selEnd, length = this.value.length;
        const replace = (text, from = start, to = end) => {
            this.input.setRangeText(text, from, to, 'end');
            this.sanitise(); this.goalX = null; this.touch(); this.onChange();
        };
        if (ctrl) {
            const letter = key.toLowerCase();
            if (letter === 'c' || letter === 'x') {
                const text = this.value.slice(start, end);
                if (text) { try { navigator.clipboard.writeText(text); } catch (_) {} if (letter === 'x') replace(''); }
                return true;
            }
            if (letter === 'v') {
                try {
                    navigator.clipboard.readText().then(text => {
                        if (text) replace(this.multiline ? text.replace(/\s+/g, ' ') : text.replace(/[\r\n\t]+/g, ' '));
                    }).catch(() => {});
                } catch (_) {}
                return true;
            }
            if (letter === 'z' || letter === 'y') return true;
        }
        if (key === 'Backspace') {
            if (end > start) replace('');
            else if (start > 0) replace('', ctrl ? this.prevWord(start) : start - 1, start);
            return true;
        }
        if (key === 'Delete') {
            if (end > start) replace('');
            else if (start < length) replace('', start, ctrl ? this.nextWord(start) : start + 1);
            return true;
        }
        if (key === 'ArrowLeft' || key === 'ArrowRight') {
            const left = key === 'ArrowLeft', here = this.caret;
            let target = left ? (ctrl ? this.prevWord(here) : here - 1) : (ctrl ? this.nextWord(here) : here + 1);
            if (!e.shiftKey && this.hasSelection) target = left ? start : end;
            target = Math.max(0, Math.min(length, target));
            this.setSelection(e.shiftKey ? this.anchor : target, target);
            this.goalX = null; this.touch(); this.onChange();
            return true;
        }
        if (key.length === 1 && !ctrl && !e.altKey) { replace(key); return true; }
        return false;
    }

    // ---- pointer ---------------------------------------------------------------------------------
    indexFromPoint(x, y) {
        const g = this.geo;
        if (!g) return this.caret;
        const row = Math.floor((y - g.top + 1) / g.lineHeight);
        const lineNumber = Math.max(0, Math.min(g.layout.length - 1, g.first + row));
        return this.indexAtX(g.layout, lineNumber, x - (g.left + g.offsetX), g.font);
    }
    pointerDown(x, y, { shift = false } = {}) {
        if (!this.geo) return false;
        this.focus();
        const index = this.indexFromPoint(x, y), now = performance.now();
        this.clicks = now - this.lastClickAt < 450 && Math.abs(index - this.lastClickIndex) <= 1 ? Math.min(3, this.clicks + 1) : 1;
        this.lastClickAt = now; this.lastClickIndex = index;
        if (this.clicks === 2) {
            const [a, b] = this.wordAt(index);
            this.setSelection(a, b); this.dragAnchor = a;
        } else if (this.clicks === 3) {
            const g = this.geo, line = g.layout[this.lineOf(g.layout, index)];
            this.setSelection(line.start, line.end);
            this.dragAnchor = line.start;
        } else {
            this.dragAnchor = shift ? this.anchor : index;
            this.setSelection(this.dragAnchor, index);
        }
        this.dragging = true; this.goalX = null; this.touch();
        return true;
    }
    pointerMove(x, y) {
        if (!this.dragging || !this.geo) return false;
        if (this.clicks === 1) this.setSelection(this.dragAnchor, this.indexFromPoint(x, y));
        this.touch();
        return true;
    }
    pointerUp() { this.dragging = false; }
    scrollBy(lines) {
        if (!this.multiline || !this.geo) return;
        const max = Math.max(0, this.geo.layout.length - this.geo.lines);
        this.scroll = Math.max(0, Math.min(max, this.scroll + lines));
        this.geo.first = this.scroll;
    }

    // ---- drawing ---------------------------------------------------------------------------------
    // Draws the visible lines with the selection in inverse video and a blinking caret. Returns the layout.
    draw({ left, top, width, lines = 1, lineHeight = 9, font = FONTS.Regular, placeholder = '', focused = true }) {
        const text = this.value, from = this.selStart, to = this.selEnd;
        let layout, first = 0, offsetX = 0;
        if (this.multiline) { layout = this.layoutLines(width, font); this.ensureVisible(layout, lines); first = this.scroll; }
        else { layout = [{ start: 0, end: text.length }]; this.ensureVisibleX(width, font); offsetX = -this.scrollX; }
        this.geo = { layout, first, left, top, width, lines, lineHeight, font, offsetX };

        ctx.save();
        ctx.beginPath();
        ctx.rect(left - 1, top - 1, width + 2, lines * lineHeight);
        ctx.clip();
        if (!text.length && placeholder) drawString(left, top, placeholder, font, 2);
        for (let row = 0; row < lines; row++) {
            const lineNumber = first + row;
            if (lineNumber >= layout.length) break;
            const line = layout[lineNumber], y = top + row * lineHeight, x = left + offsetX;
            drawString(x, y, text.slice(line.start, line.end), font, 1);
            const a = Math.max(from, line.start), b = Math.min(to, line.end);
            if (to > from && b > a) {
                const x0 = x + this.xAt(line, a, font), x1 = x + this.xAt(line, b, font);
                ctx.fillStyle = getColor(1);
                ctx.fillRect(x0 - 1, y - 1, x1 - x0 + 1, lineHeight);
                drawString(x0, y, text.slice(a, b), font, 0);
            }
        }
        if (focused && this.caretOn && !this.hasSelection) {
            const lineNumber = this.lineOf(layout, this.caret), row = lineNumber - first;
            if (row >= 0 && row < lines) {
                const x = left + offsetX + this.xAt(layout[lineNumber], this.caret, font) - 1;
                ctx.fillStyle = getColor(1);
                ctx.fillRect(x, top + row * lineHeight - 1, 1, lineHeight - 1);
            }
        }
        ctx.restore();
        return layout;
    }
}
