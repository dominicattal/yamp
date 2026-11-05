#include "db.h"
#include "config.h"
#include <chrono>
#include <sqlite3.h>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>

#define MPUID_SHIFT 62
#define MPUID_ID_MASK ((1ULL<<MPUID_SHIFT)-1)
#define MPUID_TYPE_MASK (3ULL<<MPUID_SHIFT)

const char* schema = R"(
    CREATE TABLE IF NOT EXISTS Songs (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        name VARCHAR(256),
        path VARCHAR(256),
        cover BLOB,
        length REAL,
        volume REAL DEFAULT 1.0,
        year INT,
        lyrics TEXT
    );

    CREATE TABLE IF NOT EXISTS Albums (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        name VARCHAR(256),
        cover BLOB,
        year INT,
        length REAL
    );

    CREATE TABLE IF NOT EXISTS Artists (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        name VARCHAR(256),
        picture BLOB
    );

    CREATE TABLE IF NOT EXISTS Playlists (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        name VARCHAR(256),
        cover BLOB,
        length REAL
    );

    CREATE TABLE IF NOT EXISTS ArtistAlbum (
        artist_id INTEGER,
        album_id INTEGER PRIMARY KEY
    );

    CREATE TABLE IF NOT EXISTS ArtistSong (
        artist_id INTEGER,
        song_id INTEGER PRIMARY KEY
    );

    CREATE TABLE IF NOT EXISTS AlbumSong (
        album_id INTEGER,
        song_id INTEGER PRIMARY KEY,
        track INTEGER
    );

    CREATE TABLE IF NOT EXISTS PlaylistSong (
        playlist_id INTEGER,
        song_id INTEGER,
        track INTEGER,
        PRIMARY KEY (playlist_id, song_id)
    );

    CREATE TABLE IF NOT EXISTS Pins (
        uid INTEGER PRIMARY KEY,
        last_accessed INTEGER
    );
)";

MPUID db_get_song_uid(SongID song_id)
{
    return (MPUID_SONG<<MPUID_SHIFT) | static_cast<MPUID>(song_id);
}

MPUID db_get_playlist_uid(PlaylistID playlist_id)
{
    return (MPUID_PLAYLIST<<MPUID_SHIFT) | static_cast<MPUID>(playlist_id);
}

MPUID db_get_album_uid(AlbumID album_id)
{
    return (MPUID_ALBUM<<MPUID_SHIFT) | static_cast<MPUID>(album_id);
}

MPUID db_get_artist_uid(ArtistID artist_id)
{
    return (MPUID_ARTIST<<MPUID_SHIFT) | static_cast<MPUID>(artist_id);
}

MPUID db_get_uid(int id, MPUIDEnum type)
{
    return (type<<MPUID_SHIFT) | static_cast<MPUID>(id);
}

MPUIDEnum db_get_uid_type(MPUID uid)
{
    return static_cast<MPUIDEnum>((uid & MPUID_TYPE_MASK)>>MPUID_SHIFT);
}

int db_get_id(MPUID uid)
{
    return uid & MPUID_ID_MASK;
}

Art::~Art()
{
    sqlite3_finalize(this->stmt);
}

void Art::clear()
{
    sqlite3_finalize(this->stmt);
    this->stmt = nullptr;
    this->data = nullptr;
    this->size = 0;
}

DBConn::DBConn()
{
    sqlite3_open("build/yamp.db", &this->conn);
    char* error_msg{};
    sqlite3_exec(DBConn::conn, schema, NULL, NULL, &error_msg);
    if (error_msg != NULL) {
        SPDLOG_INFO("Failed to create DB connection: SQLite3 error - {}", error_msg);
        sqlite3_free(error_msg);
        return;
    }
    SPDLOG_INFO("Successfully created DB connection {}", static_cast<void*>(this->conn));
}

DBConn::~DBConn()
{
    int res = sqlite3_close(this->conn);
    if (res == SQLITE_BUSY)
        SPDLOG_WARN("Failed to close DB connection {}: SQLITE_BUSY", static_cast<void*>(this->conn));
    else
        SPDLOG_INFO("Successfully closed DB connection {}", static_cast<void*>(this->conn));
}

void DBConn::begin_transaction()
{
    sqlite3_exec(this->conn, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);
}

void DBConn::commit_transaction()
{
    sqlite3_exec(this->conn, "COMMIT TRANSACTION", nullptr, nullptr, nullptr);
}

