#ifndef MP_H
#define MP_H

#include "lru_cache.h"
#include "db.h"
#include <string>
#include <string_view>
#include <vector>
#include <deque>
#include <functional>
#include <memory>
#include <future>

#define HISTORY_MAX_SIZE 1000
#define TEXTURE_SIZE_BITS 12
#define LARGE_COVER_ART_SIZE_BITS 8
#define SMALL_COVER_ART_SIZE_BITS 6
#define TEXTURE_SIZE (1<<TEXTURE_SIZE_BITS)
#define LARGE_COVER_ART_SIZE (1<<LARGE_COVER_ART_SIZE_BITS)
#define SMALL_COVER_ART_SIZE (1<<SMALL_COVER_ART_SIZE_BITS)
#define SLOTS_PER_TEXTURE (1<<(TEXTURE_SIZE_BITS<<1)>>(LARGE_COVER_ART_SIZE_BITS<<1))
#define MAX_LYRICS_SIZE (1<<15)

struct SongTrack {
    LruCacheRef<Song> song;
    int track;
};

struct Pin {
    LruCacheRef<Song> song;
    LruCacheRef<Album> album;
    LruCacheRef<Artist> artist;
    LruCacheRef<Playlist> playlist;
    MPUIDEnum type;
    int order;
};

template<typename T>
struct SearchResult {
    std::vector<LruCacheRef<T>> entries;
    int page_num;
    int num_pages;
    int results_per_page;
    int num_results;
};

struct MPContext {

    LruCache<Song> songs;
    LruCache<Album> albums;
    LruCache<Artist> artists;
    LruCache<Playlist> playlists;

    LruCache<ArtistID> song_artist;
    LruCache<AlbumID> song_album;
    LruCache<ArtistID> album_artist;

    LruCacheRef<Song> current_song;
    LruCacheRef<Artist> current_song_artist;
    LruCacheRef<Album> current_song_album;
    char current_song_lyrics[MAX_LYRICS_SIZE];

    int num_songs;
    int num_albums;
    int num_artists;
    int num_playlists;

    // in seconds
    float current_song_cursor;
    float current_song_length;

    // this stores songs the user explicity queues up
    std::deque<LruCacheRef<Song>> queue;

    // this stores songs that come on autoplay
    std::deque<LruCacheRef<Song>> autoplay_queue;

    // this stores the order songs should be played in the group
    std::deque<SongTrack> group_queue;

    // this stores the history of played songs
    std::deque<SongID> song_history;

    // whether mp is playing a group (playlist or album) or not.
    bool playing_group;
    // true if group is album, false otherwise
    bool group_is_album;
    // album_id if group_is_album == true, playlist_id otherwise
    int group_id;

    bool mute;
    float volume;

    // whether to play songs in random order. when this is on, music will play even
    // if the end of the queue is reached
    bool shuffle;

    bool paused;
    bool autoplay;

    LoopMode loop_mode;

    std::vector<Pin> pins;

    bool adding_songs;
    int num_songs_added;
    int num_songs_to_add;
    std::string adding_songs_current_path;
    std::mutex adding_songs_mutex;
    std::vector<SongID> added_songs;
    std::vector<ArtistID> added_artists;
    std::vector<AlbumID> added_albums;
};

extern MPContext mp_ctx;

// Initializes and cleans up music play context
void mp_init();
void mp_cleanup();
void mp_change_db(const std::string& db_path);

// Call this every frame. Checks whether current song ended or not
void mp_update();

// Add a song to the database. Right now, just add it to the database whether or not it exists already.
// Eventually, flow should be like preview song in ui -> add config -> choose to save to database
void mp_add_song(std::string&& song_path);
void mp_add_songs(std::vector<std::string>&& song_paths);
void mp_recursive_add_songs(const std::string& folder_path);
// When finished adding song, call this to destroy objects in mp_ctx.loaded_songs
void mp_reset_loaded_songs();

// Add a playlist to the database. Returns the id of the created playlist.
void mp_rename_playlist(PlaylistID playlist_id, const char* new_playlist_name);

