# Text and scripts

What `pc_extract --only text,scripts` writes under `data/text/` and `data/scripts/`, and how to read it. Everything comes from
the XBE plus one disc file (the font atlas). The decomp's `docs/port/data.md` ("Text and the font", sections 14 and 15) and
`docs/port/progression.md` ("The script interpreter") are the spec; the code is `tools/extract/glyphs.*`, `event_scripts.*`,
`ui_strings.*`, `text.*` and `scripts.*`.

## Text in the original

- Event text (conversations, barks, bios, maker talks) is 16-bit glyph numbers, -1 ended. Negative numbers are controls.
- Table text (names, labels, help lines, message boxes, subtitles) is Shift-JIS, full-width even for English. The game turns it
  into glyph numbers pair by pair through `font_sjis_order` (`sjis_to_glyphs` 0xBDCA5), so it always reads two bytes a character;
  a pair not in the font becomes glyph 0, a full-width space.
- There is one font. Glyph g is pair g of `font_sjis_order` (0x27DE38, 2297 pairs). Its order: JIS rows 1 to 5 (symbols, digits,
  Latin, hiragana, katakana, 378 glyphs), then Ⅰ to Ⅹ (CP932's NEC row 13, not JIS X 0208), then from 0x184 the kanji and a few
  Greek letters in the game's own order. No box drawing, no Greek or Cyrillic rows.

All text stays exactly as the game has it: full-width Latin stays full-width (`Ｗｅｌｃｏｍｅ`), so every character maps back to one
glyph. Narrow it when you draw with a new font, not in the data.

## data/text/

`font.png` is `ascii_font_sys_tex.bin` (asset 1684) decoded: 512x2048, 18x18 cells, 28 to a row, glyph g at
((g % 28) * 18, (g / 28) * 18).

`font.sexp` has one form per glyph:

```
(glyph 157 "Ａ" (sjis 0x8260) (cell 270 90) (left 3) (advance 12))
```

`left` and `advance` are `glyph_widths` (0x1C3FB0): where the drawn part starts in the cell and how far the pen moves. They exist
for glyphs below 0x184; the rest are 0 and 18. The character is `font_unicode_order` (0x27F030) for glyphs 0..2221. That table
stops there (a 0 at 2222), so glyphs 2222..2296, 75 kanji, take CP932's reading of their pair and carry `(cp932)`; each was checked
against its cell in the atlas. Glyph 829 is pair 0x83BD, a code CP932 doesn't assign; the atlas cell is ・ and `font_unicode_order`
says U+30FB, which is what it gets (a comment marks it).

Event text, one file per group: `talk.sexp` (the 110 conversations), `bark.sexp` (battle barks and the tutorial), `bio.sexp`
(Sonic Amp artist notes, Japanese ones included), `maker.sexp` (the dealer's three maker talks).

```
(text "talk_30.3" "Ｗｅｌｃｏｍｅ　ｔｏ　{c:ffeeee66}Ｓｈｉｎｊｕｋｕ\nＷｉｒｅｄ{c}，　ｗｈｅｒｅ ...")
```

The id is the script and the index of the command that draws the line, so `talk_30.3` is `event_state_30[3]` in the decomp. Each
line is the second record of a text pair; the first (the furigana) is a single blank in every US script and is left out. A
furigana that isn't blank would appear as its own `(text "id" "..." (furigana))`; none do.

Control codes (`wide_text_draw` 0xBD6A0) as markup:

| code | markup | what |
| --- | --- | --- |
| -2 | `\n` | new line |
| -3 | `{mark-reset}` | lets the next -4 draw its mark again; no script uses it |
| -4 | `{press}` | draws the press mark (glyph 0x64) and stops the line until accept is held; no script uses it |
| -5 | `{player}` | the player's pilot name |
| -6 | `{chip}` | the boarded chip's name; no US text uses it |
| -7, -8 | `{speaker-a}`, `{speaker-b}` | a bark's pilot and Scoobee names |
| -9 | `{c}` | back to the command's colour |
| -10..-18 | `{c:AARRGGBB}` | ff66eeee, ffee9500, ffee88ee, ff808080, ff308080, ff8e4500, ff66ee33, ffeeee66, ff256584 |

Anything else would come out as `{ctl:N}` or `{glyph:N}`; nothing does. The text never holds ASCII braces (the font has none), so the
markup can't clash.

`tables.sexp` is every text field of the tables the game names things from, by table, row and field:

```
(string "tournament.17.name" "Ｓｉｚｚｌｉｎ＇　Ｆｉｅｌｄ" (missing 1))
```

Tables: tournament, tournament-rule (name, subtitle), body, leg, chip, arm, back, option, color, sticker, scoobee (the loadout name),
identity (name, name-en), team, help (the help menu labels), save-dialog and jp-tutorial (six lines a message box), music (title,
artist, lyricist, composer, held in the row), ending-subtitle, dealer (the help line). Addresses and strides are in `ui_strings.cpp`.
Empty and null fields are left out.

`(missing n)` counts pairs the game can't find in the font and draws blank. The text still says what the bytes say (CP932 for a
pair the font lacks, a single byte as itself), so the port can choose to show it right. The cases: the full-width apostrophe ＇
(0xFA56) in tournaments 17 and 37, identity 516 "Crackin" in half-width ASCII, and music rows 0 and 1, whose lyricist and composer
"Akihiko Hirama" have a half-width space that throws the pairing off for the rest of the name.

`strings.sexp` is every other displayable string in `.rdata` and `.data` (whole Shift-JIS pairs, 4-aligned, as cl placed each
literal), by address, with what points at it:

```
(string "str.19b41c" "　Ｔｈａｎｋ　ｙｏｕ～♪" (used-by (data 0x1de1cc)))
```

`str.19b41c` is the decomp's `D_0019B41C`. `(data VA)` is a pointer word, `(code VA)` an instruction operand. Kept: strings something
points at (for code, short strings only after a push or mov imm32, since two-pair runs turn up inside call offsets), and unreferenced
runs of three pairs or more that all draw. Every kept string has a referrer; ASCII strings (debug, file names, Winsock names) are
not displayable and aren't here.

## data/scripts/

Every event script, 1170 of them: `talk_XX.sexp` for each conversation state 0x30..0x9D, and `bark.sexp`, `bio.sexp`, `maker.sexp`
holding their group's scripts. They are found by walking EVENTSEG and three runs of `.data` for arrays of 0x34-byte commands ending
in type 13 whose text points at valid token streams; the count and every address match the decomp's `src/data`.

```
(script talk_3e (address 0xe44c00) (commands 35) (state 0x3e)
  (trigger 14 (from 0x6) (to 0x1e) (needs 12) (repeatable) (predicate 13) (tournament 14) (area 0) (rule 22))
  (policy (background townmap) (cast roy) (music shuffled) (volume 0.5))
  (used-by (code 0xcc854))
  (label page-0)
  (sprite 18 (at 55 167) (order 100) (color 0xffaaddff))
  (portrait roy 8 (at 329 37) (order 10) (color 0xffffffff) (fade-in))
  (text "talk_3e.3" (at 62 185) (speed 0) (color 0xff66eeee)) ;  Roy
  (text "talk_3e.5" (at 80 209) (speed 3) (color 0xffdddada)) ; Thought everything would / work out fine. ...
  (text "talk_3e.7" (at 215 304) (speed 0) (flags 0x20) (color 0xffc0c0c0)) ; ▼
  (page-wait)
  (wait 8)
  (label page-1)
  ...
  (end))
```

Header:
- `trigger` rows are the rows of `event_policy_table` (0x1D0E00) that play this state, with the row number first. Absent conditions
  mean any. `needs` is the prerequisite row (`needs-range` if first and last ever differ; they don't), `once` / `repeatable` the
  repeat byte, `predicate` the `event_predicates` slot (table in progression.md), `help-only` a row the help menu plays directly.
- `policy` is that row's background (asset stem of the talk background; `none` is 6), cast (portrait actors by asset stem, the
  second nin and mask as nin2 and mask2), music (`keep` for -1, `shuffled` for 202, else the catalogue track) and volume.
- `used-by` is what points at the script: `(code VA)`, `(reaction-profile row kind name)` for barks (kind is the `BARK_*` number),
  `(music row)` for bios, `(dealer-talk n)`, or `(data VA)`. 46 scripts have none: the Japanese bios and the leftovers in
  docs/findings.md "Text nothing points at".

Commands, in order, named as progression.md names them:
- `text`: one text pair, merged. The id is the line's command; the arguments are the line record's (the handler uses the second
  record's). `speed` is frames a glyph (0 draws it whole), `flags` bit 31 stops accept from finishing the line, the low bits go to the
  draw (0x20 on the ▼ mark). `furigana-size` appears when the width and height aren't 18: they only size the furigana, the line is
  always 18. `texture` is the font texture offset when not 0. A trailing comment is the line narrowed to ASCII for reading.
  `(text-skipped id)` would mark a type 1 record on an odd slot with no pair, which the interpreter never draws; none exist.
- `wait n` (type 2): holds n frames, then becomes the page break.
- `sprite id (at x y) (order o) (color c)` (type 3): render registry sprite. Scales are always 1 and left out.
- `portrait actor expression (at x y) (order o) (color c)` (type 4) with `fade-in`, `fade-out`, `still` (flag 4, no mouth).
- `delay n` (type 5): a skippable delay, negative holds forever. Only the `.data` scripts use it.
- `page-wait` (type 10), `end` (type 13).
- Types 0, 6..9, 11 and 12 would print as `(op type args...)`; no script has them.

`(label page-N)` marks each place the interpreter's walk restarts: the start and after every `wait`. Each frame the interpreter runs
again everything from the last label it passed (that's how sprites and finished text stay up), so the label is the only control-flow
target there is; the language has no jumps or branches. Loaders must keep the command order exactly.

Counts from the retail XBE: 1170 scripts (110 talks, 74 bios, 983 barks, 3 maker talks), 48839 command records: 24622 text records
(12311 pairs), 8147 sprites, 5844 delays, 4406 waits, 2484 portraits, 2166 page waits, 1170 ends.
