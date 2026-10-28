#ifndef DB_H
#define DB_H

#include <string>
#include <cstdint>
#include <vector>

// SCHEMA

using GenericID = int;
using SongID = int;
using AlbumID = int;
using ArtistID = int;
using PlaylistID = int;
using MPUID = uint64_t;

#define INVALID_ID -1
#define INVALID_YEAR 0

enum MPUIDEnum : MPUID {
    MPUID_SONG,
    MPUID_ALBUM,
    MPUID_ARTIST,
    MPUID_PLAYLIST
};

enum LoopMode {
    LOOP_NONE,
    LOOP_GROUP,
    LOOP_TRACK
};

struct Song {
    std::string title;
    std::string path;
    SongID id;
    unsigned int year;
    float length;
    float volume;
};

template<typename T>
struct SearchResultID {
    std::vector<T> entries;
    int page_num;
    int num_pages;
    int results_per_page;
    int num_results;
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
    unsigned int year;
    float length;
};

struct Playlist {
    std::string name;
    PlaylistID id;
    float length;
};

struct PinID {
    MPUID uid;
    int order;
};

struct Art {
    void* sqlite_stmt;
    const uint8_t* data;
    size_t size;
};

MPUID db_get_uid(int id, MPUIDEnum type);
MPUID db_get_song_uid(SongID song_id);
MPUID db_get_playlist_uid(PlaylistID playlist_id);
MPUID db_get_album_uid(PlaylistID playlist_id);
MPUID db_get_artist_uid(ArtistID artist_id);
MPUIDEnum db_get_uid_type(MPUID uid);
int db_get_id(MPUID uid);

void db_init();
void db_cleanup();
void db_switch(const std::string& db_path);

// Create
void db_create_song(const char* title, const char* song_path, unsigned int year, double song_length, const uint8_t* art, int art_size);
void db_create_album(const char* name, unsigned int year, const uint8_t* art, int art_size);
void db_create_artist(const char* name);
void db_create_playlist(const char* name);
AlbumID db_create_default_album();
ArtistID db_create_default_artist();
PlaylistID db_create_default_playlist();
void db_create_artist_song(int artist_id, SongID song_id);
void db_create_album_song(AlbumID album_id, SongID song_id, int track);
void db_create_playlist_song(PlaylistID playlist_id, SongID song_id, int track);
void db_create_artist_album(int artist_id, AlbumID album_id);
bool db_create_pin(int id, MPUIDEnum type);

// Read
SongID db_get_song_id(const char* song_path);
AlbumID db_get_album_id(const char* name);
ArtistID db_get_artist_id(const char* name);
PlaylistID db_get_playlist_id(const char* name);
Song db_get_song_info(SongID song_id);
Art db_get_song_art(SongID song_id);
Album db_get_album_info(AlbumID album_id);
Art db_get_album_art(AlbumID album_id);
void db_free_art(Art* art);
Artist db_get_artist_info(int artist_id);
Playlist db_get_playlist_info(PlaylistID playlist_id);
AlbumID db_get_album_from_song(SongID song_id);
ArtistID db_get_artist_from_song(SongID song_id);
ArtistID db_get_artist_from_album(AlbumID album_id);
std::vector<SongTrackID> db_get_songs_from_album(AlbumID album_id);
std::vector<SongTrackID> db_get_songs_from_playlist(PlaylistID playlist_id);
std::vector<SongID> db_get_songs_from_artist(int artist_id);
std::vector<AlbumID> db_get_albums_from_artist(int artist_id);
std::vector<PlaylistID> db_get_playlists();
std::vector<PinID> db_get_pins();
bool db_exists_artist_album(ArtistID artist_id, AlbumID album_id);
int db_get_random_song();
void db_get_song_lyrics(SongID song_id, char* lyrics_buffer, size_t lyrics_buffer_size);
int db_get_num_songs();
int db_get_num_albums();
int db_get_num_artists();
int db_get_num_playlists();

// Search - return num results + vector of ids for current page
SearchResultID<SongID> db_search_songs(const char* search_query, int results_per_page, int page_num);
SearchResultID<AlbumID> db_search_albums(const char* search_query, int results_per_page, int page_num);
SearchResultID<ArtistID> db_search_artists(const char* search_query, int results_per_page, int page_num);
SearchResultID<PlaylistID> db_search_playlists(const char* search_query, int results_per_page, int page_num);
SearchResultID<SongID> db_search_songs_from_artist(ArtistID artist_id, int results_per_page, int page_num);

// Update
void db_update_song(Song& song);
void db_update_song_art(SongID song_id, Art* art);
void db_update_song_lyrics(SongID song_id, const char* lyrics);
void db_update_album(AlbumID album_id, const char* new_name);
void db_update_artist(ArtistID artist_id, const char* new_name);
void db_update_song_artist(SongID song_id, const char* artist);
void db_update_playlist(PlaylistID playlist_id, const char* new_name);
void db_update_album_length(AlbumID album_id);
void db_update_song_volume(SongID song_id, float volume);

// Delete
void db_delete_artist(int artist_id);
void db_delete_album(AlbumID album_id);
void db_delete_song(SongID song_id);

#endif
