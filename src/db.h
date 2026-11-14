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
#define INVALID_UID -1ULL
#define INVALID_TRACK 0
#define INVALID_YEAR 0

#define ART_WIDTH 256
#define ART_CHANNELS 4
#define ART_SIZE (ART_WIDTH * ART_WIDTH * ART_CHANNELS * sizeof(uint8_t))

typedef struct sqlite3 sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;

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
    std::string name;
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
    uint64_t last_accessed;
};

class Art {
public:
    uint8_t* data;

    Art() = default;
    Art(uint8_t* data)
        : data{data}
    {
    }

    ~Art()
    {
        free(data);
    }

    Art(Art& other) = delete;
    Art& operator=(Art& other) = delete;

    Art(Art&& other)
    {
        this->data = other.data;
        other.data = nullptr;
    }

    Art& operator=(std::nullptr_t)
    {
        clear();
        this->data = nullptr;
        return *this;
    }

    Art& operator=(Art&& other)
    {
        clear();
        this->data = other.data;
        other.data = nullptr;
        return *this;
    }

    void clear()
    {
        free(this->data);
        this->data = nullptr;
    }

    bool operator==(std::nullptr_t)
    {
        return data == nullptr;
    }
};

class DBConn {
public:
    DBConn() = default;
    DBConn(const char* db_path);
    ~DBConn();

    bool open(const char* db_path);
    bool close();

    DBConn(DBConn& other) = delete;
    DBConn(DBConn&& other) = delete;
    DBConn& operator=(const DBConn& other) = delete;
    DBConn& operator=(const DBConn&& other) = delete;

    // Transaction
    void begin_transaction();
    void commit_transaction();

    // Create
    SongID     create_default_song();
    AlbumID    create_default_album();
    ArtistID   create_default_artist();
    PlaylistID create_default_playlist();
    SongID     create_song(Song& song);
    AlbumID    create_album(Album& album);
    ArtistID   create_artist(Artist& artist);
    PlaylistID create_playlist(Playlist& playlist);
    void       create_artist_song(ArtistID artist_id, SongID song_id);
    void       create_album_song(AlbumID album_id, SongID song_id, int track);
    void       create_playlist_song(PlaylistID playlist_id, SongID song_id, int track);
    void       create_artist_album(ArtistID artist_id, AlbumID album_id);
    PinID      create_pin(MPUID uid);

    // Read
    SongID     get_song_id(const char* song_path);
    Song       get_song_info(SongID song_id);
    Art        get_song_art(SongID song_id);
    int        get_song_track(SongID song_id);
    void       get_song_lyrics(SongID song_id, char* lyrics_buffer, size_t lyrics_buffer_size);
    SongID     get_random_song();

    AlbumID    get_album_id(const char* name);
    Album      get_album_info(AlbumID album_id);
    Art        get_album_art(AlbumID album_id);
    AlbumID    get_album_from_song(SongID song_id);

    ArtistID   get_artist_id(const char* name);
    Artist     get_artist_info(ArtistID artist_id);
    ArtistID   get_artist_from_song(SongID song_id);
    ArtistID   get_artist_from_album(AlbumID album_id);
    Art        get_artist_art(ArtistID artist_id);

    PlaylistID get_playlist_id(const char* name);
    Playlist   get_playlist_info(PlaylistID playlist_id);
    Art        get_playlist_art(PlaylistID playlist_id);

    std::vector<SongTrackID> get_songs_from_album(AlbumID album_id);
    std::vector<SongTrackID> get_songs_from_playlist(PlaylistID playlist_id);
    std::vector<SongID>      get_songs_from_artist(ArtistID artist_id);
    std::vector<AlbumID>     get_albums_from_artist(ArtistID artist_id);
    std::vector<PlaylistID>  get_playlists();
    std::vector<PinID>       get_pins();

    int  get_num_songs();
    int  get_num_albums();
    int  get_num_artists();
    int  get_num_playlists();
    bool artist_album_exists(ArtistID artist_id, AlbumID album_id);

    // Search - return num results + vector of ids for current page
    SearchResultID<SongID>     search_songs(const char* search_query, int results_per_page, int page_num);
    SearchResultID<AlbumID>    search_albums(const char* search_query, int results_per_page, int page_num);
    SearchResultID<ArtistID>   search_artists(const char* search_query, int results_per_page, int page_num);
    SearchResultID<PlaylistID> search_playlists(const char* search_query, int results_per_page, int page_num);
    SearchResultID<SongID>     search_songs_from_artist(ArtistID artist_id, int results_per_page, int page_num);
    SearchResultID<AlbumID>    search_albums_from_artist(ArtistID artist_id, int results_per_page, int page_num);

    // Update
    void update_song(Song& song);
    void update_song_art(SongID song_id, Art& art);
    void update_song_lyrics(SongID song_id, const char* lyrics);
    void update_song_album(SongID song_id, AlbumID album_id, int track);
    void update_song_artist(SongID song_id, ArtistID artist_id);
    void update_song_volume(SongID song_id, float volume);

    void update_album(Album& album);
    void update_album_art(AlbumID album_id, Art& art);
    void update_album_artist(AlbumID album_id, ArtistID artist_id);
    void update_album_length(AlbumID album_id);
    void update_album_songs_year(AlbumID album_id, int year);
    void update_album_songs_art(AlbumID album_id);

    void update_artist(Artist& artist);
    void update_artist_art(ArtistID artist_id, Art& art);
    void merge_albums(AlbumID dst_album_id, AlbumID src_album_id);
    void merge_artists(ArtistID dst_artist_id, ArtistID src_artist_id);

    void update_playlist(Playlist& playlist);
    void update_playlist_art(PlaylistID playlist_id, Art& art);

    void update_pin(MPUID uid, uint64_t last_accessed);

    // Delete
    void delete_song(SongID song_id);
    void delete_album(AlbumID album_id);
    void delete_artist(ArtistID artist_id);
    void delete_playlist(PlaylistID playlist_id);
    void delete_pin(MPUID uid);

private:
    sqlite3* conn;
};

// UID
MPUID     db_get_uid(int id, MPUIDEnum type);
MPUID     db_get_song_uid(SongID song_id);
MPUID     db_get_playlist_uid(PlaylistID playlist_id);
MPUID     db_get_album_uid(PlaylistID playlist_id);
MPUID     db_get_artist_uid(ArtistID artist_id);
MPUIDEnum db_get_uid_type(MPUID uid);
int       db_get_id(MPUID uid);

#endif