SongID DBConn::get_song_id(const char* song_path)
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT id FROM Songs WHERE path=?1";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, song_path, -1, SQLITE_STATIC);
    int res = sqlite3_step(stmt);
    SongID song_id = (res == SQLITE_ROW) ? sqlite3_column_int(stmt, 0) : INVALID_ID;
    sqlite3_finalize(stmt);
    return song_id;
}

Song DBConn::get_song_info(SongID song_id)
{
    Song song{};
    sqlite3_stmt* stmt;
    const char* query = "SELECT name, path, year, length, volume FROM Songs WHERE id=?1";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, song_id);
    [[maybe_unused]] int res = sqlite3_step(stmt);
    assert(res == SQLITE_ROW);
    song.id = song_id;
    song.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    song.path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    song.year = sqlite3_column_int(stmt, 2);
    song.length = static_cast<float>(sqlite3_column_double(stmt, 3));
    song.volume = sqlite3_column_double(stmt, 4);
    sqlite3_finalize(stmt);
    return song;
}

Art DBConn::get_song_art(SongID song_id)
{
    sqlite3_stmt* stmt{};
    const char* query = "SELECT cover FROM Songs WHERE id=?1";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, song_id);
    int res = sqlite3_step(stmt);
    if (res != SQLITE_ROW)
        return {stmt, NULL, 0};
    const unsigned char* blob = static_cast<const unsigned char*>(sqlite3_column_blob(stmt, 0));
    int blob_bytes = sqlite3_column_bytes(stmt, 0);
    return Art{stmt, blob, static_cast<size_t>(blob_bytes)};
}

Album DBConn::get_album_info(AlbumID album_id)
{
    Album album{};
    sqlite3_stmt* stmt;
    const char* query = "SELECT name, length, year FROM Albums WHERE id=?1";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, album_id);
    [[maybe_unused]] int res = sqlite3_step(stmt);
    assert(res == SQLITE_ROW);
    album.id = album_id;
    const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    if (name != nullptr)
        album.name = name;
    album.length = sqlite3_column_double(stmt, 1);
    album.year = sqlite3_column_int(stmt, 2);
    sqlite3_finalize(stmt);
    return album;
}

Art DBConn::get_album_art(AlbumID album_id)
{
    sqlite3_stmt* stmt{};
    const char* query = "SELECT cover FROM Albums WHERE id=?1";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, album_id);
    int res = sqlite3_step(stmt);
    if (res != SQLITE_ROW)
        return Art{stmt, NULL, 0};
    const unsigned char* blob = static_cast<const unsigned char*>(sqlite3_column_blob(stmt, 0));
    int blob_bytes = sqlite3_column_bytes(stmt, 0);
    return Art{stmt, blob, static_cast<size_t>(blob_bytes)};
}

Art DBConn::get_artist_art(ArtistID artist_id)
{
    sqlite3_stmt* stmt{};
    const char* query = "SELECT picture FROM Artists WHERE id=?1";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, artist_id);
    int res = sqlite3_step(stmt);
    if (res != SQLITE_ROW)
        return Art{stmt, NULL, 0};
    const unsigned char* blob = static_cast<const unsigned char*>(sqlite3_column_blob(stmt, 0));
    int blob_bytes = sqlite3_column_bytes(stmt, 0);
    return Art{stmt, blob, static_cast<size_t>(blob_bytes)};
}

Art DBConn::get_playlist_art(PlaylistID playlist_id)
{
    sqlite3_stmt* stmt{};
    const char* query = "SELECT cover FROM Playlists WHERE id=?1";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, playlist_id);
    int res = sqlite3_step(stmt);
    if (res != SQLITE_ROW)
        return Art{stmt, NULL, 0};
    const unsigned char* blob = static_cast<const unsigned char*>(sqlite3_column_blob(stmt, 0));
    int blob_bytes = sqlite3_column_bytes(stmt, 0);
    return Art{stmt, blob, static_cast<size_t>(blob_bytes)};
}

Artist DBConn::get_artist_info(ArtistID artist_id)
{
    Artist artist{};
    artist.id = artist_id;
    
    sqlite3_stmt* stmt;
    const char* query = "SELECT name FROM Artists WHERE id=?1";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, artist_id);
    [[maybe_unused]] int res = sqlite3_step(stmt);
    assert(res == SQLITE_ROW);
    const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    if (name != nullptr)
        artist.name = name;

    return artist;
}

