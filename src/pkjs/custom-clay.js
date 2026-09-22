// Injected into the Clay settings page (via toString), so it must be self-contained.
// Choosing a preset fills in every colour picker; editing any picker by hand flips
// the preset back to "Custom" unless the colours still match a preset exactly.
module.exports = function () {
  var clayConfig = this;

  var KEYS = ['COLOR_BG', 'COLOR_TIME', 'COLOR_TEXT', 'COLOR_LABEL', 'COLOR_RULE',
              'COLOR_BAR', 'COLOR_EMPTY', 'ACCENT', 'COLOR_HEART'];

  //                 bg        time      text      label     rule      bar       empty     accent    heart
  var PRESETS = {
    classic: ['000000', 'FFFFFF', 'FFFFFF', 'AAAAAA', 'FFFFFF', 'FFFFFF', '555555', 'FF0000', 'FF0000'],
    paper:   ['FFFFFF', '000000', '000000', '555555', '000000', '000000', 'AAAAAA', 'FF0000', 'FF0000'],
    mint:    ['000000', '55FFAA', 'FFFFFF', 'AAAAAA', '00AA55', '55FFAA', '005555', '00FFAA', 'FF5555'],
    amber:   ['000000', 'FFAA00', 'FFAA55', 'AA5500', 'AA5500', 'FFAA00', '550000', 'FF5500', 'FF0055'],
    ice:     ['000055', 'FFFFFF', 'AAFFFF', '55AAFF', '0055AA', 'AAFFFF', '0000AA', '00FFFF', 'FF55AA'],
    night:   ['000000', 'FF0000', 'FF0000', 'AA0000', 'AA0000', 'FF0000', '550000', 'FF5555', 'FF0000'],
    sangria: ['FFFFFF', '000000', '000000', 'AA0000', 'AA0000', '000000', 'AAAAAA', 'AA0000', 'FF0055']
  };

  var applying = false;

  function pickers() {
    return KEYS.map(function (k) { return clayConfig.getItemByMessageKey(k); });
  }

  function matchingPreset() {
    var current = pickers().map(function (p) { return p.get(); });
    for (var name in PRESETS) {
      var same = PRESETS[name].every(function (hex, i) { return parseInt(hex, 16) === current[i]; });
      if (same) return name;
    }
    return '';
  }

  clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function () {
    var preset = clayConfig.getItemById('preset');
    if (!preset) return;   // black-and-white watch: colour items aren't on the page

    preset.set(matchingPreset());

    preset.on('change', function () {
      var colors = PRESETS[preset.get()];
      if (!colors) return;
      applying = true;
      pickers().forEach(function (p, i) { p.set(colors[i]); });
      applying = false;
    });

    pickers().forEach(function (p) {
      p.on('change', function () {
        if (!applying) preset.set(matchingPreset());
      });
    });
  });
};
