"""Lane M: the BB1 manual's content. Every factual sentence goes through S(text, sources...) so the trace maps it to
firmware file:line ('path::pattern' is resolved to a line on the named tree at build time)."""
from lib import S, where, gesture, panel, md, led_key
import json, html

SCR = '/fm1/guide/screens/'
def img(name, alt, cls='shot'):
    return f'<img class="{cls}" src="{SCR}{name}.png" width="480" height="480" alt="{html.escape(alt)}" loading="lazy">'
def strip(name):
    return f'<img class="strip" src="{SCR}strip-{name}.png" width="480" height="48" alt="" aria-hidden="true">'

# ---------------------------------------------------------------- source shorthands
NAV = 'firmware/src/bb_nav.c::'
NAVH = 'firmware/src/bb_nav.h::'
NAVF = 'firmware/src/bb_nav_firmware.inc::'
TOP = 'firmware/src/bb/bb_top_mode.h::'
NAV21 = 'big-beat/NAVIGATION-21.md::'
UI = 'firmware/src/bb_ui_v3/bb_ui.c::'
UIH = 'firmware/src/bb_ui_v3/bb_ui.h::'
PLAYUI = 'firmware/src/bb_ui_v3/bb_ui_play.c::'
UIF = 'firmware/src/bb_ui_v3_firmware.inc::'
TX = 'firmware/src/bb/bb_text_block.def::'
ARR = 'firmware/src/bb_arranger_firmware.inc::'
ARD = 'firmware/src/bb_arranger_draw.inc::'
REC = 'sr:firmware/src/bb_record_firmware.inc::'
RECD = 'sr:firmware/src/bb_arranger_draw.inc::'
SRTX = 'sr:firmware/src/bb/bb_text_block.def::'
GEN = 'firmware/src/bb_gen_firmware.inc::'
MIDI = 'firmware/src/bb/bb_midi_compat.h::'
MIDIS = 'firmware/src/bb/bb_midi_settings.h::'
MIDU = 'firmware/src/bb_midi_ui_input.inc::'
MIDD = 'firmware/src/bb_midi_ui_draw.inc::'
SESS = 'firmware/src/bb_target_session.c::'
PICK = 'firmware/src/bb/bb_sound_picker.c::'
PICKI = 'firmware/src/bb_sound_picker_input.inc::'
AUD = 'firmware/src/bb/bb_audio.c::'
UK = 'firmware/src/bb/bb_user_kit.h::'
UKF = 'firmware/src/bb_user_kit_firmware.inc::'
PE = 'firmware/src/bb/bb_pattern_edit.h::'
ARRH = 'firmware/src/bb/bb_arrangement.h::'
CORE = 'firmware/src/bb/bb_core.c::'
COREH = 'firmware/src/bb/bb_core.h::'
SWING = 'firmware/src/bb/bb_swing.h::'
CTRL = 'firmware/src/bb/bb_control.c::'
FALL = 'firmware/src/bb/bb_voice_fallback.h::'
RECIPE = 'big-beat/integration/build/recipes/composition-21/recipe.json::'
INST = 'site:public/fm1/install/index.html::'
CAT = 'catalogue walk on 525d2bcf (claude/2.1/manual/tools/catcount.c output data/catalogue.txt)'
KEYMAP = 'key-role and LED dump on 525d2bcf (claude/2.1/manual/tools/keymap.c output data/keymap.json, from bb_nav_key_role, bb_nav_key_word and bb_nav_leds)'
RENDER = 'render: claude/2.1/manual/tools/manual_capture.c on the tree (firmware renderer)'
CONDUCT = 'firmware/src/bb_conductor_firmware.inc::'
AUDH = 'firmware/src/bb_dillo_audition.h::'
CATJ = 'big-beat/catalogue/catalogue.json (67 named songs + 4 preset shelves in songs[], 21 eras, 27 ACID banks; SPEC 2.1.5)'
VIEW = 'firmware/src/bb_conductor_firmware.inc::static int bb_cd_is_shelf(unsigned bank)'

KM = None
def keymap(path):
    global KM
    KM = json.load(open(path))

def card(title, body=(), art='', strip_name=None, span=4, cls=''):
    return dict(title=title, body=list(body), art=art, strip=strip_name, span=span, cls=cls)

def sec(num, title, intro=(), cards=(), extra=''):
    return dict(num=num, title=title, intro=list(intro), cards=list(cards), extra=extra)

def table(head, rows, cls=''):
    h = ''.join(f'<th>{c}</th>' for c in head)
    b = ''.join('<tr>' + ''.join(f'<td>{md(c)}</td>' for c in r) + '</tr>' for r in rows)
    return f'<div class="tbl {cls}"><table><thead><tr>{h}</tr></thead><tbody>{b}</tbody></table></div>'

# the key words the firmware itself uses in its legend (bb_nav.c 2.1 one table: bb_nav_key_word), for drawing keys on the
# panel; where one word covers a run of keys the key's own target is printed instead (pad, part, tab, section number)
PADS = ['KICK', 'SNARE', 'LT', 'MT', 'HT', 'RIM', 'CLAP', 'COW', 'CYM', 'OH', 'CH']
PER = {'PADS': lambda a: PADS[a], 'KILL': lambda a: ['A', 'B', 'DILLO', 'DRUMS'][a], 'TAKE': lambda a: ['A', 'B', 'DILLO'][a],
       'LIST': lambda a: ['SONGS', 'ERAS', 'PRESETS'][a], 'TABS': lambda a: ['ECHO', 'TAPE', 'SPRING', 'MOTION', 'SENDS'][a],
       'PARTS': lambda a: ['A', 'B', 'DILLO'][a], 'MOVES': lambda a: ['RHYTHM', 'PITCH', 'ACCENT', 'SLIDE'][a - 6],
       'NOTES': lambda a: '', 'SONG': lambda a: 'SONG', 'STEPS': lambda a: 'STEP', 'DILLO VIEW': lambda a: 'DILLO'}
def page_keys(name, override=None):
    """key -> (label, LED letter) for every key with a role: . off, h half-lit, B bright, p pulse (bb_nav_leds)"""
    keys = {}
    for i, (w, role, a, led, help_, rw, rr, ra) in enumerate(KM[name]['keys']):
        if not role or led == '.': continue     # off: does nothing here now
        k = f'W{i+1}' if i < 16 else f'B{i-15}'
        lab = PER[w](a) if w in PER else w[:7]
        if w in ('MUTE', 'SOLO') and name.startswith('studio'): lab = ['A', 'B', 'DILLO'][a]
        if w == 'SECTION' and name.startswith('studio'): lab = str(a + 1)
        keys[k] = (lab, led)
    if override: keys.update(override)
    return keys

def knobrow(name):
    k = KM[name]['knobs']
    return '<div class="knobrow">' + ''.join(f'<div><span class="kc kc-knob">K{i+1}</span><b>{html.escape(x) if x else "—"}</b></div>' for i, x in enumerate(k)) + '</div>'


