# The HD screens' files

With the HD pack (`mods/HD PACK`, made by `tools/hd/` from the player's own
copies of the KINGDOM HEARTS HD collections) the port draws some of the game's
screens itself, in the collections' pieces: the save list, the camp menu, the
theater's chapter list (MODO CINE), the room list, Mission Mode's character
select and the settings menu, and subtitles over the HD videos. Each is a text
file of lines a tool writes, read by `port/host/screens` and
`port/host/settings`. Without the mod the game's own screens show.

## The shared lines

`port/host/screens/layout_screen.h` reads what every screen file has, and
draws pieces and lines of text in a 1920 x 1080 picture fitted to the window
(its aspect kept, the rest black):

| Line | What it is |
|---|---|
| `piece NAME U V W H` | a picture in the atlas |
| `icon NAME U V W H` | an input's icon: `xbox:<input>`, `playstation:<input>`, `pad:<input>`, `key:<SDL scancode>` |
| `place NAME X Y W H` | where a piece goes |
| `text NAME X Y SIZE ALIGN GREY [R G B]` | a line's place (Y its capitals' top; their middle centred), its size (the font's scale), `left`/`centre`/`right`, its grey or its colour |
| `atlas FILE` | the pieces' picture, beside the file |
| `font FILE CELL TOP BOTTOM [rgba]` | the font's picture: its grey made the text's alpha, or with `rgba` its own colours (an outlined font); its cells' height, where its capitals ink them |
| `glyph CODE X Y W LEFT ADVANCE` | a glyph: its cell's place and width, its bearing, its advance |

The game's own strings are used where the game has them. In them the DS's
buttons are characters of the game's font (U+3349 its A, U+3314 its B, U+3322
its X, U+3336 U+3351 U+3357 its START); each is drawn as the icon of the input
the player has for that button on the device played -- the file's icons of an
Xbox pad, a PlayStation's and the keyboard's keys, the port's Switch icons for
any other pad (the DS's letters in the same places), the input's name where
there is no icon.

## The save list (`layouts/save/<lang>/save.txt`)

Written by `tools/hd/save_screen.py`, in DDD's save screen pieces; drawn by
`save_screen.c` over the game's own list (`runtime/menus/savelist.c`).

| Line | What it is |
|---|---|
| `rows X Y W H PITCH SHOWN` | the first row, the rows' pitch, how many show |
| `scroll Y0 Y1` | the thumb's top at the list's first row and last |
| `pitch NAME N` | the lines' pitch of a text of several |
| `words empty WORDS...` | DDD's words for an empty file |
| `words guest WORDS...` | the guest's row's words |

Days's own words are read as the game reads them (`UI/load/lrd_&.s.z` for the
list, `UI/newgame/ngm_&.s.z` for the difficulties, `UI/btl/&/info.s.z` for the
day). The keys are the game's A and B prompt (`UI/mlt/mlt_&.s.z` record 63);
the glove points at the selected row or button. With the guest's row (Mission
Mode's) Y plays as the guest from anywhere in the list.

## The camp menu (`layouts/camp/<lang>/camp.txt`)

Written by `tools/hd/ddd_camp.py`, in DDD's camp pieces; drawn by
`camp_screen.c` from what `runtime/menus/camp.c` reads of the game's menu.

| Line | What it is |
|---|---|
| `backdrop FILE` | the picture behind the game's 3D |
| `bars X Y W PITCH H` | the options' bars: the first's left, top and size, their pitch (places `bar_selected`, `bar_body`) |
| `option N WORDS`, `story_option N WORDS` | an option's name; the story camp's |
| `story_sub N WORDS` | the story's Holo-Missions' two options (0, 1) and Files' (2, 3) |
| `story_model_source`, `story_model_view` | the story camp's model frame |
| `day_colour R G B` | the story's day's tag (place `day_tag`) |
| `panel X Y W H` | the status panel (its places and texts from its top left) |
| `trials X Y W H` | the trials' bar: label, symbols and counts centred in it |
| `menu_place WORDS` | where a member is on the menu |
| `mode_label WORDS`, `difficulty N WORDS`, `mission_mode WORDS` | the game mode's row |
| `model X Y W H` | the window for the game's model (the backdrop drawn around it) |
| `model_source X Y W H` | the DS's frame of the model, in upper-screen dots |
| `model_view X Y W H` | the part of it the window shows (all of it when not given) |
| `description_pitch`, `digit_overlap`, `members`, `munny_colour`, `hearts_colour` | the description's pitch, the digits' overlap, the members' pitch, the colours |
| `page_backdrop FILE` | the Preferences page's picture |
| `page_tabs X Y W H PITCH`, `page_rows X Y W H PITCH SHOWN`, `detail_pitch N` | its tabs and rows |

It shows the options and the one selected, its description, the character's
face, name and numbers, the trials, money and heart points, the group's members
and where each is, the game's line over the menu (the host choosing a mission,
START to join or begin), and the Preferences page in the model's and panel's
place, the game's fade over all. Title Screen's question is the port's "Return
to the main menu?" with DDD's Yes and No buttons and the game's words for them.