// Immediately play a song
void mp_play_song(SongID song_id);

// Queue a song or mulitple songs
void mp_queue_song(SongID song_id);

// Will pause if song is playing and resume if song is not playing
void mp_pause_or_resume();

void mp_toggle_shuffle();
void mp_toggle_autoplay();
void mp_update_cursor();
// multiplied by the context volume
void mp_update_volume(float volume);

void mp_toggle_mute();

void mp_play_playlist(PlaylistID playlist_id);
void mp_play_album(AlbumID album_id);

// Search for songs based on query
SearchResult<Song> mp_search_songs(const char* search_query, int results_per_page, int page_num);
SearchResult<Album> mp_search_albums(const char* search_query, int results_per_page, int page_num);
SearchResult<Artist> mp_search_artists(const char* search_query, int results_per_page, int page_num);
SearchResult<Playlist> mp_search_playlists(const char* search_query, int results_per_page, int page_num);

LruCacheRef<Song>       mp_get_song(SongID song_id);
LruCacheRef<Album>      mp_get_album(AlbumID album_id);
LruCacheRef<Artist>     mp_get_artist(ArtistID artist_id);
LruCacheRef<Playlist>   mp_get_playlist(int playlist_id);

LruCacheRef<Artist>     mp_get_artist_from_song(SongID song_id);
LruCacheRef<Album>      mp_get_album_from_song(SongID song_id);
LruCacheRef<Artist>     mp_get_artist_from_album(AlbumID album_id);

std::vector<SongTrackID> mp_get_songs_from_album(AlbumID album_id);
std::vector<SongTrackID> mp_get_songs_from_playlist(int playlist_id);
std::vector<SongID> mp_get_songs_from_artist(ArtistID artist_id);
std::vector<AlbumID> mp_get_albums_from_artist(ArtistID artist_id);

SearchResult<Song> mp_get_paginated_songs_from_artist(ArtistID artist_id, int results_per_page, int page_num);

std::vector<LruCacheRef<Playlist>> mp_get_playlists();

void mp_get_song_lyrics(SongID song_id, char* lryics_buffer, size_t lyrics_buffer_size);
void mp_update_song(Song& song);
void mp_update_song_lyrics(SongID song_id, const char* lyrics);
void mp_update_song_art(SongID song_id, Art* art);
void mp_update_album(AlbumID album_id, const char* name);
void mp_update_artist(ArtistID artist_id, const char* name);
void mp_update_song_artist(SongID song_id, ArtistID artist_id);
void mp_update_song_album(SongID song_id, AlbumID album_id);
void mp_update_album_artist(AlbumID album_id, ArtistID artist_id);

LruCacheRef<Album> mp_create_album();
LruCacheRef<Artist> mp_create_artist();
LruCacheRef<Playlist> mp_create_playlist();

void mp_pin_song(SongID song_id);
void mp_pin_album(AlbumID album_id);
void mp_pin_artist(ArtistID artist_id);
void mp_pin_playlist(PlaylistID playlist_id);

void mp_add_song_to_playlist(SongID song_id, int playlist_id);
void mp_add_album_to_playlist(AlbumID album_id, int playlist_id);

Art mp_get_song_art(SongID song_id);
Art mp_get_album_art(AlbumID album_id);
Art mp_get_art(MPUID uid);
void mp_free_art(Art* art);

// Skip current song in queue
void mp_play_previous();
void mp_play_next();
void mp_go_backward_in_current_song(float seconds);
void mp_go_forward_in_current_song(float seconds);
void mp_queue_clear();

MPUID mp_get_uid(int id, MPUIDEnum type);
MPUID mp_get_song_uid(SongID song_id);
MPUID mp_get_playlist_uid(PlaylistID playlist_id);
MPUID mp_get_album_uid(PlaylistID playlist_id);
MPUID mp_get_artist_uid(ArtistID artist_id);
MPUIDEnum mp_get_uid_type(MPUID uid);
int mp_get_id(MPUID uid);

#endif
