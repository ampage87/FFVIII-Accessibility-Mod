"""EWM vs FF8 Wait mode -- does the freeze change the TURN ECONOMY?

The question Aaron asked: is Enhanced Wait Mode making battles easier than they
should be, and if enemies are getting fewer turns, by what ratio should enemy
speed be bumped to compensate?

WHAT THE MOD ACTUALLY DOES (battle_tts_ewm_atb_hook.inl, read not assumed):

    for (int slot = 0; slot < BATTLE_TOTAL_SLOTS; slot++) {
        if (slot == (int)excludeSlot) continue;
        ... save the gauge, zero it, call the engine, restore ...
    }

BATTLE_TOTAL_SLOTS is 7 -- three party, four enemy. The freeze is applied to
EVERY slot except the one whose turn is being taken. It is symmetric.

WHAT FF8's OWN "WAIT" MODE DOES (Game8, and the FF wiki agree): the gauge pauses
"when choosing enemies to target with abilities, choosing items and magics from
the menu, and choosing which GF to summon" -- sub-menus and target selection.
NOT the top-level command menu. Also symmetric across slots, just for less time.

So both modes stop the same clock for everybody. This simulation exists to check
whether that reasoning survives contact with the actual mechanics, and to put a
number on the residual asymmetry (the excluded slot).

Model, per 30 Hz frame:
  * every combatant has a gauge 0..12000 filling at a rate from its Speed
    (FF8 gauge maximum confirmed from the mod's own ATB logs, e.g.
     "ATB=[s0=7695/12000 s1=11004/12000 s2=0/12000 s3=11648/12000]")
  * a party member who fills goes into DECIDE for D seconds, then the action
    resolves and TTS talks for S seconds
  * an enemy who fills acts at once, and TTS talks about it for S seconds
  * FREEZE RULES:
      wait_sighted : frozen only for the sub-menu part of D. Never during S.
      wait_blind   : the same RULE, with a blind player's much longer D.
      ewm          : frozen for all of D and all of S, every slot but the actor.
      none         : nothing ever frozen (FF8 Active mode, as a control)
"""
import random, statistics, sys

FPS      = 30.0
ATB_MAX  = 12000.0

def rate_for(speed):
    # Relative only -- the study is a ratio, so the constant cancels. Chosen so
    # a Speed-20 character fills in about 8 seconds at 30 fps, which matches the
    # cadence in the logs.
    return ATB_MAX / (FPS * (240.0 / max(speed, 1)))

class Sim:
    def __init__(self, mode, party_spd, enemy_spd, d_top, d_sub, s_tts, rng):
        self.mode = mode
        self.spd  = list(party_spd) + list(enemy_spd)
        self.np   = len(party_spd)
        self.g    = [rng.uniform(0, ATB_MAX * 0.5) for _ in self.spd]
        self.queue = []           # ready combatants, in the order they FILLED
        self.d_top, self.d_sub, self.s_tts = d_top, d_sub, s_tts
        self.rng  = rng
        self.busy_frames = 0      # frames left in the current occupied window
        self.busy_kind   = None   # 'decide' | 'speak'
        self.actor       = None
        self.party_turns = 0
        self.enemy_turns = 0
        self.frozen_frames = 0
        self.total_frames  = 0

    def frozen_now(self):
        if self.busy_kind is None: return False
        if self.mode == 'none': return False
        if self.mode == 'ewm': return True            # decide AND speak
        # wait_*: only the sub-menu slice of a party decision
        return self.busy_kind == 'decide_sub'

    def step(self):
        self.total_frames += 1
        frozen = self.frozen_now()
        if frozen: self.frozen_frames += 1
        # While the player DECIDES the mod excludes the active slot -- but that
        # slot is sitting at MAX, so excluding it moves nothing. While an action
        # or damage animation runs it caps every slot (excludeSlot = 0xFF).
        excluded = self.actor if self.busy_kind in ('decide_top', 'decide_sub') else None
        for i in range(len(self.g)):
            if frozen and i != excluded:
                continue
            if self.g[i] < ATB_MAX:
                self.g[i] += rate_for(self.spd[i])
                if self.g[i] >= ATB_MAX:
                    self.g[i] = ATB_MAX
                    # FILL ORDER is turn order. Reading `ready[0]` off the slot
                    # array instead put every party slot ahead of every enemy
                    # slot and starved the enemies to nearly zero turns -- an
                    # artefact of the model, not of the mod.
                    if i not in self.queue: self.queue.append(i)
        if self.busy_frames > 0:
            self.busy_frames -= 1
            if self.busy_frames == 0:
                self._advance_phase()
            return
        # nobody busy: the longest-ready combatant acts
        if not self.queue: return
        i = self.queue.pop(0)
        self.actor = i
        # THE GAUGE EMPTIES WHEN THE ACTION EXECUTES, NOT WHEN THE MENU OPENS.
        # Getting this backwards is what produced a 96% "enemy turn loss" on the
        # first run: the actor was emptied at the start of an 18-second frozen
        # decide window and then, as the one excluded slot, refilled for free
        # while every other gauge stood still. The engine holds the gauge FULL
        # through the menu and zeroes it on execution -- and the mod releases the
        # freeze at that same moment (battle_tts_ewm_update.inl: "Action
        # executing (no grace) -- release cap", excludeSlot = 0xFF).
        if i < self.np:
            self.party_turns += 1
            self.phase = 'sub'
            self.busy_kind = 'decide_top'
            self.busy_frames = max(1, int(self.rng.gauss(*self.d_top) * FPS))
        else:
            self.enemy_turns += 1
            self.g[i] = 0.0
            self.busy_kind = 'speak'
            self.busy_frames = max(1, int(self.rng.gauss(*self.s_tts) * FPS))

    def _advance_phase(self):
        if self.busy_kind == 'decide_top':
            self.busy_kind = 'decide_sub'
            self.busy_frames = max(1, int(self.rng.gauss(*self.d_sub) * FPS))
        elif self.busy_kind == 'decide_sub':
            self.g[self.actor] = 0.0          # the action executes HERE
            self.busy_kind = 'speak'
            self.busy_frames = max(1, int(self.rng.gauss(*self.s_tts) * FPS))
        else:
            self.busy_kind = None
            self.actor = None