Playlist DBConn::get_playlist_info(PlaylistID playlist_id)
{
    Playlist playlist{};
    playlist.id = playlist_id;

    sqlite3_stmt* stmt;
    const char* query = "SELECT name, length FROM Playlists WHERE id=?1";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, playlist_id);
    [[maybe_unused]] int res = sqlite3_step(stmt);
    assert(res == SQLITE_ROW);
    playlist.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    playlist.length = sqlite3_column_double(stmt, 1);
    sqlite3_finalize(stmt);
    return playlist;
}

AlbumID DBConn::get_album_from_song(SongID song_id)
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT album_id FROM AlbumSong WHERE song_id=?1";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, song_id);
    int res = sqlite3_step(stmt);
    AlbumID album_id = (res == SQLITE_ROW) ? sqlite3_column_int(stmt, 0) : INVALID_ID;
    sqlite3_finalize(stmt);
    return album_id;
}

ArtistID DBConn::get_artist_from_song(SongID song_id)
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT artist_id FROM ArtistSong WHERE song_id=?1";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, song_id);
    int res = sqlite3_step(stmt);
    ArtistID artist_id = (res == SQLITE_ROW) ? sqlite3_column_int(stmt, 0) : INVALID_ID;
    sqlite3_finalize(stmt);
    return artist_id;
}

ArtistID DBConn::get_artist_from_album(AlbumID album_id)
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT artist_id FROM ArtistAlbum WHERE album_id=?1";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, album_id);
    int res = sqlite3_step(stmt);
    ArtistID artist_id = (res == SQLITE_ROW) ? sqlite3_column_int(stmt, 0) : INVALID_ID;
    sqlite3_finalize(stmt);
    return artist_id;
}

int DBConn::get_song_track(SongID song_id)
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT track FROM AlbumSong WHERE song_id=?1";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, song_id);
    int res = sqlite3_step(stmt);
    int track = (res == SQLITE_ROW) ? sqlite3_column_int(stmt, 0) : INVALID_TRACK;
    sqlite3_finalize(stmt);
    return track;
}

std::vector<SongTrackID> DBConn::get_songs_from_album(AlbumID album_id)
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT song_id, track FROM AlbumSong WHERE album_id=?1 ORDER BY track";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, album_id);

    std::vector<SongTrackID> songs{};
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        SongID song_id = sqlite3_column_int(stmt, 0);
        int track = sqlite3_column_int(stmt, 1);
        songs.emplace_back(song_id, track);
    }

    sqlite3_finalize(stmt);
    return songs;
}

std::vector<SongTrackID> DBConn::get_songs_from_playlist(PlaylistID playlist_id)
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT song_id, track FROM PlaylistSong WHERE playlist_id=?1 ORDER BY track";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, playlist_id);

    std::vector<SongTrackID> songs{};
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        SongID song_id = sqlite3_column_int(stmt, 0);
        int track = sqlite3_column_int(stmt, 1);
        songs.emplace_back(song_id, track);
    }

    sqlite3_finalize(stmt);
    return songs;
}

std::vector<SongID> DBConn::get_songs_from_artist(ArtistID artist_id)
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT song_id FROM ArtistSong WHERE artist_id=?1";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, artist_id);

    std::vector<SongID> songs{};
    while (sqlite3_step(stmt) == SQLITE_ROW)
        songs.push_back(sqlite3_column_int(stmt, 0));

    sqlite3_finalize(stmt);
    return songs;
}

std::vector<AlbumID> DBConn::get_albums_from_artist(ArtistID artist_id)
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT album_id FROM ArtistAlbum WHERE artist_id=?1";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, artist_id);

    std::vector<AlbumID> albums{};
    while (sqlite3_step(stmt) == SQLITE_ROW)
        albums.push_back(sqlite3_column_int(stmt, 0));

    sqlite3_finalize(stmt);
    return albums;
}

std::vector<PlaylistID> DBConn::get_playlists()
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT id FROM Playlists";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 

    std::vector<PlaylistID> playlists{};
    while (sqlite3_step(stmt) == SQLITE_ROW)
        playlists.push_back(sqlite3_column_int(stmt, 0));

    sqlite3_finalize(stmt);
    return playlists;
}

