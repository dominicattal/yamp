#ifndef MP_H
#define MP_H

#include "lru_cache.h"
#include <string>
#include <string_view>
#include <vector>
#include <deque>
#include <functional>
#include <memory>

using GenericID = int;
using SongID = int;
using AlbumID = int;
using ArtistID = int;
using PlaylistID = int;
using MPUID = uint64_t;

#define INVALID_ID -1
#define HISTORY_MAX_SIZE 1000
#define STRING_LENGTH 512
#define TEXTURE_SIZE_BITS 12
#define LARGE_COVER_ART_SIZE_BITS 8
#define SMALL_COVER_ART_SIZE_BITS 6
#define TEXTURE_SIZE (1<<TEXTURE_SIZE_BITS)
#define LARGE_COVER_ART_SIZE (1<<LARGE_COVER_ART_SIZE_BITS)
#define SMALL_COVER_ART_SIZE (1<<SMALL_COVER_ART_SIZE_BITS)
#define SLOTS_PER_TEXTURE (1<<(TEXTURE_SIZE_BITS<<1)>>(LARGE_COVER_ART_SIZE_BITS<<1))

enum LoopMode {
    LOOP_NONE,
    LOOP_GROUP,
    LOOP_TRACK
};

struct Song {
    std::string title;
    std::string path;
    SongID id;
    float length;
};

struct SongTrack {
    LruCacheRef<Song> song;
    int track;
};

struct SongTrackID {
    SongID song_id;
    int track;
};

struct Artist {
    std::string name;
    ArtistID id;
};

struct Album {
    std::string name;
    AlbumID id;
    float length;
};

struct Playlist {
    std::string name;
    PlaylistID id;
    float length;
};

struct Art {
    void* sqlite_stmt;
    const uint8_t* data;
    size_t size;
};

template<typename T>
struct SearchResult {
    std::vector<LruCacheRef<T>> entries;
    int page_num;
    int num_pages;
    int num_songs;
};

using SongCallback = std::function<void(Song* song)>;

struct MPContext {

    LruCache<Song> songs;
    LruCache<Album> albums;
    LruCache<Artist> artists;
    LruCache<Playlist> playlists;

    LruCache<ArtistID> song_artist;
    LruCache<AlbumID> song_album;
    LruCache<ArtistID> album_artist;
    LruCache<std::vector<SongTrackID>> album_songs;
    LruCache<std::vector<SongTrackID>> playlist_songs;
    LruCache<std::vector<SongID>> artist_songs;
    LruCache<std::vector<AlbumID>> artist_albums;

    LruCacheRef<Song> current_song;
    LruCacheRef<Artist> current_song_artist;
    LruCacheRef<Album> current_song_album;

    SongCallback song_constructor_callback;

    // this stores songs the user explicity queues up
    std::deque<LruCacheRef<Song>> queue;

    // this stores songs that come on autoplay
    std::deque<LruCacheRef<Song>> autoplay_queue;

    // this stores the order songs should be played in the group
    std::deque<SongTrack> group_queue;

    // this stores the history of played songs
    std::deque<SongID> song_history;

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

    bool paused;
    bool autoplay;

    LoopMode loop_mode;
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
void mp_rename_playlist(PlaylistID playlist_id, const char* new_playlist_name);

// Immediately play a song
void mp_play_song(SongID song_id);

// Queue a song or mulitple songs
void mp_queue_song(SongID song_id);

// Will pause if song is playing and resume if song is not playing
void mp_pause_or_resume();

void mp_toggle_shuffle();
void mp_toggle_autoplay();
void mp_update_volume();
void mp_update_cursor();

void mp_play_playlist(int playlist_id);
void mp_play_album(AlbumID album_id);

// Search for songs based on query
const std::vector<LruCacheRef<Song>>& mp_search_songs(const char* search_query, int page_limit, int page_num);

LruCacheRef<Song>       mp_get_song(SongID song_id);
LruCacheRef<Album>      mp_get_album(AlbumID album_id);
LruCacheRef<Artist>     mp_get_artist(ArtistID artist_id);
LruCacheRef<Playlist>   mp_get_playlist(int playlist_id);

LruCacheRef<Artist>     mp_get_artist_from_song(SongID song_id);
LruCacheRef<Album>      mp_get_album_from_song(SongID song_id);
LruCacheRef<Artist>     mp_get_artist_from_album(AlbumID album_id);

LruCacheRef<std::vector<SongTrackID>> mp_get_songs_from_album(AlbumID album_id);
LruCacheRef<std::vector<SongTrackID>> mp_get_songs_from_playlist(int playlist_id);
LruCacheRef<std::vector<int>> mp_get_songs_from_artist(ArtistID artist_id);
LruCacheRef<std::vector<int>> mp_get_albums_from_artist(ArtistID artist_id);

// If page_num == -1, then return the first page and the number of results
// If page_num >= 0, then assume caller knows the number of results and just return songs
SearchResult<Song> mp_get_paginated_songs_from_artist(ArtistID artist_id, int page_limit, int page_num = -1);

std::vector<LruCacheRef<Playlist>> mp_get_playlists();

void mp_add_song_to_playlist(SongID song_id, int playlist_id);
void mp_add_album_to_playlist(AlbumID album_id, int playlist_id);

Art mp_get_song_art(SongID song_id);
Art mp_get_album_art(AlbumID album_id);
void mp_free_art(Art* art);

// Skip current song in queue
void mp_play_previous();
void mp_play_next();
void mp_queue_clear();

MPUID mp_get_song_uid(SongID song_id);
MPUID mp_get_playlist_uid(PlaylistID playlist_id);
MPUID mp_get_album_uid(PlaylistID playlist_id);
MPUID mp_get_artist_uid(ArtistID artist_id);

#endif
