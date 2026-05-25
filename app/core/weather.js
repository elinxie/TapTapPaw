// weather.js
// Works on macOS + Windows (Node.js 18+ for built-in fetch)
// API used: Open-Meteo (no API key)

const WEATHER_CODE_MAP = {
  0: "sun",
  1: "mainly_clear",
  2: "partly_cloudy",
  3: "cloudy",
  45: "fog",
  48: "fog",
  51: "drizzle",
  53: "drizzle",
  55: "drizzle",
  61: "rain",
  63: "rain",
  65: "heavy_rain",
  66: "freezing_rain",
  67: "freezing_rain",
  71: "snow",
  73: "snow",
  75: "snow",
  77: "snow",
  80: "rain_showers",
  81: "rain_showers",
  82: "heavy_showers",
  95: "thunderstorm",
  96: "thunderstorm_hail",
  99: "thunderstorm_hail",
};

// ESP32 wallpaper_sjpg order:
// 0 sunny, 1 cloudy, 2 rainy, 3 stormy, 4 snowy, 5 dark/night
function classify(code, isDay = true) {
  const clearCodes = [0, 1];
  const cloudCodes = [2, 3, 45, 48];
  const rainCodes = [51, 53, 55, 61, 63, 65, 66, 67, 80, 81, 82];
  const stormCodes = [95, 96, 99];
  const snowCodes = [71, 73, 75, 77];

  // At night (after ~6pm and before ~6am via API is_day), clear weather uses night wallpaper
  if (clearCodes.includes(code)) return isDay ? 0 : 5;
  if (cloudCodes.includes(code)) return isDay ? 1 : 5;
  if (rainCodes.includes(code)) return 2;
  if (stormCodes.includes(code)) return 3;
  if (snowCodes.includes(code)) return 4;

  // Fallback to cloudy if code is unknown
  return 1;
}

async function getGeoLocation() {
  const url = 'http://ip-api.com/json/';
  const res = await fetch(url);
  if (!res.ok) {
    throw new Error(`GeoLocation API failed: ${res.status}`);
  }
  const data = await res.json();
  if (data.status !== 'success') {
    throw new Error(`GeoLocation API failed: ${data.message || 'Unknown error'}`);
  }
  return {
    lat: data.lat,
    lon: data.lon,
  };
}
async function getWeather(lat, lon) {
  const url =
    `https://api.open-meteo.com/v1/forecast` +
    `?latitude=${lat}&longitude=${lon}` +
    `&current=temperature_2m,relative_humidity_2m,weather_code,precipitation,is_day` +
    `&timezone=auto`;

  const res = await fetch(url);
  if (!res.ok) throw new Error(`Weather API failed: ${res.status}`);

  const data = await res.json();
  const c = data.current;
  const isDay = Number(c.is_day) === 1;

  return {
    temp_c: Math.round(c.temperature_2m),
    humidity: c.relative_humidity_2m,
  //  precipitation_mm: c.precipitation,
 //   weather_code: c.weather_code,
 //   weather_text: WEATHER_CODE_MAP[c.weather_code] || "unknown",
    weather_group: classify(c.weather_code, isDay), // 0..5 -> wallpaper_sjpg index
    observed_at: data.current.time,
  };
}

// example usage

(async () => {
  try {
    const weather = await getWeather(13.7563, 100.5018); // Bangkok
    console.log(weather);
  } catch (err) {
    console.error(err.message);
  }
})();


module.exports = { getWeather, getGeoLocation };
