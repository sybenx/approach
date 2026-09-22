// Runs on the phone. Fetches current weather from Open-Meteo (no API key)
// and sends it to the watch. The watch asks for a refresh every 30 minutes.

var TEMP_UNIT = 'fahrenheit';   // or 'celsius'

// Map WMO weather codes to the three icons the face knows:
// 0 = sun, 1 = sun behind cloud, 2 = rain (covers drizzle, snow, storms)
function conditionFromCode(code) {
  if (code <= 1) return 0;
  if (code <= 3 || code === 45 || code === 48) return 1;
  return 2;
}

function sendWeather(temp, cond) {
  Pebble.sendAppMessage({ TEMP: temp, COND: cond },
    function () {},
    function (e) { console.log('Send failed: ' + JSON.stringify(e)); });
}

function fetchWeather() {
  navigator.geolocation.getCurrentPosition(function (pos) {
    var url = 'https://api.open-meteo.com/v1/forecast'
      + '?latitude=' + pos.coords.latitude.toFixed(3)
      + '&longitude=' + pos.coords.longitude.toFixed(3)
      + '&current=temperature_2m,weather_code'
      + '&temperature_unit=' + TEMP_UNIT;

    var xhr = new XMLHttpRequest();
    xhr.onload = function () {
      try {
        var d = JSON.parse(this.responseText);
        sendWeather(Math.round(d.current.temperature_2m), conditionFromCode(d.current.weather_code));
      } catch (e) {
        console.log('Weather parse failed: ' + e);
      }
    };
    xhr.onerror = function () { console.log('Weather request failed'); };
    xhr.open('GET', url);
    xhr.send();
  }, function (err) {
    console.log('Location failed: ' + err.message);
  }, { timeout: 15000, maximumAge: 600000 });
}

Pebble.addEventListener('ready', function () { fetchWeather(); });
Pebble.addEventListener('appmessage', function () { fetchWeather(); });
