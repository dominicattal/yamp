#ifndef MP_H
#define MP_H

#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <deque>

struct Song {
    std::string title;
    std::string artist;
    std::string track;
    std::string path;
    int id;
};

struct Album {
    std::string name;
    std::vector<Song*> songs;
    int id;
};

struct Playlist {
    std::string name;
    std::vector<Song*> songs;
    int id;
};

struct MPContext {
    Song current_song;
    std::vector<Song> songs;
    std::vector<Album> albums;
    std::vector<Playlist> playlists;
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

Song* mp_get_song_by_id(int id);
Album* mp_get_album_by_id(int id);

// Skip current song in queue
void mp_queue_skip();

#endif
