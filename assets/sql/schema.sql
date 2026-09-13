CREATE TABLE IF NOT EXISTS Songs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    title VARCHAR(256),
    path VARCHAR(256) UNIQUE,
    length REAL
);

CREATE TABLE IF NOT EXISTS Artists (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name VARCHAR(256) UNIQUE
);

CREATE TABLE IF NOT EXISTS Albums (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name VARCHAR(256) UNIQUE
);

CREATE TABLE IF NOT EXISTS Playlists (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name VARCHAR(256) UNIQUE
);

CREATE TABLE IF NOT EXISTS ArtistAlbum (
    artist_id INTEGER,
    album_id INTEGER,
    PRIMARY KEY (artist_id, album_id)
);

CREATE TABLE IF NOT EXISTS ArtistSong (
    artist_id INTEGER,
    song_id INTEGER,
    PRIMARY KEY (artist_id, song_id)
);

CREATE TABLE IF NOT EXISTS AlbumSong (
    album_id INTEGER,
    song_id INTEGER,
    track INTEGER,
    PRIMARY KEY (album_id, song_id)
);

CREATE TABLE IF NOT EXISTS PlaylistSong (
    playlist_id INTEGER,
    song_id INTEGER,
    track INTEGER,
    PRIMARY KEY (playlist_id, song_id)
);