ArtistID DBConn::get_artist_id(const char* name)
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT id FROM Artists WHERE name=?1";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    int res = sqlite3_step(stmt);
    ArtistID artist_id = (res == SQLITE_ROW) ? sqlite3_column_int(stmt, 0) : INVALID_ID;
    sqlite3_finalize(stmt);
    return artist_id;
}

ArtistID DBConn::create_artist(Artist& artist)
{
    sqlite3_stmt* stmt;
    const char* query = "INSERT INTO Artists (name) VALUES (?1)";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, artist.name.c_str(), -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return sqlite3_last_insert_rowid(this->conn);
}

ArtistID DBConn::create_default_artist()
{
    const char* query = "INSERT INTO Artists DEFAULT VALUES";
    sqlite3_exec(this->conn, query, nullptr, nullptr, nullptr);
    return sqlite3_last_insert_rowid(this->conn);
}

PlaylistID DBConn::create_default_playlist()
{
    const char* query = "INSERT INTO Playlists DEFAULT VALUES";
    sqlite3_exec(this->conn, query, nullptr, nullptr, nullptr);
    return sqlite3_last_insert_rowid(this->conn);
}

SongID DBConn::create_default_song()
{
    const char* query = "INSERT INTO Songs DEFAULT VALUES";
    sqlite3_exec(this->conn, query, nullptr, nullptr, nullptr);
    return sqlite3_last_insert_rowid(this->conn);
}

AlbumID DBConn::create_default_album()
{
    const char* query = "INSERT INTO Albums DEFAULT VALUES";
    sqlite3_exec(this->conn, query, nullptr, nullptr, nullptr);
    return sqlite3_last_insert_rowid(this->conn);
}

SongID DBConn::create_song(Song& song)
{
    sqlite3_stmt* stmt;
    const char* query = "INSERT INTO Songs (name, path, year, length) VALUES (?1, ?2, ?3, ?4);";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, song.name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, song.path.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, song.year);
    sqlite3_bind_double(stmt, 4, song.length);
    int res = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (res == SQLITE_CONSTRAINT)
        SPDLOG_WARN("song name={} path={} exists in db already", song.name, song.path);
    return sqlite3_last_insert_rowid(this->conn);
}

SearchResultID<SongID> DBConn::search_songs(const char* search_query, int results_per_page, int page_num)
{
    SearchResultID<SongID> search_result_id{};
    sqlite3_stmt* stmt;
    const char* query;
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%%%s%%", search_query);

    search_result_id.page_num = page_num;
    search_result_id.results_per_page = results_per_page;

    this->begin_transaction();
    query = "SELECT COUNT(id) FROM Songs WHERE name LIKE ?1"; 
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, buffer, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    search_result_id.num_results = sqlite3_column_int(stmt, 0);
    search_result_id.num_pages = (search_result_id.num_results - 1) / search_result_id.results_per_page + 1;
    sqlite3_finalize(stmt);

    query = "SELECT id FROM Songs WHERE name LIKE ?1 LIMIT ?2 OFFSET ?3";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, buffer, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, results_per_page);
    sqlite3_bind_int(stmt, 3, results_per_page * (page_num - 1));
    while (sqlite3_step(stmt) == SQLITE_ROW)
        search_result_id.entries.push_back(sqlite3_column_int(stmt, 0));
    sqlite3_finalize(stmt);
    this->commit_transaction();

    return search_result_id;
}

SearchResultID<AlbumID> DBConn::search_albums(const char* search_query, int results_per_page, int page_num)
{
    SearchResultID<AlbumID> search_result_id{};
    sqlite3_stmt* stmt;
    const char* query;
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%%%s%%", search_query);

    search_result_id.page_num = page_num;
    search_result_id.results_per_page = results_per_page;

    this->begin_transaction();
    query = "SELECT COUNT(id) FROM Albums WHERE name LIKE ?1"; 
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, buffer, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    search_result_id.num_results = sqlite3_column_int(stmt, 0);
    search_result_id.num_pages = (search_result_id.num_results - 1) / search_result_id.results_per_page + 1;
    sqlite3_finalize(stmt);

    query = "SELECT id FROM Albums WHERE name LIKE ?1 LIMIT ?2 OFFSET ?3";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, buffer, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, results_per_page);
    sqlite3_bind_int(stmt, 3, results_per_page * (page_num - 1));
    while (sqlite3_step(stmt) == SQLITE_ROW)
        search_result_id.entries.push_back(sqlite3_column_int(stmt, 0));
    sqlite3_finalize(stmt);
    this->commit_transaction();

    return search_result_id;
}

