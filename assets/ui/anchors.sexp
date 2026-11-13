; Widescreen anchors: where each piece of the original's 640x480 layouts goes in a window wider
; than 4:3 (src/screens/ui_anchors.hpp, docs/screens.md "Widescreen"). The port's own data.
;
; (screen NAME (states S...) RULE...)  a front-end state's rules, first match wins
; (group NAME RULE...)                  rules a screen takes in with (use NAME)
; (backdrop 0xAARRGGBB)                 a flat colour over the whole window under everything
;
; A rule matches a quad by what it gives (all of them):
;   (sprite ID|LO-HI... [(in X0 Y0 X1 Y1)] KIND)  registry sprites, optionally only in a region
;   (slots LO HI [(in X0 Y0 X1 Y1)] KIND)         texture slots: below 0x10000 the conversations'
;                                                 pictures and the font, 0x225 the movie
;   (size W H [(in X0 Y0 X1 Y1)] KIND)            a quad of that size: a screen's frame pieces
;   (region X0 Y0 X1 Y1 KIND)                     anything whose middle is there
;   (ticker-end X0 Y0 X1 Y1 KIND)                 a scrolling text line's clip end there: left
;                                                 or right moves it with the pieces beside it
; Regions are the 640x480 layout's (a split view's layout fitted back to one view's). KIND:
;   centre      stays in the 4:3 box, clipped to its title-safe area (nothing matching is centre)
;   left, right moves out toward the window's safe edge, as far as HUD WIDTH lets it
;   free        stays, clipped to the window: world points, art that runs on past the box
;   extend [mirror|tile|clamp] [BAND] [(inset N)] [(split U)] [(soft R [DIM])] [(feather W)]
;          [(lines P)] [(fill U0 V0 U1 V1)]
;          [(rows V0 V1 [mode] [BAND] [(side left|right)] [options as above])...]
;               a background or bar touching the box's safe edges goes on to the window's, always
;               at the picture's own scale: its edge band of BAND texels mirrored back and forth
;               (a band wider than the reach is a reflection), repeated as a period, or its edge
;               column repeated (clamp: only for edges that are flat across, a bar's plain run;
;               anything with structure there is mirrored). (inset N) first cuts off what lies N
;               pixels from the screen's sides (N/2 from top and bottom; 32 is retail's
;               title-safe area, whose border art was never meant to show). (split U) opens a bar
;               at texel column U: the part left of it moves out with the left pieces, the part
;               right of it with the right, the plain column between repeated across the gap.
;               (soft R [DIM]) blurs the continuation (so nothing near the edge shows again sharp)
;               and dims it; (lines P) keeps the scanlines drawn into the picture every P texel
;               rows: the blur steps by whole periods, so they run on sharp and in phase;
;               (feather W) fades the picture's outer W pixels into its soft continuation, so
;               there is no seam. (fill U0 V0 U1 V1) covers the sides with that patch of the
;               picture, mirrored across and repeated down in step with the lines, under one
;               fading reflection of the edge band: art whose edges hold marks that mustn't show
;               mirrored. (rows ...) cut a picture into bands of texel rows treated apart, each side
;               by the first band naming it; (side ...) limits a band to one side.
;   blur [RADIUS] [DIM]  a blurred copy dimmed to DIM fills the window behind it

; ---------------------------------------------------------------------------------------------
; the fight's HUD (hud.md): gauges and readouts to the corners, the reticle, boxes, marks and
; tags where the world puts them, the conversations and barks in the middle
(group hud
  (slots 0 0xffff free)                          ; conversations, barks, the font
  (sprite 0x560-0x573 0x5b8-0x5bb free)          ; the reticle, boxes, marks, radar, name tags
  (sprite 0xe7f 0xe80 0xe8c 0xeb1 0xe8f 0xe90 centre) ; READY, GO, Crasher!, the pause plate
  (sprite 1398-1401 1435-1438 centre)            ; the durability and O.C gauges, bars with frames
  (region -9999 -9999 232 9999 left)
  (region 408 -9999 9999 9999 right))

; fights, test drives, free play, the replay, the attract demo, the ending's scene
(screen battle (states 6 0x2f 0x2a 5 1 0xa)
  (use hud))

