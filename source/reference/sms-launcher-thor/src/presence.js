'use strict';

// What Discord shows while the game runs. The game prints a `[presence] {...}`
// line whenever what the player is doing changes (sms-pc-port's
// platform/presence, with SMS_PRESENCE=1); this turns the latest one into a
// Discord activity. Image keys are art assets uploaded to the Discord
// application (Rich Presence > Art Assets).
const LAUNCHER_URL = 'https://github.com/chasem-dev/sms-launcher/releases/latest';
const DISCORD_URL = 'https://discord.gg/Qh8rGWAvDD';
const PREFIX = '[presence] ';

// TApplication::mAppState.
const STATE = { BOOT: 2, NLOGO: 3, DONE: 4, GAMEPLAY: 5, MOVIE: 6, TITLE: 8, MENU: 9 };
const OPTION_MAP = 15;
const SHINES = 120, BLUE_COINS = 240;

// Shine stages in stagename.bmg order, with their art asset keys.
const STAGES = [
  ['Delfino Airstrip', 'airstrip'], ['Delfino Plaza', 'delfino_plaza'], ['Bianco Hills', 'bianco_hills'],
  ['Ricco Harbor', 'ricco_harbor'], ['Gelato Beach', 'gelato_beach'], ['Pinna Park', 'pinna_park'],
  ['Sirena Beach', 'sirena_beach'], ['Pianta Village', 'pianta_village'], ['Noki Bay', 'noki_bay'],
  ['Corona Mountain', 'corona_mountain']
];

// Area -> shine stage (StageUtil.cpp's shineStageTable).
const SHINE_STAGE = [
  0, 1, 2, 3, 4, 5, 6, 6, 7, 8, 9, 1, 1, 5, 6, 1, 8, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 1, 3, 8,
  4, 4, 5, 5, 5, 5, 5, 5, 6, 5, 7, 7, 8, 8, 2, 2, 3, 3, 5, 6, 9, 1, 1, 2, 6, 8, 5, 3, 9
];

// Areas that are a place of their own within a stage.
const PLACES = { 7: 'Hotel Delfino', 14: 'Casino Delfino', 16: 'Noki Bay depths' };

// Episode names in the order the episode select screen lists them, by shine stage.
const EPISODES = {
  2: ['Road to the Big Windmill', 'Down with Petey Piranha!', 'The Hillside Cave Secret',
    'Red Coins of Windmill Village', 'Petey Piranha Strikes Back', 'The Secret of the Dirty Lake',
    'Shadow Mario on the Loose', 'The Red Coins of the Lake'],
  3: ['Gooper Blooper Breaks Out', 'Blooper Surfing Safari', 'The Caged Shine Sprite', 'The Secret of Ricco Tower',
    'Gooper Blooper Returns', 'Red Coins on the Water', 'Shadow Mario Revisited', "Yoshi's Fruit Adventure"],
  4: ['Dune Bud Sand Castle Secret', 'Mirror Madness! Tilt, Slam, Bam!', 'Wiggler Ahoy! Full Steam Ahead!',
    'The Sand Bird is Born', "Il Piantissimo's Sand Sprint", 'Red Coins in the Coral Reef',
    "It's Shadow Mario! After Him!", 'The Watermelon Festival'],
  5: ['Mecha-Bowser Appears!', "The Beach Cannon's Secret", 'Red Coins of the Pirate Ships', 'The Wilted Sunflowers',
    'The Runaway Ferris Wheel', "The Yoshi-Go-Round's Secret", 'Shadow Mario in the Park', 'Roller Coaster Balloons'],
  6: ['The Manta Storm', "The Hotel Lobby's Secret", 'Mysterious Hotel Delfino', 'The Secret of Casino Delfino',
    'King Boo Down Below', 'Scrubbing Sirena Beach', 'Shadow Mario Checks In', 'Red Coins in the Hotel'],
  7: ['Chain Chomplets Unchained', "Il Piantissimo's Crazy Climb", 'The Goopy Inferno', "Chain Chomp's Bath",
    'Secret of the Village Underside', 'Piantas in Need', 'Shadow Mario Runs Wild', 'Fluff Festival Coin Hunt'],
  8: ['Uncork the Waterfall', 'The Boss of Tricky Ruins', 'Red Coins in a Bottle', "Eely-Mouth's Dentist",
    "Il Piantissimo's Surf Swim", "The Shell's Secret", 'Hold It, Shadow Mario!', 'The Red Coin Fish']
};

