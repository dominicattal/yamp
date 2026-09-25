#ifndef MP_H
#define MP_H

#include "lru_cache.h"
#include <string>
#include <string_view>
#include <vector>
#include <deque>
#include <functional>
#include <memory>

enum LoopMode {
    LOOP_NONE,
    LOOP_GROUP,
    LOOP_TRACK
};

struct Song {
    std::string title;
    std::string path;
    int id;
    float length;
};

struct SongTrack {
    LruCacheRef<Song> song;
    int track;
};

struct SongTrackID {
    int song_id;
    int track;
};

struct Artist {
    std::string name;
    int id;
};

struct Album {
    std::string name;
    int id;
    float length;
};

struct Playlist {
    std::string name;
    int id;
    float length;
};

struct FrontCover {
    unsigned char* data;
    int width;
    int height;
};

using SongCallback = std::function<void(Song* song)>;

struct MPContext {

    LruCache<Song> songs;
    LruCache<Album> albums;
    LruCache<Artist> artists;
    LruCache<Playlist> playlists;

    LruCache<int> song_artist;
    LruCache<int> song_album;
    LruCache<int> album_artist;
    LruCache<std::vector<SongTrackID>> album_songs;
    LruCache<std::vector<SongTrackID>> playlist_songs;
    LruCache<std::vector<int>> artist_songs;
    LruCache<std::vector<int>> artist_albums;

    LruCacheRef<Song> current_song;

    SongCallback song_constructor_callback;

    // this stores songs the user explicity queues up
    std::deque<LruCacheRef<Song>> queue;

    // this stores songs that come on autoplay
    std::deque<LruCacheRef<Song>> autoplay_queue;

    // this stores the order songs should be played in the group
    std::deque<SongTrack> group_queue;

    // this stores the songs from the most recent search
    std::vector<LruCacheRef<Song>> search_result;
    int num_results;

    // whether mp is playing a group (playlist or album) or not.
    bool playing_group;
    // true if group is album, false otherwise
    bool group_is_album;
    // album_id if group_is_album == true, playlist_id otherwise
    int group_id;

    float volume;

    // seconds
    float current_song_cursor;
    float current_song_length;

    // whether to play songs in random order. when this is on, music will play even
    // if the end of the queue is reached
    bool shuffle;

    bool autoplay;

    int loop_mode;
};

extern MPContext mp_ctx;

// Initializes and cleans up music play context
void mp_init();
void mp_cleanup();

// Call this every frame. Checks whether current song ended or not
void mp_update();

// Add a song to the database. Right now, just add it to the database whether or not it exists already.
// Eventually, flow should be like preview song in ui -> add config -> choose to save to database
void mp_add_song(const std::string& song_path);
void mp_add_songs(const std::vector<std::string>& song_paths);
void mp_recursive_add_songs(const std::string& folder_path);

// Add a playlist to the database. Returns the id of the created playlist.
LruCacheRef<Playlist> mp_create_playlist();
void mp_rename_playlist(int playlist_id, const char* new_playlist_name);

// Immediately play a song
void mp_play_song(int song_id);

// Queue a song or mulitple songs
void mp_queue_song(int song_id);

// Will pause if song is playing and resume if song is not playing
void mp_pause_or_resume();

void mp_toggle_shuffle();
void mp_toggle_autoplay();
void mp_update_volume();
void mp_update_cursor();

void mp_play_playlist(int playlist_id);
void mp_play_album(int album_id);

// Load song cover art from memory
FrontCover mp_song_front_cover_load(const std::string& cover_path);
void mp_song_front_cover_update(int song_id, const std::string& cover_path);
void mp_song_front_cover_free(FrontCover* data);
void mp_song_update(int song_id, const char* title, const char* artist, const char* album, const char* cover_path);

// Search for songs based on query
const std::vector<LruCacheRef<Song>>& mp_search_songs(const char* search_query, int page_limit, int page_num);

LruCacheRef<Song>       mp_get_song(int song_id);
LruCacheRef<Album>      mp_get_album(int album_id);
LruCacheRef<Artist>     mp_get_artist(int artist_id);
LruCacheRef<Playlist>   mp_get_playlist(int playlist_id);

LruCacheRef<Artist>     mp_get_artist_from_song(int song_id);
LruCacheRef<Album>      mp_get_album_from_song(int song_id);
LruCacheRef<Artist>     mp_get_artist_from_album(int album_id);

LruCacheRef<std::vector<SongTrackID>> mp_get_songs_from_album(int album_id);
LruCacheRef<std::vector<SongTrackID>> mp_get_songs_from_playlist(int playlist_id);
LruCacheRef<std::vector<int>> mp_get_songs_from_artist(int artist_id);
LruCacheRef<std::vector<int>> mp_get_albums_from_artist(int artist_id);

std::vector<LruCacheRef<Playlist>> mp_get_playlists();

void mp_add_song_to_playlist(int song_id, int playlist_id);
void mp_add_album_to_playlist(int album_id, int playlist_id);

// Skip current song in queue
void mp_queue_skip();
void mp_queue_clear();

#endif