def build():
    ch = []
    # =================================================================== 1
    where('1')
    ch.append(dict(num=1, title='before you start', intro=[
        S('Big Beat Studio BB1, BB1 for short, turns the FM-1 into two drum machines and Dillo, an acid bass synth, playing together through tape echo and spring.',
          RECIPE + '"purpose"', NAV + 'r[0] = BB_NAV_R_MIX_DRUM_A; r[1] = BB_NAV_R_MIX_DRUM_B;', NAV + 'nav_fx_bank_names[5] = {"ECHO", "TAPE DRIVE", "SPRING", "MOTION", "SENDS"}'),
        S('Every button opens one page. The four knobs always do what the bottom of the screen says. {HOME} brings you back to the selected instrument; hold it and the screen explains every lit key on the page you are on.',
          NAV + 'static const char *const nav_page_names', NAV + 'void bb_nav_knobs', NAV + 'HELP keeps every light (HOME bright as the help mark)', NAV21 + '`HOME` returns to the landing screen'),
        S('A key that is lit does something here; a dark key does nothing.', NAVH + 'A dark key or button never acts')],
        sections=[
        sec('1.1', 'what you need', cards=[
            card('the unit', [S('An M-VAVE FM-1 with BB1 installed. Chapter 3 shows how to install it.', INST + 'One click, about five minutes')], gesture('MASTER'), span=4),
            card('a computer', [S('Desktop Chrome on a computer, and a USB data cable plugged straight into it. Only the installer needs these.', INST + 'desktop Chrome on a computer', INST + 'A USB data cable, plugged straight into the computer')], gesture('USB'), span=4),
            card('headphones or speakers', [S('Start with {MASTER} turned fully down, then bring it up once something plays.', INST + 'MASTER turned fully down')], gesture('TURN:MASTER'), span=4),
        ]),
        sec('1.2', 'what is inside', cards=[
            card('three instruments', [
                S('RHYTHM CONDUCTOR plays KIT A, KIT B and Dillo as one band, but still lets one player take a different section.', TOP + 'BB_TOP_CONDUCTOR', NAV21 + '`REC` plus a section key'),
                S('BIG BEAT is the drum machine: KIT A with an optional KIT B layer. DILLO is the monophonic acid bass, with its own playable screen.', TOP + 'BB_TOP_BIG_BEAT', TOP + 'BB_TOP_DILLO', NAV + '"LEVEL", "CUTOFF", "RESO", "ENV MOD", "DECAY", "ACCENT", "DRIVE", "SLIDE", "WAVE"')],
                img('studio', 'RHYTHM CONDUCTOR: KIT A, KIT B and Dillo playing Blue Monday'), span=6),
            card('the library', [
                S('SONGS: 67 named songs. PRESETS: the four Roland rhythm machines, CR-8000, CR-78, TR-66 and TR-77, with 138 rhythms. ERAS: 21 eras with 168 parts. ACID: 27 banks with 205 phrases for Dillo, on Dillo\'s PATTERN page.', CAT, CATJ, VIEW),
                S('Eight drum machines and four USER kits of your own; nine Dillo sounds.', 'firmware/src/bb_ui_kit_label.h::"Linn LM-1"', UK + '#define BB_UK_SLOTS 4u', COREH + '#define BB_DILLO_PATCHES 9'),
                S('Chapter 15 lists all of it.')], img('studio-songs', 'The song list'), span=6),
        ]),
        sec('1.3', 'jump in', cards=[
            card('press play', [S('The welcome card offers a song. Turn {ALGO} for the next song, then press {PLAY} to start the band and open RHYTHM CONDUCTOR. {EDIT} or a white key opens the full song list.', UI + 'ALGO: NEXT SONG', UI + 'EDIT OR WHITE KEY: SONG LIST', CONDUCT + 'static int bb_cd_play(void)', CONDUCT + 'static void bb_cd_choose(void)')], img('welcome', 'Welcome: choose the next song and press PLAY'), span=12),
            card('get started', [S('In a hurry? Chapter 8 plays a song with the whole band, then builds a beat of your own.')],
                 '<a class="jump" href="/fm1/guide/8/">8 · get started →</a>', span=12, cls='wide')]),
    ]))
    # =================================================================== 2
    where('2')
    ch.append(dict(num=2, title='hardware overview', intro=[
        S('The FM-1 has four large encoders on the left, four knobs and twelve buttons on the right, two octave buttons and a 27-key keyboard.',
          NAVH + 'BB_NAV_B_FX, BB_NAV_B_SCL', NAVH + 'BB_NAV_E_SELECT, BB_NAV_E_ALGO, BB_NAV_E_PRESET', NAVH + '#define BB_NAV_KEYS 27'),
        S('This guide names every control the way the firmware does.')], sections=[
        sec('2.1', 'the panel', cards=[
            card('front panel', [], panel(lit=['MASTER', 'SELECT', 'ALGO', 'PRESETS', 'K1', 'K2', 'K3', 'K4'] + [b for b in
                 ['FX', 'SCL', 'ENV', 'LFO', 'EDIT', 'GLO', 'HOME', 'SAVE', 'ARP', 'SEQ', 'PLAY', 'REC', 'OCT−', 'OCT+']], screen=SCR + 'studio.png', keys={},
                 alt='The FM-1 front panel with every control'), span=12, cls='wide'),
        ], extra=table(['control', 'what it does'], [
            ['{MASTER}', S('The volume, for everything. It is the hardware level, not a page.', INST + 'MASTER turned fully down')],
            ['{SELECT}', S('On a HOME landing, chooses RHYTHM CONDUCTOR, BIG BEAT or DILLO and changes the screen immediately. Inside an editor it keeps the local player-focus job. Hold {REC} and turn it to change the tempo.', NAVF + 'if(bb_nav.landing)bb_nav_mode_move(d);else bb_nav_focus_move(d);', TOP + 'bb_top_mode_step', NAVH + 'BB_NAV_TO_TEMPO')],
            ['{ALGO}', S('Opens PATTERN and moves through eras, songs and, on Dillo, the ACID phrases. On the song list it moves the cursor without leaving it. Hold {REC} and turn it to queue the next section.', NAVH + 'BB_NAV_TO_PATTERN', NAV + 'else if (s->page == BB_NAV_STUDIO && s->bank == BB_NAV_HOME_SONGS) { r->target = BB_NAV_TO_PATTERN', NAVH + 'BB_NAV_TO_SECTION')],
            ['{PRESETS}', S('Opens KIT and moves through the kits, or Dillo\'s sounds while the keys keep playing notes. Hold {REC} and turn it to choose the drum voice.', NAVH + 'BB_NAV_TO_KIT', NAV + '2.1 P2: Dillo PATCH: keys stay notes', NAVH + 'BB_NAV_TO_VOICE')],
            ['{K1-K4}', S('Four knobs. Their job is printed at the bottom of the screen and changes with the page.', NAV + 'void bb_nav_knobs')],
            ['{HOME}', S('Back to the selected instrument: RHYTHM CONDUCTOR, BIG BEAT or DILLO. Hold it for help.', NAV + '6. HOME release: a clean tap goes home', NAVF + 'HOME always restores the selected destination', NAV + 's->help = 1; s->home_used = 1;')],
            ['{PLAY}', S('Start and stop, from any page. On the song list it plays the song under the cursor.', NAV + '7. PLAY: transport from every page', NAVF + 'PLAY on the song list = CHOOSE + start + PERFORM')],
            ['{REC}', S('The shift button. Hold it and press or turn a second control (chapter 5).', NAV + 'const int rec = (held & B(REC)) != 0;')],
            ['{EDIT}', S('Confirms or applies. The screen says what it will do on this page.', NAV + 'const char *bb_nav_edit_label', UI + 'what EDIT does here, in grey')],
            ['{OCT−} {OCT+}', S('Page a list, a step bank, the section keys, an FX tab or the octave, depending on the page. Inside help they turn the help pages.', NAVH + 'BB_NAV_BR_OCT_STEPS, BB_NAV_BR_OCT_KEYS, BB_NAV_BR_OCT_LIST, BB_NAV_BR_OCT_SECTIONS, BB_NAV_BR_OCT_BANK', NAVH + 'OCT-/+ page inside HELP')],
            ['{FX} {SCL} {ENV} {LFO} {GLO} {SAVE} {ARP} {SEQ}', S('Each opens its page or does its one job: chapter 9.', NAV + 'static int nav_button')],
        ])),
        sec('2.2', 'the keyboard', cards=[
            card('white and black keys', [
                S('The 16 white keys are numbered 1 to 16 from the left. The 11 black keys are B1 to B11 from the left.', NAVH + 'bit 0..15 = white keys 1..16', NAVH + 'bit 16..26 = black keys 1..11'),
                S('The keyboard runs from F to G; as notes, the bottom F is MIDI note 41.', NAVH + 'The FM-1 keyboard runs', NAV + '#define NOTE_BASE 41')],
                panel(keys={f'W{i}': '' for i in range(1, 17)} | {f'B{i}': '' for i in range(1, 12)}, alt='The keyboard: white keys 1 to 16, black keys B1 to B11'), span=12, cls='wide'),
        ]),
        sec('2.3', 'connections', cards=[
            card('usb', [S('USB carries the installer, MIDI in and out, and MIDI clock out.', MIDD + 'USB OUT / NO USB THRU', MIDU + 'c.clock_out')], gesture('USB'), span=6),
            card('trs midi in', [S('The TRS input takes MIDI notes and clock, like USB. Choose the clock source on MIDI CLOCK.', MIDI + 'source!=BB_KEY_USB&&source!=BB_KEY_TRS', MIDD + 'value=bb_midi_compat_config.clock_source==BB_MIDI_CLOCK_TRS')], gesture('TRS'), span=6),
        ]),
    ]))
    # =================================================================== 3
    where('3')
    ch.append(dict(num=3, title='power on and install', intro=[
        S('BB1 starts on a welcome card, ready to play.', NAVF + 'power-on is the song list, and PLAY plays it'),
        S('Installing and going back happen on the installer page in your browser; it is the authority for both.', INST + 'Install BB1')], sections=[
        sec('3.1', 'power on', cards=[
            card('first screen', [S('The welcome card starts with Planet Rock. Turn {ALGO} to choose a song. {EDIT} or a white key opens the full list; there {ALGO} or white keys {1}–{16} select an entry; {B5} {B6} {B7} switch between SONGS, ERAS and PRESETS. The bottom line says what {PLAY} will do.', NAVF + 'power-on is the song list, and PLAY plays it', NAV + 'if (white) return BB_NAV_K_SONG_PICK;', UIF + 'static const char *const coll[3] = {"SONGS", "ERAS", "PRESETS"};', UI + '"ALGO: NEXT SONG"')],
                 img('welcome', 'Welcome card after choosing Blue Monday'), strip_name='welcome', span=6),
            card('then', [S('Press {PLAY}: KIT A, KIT B and Dillo load the song together, it starts, and RHYTHM CONDUCTOR shows its sections.', NAVF + 'PLAY on the song list = CHOOSE + start + PERFORM', CONDUCT + 'static void bb_cd_choose_start(int start) {')], gesture('TURN:ALGO > PLAY'), span=6),
        ]),
        sec('3.2', 'install or update', cards=[
            card('before you click', [
                S('Use desktop Chrome on a computer and a USB data cable plugged straight in.', INST + 'desktop Chrome on a computer', INST + 'A USB data cable, plugged straight into the computer'),
                S('Stop playback, turn {MASTER} fully down and close other music apps.', INST + 'Playback stopped, MASTER turned fully down', INST + 'Other music apps closed')], gesture('PLAY > TURN:MASTER'), span=6),
            card('one click', [
                S('<a href="/fm1/install/">Open the installer</a>. One click installs the firmware and the sound library it needs together; allow about five minutes.', INST + 'One click, about five minutes', INST + 'its matching sound library 2.1'),
                S('Do not unplug or power off the FM-1 while it is writing.', INST + "Don't unplug or power off while writing."),
                S('When it is done the FM-1 shows up as Big Beat.', INST + 'Big Beat')], '<a class="jump" href="/fm1/install/">install BB1 →</a>', span=6),
        ]),
        sec('3.3', 'the sound library', cards=[
            card('a matched pair', [
                S('The firmware and its sound library are installed as a pair. The library holds the sampled machines and the help texts.', 'firmware/src/bb/bb_text_block.h::BB_TX_FLASH_OFF', 'big-beat/integration/build/recipes/composition-21/README.md::The library argument is mandatory'),
                S('If the help texts are missing or from another library, help pages read `HELP UNAVAILABLE`. Every control still works.', 'firmware/src/bb/bb_text_block.h::"HELP UNAVAILABLE"', UI + 'block missing or stale: say so'),
                S('Reinstall from the installer page to put the matching library back.', INST + 'its matching sound library 2.1')], img('help-play', 'HELP · PLAY'), span=12, cls='wide'),
        ]),
        sec('3.4', 'going back', cards=[
            card('another firmware', [
                S('The installer can put the official FM-1 firmware back. Keep a backup made with the firmware you plan to return to.', INST + 'Remove BB1 / return to official V15', INST + 'Keep a backup made with the firmware you plan to return to.'),
                S('The official firmware cannot open BB1 music: save the projects, scenes and USER-kit information you need first, as the installer page\'s backup checklist says.', INST + "The official firmware can't open BB1 music.", INST + 'backup checklist')], gesture('USB'), span=12, cls='wide'),
        ]),
    ]))
    # =================================================================== 4
    where('4')
    def smapf(shot, alt, marks, items):
        return ('<div class="smap"><div class="smap-img">' + img(shot, alt) +
                ''.join(f'<span class="mk" style="left:{x}%;top:{y}%">{n}</span>' for n, x, y in marks) +
                '</div><ol class="smap-list">' + ''.join(f'<li><b>{n}</b> {S(tx, *ss)}</li>' for n, tx, *ss in items) + '</ol></div>')
    smap = smapf('play-a', 'PLAY, numbered', [('1', 3, 2), ('2', 37, 2), ('3', 62, 2), ('4', 79, 2), ('5', 95, 2), ('6', 3, 12), ('7', 3, 21), ('8', 50, 52), ('9', 50, 77), ('10', 3, 86)], [
        ('1', 'page and the control that opens it', NAV + 'const char *bb_nav_page_label'), ('2', 'the active instrument or local player, in its colour', UIF + 'in->instr_name = bb_nav_row'),
        ('3', 'beat dots', UIF + 'in->beat = (uint8_t)'), ('4', 'tempo in BPM', UIH + 'uint16_t bpm_x10;'), ('5', 'play or stop', UIF + 'in->running = bb_face.requested.running;'),
        ('6', 'what you are choosing or setting', UIH + 'const char *title, *subtitle;'), ('7', 'grey line: what EDIT does, or the state', UI + 'what EDIT does here, in grey'),
        ('8', 'the page itself: steps, lanes or meters', UIH + '/* STUDIO */'), ('9', 'key map: every key lit as its light is, bright, half-lit, pulsing or off; the key legend shows here for about 2 s', UIF + 'in->key_led[k] = bb_nav_l.key[k];', UIH + '#define BB_UI_LEGEND_MS 2000u'),
        ('10', 'the four knobs: name and value', UIH + 'bb_ui_knob knob[4];')])
    pmap = smapf('studio', 'RHYTHM CONDUCTOR, numbered', [('1', 3, 2), ('2', 3, 12), ('3', 3, 21), ('4', 50, 45), ('5', 3, 85), ('6', 3, 93)], [
        ('1', 'RHYTHM CONDUCTOR, the bandleader screen', NAV + 's->landing ? "CONDUCTOR"'), ('2', 'the focused player\'s song', UIF + 'in->perf_song = o;'),
        ('3', '{K1} KIT A, {K2} KIT B and {K3} DILLO are the three mixer levels; {B1} {B2} {B3} select and mute or solo them. When a section is queued, every part that will move pulses here.', UIF + 'BB_UI_LN_NEXT'),
        ('4', 'one button per section, on white keys {1}–{16}: dark = ready, full colour = playing, pulsing outline = next', UIF + 'in->tile_state[k] = s == bb_nav_f.section || ((bb_nav_f.sec_lit >> s) & 1u)'),
        ('5', 'what is playing, or what comes next; ALL means Kit A, Kit B and Dillo will move together', UI + 'if (qparts == 7u) PF_PUT("ALL > ");'), ('6', 'the direct routes into the selected instrument and back to the song list', UIF + 'in->footer = "ARP PLAY')])
    lmap = smapf('kit-a', 'A chooser list, numbered', [('1', 3, 2), ('2', 3, 30), ('3', 3, 9), ('4', 95, 9), ('5', 3, 93)], [
        ('1', 'page and the control that opens it', NAV + 'const char *bb_nav_page_label'), ('2', 'ten rows; the highlighted row is the cursor', UIF + 'RC2: the whole list (ten rows, scrolling)'),
        ('3', 'the mark: what is playing now', UIF + 'in->playing_item = valid && view.active_index'), ('4', 'the white key that picks the row', UIF + 'in->item_key[i] = (uint8_t)(drum && first + i < 16u ? first + i + 1u : 0u);'),
        ('5', 'which of how many, and what {EDIT} does or what is waiting', UIF + 'static void bb_v3_footer(const char *what')])
    ch.append(dict(num=4, title='the screen', intro=[
        S('Every page uses the same frame: a status bar on top, the page in the middle, the key map and the four knobs at the bottom.', UIH + 'knob bar + key map strip'),
        S('The screens in this guide are drawn by BB1\'s own display code.', RENDER)], sections=[
        sec('4.1', 'screen map', intro=[S('Most pages share one frame: a status bar on top, the page in the middle, the key map and the four knobs at the bottom. RHYTHM CONDUCTOR and the chooser lists use the whole screen.', UIH + 'knob bar + key map strip', UIF + 'RC2: the whole list (ten rows, scrolling)')],
            cards=[card('play', [], smap, span=12, cls='wide dark'), card('perform', [], pmap, span=12, cls='wide dark'), card('a chooser list', [], lmap, span=12, cls='wide dark')]),
        sec('4.2', 'feedback', cards=[
            card('knob name and value', [S('Turn a knob and its name and value show over its column for about a second, on every page.', UIH + '#define BB_UI_POPUP_MS 1000u')], img('play-a-turn', 'K2 SN/CL 55'), strip_name='play-a', span=4),
            card('key legend', [S('Enter a page, or press a key that has no job there, and a one-line key legend shows for about two seconds. B1–B11 are the black keys.', UIH + '#define BB_UI_LEGEND_MS 2000u', 'firmware/src/bb_ui_v3_firmware.inc::BB_EV_LEGEND: the page')], img('play-a-legend', 'PLAY with its key legend'), strip_name='play-a', span=4),
            card('queued, then applied', [S('A change waits for the next bar: its key pulses and the screen says `QUEUED`. When the music has it the key turns bright and the screen says `APPLIED`.', TX + 'TX_KIT_QUEUED', TX + 'TX_KIT_APPLIED', NAVH + 'pulse (BREATH) = waiting')], img('notice-kit-applied', 'KIT APPLIED'), strip_name='play-a', span=4),
        ]),
        sec('4.3', 'help', cards=[
            card('hold home', [S('Hold {HOME} on any page, SONG and MIDI included. The lights stay on and the help lists what each lit key, button and knob does here. Let go to return.', NAV + 's->help = 1; s->home_used = 1;', NAV + 'HELP keeps every light (HOME bright as the help mark)', UI + '"Let go of HOME to return"'),
                               S('{OCT−} {OCT+} turn the help pages; the page count shows top right.', NAVH + 'OCT-/+ page inside HELP', UI + 'OCT turns the page')], gesture('HOLD:HOME'), span=4),
            card('press to explain', [S('Keep holding {HOME} and press any key or button, or turn a knob: its row is picked out and the bottom line says what it does. Nothing else happens.', NAVH + 'while HOME is held, a key, button or knob is explained instead of acting', UIF + 'the control pressed while HOME is held'),
                                      S('{PLAY} still starts and stops.', NAV + '7. PLAY: transport from every page')], img('help-explain', 'HELP · STUDIO with B5 explained'), span=4),
            card('hold rec', [S('Hold {REC} on its own for a moment to see this page\'s REC chords; the lights and the key legend switch to what each key does with {REC}. Any button, key or knob hides the help and works as usual.', NAV + '2.0 N5: REC held alone for the hold time', NAVH + 'REC was held at the last scan: LEDs, legend and help use the REC map')], gesture('HOLD:REC'), span=4),
        ]),
        sec('4.4', 'the armadillo', cards=[
            card('attract', [S('Leave RHYTHM CONDUCTOR alone for 30 seconds while playing and Dillo the armadillo takes over the screen. Press anything to bring the page back.', UIH + '#define BB_UI_ATTRACT_MS 30000u', UIH + 'last input on STUDIO (attract after 30 s)')], img('attract', 'Dillo the armadillo'), span=6),
            card('colours', [S('KIT A is orange, KIT B is blue and Dillo is pink, on every page and lane.', UIH + '#define BB_UI_KIT_A 0xFD68u', UIH + '#define BB_UI_KIT_B 0x4E7Cu', UIH + '#define BB_UI_DILLO 0xFB14u')], img('studio-legend', 'STUDIO with the key legend'), span=6),
        ]),
    ]))
    # =================================================================== 5
    where('5')
    combos = [
        ['{REC} {+} {HOME}', S('All notes off; the music keeps playing.', NAVH + 'BB_NAV_A_PANIC', TX + 'TX_ALL_NOTES_OFF,')],
        ['{REC} {+} {SELECT}', S('Tempo.', NAVH + 'BB_NAV_TO_TEMPO')],
        ['{REC} {+} {ALGO}', S('Queue the next or previous section.', NAVH + 'BB_NAV_TO_SECTION')],
        ['{REC} {+} {PRESETS}', S('Drum voice; on Dillo STEPS, the step\'s pitch.', NAVH + 'BB_NAV_TO_VOICE', NAVH + 'BB_NAV_TO_STEP_PITCH')],
        ['{REC} {+} key', S('Accent the hit or note.', NAVH + 'sound keys: REC held = accent')],
        ['{REC} {+} {GLO}', S('Tap tempo, on every page: tap three or more times.', NAV + 'static void nav_tap', NAV + 'REC+GLO taps tempo on every page', TX + 'TX_TAP_TEMPO,')],
        ['{REC} {+} {FX} (hold)', S('Dub throw: the echo opens while you hold.', NAVH + 'BB_NAV_A_THROW_ON', TX + 'TX_DUB_THROW_HOLD')],
        ['{REC} {+} {SAVE}', S('Quick save to the current scene (stopped).', NAVH + 'BB_NAV_A_QUICK_SAVE')],
        ['{REC} {+} {ENV}', S('Dillo: the sound back to its preset.', NAVH + 'BB_NAV_A_REVERT', NAV + 'return drum ? BB_NAV_BR_NONE : BB_NAV_BR_REVERT;', TX + 'TX_SOUND_BACK_TO_ITS_PRESET')],
        ['{REC} {+} {ARP}', S('This layer on or off.', NAVH + 'BB_NAV_A_LAYER_ENABLE')],
        ['{REC} {+} {SEQ}', S('On PLAY or STEPS with a drum kit: record into the playing pattern. Elsewhere: the next 16-step bank.', NAV + 'if (rec && s->page == BB_NAV_PLAY && drum) return BB_NAV_BR_RECORD;', NAVH + 'BB_NAV_A_STEP_BANK_NEXT')],
        ['{REC} {+} {EDIT}', S('On PLAY or STEPS: Undo. On other pages: mute or unmute the layer.', NAV + 'if (rec) return s->page == BB_NAV_PLAY ? BB_NAV_BR_UNDO : BB_NAV_BR_MUTE;', NAV + 'case BB_NAV_BR_MUTE: o->actions |= BB_NAV_A_MUTE;')],
        ['{REC} {+} {SCL}', S('On Dillo: the next key you press sets the scale\'s root.', NAVH + 'BB_NAV_A_ROOT_WAIT', TX + 'TX_ROOT_PRESS_A_KEY')],
        ['{REC} {+} {LFO}', S('EVOLVE on or off.', NAVH + 'BB_NAV_A_EVO_TOGGLE')],
        ['{REC} {+} {OCT−/+}', S('On PATTERN: page the black section keys, eleven at a time, up to 55.', NAV + 'return next >= 0 && next < (int)nav_section_pages(s) ? BB_NAV_BR_OCT_SECTIONS', NAVH + '#define BB_NAV_SECTION_PAGES 5u')],
        ['{REC} {+} {B1-3} on CONDUCTOR', S('Focus and solo KIT A, KIT B or Dillo.', NAVF + 'bb_nav_conductor_focus(arg);bb_nav_instr_solo(arg);')],
        ['{REC} {+} {1-16} on CONDUCTOR', S('That section for the focused player only, on the bar.', NAV + 'return rec ? BB_NAV_K_SECTION : BB_NAV_K_CONDUCT;')],
        ['{REC} {+} {B1-4} on PLAY', S('Solo a drum group.', NAV + 'return rec ? BB_NAV_K_GROUP_SOLO : BB_NAV_K_GROUP_MUTE')],
        ['{REC} {+} {1-4} on SAVE', S('Load that scene (stopped).', NAV + 'rec ? BB_NAV_K_SLOT_LOAD : BB_NAV_K_SLOT')],
        ['{REC} {+} key on EVOLVE', S('Lock a step so EVOLVE leaves it alone.', NAV + 'if (rec) return BB_NAV_K_EVO_LOCK;')],
    ]
    ch.append(dict(num=5, title='keys and combos', intro=[
        S('{REC} is the shift button: hold it, then press or turn a second control.', NAV + 'const int rec = (held & B(REC)) != 0;'),
        S('Each key has one job per page, and its light says so: one rule on every page (5.5).', NAVH + 'Each key has exactly ONE role per', NAVH + 'Every key role and every button role has ONE descriptor')], sections=[
        sec('5.1', 'buttons', cards=[card('the twelve buttons', [], panel(lit=['FX', 'SCL', 'ENV', 'LFO', 'EDIT', 'GLO', 'HOME', 'SAVE', 'ARP', 'SEQ', 'PLAY', 'REC'], alt='The twelve buttons'), span=12, cls='wide')],
            extra=table(['button', 'press', 'opens'], [
                ['{HOME}', S('Return to the selected instrument; hold for help.', NAVF + 'HOME always restores the selected destination'), 'CONDUCTOR / BIG BEAT / DILLO'],
                ['{ARP}', S('Play the keys live.', NAV + 'return nav_enter_play(s, o, BB_NAV_PLAY_LIVE);'), 'PLAY'],
                ['{SEQ}', S('Write steps on the white keys.', NAV + 'return nav_enter_play(s, o, BB_NAV_PLAY_STEPS);'), 'STEPS'],
                ['{ENV}', S('Shape the drum voice or Dillo.', NAV + 'return nav_enter(s, o, BB_NAV_SOUND);'), 'SOUND'],
                ['{FX}', S('Five tabs: ECHO, TAPE DRIVE, SPRING, MOTION, SENDS.', NAV + 'nav_fx_bank_names[5] = {"ECHO", "TAPE DRIVE", "SPRING", "MOTION", "SENDS"}'), 'FX'],
                ['{LFO}', S('Opens EVOLVE; on EVOLVE each press is the next mode: EVOLVE, EUCLID, RANDOM.', NAV + 'return s->page == BB_NAV_EVOLVE ? BB_NAV_BR_MODE : BB_NAV_BR_EVOLVE;', NAVH + 'LFO (or black 3) on EVOLVE: next mode'), 'EVOLVE'],
                ['{GLO}', S('Tempo, swing, balance, transpose, MIDI and the SONG arranger.', NAV + 'if (!rec) return BB_NAV_BR_GLOBAL;', NAV + 'white 4 the SONG'), 'GLOBAL'],
                ['{SAVE}', S('Four scenes.', NAV + 'case BB_NAV_BR_SAVE_PAGE: return nav_enter(s, o, BB_NAV_SAVE);'), 'SAVE'],
                ['{SCL}', S('Drums: swing on or off. Dillo: the next scale.', NAV + 'o->actions |= BB_NAV_A_SWING_TOGGLE;', NAV + 's->scale = (uint8_t)((s->scale + 1u) % BB_NAV_SCALES);'), '—'],
                ['{EDIT}', S('Confirm, apply or store, as the screen says; on RHYTHM CONDUCTOR it opens the song list.', NAV + 'return s->bank == BB_NAV_HOME_SONGS ? BB_NAV_BR_CHOOSE : BB_NAV_BR_SONGS;'), 'the song list (from CONDUCTOR)'],
                ['{PLAY}', S('Start and stop; on the song list, load the selected song on KIT A, KIT B and Dillo together.', NAV + 'BB_NAV_A_TRANSPORT', NAVF + 'PLAY on the song list = CHOOSE + start + PERFORM'), '—'],
                ['{REC}', S('Shift.', NAV + 'const int rec')], ])),
        sec('5.2', 'encoders and knobs', cards=[
            card('select', [S('On a HOME landing, turn to choose RHYTHM CONDUCTOR, BIG BEAT or DILLO. On a deeper page it selects the local player instead.', NAVF + 'if(bb_nav.landing)bb_nav_mode_move(d);else bb_nav_focus_move(d);', TOP + 'bb_top_mode_name')], gesture('TURN:SELECT'), span=3),
            card('algo', [S('Turn to open PATTERN and browse.', NAV + 'if (nav_enter(s, o, BB_NAV_PATTERN))')], gesture('TURN:ALGO'), span=3),
            card('presets', [S('Turn to open KIT and browse.', NAV + 'if (nav_enter(s, o, BB_NAV_KIT))')], gesture('TURN:PRESETS'), span=3),
            card('k1–k4', [S('Turn to change what the bottom of the screen names.', NAV + '10. K1-K4: the page')], gesture('TURN:K1 K2 K3 K4'), span=3),
        ]),
        sec('5.3', 'rec combos', cards=[], extra=table(['combo', 'does'], combos)),
        sec('5.4', 'both octave buttons', cards=[
            card('oct− and oct+ together', [S('Holding {OCT−} and {OCT+} together is the FM-1\'s own update gesture: BB1 does nothing else while both are held. Leave it to the installer.', NAV + '1. UBOOT: both OCT held')], gesture('HOLD:OCT− + HOLD:OCT+'), span=12, cls='wide')]),
        sec('5.5', 'the lights', intro=[S('Every key and button follows one rule, on every page. The screen\'s key map, the key legend and the help come from the same table, so they always agree with the lights.', NAVH + 'LEDs, the legend, the EDIT caption and HELP are all read from these tables')],
            cards=[card('four states', [led_key()], '', span=12, cls='wide')],
            extra=table(['light', 'means'], [
                ['bright', S('On, playing, or the page you are on.', NAVH + 'bright (LIT) = on / playing / you are here')],
                ['half-lit', S('It works here. Half-lit is clearly visible, but dimmer than bright.', NAVH + 'half (DIM) = works here', 'firmware/src/bb_half_pick.h::BB_HALF_LIT_RELEASE')],
                ['pulse', S('Waiting for the bar, or for you: a queued section, kit or start, or a delete waiting for {EDIT}.', NAVH + 'pulse (BREATH) = waiting', NAV + 'if (song & BB_NAV_SONG_F_DELETE) return BB_NAV_LED_BREATH;')],
                ['off', S('Does nothing here, now. Pressing it does nothing.', NAVH + 'A dark key or button never acts')]])),
    ]))
    # =================================================================== 6
    where('6')
    conv = [('single press', 'HOME', 'A key on its own: press it and let go.'), ('combination press', 'HOLD:REC + HOME', 'Keys joined by a plus: hold the first, press the second.'),
            ('sequence press', 'ALGO > EDIT > PLAY', 'Keys joined by an arrow: one after the other.'), ('hold', 'HOLD:HOME', 'HOLD! above a key: keep it down.'),
            ('tap', 'TAP:GLO', 'TAP! above a key: tap it, in time.'), ('turn', 'TURN:K1', 'TURN! above a knob or encoder: turn it.'),
            ('hold and turn', 'HOLD:REC + TURN:SELECT', 'Hold the key and turn the knob.'), ('hold a key, turn a knob', 'HOLD:7 + TURN:K4', 'Hold the white key and turn K4.'),
            ('black keys', 'B1', 'Black keys are B1 to B11, white keys 1 to 16.')]
    ch.append(dict(num=6, title='guide conventions', intro=[
        S('Sometimes you press keys one after the other, sometimes together. These drawings show which.'),
        S('Screen text appears in capitals, as on the screen: `KIT APPLIED`.')], sections=[
        sec('6.1', 'drawings', cards=[card(t, [x], gesture(g), span=4) for t, g, x in conv]),
        sec('6.2', 'in the text', cards=[card('chips', [
            'Controls in a sentence are drawn small: {HOME}, {REC}, {SELECT}, {K1}, white key {7}, black key {B3}.',
            '{REC} {+} {SEQ} means hold REC and press SEQ. {ALGO} {>} {EDIT} means turn ALGO, then press EDIT.'], '', span=12, cls='wide')]),
    ]))
    # =================================================================== 7
    where('7')
    ch.append(dict(num=7, title='workflow', intro=[
        S('Tap {HOME}, then turn {SELECT}: RHYTHM CONDUCTOR, BIG BEAT and DILLO are three clear musical destinations. The screen changes with the instrument.', NAVF + 'bb_nav_mode_move', TOP + 'bb_top_mode_name'),
        S('Changes land on the bar, so you can line up the next one while this one plays.', TX + 'TX_NEXT_SECTION_QUEUED')], sections=[
        sec('7.1', 'three destinations', cards=[
            card('rhythm conductor', [S('RHYTHM CONDUCTOR is the bandleader view: all three players, the song sections and the next-bar cue in one place.', TOP + 'BB_TOP_CONDUCTOR', NAV21 + 'Conductor: coordinated section/composition view'), S('{B1} {B2} {B3} focus and mute KIT A, KIT B or Dillo; hold {REC} to solo. The focus also decides which player a one-player section cue moves.', NAVF + 'bb_nav_conductor_focus(arg);bb_nav_instr_mute(arg);', NAV + 'return rec ? BB_NAV_K_SECTION : BB_NAV_K_CONDUCT;')], img('studio', 'RHYTHM CONDUCTOR'), span=4),
            card('big beat', [S('BIG BEAT is the drum instrument. Its landing is the live drum-machine screen; KIT A is the main kit and KIT B is the optional second layer.', TOP + 'BB_TOP_BIG_BEAT', NAV21 + 'BIG BEAT'), S('From here, {PRESETS} opens drum kits, {ALGO} opens patterns and {SEQ} opens steps.', NAV + 'BB_NAV_TO_KIT', NAV + 'BB_NAV_TO_PATTERN', NAV + 'return nav_enter_play(s, o, BB_NAV_PLAY_STEPS);')], img('home-big-beat', 'BIG BEAT landing'), span=4),
            card('dillo', [S('DILLO has its own live acid-bass screen. Its keys, filter, patch and pattern controls are immediately visible instead of hiding behind a drum focus outline.', TOP + 'BB_TOP_DILLO', NAV21 + 'DILLO'), S('{PRESETS} opens sounds, {ALGO} opens ACID phrases and {SEQ} opens Dillo steps.', NAV + '2.1 P2: Dillo PATCH: keys stay notes', NAV + 'if (nav_enter(s, o, BB_NAV_PATTERN))')], img('home-dillo', 'DILLO landing'), span=4),
        ]),
        sec('7.2', 'a song, then sections', cards=[
            card('one song for the band', [S('On the song list, {PLAY} always loads the song on KIT A, KIT B and Dillo together and starts it. {EDIT} loads all three without starting: at once when stopped, on the next bar when playing.', NAVF + 'PLAY on the song list = CHOOSE + start + PERFORM', CONDUCT + 'unsigned move=0,take=7u;', CONDUCT + 'choosing a song loads its initial section on all three parts')], img('studio-songs', 'The song list'), span=4),
            card('one key moves the band', [S('On RHYTHM CONDUCTOR each white key is a section of the focused player\'s song, named on its button. Press one and every player on that song moves to it on the next bar, all together or not at all.', NAV + 'white 1-16 = the sections (16 a page', CONDUCT + 'All or nothing on')], img('studio-queued', 'RHYTHM CONDUCTOR: Fill 1 next'), span=4),
            card('mixed songs', [S('Players on another song keep playing where they are. Changing one player on its own never pulls the others back: only the song list sets several players to one song.', CONDUCT + 'every part on another song HOLDs', CONDUCT + 'that song moves to section n; every part on another song HOLDs')], img('studio-hold', 'KIT B selected; KIT A holds its own song'), span=4),
        ]),
        sec('7.3', 'kit, then pattern', cards=[
            card('choose a kit', [S('Turn {PRESETS}, then press {EDIT}. BB1 moves to PATTERN: `CHOOSE PATTERN / EDIT: CHOOSE`.', NAVH + 'After KIT_CONFIRM (or a kit key) succeeded: continue to PATTERN', PICKI + 'bb_say(BB_TX(TX_CHOOSE_PATTERN_EDIT_CHOOSE))')], gesture('TURN:PRESETS > EDIT'), span=4),
            card('confirm the pattern', [S('Press {EDIT} on PATTERN. The kit and the pattern arrive together on the next bar: `KIT QUEUED`, then `KIT APPLIED`.', TX + 'TX_KIT_QUEUED', TX + 'TX_KIT_APPLIED')], img('pattern-a-next', 'PATTERN with NEXT KIT: DrumTraks'), strip_name='pattern-a', span=4),
            card('next up', [S('Until then the bottom line reads `NEXT KIT:` and the kit\'s name.', UIF + 'const char *g = "NEXT KIT: "')], img('notice-kit-queued', 'KIT QUEUED'), span=4),
        ]),
        sec('7.4', 'edits are copies', cards=[
            card('your own copy', [S('The first step you change in a library pattern makes an editable copy; the original stays in the library.', TX + 'TX_EDIT_KEEPS_AS_A_COPY', NAVF + '2.0: first edit makes an editable copy')], gesture('SEQ > 7'), span=6),
            card('undo', [S('{REC} {+} {EDIT} on PLAY or STEPS undoes the last edit or kept take, up to eight back.', NAV + 'BB_NAV_A_UNDO', PE + 'enum { BB_PE_HISTORY = 8 };')], gesture('HOLD:REC + EDIT'), span=6),
        ]),
        sec('7.5', 'keeping it', cards=[
            card('scenes', [S('SAVE keeps the whole setup in one of four scenes (chapter 11.8).', TX + 'TX_SAVE_KEYS_1_4_EDIT_STORE')], gesture('SAVE > 1 > EDIT'), span=6),
            card('songs', [S('SONG, on {GLO} white key {4}, arranges sections into a song. It lives in memory: once you change it the grey line reads `EDITED RAM / PROJECT FILE TOOLS`.', NAV + 'white 4 the SONG', ARD + 'TX_EDITED_RAM_PROJECT_FILE_TOOLS')], gesture('GLO > 4'), span=6),
        ]),
    ]))
    # =================================================================== 8
    where('8')
    steps = [
        ('volume down', 'TURN:MASTER', [S('Turn {MASTER} down. Start low.', INST + 'MASTER turned fully down')], None),
        ('the song list', 'HOME > EDIT', [S('Switch on: BB1 opens on the welcome card. Press {EDIT} for the song list. From a playing page, press {HOME}, then {EDIT}.', NAVF + 'power-on is the song list, and PLAY plays it', NAV + 'return s->bank == BB_NAV_HOME_SONGS ? BB_NAV_BR_CHOOSE : BB_NAV_BR_SONGS;')], 'studio-songs'),
        ('pick a song', 'TURN:ALGO', [S('Turn {ALGO} or press a white key {1}–{16}. {OCT−} {OCT+} turn the list page.', NAV + 'if (white) return BB_NAV_K_SONG_PICK;', NAV + 'if (s->bank == BB_NAV_HOME_SONGS) return BB_NAV_BR_OCT_LIST;')], None),
        ('play it', 'PLAY > TURN:MASTER', [S('Press {PLAY}, then bring {MASTER} up. KIT A, KIT B and Dillo play the song together, with its own tempo and swing, and RHYTHM CONDUCTOR shows its sections.', NAVF + 'PLAY on the song list = CHOOSE + start + PERFORM', CONDUCT + 'a song brings its sheet tempo and swing')], 'studio'),
        ('mute one', 'B2', [S('Press {B2}: KIT B mutes and its name reads MUTED. Press it again to bring it back.', NAV + 'if (!white) return n < 3 ? (rec ? BB_NAV_K_INSTR_SOLO : BB_NAV_K_INSTR_MUTE)', UIF + 'BB_UI_LN_MUTED')], 'studio-muted-b'),
        ('move the band', '5', [S('Press a white key: the whole band moves to that section on the next bar. Until then its button and key pulse.', NAV + 'white 1-16 = the sections (16 a page', NAVH + 'pulse (BREATH) = waiting')], 'studio-queued'),
        ('mix', 'TURN:K1', [S('Turn the four knobs: DRUM A, DRUM B, DILLO, FX RET.', NAV + 'r[0] = BB_NAV_R_MIX_DRUM_A')], None),
        ('a second machine', 'HOME > TURN:SELECT > TURN:PRESETS > EDIT', [S('Tap {HOME}, turn {SELECT} to BIG BEAT, then turn {PRESETS} to choose a machine. KIT B is the optional second drum layer inside BIG BEAT.', TOP + 'BB_TOP_BIG_BEAT', NAVH + 'After KIT_CONFIRM')], 'kit-b'),
        ('dillo\'s sound', 'HOME > TURN:SELECT > TURN:PRESETS', [S('Tap {HOME}, turn {SELECT} to DILLO, then turn {PRESETS} while you play the keys: each sound arrives when you stop turning.', TOP + 'BB_TOP_DILLO', NAV + '2.1 P2: Dillo PATCH: keys stay notes', AUDH + '#define BB_AUD_SETTLE_MS 350u')], 'kit-dillo'),
        ('echo', 'FX > TURN:K4', [S('Press {FX}. {K4} is ECHO WET: how much echo you hear.', NAV + 'BB_NAV_R_ECHO_TIME, BB_NAV_R_ECHO_FDBK, BB_NAV_R_ECHO_HEADS, BB_NAV_R_ECHO_WET')], 'fx-echo'),
        ('throw it', 'HOLD:REC + HOLD:FX', [S('Hold {REC} and hold {FX}: the echo opens while you hold, then snaps back.', NAVH + 'BB_NAV_A_THROW_ON', NAVH + 'BB_NAV_A_THROW_OFF')], None),
        ('keep it', 'PLAY > SAVE > 1 > EDIT', [S('Stop, press {SAVE}, pick a scene with keys {1}–{4} and press {EDIT}: `SAVED / FLASH VERIFIED`.', NAV + 'if (white && n < 4) return rec ? BB_NAV_K_SLOT_LOAD : BB_NAV_K_SLOT;', SESS + 'bb_session_message="SAVED / FLASH VERIFIED"')], 'notice-saved'),
    ]
    cards = []
    for i, (t, g, body, shot) in enumerate(steps):
        cards.append(card(f'<span class="n">{i+1}</span>{t}', body, gesture(g) + (img(shot, t, 'shot sm') if shot else ''), span=6, cls='step'))
    where('8')
    steps2 = [
        ('steps', 'SEQ', [S('Press {SEQ}. The 16 white keys switch steps 1–16 on and off for the selected voice.', NAV + 'if (white) return BB_NAV_K_STEP;')], 'steps-a'),
        ('voice', 'B1 > HOLD:REC + TURN:PRESETS', [S('Black keys {B1}–{B8} choose KICK to HIGH CONGA; {REC} {+} {PRESETS} reaches every voice.', NAV + 'if (n < 8) return BB_NAV_K_VOICE;', 'firmware/src/bb_rich_panel.inc::"KICK","SNARE","LOW TOM"')], None),
        ('accent', 'HOLD:B9 + 5', [S('Hold {B9} while you set a step to accent it.', NAV + 'n == 8 ? BB_NAV_K_ACCENT_HOLD')], None),
        ('ratchet', 'HOLD:7 + TURN:K4', [S('Hold a step key and turn {K4}: that step repeats x1 to x4. K4 reads `RATCHET` while you hold.', NAV + 'labels[3] = "RATCHET";', TX + 'TX_RATCHET_X1_X4')], 'steps-a-ratchet'),
        ('more steps', 'OCT+', [S('{OCT−} {OCT+} or {B10} {B11} move between 16-step banks; a pattern has up to 64 steps.', NAV + 'case BB_NAV_BR_OCT_STEPS: o->oct = BB_NAV_OCT_STEP_BANK;', COREH + '#define BB_STEPS 64')], None),
        ('undo', 'HOLD:REC + EDIT', [S('{REC} {+} {EDIT} undoes the last change.', NAV + 'BB_NAV_A_UNDO')], None),
    ]
    cards2 = [card(f'<span class="n">{i+1}</span>{t}', b, gesture(g) + (img(s, t, 'shot sm') if s else ''), span=4, cls='step') for i, (t, g, b, s) in enumerate(steps2)]
    ch.append(dict(num=8, title='get started', intro=[S('Let\'s play a song with the whole band, move it section by section, then add a machine, a sound, echo and a saved scene.'),
        S('Then write steps of your own.')], sections=[sec('8.1', 'play a song with the band', cards=cards), sec('8.2', 'write your own steps', cards=cards2)]))
    # =================================================================== 9 pages
    where('9')
    def pagesec(num, title, purpose, shotname, kmname, lit, rows, more=(), knobs=True, override=None, shot2=None):
        art = panel(lit=lit, screen=SCR + shotname + '.png', keys=page_keys(kmname, override) if kmname else {}, alt=f'{title}: the controls that work here')
        cs = [card('key map', [], art + (knobrow(kmname) if kmname and knobs else ''), span=8, cls='map'),
              card('on screen', [], img(shotname, title) + (img(shot2, title, 'shot sm') if shot2 else ''), strip_name=shotname, span=4)]
        return sec(num, title, intro=[purpose], cards=list(cs) + list(more), extra=table(['control', 'does'], rows) if rows else '')
    P = []
    def kmcard(title, km, shot, lit):
        return [card(title, [], panel(lit=lit, screen=SCR + shot + '.png', keys=page_keys(km), alt=f'{title}: the controls that work here') + knobrow(km), span=8, cls='map'),
                card('on screen', [], img(shot, title), strip_name=shot, span=4)]
    FOC = NAV + 'white 1-16 = the sections (16 a page'
    songsrows = [
        ['{1-16} or {ALGO}', S('Move the cursor on this page of the list. {OCT−} {OCT+} turn the page.', NAV + 'if (white) return BB_NAV_K_SONG_PICK;', NAV + 'if (s->bank == BB_NAV_HOME_SONGS) return BB_NAV_BR_OCT_LIST;')],
        ['{B5} {B6} {B7}', S('SONGS, ERAS or PRESETS: the tab shown is bright and underlined on screen.', NAVH + 'HOME/SONGS black 5-7', UIF + 'in->tab = coll; in->tabs = 3; in->tab_cur = bb_cd.coll;')],
        ['{PLAY}', S('Loads the song\'s first section on KIT A, KIT B and Dillo, all together, with the song\'s own tempo and swing; then RHYTHM CONDUCTOR. If BB1 is too busy it says `BUSY - RETRY` and does not start.', NAVF + 'PLAY on the song list = CHOOSE + start + PERFORM', CONDUCT + 'unsigned move=0,take=7u;', CONDUCT + 'bb_say(BB_TX(TX_BUSY_RETRY))')],
        ['{EDIT}', S('CHOOSE: loads it the same way without starting (on the next bar if already playing), then RHYTHM CONDUCTOR.', NAV + 'return s->bank == BB_NAV_HOME_SONGS ? BB_NAV_BR_CHOOSE', CONDUCT + 'static void bb_cd_choose(void){if(!bb_cd_open_list())bb_cd_choose_start(0);}')],
        ['{HOME}', S('To the selected instrument without choosing.', NAVF + 'HOME always restores the selected destination')],
        ['{K1-K4}', S('DRUM A, DRUM B, DILLO and FX RET levels, here and on RHYTHM CONDUCTOR.', NAV + 'r[0] = BB_NAV_R_MIX_DRUM_A')]]
    bandrows = [
        ['{1-16}', S('A section of the focused player\'s song, named on its button. Every player on that song moves to it on the next bar, all together or not at all. Players on another song keep playing.', FOC, CONDUCT + 'every part on another song HOLDs', CONDUCT + 'All or nothing on')],
        ['{OCT−} {OCT+}', S('The next sixteen sections; the screen says `SECTIONS 17-32`, and so on up to `SECTIONS 49-64`. Keys past the song\'s last section stay dark and do nothing.', NAV + 'static const char *const t[4] = {"SECTIONS 1-16", "SECTIONS 17-32", "SECTIONS 33-48", "SECTIONS 49-64"};', NAVH + '#define BB_NAV_CONDUCT_PAGES 4u', NAV + 'return arg < f->section_count ? BB_NAV_LED_DIM : BB_NAV_LED_DARK;')],
        ['{B1} {B2} {B3}', S('Focus and mute KIT A, KIT B or Dillo; with {REC}, focus and solo. Focus chooses the player for a one-player section cue.', NAVF + 'bb_nav_conductor_focus(arg);bb_nav_instr_mute(arg);', NAV + 'return rec ? BB_NAV_K_SECTION : BB_NAV_K_CONDUCT;')],
        ['change your mind', S('Press another section before the bar and the newest wins. Press the pulsing key again, or the one that is playing, to cancel. {PLAY} to stop cancels too: `CANCELLED BY STOP`.', CONDUCT + 'Newest key wins (cancel, then resubmit when the handoff is free)', CONDUCT + 'pulsing key again cancels; the bright (playing) key cancels a waiting cue', 'firmware/src/bb/bb_fault_notice.h::"CANCELLED BY STOP"'),
                                S('If the bar comes before a cancel can land, the section plays and the screen says `TOO LATE TO CANCEL`.', CONDUCT + 'bb_say("TOO LATE TO CANCEL")')],
        ['{SELECT}', S('On this landing, chooses RHYTHM CONDUCTOR, BIG BEAT or DILLO and changes the visible instrument. Use {B1}–{B3} for local player focus inside the Conductor.', NAVF + 'if(bb_nav.landing)bb_nav_mode_move(d);else bb_nav_focus_move(d);', NAVF + 'bb_nav_conductor_focus')],
        ['{REC} {+} {1-16}', S('That section for the focused player only.', NAV + 'return rec ? BB_NAV_K_SECTION : BB_NAV_K_CONDUCT;')],
        ['{EDIT}', S('Back to the song list.', NAV + 'return s->bank == BB_NAV_HOME_SONGS ? BB_NAV_BR_CHOOSE : BB_NAV_BR_SONGS;')],
        ['{ARP} {SEQ}', S('Open PLAY or STEPS.', NAV + 'return rec ? BB_NAV_BR_LAYER : BB_NAV_BR_LIVE;')]]
    P.append(sec('9.1', 'songs and rhythm conductor', intro=[
        S('RHYTHM CONDUCTOR is the coordinated playing screen: the song, its sections as buttons, and KIT A, KIT B and Dillo. {EDIT} opens the SONGS, ERAS and PRESETS list.', NAVH + 'enum { BB_NAV_HOME_CONDUCT, BB_NAV_HOME_SONGS };', NAV + 'if (s->page == BB_NAV_STUDIO) return s->bank == BB_NAV_HOME_SONGS ? "SONGS" : s->landing ? "CONDUCTOR" : "PERFORM";'),
        S('Tap {HOME} to return to the selected instrument. Turn {SELECT} there for RHYTHM CONDUCTOR, BIG BEAT or DILLO; {PLAY} on the song list loads all three players and returns to the Conductor.', NAVF + 'HOME always restores the selected destination', NAVF + 'bb_nav_mode_move', NAVF + 'PLAY on the song list = CHOOSE + start + PERFORM')],
        cards=kmcard('the song list', 'studio-songs', 'studio-songs', ['ALGO', 'EDIT', 'PLAY', 'HOME', 'OCT−', 'OCT+', 'K1', 'K2', 'K3', 'K4']) + [
            card('songs, eras, presets', [S('SONGS lists the 67 named songs, numbered 1–67. ERAS lists the 21 eras. PRESETS lists the four Roland rhythm machines: pick one and RHYTHM CONDUCTOR shows its rhythms.', CAT, VIEW, UIF + 'static const char *const coll[3] = {"SONGS", "ERAS", "PRESETS"};'),
                                           S('Dillo\'s ACID phrases are on Dillo\'s PATTERN page (9.5).', 'firmware/src/bb_ui_v3_firmware.inc::p->view.group == BB_BROWSER_ACID ? "ACID"')], img('studio-songs-presets', 'The song list: PRESETS'), span=4),
            card('eras', [], img('studio-songs-eras', 'The song list: ERAS'), span=4),
            card('change one later', [S('A song always loads KIT A, KIT B and Dillo together. To make your own combination, select a player afterward and choose its PATTERN independently.', CONDUCT + 'unsigned move=0,take=7u;', NAV + 'if (nav_enter(s, o, BB_NAV_PATTERN))')], img('studio-songs', 'The song list: every song loads all three'), span=4),
        ] + kmcard('rhythm conductor', 'studio', 'studio', ['HOME', 'EDIT', 'SELECT', 'OCT−', 'OCT+', 'K1', 'K2', 'K3', 'K4']) + [
            card('waiting for the bar', [S('A queued section pulses, on its button and its key, and the line under the buttons says so.', NAV + 'if ((q & BB_NAV_Q_SECTION) && (f->section_queued == arg', UI + 'AGAIN CANCELS')], img('studio-queued', 'RHYTHM CONDUCTOR: Fill 1 next bar'), span=4),
            card('stopped', [S('Stopped, the line reads `Ready:` and the section; {PLAY} starts it.', UI + 'else if (p && !nq) PF_PUT(in->running ? "Playing " : "Ready: ");')], img('studio-ready', 'RHYTHM CONDUCTOR, stopped'), span=4),
            card('muted', [S('A muted player reads MUTED in the part row.', UIF + 'BB_UI_LN_MUTED')], img('studio-muted-b', 'RHYTHM CONDUCTOR: KIT B muted'), span=4),
            card('mixed songs', [S('Here KIT B is selected and on Blue Monday. KIT A plays Sexual Healing, so it keeps playing while KIT B and Dillo move.', FOC, CONDUCT + 'every part on another song HOLDs')], img('studio-hold', 'KIT B selected; KIT A holds its own song'), span=4),
            card('all or nothing', [S('If one player cannot play the section, nothing changes and the screen names it: `CUE REFUSED: B NEEDS VOICES`. A cue that could not be ready in time says `MISSED THE BAR`; the music carries on.', CONDUCT + 'const char *s="CUE REFUSED: ";', CONDUCT + '"MISSED THE BAR"')], img('notice-cue-refused', 'CUE REFUSED: B NEEDS VOICES'), span=4),
            card('names always fit', [S('One to four sections make one row of tall buttons, five to eight two rows of four, nine to sixteen two rows of eight. A long name turns on its side (read bottom to top), or takes a fixed short form; no letter is ever cut.', 'firmware/src/bb_ui_v3/bb_ui.c::pf_side', 'claude/2.1/rc2/README.md section 3 (name fit: 744 names, 0 lint failures)')], img('studio-cr78', 'RHYTHM CONDUCTOR: Roland CR-78, 11 sections'), span=4),
        ] + kmcard('the last page of the tr-66', 'studio-p4', 'studio-p4', ['OCT−', 'SELECT']) + [
            card('every section has a key', [S('Roland TR-66 has 54 rhythms: {OCT+} three times shows 49–54 on keys {1}–{6}, and the rest stay dark. The CR-8000 has 43 on three pages.', CATJ, NAVH + '#define BB_NAV_CONDUCT_PAGES 4u', NAV + 'return arg < f->section_count ? BB_NAV_LED_DIM : BB_NAV_LED_DARK;')], gesture('OCT+ > OCT+ > OCT+ > 2'), span=6),
            card('song playing', [S('While the SONG arranger plays an arrangement, the section keys are off and say `SONG PLAYING`.', CONDUCT + 'bb_say("SONG PLAYING")', CONDUCT + 'f->section_count=0;')], '', span=6),
        ], extra=table(['the song list', 'does'], songsrows) + table(['rhythm conductor', 'does'], bandrows)))
    P.append(pagesec('9.2', 'play', S('PLAY · ARP turns the keys into drum pads, or Dillo\'s keyboard.', NAV + 'return nav_is_drum(s) ? BB_NAV_KEYS_PADS : BB_NAV_KEYS_NOTES;'), 'play-a', 'play-drums', ['ARP', 'SCL', 'K1', 'K2', 'K3', 'K4'], [
        ['{1-11}', S('Pads: kick, snare, low, mid and high tom, rim, clap, cowbell, cymbal, open hat, closed hat. Hold {REC} for an accent.', NAV + 'if (n < 11) { *arg = n; return BB_NAV_K_PAD; }', MIDI + 'notes[BB_DRUM_TRACKS]={36,38,45,47,50,37,39,56,49,46,42}')],
        ['{B1-4}', S('Mute KICK, SN/CL, HATS or PERC; with {REC}, solo.', NAV + 'if (n < 4) { *arg = n; return rec ? BB_NAV_K_GROUP_SOLO')],
        ['{B6} {B7}', S('Previous or next section, on the bar.', NAV + 'if (n == 5) return BB_NAV_K_SECTION_PREV;')],
        ['{B8-11}', S('Kill DRUM A, DRUM B, DILLO or ALL DRUMS.', NAVH + 'BB_NAV_K_KILL,          /* arg 0 DRUM A, 1 DRUM B, 2 DILLO, 3 ALL DRUMS */')],
        ['{K1-K4}', S('KICK, SN/CL, HATS and PERC group levels.', NAV + 'r[0] = BB_NAV_R_GROUP_KICK')],
        ['{SCL}', S('Swing on or off: `SWING ON`, `SWING OFF`.', NAV + 'BB_NAV_A_SWING_TOGGLE', TX + 'TX_SWING_ON')]],
        more=[card('read the pads', [S('The screen mirrors the playing keys: black-key controls across the top, eleven named drum pads below. Pad colours group the voices; flashes follow pattern hits or your fingers.', PLAYUI + 'DRUM PADS', 'firmware/src/bb_ui_play_firmware.inc::a pad flashes when the step position reaches a hit')], '', span=12), card('dillo on play', [S('With DILLO selected the keys play notes; {OCT−} {OCT+} move them up to two octaves either way, and {SCL} steps through OFF, MINOR, MAJOR, DORIAN and PENTA.', NAV + 'if (next < -2 || next > 2)', NAV + '{ "OFF", "MINOR", "MAJOR", "DORIAN", "PENTA" }'),
                                          S('The keyboard shows the notes under your fingers. Above it, the filter curves respond to cutoff, resonance and envelope settings.', PLAYUI + 'DILLO KEYS', PLAYUI + 'pl_curve(pl_fc(in->cutoff'),
                                          S('Knobs: CUTOFF, RESO, ENV MOD, DECAY.', NAV + 'r[0] = BB_NAV_R_CUTOFF; r[1] = BB_NAV_R_RESO; r[2] = BB_NAV_R_ENV; r[3] = BB_NAV_R_SYNTH_DECAY;')],
                   img('play-dillo', 'PLAY on Dillo'), strip_name='play-dillo', span=12, cls='wide')]))
    P.append(pagesec('9.3', 'steps', S('STEPS · SEQ writes a pattern step by step on the white keys.', NAV + 'if (white) return BB_NAV_K_STEP;'), 'steps-a', 'steps-drums', ['SEQ', 'OCT−', 'OCT+', 'K4', 'PRESETS'], [
        ['{1-16}', S('Steps 1–16 of this bank, for the selected voice.', NAV + 'if (white) return BB_NAV_K_STEP;')],
        ['{B1-8}', S('Choose KICK, SNARE, LOW, MID, HIGH TOM and LOW, MID, HIGH CONGA.', NAV + 'if (n < 8) return BB_NAV_K_VOICE;', 'firmware/src/bb_rich_panel.inc::"KICK","SNARE","LOW TOM","MID TOM","HIGH TOM","LOW CONGA"')],
        ['{B9}', S('Hold to write accented steps.', NAV + 'BB_NAV_K_ACCENT_HOLD')], ['{B10} {B11}', S('Previous or next bank.', NAV + 'BB_NAV_K_BANK_PREV : BB_NAV_K_BANK_NEXT')],
        ['hold a step {+} {K4}', S('Ratchet x1–x4.', NAV + 'r->target = BB_NAV_TO_RATCHET;')],
        ['Dillo: {B1-11}', S('The selected step\'s pitch. {EDIT} sets the step type: GATE, ACCENT or SLIDE.', NAV + 'return BB_NAV_K_STEP_PITCH;', NAVH + 'EDIT on Dillo steps: GATE/ACCENT/SLIDE')]],
        more=[card('read the drum steps', [S('The selected drum voice has sixteen tall step keys. The smaller rows beneath show the other drum lanes; divisions within a step show ratchets.', PLAYUI + 'DRUM STEPS', PLAYUI + 'ratchet: the key split in x2..x4')], '', span=6),
              card('read Dillo steps', [S('Pitch bars show the notes, arcs join slides, and accent marks show emphasis. The black-key row names the pitches available for the selected step.', PLAYUI + 'DILLO STEPS: the 303 sheet', PLAYUI + 'slide: an arc')], '', span=6)],
        shot2='steps-dillo'))
    P.append(pagesec('9.4', 'kit', S('KIT · PRESET is the full-screen list of kits for KIT A or KIT B, or Dillo\'s sounds (PATCH · PRESET): ten rows, the playing one marked, the white key of each row on the right, and one line at the bottom. Turn a knob and its value shows at the bottom for about a second.', UIF + 'RC2: the whole list (ten rows, scrolling)', UIF + 'in->item_key[i] = (uint8_t)(drum && first + i < 16u ? first + i + 1u : 0u);', NAV + 'return nav_is_drum(s) ? BB_TX(TX_KIT_PRESET) : BB_TX(TX_PATCH_PRESET);'), 'kit-a', 'kit-a', ['PRESETS', 'EDIT', 'OCT−', 'OCT+', 'K1'], [
        ['{PRESETS}', S('Move through the list.', NAVH + 'BB_NAV_TO_KIT')], ['{1-16}', S('Pick straight from this page of the list.', NAV + 'return white ? BB_NAV_K_PICK : BB_NAV_K_NONE;')],
        ['{OCT−} {OCT+}', S('The previous or next page of the list.', NAV + 'o->oct = BB_NAV_OCT_LIST_PAGE;')],
        ['{EDIT}', S('Drums: CHOOSE, then confirm a pattern. Dillo: KEEP.', NAV + 'return drum ? BB_NAV_BR_CHOOSE : BB_NAV_BR_LOAD;', NAV + '[BB_NAV_BR_LOAD] = {"KEEP"')],
        ['{B1}', S('KIT A only: BUILD, the kit builder (chapter 11.6).', NAV + 'if (!white && n == 0) return BB_NAV_K_BUILD;')],
        ['{K1}', S('Layer level.', NAV + 'r[0] = drum ? BB_NAV_R_LAYER_LEVEL : BB_NAV_R_SYNTH_LEVEL;')]],
        more=[card('the list', [S('Drums: 808, the seven sampled machines and, on KIT A, USER 1–4.', PICK + 'bb_sound_picker_machine_items', PICK + 'bb_sound_picker_user_name'),
                                S('The bottom line counts the list and says what {EDIT} does: `KITS` with your position in the list, or `LOADING` while a kit is on its way.', UIF + 'bb_v3_footer(drum ? "KITS" : "SOUNDS"', UIF + 'bb_sound_pick.pending ? "LOADING"')], img('kit-a-user', 'KIT: machine and USER kit list'), span=6),
              card('dillo sounds', [S('On DILLO the page is PATCH: the nine Dillo sounds, without key numbers, because the keys stay notes so you can keep playing while you choose.', COREH + '#define BB_DILLO_PATCHES 9', NAV + '2.1 P2: Dillo PATCH: keys stay notes'),
                                    S('Turn {PRESETS} and the sound under the cursor loads about a third of a second (350 ms) after you stop turning; sounds you scroll past are not loaded, and held notes keep sounding.', AUDH + '#define BB_AUD_SETTLE_MS 350u', 'firmware/src/bb_dillo_audition_firmware.inc::one command per settle, never per detent'),
                                    S('{EDIT} (KEEP) loads it now and stays on the page; `SOUND KEPT` shows once the new sound is playing. {HOME} puts back the sound you had before you started turning, your own edits included: `SOUND RESTORED`. Leaving any other way keeps the new sound.', 'firmware/src/bb_dillo_audition_firmware.inc::bb_say("SOUND KEPT")', 'firmware/src/bb_dillo_audition_firmware.inc::bb_say("SOUND RESTORED")', 'firmware/src/bb_dillo_audition_firmware.inc::page puts it back (SOUND RESTORED), any other exit keeps the new sound'),
                                    S('{OCT−} {OCT+} move the keys an octave, as on PLAY.', NAV + 'if (!drum) return nav_oct_room(s, dir, 1) ? BB_NAV_BR_OCT_KEYS : BB_NAV_BR_NONE;   /* 2.1 P2: as PLAY */')],
                    img('kit-dillo', 'PATCH: Dillo sounds, keys as notes'), span=6)]))
    P[-1]['cards'] += [card('dillo patch keys', [], panel(lit=['PRESETS', 'EDIT', 'HOME', 'OCT−', 'OCT+', 'K1'], screen=SCR + 'kit-dillo.png', keys=page_keys('kit-dillo'), alt='PATCH: the controls that work here') + knobrow('kit-dillo'), span=8, cls='map'),
                       card('home restores', [], img('notice-sound-restored', 'SOUND RESTORED'), span=4)]
    P.append(pagesec('9.5', 'pattern', S('PATTERN · ALGO is the list of patterns for the selected player: ERAS, then SONGS, and on Dillo the 27 ACID banks after them. It works only on that player; the song list (9.1) moves the band.', NAVH + 'BB_NAV_PATTERN,  /* ALGORITHM: browse pattern', UIF + 'p->view.group == BB_BROWSER_ACID ? "ACID"', CAT), 'pattern-a', 'pattern', ['ALGO', 'EDIT', 'OCT−', 'OCT+', 'K1', 'K2', 'K3', 'K4'], [
        ['{ALGO}', S('Move through the list, from one collection into the next; the bottom line says which: `ERAS 6/21`. {REC} {+} {ALGO} queues the next section of what plays.', UIF + 'p->view.group == BB_BROWSER_ACID ? "ACID" : p->view.group ? "SONGS" : "ERAS"', NAVH + 'BB_NAV_TO_PATTERN', NAVH + 'BB_NAV_TO_SECTION')],
        ['{1-16}', S('Pick from this page of the list.', NAV + 'return white ? BB_NAV_K_PICK')], ['{B1-11}', S('Launch a section of this player\'s pattern, on the bar. {REC} {+} {OCT−} {OCT+} page the keys, up to section 55.', NAV + 'return white ? BB_NAV_K_PICK : BB_NAV_K_SECTION;', NAV + 'return next >= 0 && next < (int)nav_section_pages(s) ? BB_NAV_BR_OCT_SECTIONS')],
        ['{EDIT}', S('CHOOSE: load it, on the bar while playing. Stopped, {PLAY} confirms and starts.', NAV + 'case BB_NAV_PATTERN: return BB_NAV_BR_CHOOSE;', NAV + 'o->pattern_play = s->page == BB_NAV_PATTERN ? 1u', TX + 'TX_CHOOSE_PATTERN_PLAY_START')],
        ['{K1-K4}', S('Drums: LEVEL, TUNE, DECAY, SWING. Dillo: CUTOFF, RESO, ENV MOD, DECAY.', NAV + 'r[0] = BB_NAV_R_LAYER_LEVEL; r[1] = BB_NAV_R_TUNE;')]],
        more=[card('substitutions', [S('If the kit lacks a voice the pattern asks for, the bottom line names the swap, for example `SUBST SY>CB +1`; a kit waiting for the bar reads `NEXT KIT:` and its name.', UIF + '"SUBST SY>CB +1"', UIF + 'const char *g = "NEXT KIT: "')], img('pattern-a-next', 'PATTERN: NEXT KIT'), span=6),
              card('acid', [S('On DILLO the list carries on from SONGS into ACID: 27 banks, 205 phrases.', CAT, UIF + 'p->view.group == BB_BROWSER_ACID ? "ACID"')], img('pattern-dillo-acid', 'PATTERN: ACID'), span=6)]))
    P.append(pagesec('9.6', 'sound', S('SOUND · ENV shapes the selected drum voice, or Dillo.', NAVH + 'BB_NAV_SOUND,    /* ENV: edit selected drum voice or the synth'), 'sound-a', 'sound-drums', ['ENV', 'OCT−', 'OCT+', 'K1', 'K2', 'K3', 'K4'], [
        ['{1-11}', S('Choose the drum voice.', NAV + 'if (white && n < 11) { *arg = n; return BB_NAV_K_VOICE; }')],
        ['{OCT−} {OCT+}', S('BASIC or COLOUR knobs.', NAV + 'case BB_NAV_INSTR_DRUM: return s->bank ? "COLOUR" : "BASIC";')],
        ['BASIC {K1-K4}', S('LEVEL, TUNE, DECAY, TONE.', NAV + 'r[0] = BB_NAV_R_VOICE_LEVEL; r[1] = BB_NAV_R_TUNE; r[2] = BB_NAV_R_DECAY; r[3] = BB_NAV_R_TONE;')],
        ['COLOUR {K1-K4}', S('Depends on the voice: kick TONE, ATTACK, DIST, LEVEL; snare TONE, SNAPPY, DIST, LEVEL; toms MODE, DRIVE, DIST, LEVEL; clap MODE, ATTACK, DIST, DRIVE; the rest DIST, TUNE, DECAY, DRIVE.', NAV + 'nav_colour_bass[4]', NAV + 'nav_colour_metal[4]')],
        ['Dillo {K1-K4}', S('ACCENT, DRIVE, SLIDE, WAVE; the keys play notes.', NAV + 'r[0] = BB_NAV_R_ACCENT; r[1] = BB_NAV_R_SYNTH_DRIVE; r[2] = BB_NAV_R_SLIDE; r[3] = BB_NAV_R_WAVE;')],
        ['{REC} {+} {ENV}', S('Back to the preset: `SOUND BACK TO ITS PRESET`.', NAVH + 'BB_NAV_A_REVERT', TX + 'TX_SOUND_BACK_TO_ITS_PRESET')]], shot2='sound-dillo'))
    fxrows = [['{1} ECHO', 'TIME MS', 'FDBK %', 'HEADS', 'ECHO WET'], ['{2} TAPE DRIVE', 'TONE %', 'DRIVE %', 'WEAR %', 'ECHO WET'], ['{3} SPRING', 'SIZE %', 'DAMP %', 'SPR WET', 'ECHO WET'],
              ['{4} MOTION', 'MOTION %', 'WEAR %', 'FDBK %', 'ECHO WET'], ['{5} SENDS', 'ECHO', 'SPRING', 'LEVEL (drums)', 'DRIVE (drums)']]
    S('FX tabs and their knobs: ECHO, TAPE DRIVE, SPRING, MOTION, SENDS.', NAV + 'static const uint8_t nav_fx_roles[4][4]', NAV + 'r[0] = BB_NAV_R_SEND_ECHO; r[1] = BB_NAV_R_SEND_SPRING;')
    P.append(sec('9.7', 'fx', intro=[S('FX holds the tape echo and the spring on five named tabs: ECHO, TAPE DRIVE, SPRING, MOTION and SENDS.', NAV + 'nav_fx_bank_names[5] = {"ECHO", "TAPE DRIVE", "SPRING", "MOTION", "SENDS"}'),
                                     S('White keys {1}–{5} choose a tab, the bright one is shown, and the tab row on screen names them. {OCT−} {OCT+} step through them. FX opens on the tab you used last.', NAV + 'return white && n < 5 ? BB_NAV_K_FX_TAB : BB_NAV_K_NONE;', NAVH + 'the FX tab last shown (FX opens on it)', UIF + 'in->tab = bb_v3_tabs; in->tabs = 5;')], cards=[
        card('key map', [], panel(lit=['FX', 'OCT−', 'OCT+', 'K1', 'K2', 'K3', 'K4'], screen=SCR + 'fx-echo.png', keys=page_keys('fx-0'), alt='FX: the controls that work here') + knobrow('fx-0'), span=8, cls='map'),
        card('read the effects', [S('ECHO lays out the selected delay heads and their repeats against four beats. TAPE DRIVE, SPRING and MOTION draw the effect settings; SENDS shows each player feeding echo and spring. These pictures respond to the controls; they are not recordings of the audio output.', PLAYUI + 'static PL_NOINLINE void pl_fx_echo', PLAYUI + 'static PL_NOINLINE void pl_fx_wave', PLAYUI + 'static PL_NOINLINE void pl_fx_sends', PLAYUI + 'each bank drawn from its knob values only')], '', span=12),
        card('echo', [S('{K4} ECHO WET is how much echo you hear, on every tab but SENDS.', NAV + 'BB_NAV_R_ECHO_TIME, BB_NAV_R_ECHO_FDBK, BB_NAV_R_ECHO_HEADS, BB_NAV_R_ECHO_WET')], img('fx-echo', 'FX ECHO'), strip_name='fx-echo', span=4),
        card('tape drive', [S('TAPE DRIVE saturates the echo\'s repeats only; the dry sound never passes through it. The screen says so: `ECHO ONLY · DRY DRIVE: SOUND`.', 'firmware/src/bb/bb_echo.c::record=e->low/(1.0f+drive*magnitude(e->low));', UIF + '"ECHO ONLY \\xB7 DRY DRIVE: SOUND"'),
                            S('Dry drive is on SOUND: Dillo\'s DRIVE, and DRIVE and DIST on a drum voice\'s COLOUR tab.', NAV + 'r[0] = BB_NAV_R_ACCENT; r[1] = BB_NAV_R_SYNTH_DRIVE;', NAV + 'nav_colour_bass[4]')], img('fx-tape', 'FX TAPE DRIVE'), strip_name='fx-tape', span=4),
        card('spring', [S('SPR WET is how much spring you hear.', NAV + 'BB_NAV_R_SPRING_WET, BB_NAV_R_ECHO_WET')], img('fx-spring', 'FX SPRING'), strip_name='fx-spring', span=4),
        card('sends', [S('SENDS sets how much of the selected player goes to the echo and the spring.', NAV + 'r[0] = BB_NAV_R_SEND_ECHO; r[1] = BB_NAV_R_SEND_SPRING;')], img('fx-sends', 'FX SENDS'), strip_name='fx-sends', span=4),
        card('dub throw', [S('{REC} {+} {FX}, held: the echo opens wide while you hold, then snaps back. On STUDIO, {K4} FX RET sets the overall effects level.', NAVH + 'BB_NAV_A_THROW_ON', NAV + 'BB_NAV_R_FX_RETURN')], gesture('HOLD:REC + HOLD:FX'), span=12, cls='wide'),
    ], extra=table(['tab', 'K1', 'K2', 'K3', 'K4'], fxrows)))
    P.append(pagesec('9.8', 'save', S('SAVE keeps the whole setup in one of four scenes.', NAVH + 'BB_NAV_SAVE,     /* SAVE: four session slots'), 'save', 'save', ['SAVE', 'EDIT', 'REC', 'K1'], [
        ['{1-4} or {K1}', S('Choose scene 1–4.', NAV + 'if (white && n < 4) return rec ? BB_NAV_K_SLOT_LOAD : BB_NAV_K_SLOT;', NAV + 'r[0] = BB_NAV_R_SLOT;')],
        ['{EDIT}', S('STORE, while stopped.', NAV + 'case BB_NAV_SAVE: return BB_NAV_BR_STORE;', NAV + 'case BB_NAV_BR_QUICK_SAVE: case BB_NAV_BR_STORE: return nav_save_blocked(f) ? BB_NAV_LED_DARK')],
        ['{REC} {+} {1-4}', S('Load that scene, while stopped: `LOADED / PRESS PLAY`.', NAV + 'BB_NAV_K_SLOT_LOAD', SESS + 'TX_LOADED_PRESS_PLAY')]]))
    P.append(pagesec('9.9', 'evolve', S('EVOLVE · LFO lets a pattern wander and come home, or generates new ones. {LFO} opens it; each further press steps the mode: EVOLVE, EUCLID, RANDOM. The mode is the title, with the three as tabs.', NAVH + 'BB_NAV_EVOLVE,   /* LFO: pattern movement', NAVH + 'LFO (or black 3) on EVOLVE: next mode', UIF + 'the mode is the title; EDIT: CAPTURE / APPLY'), 'evolve', 'evolve', ['LFO', 'EDIT', 'K1', 'K2', 'K3', 'K4'], [
        ['{LFO} or {B3}', S('The next mode, without changing the music. The tab row reads `LFO OR B3`.', NAV + 'return s->page == BB_NAV_EVOLVE ? BB_NAV_BR_MODE : BB_NAV_BR_EVOLVE;', NAV + 'if (n == 2) return BB_NAV_K_GEN_MODE;', UIF + 'in->tab_note = "LFO OR B3";')],
        ['{1-3}', S('EVOLVE: parts to evolve, KIT A, KIT B, Dillo; bright when chosen.', NAV + 'return n < 3 ? BB_NAV_K_EVO_KEY', NAV + 'case LR_EVO_PART: return (f->evo_parts >> arg) & 1u ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;')],
        ['{7-10}', S('EVOLVE: what may change, rhythm, pitch, accent, slide; bright when allowed.', NAV + '(n >= 6 && n <= 9) ? BB_NAV_K_EVO_MOVE', 'firmware/src/bb/bb_evolve.h::BB_EVOLVE_RHYTHM=1,BB_EVOLVE_PITCH=2,BB_EVOLVE_ACCENT=4,BB_EVOLVE_SLIDE=8')],
        ['{1-16} in EUCLID', S('On drums, the voice it writes; bright on it. RANDOM uses no white keys.', NAV + 'if (s->gen_mode == BB_NAV_MODE_EUCLID) return nav_is_drum(s) ? BB_NAV_K_GEN_VOICE : BB_NAV_K_NONE;')],
        ['{K1-K4}', S('EVOLVE: AMOUNT, EVERY, ROOT, SCALE. EUCLID and RANDOM: their own settings (chapter 11.4).', NAV + 'r[0] = BB_NAV_R_EVO_AMOUNT;', UIF + 'static const char *const eu[4] = {"HITS", "STEPS", "ROTATE", "ACCENTS"};')], ['{B1} {B2}', S('FREEZE, RETURN.', NAV + 'if (n == 0) return BB_NAV_K_EVO_FREEZE;')],
        ['{EDIT}', S('EVOLVE: CAPTURE, what plays becomes home. EUCLID and RANDOM: APPLY, the generator\'s pattern.', NAV + 'return f && f->gen_mode != BB_NAV_MODE_EVOLVE ? BB_NAV_BR_APPLY : BB_NAV_BR_CAPTURE;')],
        ['{REC} {+} white key', S('Lock that step, in every mode; bright when locked.', NAV + 'if (rec) return BB_NAV_K_EVO_LOCK;')],
        ['{REC} {+} {LFO}', S('EVOLVE on or off.', NAVH + 'BB_NAV_A_EVO_TOGGLE')]], shot2='evolve-euclid'))
    P.append(pagesec('9.10', 'global', S('GLOBAL · GLO sets tempo, swing, the A/B balance and Dillo\'s transpose, and opens the MIDI pages.', NAV + 'r[0] = BB_NAV_R_BPM; r[1] = BB_NAV_R_SWING; r[2] = BB_NAV_R_AB_BALANCE; r[3] = BB_NAV_R_TRANSPOSE;'), 'global', 'global', ['GLO', 'K1', 'K2', 'K3', 'K4'], [
        ['{K1}', S('BPM, 30 to 300.', NAVF + 'bb_send(BB_CMD_TEMPO,(unsigned)bb_limit((int)bb_face.requested.bpm+bb_limit(delta,-270,270),30,300))')],
        ['{K2}', S('Swing, 50 to 75 % in half steps.', NAVF + 'bb_send(BB_CMD_SWING_X2', SWING + 'bb_swing_valid')],
        ['{K3}', S('A/B BAL: turn right for more KIT B.', NAVF + 'Opposite-direction level pair: right = more DRUM B.')],
        ['{K4}', S('TRANSPOS: Dillo\'s pattern, up to 48 semitones either way.', NAVF + 'Dillo pattern transpose', 'firmware/src/bb/bb_transpose.h::#define BB_TRANSPOSE_LIMIT 48')],
        ['{1} {2}', S('MIDI CHANNELS and MIDI CLOCK (chapter 13). There, {B1} or {HOME} goes back, and {EDIT} or {SAVE} keeps the settings.', NAV + 'white 1 and 2 open the MIDI CHANNELS and MIDI CLOCK banks', NAV + 'if (!white) return n == 0 ? BB_NAV_K_BACK : BB_NAV_K_NONE;', NAV + 'return b == BB_NAV_B_EDIT || b == BB_NAV_B_SAVE ? BB_NAV_BR_GLOBALS_SAVE')],
        ['{4}', S('The SONG arranger (9.11).', NAV + 'white 4 the SONG', NAVF + 'GLOBAL white 4 SONG, white 5 the Dillo kit view')],
        ['{5}', S('With KIT A or KIT B selected: the drum view, which voices are triggering. {HOME} or {GLO} leaves. With DILLO selected it is off.', NAV + 'return n == 4 && nav_is_drum(s) && BB_NAV_HAS_DILLO_VIEW ? BB_NAV_K_SUBPAGE', 'firmware/src/bb_dillo_kit_firmware.inc::bb_say(BB_TX(TX_DILLO_KIT_TRIGGER_ACTIVITY))')]], shot2='midi-clock'))
    songrows = [
        ['{1} {2}', S('ADD: pick a pattern for KIT A, KIT B and Dillo, then {EDIT} adds it as a section.', NAV + 'if (white) return n < 2 ? BB_NAV_K_SONG_ADD', ARR + 'bb_arr_add();')],
        ['{3} / {4}', S('COPY the row; DELETE it. Key {4} pulses until {EDIT} confirms; {HOME} cancels. Both are off until the song has rows.', NAV + 'n == 2 ? BB_NAV_K_SONG_COPY : n == 3 ? BB_NAV_K_SONG_DELETE', NAV + 'return role == BB_NAV_K_SONG_DELETE && (song & BB_NAV_SONG_F_DELETE) ? BB_NAV_LED_BREATH', ARD + 'TX_REMOVE_THIS_ROW_EDIT_HOME')],
        ['{B1} / {B2}', S('Undo / redo the arrangement; each is off when there is nothing to undo or redo.', NAV + 'return n == 0 ? BB_NAV_K_SONG_UNDO : n == 1 ? BB_NAV_K_SONG_REDO', NAV + 'case LR_UNDO: return (song & BB_NAV_SONG_F_UNDO) ? BB_NAV_LED_DIM : BB_NAV_LED_DARK;')],
        ['{K1-K4}', S('ROW, REPEATS (1–64), BARS (the section\'s length), LOOP.', ARD + 'const char *labels[4]={"ROW","REPEATS","BARS","LOOP"};', ARR + 'row.repeats=(uint8_t)bb_limit((int)row.repeats+bb_limit(d,-63,63),1,64)')],
        ['{EDIT}', S('LOAD while stopped, CUE while playing: `NEXT SECTION QUEUED`, then `SECTION APPLIED`.', NAV + 'if (r == BB_NAV_BR_SONG_EDIT && f && !f->running) return "LOAD";', NAV + '[BB_NAV_BR_SONG_EDIT] = {"CUE"', ARR + 'TX_NEXT_SECTION_QUEUED', ARR + 'TX_SECTION_APPLIED')],
        ['{PLAY}', S('Start and stop the song.', ARR + 'static int bb_arr_transport(void){')],
        ['{REC}', S('A tap records a take into the selected row (chapter 10.4); held, it shows the REC help.', NAV + 'on SONG a clean REC tap (no chord, no help shown) records a take')],
        ['{HOME}', S('From ADD back to SONG; from SONG to the selected instrument. {GLO} also leaves.', NAV + 'SONG ADD -> SONG, MIDI / CLOCK -> GLOBAL: one level per tap', NAVF + 'HOME always restores the selected destination')]]
    P.append(sec('9.11', 'song', intro=[S('SONG · HOME arranges sections into rows and plays them in order. {GLO} then white key {4} opens it.', ARD + 'TX_SONG_HOME', NAV + 'white 4 the SONG'),
                                       S('The line under the title is the key legend: the same table as the lights.', ARD + 'in->title=bb_v3_legend;')], cards=[
        card('key map', [], panel(lit=['EDIT', 'HOME', 'GLO', 'PLAY', 'REC', 'K1', 'K2', 'K3', 'K4'], screen=SCR + 'song.png', keys=page_keys('song'), alt='SONG: the controls that work here') + knobrow('song'), span=8, cls='map'),
        card('song', [], img('song', 'SONG'), strip_name='song', span=4),
        card('a row turned', [], img('song-turn', 'SONG: BARS turned'), span=4), card('help', [], img('help-song', 'HELP on SONG'), span=4),
        card('add', [S('ADD: {K1} TARGET, {K2} GROUP, {K3} PART, {K4} BARS; {ALGO} the source bank. Key {3} plays the section without Dillo; key {1} goes back.', ARD + 'labels[0]="TARGET";labels[1]="GROUP";labels[2]="PART";labels[3]="BARS";', NAV + 'ALGORITHM on SONG ADD', NAV + 'return n == 0 ? BB_NAV_K_BACK : n == 2 ? BB_NAV_K_SONG_DILLO')], img('song-add', 'SONG / ADD'), span=4),
    ], extra=table(['control', 'does'], songrows)))
    ch.append(dict(num=9, title='pages', intro=[S('Every button opens one page. This chapter takes them one by one, with a key map for each.', NAVH + 'The one page variable. Only one page is ever current.'),
        S('On the key maps, highlighted keys do something on that page; labels are the firmware\'s own, and each key carries its light: bright, half-lit or pulse (5.5).', KEYMAP)], sections=P))
    # =================================================================== 10
    where('10')
    ch.append(dict(num=10, title='play and record', intro=[S('Play pads and notes live, record takes into the playing pattern, and undo what you do not keep.', NAVH + 'BB_NAV_A_RECORD'),
        S('Takes are quantised to the pattern\'s own grid, swung or straight.', REC + 'unsigned subdivision=bb_record.clock.lattice')], sections=[
        sec('10.1', 'play it live', cards=[
            card('pads', [S('Press {ARP}. Keys {1}–{11} are the pads; hold {REC} as you hit for an accent.', NAV + 'if (n < 11) { *arg = n; return BB_NAV_K_PAD; }', NAV + 'flags = BB_NAV_KF_ACCENT')], gesture('ARP > HOLD:REC + 1'), span=4),
            card('cuts', [S('{B1}–{B4} mute drum groups, {B8}–{B11} kill whole players.', NAV + 'return rec ? BB_NAV_K_GROUP_SOLO : BB_NAV_K_GROUP_MUTE;', NAV + 'return BB_NAV_K_KILL;')], gesture('B8'), span=4),
            card('sections', [S('{B6} {B7} step through the song\'s sections; the change lands on the bar.', NAV + 'BB_NAV_K_SECTION_PREV', NAVH + 'BB_NAV_K_SECTION_PREV, BB_NAV_K_SECTION_NEXT,   /* queued')], gesture('B7'), span=4),
        ]),
        sec('10.2', 'record a take', cards=[
            card('<span class="n">1</span>arm', [S('On PLAY or STEPS with KIT A or KIT B selected, press {REC} {+} {SEQ}. If stopped, BB1 starts playing.', NAV + 'if (rec && s->page == BB_NAV_PLAY && drum) return BB_NAV_BR_RECORD;', REC + 'if(!bb_face.requested.running)bb_nav_transport(0);')], gesture('HOLD:REC + SEQ'), span=4, cls='step'),
            card('<span class="n">2</span>count in', [S('It counts in to the next bar: `COUNT IN / RECORD NEXT BAR`.', SRTX + 'TX_COUNT_IN_RECORD_NEXT_BAR', REC + 'bb_record.phase=bb_record.countin?BB_RECORD_COUNT:BB_RECORD_LIVE;')], img('record-count-in', 'RECORD A: count in', 'shot sm'), span=4, cls='step'),
            card('<span class="n">3</span>play the pads', [S('Hit pads {1}–{11}. Hits land on the nearest step and add to the pattern; nothing is erased.', SRTX + 'TX_KIT_A_NEAREST_STEP', SRTX + 'TX_PAD_HITS_OVERDUB_NO_ERASE')], img('record-live', 'RECORD A: recording', 'shot sm'), span=4, cls='step'),
            card('<span class="n">4</span>finish', [S('Press {REC} to finish. The music keeps playing.', SRTX + 'TX_PADS_1_11_REC_FINISHES', REC + 'a pattern take keeps the music playing')], gesture('REC'), span=4, cls='step'),
            card('<span class="n">5</span>keep or discard', [S('{EDIT} keeps the take as a copy: `TAKE QUEUED · NEXT LOOP`, then `TAKE APPLIED · REC+EDIT UNDO`. {REC} discards it.', SRTX + 'TX_EDIT_KEEP_REC_DISCARD', SRTX + 'TX_TAKE_QUEUED_NEXT_LOOP', SRTX + 'TX_TAKE_APPLIED_REC_EDIT_UNDO')], img('record-review', 'RECORD A: keep or discard', 'shot sm'), span=4, cls='step'),
            card('<span class="n">6</span>undo', [S('{REC} {+} {EDIT} undoes a kept take.', SRTX + 'TX_REC_EDIT_UNDOES_A_KEPT_TAKE')], gesture('HOLD:REC + EDIT'), span=4, cls='step'),
        ]),
        sec('10.3', 'swing', cards=[
            card('record with swing', [S('Swing can stay on while you record. Hits are placed on the swung grid of the pattern as it plays.', REC + 'uint32_t even=bb_swing_denominator', REC + 'bb_live_record_begin_pair_phase'),
                                       S('The record page shows the swing the take uses, for example `K3 ACCENT / SWING 58`; half steps show as .5.', RECD + 'frozen swing is authoritative', RECD + 'bb_arr_text(bb_arr_rows[0]+n,sizeof bb_arr_rows[0]-n,".5")')],
                 img('record-live', 'K3 ACCENT / SWING 58'), strip_name='record-live', span=6),
            card('accent', [S('{K3} turns ACCENT on for every hit of the take.', REC + 'if(knobs[2])bb_record.accent=(uint8_t)(knobs[2]>0);', REC + 'command.value=(uint16_t)(bb_record.accent?127:88)')], gesture('TURN:K3'), span=6),
        ]),
        sec('10.4', 'record into a song row', cards=[
            card('set up', [S('On SONG, {REC} opens the setup: {K1} TARGET A or B, {K2} START NOW or NEXT, {K3} ACCENT, {K4} LAYER on.', ARR + 'bb_record.phase=BB_RECORD_SETUP', RECD + 'labels[0]="TARGET";labels[1]="START";labels[2]="ACCENT";labels[3]="NEW HITS";', ARD + 'in->knob[3].label="LAYER"')], gesture('REC > TURN:K1'), span=6),
            card('go', [S('{REC} starts, {HOME} cancels. {EDIT} keeps the take in the row; {REC} discards it.', SRTX + 'TX_REC_STARTS_HOME_CANCELS', SRTX + 'TX_EDIT_KEEP_REC_DISCARD')], gesture('REC > EDIT'), span=6),
        ]),
        sec('10.5', 'limits', cards=[card('a take', [
            S('A pattern holds up to 128 hits: `128 HITS FULL / TAKE REFUSED`.', SRTX + 'TX_128_HITS_FULL_TAKE_REFUSED'),
            S('Pads a kit does not have are refused: `PAD NOT IN THIS KIT`.', REC + 'bb_say(BB_TX(TX_PAD_NOT_IN_THIS_KIT))'),
            S('Undo keeps the last eight edits and takes.', PE + 'enum { BB_PE_HISTORY = 8 };')], '', span=12, cls='wide')]),
    ]))
    # =================================================================== 11
    where('11')
    ch.append(dict(num=11, title='functions', intro=[S('The tools behind the pages: the Rhythm Conductor, songs, generators, ratchets, your own kits, drum choices, scenes, tempo and swing.')], sections=[
        sec('11.1', 'rhythm conductor', intro=[S('RHYTHM CONDUCTOR is a top-level instrument: one key moves KIT A, KIT B and Dillo to a section together, on the bar (9.1).', TOP + 'BB_TOP_CONDUCTOR', CONDUCT + 'the Rhythm Conductor (SPEC section 2; home-keymap.png)')], cards=[
            card('<span class="n">1</span>play a song', [S('On the song list pick a song and press {PLAY}. KIT A, KIT B and Dillo get its first section in one go, and it starts.', NAVF + 'PLAY on the song list = CHOOSE + start + PERFORM', CONDUCT + 'SONGS: EDIT = CHOOSE')], gesture('TURN:ALGO > PLAY'), span=4, cls='step'),
            card('<span class="n">2</span>cue', [S('On RHYTHM CONDUCTOR press a white key. Its button pulses; on the next bar every player on the focused player\'s song moves, and the button lights up.', CONDUCT + 'b->target_tick=(st.current_tick|15u)+1u;', NAV + 'if ((q & BB_NAV_Q_SECTION) && (f->section_queued == arg')], gesture('5'), span=4, cls='step'),
            card('<span class="n">3</span>change it', [S('Another key before the bar replaces it; the same key again cancels; {PLAY} to stop cancels.', CONDUCT + 'Newest key wins (cancel, then resubmit when the handoff is free)')], gesture('5 > 5'), span=4, cls='step'),
            card('what it never touches', [S('A cue moves patterns only. Mutes, levels, kits, sounds and effects stay as they are; a muted player still follows, so it comes back in the right place.', CONDUCT + 'Mixes, mutes, kits, sounds and effects are never'),
                                           S('Stopped, a cue applies at once and {PLAY} starts on it.', CONDUCT + 'stopped: the section applied at once')], '', span=6),
            card('one player alone', [S('First focus KIT A, KIT B or Dillo with {B1}, {B2} or {B3}. Then {REC} {+} a white key moves only that player. The others keep their songs and sections.', NAVF + 'bb_nav_conductor_focus', NAV + 'return rec ? BB_NAV_K_SECTION : BB_NAV_K_CONDUCT;', CONDUCT + 'REC+black stays the focused part only')], gesture('B2 > HOLD:REC + 5'), span=6),
            card('combine rhythms', [S('On RHYTHM CONDUCTOR, press two or more section keys together to combine their rhythms. Tap a single section key to return to that section alone.', 'firmware/src/bb/bb_chord.h::Section keys pressed together are one combination', CONDUCT + 'if(!(m&(m-1u)))'),
                S('To adjust a combination, keep a section key held after the initial press, tap other section keys to add or remove them, then release all keys. The last section cannot be removed this way.', 'firmware/src/bb/bb_chord.h::Hold-and-add (secondary)', 'firmware/src/bb/bb_chord.h::return c->tog && (cur ^ c->tog)'),
                S('While a combination is queued, pressing the same combination again cancels it; a different combination replaces it.', CONDUCT + 'same combination again while it waits cancels it')], gesture('1 + 3'), span=12),
        ]),
        sec('11.2', 'song', cards=[
            card('build a song', [S('A song is a list of rows; each row plays a section a number of times. Up to 64 rows and 32 sections.', ARRH + 'enum { BB_ARR_SECTIONS=32, BB_ARR_ROWS=64')], gesture('GLO > 4 > 1 > EDIT'), span=6),
            card('lengths in bars', [S('BARS shows a section\'s length in bars: 4, 1.5, or steps such as 17S when it is not on a beat.', ARD + 'a section length (16th steps, 16 per bar) as bars: "4", "1.5" on a beat, else steps "17S"')], img('song-turn', 'BARS 4', 'shot sm'), span=6),
        ]),
        sec('11.3', 'evolve', cards=[
            card('wander', [S('Turn EVOLVE on with {REC} {+} {LFO}. Every EVERY phrases it changes up to AMOUNT of the parts you chose.', NAVH + 'BB_NAV_A_EVO_TOGGLE', NAV + 'BB_NAV_R_EVO_AMOUNT; r[1] = BB_NAV_R_EVO_EVERY')], gesture('HOLD:REC + LFO'), span=4),
            card('home', [S('{EDIT} captures what plays as home; {B2} RETURN goes back to it; {B1} FREEZE holds still.', NAV + 'return f && f->gen_mode != BB_NAV_MODE_EVOLVE ? BB_NAV_BR_APPLY : BB_NAV_BR_CAPTURE;', NAV + 'if (n == 1) return BB_NAV_K_EVO_HOME;')], gesture('B2'), span=4),
            card('lock', [S('{REC} {+} a white key locks that step.', NAV + 'if (rec) return BB_NAV_K_EVO_LOCK;')], gesture('HOLD:REC + 5'), span=4),
        ]),
        sec('11.4', 'generators', cards=[
            card('euclid', [S('On EVOLVE, {LFO} or {B3} steps EVOLVE, EUCLID and RANDOM. EUCLID spreads HITS evenly over STEPS, turned by ROTATE, with ACCENTS.', NAVH + 'LFO (or black 3) on EVOLVE: next mode', GEN + 'bb_gen_mode_next', 'firmware/src/bb_ui_v3_firmware.inc::static const char *const eu[4] = {"HITS", "STEPS", "ROTATE", "ACCENTS"};'),
                            S('On drums the white keys choose the voice it writes.', NAVF + 'case BB_NAV_K_GEN_VOICE: if(arg<16){bb_gen_state.voice=(uint8_t)arg;')], img('evolve-euclid', 'EUCLID'), strip_name='evolve-euclid', span=4),
            card('random', [S('RANDOM writes a pattern from DENSITY, ACCENT, ANCHOR (SLIDE on Dillo) and SEED, 1 to 9999.', 'firmware/src/bb_ui_v3_firmware.inc::static const char *const ra[4] = {"DENSITY", "ACCENT", "ANCHOR", "SEED"};', GEN + 'bb_gen_state.seed=(uint32_t)bb_limit((int)bb_gen_state.seed+d,1,9999)')], img('evolve-random', 'RANDOM'), strip_name='evolve-random', span=4),
            card('apply', [S('The grey line reads `EDIT: APPLY`. {EDIT} applies it on the loop: `VARIATION QUEUED / B2 RETURN`, then `VARIATION APPLIED`. {B2} returns home.', NAV + 'return f && f->gen_mode != BB_NAV_MODE_EVOLVE ? BB_NAV_BR_APPLY : BB_NAV_BR_CAPTURE;', TX + 'TX_VARIATION_QUEUED_B2_RETURN', TX + 'TX_VARIATION_APPLIED')], img('notice-variation-applied', 'VARIATION APPLIED'), span=4),
        ]),
        sec('11.5', 'ratchets', cards=[
            card('hold and turn', [S('On STEPS, hold a drum step key and turn {K4}: the step plays 1 to 4 times. Let go without turning and the key just toggles the step.', NAV + 'release of the held drum step toggles it unless K4 was turned', TX + 'TX_RATCHET_X1_X4')], gesture('SEQ > HOLD:7 + TURN:K4'), span=6),
            card('on screen', [S('The step\'s ticks show the count; `RATCHET APPLIED / REC+EDIT UNDO` confirms it.', 'firmware/src/bb_ui_v3/bb_ui.h::uint8_t ratchet[16];', TX + 'TX_RATCHET_APPLIED_REC_EDIT_UNDO')], img('steps-a-ratchet', 'STEPS: RATCHET x4'), strip_name='steps-a', span=6),
        ]),
        sec('11.6', 'kit builder', intro=[S('BUILD makes a USER kit for KIT A from the voices of the installed machines: each of 17 lanes takes its sound from any kit.', UK + '#define BB_UK_VOICES 17u', NAV + 'KIT on drum A (an 808-family layer, focus row 0) has BUILD')], cards=[
            card('<span class="n">1</span>build', [S('Select KIT A, open KIT and press {B1}: `KIT  BUILD`.', NAV + 'if (!white && n == 0) return BB_NAV_K_BUILD;')], gesture('PRESETS > B1'), span=4, cls='step'),
            card('<span class="n">2</span>pick a lane', [S('Keys {1}–{11} and {B2}–{B7} choose a lane and play it.', NAV + 'black 2-7: LC MC HC CL MA RD', TX + 'TX_LANE_CHOOSE_HEAR')], img('kit-build', 'KIT BUILD', 'shot sm'), span=4, cls='step'),
            card('<span class="n">3</span>shape it', [S('{PRESETS} chooses the lane\'s kit; {K1}–{K4} LEVEL, TUNE, DECAY, VOICE.', TX + 'TX_LANE_S_KIT', NAV + 'r[0] = BB_NAV_R_UK_LEVEL; r[1] = BB_NAV_R_UK_TUNE;')], gesture('TURN:PRESETS + TURN:K4'), span=4, cls='step'),
            card('<span class="n">4</span>save as', [S('{EDIT} opens SAVE AS; keys {1}–{4} choose USER 1–4.', NAV + 'if (s->bank == BB_NAV_KIT_BUILD) return BB_NAV_BR_SAVE_AS;', NAV + 'return white && n < 4 ? BB_NAV_K_UK_SLOT')], img('kit-save-as', 'KIT SAVE AS', 'shot sm'), span=4, cls='step'),
            card('<span class="n">5</span>store', [S('Stop, then {EDIT}: `STORING USER KIT`, then `USER KIT STORED`.', NAV + 'if (s->bank == BB_NAV_KIT_SAVE_AS) return BB_NAV_BR_STORE;', TX + 'TX_STORING_USER_KIT', TX + 'TX_USER_KIT_STORED')], gesture('PLAY > EDIT'), span=4, cls='step'),
            card('<span class="n">6</span>play it', [S('USER 1–4 sit at the end of KIT A\'s list; load one like any kit. {HOME} steps back: SAVE AS, BUILD, the list.', PICK + 'bb_sound_picker_user_name', NAV + 'SAVE AS -> BUILD -> the KIT list: one level per tap')], img('kit-a-user', 'KIT: USER kits', 'shot sm'), span=4, cls='step'),
            card('good to know', [S('USER kits play on KIT A. A lane whose kit is not installed plays the 808: `KIT NOT INSTALLED · 808 STANDS IN`.', NAV + 'drum B and Dillo do not', TX + 'TX_KIT_NOT_INSTALLED_808_STANDS_IN'),
                                  S('Scenes keep KIT A\'s USER kit with them.', 'firmware/src/bb_target_session.c::2.0 kit builder: USER 1-4 live in their own')], '', span=12, cls='wide'),
        ]),
        sec('11.7', 'drum choices', cards=[
            card('choose a machine', [S('Each drum list offers 808 and the seven sampled machines. KIT A also offers USER 1–4.', PICK + 'bb_sound_picker_machine_items', PICK + 'BB_SOUND_PICKER_USER_ROWS')], img('kit-a-user', 'KIT: machine and USER kit list'), span=12),
        ]),
        sec('11.8', 'scenes', cards=[
            card('store', [S('Stop, press {SAVE}, choose a scene with {1}–{4} and press {EDIT}: `SAVED / FLASH VERIFIED`.', NAV + 'BB_NAV_A_SAVE_STORE', SESS + 'SAVED / FLASH VERIFIED'),
                           S('{REC} {+} {SAVE} stores to the current scene from any page.', NAVH + 'BB_NAV_A_QUICK_SAVE')], gesture('SAVE > 2 > EDIT'), span=4),
            card('load', [S('On SAVE, stopped, {REC} {+} key {1}–{4} loads that scene: `LOADED / PRESS PLAY`.', NAV + 'BB_NAV_K_SLOT_LOAD', TX + 'TX_LOADED_PRESS_PLAY')], gesture('HOLD:REC + 2'), span=4),
            card('other data', [S('A scene holding another firmware\'s data reads `SCENE 4 HAS OTHER DATA` and is never loaded. To replace it, hold {SAVE} for two seconds on that scene.', SESS + 'TX_OTHER_DATA_HOLD_SAVE', NAVF + 'a fresh SAVE press held 2 s on this page and slot')], gesture('HOLD:SAVE'), span=4),
        ]),
        sec('11.9', 'tempo and swing', cards=[
            card('tempo', [S('{REC} {+} {SELECT} turns the tempo; {REC} {+} {GLO} taps it; GLOBAL {K1} sets 30 to 300 BPM.', NAVH + 'BB_NAV_TO_TEMPO', NAV + 'static void nav_tap', CTRL + 'case BB_CMD_TEMPO:return c->value>=30 && c->value<=300;')], gesture('HOLD:REC + TAP:GLO'), span=6),
            card('swing', [S('On drums {SCL} switches swing on and off; GLOBAL {K2} sets it from 50 to 75 %.', NAV + 'BB_NAV_A_SWING_TOGGLE', NAVF + 'bb_limit(delta,-50,50),100,150')], gesture('SCL'), span=6),
        ]),
    ]))
    # =================================================================== 12
    where('12')
    howto = [
        ('play two machines at once', ['Tap {HOME}, then turn {SELECT} to BIG BEAT.', 'Use the local player focus for KIT B, then turn {PRESETS}, choose a machine and press {EDIT}.', 'Turn {ALGO}, choose a pattern, press {EDIT}. Both layers play on the bar.'], 'HOME > TURN:SELECT > TURN:PRESETS > EDIT'),
        ('make a kit from three machines', ['Select KIT A, turn {PRESETS}, press {B1}.', 'Press {1}, turn {PRESETS} to LinnDrum. Press {2}, choose DMX. Press {3}, choose Simmons.', 'Press {EDIT}, key {1}, stop, {EDIT}.'], 'B1 > 1 > TURN:PRESETS'),
        ('record a swung take', ['Press {SCL}: `SWING ON`.', 'Press {ARP}, then {REC} {+} {SEQ}.', 'Play pads {1}–{11}, press {REC}, then {EDIT} to keep.'], 'SCL > HOLD:REC + SEQ'),
        ('make a four-row song', ['Press {GLO}, then white key {4} for SONG.', 'Press {1}, choose a part with {K3}, press {EDIT}. Repeat three times.', 'Press {PLAY}.'], 'EDIT > 1 > EDIT'),
        ('euclidean hats', ['Press {LFO} until the title reads EUCLID.', 'Press {11} for CLOSED HAT; set HITS and STEPS with {K1} {K2}.', 'Press {EDIT}.'], 'LFO > B3 > EDIT'),
        ('follow another clock', ['Press {GLO}, then {2} for MIDI CLOCK.', 'Set CLOCK IN with {K1} and FOLLOW ON with {K2}.', 'Stop, press {EDIT} to keep the setting.'], 'GLO > 2 > TURN:K2'),
        ('send clock to a computer', ['On MIDI CLOCK, stopped, turn {K4} to `USB ON`.', 'BB1 now ignores clock coming in on USB.', 'Press {EDIT} to keep it.'], 'GLO > 2 > TURN:K4'),
        ('play dillo from a keyboard', ['Set your keyboard to channel 1.', 'Play: notes, pitch bend and sustain go to Dillo.', 'CC74 is cutoff, CC71 resonance.'], 'USB'),
        ('start again', ['Hold {REC} and press {HOME}: all notes off.', 'Press {PLAY} to stop.', 'Load a scene with {SAVE}, then {REC} {+} {1}.'], 'HOLD:REC + HOME'),
        ('drop the band into the break', ['On the song list, press {PLAY}.', 'On RHYTHM CONDUCTOR, press the white key of the break: its button pulses.', 'On the bar, all three players jump together.'], 'PLAY > 1'),
        ('try dillo\'s sounds while playing', ['Tap {HOME}, turn {SELECT} to DILLO, then hold a note.', 'Turn {PRESETS}: each sound arrives as you stop.', '{EDIT} keeps it; {HOME} puts the old one back.'], 'HOME > TURN:SELECT > TURN:PRESETS > EDIT'),
        ('find out what a key does', ['Hold {HOME}.', 'Press the key: its row lights and the bottom line explains it.', 'Let go of {HOME}.'], 'HOLD:HOME + 5'),
    ]
    src12 = [[NAVH + 'BB_NAV_TO_INSTRUMENT', NAVH + 'After KIT_CONFIRM'], [NAV + 'BB_NAV_K_BUILD', TX + 'TX_LANE_S_KIT', NAV + 'BB_NAV_A_UK_STORE'], [NAV + 'BB_NAV_A_SWING_TOGGLE', REC + 'bb_live_record_begin_pair_phase'],
             [NAV + 'white 4 the SONG', ARR + 'bb_arr_add();'], [NAV + 'return s->page == BB_NAV_EVOLVE ? BB_NAV_BR_MODE : BB_NAV_BR_EVOLVE;', NAVF + 'bb_gen_state.voice=(uint8_t)arg'], [MIDU + 'c.tempo_follow', MIDU + 'bb_midi_ui_save'],
             [MIDU + 'c.clock_out', MIDI + 'c->clock_out && source==BB_KEY_USB'], [MIDIS + '#define BB_MIDI_SETTINGS_DEFAULT', MIDI + 'cin==14', MIDI + 'a==74 || a==71'], [NAVH + 'BB_NAV_A_PANIC', NAV + 'BB_NAV_K_SLOT_LOAD'],
             [NAV + 'white 1-16 = the sections (16 a page', NAV + 'if ((q & BB_NAV_Q_SECTION) && (f->section_queued == arg'], [AUDH + '#define BB_AUD_SETTLE_MS 350u', 'firmware/src/bb_dillo_audition_firmware.inc::bb_say("SOUND RESTORED")'],
             [NAVH + 'while HOME is held, a key, button or knob is explained instead of acting']]
    cards = []
    for (t, st, g), srcs in zip(howto, src12):
        S(t + ': ' + ' '.join(st), *srcs)
        cards.append(card(t, ['<ol class="steps">' + ''.join(f'<li>{x}</li>' for x in st) + '</ol>'], gesture(g, .8), span=4))
    ch.append(dict(num=12, title='how to', intro=[S('Short recipes for things people ask.'), S('Each takes a minute.')], sections=[sec('12.1', 'recipes', cards=cards)]))
    # =================================================================== 13
    where('13')
    msgs = [['`NEXT` / `QUEUED`', S('Your change waits for the bar.', TX + 'TX_PATTERN_QUEUED')], ['`APPLIED`', S('The music has it.', TX + 'TX_SECTION_APPLIED')],
            ['`BUSY / SELECT AGAIN`', S('It was mid-change: press again.', TX + 'TX_BUSY_SELECT_AGAIN')], ['`STOP FIRST`', S('Saving, loading and storing need the music stopped.', TX + 'TX_STOP_FIRST')],
            ['`KIT WAITING / EDIT: RETRY`', S('A queued kit has not started after a while: press {EDIT} to try again.', TX + 'TX_KIT_WAITING_EDIT_RETRY', NAV + 's->queue_timeout_ms = 8000;')],
            ['`NO PATTERNS FOR THIS KIT`', S('That kit cannot play the highlighted pattern.', TX + 'TX_NO_PATTERNS_FOR_THIS_KIT')], ['`LAYER OFF`', S('This player is switched off: {REC} {+} {ARP}.', TX + 'TX_LAYER_OFF,', NAVH + 'BB_NAV_A_LAYER_ENABLE')],
            ['`CUE REFUSED: B NEEDS VOICES`', S('A section could not play on the player named (A, B or DILLO), so nothing moved. Other causes: `NEEDS A RICH KIT`, `NEEDS TIMING`, `NO DRUMS HERE`, `NO LINE HERE`.', CONDUCT + 'const char *s="CUE REFUSED: ";', CONDUCT + 'why="NEEDS VOICES"')],
            ['`TOO LATE TO CANCEL`', S('The bar came before your cancel could land, so the section plays.', CONDUCT + 'bb_say("TOO LATE TO CANCEL")')],
            ['`BUSY - RETRY`', S('There was no room to send it yet; nothing changed. Try again.', CONDUCT + 'bb_say(BB_TX(TX_BUSY_RETRY))')],
            ['`MISSED THE BAR` / `CUE CANCELLED`', S('The cue was not ready in time, or you cancelled it; the music carries on.', CONDUCT + '"MISSED THE BAR"', CONDUCT + '"CUE CANCELLED"')],
            ['`CANCELLED BY STOP`', S('Stopping ended a queued change.', 'firmware/src/bb/bb_fault_notice.h::"CANCELLED BY STOP"')],
            ['`SONG PLAYING`', S('The SONG arranger is in charge; the section keys wait.', CONDUCT + 'bb_say("SONG PLAYING")')],
            ['`SOUND RESTORED` / `SOUND KEPT`', S('Dillo\'s sound put back by {HOME}, or kept by {EDIT}.', 'firmware/src/bb_dillo_audition_firmware.inc::bb_say("SOUND RESTORED")', 'firmware/src/bb_dillo_audition_firmware.inc::bb_say("SOUND KEPT")')],
            ['`NOT IN THIS BUILD`', S('That combination has no job in this firmware.', TX + 'TX_NOT_IN_THIS_BUILD')], ['`HELP UNAVAILABLE`', S('The help texts are missing: reinstall from the installer page.', 'firmware/src/bb/bb_text_block.h::"HELP UNAVAILABLE"')],
            ['`SCENE n HAS OTHER DATA`', S('That scene belongs to another firmware (chapter 11.8).', TX + 'TX_HAS_OTHER_DATA')], ['`AUDIO FAULT / LOAD BLOCKED`', S('The change was refused so the music keeps playing.', NAV + '"AUDIO FAULT / LOAD BLOCKED"')],
            ['`OCTAVE LIMIT` / `TRANSPOSE LIMIT`', S('As far as it goes.', TX + 'TX_OCTAVE_LIMIT', TX + 'TX_TRANSPOSE_LIMIT')]]
    notes = [[str(n), x] for n, x in [(36, 'kick'), (38, 'snare'), (45, 'low tom'), (47, 'mid tom'), (50, 'high tom'), (37, 'rim'), (39, 'clap'), (56, 'cowbell'), (49, 'cymbal'), (46, 'open hat'), (42, 'closed hat')]]
    S('Drum notes 36, 38, 45, 47, 50, 37, 39, 56, 49, 46, 42 play kick to closed hat.', MIDI + 'static const uint8_t notes[BB_DRUM_TRACKS]={36,38,45,47,50,37,39,56,49,46,42};')
    impl = [['notes', S('KIT A on channel 10 and KIT B on 11 (drum notes below, with velocity); Dillo on channel 1.', MIDIS + '#define BB_MIDI_SETTINGS_DEFAULT {BB_MIDI_CLOCK_USB,1,1,0,{10,11},{1,2}}')],
            ['controllers', S('Dillo: CC7 level, CC74 cutoff, CC71 resonance, CC64 sustain, CC120 all sound off, CC121 reset, CC123 all notes off; pitch bend.', MIDI + 'if(a==7)', MIDI + 'a==74 || a==71', MIDI + '(a==64 || a==121 || a==123)', MIDI + 'if(a==120)')],
            ['program change', S('Programs 0–8 on Dillo\'s channel load its nine sounds.', MIDI + 'out->type=synth?BB_CMD_TELEGRAM_PATCH:BB_CMD_DILLO_PATCH')],
            ['clock', S('Start plays from the top, Continue resumes, Stop stops (TRANSPORT ON). FOLLOW takes the tempo, to the nearest whole BPM, 30–300; it follows tempo, not phase.', MIDI + 'status==0xfa?BB_CMD_TRANSPORT_RESTART:status==0xfb?BB_CMD_TRANSPORT_RESUME', MIDI + 'Clock mode is integer tempo-follow, NOT external phase synchronization', MIDI + 'if(bpm<30u || bpm>300u')],
            ['clock out', S('USB only, off unless you turn it on; while it is on, clock coming in on USB is ignored.', MIDIS + 'USB-only output boolean; OFF remains default', MIDD + '"USB REALTIME IN OFF / OUT ON"')],
            ['not used', S('Song position, thru and bank remapping.', MIDI + 'no Song Position Pointer, GM bank remap or implicit THRU')]]
    ch.append(dict(num=13, title='system', intro=[S('MIDI, messages, troubleshooting and recovery.'), S('For installing and going back, the installer page is the authority.', INST + 'Install BB1')], sections=[
        sec('13.1', 'midi channels', cards=[card('midi channels', [S('{GLO}, then {1}. {K1}–{K3} set KIT A, KIT B and Dillo (BASS CH): 1–16 or OFF; two parts never share a channel. LEAD RSV is reserved.', MIDU + 'bb_midi_next_channel', MIDIS + 'if(used&bit)return 0;', MIDD + '"LEAD RSV"'),
            S('Stop, then {EDIT} or {SAVE} keeps the settings: `GLOBAL SETTINGS SAVED`. {B1} or {HOME} goes back to GLOBAL; hold {HOME} for help here too.', MIDU + 'TX_STOP_WAIT_TO_SAVE_GLOBALS', MIDU + '"GLOBAL SETTINGS SAVED"', NAV + 'return b == BB_NAV_B_EDIT || b == BB_NAV_B_SAVE ? BB_NAV_BR_GLOBALS_SAVE', NAV + 'MIDI / CLOCK -> GLOBAL: one level per tap')], img('midi-channels', 'MIDI CHANNELS'), strip_name='midi-channels', span=6),
            card('midi clock', [S('{2} opens MIDI CLOCK: {K1} CLOCK IN (USB or TRS), {K2} FOLLOW, {K3} TRANSPORT, {K4} CLOCK OUT.', MIDD + '{"CLOCK IN","FOLLOW","TRANSPORT","CLOCK OUT"}'),
                                S('Defaults: USB, FOLLOW ON, TRANSPORT ON, CLOCK OUT OFF.', MIDIS + '#define BB_MIDI_SETTINGS_DEFAULT {BB_MIDI_CLOCK_USB,1,1,0')], img('midi-clock', 'MIDI CLOCK'), strip_name='midi-clock', span=6)],
            extra=table(['', 'implementation'], impl)),
        sec('13.2', 'drum note map', extra=table(['note', 'plays'], notes, 'narrow')),
        sec('13.3', 'messages', extra=table(['you see', 'it means'], msgs)),
        sec('13.4', 'troubleshooting', cards=[
            card('no sound', [S('Check {MASTER}, then the HOME knobs, then mutes: a half-lit key {B1}–{B3} on RHYTHM CONDUCTOR, or MUTED in its part row, is muted.', NAV + 'return nav_audible(f->instr_muted, f->instr_solo, arg) ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;', UIF + 'BB_UI_LN_MUTED'), S('{REC} {+} {HOME} stops stuck notes.', NAVH + 'BB_NAV_A_PANIC')], gesture('HOLD:REC + HOME'), span=4),
            card('nothing changes', [S('Changes land on the bar: a pulsing key is waiting. If `KIT WAITING / EDIT: RETRY` shows, press {EDIT}. An off key does nothing on this page.', TX + 'TX_KIT_WAITING_EDIT_RETRY', NAVH + 'A dark key or button never acts')], gesture('EDIT'), span=4),
            card('where am i?', [S('Hold {HOME} for help, and press a key while holding it to hear what it does; tap {HOME} to go back.', NAV + 's->help = 1;', NAVH + 'while HOME is held, a key, button or knob is explained instead of acting', NAV + 'HOME only ever goes back')], gesture('HOLD:HOME'), span=4),
        ]),
        sec('13.5', 'recovery', cards=[
            card('fm-1 not found', [S('Try another USB cable (some only charge), plug straight into the computer, and close other apps that use MIDI.', INST + 'Try another USB cable (some only charge)')], gesture('USB'), span=4),
            card('stopped halfway', [S('Allow MIDI access including SysEx, close other MIDI apps and use a direct cable. If the MIDI update loader is then visible, retry the same button.', INST + 'Allow MIDI access including SysEx', INST + 'If the MIDI update loader is then visible, retry this same package.')], gesture('USB'), span=4),
            card('will not start', [S('A blank screen or ROM mode is not the MIDI loader: stop retrying in the browser. The installer page links a hardware-assisted recovery route.', INST + 'WL80UBOOT / ROM mode is not the MIDI loader;', INST + 'for a hardware-assisted recovery route')], '<a class="jump" href="/fm1/install/">installer →</a>', span=4),
        ]),
    ]))
    # =================================================================== 14
    where('14')
    spec = [['instruments', S('RHYTHM CONDUCTOR, BIG BEAT and DILLO. The Conductor coordinates two drum layers (KIT A, KIT B) and Dillo; BIG BEAT contains the two drum layers; Dillo is a monophonic bass synth.', TOP + 'BB_TOP_CONDUCTOR', TOP + 'BB_TOP_BIG_BEAT', TOP + 'BB_TOP_DILLO')],
            ['drum voices', S('11 pads per kit; 17 voices to write on STEPS.', NAV + 'if (n < 11) { *arg = n; return BB_NAV_K_PAD; }', 'firmware/src/bb_rich_panel.inc::static const uint8_t bb_rich_lanes[17]')],
            ['kits', S('808, 7 sampled machines, 4 USER kits (KIT A).', 'firmware/src/bb_ui_kit_label.h::"Linn LM-1"', UK + '#define BB_UK_SLOTS 4u')],
            ['dillo', S('9 sounds; square or saw; scales OFF, MINOR, MAJOR, DORIAN, PENTA; keys ±2 octaves.', COREH + '#define BB_DILLO_PATCHES 9', NAV + '{ "OFF", "MINOR", "MAJOR", "DORIAN", "PENTA" }')],
            ['patterns', S('Up to 64 steps in four 16-step banks; ratchets x1–x4 per drum step.', COREH + '#define BB_STEPS 64', TX + 'TX_RATCHET_X1_X4')],
            ['library', S('SONGS: 67 named songs (233 parts). PRESETS: 4 Roland rhythm machines (138 rhythms). ERAS: 21 eras, 168 parts. ACID: 27 banks, 205 phrases.', CAT, CATJ, VIEW)],
            ['sections', S('RHYTHM CONDUCTOR: up to 64 sections on white keys, four pages of 16. PATTERN: up to 55 on black keys, five pages of 11.', NAVH + '#define BB_NAV_CONDUCT_PAGES 4u', NAVH + '#define BB_NAV_SECTION_PAGES 5u')],
            ['dillo sounds live', S('A new sound loads 350 ms after {PRESETS} stops turning.', AUDH + '#define BB_AUD_SETTLE_MS 350u')],
            ['lights', S('Four states on every key and button: off, half-lit, bright, pulse.', NAVH + 'enum { BB_NAV_LED_DARK, BB_NAV_LED_DIM, BB_NAV_LED_LIT, BB_NAV_LED_BREATH };')],
            ['song', S('64 rows, 32 sections, 16 arrangement undo steps; repeats 1–64.', ARRH + 'BB_ARR_SECTIONS=32, BB_ARR_ROWS=64, BB_ARR_HISTORY=16')],
            ['recording', S('Up to 128 hits per pattern; eight-step Undo.', SRTX + 'TX_128_HITS_FULL_TAKE_REFUSED', PE + 'BB_PE_HISTORY = 8')],
            ['tempo, swing', S('30–300 BPM; swing 50–75 % in half steps.', CTRL + 'c->value>=30 && c->value<=300', SWING + 'bb_swing_x2(value)<=150u')],
            ['effects', S('Tape echo (three heads, tape drive, motion) and spring; per-player sends.', NAV + 'nav_fx_bank_names[5] = {"ECHO", "TAPE DRIVE", "SPRING", "MOTION", "SENDS"}')],
            ['scenes', S('4 scenes, saved stopped and read back.', NAVH + 'BB_NAV_SAVE,     /* SAVE: four session slots', SESS + 'SAVED / FLASH VERIFIED')],
            ['midi', S('USB and TRS in; USB clock out; channels 1–16.', MIDI + 'source!=BB_KEY_USB&&source!=BB_KEY_TRS', MIDIS + 'USB-only output')],
            ['screen', S('240 × 240 colour display.', UI + 'u_rect(8, 23, 224, 1, BB_UI_FAINT);', 'firmware/src/lcd.c::240')]]
    ch.append(dict(num=14, title='technical specifications', intro=[S('What BB1 does, in numbers, from its own source.', RENDER)], sections=[sec('14.1', 'specifications', extra=table(['', ''], spec))]))
    # =================================================================== 15
    where('15')
    import re as _re
    cat = open(CATFILE).read()
    def group(g):
        m = _re.search(rf'== {g} banks \d+\n(.*?)\n== {g} parts', cat, _re.S)
        return [l.split('\t')[1:] for l in m.group(1).split('\n')]
    eras = group('ERAS'); songs = group('SONGS'); acid = group('ACID')
    boxes = [s for s in songs if s[0].startswith('Roland ')]
    realsongs = [s for s in songs if not s[0].startswith('Roland ')]
    fall = [['808', 'ride', 'cymbal'], ['808 on KIT B', 'synth percussion, zap', 'cowbell, high tom'], ['LinnDrum', 'middle conga · claves · shaker', 'middle tom · rimshot · maracas'],
            ['DrumTraks', 'low, high tom · congas · claves · shaker', 'middle tom · middle tom · rimshot · maracas'], ['DMX', 'congas · claves · cowbell · shaker', 'toms · rimshot · rimshot · maracas'],
            ['Linn LM-1', 'congas · claves · ride · shaker', 'toms · rimshot · cymbal · maracas'], ['Simmons', 'congas · claves · clap · maracas · cowbell · cymbal · ride · shaker', 'toms · rimshot · snare · closed hat · rimshot · open hat · open hat · closed hat'],
            ['Rhythm King', 'middle tom · low, middle conga · high conga · clap · ride · shaker', 'low tom · low tom · high tom · snare · cymbal · maracas'], ['Drumulator', 'middle, high conga · maracas · shaker', 'middle, high tom · closed hat · closed hat']]
    S('When a machine lacks a voice, BB1 plays the nearest one it has (table).', FALL + 'bb_voice_fallback')
    dillo = ['Silver Pulse', 'Liquid Wire', 'Dry Jack', 'Open Circuit', 'Rubber Acid', 'Long Fuse', 'Hot Valve', 'Hollow Dub', 'Square Acid']
    S('The nine Dillo sounds: ' + ', '.join(dillo) + '.', CORE + 'const bb_named_dillo bb_dillo_bank[BB_DILLO_PATCHES]')
    S('Eras, songs, presets and ACID banks as listed, from the catalogue walk.', CAT)
    def lst(items): return '<ul class="cols4">' + ''.join(f'<li>{html.escape(x)}</li>' for x in items) + '</ul>'
    ch.append(dict(num=15, title='sound library', intro=[S('Everything BB1 can play, as its own lists name it.', CAT), S('The sound library is installed with the firmware as a matched pair (chapter 3.3).', INST + 'its matching sound library 2.1')], sections=[
        sec('15.1', 'machines', cards=[card('808 and sampled', [S('808 is synthesised voice by voice; LinnDrum, DrumTraks, DMX, Linn LM-1, Simmons, Rhythm King and Drumulator are sampled.', 'firmware/src/bb_ui_kit_label.h::"Linn LM-1"')], '', span=12, cls='wide')],
            extra=table(['machine', 'asks for', 'plays'], fall)),
        sec('15.2', 'your kits', cards=[card('USER 1–4', [S('Build your own kits for KIT A (chapter 11.6).', UK + '#define BB_UK_SLOTS 4u')], '', span=12, cls='wide')]),
        sec('15.3', 'dillo sounds', cards=[card('nine sounds', [lst(dillo), S('Square Acid suits the ACID phrases.', CORE + 'for the ACID bank. Square wave')], '', span=12, cls='wide')]),
        sec('15.4', 'eras', cards=[card(f'{len(eras)} eras · {sum(int(e[1]) for e in eras)} parts', [lst([e[0] for e in eras])], '', span=12, cls='wide')]),
        sec('15.5', 'songs', cards=[card(f'{len(realsongs)} named songs · {sum(int(s[1]) for s in realsongs)} parts', [lst([s[0].strip() for s in realsongs])], '', span=12, cls='wide')]),
        sec('15.6', 'presets', cards=[card(f'{len(boxes)} machines · {sum(int(b[1]) for b in boxes)} rhythms', [lst([f'{b[0]} · {b[1]}' for b in boxes]),
            S('The four Roland rhythm machines are the PRESETS tab of the song list. The longest, TR-66 with 54 rhythms, plays to the end: {OCT+} on RHYTHM CONDUCTOR turns its pages (9.1).', CAT, VIEW, NAVH + '#define BB_NAV_CONDUCT_PAGES 4u')], '', span=12, cls='wide')]),
        sec('15.7', 'acid', cards=[card(f'{len(acid)} banks · {sum(int(a[1]) for a in acid)} phrases', [lst([a[0] for a in acid]),
            S('Many phrases are adapted from transcriptions at <a href="https://acid-tabs.com/">Acid-Tabs.com</a> by J. McConaghy, used under <a href="https://creativecommons.org/licenses/by-sa/4.0/">CC BY-SA 4.0</a>; those adapted phrases are shared under the same licence.', 'website 4dda2b73 public/fm1/guide/index.html:318 (the existing licence credit for the ACID bank; firmware/src/bb/bb_core.c Square Acid note)')], '', span=12, cls='wide')]),
        sec('15.8', 'the library file', cards=[card('the matching library', [
            S('The installer puts the sound library that pairs with this firmware on with it. It carries the sampled machines and the help texts; the firmware checks the help texts are the ones it was built with.', 'big-beat/integration/build/recipes/composition-21/README.md::The library argument is mandatory', 'firmware/src/bb/bb_text_pin.h::BB_TX_BLOCK_SHA256'),
            S('With another library, help reads `HELP UNAVAILABLE` and the controls still work.', 'firmware/src/bb/bb_text_block.h::"HELP UNAVAILABLE"')], '', span=12, cls='wide')]),
    ]))
    return ch

CATFILE = ''
