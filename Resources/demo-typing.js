// Human-looking typing for demo recording mode (Ctrl+Shift+D, then Ctrl+1..5 on the Neural Synthesis prompt).
// The same generator the website demo uses: every call is a fresh performance of the text with its own pace,
// thinking pauses and mistakes (wrong neighbouring key, doubled key, swapped letters, a skipped letter noticed a
// few keys later, a whole word fumbled and retyped). Whatever happens, the finished text is exactly `text`.
// Returns steps: {type:"key"|"back"|"pause", ch?, ms}. Developer tool only; nothing here talks to a server.
(() => {
  function buildTypingSequence(text) {
    const rand = function (lo, hi) { return lo + Math.random() * (hi - lo); };
    const chance = function (p) { return Math.random() < p; };
    const pick = function (list) { return list[Math.floor(Math.random() * list.length)]; };
    const isLetter = function (ch) { return /[a-z]/i.test(ch); };
    const rows = ["qwertyuiop", "asdfghjkl", "zxcvbnm"];

    // A key a finger could plausibly hit instead: left/right, or one row up or down.
    function neighbour(ch) {
      const lower = ch.toLowerCase();
      for (let r = 0; r < rows.length; r++) {
        const i = rows[r].indexOf(lower);
        if (i < 0) continue;
        const options = [];
        const add = function (row, index) {
          if (rows[row] && index >= 0 && index < rows[row].length) options.push(rows[row].charAt(index));
        };
        add(r, i - 1); add(r, i + 1); add(r - 1, i); add(r - 1, i + 1); add(r + 1, i - 1); add(r + 1, i);
        if (options.length) return pick(options);
      }
      return ch;
    }

    const steps = [];
    const pace = 1.35; // keys, pauses and backspaces all run 35% faster than the base timings below
    let speed = rand(52, 92); // ms per key for the current word
    const key = function (ch, slow) {
      const space = ch === " " ? rand(10, 60) : 0;
      steps.push({ type: "key", ch: ch, ms: (speed * (slow || 1) * rand(0.75, 1.3) + space) / pace });
    };
    const pause = function (ms) { steps.push({ type: "pause", ms: ms / pace }); };
    const back = function (count) {
      for (let k = 0; k < count; k++) {
        steps.push({ type: "back", ms: (k === count - 1 ? rand(120, 240) : rand(55, 95)) / pace });
      }
    };

    // Planned mistakes per prompt, averaging 0.93 (it was 1.17 with [0, 1, 1, 1, 2, 2]).
    let mistakes = pick([0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2]);
    pause(rand(150, 550)); // settling before the first key

    let i = 0;
    while (i < text.length) {
      const ch = text.charAt(i);
      const atWordStart = i === 0 || text.charAt(i - 1) === " ";
      if (atWordStart) {
        speed = rand(52, 95);
        if (i > 0 && chance(0.14)) pause(rand(280, 800)); // stops to think between words
      } else if (chance(0.03)) {
        pause(rand(120, 300)); // a tiny hitch mid-word
      }

      const remaining = text.length - i;
      if (mistakes > 0 && i >= 3 && remaining > 5 && isLetter(ch) && chance(mistakes / Math.max(1, remaining - 4))) {
        mistakes--;
        let wordEnd = i;
        while (wordEnd < text.length && text.charAt(wordEnd) !== " ") wordEnd++;
        const wordLength = wordEnd - i;
        let kind = pick(["wrong-now", "wrong-late", "double", "swap", "skip", "word"]);
        if (kind === "word" && !(atWordStart && wordLength >= 5)) kind = "wrong-late";
        if (kind === "swap" && !(isLetter(text.charAt(i + 1)) && text.charAt(i + 1).toLowerCase() !== ch.toLowerCase())) kind = "double";

        if (kind === "wrong-now") {
          // Hits the wrong key and sees it straight away.
          key(neighbour(ch)); pause(rand(160, 380)); back(1); key(ch, 1.2); i += 1;
        } else if (kind === "wrong-late") {
          // Wrong key, keeps going for a few letters, then spots it and goes back.
          const k = Math.min(1 + Math.floor(Math.random() * 3), remaining - 1);
          key(neighbour(ch));
          for (let j = 1; j <= k; j++) key(text.charAt(i + j));
          pause(rand(220, 520)); back(k + 1);
          for (let j = 0; j <= k; j++) key(text.charAt(i + j), 1.15);
          i += k + 1;
        } else if (kind === "double") {
          key(ch); key(ch, 0.8); pause(rand(120, 300)); back(1); i += 1;
        } else if (kind === "swap") {
          key(text.charAt(i + 1)); key(ch); pause(rand(140, 320)); back(2);
          key(ch, 1.2); key(text.charAt(i + 1), 1.2); i += 2;
        } else if (kind === "skip") {
          // Misses a letter, notices a few keys later, goes back and fixes it.
          const k = Math.min(2 + Math.floor(Math.random() * 3), remaining - 1);
          for (let j = 1; j <= k; j++) key(text.charAt(i + j));
          pause(rand(260, 560)); back(k);
          key(ch, 1.2);
          for (let j = 1; j <= k; j++) key(text.charAt(i + j), 1.1);
          i += k + 1;
        } else {
          // Fumbles a whole word, stares at it, deletes it and types it carefully.
          const word = text.slice(i, wordEnd);
          const wrong = word.split("");
          const changes = 1 + Math.floor(Math.random() * 2);
          for (let c = 0; c < changes; c++) {
            const at = 1 + Math.floor(Math.random() * (word.length - 1));
            if (isLetter(wrong[at])) wrong[at] = neighbour(wrong[at]);
          }
          for (let j = 0; j < wrong.length; j++) key(wrong[j]);
          pause(rand(420, 820)); back(word.length); pause(rand(80, 200));
          for (let j = 0; j < word.length; j++) key(word.charAt(j), 1.25);
          i = wordEnd;
        }
        continue;
      }

      key(ch);
      i += 1;
    }
    pause(rand(150, 450)); // a beat before pressing Make
    return steps;
  }
  window.buildHumanTyping = buildTypingSequence;
})();