SearchResultID<ArtistID> DBConn::search_artists(const char* search_query, int results_per_page, int page_num)
{
    SearchResultID<ArtistID> search_result_id{};
    sqlite3_stmt* stmt;
    const char* query;
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%%%s%%", search_query);

    search_result_id.page_num = page_num;
    search_result_id.results_per_page = results_per_page;

    this->begin_transaction();
    query = "SELECT COUNT(id) FROM Artists WHERE name LIKE ?1"; 
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, buffer, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    search_result_id.num_results = sqlite3_column_int(stmt, 0);
    search_result_id.num_pages = (search_result_id.num_results - 1) / search_result_id.results_per_page + 1;
    sqlite3_finalize(stmt);

    query = "SELECT id FROM Artists WHERE name LIKE ?1 LIMIT ?2 OFFSET ?3";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, buffer, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, results_per_page);
    sqlite3_bind_int(stmt, 3, results_per_page * (page_num - 1));
    while (sqlite3_step(stmt) == SQLITE_ROW)
        search_result_id.entries.push_back(sqlite3_column_int(stmt, 0));
    sqlite3_finalize(stmt);
    this->commit_transaction();

    return search_result_id;
}

SearchResultID<PlaylistID> DBConn::search_playlists(const char* search_query, int results_per_page, int page_num)
{
    SearchResultID<PlaylistID> search_result_id{};
    sqlite3_stmt* stmt;
    const char* query;
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%%%s%%", search_query);

    search_result_id.page_num = page_num;
    search_result_id.results_per_page = results_per_page;

    this->begin_transaction();
    query = "SELECT COUNT(id) FROM Playlists WHERE name LIKE ?1"; 
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, buffer, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    search_result_id.num_results = sqlite3_column_int(stmt, 0);
    search_result_id.num_pages = (search_result_id.num_results - 1) / search_result_id.results_per_page + 1;
    sqlite3_finalize(stmt);

    query = "SELECT id FROM Playlists WHERE name LIKE ?1 LIMIT ?2 OFFSET ?3";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, buffer, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, results_per_page);
    sqlite3_bind_int(stmt, 3, results_per_page * (page_num - 1));
    while (sqlite3_step(stmt) == SQLITE_ROW)
        search_result_id.entries.push_back(sqlite3_column_int(stmt, 0));
    sqlite3_finalize(stmt);
    this->commit_transaction();

    return search_result_id;
}

SearchResultID<SongID> DBConn::search_songs_from_artist(ArtistID artist_id, int results_per_page, int page_num)
{
    SearchResultID<SongID> search_result_id{};
    sqlite3_stmt* stmt;
    const char* query;

    search_result_id.page_num = page_num;
    search_result_id.results_per_page = results_per_page;

    this->begin_transaction();
    query = "SELECT COUNT(song_id) FROM ArtistSong WHERE artist_id=?1"; 
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, artist_id);
    sqlite3_step(stmt);
    search_result_id.num_results = sqlite3_column_int(stmt, 0);
    search_result_id.num_pages = (search_result_id.num_results - 1) / search_result_id.results_per_page + 1;
    sqlite3_finalize(stmt);

    query = "SELECT song_id FROM ArtistSong WHERE artist_id=?1 LIMIT ?2 OFFSET ?3";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, artist_id);
    sqlite3_bind_int(stmt, 2, results_per_page);
    sqlite3_bind_int(stmt, 3, results_per_page * (page_num - 1));
    while (sqlite3_step(stmt) == SQLITE_ROW)
        search_result_id.entries.push_back(sqlite3_column_int(stmt, 0));
    sqlite3_finalize(stmt);
    this->commit_transaction();

    return search_result_id;
}