// A game output line -> its presence, or null for any other line.
function parseLine(line) {
  if (typeof line !== 'string' || !line.startsWith(PREFIX)) return null;
  try {
    const data = JSON.parse(line.slice(PREFIX.length));
    return data && typeof data === 'object' && Number.isInteger(data.state) ? data : null;
  } catch (_) { return null; }
}

function isPresenceLine(line) { return typeof line === 'string' && line.startsWith(PREFIX); }

function plural(count, word, words = `${word}s`) { return `${count} ${count === 1 ? word : words}`; }

function hours(ms) {
  const total = Math.floor(ms / 60000);
  return total < 60 ? `${total} min played` : `${Math.floor(total / 60)} h played`;
}

// Where the player is: a stage, a place in one, a secret course or a boss.
function place(area, episode) {
  const stage = STAGES[SHINE_STAGE[area]];
  if (!stage) return null;
  const [stageName, image] = stage;
  if (area >= 0x37 && area <= 0x3c) return { name: `Boss fight · ${stageName}`, image };
  if (area > 0x14 && area < 0x34) return { name: `Secret course · ${stageName}`, image };
  const title = EPISODES[SHINE_STAGE[area]]?.[episode];
  const name = PLACES[area] || stageName;
  return { name: title ? `${name} · ${title}` : name, image };
}

// The latest game presence and the session -> a Discord activity.
function activity(game, { startedAt, eclipse = false, playtime = 0 } = {}) {
  const counts = game && game.shines > 0
    ? `${eclipse ? plural(game.shines, 'Shine') : `${game.shines} / ${SHINES} Shines`}${game.blueCoins > 0
      ? ` · ${eclipse ? plural(game.blueCoins, 'Blue Coin') : `${game.blueCoins} / ${BLUE_COINS} Blue Coins`}` : ''}`
    : undefined;
  const result = {
    // A game without platform/presence never says more than this.
    details: eclipse ? 'Playing Super Mario Eclipse' : undefined,
    state: counts,
    timestamps: startedAt ? { start: startedAt } : undefined,
    assets: { large_image: eclipse ? 'eclipse' : 'logo', large_text: eclipse ? 'Super Mario Eclipse' : 'Super Mario Sunshine',
      small_image: 'launcher', small_text: 'Played with SMS Launcher' },
    buttons: [{ label: 'Play it on PC', url: LAUNCHER_URL }, { label: 'Join the Discord', url: DISCORD_URL }]
  };
  if (game && !eclipse) {
    const where = game.state === STATE.GAMEPLAY && game.area !== OPTION_MAP ? place(game.area, game.episode) : null;
    if (where) {
      result.details = where.name;
      result.assets.large_image = where.image;
      result.assets.large_text = [game.lives > 0 ? plural(game.lives, 'life', 'lives') : '',
        playtime >= 60000 ? hours(playtime) : ''].filter(Boolean).join(' · ') || 'Super Mario Sunshine';
    } else if (game.state === STATE.TITLE) {
      result.details = 'Choosing an episode';
      const stage = STAGES[SHINE_STAGE[game.area]];
      if (stage) { result.state = counts ? `${stage[0]} · ${counts}` : stage[0]; result.assets.large_image = stage[1]; }
    } else if (game.state === STATE.MOVIE || game.state === STATE.DONE) result.details = 'Watching a cutscene';
    else if ((game.state === STATE.GAMEPLAY && game.area === OPTION_MAP) || game.state === STATE.MENU)
      result.details = 'On the title screen';
    else if (game.state === STATE.BOOT || game.state === STATE.NLOGO) result.details = 'Starting up';
  }
  if (game?.paused) result.state = result.state ? `Paused · ${result.state}` : 'Paused';
  else if (game?.cutscene && !eclipse) result.state = 'Watching a cutscene';
  // Discord rejects texts shorter than 2 or longer than 128 characters.
  for (const key of ['details', 'state']) if (result[key]) result[key] = result[key].slice(0, 128);
  return JSON.parse(JSON.stringify(result));
}

module.exports = { LAUNCHER_URL, DISCORD_URL, PREFIX, STAGES, EPISODES, parseLine, isPresenceLine, activity };
