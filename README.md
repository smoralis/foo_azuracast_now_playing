# foo_azuracast_now_playing

A foobar2000 component that shows what is playing on an [AzuraCast](https://www.azuracast.com/) station: station name and description, title, artist, album, cover art, listener count, the next track, and a progress bar with elapsed / total time. The data comes from the station's public Now Playing API.

Created primary to display azuracast HLS streams metadata that foobar2000 doesn't.

Supports **Default UI** or **Columns UI** panel.




## Configuration

| Setting | Description | Default |
|---|---|---|
| Fixed Station API URL | e.g. `https://host/api/nowplaying/shortcode`. If left empty, the component works out the API endpoint from the stream that is playing (see below). | empty |
| Poll interval (ms) | How often the API is polled.| 10000 |


### Auto-detect mode

With no fixed URL set, and a track that is an `http(s)` stream, the component tries the following autoprobe , in order:

1. If the stream path contains `/listen/<shortcode>/` or `/hls/<shortcode>/`, it uses `https://host/api/nowplaying/<shortcode>`.
2. Otherwise it reads `https://host/api/nowplaying` and matches the stream against each station's `listen_url` and mount URLs, ignoring any query string.


## Cover sync delay: `?coversync=<seconds>`

Add `coversync` to the URL to hold the display back by that many seconds:

```
https://moodradio.peppermindmedia.com/hls/80s-hits/80s_hits.m3u8?coversync=70
https://moodradio.peppermindmedia.com/api/nowplaying/80s-hits?coversync=70
```

The console log (`foo_azuracast_now_playing:` prefix) prints the active delay when one is set.