; ---------------------------------------------------------------------------------------------
; the menus' frame (screen_header.cpp, every options-style screen): the header bar opens between
; DAY and money and the title, the help line runs across the window, the footer bar and its
; now-playing line go on to the edges, the picture behind is reflected at its sides, softly, its
; scanlines (every 6 texel rows) running on sharp
(group frame-text
  (slots 0 0xffff (in -9999 20 9999 60) free)    ; the line along a 640x72 header (Sonic Amp, dealers)
  (slots 0 0xffff (in -9999 60 9999 96) free)    ; the help line
  (slots 0 0xffff (in -9999 380 9999 440) free)  ; the now-playing line
  (slots 0x10000 0x1ffff (in -9999 30 288 70) left)  ; DAY and the money
  (slots 0x10000 0x1ffff (in 288 20 9999 72) right)) ; what a screen draws over its title (PC's PC)

(group frame-bars
  (size 640 96 extend clamp (inset 34) (split 260))         ; the header: DAY tab and money | the title
  (size 640 72 extend clamp (inset 34) (split 260))         ; the shorter header (town map, Sonic Amp)
  (size 640 104 extend clamp (inset 34))                    ; the footer
  ; pictures with the footer drawn in: its bar's plain run carried on, the meter never reflected
  (sprite 1526 1649 2106 2208 2751 2830 2831 2911 2726 extend (inset 34)
                (rows 0 96 clamp (split 260))
                (rows 96 414 mirror 640 (soft 6 1) (feather 24) (lines 6) (side left))
                (rows 96 376 mirror 640 (soft 6 1) (feather 24) (lines 6) (side right))
                (rows 376 480 clamp))
  ; key config's picture has the pad drawn at its right edge: that side carries on the picture's
  ; left half instead, under one fading reflection of the strip right of the pad
  (sprite 1592 extend (inset 34) (rows 0 96 clamp (split 260))
                (rows 96 344 mirror 36 (soft 6 1) (feather 24) (lines 6) (fill 34 96 300 344)
                      (side right))
                (rows 96 480 mirror 640 (soft 6 1) (feather 24) (lines 6)))
  (size 640 480 extend (inset 34) (rows 0 96 clamp (split 260))
                (rows 96 480 mirror 640 (soft 6 1) (feather 24) (lines 6))))

(group frame
  (use frame-bars)
  (use frame-text))

; the options family, and the records (whose picture holds its frame)
(screen options (states 0x1a 0x23 0xa0 0x27 0x1f 0x11 0x10 0x16 0x24 0x26 0x20 0x2b 0x21 0x2c 0x1d)
  (use frame))

; the records and save files checks: the striped card
(screen checks (states 9 0x2e)
  (slots 0 0xffff extend mirror 640))

; screen adjust: a fight's picture with its HUD drawn in (no reflection of the HUD's pieces)
(screen adjust (states 0)
  (sprite 828 blur 10 0.5))

; the parts editor: the hangar plate left, the socket count right
(screen assemble (states 4)
  (sprite 969 1044 left)                         ; HANGAR 01
  (sprite 977 978 right)                         ; BODY SOCKET HOLE NUMBER
  (sprite 3693-3702 (in 520 120 600 150) right)  ; its two counts (number font digits 0..9) by its "/"
  (use frame))

; area select and the job menu
(screen area-select (states 3)
  (sprite 857 extend clamp)                      ; the bar under the area tabs
  (use frame))

; the hangar: the wheel of garages runs on past the box with its machines, its shaded ends at the
; window's sides
(screen hangar (states 0xe)
  (sprite 1574 left)
  (sprite 1575 right)
  (sprite 1528-1547 free)                        ; the cards, their numbers and classes
  (sprite 1553 extend clamp)                     ; the name plates' bar
  (use frame))