## MODO CINE's list (`layouts/cine/<lang>/cine.txt`)

Written by `tools/hd/theater_cine.py`, in the theater's pieces; drawn by
`cine_screen.c`.

| Line | What it is |
|---|---|
| `rows Y PITCH SHOWN` | the first row's top, the rows' pitch, how many show |
| `digits X Y H PITCH R G B` | the day's digits: the last's middle, top, height, pitch, colour |
| `scene MOVIE DAY ART` | each movie's chapter day and artwork (`layouts/cine/art/ART`) |

The rows are the game's page, each its scene's name and length
(`UI/thr/thr_&.s.z`); a day's first scene shows the theater's square and DAY.
Behind them the selected scene's artwork, read on a thread of its own, fades in
when the selection changes.

## The rooms (`layouts/rooms/<lang>/rooms.txt`)

Written by `tools/hd/theater_mission.py`, in the theater's pieces; drawn by
`room_screen.c` from `runtime/net/roomlist.c`'s list (`MULTIPLAYER.md`).

| Line | What it is |
|---|---|
| `fixed TOP PITCH` | the list's first rows, Create a Group and the search |
| `rows TOP PITCH SHOWN` | the rooms under them, SHOWN at a time with a scroll bar |
| `form TOP PITCH` | a new room's rows (name, password, Create) or a password asked for |
| `cards PITCH` | a room's places |
| `frame THERE NOBODY` | a card's ring |
| `signal PITCH H1 H2 H3 H4` | a room's signal, four bars |
| `signal_ms A B C` | the pings under which four, three or two bars are lit (one above C, none unmeasured) |
| `art FILE` | the background |

Each row is a theater chapter's: a room's signal, name, host, "Password",
players and ping; a field's text (a password as asterisks) and label; an
action's words. The members' screen shows a card for each of the four places,
ringed in the place's colour when a player is there, and the host's Proceed lit
when the game would go on. The game's words are its own list's
(`UI/mlt/mlt_&.s.z`), the rest the port's (`ui_text.h`). Typing: the keyboard
through the system's text input; a gamepad as the settings menu's text entry
(up and down change the last character, right adds one, left erases one).

## Mission Mode's character select (`layouts/select/<lang>/select.txt`)

Written by `tools/hd/theater_mission.py`, in the theater's pieces; drawn by
`select_screen.c` from what `runtime/menus/charselect.c` reads.

| Line | What it is |
|---|---|
| `grid COLUMNS GAP` | the grid's columns and the tiles' gap (from place `tile`) |
| `stats PITCH` | the gauges' rows' pitch (from place `stat`) |
| `gauge_pitch PITCH` | a gauge's bars' pitch (from place `gauge`) |
| `gauge_colour R G B` | the bars' colours, left to right (the game's) |
| `plates PITCH` | the places' plates' pitch (from place `plate`) |
| `frame PLACE OTHERS` | a tile's frame: with a place on it, without |
| `art FILE` | the background |

It shows the characters the game lets be chosen, in its order, grey unless a
place is on one (then in colour, framed in the place's colour); this copy's
character large with its name, weapon and gauges; each place's plate along the
bottom, ringed once its player has chosen; the game's message and help.

## The settings menu (`layouts/settings/<lang>/settings.txt`)

Written by `tools/hd/theater_settings.py`, in the HD collection's own menu's
pieces; drawn by `settings_skin.cpp` where the menu draws its own rectangles.
Places are the pieces' picture's (1920 x 1080), its width mapped onto the
window's width and its height onto the window's height.

| Line | What it is |
|---|---|
| `picture W H` | the picture the places are in (1920 x 1080) |
| `atlas FILE` | the pieces' pixels, beside this file |
| `font FILE CELL` | the font's picture (its glyphs' grey), its cells' height |
| `screen X Y W H U V` | the screen behind the menu: its place, its texels |
| `part PIECE STATE ROLE X Y W H U V` | a piece's part in a state: its role (left, middle, right, top, bottom, whole, title; the slider's track, fill, knob), its place in the picture, its texels |
| `glyph CODE X Y W` | a character's cell in the font's picture and its width |

A piece of a left, a middle and a right part has its ends scaled with the
rectangle's height and its middle stretched between them (its other states
placed as they lie against its shown one); a stacked one likewise upright; one
of a whole part is stretched over it.

## The HD videos' subtitles

Written by `tools/hd/theater_movies.py --subtitles` as
`data/cutscenes/cinematics/subtitles/<lang>/hd<id>.txt` with its atlas; drawn
over the HD video by `host/movies/movie_subtitles.c`.

| Line | What it is |
|---|---|
| `picture W H` | the picture the places are in (1920 x 1080) |
| `rate R` | the frames counted a second (the theater's videos: 30) |
| `atlas FILE` | the lines' pixels, beside this file |
| `line FIRST END X Y U V W H` | a line: shown from frame FIRST to before END, at X, Y of the picture, from the atlas's U, V, W x H |

A frame of the schedule is a picture of the theater's video: a video at that
rate, or at 30000/1001 (the same pictures, slower), counts its own pictures;
another counts the time at the schedule's rate.
