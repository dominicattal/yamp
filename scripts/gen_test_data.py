import sqlite3
import os
from random import randrange

cover_art_bytes = 262144
random_cover_art = sqlite3.Binary(bytes([randrange(0,256) for _ in range(cover_art_bytes)]))

test_song_path = "assets/music/ChopinFourteenWaltzes/06 No. 6 in D flat, Op. 64 No. 1 Minute valse.mp3"

db_path = "build/test1M.db"
try:
    os.remove(db_path)
except:
    pass

con = sqlite3.connect(db_path)
cur = con.cursor()

schema_file = open("assets/sql/schema.sql", "r")
schema = schema_file.read()
schema_file.close()

cur.executescript(schema)

N = 1000000

artist_num = 0
tot_num_songs = 0
tot_num_albums = 0
while tot_num_songs < N:
    num_albums = randrange(3, 10)
    cur.execute("INSERT INTO Artists (name) VALUES (?)", [f"Artist {artist_num+1}"])
    cur.executemany("INSERT INTO Albums (name, cover) VALUES (?, ?)",
                    [[f"Album {album_num+tot_num_albums+1}", random_cover_art] for album_num in range(num_albums)])
    cur.executemany("INSERT INTO ArtistAlbum (artist_id, album_id) VALUES (?, ?)",
                    [[artist_num+1, album_num+tot_num_albums+1] for album_num in range(num_albums)])
    for album_num in range(num_albums):
        num_songs = min(randrange(12, 100), N - tot_num_songs)
        cur.executemany("INSERT INTO Songs (title, path, cover) VALUES (?, ?, ?)", 
                        [[f"Song {song_num+tot_num_songs+1}", test_song_path, random_cover_art] for song_num in range(num_songs)])
        cur.executemany("INSERT INTO AlbumSong (album_id, song_id, track) VALUES (?, ?, ?)",
                        [(album_num+tot_num_albums+1, song_num+tot_num_songs+1, song_num+1) for song_num in range(num_songs)])
        cur.executemany("INSERT INTO ArtistSong (artist_id, song_id) VALUES (?, ?)",
                        [(artist_num+1, song_num+tot_num_songs+1) for song_num in range(num_songs)])
        tot_num_songs += num_songs

    tot_num_albums += num_albums
    artist_num += 1

con.commit()

con.close()
