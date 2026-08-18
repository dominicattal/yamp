CREATE TABLE IF NOT EXISTS Songs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    title VARCHAR(255),
    artist VARCHAR(255),
    date INT,
    path VARCHAR(255)
);

CREATE TABLE IF NOT EXISTS Album (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name VARCHAR(255),
    path VARCHAR(255)
);

CREATE TABLE IF NOT EXISTS Playlist (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name VARCHAR(255)
);

CREATE TABLE IF NOT EXISTS AlbumSong (
    song_id INT,
    album_id INT,
    track INT
);

CREATE TABLE IF NOT EXISTS PlaylistSong (
    song_id INT,
    playlist_id INT,
    track INT
);