def run(mode, d_top, d_sub, s_tts, party_spd, enemy_spd, turns_target, seed):
    rng = random.Random(seed)
    s = Sim(mode, party_spd, enemy_spd, d_top, d_sub, s_tts, rng)
    while s.party_turns < turns_target and s.total_frames < 30 * 60 * 60 * 6:
        s.step()
    return s

def summarise(label, mode, d_top, d_sub, s_tts, party_spd, enemy_spd, n=400, turns=60):
    ratios, frozen, secs = [], [], []
    for k in range(n):
        s = run(mode, d_top, d_sub, s_tts, party_spd, enemy_spd, turns, 1000 + k)
        if s.party_turns:
            ratios.append(s.enemy_turns / s.party_turns)
            frozen.append(s.frozen_frames / s.total_frames)
            secs.append(s.total_frames / FPS)
    m  = statistics.mean(ratios)
    sd = statistics.pstdev(ratios)
    print("%-34s enemy turns per party turn = %.4f  (sd %.4f)   frozen %5.1f%%   battle %5.1f s"
          % (label, m, sd, 100 * statistics.mean(frozen), statistics.mean(secs)))
    return m

if __name__ == '__main__':
    PARTY = [22, 25, 20]     # three party members
    ENEMY = [22]             # Bahamut's Speed, from the mod's own SCAN-CACHE line
    # (mu, sigma) seconds
    SIGHTED_TOP = (2.0, 0.8);  SIGHTED_SUB = (1.5, 0.6);  SIGHTED_TTS = (0.0, 0.0)
    BLIND_TOP   = (8.0, 3.0);  BLIND_SUB   = (6.0, 2.5);  BLIND_TTS   = (4.0, 1.5)
    print("=== single enemy (Bahamut, Spd 22) vs a three-person party ===")
    a = summarise("FF8 Active (no freeze), sighted", 'none',      SIGHTED_TOP, SIGHTED_SUB, SIGHTED_TTS, PARTY, ENEMY)
    b = summarise("FF8 Wait, sighted",               'wait',      SIGHTED_TOP, SIGHTED_SUB, SIGHTED_TTS, PARTY, ENEMY)
    c = summarise("FF8 Wait, blind + TTS",           'wait',      BLIND_TOP,   BLIND_SUB,   BLIND_TTS,   PARTY, ENEMY)
    d = summarise("Enhanced Wait Mode (the mod)",    'ewm',       BLIND_TOP,   BLIND_SUB,   BLIND_TTS,   PARTY, ENEMY)
    print()
    print("EWM vs FF8 Wait (sighted):  %+.1f%% enemy turns" % (100 * (d / b - 1)))
    print("EWM vs FF8 Active:          %+.1f%% enemy turns" % (100 * (d / a - 1)))
    print("compensating enemy-speed multiplier to match FF8 Wait: %.3fx" % (b / d))