SearchResultID<AlbumID> DBConn::search_albums_from_artist(ArtistID artist_id, int results_per_page, int page_num)
{
    SearchResultID<AlbumID> search_result_id{};
    sqlite3_stmt* stmt;
    const char* query;

    search_result_id.page_num = page_num;
    search_result_id.results_per_page = results_per_page;

    this->begin_transaction();
    query = "SELECT COUNT(album_id) FROM ArtistAlbum WHERE artist_id=?1"; 
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, artist_id);
    sqlite3_step(stmt);
    search_result_id.num_results = sqlite3_column_int(stmt, 0);
    search_result_id.num_pages = (search_result_id.num_results - 1) / search_result_id.results_per_page + 1;
    sqlite3_finalize(stmt);

    query = "SELECT album_id FROM ArtistAlbum WHERE artist_id=?1 LIMIT ?2 OFFSET ?3";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, artist_id);
    sqlite3_bind_int(stmt, 2, results_per_page);
    sqlite3_bind_int(stmt, 3, results_per_page * (page_num - 1));
    while (sqlite3_step(stmt) == SQLITE_ROW)
        search_result_id.entries.push_back(sqlite3_column_int(stmt, 0));
    sqlite3_finalize(stmt);
    this->commit_transaction();

    return search_result_id;
}