; the hubs (garage, Club Wired, Plus Tech, Wild Machines, the help menu's club): the wheel runs on
; past the box, the footer opens with the place's logo to the right. The hub picture holds its
; header tab with the motto drawn across it, so the tab stays whole with DAY and money on it and
; the picture is reflected softly from the tab down, its scanlines running on
(group hub
  (size 640 184 extend clamp (inset 34) (split 300))        ; the footer with the logo
  (sprite 1476 3042 2128 1088 1076 extend (inset 34) (rows 0 40 clamp)
                (rows 40 106 mirror 12 (soft 6 1) (feather 12) (lines 6) (side left))
                (rows 40 480 mirror 640 (soft 6 1) (feather 24) (lines 6)))
  (size 640 72 extend clamp (inset 34))                     ; the help menu's header
  (slots 0 0xffff (in -9999 300 9999 360) free)  ; the now-playing line
  (ticker-end 300 300 9999 360 right)            ; its end, with the logo and the meter
  (sprite 3177 right)                            ; the level meter on the footer's right
  (region 380 90 9999 300 free))                 ; the wheel

(screen hubs (states 0xd 8 0x1b 0xf)
  (shows 1476 3042 2128 1088)
  (use hub))

; the hubs' pages (Wild Machines' and Plus Tech's lists, the used Scoobees...) are framed
(screen hub-pages (states 0xd 8 0x1b 0xf)
  (use frame))

; the help menu (Pepper's talk): Pepper to the right
(screen help (states 0x13)
  (slots 534 534 right)
  (use hub))

; the town map: the map's camouflage carried on from a patch without place marks (its rows in
; step with the map's fine lines) under one fading reflection of the map's edge, the logo's,
; music's and footer's plain bands run on, the header opened
(screen town-map (states 0x29)
  (sprite 2668 extend (inset 34)
                (rows 0 356 mirror 24 (soft 10 0.9) (feather 32) (lines 2) (fill 40 26 330 78)
                      (side left))
                (rows 0 374 mirror 16 (soft 10 0.9) (feather 32) (lines 2) (fill 340 26 600 78)
                      (side right))
                (rows 356 480 clamp))
  (slots 0 0xffff (in -9999 20 9999 72) free)    ; the place's line along the header
  (use frame))

; Sonic Amp: the spotlight band and the tab bar go on to the edges
(screen sonic-amp (states 0x14)
  (sprite 2444 2463 extend clamp)
  (use frame))

; the dealer
(screen dealer (states 0x22)
  (use frame))

; the result: the receipt over the demo battle, the frame's bars to the edges
(screen result (states 0x1e)
  (use frame))

; the conversations (states 0x30..0x9D, src/talk): the place's frame picture opened like a hub's
; (header tab | its right, the footer with the place's logo to the right), its streaked middle
; (lines every 8 rows) reflected softly, the speaker's portrait to the right, the box in the middle
(screen talk (states 0x30 0x31 0x32 0x33 0x34 0x35 0x36 0x37 0x38 0x39 0x3a 0x3b 0x3c 0x3d 0x3e 0x3f
  0x40 0x41 0x42 0x43 0x44 0x45 0x46 0x47 0x48 0x49 0x4a 0x4b 0x4c 0x4d 0x4e 0x4f 0x50 0x51 0x52
  0x53 0x54 0x55 0x56 0x57 0x58 0x59 0x5a 0x5b 0x5c 0x5d 0x5e 0x5f 0x60 0x61 0x62 0x63 0x64 0x65
  0x66 0x67 0x68 0x69 0x6a 0x6b 0x6c 0x6d 0x6e 0x6f 0x70 0x71 0x72 0x73 0x74 0x75 0x76 0x77 0x78
  0x79 0x7a 0x7b 0x7c 0x7d 0x7e 0x7f 0x80 0x81 0x82 0x83 0x84 0x85 0x86 0x87 0x88 0x89 0x8a 0x8b
  0x8c 0x8d 0x8e 0x8f 0x90 0x91 0x92 0x93 0x94 0x95 0x96 0x97 0x98 0x99 0x9a 0x9b 0x9c 0x9d)
  (size 640 480 extend (inset 34) (rows 0 72 clamp (split 260))
                (rows 72 300 mirror 640 (soft 6 1) (feather 24) (lines 8))
                (rows 300 480 clamp (split 300)))
  (size 640 184 extend clamp (inset 34) (split 300)) ; the footer, drawn over the portrait
  (size 264 336 right)                           ; the portrait
  (slots 518 518 (in 330 20 9999 64) right)      ; the skip hint
  (slots 0 0xffff (in -9999 300 9999 360) free)  ; the now-playing line
  (ticker-end 300 300 9999 360 right))           ; its end, with the logo

; the title: the blue picture's streaks run on, the scanline rows and black bands to the edges
(screen title (states 0x28 0x15)
  (sprite 2643 extend mirror 48)
  (sprite 2644 2645 extend clamp))

; movies: 4:3 in the middle over a blurred, dimmed copy of the frame
(screen movies (states 0x9e 0x2d 2 0x19 0xb 0x25 0xc)
  (slots 0x225 0x225 blur 14 0.45))
