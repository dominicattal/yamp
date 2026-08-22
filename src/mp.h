#ifndef MP_H
#define MP_H

#include <string>
#include <string_view>
#include <vector>
#include <deque>

struct Song {
    std::string title;
    std::string path;
    int id;
};

struct Artist {
    std::string name;
    int id;
};

struct Album {
    std::string name;
    int id;
};

struct Playlist {
    std::string name;
    int id;
};

struct AlbumSong {
    int album_id;
    int song_id;
    int track;
};

struct PlaylistSong {
    int playlist_id;
    int song_id;
    int track;
};

struct ArtistSong {
    int artist_id;
    int song_id;
};

struct ArtistAlbum {
    int artist_id;
    int album_id;
};

struct SongTrack {
    int song_id;
    int track;
};

struct MPContext {
    Song current_song;
    std::vector<Song> songs;
    std::vector<Album> albums;
    std::vector<Artist> artists;
    std::vector<Playlist> playlists;
    std::vector<AlbumSong> album_songs;
    std::vector<ArtistSong> artist_songs;
    std::vector<ArtistAlbum> artist_albums;
    std::vector<PlaylistSong> playlist_songs;
    std::deque<std::string> queue;
};

extern MPContext mp_ctx;

// Initializes and cleans up music play context
void mp_init();
void mp_cleanup();

// Add a song to the program. Right now, just add it to the database whether or not it exists.
// Eventually, flow should be like preview song in ui -> add config -> choose to save to database
void mp_add_song(const std::string& song_path);
void mp_add_songs(const std::vector<std::string>& song_paths);

// Immediately play a song
void mp_play_song(const std::string& song_path);

// Queue a song or mulitple songs
void mp_queue_song(const std::string& song_path);
void mp_queue_songs(const std::vector<std::string>& song_paths);

const Song* mp_get_song_by_id(int id);
const Album* mp_get_album_by_id(int id);
const Artist* mp_get_artist_by_id(int id);

int mp_get_album_id_from_song_id(int song_id);
int mp_get_artist_id_from_song_id(int song_id);
int mp_get_artist_id_from_album_id(int album_id);
std::vector<SongTrack> mp_get_song_ids_from_album_id(int album_id);
std::vector<int> mp_get_album_ids_from_artist_id(int artist_id);

// Skip current song in queue
void mp_queue_skip();

#endif