void DBConn::update_song(Song& song)
{
    sqlite3_stmt* stmt;
    const char* query = "UPDATE Songs SET name=?1, year=?2, volume=?3, path=?4, length=?5 WHERE id=?6";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, song.name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, song.year);
    sqlite3_bind_double(stmt, 3, song.volume);
    sqlite3_bind_text(stmt, 4, song.path.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 5, song.length);
    sqlite3_bind_int(stmt, 6, song.id);
    [[maybe_unused]] int res = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void DBConn::update_song_art(SongID song_id, Art& art)
{
    sqlite3_stmt* stmt;
    const char* query = "UPDATE Songs SET cover=?1 WHERE id=?2";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_blob(stmt, 1, art.data, art.size, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, song_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void DBConn::update_song_lyrics(SongID song_id, const char* lyrics)
{
    sqlite3_stmt* stmt;
    const char* query = "UPDATE Songs SET lyrics=?1 WHERE id=?2";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, lyrics, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, song_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void DBConn::update_song_album(SongID song_id, AlbumID album_id, int track)
{
    sqlite3_stmt* stmt;
    const char* query;
    if (album_id == INVALID_ID)
        query = "DELETE FROM AlbumSong WHERE song_id=?1";
    else
        query = "INSERT INTO AlbumSong (song_id, album_id, track) VALUES (?1, ?2, ?3) ON CONFLICT (song_id) DO UPDATE SET album_id=?2, track=?3";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, song_id);
    sqlite3_bind_int(stmt, 2, album_id);
    sqlite3_bind_int(stmt, 3, track);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void DBConn::update_song_artist(SongID song_id, AlbumID artist_id)
{
    sqlite3_stmt* stmt;
    const char* query;
    if (artist_id == INVALID_ID)
        query = "DELETE FROM ArtistSong WHERE song_id=?1";
    else
        query = "INSERT INTO ArtistSong (song_id, artist_id) VALUES (?1, ?2) ON CONFLICT (song_id) DO UPDATE SET artist_id=?2";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, song_id);
    sqlite3_bind_int(stmt, 2, artist_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void DBConn::update_song_volume(SongID song_id, float volume)
{
    sqlite3_stmt* stmt;
    const char* query = "UPDATE Songs SET volume=?1 WHERE id=?2";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_double(stmt, 1, volume);
    sqlite3_bind_int(stmt, 2, song_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void DBConn::update_album(Album& album)
{
    sqlite3_stmt* stmt;
    const char* query = "UPDATE Albums SET name=?1, year=?2 WHERE id=?3";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, album.name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, album.year);
    sqlite3_bind_int(stmt, 3, album.id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void DBConn::update_album_art(AlbumID album_id, Art& art)
{
    sqlite3_stmt* stmt;
    const char* query = "UPDATE Albums SET cover=?1 WHERE id=?2";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_blob(stmt, 1, art.data, art.size, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, album_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void DBConn::update_album_artist(AlbumID album_id, ArtistID artist_id)
{
    sqlite3_stmt* stmt;
    const char* query;
    if (artist_id == INVALID_ID)
        query = "DELETE FROM ArtistAlbum WHERE album_id=?1";
    else
        query = "INSERT INTO ArtistAlbum (album_id, artist_id) VALUES (?1, ?2) ON CONFLICT (album_id) DO UPDATE SET artist_id=?2";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, album_id);
    sqlite3_bind_int(stmt, 2, artist_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void DBConn::update_album_length(AlbumID album_id)
{
    sqlite3_stmt* stmt;
    const char* query = "UPDATE Albums SET length=(SELECT SUM(length) FROM Songs S JOIN AlbumSong A ON S.id=A.song_id WHERE A.album_id=?1) WHERE id=?1";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, album_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}


void DBConn::update_artist(Artist& artist)
{
    sqlite3_stmt* stmt;
    const char* query = "UPDATE Artists SET name=?1 WHERE id=?2";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, artist.name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, artist.id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void DBConn::update_artist_art(ArtistID artist_id, Art& art)
{
    sqlite3_stmt* stmt;
    const char* query = "UPDATE Artists SET picture=?1 WHERE id=?2";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_blob(stmt, 1, art.data, art.size, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, artist_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void DBConn::merge_artists(ArtistID dst_artist_id, ArtistID src_artist_id)
{
    this->begin_transaction();
    sqlite3_stmt* stmt;
    const char* query = "UPDATE ArtistAlbum SET artist_id=?1 WHERE artist_id=?2";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, dst_artist_id);
    sqlite3_bind_int(stmt, 2, src_artist_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    query = "UPDATE ArtistSong SET artist_id=?1 WHERE artist_id=?2";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, dst_artist_id);
    sqlite3_bind_int(stmt, 2, src_artist_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    query = "DELETE FROM Artists WHERE id=?1";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, src_artist_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    this->commit_transaction();
}

void DBConn::delete_artist(ArtistID artist_id)
{
    sqlite3_stmt* stmt;
    const char* query = "DELETE FROM Artists WHERE id=?1";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, artist_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void DBConn::create_artist_song(ArtistID artist_id, SongID song_id)
{
    sqlite3_stmt* stmt;
    const char* query = "INSERT INTO ArtistSong (artist_id, song_id) VALUES (?1, ?2)";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, artist_id);
    sqlite3_bind_int(stmt, 2, song_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void DBConn::delete_album(AlbumID album_id)
{
    sqlite3_stmt* stmt;
    const char* query = "DELETE FROM Albums WHERE album_id=?1";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, album_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void DBConn::delete_song(SongID song_id)
{
    sqlite3_stmt* stmt;
    const char* query = "DELETE FROM Songs WHERE song_id=?1";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, song_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void DBConn::delete_pin(MPUID uid)
{
    sqlite3_stmt* stmt;
    const char* query = "DELETE FROM Pins WHERE uid=?1";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_int64(stmt, 1, uid);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

AlbumID DBConn::get_album_id(const char* name)
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT id FROM Albums WHERE name=?1";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    int res = sqlite3_step(stmt);
    AlbumID album_id = (res == SQLITE_ROW) ? sqlite3_column_int(stmt, 0) : INVALID_ID;
    sqlite3_finalize(stmt);
    return album_id;
}

AlbumID DBConn::create_album(Album& album)
{
    sqlite3_stmt* stmt;
    const char* query = "INSERT INTO Albums (name, year) VALUES (?1, ?2)";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, album.name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, album.year);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return sqlite3_last_insert_rowid(this->conn);
}

void DBConn::create_album_song(AlbumID album_id, SongID song_id, int track)
{
    sqlite3_stmt* stmt;
    const char* query = "INSERT INTO AlbumSong (album_id, song_id, track) VALUES (?1, ?2, ?3)";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, album_id);
    sqlite3_bind_int(stmt, 2, song_id);
    sqlite3_bind_int(stmt, 3, track);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    DBConn::update_album_length(album_id);
}

PlaylistID DBConn::get_playlist_id(const char* name)
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT id FROM Playlists WHERE name=?1";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    int res = sqlite3_step(stmt);
    PlaylistID playlist_id = (res == SQLITE_ROW) ? sqlite3_column_int(stmt, 0) : INVALID_ID;
    sqlite3_finalize(stmt);
    return playlist_id;
}

PlaylistID DBConn::create_playlist(Playlist& playlist)
{
    sqlite3_stmt* stmt;
    const char* query = "INSERT INTO Playlists (name) VALUES (?1)";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, playlist.name.c_str(), -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return sqlite3_last_insert_rowid(this->conn);
}

void DBConn::update_playlist(Playlist& playlist)
{
    sqlite3_stmt* stmt;
    const char* query = "UPDATE Playlists SET name=?1 WHERE id=?2";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, playlist.name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, playlist.id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void DBConn::update_playlist_art(PlaylistID playlist_id, Art& art)
{
    sqlite3_stmt* stmt;
    const char* query = "UPDATE Playlists SET cover=?1 WHERE id=?2";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_blob(stmt, 1, art.data, art.size, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, playlist_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void DBConn::update_album_songs_year(AlbumID album_id, int year)
{
    sqlite3_stmt* stmt;
    const char* query = "UPDATE Songs SET year=?1 WHERE id IN (SELECT song_id FROM AlbumSong WHERE album_id=?2);";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, year);
    sqlite3_bind_int(stmt, 2, album_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void DBConn::update_album_songs_art(AlbumID album_id)
{
    sqlite3_stmt* stmt;
    const char* query = R"(
        UPDATE Songs
        SET cover=(SELECT cover FROM Albums WHERE id=?1)
        WHERE id IN (SELECT song_id FROM AlbumSong WHERE album_id=?1);
    )";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, album_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void DBConn::create_playlist_song(PlaylistID playlist_id, SongID song_id, int track)
{
    sqlite3_stmt* stmt;
    const char* query = "INSERT INTO PlaylistSong (playlist_id, song_id, track) VALUES (?1, ?2, ?3)";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, playlist_id);
    sqlite3_bind_int(stmt, 2, song_id);
    sqlite3_bind_int(stmt, 3, track);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

bool DBConn::artist_album_exists(ArtistID artist_id, AlbumID album_id)
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT album_id FROM ArtistAlbum WHERE artist_id=?1 AND album_id=?2";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, artist_id);
    sqlite3_bind_int(stmt, 2, album_id);
    int res = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return res == SQLITE_ROW;
}

SongID DBConn::get_random_song()
{
    SongID song_id = INVALID_ID;
    sqlite3_stmt* stmt;
    const char* query = "SELECT id FROM Songs ORDER BY random() LIMIT 1";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL);
    int res = sqlite3_step(stmt);
    if (res == SQLITE_ROW)
        song_id = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return song_id;
}

void DBConn::get_song_lyrics(SongID song_id, char* lyrics_buffer, size_t lyrics_buffer_size)
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT lyrics FROM Songs WHERE id=?1";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, song_id);
    [[maybe_unused]] int res = sqlite3_step(stmt);
    assert(res == SQLITE_ROW);
    const char* lyrics = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    snprintf(lyrics_buffer, lyrics_buffer_size, "%s", (lyrics) ? lyrics : "");
    sqlite3_finalize(stmt);
}

int DBConn::get_num_songs()
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT COUNT(id) FROM Songs";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL);
    sqlite3_step(stmt);
    int num_songs = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return num_songs;
}

int DBConn::get_num_albums()
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT COUNT(id) FROM Albums";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL);
    sqlite3_step(stmt);
    int num_albums = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return num_albums;
}

int DBConn::get_num_artists()
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT COUNT(id) FROM Artists";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL);
    sqlite3_step(stmt);
    int num_artists = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return num_artists;
}

int DBConn::get_num_playlists()
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT COUNT(id) FROM Playlists";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL);
    sqlite3_step(stmt);
    int num_playlists = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return num_playlists;
}

void DBConn::create_artist_album(ArtistID artist_id, AlbumID album_id)
{
    sqlite3_stmt* stmt;
    const char* query = "INSERT INTO ArtistAlbum (artist_id, album_id) VALUES (?1, ?2)";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, artist_id);
    sqlite3_bind_int(stmt, 2, album_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::vector<PinID> DBConn::get_pins()
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT uid, last_accessed FROM Pins ORDER BY last_accessed DESC";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL);
    std::vector<PinID> pin_ids{};
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        MPUID uid = sqlite3_column_int64(stmt, 0);
        pin_ids.emplace_back(uid, sqlite3_column_int64(stmt, 1));
    }
    sqlite3_finalize(stmt);
    return pin_ids;
}

bool DBConn::create_pin(MPUID uid)
{
    sqlite3_stmt* stmt;
    const char* query = "INSERT INTO Pins (uid, last_accessed) VALUES (?1, ?2)";
    sqlite3_prepare_v2(this->conn, query, -1, &stmt, NULL);
    sqlite3_bind_int64(stmt, 1, uid);
    sqlite3_bind_int64(stmt, 2, std::chrono::steady_clock::now().time_since_epoch().count());
    int res = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return res == SQLITE_DONE;
}
